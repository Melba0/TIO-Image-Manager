#pragma once
#include <memory>
#include <string>
#include <vector>
#include <variant>

enum class Quantifier { Any, All };

enum class BinOp {
    Add, Sub, Mul, Div,
    GT, LT, GE, LE, EQ, NE,
    And, Or,
    SetUnion, SetIntersect, SetDiff
};

enum class UnaryOp { Not, Percent, Negate };

struct ASTNode {
    virtual ~ASTNode() = default;
};

struct Expr : ASTNode {};

struct NumberExpr : Expr {
    double value;
    explicit NumberExpr(double v) : value(v) {}
};

struct StringExpr : Expr {
    std::string value;
    explicit StringExpr(std::string v) : value(std::move(v)) {}
};

struct IdentExpr : Expr {
    std::string name;
    explicit IdentExpr(std::string n) : name(std::move(n)) {}
};

struct BinaryExpr : Expr {
    std::unique_ptr<Expr> left;
    BinOp op;
    std::unique_ptr<Expr> right;
    BinaryExpr(std::unique_ptr<Expr> l, BinOp o, std::unique_ptr<Expr> r)
        : left(std::move(l)), op(o), right(std::move(r)) {}
};

struct UnaryExpr : Expr {
    UnaryOp op;
    std::unique_ptr<Expr> operand;
    UnaryExpr(UnaryOp o, std::unique_ptr<Expr> e)
        : op(o), operand(std::move(e)) {}
};

struct DollarExpr : Expr {};

// `collection("name")` : the image set of a user-created virtual album.
struct CollectionExpr : Expr {
    std::string name;
    explicit CollectionExpr(std::string n) : name(std::move(n)) {}
};

struct CaretExpr : Expr {
    std::unique_ptr<Expr> operand;
    explicit CaretExpr(std::unique_ptr<Expr> e) : operand(std::move(e)) {}
};

struct QuantifierExpr : Expr {
    std::unique_ptr<Expr> source;
    Quantifier quant;
    std::unique_ptr<Expr> condition;
    QuantifierExpr(std::unique_ptr<Expr> s, Quantifier q, std::unique_ptr<Expr> c)
        : source(std::move(s)), quant(q), condition(std::move(c)) {}
};

// `imgs : (condition)` : filter an image set by an image-level condition.
// The condition is evaluated once per image (in that image's context) and the
// image is kept when the resulting score > 0.
struct FilterExpr : Expr {
    std::unique_ptr<Expr> source;
    std::unique_ptr<Expr> condition;
    FilterExpr(std::unique_ptr<Expr> s, std::unique_ptr<Expr> c)
        : source(std::move(s)), condition(std::move(c)) {}
};

// `any(condition)` / `all(condition)` : object-level predicates evaluated in
// the current image's context.  any() -> max over the image's objects,
// all() -> min over the image's objects (vacuously true on empty images).
struct AnyAllExpr : Expr {
    Quantifier quant;
    std::unique_ptr<Expr> condition;
    AnyAllExpr(Quantifier q, std::unique_ptr<Expr> c)
        : quant(q), condition(std::move(c)) {}
};

// `ObjectSet >> extension_name` : expand objects with an extension pack.
struct ExpandExpr : Expr {
    std::unique_ptr<Expr> source;
    std::string ext_name;
    ExpandExpr(std::unique_ptr<Expr> s, std::string n)
        : source(std::move(s)), ext_name(std::move(n)) {}
};

// `cnt(class)` : number of objects matching a class (is-a aware).
struct CntExpr : Expr {
    std::string class_name;
    explicit CntExpr(std::string c) : class_name(std::move(c)) {}
};

// `name(arg1, arg2, ...)` : macro call (built-in math / built-in object macro / user macro).
struct MacroCallExpr : Expr {
    std::string name;
    std::vector<std::unique_ptr<Expr>> args;
    MacroCallExpr(std::string n, std::vector<std::unique_ptr<Expr>> a)
        : name(std::move(n)), args(std::move(a)) {}
};

// `object.property` (possibly chained: `obj.attr.h`).
struct PropertyAccessExpr : Expr {
    std::unique_ptr<Expr> object;
    std::string property;
    PropertyAccessExpr(std::unique_ptr<Expr> o, std::string p)
        : object(std::move(o)), property(std::move(p)) {}
};

// `obj` : the current object (single object value, broadcast in object context).
struct CurrentObjectExpr : Expr {};

// `obj any/all ...` : the current image's objects (an ObjectSet).
struct CurrentImageObjectsExpr : Expr {};

// Built-in math functions mapped directly to std:: functions.
enum class MathFn { Max, Min, Abs, Sqrt, Pow, Log, Exp };

// Built-in color macros computed from DetectedObject::attr.
enum class ColorFn {
    Color, Cct, Warmth, Coolness, Brightness, Saturation,          // object-level
    ImgTemp, ImgWarmth, ImgCoolness, ImgColor, ImgBright, ImgColorful  // image-level
};

// Built-in hue-histogram macros (expose the cached 32-dim hue_hist to the DSL).
enum class HistFn {
    ObjHist,   // obj_hist(obj)          -> 32-dim histogram of the object's region
    ImgHist,   // img_hist()             -> 32-dim global histogram of the current image
    HistSim,   // hist_sim(A, B)         -> cosine similarity of two histograms (0..1)
    HistValue, // hist_value(obj, idx)   -> value of bin `idx` of the object's histogram
    ImgHistValue // img_hist_value(idx)  -> value of bin `idx` of the image's global histogram
};

// Built-in image-quality / EXIF / user-tag macros (read the current image's
// ImageAttrs, or a specific object for obj_blur).
enum class ImgFn {
    OverExposure,   // img_over()            -> overexposure_score (0..1)
    UnderExposure,  // img_under()           -> underexposure_score (0..1)
    ExposureGood,   // img_exp_good()        -> exposure_goodness (0..1)
    LumaHistVal,    // img_hist_val(idx)     -> luminance-histogram bin idx (0..63)
    GlobalBlur,     // img_blur()            -> global_blur_score (0..1, 1 = sharp)
    GlobalBlurry,   // img_blurry()          -> 1 - global_blur_score
    LocalBlur,      // obj_blur(obj)         -> object region sharpness (0..1)
    Camera,         // img_camera()          -> "make model" (string)
    Iso,            // img_iso()             -> ISO (num, -1 if absent)
    Shutter,        // img_shutter()         -> shutter speed s (num)
    Aperture,       // img_aperture()        -> f-number (num)
    FocalLength,    // img_fl()              -> focal length mm (num)
    Date,           // img_date()            -> datetime string
    Tag,            // img_tag(key)          -> user-tag value (string, "" if absent)
    HasTag,         // img_has_tag(key)      -> bool
    TagEquals,      // img_tag_equals(k, v)  -> bool (string equality)
    Stof,           // stof(s)               -> number parsed from a string
    StrContains     // str_contains(s, sub)  -> bool
};

// Built-in Places365 scene-recognition macros (read the cached scene_vector /
// dominant_scene of the current image).
enum class SceneFn {
    SceneProb,      // img_scene(name)       -> score of that scene (0..1)
    SceneTop,       // img_scene_top()       -> dominant scene name (string)
    SceneVec,       // img_scene_vec()       -> full 365-dim vector (internal HIST_VEC-like)
    IsIndoor        // img_is_indoor()       -> indoor probability (0..1)
};

// Built-in clustering macros (read the object's cached cluster_ids).
enum class ClusterFn {
    ClusterId,      // cluster_id(obj, cluster_name)      -> cluster id string ("" if none)
    ClusterSim      // cluster_sim(a, b, cluster_name)    -> 1 if a and b share a cluster
};

// Unified macro definition table entry (built-in and user macros are identical here).
struct MacroDef {
    std::vector<std::string> params;
    std::shared_ptr<Expr> body;   // AST body (object/atmosphere/relational/user macros)
    bool is_math = false;         // true -> evaluated by applyMath()
    MathFn math_fn = MathFn::Max;
    bool is_color = false;        // true -> evaluated by applyColor() from obj.attr
    ColorFn color_fn = ColorFn::Color;
    bool is_hist = false;         // true -> evaluated by evalHistMacro()
    HistFn hist_fn = HistFn::ObjHist;
    bool is_img = false;          // true -> evaluated by evalImgMacro()
    ImgFn img_fn = ImgFn::OverExposure;
    bool is_scene = false;        // true -> evaluated by evalSceneMacro()
    SceneFn scene_fn = SceneFn::SceneProb;
    bool is_cluster = false;      // true -> evaluated by evalClusterMacro()
    ClusterFn cluster_fn = ClusterFn::ClusterId;

    static MacroDef ast(std::vector<std::string> params, std::shared_ptr<Expr> body) {
        MacroDef d;
        d.params = std::move(params);
        d.body = std::move(body);
        return d;
    }
    static MacroDef math(MathFn fn, int argc) {
        MacroDef d;
        d.is_math = true;
        d.math_fn = fn;
        d.params.resize(argc);
        return d;
    }
    static MacroDef color(ColorFn fn, int argc) {
        MacroDef d;
        d.is_color = true;
        d.color_fn = fn;
        d.params.resize(argc);
        return d;
    }
    static MacroDef hist(HistFn fn, int argc) {
        MacroDef d;
        d.is_hist = true;
        d.hist_fn = fn;
        d.params.resize(argc);
        return d;
    }
    static MacroDef img(ImgFn fn, int argc) {
        MacroDef d;
        d.is_img = true;
        d.img_fn = fn;
        d.params.resize(argc);
        return d;
    }
    static MacroDef scene(SceneFn fn, int argc) {
        MacroDef d;
        d.is_scene = true;
        d.scene_fn = fn;
        d.params.resize(argc);
        return d;
    }
    static MacroDef cluster(ClusterFn fn, int argc) {
        MacroDef d;
        d.is_cluster = true;
        d.cluster_fn = fn;
        d.params.resize(argc);
        return d;
    }
};

// `macro name(p1, p2, ...) = <expression>` statement.
struct MacroDefStmt : ASTNode {
    std::string name;
    std::vector<std::string> params;
    std::shared_ptr<Expr> body;
};

struct AssignStmt : ASTNode {
    std::string name;
    std::unique_ptr<Expr> value;
    AssignStmt(std::string n, std::unique_ptr<Expr> v)
        : name(std::move(n)), value(std::move(v)) {}
};

// `del <target>` : delete images.  target is a string path, an identifier that
// holds an image set, or any image-set expression.  Top-level statement only.
struct DelStmt : ASTNode {
    std::unique_ptr<Expr> target;
    explicit DelStmt(std::unique_ptr<Expr> t) : target(std::move(t)) {}
};

struct ExprStmt : ASTNode {
    std::unique_ptr<Expr> expr;
    explicit ExprStmt(std::unique_ptr<Expr> e) : expr(std::move(e)) {}
};

struct Program : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> statements;
};