#include "Evaluator.h"
#include <stdexcept>
#include <iostream>
#include <unordered_set>
#include <algorithm>
#include <cmath>

namespace {
bool isCoordProperty(const Expr* e);
}

Evaluator::Evaluator(Context& ctx) : ctx_(ctx) {}

Value Evaluator::evaluate(const ASTNode& node) {
    if (auto* stmt = dynamic_cast<const AssignStmt*>(&node)) {
        auto val = evaluateExpr(*stmt->value);
        ctx_.setVariable(stmt->name, val);
        return val;
    }
    if (auto* expr_stmt = dynamic_cast<const ExprStmt*>(&node)) {
        return evaluateExpr(*expr_stmt->expr);
    }
    if (auto* del_stmt = dynamic_cast<const DelStmt*>(&node)) {
        return evalDel(*del_stmt);
    }
    if (auto* macro_def = dynamic_cast<const MacroDefStmt*>(&node)) {
        MacroDef def;
        def.params = macro_def->params;
        def.body = macro_def->body;
        ctx_.registerMacro(macro_def->name, std::move(def));
        return Value::makeNone();
    }
    if (auto* program = dynamic_cast<const Program*>(&node)) {
        Value last;
        for (const auto& stmt : program->statements) {
            last = evaluate(*stmt);
        }
        return last;
    }
    throw std::runtime_error("Unknown AST node type");
}

Value Evaluator::evaluateExpr(const Expr& expr) {
    if (auto* n = dynamic_cast<const NumberExpr*>(&expr)) return evalNumber(*n);
    if (auto* s = dynamic_cast<const StringExpr*>(&expr)) return evalString(*s);
    if (auto* id = dynamic_cast<const IdentExpr*>(&expr)) return evalIdent(*id);
    if (auto* d = dynamic_cast<const DollarExpr*>(&expr)) return evalDollar(*d);
    if (auto* b = dynamic_cast<const BinaryExpr*>(&expr)) return evalBinary(*b);
    if (auto* u = dynamic_cast<const UnaryExpr*>(&expr)) return evalUnary(*u);
    if (auto* c = dynamic_cast<const CaretExpr*>(&expr)) return evalCaret(*c);
    if (auto* q = dynamic_cast<const QuantifierExpr*>(&expr)) return evalQuantifier(*q);
    if (auto* f = dynamic_cast<const FilterExpr*>(&expr)) return evalFilter(*f);
    if (auto* aa = dynamic_cast<const AnyAllExpr*>(&expr)) return evalAnyAll(*aa);
    if (auto* e = dynamic_cast<const ExpandExpr*>(&expr)) return evalExpand(*e);
    if (auto* cnt = dynamic_cast<const CntExpr*>(&expr)) return evalCnt(*cnt);
    if (auto* mc = dynamic_cast<const MacroCallExpr*>(&expr)) return evalMacroCall(*mc);
    if (auto* pa = dynamic_cast<const PropertyAccessExpr*>(&expr)) return evalPropertyAccess(*pa);
    if (auto* co = dynamic_cast<const CurrentObjectExpr*>(&expr)) return evalCurrentObject(*co);
    if (auto* cio = dynamic_cast<const CurrentImageObjectsExpr*>(&expr)) return evalCurrentImageObjects(*cio);
    throw std::runtime_error("Unknown expression type");
}

Value Evaluator::evalNumber(const NumberExpr& node) {
    return Value::makeNum(node.value);
}

Value Evaluator::evalString(const StringExpr& node) {
    return Value::makeString(node.value);
}

Value Evaluator::evalIdent(const IdentExpr& node) {
    static const std::unordered_set<std::string> properties = {
        "class", "x", "y", "w", "h", "area", "confidence",
        "super_class", "original_class", "attr"
    };

    // 1) macro parameter bindings (innermost scope first)
    if (const Value* p = ctx_.findMacroParam(node.name)) {
        return *p;
    }

    // 2) property of the current object
    auto* obj = ctx_.getCurrentObject();
    if (obj && properties.count(node.name)) {
        return getProperty(node.name);
    }

    // 3) session variable
    if (ctx_.hasVariable(node.name)) {
        return ctx_.getVariable(node.name);
    }

    // 4) bare macro call: broadcast the current object into the first parameter
    if (const MacroDef* m = ctx_.getMacro(node.name)) {
        if (m->is_math) {
            throw std::runtime_error("Math macro '" + node.name + "' requires explicit arguments");
        }
        if (!m->body) {
            throw std::runtime_error("Macro '" + node.name + "' requires explicit arguments");
        }
        if (m->params.size() != 1) {
            throw std::runtime_error("Macro '" + node.name + "' requires " +
                                     std::to_string(m->params.size()) + " arguments");
        }
        if (!obj) {
            throw std::runtime_error("Cannot broadcast macro '" + node.name +
                                     "' without an object context");
        }
        ctx_.pushMacroScope();
        ctx_.bindMacroParam(m->params[0], Value::makeObject(*obj));
        Value result = evaluateExpr(*m->body);
        ctx_.popMacroScope();
        return result;
    }

    // 5) bare class name: existential class-match over the current objects
    //    (e.g. `cat` inside a quantifier condition means "there is a cat").
    if (ModelRegistry* reg = ctx_.getRegistry()) {
        if (reg->hasClass(node.name)) {
            return Value::makeScore(existentialClassMatch(node.name));
        }
    }

    throw std::runtime_error("Undefined identifier: " + node.name);
}

Value Evaluator::getProperty(const std::string& name) const {
    auto* obj = ctx_.getCurrentObject();
    if (!obj) {
        throw std::runtime_error("Cannot access property '" + name + "' outside of object context");
    }
    return getPropertyOf(Value::makeObject(*obj), name);
}

Value Evaluator::getPropertyOf(const Value& v, const std::string& prop) const {
    if (v.type == Value::OBJECT) {
        const DetectedObject& o = v.object;
        if (prop == "class") return Value::makeString(o.class_name);
        if (prop == "x") return Value::makeNum(o.x);
        if (prop == "y") return Value::makeNum(o.y);
        if (prop == "w") return Value::makeNum(o.w);
        if (prop == "h") return Value::makeNum(o.h);
        if (prop == "area") return Value::makeNum(o.area);
        if (prop == "confidence") return Value::makeNum(o.confidence);
        if (prop == "super_class") return Value::makeString(o.super_class);
        if (prop == "original_class") return Value::makeString(o.original_class);
        if (prop == "attr") return Value::makeAttr(o.attr);
        throw std::runtime_error("Unknown property: " + prop);
    }
    if (v.type == Value::ATTR) {
        if (prop == "h") return Value::makeNum(v.attr.h);
        if (prop == "s") return Value::makeNum(v.attr.s);
        if (prop == "v") return Value::makeNum(v.attr.v);
        if (prop == "h_std") return Value::makeNum(v.attr.h_std);
        if (prop == "s_std") return Value::makeNum(v.attr.s_std);
        if (prop == "v_std") return Value::makeNum(v.attr.v_std);
        if (prop == "color_temperature") return Value::makeNum(v.attr.color_temperature);
        if (prop == "dominant_color_name") return Value::makeString(v.attr.dominant_color_name);
        if (prop == "lbp") return Value::makeNum(v.attr.lbp);
        throw std::runtime_error("Unknown attr field: " + prop);
    }
    throw std::runtime_error("Property access '" + prop + "' on a non-object value");
}

Value Evaluator::evalDollar(const DollarExpr&) {
    // If inside a quantifier iteration, return the current image as singleton set
    if (!ctx_.getCurrentImage().empty()) {
        return Value::makeImageSet({ctx_.getCurrentImage()});
    }
    // Otherwise return all images (neutral score 1.0)
    return Value::makeImageSet(ctx_.getAllImagePaths());
}

Value Evaluator::evalCaret(const CaretExpr& node) {
    auto val = evaluateExpr(*node.operand);
    if (val.type != Value::OBJECT_SET) {
        throw std::runtime_error("Caret (^) expects ObjectSet, got " + std::to_string(val.type));
    }
    // Aggregate per-image scores: take the max object score per image.
    std::unordered_map<std::string, float> scores;
    for (const auto& obj : val.object_set) {
        float s = obj.score > 0 ? obj.score : (float)obj.confidence;
        auto it = scores.find(obj.image_path);
        if (it == scores.end() || s > it->second) scores[obj.image_path] = s;
    }
    return Value::makeImageSet(scores);
}

Value Evaluator::evalUnary(const UnaryExpr& node) {
    auto operand = evaluateExpr(*node.operand);

    if (node.op == UnaryOp::Not) {
        // fuzzy negation: 1 - score
        return Value::makeScore(1.0f - scoreOf(operand));
    }

    if (node.op == UnaryOp::Negate) {
        if (operand.type != Value::NUM) {
            throw std::runtime_error("Unary minus (-) expects a Num");
        }
        return Value::makeNum(-operand.num_val);
    }

    if (node.op == UnaryOp::Percent) {
        if (operand.type != Value::IMAGE_SET) {
            throw std::runtime_error("Percent (%) expects ImageSet, got type " + std::to_string(operand.type));
        }
        // Extract all objects from the images in the set; each object's base
        // score is its YOLO confidence.
        std::vector<DetectedObject> objects;
        for (const auto& img_path : operand.image_set) {
            auto* dets = ctx_.getObjects(img_path);
            if (dets) {
                for (const auto& d : *dets) {
                    DetectedObject det = d;
                    det.image_path = img_path;
                    det.score = (float)det.confidence;
                    objects.push_back(det);
                }
            }
        }
        return Value::makeObjectSet(objects);
    }

    throw std::runtime_error("Unknown unary operator");
}

Value Evaluator::evalBinary(const BinaryExpr& node) {
    // ---- Set operations (with fuzzy score aggregation) ----
    if (node.op == BinOp::SetUnion || node.op == BinOp::SetIntersect || node.op == BinOp::SetDiff) {
        auto left = evaluateExpr(*node.left);
        auto right = evaluateExpr(*node.right);

        // Polymorphic `&` / `|`: on fuzzy scores they act as logical AND / OR.
        bool is_score = (left.type == Value::SCORE || left.type == Value::BOOL) &&
                        (right.type == Value::SCORE || right.type == Value::BOOL);
        if (is_score) {
            float a = scoreOf(left), b = scoreOf(right);
            if (node.op == BinOp::SetUnion) return Value::makeScore(std::max(a, b));
            if (node.op == BinOp::SetIntersect) return Value::makeScore(std::min(a, b));
            throw std::runtime_error("Set difference (-) does not apply to score operands");
        }

        if (left.type != right.type) {
            throw std::runtime_error("Set operation on different types");
        }
        if (left.type == Value::IMAGE_SET) {
            std::unordered_map<std::string, float> result;
            if (node.op == BinOp::SetUnion) {
                result = left.image_scores;
                for (const auto& kv : right.image_scores) {
                    auto it = result.find(kv.first);
                    result[kv.first] = (it == result.end()) ? kv.second : std::max(it->second, kv.second);
                }
            } else if (node.op == BinOp::SetIntersect) {
                for (const auto& p : left.image_set) {
                    auto r = right.image_scores.find(p);
                    if (r != right.image_scores.end()) {
                        result[p] = std::min(left.image_scores[p], r->second);
                    }
                }
            } else { // SetDiff
                for (const auto& p : left.image_set) {
                    if (!right.image_set.count(p)) result[p] = left.image_scores[p];
                }
            }
            return Value::makeImageSet(result);
        }
        if (left.type == Value::OBJECT_SET) {
            std::vector<DetectedObject> result;
            auto& left_objs = left.object_set;
            auto& right_objs = right.object_set;

            if (node.op == BinOp::SetUnion) {
                result = left_objs;
                std::unordered_set<std::string> seen;
                for (const auto& o : left_objs) {
                    seen.insert(o.image_path + ":" + o.class_name + ":" +
                                std::to_string(o.x) + ":" + std::to_string(o.y));
                }
                for (const auto& o : right_objs) {
                    std::string key = o.image_path + ":" + o.class_name + ":" +
                                      std::to_string(o.x) + ":" + std::to_string(o.y);
                    if (!seen.count(key)) {
                        seen.insert(key);
                        result.push_back(o);
                    }
                }
            } else if (node.op == BinOp::SetIntersect) {
                std::unordered_set<std::string> right_keys;
                for (const auto& o : right_objs) {
                    right_keys.insert(o.image_path + ":" + o.class_name + ":" +
                                      std::to_string(o.x) + ":" + std::to_string(o.y));
                }
                for (const auto& o : left_objs) {
                    std::string key = o.image_path + ":" + o.class_name + ":" +
                                      std::to_string(o.x) + ":" + std::to_string(o.y);
                    if (right_keys.count(key)) result.push_back(o);
                }
            } else { // SetDiff
                std::unordered_set<std::string> right_keys;
                for (const auto& o : right_objs) {
                    right_keys.insert(o.image_path + ":" + o.class_name + ":" +
                                      std::to_string(o.x) + ":" + std::to_string(o.y));
                }
                for (const auto& o : left_objs) {
                    std::string key = o.image_path + ":" + o.class_name + ":" +
                                      std::to_string(o.x) + ":" + std::to_string(o.y);
                    if (!right_keys.count(key)) result.push_back(o);
                }
            }
            return Value::makeObjectSet(result);
        }
        throw std::runtime_error("Set operations require ImageSet or ObjectSet");
    }

    // ---- Fuzzy logical operators: && -> min, || -> max ----
    if (node.op == BinOp::And || node.op == BinOp::Or) {
        auto left = evaluateExpr(*node.left);
        auto right = evaluateExpr(*node.right);
        float a = scoreOf(left), b = scoreOf(right);
        return Value::makeScore(node.op == BinOp::And ? std::min(a, b) : std::max(a, b));
    }

    // ---- Arithmetic / comparison ----
    auto left = evaluateExpr(*node.left);
    auto right = evaluateExpr(*node.right);

    // String comparison (with class-match confidence weighting)
    if (left.type == Value::STRING && right.type == Value::STRING) {
        std::string kind = classPropKind(node.left.get());
        if (kind == "class" || kind == "super") {
            float s = 0.0f;
            if (const DetectedObject* obj = ctx_.getCurrentObject()) {
                const std::string& query = right.str_val;
                if (kind == "class") {
                    if (obj->class_name == query) s = (float)obj->confidence;
                    else if (obj->super_class == query) s = (float)(obj->confidence * 0.8);
                } else {  // super_class property
                    if (obj->super_class == query) s = (float)(obj->confidence * 0.8);
                }
            } else {
                s = (left.str_val == right.str_val) ? 1.0f : 0.0f;
            }
            return Value::makeScore(node.op == BinOp::NE ? 1.0f - s : s);
        }
        bool eq = (left.str_val == right.str_val);
        return Value::makeScore(node.op == BinOp::NE ? (eq ? 0.0f : 1.0f) : (eq ? 1.0f : 0.0f));
    }

    // Numeric operations / fuzzy comparisons.
    // SCORE and BOOL values are treated as numbers in comparisons
    // (e.g. `img_warmth() > 0.5` compares a fuzzy score against a literal).
    auto isNumeric = [](const Value& v) {
        return v.type == Value::NUM || v.type == Value::SCORE || v.type == Value::BOOL;
    };
    auto numOf = [](const Value& v) -> double {
        if (v.type == Value::NUM) return v.num_val;
        if (v.type == Value::SCORE) return v.score_val;
        if (v.type == Value::BOOL) return v.bool_val ? 1.0 : 0.0;
        return 0.0;
    };

    if (isNumeric(left) && isNumeric(right)) {
        double a = numOf(left), b = numOf(right);
        switch (node.op) {
            case BinOp::Add: return Value::makeNum(a + b);
            case BinOp::Sub: return Value::makeNum(a - b);
            case BinOp::Mul: return Value::makeNum(a * b);
            case BinOp::Div:
                if (b == 0) throw std::runtime_error("Division by zero");
                return Value::makeNum(a / b);
            case BinOp::EQ: {
                if (dynamic_cast<const CntExpr*>(node.left.get())) {
                    double denom = std::max(a, b);
                    float s = denom > 0 ? (float)(1.0 - std::abs(a - b) / denom) : 1.0f;
                    return Value::makeScore(clamp01(s));
                }
                double sigma = isCoordProperty(node.left.get()) ? 0.05 : 0.1;
                return Value::makeScore((float)std::exp(-(a - b) * (a - b) / (2 * sigma * sigma)));
            }
            case BinOp::NE: {
                if (dynamic_cast<const CntExpr*>(node.left.get())) {
                    double denom = std::max(a, b);
                    float s = denom > 0 ? (float)(1.0 - std::abs(a - b) / denom) : 1.0f;
                    return Value::makeScore(clamp01(1.0f - s));
                }
                double sigma = isCoordProperty(node.left.get()) ? 0.05 : 0.1;
                return Value::makeScore((float)(1.0 - std::exp(-(a - b) * (a - b) / (2 * sigma * sigma))));
            }
            case BinOp::GT:
                if (dynamic_cast<const CntExpr*>(node.left.get())) return Value::makeScore(sigmoid(a - 2.5));
                return Value::makeScore(sigmoid(a - b));
            case BinOp::LT:
                if (dynamic_cast<const CntExpr*>(node.left.get())) return Value::makeScore(sigmoid(2.5 - a));
                return Value::makeScore(sigmoid(b - a));
            case BinOp::GE: return Value::makeScore(sigmoid(a - b));
            case BinOp::LE: return Value::makeScore(sigmoid(b - a));
            default: throw std::runtime_error("Invalid operator for numbers");
        }
    }

    throw std::runtime_error("Type mismatch in binary operation");
}

Value Evaluator::evalQuantifier(const QuantifierExpr& node) {
    auto source = evaluateExpr(*node.source);

    // Save outer iteration context so nested quantifiers can restore it.
    const std::vector<DetectedObject>* saved_objs = ctx_.getCurrentObjects();
    const DetectedObject* saved_obj = ctx_.getCurrentObject();
    std::string saved_img = ctx_.getCurrentImage();

    if (source.type == Value::IMAGE_SET) {
        // ImageSet any/all -> scored ImageSet (fuzzy membership per image)
        std::unordered_map<std::string, float> scores;

        for (const auto& img_path : source.image_set) {
            auto* dets = ctx_.getObjects(img_path);
            if (!dets) continue;

            ctx_.setCurrentImage(img_path);
            ctx_.setCurrentObjects(dets);

            float img_score;
            if (node.quant == Quantifier::Any) {
                img_score = 0.0f;
                for (const auto& d : *dets) {
                    ctx_.setCurrentObject(&d);
                    img_score = std::max(img_score, evalScore(*node.condition));
                }
            } else { // All
                if (dets->empty()) {
                    img_score = 1.0f;  // vacuously true on empty
                } else {
                    img_score = 1.0f;
                    for (const auto& d : *dets) {
                        ctx_.setCurrentObject(&d);
                        img_score = std::min(img_score, evalScore(*node.condition));
                    }
                }
            }

            if (img_score > 0) {
                scores[img_path] = img_score;
            }
        }

        ctx_.setCurrentObjects(saved_objs);
        ctx_.setCurrentObject(saved_obj);
        ctx_.setCurrentImage(saved_img);

        // hard mode: threshold the fuzzy scores at 0.5 (boolean filtering)
        if (hard_mode_) {
            for (auto it = scores.begin(); it != scores.end();) {
                if (it->second < 0.5f) it = scores.erase(it);
                else ++it;
            }
        }
        return Value::makeImageSet(scores);
    }

    if (source.type == Value::OBJECT_SET) {
        // ObjectSet any/all -> fuzzy score (max for any, min for all)
        ctx_.setCurrentObjects(&source.object_set);
        float s;
        if (node.quant == Quantifier::Any) {
            s = 0.0f;
            for (const auto& obj : source.object_set) {
                ctx_.setCurrentObject(&obj);
                s = std::max(s, evalScore(*node.condition));
            }
        } else { // All
            s = source.object_set.empty() ? 1.0f : 1.0f;
            for (const auto& obj : source.object_set) {
                ctx_.setCurrentObject(&obj);
                s = std::min(s, evalScore(*node.condition));
            }
        }
        ctx_.setCurrentObjects(saved_objs);
        ctx_.setCurrentObject(saved_obj);
        ctx_.setCurrentImage(saved_img);
        return Value::makeScore(s);
    }

    ctx_.setCurrentObjects(saved_objs);
    ctx_.setCurrentObject(saved_obj);
    ctx_.setCurrentImage(saved_img);
    throw std::runtime_error("Quantifier expects ImageSet or ObjectSet, got type " + std::to_string(source.type));
}

// `imgs : (condition)` : evaluate the condition once per image (in that image's
// context) and keep the image when the resulting fuzzy score > 0.
Value Evaluator::evalFilter(const FilterExpr& node) {
    Value source = evaluateExpr(*node.source);
    if (source.type != Value::IMAGE_SET) {
        throw std::runtime_error("Filter (:) expects an ImageSet on the left, got type " +
                                 std::to_string(source.type));
    }

    const std::vector<DetectedObject>* saved_objs = ctx_.getCurrentObjects();
    const DetectedObject* saved_obj = ctx_.getCurrentObject();
    std::string saved_img = ctx_.getCurrentImage();

    std::unordered_map<std::string, float> scores;
    for (const auto& img_path : source.image_set) {
        auto* dets = ctx_.getObjects(img_path);
        if (!dets) continue;

        ctx_.setCurrentImage(img_path);
        ctx_.setCurrentObjects(dets);

        float s = evalScore(*node.condition);
        if (s > 0) scores[img_path] = s;
    }

    ctx_.setCurrentObjects(saved_objs);
    ctx_.setCurrentObject(saved_obj);
    ctx_.setCurrentImage(saved_img);

    // hard mode: threshold the fuzzy scores at 0.5 (boolean filtering)
    if (hard_mode_) {
        for (auto it = scores.begin(); it != scores.end();) {
            if (it->second < 0.5f) it = scores.erase(it);
            else ++it;
        }
    }
    return Value::makeImageSet(scores);
}

// `any(condition)` / `all(condition)` : object-level predicates over the
// current image's objects.  any -> max score, all -> min score (vacuous on
// empty images).
Value Evaluator::evalAnyAll(const AnyAllExpr& node) {
    const auto* dets = ctx_.getCurrentObjects();
    const DetectedObject* saved_obj = ctx_.getCurrentObject();

    float s;
    if (node.quant == Quantifier::Any) {
        s = 0.0f;
        if (dets) {
            for (const auto& d : *dets) {
                ctx_.setCurrentObject(&d);
                s = std::max(s, evalScore(*node.condition));
            }
        }
    } else { // All
        s = 1.0f;
        if (dets) {
            for (const auto& d : *dets) {
                ctx_.setCurrentObject(&d);
                s = std::min(s, evalScore(*node.condition));
            }
        }
    }

    ctx_.setCurrentObject(saved_obj);
    return Value::makeScore(s);
}

int Evaluator::countMatching(const std::vector<DetectedObject>& objs, const std::string& cls) const {
    int count = 0;
    ModelRegistry* reg = ctx_.getRegistry();
    for (const auto& o : objs) {
        if (o.class_name == cls || o.super_class == cls) {
            ++count;
        } else if (reg && reg->isChildOf(o.class_name, cls)) {
            ++count;
        }
    }
    return count;
}

Value Evaluator::evalCnt(const CntExpr& node) {
    int count = 0;
    if (const auto* objs = ctx_.getCurrentObjects()) {
        count = countMatching(*objs, node.class_name);
    } else {
        // No iteration context: count across the whole photo library.
        for (const auto& img : ctx_.getCache().images) {
            count += countMatching(img.objects, node.class_name);
        }
    }
    return Value::makeNum(count);
}

Value Evaluator::evalExpand(const ExpandExpr& node) {
    auto src = evaluateExpr(*node.source);
    if (src.type != Value::OBJECT_SET) {
        throw std::runtime_error("Expand (>>) expects an ObjectSet on the left, got type " +
                                 std::to_string(src.type));
    }
    ExtensionManager* ext = ctx_.getExtensionManager();
    if (!ext) {
        throw std::runtime_error("ExtensionManager not available");
    }
    auto new_objs = ext->expand(src.object_set, node.ext_name);
    return Value::makeObjectSet(new_objs);
}

// `del <target>` : delete images.  target is a path string, an identifier
// holding an image set, or any image-set expression.  Top-level statement only.
Value Evaluator::evalDel(const DelStmt& node) {
    Value target = evaluateExpr(*node.target);

    std::vector<std::string> paths;
    if (target.type == Value::STRING) {
        paths.push_back(target.str_val);
    } else if (target.type == Value::IMAGE_SET) {
        paths.assign(target.image_set.begin(), target.image_set.end());
    } else {
        throw std::runtime_error(
            "del expects a path string, an image set, or a variable holding an image set");
    }
    if (paths.empty()) {
        std::cout << "[Del] Nothing to delete." << std::endl;
        return Value::makeNum(0);
    }

    if (!ctx_.deleteImagesCallback()) {
        std::cerr << "[Del] Deletion unavailable: no delete callback is wired." << std::endl;
        return Value::makeNum(0);
    }

    auto removed = ctx_.deleteImagesCallback()(paths);
    std::cout << "[Del] Deleted " << removed.size() << " image(s)." << std::endl;
    return Value::makeNum((double)removed.size());
}

double Evaluator::applyMath(MathFn fn, const std::vector<double>& args) {
    switch (fn) {
        case MathFn::Max: return std::max(args[0], args[1]);
        case MathFn::Min: return std::min(args[0], args[1]);
        case MathFn::Abs: return std::abs(args[0]);
        case MathFn::Sqrt: return std::sqrt(args[0]);
        case MathFn::Pow: return std::pow(args[0], args[1]);
        case MathFn::Log: return std::log(args[0]);
        case MathFn::Exp: return std::exp(args[0]);
    }
    throw std::runtime_error("Unknown math function");
}

Value Evaluator::evalMacroCall(const MacroCallExpr& node) {
    const MacroDef* m = ctx_.getMacro(node.name);
    if (!m) {
        throw std::runtime_error("Unknown macro: " + node.name);
    }

    // left_of(a, b) / above(a, b) / inside(a, b) with bare class-name args:
    // a per-image pair check (e.g. `left_of(cat, dog)`).
    if (node.name == "left_of" || node.name == "above" || node.name == "inside") {
        if (node.args.size() == 2) {
            auto* c1 = dynamic_cast<const IdentExpr*>(node.args[0].get());
            auto* c2 = dynamic_cast<const IdentExpr*>(node.args[1].get());
            ModelRegistry* reg = ctx_.getRegistry();
            if (c1 && c2 && reg && reg->hasClass(c1->name) && reg->hasClass(c2->name)) {
                return Value::makeScore(relationalClassPair(node.name, c1->name, c2->name));
            }
        }
    }

    if (m->is_math) {
        std::vector<double> args;
        for (const auto& a : node.args) {
            Value v = evaluateExpr(*a);
            if (v.type != Value::NUM) {
                throw std::runtime_error("Math function '" + node.name + "' expects Num arguments");
            }
            args.push_back(v.num_val);
        }
        if (args.size() != m->params.size()) {
            throw std::runtime_error("Math function '" + node.name + "' expects " +
                                     std::to_string(m->params.size()) + " arguments");
        }
        return Value::makeNum(applyMath(m->math_fn, args));
    }

    if (m->is_color) {
        return evalColorMacro(node, m);
    }

    if (m->is_hist) {
        return evalHistMacro(node, m);
    }

    if (m->is_img) {
        return evalImgMacro(node, m);
    }

    // AST macro (built-in object macro or user macro)
    if (node.args.size() != m->params.size()) {
        throw std::runtime_error("Macro '" + node.name + "' expects " +
                                 std::to_string(m->params.size()) + " arguments, got " +
                                 std::to_string(node.args.size()));
    }

    ctx_.pushMacroScope();
    for (size_t i = 0; i < m->params.size(); ++i) {
        ctx_.bindMacroParam(m->params[i], evaluateExpr(*node.args[i]));
    }
    Value result = evaluateExpr(*m->body);
    ctx_.popMacroScope();
    return result;
}

Value Evaluator::evalPropertyAccess(const PropertyAccessExpr& node) {
    Value obj = evaluateExpr(*node.object);
    return getPropertyOf(obj, node.property);
}
Value Evaluator::evalCurrentObject(const CurrentObjectExpr&) {
    const DetectedObject* obj = ctx_.getCurrentObject();
    if (!obj) {
        throw std::runtime_error("'obj' used outside of an object context");
    }
    return Value::makeObject(*obj);
}

Value Evaluator::evalCurrentImageObjects(const CurrentImageObjectsExpr&) {
    std::vector<DetectedObject> objs;
    if (const auto* cur = ctx_.getCurrentObjects()) {
        objs = *cur;
        for (auto& o : objs) {
            o.image_path = ctx_.getCurrentImage();
            if (o.score <= 0) o.score = (float)o.confidence;
        }
    } else {
        for (const auto& img : ctx_.getCache().images) {
            for (const auto& o : img.objects) {
                DetectedObject d = o;
                d.image_path = img.path;
                d.score = (float)d.confidence;
                objs.push_back(d);
            }
        }
    }
    return Value::makeObjectSet(objs);
}

// ---- fuzzy helpers ----

float Evaluator::scoreOf(const Value& v) {
    if (v.type == Value::SCORE) return v.score_val;
    if (v.type == Value::BOOL) return v.bool_val ? 1.0f : 0.0f;
    if (v.type == Value::OBJECT_SET) return v.object_set.empty() ? 0.0f : 1.0f;
    if (v.type == Value::IMAGE_SET) return v.image_set.empty() ? 0.0f : 1.0f;
    if (v.type == Value::OBJECT) return 1.0f;
    if (v.type == Value::NUM) return clamp01((float)v.num_val);
    return 0.0f;
}

float Evaluator::sigmoid(double z, double k) {
    return (float)(1.0 / (1.0 + std::exp(-k * z)));
}

float Evaluator::clamp01(float x) {
    return x < 0 ? 0 : (x > 1 ? 1 : x);
}

std::string Evaluator::classPropKind(const Expr* e) {
    if (auto* id = dynamic_cast<const IdentExpr*>(e)) {
        if (id->name == "class") return "class";
        if (id->name == "super_class") return "super";
    }
    if (auto* pa = dynamic_cast<const PropertyAccessExpr*>(e)) {
        if (pa->property == "class") return "class";
        if (pa->property == "super_class") return "super";
    }
    return "";
}

namespace {
bool isCoordProperty(const Expr* e) {
    if (auto* id = dynamic_cast<const IdentExpr*>(e)) {
        return id->name == "x" || id->name == "y" || id->name == "w" ||
               id->name == "h" || id->name == "area";
    }
    if (auto* pa = dynamic_cast<const PropertyAccessExpr*>(e)) {
        return pa->property == "x" || pa->property == "y" || pa->property == "w" ||
               pa->property == "h" || pa->property == "area";
    }
    return false;
}
}  // namespace

float Evaluator::classMatch(const DetectedObject& o, const std::string& cls) const {
    if (o.class_name == cls) return (float)o.confidence;
    if (o.super_class == cls) return (float)(o.confidence * 0.8f);
    return 0.0f;
}

float Evaluator::existentialClassMatch(const std::string& cls) const {
    const auto* objs = ctx_.getCurrentObjects();
    if (!objs) return 0.0f;
    float best = 0.0f;
    for (const auto& o : *objs) {
        best = std::max(best, classMatch(o, cls));
    }
    return best;
}

float Evaluator::relationalClassPair(const std::string& op, const std::string& c1, const std::string& c2) const {
    const auto* objs = ctx_.getCurrentObjects();
    if (!objs) return 0.0f;
    float best = 0.0f;
    for (const auto& a : *objs) {
        float sa = classMatch(a, c1);
        if (sa <= 0) continue;
        for (const auto& b : *objs) {
            if (&a == &b) continue;
            float sb = classMatch(b, c2);
            if (sb <= 0) continue;
            float rel = 0.0f;
            if (op == "left_of") {
                double spacing = b.x - (a.x + a.w);   // positive when b is right of a
                rel = sigmoid(spacing, 20.0);
            } else if (op == "above") {
                double spacing = b.y - (a.y + a.h);   // positive when b is below a
                rel = sigmoid(spacing, 20.0);
            } else if (op == "inside") {
                // fraction of a's box inside b's box
                double ix1 = std::max(a.x - a.w / 2, b.x - b.w / 2);
                double iy1 = std::max(a.y - a.h / 2, b.y - b.h / 2);
                double ix2 = std::min(a.x + a.w / 2, b.x + b.w / 2);
                double iy2 = std::min(a.y + a.h / 2, b.y + b.h / 2);
                if (ix2 > ix1 && iy2 > iy1) {
                    double inter = (ix2 - ix1) * (iy2 - iy1);
                    double aa = a.w * a.h;
                    rel = aa > 0 ? (float)(inter / aa) : 0.0f;
                }
            }
            float s = std::min({sa, sb, rel});
            best = std::max(best, s);
        }
    }
    return best;
}

float Evaluator::evalScore(const Expr& e) {
    return scoreOf(evaluateExpr(e));
}

// Fuzzy color match for color(obj, "blue") etc.  1.0 when the dominant named
// color equals the target; otherwise the fraction of hue energy near the
// target color's hue (from the 32-bin hue histogram).
float Evaluator::colorMatch(const Attr& a, const std::string& name) {
    if (a.dominant_color_name == name) return 1.0f;

    auto targetHue = [&](const std::string& n) -> float {
        if (n == "red") return 0.0f;
        if (n == "orange") return 30.0f;
        if (n == "yellow") return 60.0f;
        if (n == "green") return 120.0f;
        if (n == "cyan") return 180.0f;
        if (n == "blue") return 220.0f;
        if (n == "purple") return 270.0f;
        if (n == "pink") return 330.0f;
        return -1.0f;
    };
    float h = targetHue(name);
    if (h >= 0) {
        int tb = ((int)std::lround(h / 11.25) + 32) % 32;
        float sum = 0.0f;
        for (int d = -2; d <= 2; ++d) sum += a.hue_hist[(tb + d + 32) % 32];
        return clamp01(sum);
    }
    // achromatic colors
    if (name == "gray") return (a.s < 0.15f && a.v > 0.15f && a.v < 0.85f) ? 1.0f : 0.0f;
    if (name == "white") return (a.s < 0.15f && a.v > 0.85f) ? 1.0f : 0.0f;
    if (name == "black") return (a.v < 0.15f) ? 1.0f : 0.0f;
    return 0.0f;  // "brown" only matches via dominant_color_name
}

Value Evaluator::evalColorMacro(const MacroCallExpr& node, const MacroDef* m) {
    // ---- image-level macros read the current image's ImageAttrs ----
    const ColorFn fn = m->color_fn;
    const bool is_img = (fn == ColorFn::ImgTemp || fn == ColorFn::ImgWarmth ||
                         fn == ColorFn::ImgCoolness || fn == ColorFn::ImgColor ||
                         fn == ColorFn::ImgBright || fn == ColorFn::ImgColorful);
    if (is_img) {
        const ImageAttrs* ia = ctx_.getImageAttrs(ctx_.getCurrentImage());
        if (!ia) return Value::makeScore(0.0f);
        switch (fn) {
            case ColorFn::ImgTemp:    return Value::makeScore(clamp01((ia->color_temperature - 2000.0f) / 8000.0f));
            case ColorFn::ImgWarmth:  return Value::makeScore(clamp01(1.0f - (ia->color_temperature - 2000.0f) / 6000.0f));
            case ColorFn::ImgCoolness:return Value::makeScore(clamp01((ia->color_temperature - 2000.0f) / 6000.0f));
            case ColorFn::ImgColor: {
                if (node.args.size() < 1) throw std::runtime_error("img_color() expects a color name");
                Value nameVal = evaluateExpr(*node.args[0]);
                if (nameVal.type != Value::STRING) throw std::runtime_error("img_color() expects a string color name");
                Attr tmp;
                tmp.h = ia->avg_hue;
                tmp.s = ia->avg_saturation;
                tmp.v = ia->avg_value;
                tmp.dominant_color_name = ia->dominant_color;
                tmp.hue_hist = ia->global_hue_hist;
                return Value::makeScore(colorMatch(tmp, nameVal.str_val));
            }
            case ColorFn::ImgBright:  return Value::makeScore(clamp01(ia->avg_value));
            case ColorFn::ImgColorful:
                return Value::makeScore(clamp01(ia->avg_saturation * 2.0f));
            default: break;
        }
        return Value::makeScore(0.0f);
    }

    // Resolve the object: an explicit OBJECT arg, or broadcast the current object.
    Value objVal;
    int start = 0;
    if (!node.args.empty()) {
        Value v = evaluateExpr(*node.args[0]);
        if (v.type == Value::OBJECT) {
            objVal = v;
            start = 1;
        }
    }
    if (objVal.type != Value::OBJECT) {
        const DetectedObject* o = ctx_.getCurrentObject();
        if (!o) throw std::runtime_error("Macro '" + node.name + "' needs an object context");
        objVal = Value::makeObject(*o);
    }
    const Attr& a = objVal.object.attr;

    if (m->color_fn == ColorFn::Color) {
        if (start >= (int)node.args.size()) {
            throw std::runtime_error("color() expects a color name argument");
        }
        Value nameVal = evaluateExpr(*node.args[start]);
        if (nameVal.type != Value::STRING) {
            throw std::runtime_error("color() expects a string color name");
        }
        return Value::makeScore(colorMatch(a, nameVal.str_val));
    }

    switch (m->color_fn) {
        case ColorFn::Cct:
            return Value::makeScore(clamp01((a.color_temperature - 2000.0f) / 8000.0f));
        case ColorFn::Warmth:
            // warm (low-K) colors get high scores; cool (high-K) get low.
            return Value::makeScore(clamp01(1.0f - (a.color_temperature - 2000.0f) / 6000.0f));
        case ColorFn::Coolness:
            return Value::makeScore(clamp01((a.color_temperature - 2000.0f) / 6000.0f));
        case ColorFn::Brightness:
            return Value::makeScore(clamp01(a.v));
        case ColorFn::Saturation:
            return Value::makeScore(clamp01(a.s));
        default:
            break;
    }
    return Value::makeScore(0.0f);
}

// ---- hue-histogram macros (obj_hist / img_hist / hist_sim) ----

// Cosine similarity of two 32-bin histograms, 0..1.  Zero vectors -> 0.
float Evaluator::cosineHistSim(const HistVec& a, const HistVec& b) {
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (int i = 0; i < 32; ++i) {
        dot += (double)a[i] * b[i];
        na += (double)a[i] * a[i];
        nb += (double)b[i] * b[i];
    }
    if (na <= 1e-12 || nb <= 1e-12) return 0.0f;
    return clamp01((float)(dot / std::sqrt(na * nb)));
}

// Best (highest-confidence) object of `cls` (is-a aware) in the current image.
const DetectedObject* Evaluator::bestClassObject(const std::string& cls) const {
    const auto* objs = ctx_.getCurrentObjects();
    if (!objs) return nullptr;
    ModelRegistry* reg = ctx_.getRegistry();
    const DetectedObject* best = nullptr;
    float best_conf = 0.0f;
    for (const auto& o : *objs) {
        bool m = (o.class_name == cls || o.super_class == cls);
        if (!m && reg) m = reg->isChildOf(o.class_name, cls);
        if (!m) continue;
        float c = (float)o.confidence;
        if (c > best_conf) {
            best_conf = c;
            best = &o;
        }
    }
    return best;
}

Value Evaluator::evalHistMacro(const MacroCallExpr& node, const MacroDef* m) {
    if (m->hist_fn == HistFn::ImgHist) {
        // img_hist() : current image's global hue histogram (zero vector if none).
        const ImageAttrs* ia = ctx_.getImageAttrs(ctx_.getCurrentImage());
        HistVec h{};
        if (ia) h = ia->global_hue_hist;
        return Value::makeHistVec(h);
    }

    if (m->hist_fn == HistFn::HistSim) {
        // hist_sim(A, B) : cosine similarity of two histogram values.
        if (node.args.size() != 2) {
            throw std::runtime_error("hist_sim() expects two histogram arguments");
        }
        Value a = evaluateExpr(*node.args[0]);
        Value b = evaluateExpr(*node.args[1]);
        if (a.type != Value::HIST_VEC || b.type != Value::HIST_VEC) {
            throw std::runtime_error("hist_sim() expects histograms (from obj_hist()/img_hist())");
        }
        return Value::makeScore(cosineHistSim(a.hist_val, b.hist_val));
    }

    if (m->hist_fn == HistFn::ImgHistValue) {
        // img_hist_value(idx) : one bin of the current image's global histogram.
        if (node.args.size() != 1) {
            throw std::runtime_error("img_hist_value() expects a bin index (0..31)");
        }
        int idx = evalBinIndex(*node.args[0]);
        const ImageAttrs* ia = ctx_.getImageAttrs(ctx_.getCurrentImage());
        float v = ia ? ia->global_hue_hist[idx] : 0.0f;
        return Value::makeScore(clamp01(v));
    }

    if (m->hist_fn == HistFn::HistValue) {
        // hist_value(obj, idx) : one bin of the object's histogram.
        if (node.args.size() != 2) {
            throw std::runtime_error("hist_value() expects an object and a bin index (0..31)");
        }
        int idx = evalBinIndex(*node.args[1]);
        auto attr = objectAttrFor(node, 0);
        float v = attr ? attr->hue_hist[idx] : 0.0f;
        return Value::makeScore(clamp01(v));
    }

    // obj_hist(arg) : the hue histogram of the given object.
    auto attr = objectAttrFor(node, 0);
    if (!attr) return Value::makeHistVec(HistVec{});
    return Value::makeHistVec(attr->hue_hist);
}

// Resolve the object a hist macro reads from: a bare class name (best object of
// that class in the current image), an explicit OBJECT value, or the current
// object.  Returns nullopt when nothing matches.
std::optional<Attr> Evaluator::objectAttrFor(const MacroCallExpr& node, size_t argIdx) {
    if (argIdx < node.args.size()) {
        if (auto* id = dynamic_cast<const IdentExpr*>(node.args[argIdx].get())) {
            ModelRegistry* reg = ctx_.getRegistry();
            if (reg && reg->hasClass(id->name)) {
                if (const DetectedObject* best = bestClassObject(id->name)) return best->attr;
            }
        }
        Value v = evaluateExpr(*node.args[argIdx]);
        if (v.type == Value::OBJECT) return v.object.attr;
    }
    const DetectedObject* o = ctx_.getCurrentObject();
    if (o) return o->attr;
    return std::nullopt;
}

int Evaluator::evalIndex(const Expr& arg, int max) {
    Value v = evaluateExpr(arg);
    if (v.type != Value::NUM) {
        throw std::runtime_error("index must be a number (0.." + std::to_string(max) + ")");
    }
    int idx = (int)v.num_val;
    if (idx < 0 || idx > max) {
        throw std::runtime_error("index out of range: " + std::to_string(idx) +
                                 " (valid 0.." + std::to_string(max) + ")");
    }
    return idx;
}

Value Evaluator::evalImgMacro(const MacroCallExpr& node, const MacroDef* m) {
    const ImageAttrs* ia = ctx_.getImageAttrs(ctx_.getCurrentImage());
    const ImgFn fn = m->img_fn;

    auto strArg = [&](size_t i) -> std::string {
        if (i >= node.args.size()) throw std::runtime_error(node.name + "() missing argument");
        Value v = evaluateExpr(*node.args[i]);
        if (v.type != Value::STRING) throw std::runtime_error(node.name + "() expects a string argument");
        return v.str_val;
    };

    switch (fn) {
        case ImgFn::OverExposure: return Value::makeScore(ia ? ia->overexposure_score : 0.0f);
        case ImgFn::UnderExposure: return Value::makeScore(ia ? ia->underexposure_score : 0.0f);
        case ImgFn::ExposureGood: return Value::makeScore(ia ? ia->exposure_goodness : 1.0f);
        case ImgFn::GlobalBlur: return Value::makeScore(ia ? ia->global_blur_score : 0.0f);
        case ImgFn::GlobalBlurry: return Value::makeScore(ia ? clamp01(1.0f - ia->global_blur_score) : 1.0f);
        case ImgFn::LumaHistVal: {
            int idx = evalIndex(*node.args[0], 63);
            return Value::makeScore(ia ? clamp01(ia->luma_hist[idx]) : 0.0f);
        }
        case ImgFn::LocalBlur: {
            auto attr = objectAttrFor(node, 0);
            return Value::makeScore(attr ? clamp01(attr->local_blur_score) : 0.0f);
        }
        case ImgFn::Camera: {
            std::string s;
            if (ia) {
                s = ia->camera_make;
                if (!ia->camera_model.empty()) {
                    if (!s.empty()) s += " ";
                    s += ia->camera_model;
                }
            }
            return Value::makeString(s);
        }
        case ImgFn::Iso: return Value::makeNum(ia ? (double)ia->iso : -1.0);
        case ImgFn::Shutter: return Value::makeNum(ia ? (double)ia->shutter_speed : -1.0);
        case ImgFn::Aperture: return Value::makeNum(ia ? (double)ia->aperture : -1.0);
        case ImgFn::FocalLength: return Value::makeNum(ia ? (double)ia->focal_length : -1.0);
        case ImgFn::Date: return Value::makeString(ia ? ia->datetime_original : "");
        case ImgFn::Tag: {
            std::string key = strArg(0);
            std::string val;
            if (ia) {
                auto it = ia->user_tags.find(key);
                if (it != ia->user_tags.end()) val = it->second;
            }
            return Value::makeString(val);
        }
        case ImgFn::HasTag: {
            std::string key = strArg(0);
            bool has = ia && ia->user_tags.count(key) != 0;
            return Value::makeScore(has ? 1.0f : 0.0f);
        }
        case ImgFn::TagEquals: {
            std::string key = strArg(0), want = strArg(1);
            bool eq = ia && ia->user_tags.count(key) && ia->user_tags.at(key) == want;
            return Value::makeScore(eq ? 1.0f : 0.0f);
        }
        case ImgFn::Stof: {
            std::string s = strArg(0);
            double d = 0.0;
            try {
                size_t pos = 0;
                d = std::stod(s, &pos);
            } catch (...) { d = 0.0; }
            return Value::makeNum(d);
        }
        case ImgFn::StrContains: {
            std::string s = strArg(0), sub = strArg(1);
            return Value::makeScore(s.find(sub) != std::string::npos ? 1.0f : 0.0f);
        }
    }
    return Value::makeScore(0.0f);
}