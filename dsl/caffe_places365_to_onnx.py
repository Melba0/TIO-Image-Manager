"""Convert the Places365 GoogLeNet Caffe model (caffemodel + deploy prototxt)
into an ONNX model the engine can run.

Requires: pip install protobuf onnx
Usage:
    python caffe_places365_to_onnx.py <model.caffemodel> <deploy.prototxt> <output.onnx>

The deploy prototxt lists the layers in topological order.  We rebuild the same
graph in ONNX using the weights stored in the caffemodel's blobs.
"""
import sys
import struct

import onnx
from onnx import helper, TensorProto, numpy_helper
import numpy as np

# ---- parse the caffemodel protobuf by hand (no caffe import needed) ----

class Field:
    __slots__ = ("num", "wire", "data", "i")

    def __init__(self, num, wire, data, i):
        self.num = num
        self.wire = wire
        self.data = data
        self.i = i


def read_varint(buf, i):
    result = 0
    shift = 0
    while True:
        b = buf[i]
        i += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
    return result, i


def read_fields(buf):
    i = 0
    fields = []
    while i < len(buf):
        key, i = read_varint(buf, i)
        num = key >> 3
        wire = key & 7
        if wire == 0:
            val, i = read_varint(buf, i)
            fields.append(Field(num, wire, val, i))
        elif wire == 1:
            val = struct.unpack("<d", buf[i:i+8])[0]
            i += 8
            fields.append(Field(num, wire, val, i))
        elif wire == 2:
            ln, i = read_varint(buf, i)
            data = buf[i:i+ln]
            i += ln
            fields.append(Field(num, wire, data, i))
        elif wire == 5:
            val = struct.unpack("<f", buf[i:i+4])[0]
            i += 4
            fields.append(Field(num, wire, val, i))
        else:
            raise ValueError("unexpected wire type %d" % wire)
    return fields


def find(fields, num):
    return [f for f in fields if f.num == num]


def unpack_repeated(f, dtype):
    if f.wire == 2:
        return list(struct.unpack("<" + str(len(f.data)//4) + "f", f.data)) if dtype == "f" else \
               list(struct.unpack("<" + str(len(f.data)//8) + "d", f.data)) if dtype == "d" else \
               list(read_varint_list(f.data))
    return [f.data]


def read_varint_list(buf):
    out = []
    i = 0
    while i < len(buf):
        v, i = read_varint(buf, i)
        out.append(v)
    return out


def parse_blob(f):
    shape = None
    data = []
    for sf in read_fields(f.data):
        if sf.num == 5:   # data (packed floats)
            data = unpack_repeated(sf, "f")
        elif sf.num == 7:  # shape
            for sf2 in read_fields(sf.data):
                if sf2.num == 1:
                    dims = read_varint_list(sf2.data) if sf2.wire == 2 else [sf2.data]
                    shape = dims
    return shape, data


class Layer:
    def __init__(self, name, ltype, bottom, top, blobs, params):
        self.name = name
        self.type = ltype
        self.bottom = bottom
        self.top = top
        self.blobs = blobs  # list of (shape, data)
        self.params = params


def parse_net(caffemodel_path, prototxt_path):
    buf = open(caffemodel_path, "rb").read()
    net = read_fields(buf)

    # ---- layers from caffemodel (field 100: layer) ----
    caffe_layers = {}
    for f in find(net, 100):
        lf = read_fields(f.data)
        name = top = bottom = ltype = ""
        for x in lf:
            if x.num == 1 and x.wire == 2:
                name = x.data.decode("utf-8", "replace")
            elif x.num == 2 and x.wire == 2:
                ltype = x.data.decode("utf-8", "replace")
            elif x.num == 3 and x.wire == 2:
                bottom = x.data.decode("utf-8", "replace")
            elif x.num == 4 and x.wire == 2:
                top = x.data.decode("utf-8", "replace")
        blobs = []
        for bf in find(lf, 7):
            blobs.append(parse_blob(bf))
        caffe_layers[name] = (ltype, bottom, top, blobs)

    # ---- layer ORDER + connection + conv params from deploy prototxt ----
    import re
    proto = open(prototxt_path, encoding="utf-8").read()
    # crude prototxt block parser
    blocks = []
    depth = 0
    cur = []
    start = 0
    for i, ch in enumerate(proto):
        if ch == "{":
            if depth == 0:
                start = i
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                blocks.append(proto[start:i+1])
    # top-level "layer { ... }" blocks + their text bodies
    layer_blocks = []
    i = 0
    while True:
        j = proto.find("layer {", i)
        if j < 0:
            break
        # find matching close
        d = 0
        k = j
        while k < len(proto):
            if proto[k] == "{":
                d += 1
            elif proto[k] == "}":
                d -= 1
                if d == 0:
                    break
            k += 1
        layer_blocks.append(proto[j:k+1])
        i = k + 1

    def block_text(body, key):
        # value for a top-level key in a { } block (prototxt allows "key: X" and "key {")
        import re
        m = re.search(r"\b%s\s*(?::\s*)?" % re.escape(key), body)
        if not m:
            return None
        i = m.end()
        while i < len(body) and body[i] in " \t":
            i += 1
        if i >= len(body):
            return None
        if body[i] == "{":
            # balanced braces
            d = 0
            j = i
            while j < len(body):
                if body[j] == "{":
                    d += 1
                elif body[j] == "}":
                    d -= 1
                    if d == 0:
                        j += 1
                        break
                j += 1
            return body[i:j]
        # single token value
        j = i
        while j < len(body) and body[j] not in "\n}":
            j += 1
        return body[i:j].strip()

    def parse_params(body):
        params = {}
        for key in ("convolution_param", "inner_product_param", "pooling_param",
                    "lrn_param", "concat_param", "relu_param", "dropout_param",
                    "softmax_param"):
            v = block_text(body, key)
            if v and v.startswith("{"):
                params[key] = v
        return params

    layers = []
    for blk in layer_blocks:
        inner = blk[len("layer {"):-1]
        name = (block_text(inner, "name") or "").strip().strip('"')
        ltype = (block_text(inner, "type") or "").strip().strip('"')
        bottom = [x.strip().strip('"') for x in re.findall(r"\bbottom\s*:\s*\"([^\"]*)\"", inner)]
        top = [x.strip().strip('"') for x in re.findall(r"\btop\s*:\s*\"([^\"]*)\"", inner)]
        if name in caffe_layers:
            ct, cb, ct2, blobs = caffe_layers[name]
            if not bottom:
                bottom = [cb] if cb else []
            if not top:
                top = [ct2] if ct2 else []
        else:
            blobs = []
        layers.append(Layer(name, ltype, bottom, top, blobs, parse_params(inner)))

    return layers


# ---- ONNX builders ----

def gv(name):
    return helper.make_tensor_value_info(name, TensorProto.FLOAT, None)


def ints(s, key, default=None):
    import re
    if not s:
        return default
    m = re.search(r"\b%s\s*:\s*([^\n}]+)" % key, s)
    if not m:
        return default
    return [int(x) for x in re.findall(r"\d+", m.group(1))]


def val(s, key, default=None):
    import re
    if not s:
        return default
    m = re.search(r"\b%s\s*:\s*([^\n}]+)" % key, s)
    if not m:
        return default
    t = m.group(1).strip()
    try:
        return float(t)
    except ValueError:
        return default


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    cmodel, prototxt, out = sys.argv[1], sys.argv[2], sys.argv[3]

    layers = parse_net(cmodel, prototxt)
    print("parsed", len(layers), "layers")

    nodes = []
    init = []
    tensor_of = {"data": "data"}   # caffe blob name -> onnx tensor name

    import re
    for L in layers:
        n = L.name
        bottom = L.bottom
        top = L.top[0] if L.top else n
        t = L.type.lower()

        if t == "input":
            continue

        # Each caffe blob may be written by several layers in-place (e.g. ReLU
        # with top == bottom).  ONNX is SSA, so mint a unique tensor per write
        # (caffe layer names are unique) and track the caffe blob -> tensor map.
        out_tensor = n

        if t == "convolution":
            cp = L.params.get("convolution_param", "")
            num_out = ints(cp, "num_output")[0]
            pad = ints(cp, "pad", [0])[0]
            ksize = ints(cp, "kernel_size", [1])[0]
            stride = ints(cp, "stride", [1])[0]
            group = ints(cp, "group", [1])[0]
            if group == 0:
                group = 1
            shape_w, w = L.blobs[0]
            oc, ic, kh, kw = shape_w
            wt = np.asarray(w, dtype=np.float32).reshape(oc, ic, kh, kw)
            if group > 1:
                wt = wt.reshape(group, oc // group, ic // group, kh, kw)
            wn = n + "_W"
            init.append(numpy_helper.from_array(wt, wn))
            inputs = [tensor_of[bottom[0]], wn]
            if len(L.blobs) > 1:
                shape_b, b = L.blobs[1]
                bt = np.asarray(b, dtype=np.float32).reshape(-1)
                bn = n + "_B"
                init.append(numpy_helper.from_array(bt, bn))
                inputs.append(bn)
            attrs = {
                "kernel_shape": [kh, kw],
                "strides": [stride, stride],
                "pads": [pad, pad, pad, pad],
            }
            if group > 1:
                attrs["group"] = group
            nodes.append(helper.make_node("Conv", inputs, [out_tensor], name=n, **attrs))

        elif t == "relu":
            nodes.append(helper.make_node("Relu", [tensor_of[bottom[0]]], [out_tensor], name=n))

        elif t == "pooling":
            pp = L.params.get("pooling_param", "")
            pool = "MAX" if "AVE" not in pp else "AVE"
            pad = ints(pp, "pad", [0])[0]
            ksize = ints(pp, "kernel_size", [1])[0]
            stride = ints(pp, "stride", [1])[0]
            op = "MaxPool" if pool == "MAX" else "AveragePool"
            # Caffe GoogLeNet pools use ceil_mode = true so 224 -> 7x7 at
            # pool4; ONNX MaxPool/AveragePool need explicit ceil_mode.
            attrs = {"kernel_shape": [ksize, ksize],
                     "strides": [stride, stride],
                     "pads": [pad, pad, pad, pad],
                     "ceil_mode": 1}
            nodes.append(helper.make_node(op, [tensor_of[bottom[0]]], [out_tensor], name=n, **attrs))

        elif t == "lrn":
            lp = L.params.get("lrn_param", "")
            k = val(lp, "k", 1.0)
            alpha = val(lp, "alpha", 0.0001)
            beta = val(lp, "beta", 0.75)
            size = ints(lp, "local_size", [5])[0]
            nodes.append(helper.make_node("LRN", [tensor_of[bottom[0]]], [out_tensor], name=n,
                                          size=size, alpha=alpha, beta=beta, bias=k))

        elif t == "concat":
            cp = L.params.get("concat_param", "")
            axis = ints(cp, "axis", [1])[0]
            inputs = [tensor_of[b] for b in bottom]
            nodes.append(helper.make_node("Concat", inputs, [out_tensor], name=n, axis=axis))

        elif t == "innerproduct":
            ip = L.params.get("inner_product_param", "")
            shape_w, w = L.blobs[0]
            oc, ic = shape_w
            wt = np.asarray(w, dtype=np.float32).reshape(oc, ic)
            wn = n + "_W"
            init.append(numpy_helper.from_array(wt, wn))
            # caffe fc flattens the spatial dims -> Gemm needs rank-2 input.
            flat = n + "_flat"
            nodes.append(helper.make_node("Flatten", [tensor_of[bottom[0]]], [flat], name=n + "_flat",
                                          axis=1))
            inputs = [flat, wn]
            if len(L.blobs) > 1:
                shape_b, b = L.blobs[1]
                bt = np.asarray(b, dtype=np.float32).reshape(-1)
                bn = n + "_B"
                init.append(numpy_helper.from_array(bt, bn))
                inputs.append(bn)
            nodes.append(helper.make_node("Gemm", inputs, [out_tensor], name=n,
                                          alpha=1.0, beta=1.0, transB=1))

        elif t == "dropout":
            nodes.append(helper.make_node("Identity", [tensor_of[bottom[0]]], [out_tensor], name=n))

        elif t == "softmax":
            sp = L.params.get("softmax_param", "")
            axis = ints(sp, "axis", [1])[0]
            nodes.append(helper.make_node("Softmax", [tensor_of[bottom[0]]], [out_tensor], name=n, axis=axis))

        else:
            print("WARN: unknown layer type", t, "at", n)

        # record this blob's latest tensor (a layer may write several tops;
        # in-place ops write their bottom blob too)
        for b in (L.top if L.top else [top]):
            tensor_of[b] = out_tensor

    # We want the 365 logits, not the final softmax.  The deploy prototxt ends
    # with loss3/classifier -> prob (softmax); output the classifier tensor.
    out_tensor_name = tensor_of.get("loss3/classifier", tensor_of.get("prob", "loss3/classifier"))
    # input dims from the prototxt "input_dim:" lines (batch, ch, h, w).
    # The engine runs batch=1, so force batch to 1.
    import re as _re
    proto_text = open(prototxt, encoding="utf-8").read()
    dims = [int(x) for x in _re.findall(r"\binput_dim\s*:\s*(\d+)", proto_text)]
    if len(dims) != 4:
        dims = [1, 3, 224, 224]
    dims[0] = 1
    graph_input = helper.make_tensor_value_info("data", TensorProto.FLOAT, dims)
    graph_output = helper.make_tensor_value_info(out_tensor_name, TensorProto.FLOAT, [dims[0], 365])

    graph = helper.make_graph(nodes, "places365_googlenet", [graph_input], [graph_output], init)
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
    onnx.checker.check_model(model)
    onnx.save(model, out)
    print("saved:", out)


if __name__ == "__main__":
    main()
