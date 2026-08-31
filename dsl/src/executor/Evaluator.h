#pragma once
#include <memory>
#include <vector>
#include <optional>
#include "../parser/AST.h"
#include "Context.h"

class Evaluator {
public:
    explicit Evaluator(Context& ctx);
    void setHardMode(bool on) { hard_mode_ = on; }
    bool hardMode() const { return hard_mode_; }

    Value evaluate(const ASTNode& node);
    Value evaluateExpr(const Expr& expr);
    Value evalDel(const DelStmt& node);

private:
    Value evalBinary(const BinaryExpr& node);
    Value evalUnary(const UnaryExpr& node);
    Value evalDollar(const DollarExpr& node);
    Value evalCollection(const CollectionExpr& node);
    Value evalCaret(const CaretExpr& node);
    Value evalQuantifier(const QuantifierExpr& node);
    Value evalFilter(const FilterExpr& node);
    Value evalAnyAll(const AnyAllExpr& node);
    Value evalExpand(const ExpandExpr& node);
    Value evalCnt(const CntExpr& node);
    Value evalMacroCall(const MacroCallExpr& node);
    Value evalPropertyAccess(const PropertyAccessExpr& node);
    Value evalCurrentObject(const CurrentObjectExpr& node);
    Value evalCurrentImageObjects(const CurrentImageObjectsExpr& node);
    Value evalNumber(const NumberExpr& node);
    Value evalString(const StringExpr& node);
    Value evalIdent(const IdentExpr& node);

    // Property access on current object (bare `area`, `class`, ...)
    Value getProperty(const std::string& name) const;
    // Property access on an arbitrary object/attr value (`x.area`, `x.attr.h`)
    Value getPropertyOf(const Value& v, const std::string& prop) const;

    // Count objects matching a class (exact, super_class, or is-a child).
    int countMatching(const std::vector<DetectedObject>& objs, const std::string& cls) const;

    double applyMath(MathFn fn, const std::vector<double>& args);
    Value evalColorMacro(const MacroCallExpr& node, const MacroDef* m);
    Value evalHistMacro(const MacroCallExpr& node, const MacroDef* m);
    Value evalImgMacro(const MacroCallExpr& node, const MacroDef* m);
    Value evalSceneMacro(const MacroCallExpr& node, const MacroDef* m);
    Value evalClusterMacro(const MacroCallExpr& node, const MacroDef* m);
    static float colorMatch(const Attr& a, const std::string& name);
    static float cosineHistSim(const HistVec& a, const HistVec& b);
    // Highest-confidence object of `cls` (is-a aware) in the current image.
    const DetectedObject* bestClassObject(const std::string& cls) const;
    // Resolve the object a hist macro reads from: bare class (best object of
    // that class), an explicit OBJECT value, or the current object.
    std::optional<Attr> objectAttrFor(const MacroCallExpr& node, size_t argIdx);
    // Evaluate a bin-index argument in [0, max]; throws on non-number / out of range.
    int evalIndex(const Expr& arg, int max);
    int evalBinIndex(const Expr& arg) { return evalIndex(arg, 31); }

    // ---- fuzzy helpers ----
    static float scoreOf(const Value& v);          // value -> 0..1 fuzzy score
    static float sigmoid(double z, double k = 10.0); // 1/(1+exp(-k*z))
    static float clamp01(float x);
    static std::string classPropKind(const Expr* e);   // "class" / "super" / ""
    float classMatch(const DetectedObject& o, const std::string& cls) const;
    float existentialClassMatch(const std::string& cls) const;  // max over current objects
    float relationalClassPair(const std::string& op, const std::string& c1, const std::string& c2) const;
    float evalScore(const Expr& e);

    Context& ctx_;
    bool hard_mode_ = false;
};