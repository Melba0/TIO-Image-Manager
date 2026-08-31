#include "BuiltinMacros.h"
#include "../src/executor/Context.h"
#include "../src/parser/AST.h"

namespace {

using ExprPtr = std::unique_ptr<Expr>;

ExprPtr id(const std::string& n) { return std::make_unique<IdentExpr>(n); }
ExprPtr num(double v) { return std::make_unique<NumberExpr>(v); }
ExprPtr prop(ExprPtr o, const std::string& p) {
    return std::make_unique<PropertyAccessExpr>(std::move(o), p);
}
ExprPtr bin(ExprPtr l, BinOp op, ExprPtr r) {
    return std::make_unique<BinaryExpr>(std::move(l), op, std::move(r));
}
ExprPtr call(const std::string& n, std::vector<ExprPtr> args) {
    return std::make_unique<MacroCallExpr>(n, std::move(args));
}

// unique_ptr -> shared_ptr (MacroDef.body is shared so the AST statement can be re-evaluated)
std::shared_ptr<Expr> sp(ExprPtr u) { return std::shared_ptr<Expr>(std::move(u)); }

void reg(Context& ctx, const std::string& name, std::vector<std::string> params, ExprPtr body) {
    ctx.registerMacro(name, MacroDef::ast(std::move(params), sp(std::move(body))));
}

// object property access: x.<p>
ExprPtr op(const std::string& x, const std::string& p) { return prop(id(x), p); }
// nested object property: x.attr.<p>
ExprPtr oattr(const std::string& x, const std::string& p) { return prop(prop(id(x), "attr"), p); }

}  // namespace

void registerBuiltinMacros(Context& ctx) {
    // ---- built-in math functions (mapped to std::) ----
    ctx.registerMacro("max", MacroDef::math(MathFn::Max, 2));
    ctx.registerMacro("min", MacroDef::math(MathFn::Min, 2));
    ctx.registerMacro("abs", MacroDef::math(MathFn::Abs, 1));
    ctx.registerMacro("sqrt", MacroDef::math(MathFn::Sqrt, 1));
    ctx.registerMacro("pow", MacroDef::math(MathFn::Pow, 2));
    ctx.registerMacro("log", MacroDef::math(MathFn::Log, 1));
    ctx.registerMacro("exp", MacroDef::math(MathFn::Exp, 1));

    // ---- built-in color macros (computed from obj.attr) ----
    ctx.registerMacro("color", MacroDef::color(ColorFn::Color, 2));        // color(obj,"blue")
    ctx.registerMacro("cct", MacroDef::color(ColorFn::Cct, 1));
    ctx.registerMacro("warmth", MacroDef::color(ColorFn::Warmth, 1));
    ctx.registerMacro("coolness", MacroDef::color(ColorFn::Coolness, 1));
    ctx.registerMacro("brightness", MacroDef::color(ColorFn::Brightness, 1));
    ctx.registerMacro("saturation", MacroDef::color(ColorFn::Saturation, 1));

    // ---- built-in IMAGE-level color macros (read the current image's ImageAttrs) ----
    ctx.registerMacro("img_temp", MacroDef::color(ColorFn::ImgTemp, 0));
    ctx.registerMacro("img_warmth", MacroDef::color(ColorFn::ImgWarmth, 0));
    ctx.registerMacro("img_coolness", MacroDef::color(ColorFn::ImgCoolness, 0));
    ctx.registerMacro("img_color", MacroDef::color(ColorFn::ImgColor, 1));  // img_color("blue")
    ctx.registerMacro("img_bright", MacroDef::color(ColorFn::ImgBright, 0));
    ctx.registerMacro("img_colorful", MacroDef::color(ColorFn::ImgColorful, 0));

    // ---- built-in hue-histogram macros (expose the cached 32-dim hue_hist) ----
    ctx.registerMacro("obj_hist", MacroDef::hist(HistFn::ObjHist, 1));       // obj_hist(obj)
    ctx.registerMacro("img_hist", MacroDef::hist(HistFn::ImgHist, 0));       // img_hist()
    ctx.registerMacro("hist_sim", MacroDef::hist(HistFn::HistSim, 2));       // hist_sim(A, B)
    ctx.registerMacro("hist_value", MacroDef::hist(HistFn::HistValue, 2));   // hist_value(obj, idx)
    ctx.registerMacro("img_hist_value", MacroDef::hist(HistFn::ImgHistValue, 1)); // img_hist_value(idx)

    // ---- image quality (exposure / sharpness), EXIF, user tags ----
    ctx.registerMacro("img_over", MacroDef::img(ImgFn::OverExposure, 0));       // img_over()
    ctx.registerMacro("img_under", MacroDef::img(ImgFn::UnderExposure, 0));     // img_under()
    ctx.registerMacro("img_exp_good", MacroDef::img(ImgFn::ExposureGood, 0));   // img_exp_good()
    ctx.registerMacro("img_hist_val", MacroDef::img(ImgFn::LumaHistVal, 1));    // img_hist_val(idx)
    ctx.registerMacro("img_blur", MacroDef::img(ImgFn::GlobalBlur, 0));         // img_blur()
    ctx.registerMacro("img_blurry", MacroDef::img(ImgFn::GlobalBlurry, 0));     // img_blurry()
    ctx.registerMacro("obj_blur", MacroDef::img(ImgFn::LocalBlur, 1));          // obj_blur(obj)
    ctx.registerMacro("img_camera", MacroDef::img(ImgFn::Camera, 0));           // img_camera()
    ctx.registerMacro("img_iso", MacroDef::img(ImgFn::Iso, 0));                 // img_iso()
    ctx.registerMacro("img_shutter", MacroDef::img(ImgFn::Shutter, 0));         // img_shutter()
    ctx.registerMacro("img_aperture", MacroDef::img(ImgFn::Aperture, 0));       // img_aperture()
    ctx.registerMacro("img_fl", MacroDef::img(ImgFn::FocalLength, 0));          // img_fl()
    ctx.registerMacro("img_date", MacroDef::img(ImgFn::Date, 0));               // img_date()
    ctx.registerMacro("img_tag", MacroDef::img(ImgFn::Tag, 1));                 // img_tag(key)
    ctx.registerMacro("img_has_tag", MacroDef::img(ImgFn::HasTag, 1));          // img_has_tag(key)
    ctx.registerMacro("img_tag_equals", MacroDef::img(ImgFn::TagEquals, 2));    // img_tag_equals(k, v)
    ctx.registerMacro("stof", MacroDef::img(ImgFn::Stof, 1));                   // stof(s)
    ctx.registerMacro("str_contains", MacroDef::img(ImgFn::StrContains, 2));    // str_contains(s, sub)

    // ---- built-in Places365 scene-recognition macros (cached ImageAttrs) ----
    ctx.registerMacro("img_scene", MacroDef::scene(SceneFn::SceneProb, 1));     // img_scene("beach")
    ctx.registerMacro("img_scene_top", MacroDef::scene(SceneFn::SceneTop, 0));  // img_scene_top()
    ctx.registerMacro("img_scene_vec", MacroDef::scene(SceneFn::SceneVec, 0));  // img_scene_vec() (internal)
    ctx.registerMacro("img_is_indoor", MacroDef::scene(SceneFn::IsIndoor, 0));  // img_is_indoor()

    // ---- built-in clustering macros (cached object cluster_ids) ----
    ctx.registerMacro("cluster_id", MacroDef::cluster(ClusterFn::ClusterId, 2));   // cluster_id(obj, name)
    ctx.registerMacro("cluster_sim", MacroDef::cluster(ClusterFn::ClusterSim, 3)); // cluster_sim(a, b, name)

    // ---- spatial / geometric macros ----
    reg(ctx, "big", {"x"}, bin(op("x", "area"), BinOp::GT, num(0.2)));
    reg(ctx, "small", {"x"}, bin(op("x", "area"), BinOp::LT, num(0.05)));
    reg(ctx, "left", {"x"},
        bin(bin(op("x", "x"), BinOp::Add, bin(op("x", "w"), BinOp::Div, num(2))),
            BinOp::LT, num(0.33)));
    reg(ctx, "right", {"x"},
        bin(bin(op("x", "x"), BinOp::Add, bin(op("x", "w"), BinOp::Div, num(2))),
            BinOp::GT, num(0.67)));
    reg(ctx, "top", {"x"},
        bin(bin(op("x", "y"), BinOp::Add, bin(op("x", "h"), BinOp::Div, num(2))),
            BinOp::LT, num(0.33)));
    reg(ctx, "bottom", {"x"},
        bin(bin(op("x", "y"), BinOp::Add, bin(op("x", "h"), BinOp::Div, num(2))),
            BinOp::GT, num(0.67)));
    // square(x) = abs(x.w / x.h - 1.0) < 0.1
    {
        std::vector<ExprPtr> abs_args;
        abs_args.push_back(bin(bin(op("x", "w"), BinOp::Div, op("x", "h")), BinOp::Sub, num(1.0)));
        reg(ctx, "square", {"x"},
            bin(call("abs", std::move(abs_args)), BinOp::LT, num(0.1)));
    }

    // ---- relational macros (single-object form of the spec formulas) ----
    // left_of(a,b): a.x + a.w < b.x
    reg(ctx, "left_of", {"a", "b"},
        bin(bin(op("a", "x"), BinOp::Add, op("a", "w")), BinOp::LT, op("b", "x")));
    // above(a,b): a.y + a.h < b.y
    reg(ctx, "above", {"a", "b"},
        bin(bin(op("a", "y"), BinOp::Add, op("a", "h")), BinOp::LT, op("b", "y")));
    // inside(a,b): a.x>b.x && a.x+a.w<b.x+b.w && a.y>b.y && a.y+a.h<b.y+b.h
    reg(ctx, "inside", {"a", "b"},
        bin(bin(bin(op("a", "x"), BinOp::GT, op("b", "x")),
                BinOp::And,
                bin(bin(op("a", "x"), BinOp::Add, op("a", "w")), BinOp::LT,
                    bin(op("b", "x"), BinOp::Add, op("b", "w")))),
            BinOp::And,
            bin(bin(op("a", "y"), BinOp::GT, op("b", "y")),
                BinOp::And,
                bin(bin(op("a", "y"), BinOp::Add, op("a", "h")), BinOp::LT,
                    bin(op("b", "y"), BinOp::Add, op("b", "h"))))));

    // ---- atmosphere macros (rely on DetectedObject.attr computed in preprocessing) ----
    // warm(obj): obj.attr.h > 5 && obj.attr.h < 45
    reg(ctx, "warm", {"obj"},
        bin(bin(oattr("obj", "h"), BinOp::GT, num(5)), BinOp::And,
            bin(oattr("obj", "h"), BinOp::LT, num(45))));
    // cool(obj): obj.attr.h > 180 && obj.attr.h < 260
    reg(ctx, "cool", {"obj"},
        bin(bin(oattr("obj", "h"), BinOp::GT, num(180)), BinOp::And,
            bin(oattr("obj", "h"), BinOp::LT, num(260))));
    // bright(obj): obj.attr.v > 0.75
    reg(ctx, "bright", {"obj"}, bin(oattr("obj", "v"), BinOp::GT, num(0.75)));
    // dark(obj): obj.attr.v < 0.25
    reg(ctx, "dark", {"obj"}, bin(oattr("obj", "v"), BinOp::LT, num(0.25)));
    // smooth(obj): obj.attr.lbp < 0.2
    reg(ctx, "smooth", {"obj"}, bin(oattr("obj", "lbp"), BinOp::LT, num(0.2)));
    // rough(obj): obj.attr.lbp > 0.6
    reg(ctx, "rough", {"obj"}, bin(oattr("obj", "lbp"), BinOp::GT, num(0.6)));
}