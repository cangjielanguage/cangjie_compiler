// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "Diags.h"
#include "TypeCheckUtil.h"

using namespace Cangjie;
using namespace AST;
using namespace Sema;

namespace Cangjie {
namespace {
void DiagExpectConstFunc(DiagnosticEngine& diag, const FuncDecl& fd)
{
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_expect_const, fd, MakeRange(fd.identifier), "function");
    builder.AddMainHintArguments("");
}

void DiagExpectConstExpr(DiagnosticEngine& diag, const Expr& expr, bool isWeak)
{
    std::string constKind = isWeak ? "expression" : "expression guaranteed to be evaluated at compile time";
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_expect_const, expr, constKind);
    std::string mainHint = "";
    if (expr.astKind == ASTKind::RETURN_EXPR) {
        builder.AddNote("cannot jump to non-constant context from a constant context");
    } else if (auto tryExpr = DynamicCast<const TryExpr*>(&expr); tryExpr && tryExpr->isDesugaredFromTryWithResources) {
        builder.AddNote("try-with-resources expressions are not constant");
    } else if (Ty::IsTyCorrect(expr.ty) && expr.ty->IsStructArray() &&
        (expr.astKind == ASTKind::LIT_CONST_EXPR || expr.astKind == ASTKind::ARRAY_LIT)) {
        CJC_ASSERT(!expr.ty->typeArgs.empty() && expr.ty->typeArgs.front());
        mainHint = "expressions of type 'Array' are not constant";
        if (expr.astKind == ASTKind::ARRAY_LIT) {
            auto expected = "VArray<" + expr.ty->typeArgs.front()->String() + ", $" +
                std::to_string(StaticCast<const ArrayLit&>(expr).children.size()) + ">";
            builder.AddNote("consider add type annotation '" + expected + "' to use value array");
        }
    }
    builder.AddMainHintArguments(mainHint);
}

void DiagDefineVarInConstFunction(DiagnosticEngine& diag, const VarDeclAbstract& vda)
{
    auto range = MakeRange(vda.identifier);
    diag.DiagnoseRefactor(DiagKindRefactor::sema_cannot_define_var_in_const_funciton, vda, range);
}

void DiagNoConstInit(DiagnosticEngine& diag, const FuncDecl& fd)
{
    auto range = MakeRange(fd.identifier);
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_no_const_init, fd, range);
    Ptr<const Decl> outerDecl = fd.outerDecl;
    if (outerDecl == nullptr) {
        return;
    }
    if (outerDecl->astKind == ASTKind::EXTEND_DECL) {
        const auto& ed = StaticCast<const ExtendDecl&>(*outerDecl);
        CJC_NULLPTR_CHECK(ed.extendedType);
        outerDecl = Ty::GetDeclPtrOfTy(ed.extendedType->ty);
    }
    if (outerDecl == nullptr || outerDecl->TestAttr(Attribute::IMPORTED)) {
        return;
    }
    auto outerDeclRange = MakeRange(outerDecl->identifier);
    builder.AddNote(*outerDecl, outerDeclRange,
        "consider add a 'const' constructor to this " + TypeCheckUtil::DeclKindToString(*outerDecl));
}

void DiagCannotDefineConstInit(
    DiagnosticEngine& diag, const std::vector<Ptr<Decl>>& constInits, const std::vector<Ptr<VarDeclAbstract>>& vars)
{
    CJC_ASSERT(!constInits.empty());
    auto iter = constInits.cbegin();
    CJC_NULLPTR_CHECK(*iter);
    auto range = MakeRange((*iter)->identifier);
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_class_const_init_with_var, **iter, range);
    ++iter;
    while (iter != constInits.cend()) {
        CJC_NULLPTR_CHECK(*iter);
        auto initRange = MakeRange((*iter)->identifier);
        builder.AddNote(**iter, initRange, "cannot define 'const' constructor");
        ++iter;
    }
    for (auto var : vars) {
        CJC_NULLPTR_CHECK(var);
        auto varRange = MakeRange(var->identifier);
        builder.AddNote(*var, varRange, "member variable declared with 'var'");
    }
}

bool HasAnnotationDeclTarget(const Annotation& anno)
{
    if (anno.kind != AnnotationKind::ANNOTATION || anno.args.size() != 1) {
        return false;
    }
    auto& arg = anno.args.front();
    CJC_NULLPTR_CHECK(arg);
    CJC_NULLPTR_CHECK(arg->expr);
    return arg->name == "target" && arg->expr->astKind == ASTKind::ARRAY_LIT;
}

// String operations are handled specially,
// because the implementations in the core library rely on `unsafe` and loop,
// which are not permitted in a `const` operator.
bool IsStringBinaryExpr(const Expr& expr)
{
    CJC_NULLPTR_CHECK(expr.desugarExpr);
    if (!Ty::IsTyCorrect(expr.ty) || expr.astKind != ASTKind::BINARY_EXPR ||
        expr.desugarExpr->astKind != ASTKind::CALL_EXPR) {
        return false;
    }
    CallExpr& ce = StaticCast<CallExpr&>(*expr.desugarExpr);
    CJC_NULLPTR_CHECK(ce.baseFunc);
    if (ce.baseFunc->astKind != ASTKind::MEMBER_ACCESS) {
        return false;
    }
    auto& baseExpr = StaticCast<const MemberAccess&>(*ce.baseFunc).baseExpr;
    CJC_NULLPTR_CHECK(baseExpr);
    if (!Ty::IsTyCorrect(baseExpr->ty) || !baseExpr->ty->IsString()) {
        return false;
    }
    auto baseFuncTarget = ce.baseFunc->GetTarget();
    if (baseFuncTarget == nullptr || baseFuncTarget->fullPackageName != CORE_PACKAGE_NAME || ce.args.empty()) {
        return false;
    }
    return true;
}

std::unordered_set<Ptr<const Decl>> CollectMembers(const std::vector<OwnedPtr<Decl>>& decls)
{
    std::unordered_set<Ptr<const Decl>> res;
    for (const auto& decl : decls) {
        if (decl->astKind == ASTKind::VAR_DECL && !decl->TestAttr(Attribute::STATIC)) {
            (void)res.emplace(decl.get());
        }
    }
    return res;
}

// Checks whether `StructDecl` or `ClassDecl` has `const init`.
bool HasConstInit(const std::vector<OwnedPtr<Decl>>& decls)
{
    return Utils::In(decls, [](auto& decl) {
        CJC_NULLPTR_CHECK(decl);
        return decl->IsConst() && decl->TestAttr(Attribute::CONSTRUCTOR);
    });
}

// Checks whether a decl has been marked as "const safe" - and should be skipped by the
// constant evaluation checker. Only usable in the standard library.
bool IsConstSafe(Decl& decl)
{
    auto isConstSafe = [](const OwnedPtr<AST::Annotation>& a) {
        return a->kind == AnnotationKind::CONSTSAFE;
    };
    auto& annos = decl.annotations;
    return std::find_if(annos.begin(), annos.end(), isConstSafe) != annos.end();
}
}; // namespace

class ConstEvaluationChecker {
public:
    ConstEvaluationChecker(DiagnosticEngine& diag, Package& pkg) : diag(diag), pkg(pkg)
    {
    }

    ~ConstEvaluationChecker() = default;

    void Check()
    {
        (void)ChkAllDeclsByWalk(pkg);
    }

private:
    // Walk through the `node` and do `const` checking for declarations.
    bool ChkAllDeclsByWalk(Node& node)
    {
        bool res = true;
        Walker(&node, [this, &res](auto node) {
            if (node->TestAnyAttr(Attribute::IMPORTED, Attribute::IS_BROKEN, Attribute::HAS_BROKEN)) {
                return VisitAction::SKIP_CHILDREN;
            }
            if (auto decl = DynamicCast<Decl*>(node); decl) {
                // Arguments of annotations must be `const` expressions.
                res = ChkAnnotations(*decl) && res;
                // Const-safe decls don't need to be checked.
                if (IsConstSafe(*decl)) {
                    return VisitAction::SKIP_CHILDREN;
                }
                // There are two kinds of rules to check for the `decl`:
                // 1. If `isConst` is set, the `decl` must be a `VarDecl` or `VarWithPatternDecl` or `FuncDecl`.
                //    We'll check rules related to `const` expressions.
                if (decl->IsConst()) {
                    res = ChkConstDecl(*decl) && res;
                    return VisitAction::SKIP_CHILDREN;
                }
                // 2. If it is a `StructDecl` or `ClassDecl` or `ExtendDecl`,
                //    inheritance rules such as `const init` will be checked.
                switch (decl->astKind) {
                    case ASTKind::STRUCT_DECL:
                        res = ChkStructDecl(StaticCast<const StructDecl&>(*decl)) && res;
                        return VisitAction::SKIP_CHILDREN;
                    case ASTKind::CLASS_DECL:
                        res = ChkClassDecl(StaticCast<const ClassDecl&>(*decl)) && res;
                        return VisitAction::SKIP_CHILDREN;
                    case ASTKind::EXTEND_DECL:
                        res = ChkExtendDecl(StaticCast<const ExtendDecl&>(*decl)) && res;
                        return VisitAction::SKIP_CHILDREN;
                    default:
                        return VisitAction::WALK_CHILDREN;
                }
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        return res;
    }

    bool ChkAnnotations(const Decl& decl)
    {
        bool res = true;
        for (auto& anno : decl.annotations) {
            CJC_NULLPTR_CHECK(anno);
            if (HasAnnotationDeclTarget(*anno)) {
                CJC_ASSERT(anno->args.size() == 1 && anno->args.front() && anno->args.front()->expr &&
                    anno->args.front()->expr->astKind == ASTKind::ARRAY_LIT);
                res = ChkChildren(StaticCast<ArrayLit&>(*anno->args.front()->expr), true) && res;
            }
        }
        if (decl.annotationsArray != nullptr) {
            res = ChkChildren(*decl.annotationsArray, true) && res;
        }
        return res;
    }

    // Call this function to check `Decl` if the `decl` is assumed to be `const`.
    // This function will do `const` checking even if the `Decl` is not marked as `const`.
    // For example, the `let` declarations in `const` functions
    // will be checked and marked as `const` by this function.
    bool ChkConstDecl(Decl& decl)
    {
        if (decl.TestAnyAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN)) {
            return false;
        }
        switch (decl.astKind) {
            case ASTKind::VAR_DECL:
                return ChkVarDecl(StaticCast<VarDecl&>(decl));
            case ASTKind::VAR_WITH_PATTERN_DECL:
                return ChkVarWithPatternDecl(StaticCast<VarWithPatternDecl&>(decl));
            case ASTKind::FUNC_DECL:
                return ChkFuncDecl(StaticCast<FuncDecl&>(decl));
            default:
                return false;
        }
    }

    bool ChkVarDeclAbstract(VarDeclAbstract& vd)
    {
        if (vd.isConst) {
            ConstWalker(&vd, [this](auto node) {
                if (Utils::In(node.get(), inConstContext)) {
                    return VisitAction::SKIP_CHILDREN;
                } else if (auto varDecl = DynamicCast<const VarDeclAbstract*>(node); varDecl && varDecl->isConst) {
                    inConstContext.emplace(node.get());
                } else if (node->astKind == ASTKind::FUNC_BODY || node->astKind == ASTKind::RETURN_EXPR) {
                    inConstContext.emplace(node.get());
                }
                return VisitAction::WALK_CHILDREN;
            }).Walk();
        }
        if (vd.isVar) {
            DiagDefineVarInConstFunction(diag, vd);
            // Set `isConst` to be `true` to avoid chain of errors.
            vd.isConst = true;
            return false;
        }
        if (vd.initializer == nullptr) {
            return true;
        }
        return ChkExpr(*vd.initializer, !vd.isConst);
    }

    bool ChkVarDecl(VarDecl& vd)
    {
        return ChkVarDeclAbstract(vd);
    }

    bool ChkVarWithPatternDecl(VarWithPatternDecl& vpd)
    {
        // only if vpd is const need the inner vars marked const
        // let vpd in const func are not const variables
        if (vpd.IsConst()) {
            Walker(vpd.irrefutablePattern.get(), [](auto node) {
                if (node->astKind == ASTKind::VAR_DECL) {
                    StaticCast<VarDecl&>(*node).isConst = true;
                }
                return VisitAction::WALK_CHILDREN;
            }).Walk();
        }
        return ChkVarDeclAbstract(vpd);
    }

    bool ChkFuncBody(const FuncBody& fb)
    {
        if (fb.TestAnyAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN)) {
            return false;
        }
        // The `funcDecl` of a lambda expression is null.
        if (fb.funcDecl == nullptr || fb.funcDecl->IsConst()) {
            ConstWalker(&fb, [this](auto node) {
                if (auto decl = DynamicCast<Decl*>(node)) {
                    inConstFunc.emplace(decl);
                } else if (auto re = DynamicCast<RefExpr*>(node); re && re->GetTarget()) {
                    auto target = re->GetTarget();
                    CJC_NULLPTR_CHECK(target);
                    if (!IsClassOrEnumConstructor(*target) &&
                        (re->isThis || re->isSuper || IsInstanceMember(*target))) {
                        constThisOrSuper.emplace(re);
                    }
                }
                return VisitAction::WALK_CHILDREN;
            }).Walk();
        }
        bool res = true;
        CJC_ASSERT(fb.paramLists.size() == 1 && fb.paramLists.front());
        for (auto& param : fb.paramLists.front()->params) {
            if (param->assignment && param->desugarDecl) {
                // Default parameters should be `const`.
                bool isParamConst = ChkExpr(*param->assignment, true);
                param->desugarDecl->isConst = isParamConst;
                res = isParamConst && res;
            }
        }
        // The `body` of imported `FuncBody`s is null.
        // The correctness of the imported function was checked in the package that defines the function.
        if (fb.body) {
            res = ChkBlock(*fb.body, true) && res;
        }
        return res;
    }

    bool ChkFuncDecl(FuncDecl& fd)
    {
        if (!fd.isConst) {
            DiagExpectConstFunc(diag, fd);
            // Set `isConst` to be `true` to avoid chain of errors.
            fd.isConst = true;
            return false;
        }
        if (fd.TestAttr(Attribute::CONSTRUCTOR)) {
            inInit = true;
        }
        auto res = ChkFuncBody(*fd.funcBody);
        if (fd.TestAttr(Attribute::CONSTRUCTOR)) {
            inInit = false;
        }
        return res;
    }

    bool ChkDeclHasConstAllConstInitializers(const std::vector<OwnedPtr<Decl>>& decls)
    {
        auto res = true;
        for (auto& decl : decls) {
            if (auto vda = DynamicCast<VarDeclAbstract*>(decl.get());
                vda && vda->astKind != ASTKind::PROP_DECL && vda->initializer) {
                res = ChkExpr(*vda->initializer, false) && res;
            }
        }
        return res;
    }

    // Cannot define `const` function without `const init`.
    bool ChkDeclNoConstInitNoConstFunc(const std::vector<OwnedPtr<Decl>>& decls)
    {
        auto res = true;
        for (auto& decl : decls) {
            CJC_NULLPTR_CHECK(decl);
            // Cannot define `const` instance member function without `const init`.
            // But `const` static member function can be defined without `const init`.
            if (decl->IsConst() && decl->IsFunc() && !decl->TestAttr(Attribute::STATIC)) {
                DiagNoConstInit(diag, StaticCast<const FuncDecl&>(*decl));
                res = false;
            }
        }
        return res;
    }

    bool ChkStructDecl(const StructDecl& sd)
    {
        CJC_NULLPTR_CHECK(sd.body);
        const auto& decls = sd.body->decls;
        structOrClassMembers = CollectMembers(decls);
        bool res = true;
        for (auto& decl : decls) {
            CJC_NULLPTR_CHECK(decl);
            res = ChkAllDeclsByWalk(*decl) && res;
        }
        if (HasConstInit(decls)) {
            res = ChkDeclHasConstAllConstInitializers(decls) && res;
        } else {
            res = ChkDeclNoConstInitNoConstFunc(decls) && res;
        }
        structOrClassMembers.clear();
        return res;
    }

    bool ChkClassDeclHasConstInitNoVarMember(const ClassDecl& cd)
    {
        CJC_NULLPTR_CHECK(cd.body);
        std::vector<Ptr<VarDeclAbstract>> vars; // variables declared with `var`.
        for (auto& decl : cd.body->decls) {
            // Cannot define 'var' variables together with `const init`.
            // But `mut prop` can be defined with `const init`.
            if (auto vda = DynamicCast<VarDeclAbstract*>(decl.get());
                vda && vda->isVar && vda->astKind != ASTKind::PROP_DECL) {
                vars.emplace_back(vda);
            }
        }
        if (vars.empty()) {
            return true;
        }
        std::vector<Ptr<Decl>> constInits;
        for (auto& decl : cd.body->decls) {
            CJC_NULLPTR_CHECK(decl);
            // The constant constructor marked with `PRIMARY_CONSTRUCTOR` is compiler added.
            // We don't need them since the primary constructors should have been collected.
            if (decl->IsConst() && decl->TestAttr(Attribute::CONSTRUCTOR) &&
                !decl->TestAttr(Attribute::PRIMARY_CONSTRUCTOR)) {
                constInits.emplace_back(decl.get());
            }
        }
        DiagCannotDefineConstInit(diag, constInits, vars);
        return false;
    }

    bool ChkClassDecl(const ClassDecl& cd)
    {
        CJC_NULLPTR_CHECK(cd.body);
        const auto& decls = cd.body->decls;
        structOrClassMembers = CollectMembers(decls);
        bool res = true;
        for (auto& decl : decls) {
            CJC_NULLPTR_CHECK(decl);
            res = ChkAllDeclsByWalk(*decl) && res;
        }
        if (HasConstInit(decls)) {
            res = ChkClassDeclHasConstInitNoVarMember(cd) && res;
            res = ChkDeclHasConstAllConstInitializers(decls) && res;
        } else {
            res = ChkDeclNoConstInitNoConstFunc(decls) && res;
        }
        structOrClassMembers.clear();
        return res;
    }

    bool ChkExtendDecl(const ExtendDecl& ed)
    {
        auto res = true;
        CJC_NULLPTR_CHECK(ed.extendedType);
        for (auto& decl : ed.members) {
            CJC_NULLPTR_CHECK(decl);
            res = ChkAllDeclsByWalk(*decl) && res;
        }

        bool hasConstInit = true;
        Ptr<const Decl> target = Ty::GetDeclPtrOfTy(ed.extendedType->ty);
        if (target == nullptr) {
            // Do nothing..
        } else if (target->astKind == ASTKind::STRUCT_DECL) {
            const auto& sd = StaticCast<const StructDecl&>(*target);
            CJC_NULLPTR_CHECK(sd.body);
            hasConstInit = HasConstInit(sd.body->decls);
        } else if (target->astKind == ASTKind::CLASS_DECL) {
            const auto& cd = StaticCast<const ClassDecl&>(*target);
            CJC_NULLPTR_CHECK(cd.body);
            hasConstInit = HasConstInit(cd.body->decls);
        }
        if (!hasConstInit) {
            res = ChkDeclNoConstInitNoConstFunc(ed.members) && res;
        }
        return res;
    }

    bool ChkExpr(const Expr& expr, bool isWeak)
    {
        if (expr.TestAnyAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN)) {
            return false;
        }
        if (expr.desugarExpr) {
            return ChkDesugarExpr(expr, isWeak);
        }
        auto iter = dispatchTable.find(expr.astKind);
        if (iter == dispatchTable.cend()) {
            DiagExpectConstExpr(diag, expr, isWeak);
            return false;
        }
        return iter->second(expr, isWeak);
    }

    bool ChkDesugarExpr(const Expr& expr, bool isWeak)
    {
        CJC_NULLPTR_CHECK(expr.desugarExpr);
        if (IsStringBinaryExpr(expr)) {
            const CallExpr& ce = StaticCast<const CallExpr&>(*expr.desugarExpr);
            const MemberAccess& ma = StaticCast<const MemberAccess&>(*ce.baseFunc);
            CJC_NULLPTR_CHECK(ma.baseExpr);
            bool res = ChkExpr(*ma.baseExpr, isWeak);
            for (auto& arg : ce.args) {
                CJC_ASSERT(arg && arg->expr);
                res = ChkExpr(*arg->expr, isWeak) && res;
            }
            return res;
        }
        if (expr.astKind == ASTKind::INC_OR_DEC_EXPR || expr.astKind == ASTKind::TRAIL_CLOSURE_EXPR) {
            return ChkExpr(*expr.desugarExpr, isWeak);
        }
        // 1) array[0]++ is desugared to call
        // 2) binary/unary operator call is desuguared to call
        if (expr.desugarExpr->astKind == ASTKind::CALL_EXPR) {
            return ChkCallExpr(StaticCast<CallExpr>(*expr.desugarExpr), isWeak);
        }
        // multiple assignement expr is expanded into a block of multiple expressions
        if (expr.astKind == ASTKind::ASSIGN_EXPR) {
            return CheckDesugaredMultipleAssignment(StaticCast<Block>(*expr.desugarExpr), isWeak);
        }
        // Not all syntax sugars are valid constant expressions.
        DiagExpectConstExpr(diag, expr, isWeak);
        return false;
    }

    bool CheckDesugaredMultipleAssignment(const Block& e, bool isWeak)
    {
        bool res = true;
        for (auto& node : e.body) {
            if (auto decl = DynamicCast<VarDecl>(node.get())) {
                if (!ChkVarDecl(*decl)) {
                    res = false;
                }
            } else if (auto expr = DynamicCast<Expr>(node.get())) {
                if (!ChkExpr(*expr, isWeak)) {
                    res = false;
                }
            }
        }
        return res;
    }

    bool ChkLitConstExpr(const LitConstExpr& le)
    {
        if (!Ty::IsTyCorrect(le.ty)) {
            return false;
        }
        if (le.ty->IsStructArray()) {
            DiagExpectConstExpr(diag, le, true);
            return false;
        }
        if (le.siExpr) {
            DiagExpectConstExpr(diag, le, false);
            return false;
        }
        return true;
    }

    // Check the inner `expr` for `ReturnExpr`, `ParenExpr`, `UnaryExpr`.
    template <typename T> bool ChkInnerExpr(const T& expr, bool isWeak)
    {
        CJC_NULLPTR_CHECK(expr.expr);
        return ChkExpr(*expr.expr, isWeak);
    }

    // Check the `leftExpr` for `IsExpr`, `AsExpr`.
    template <typename T> bool ChkLeftExpr(const T& expr, bool isWeak)
    {
        CJC_NULLPTR_CHECK(expr.leftExpr);
        return ChkExpr(*expr.leftExpr, isWeak);
    }

    // Check the `children` for `ArrayLit` or `TupleLit`.
    template <typename T> bool ChkChildren(const T& expr, bool isWeak)
    {
        const std::vector<OwnedPtr<Expr>>& children = expr.children;
        bool res = true;
        for (auto& child : children) {
            CJC_NULLPTR_CHECK(child);
            if (!ChkExpr(*child, isWeak)) {
                res = false;
            }
        }
        return res;
    }

    bool ChkReturnExpr(const ReturnExpr& ret, bool isWeak)
    {
        if (Utils::In(Ptr(&StaticCast<const Node&>(ret)), inConstContext) &&
            !Utils::In(Ptr(StaticCast<const Node*>(ret.refFuncBody.get())), inConstContext)) {
            DiagExpectConstExpr(diag, ret, isWeak);
            return false;
        }
        return ChkInnerExpr(ret, isWeak);
    }

    bool ChkArrayLit(const ArrayLit& al, bool isWeak)
    {
        if (!Ty::IsTyCorrect(al.ty)) {
            return false;
        }
        if (al.ty->IsStructArray()) {
            DiagExpectConstExpr(diag, al, isWeak);
            return false;
        }
        return ChkChildren(al, isWeak);
    }

    bool ChkRefExpr(const RefExpr& re, bool isWeak)
    {
        Ptr<const Decl> target = re.GetTarget();
        if (target == nullptr) {
            // There are semantic errors, and these errors should have been reported. Just return false.
            return false;
        }
        // Target of expression 'this()' or 'super()' is a constructor.
        if (!IsClassOrEnumConstructor(*target) && (re.isThis || re.isSuper || IsInstanceMember(*target)) &&
            !Utils::In(&re, constThisOrSuper)) {
            DiagExpectConstExpr(diag, re, isWeak);
            return false;
        }
        // Target of a single 'this' or 'super' expression is a type declaration.
        if ((re.isThis || re.isSuper) && target->IsNominalDecl()) {
            return true;
        }
        // `enum` constructors, `const` variables are constant expressions.
        if (target->TestAttr(Attribute::ENUM_CONSTRUCTOR) || target->IsConst()) {
            return true;
        }
        // Member variable `x` is the syntax sugar of `this.x`.
        // Since `this` is a constant expression and member accesses of constant expressions are constant expressions.
        // Therefore, `this.x` is a constant expression. Thus `x` is a constant expression.
        if (Utils::In(target, structOrClassMembers)) {
            return true;
        }
        if (isWeak && Utils::In(target, inConstFunc)) {
            // Parameters and non-constant local variables of a constant function are "weak constant".
            return true;
        }
        DiagExpectConstExpr(diag, re, isWeak);
        return false;
    }

    bool ChkCallExpr(const CallExpr& ce, bool isWeak)
    {
        CJC_NULLPTR_CHECK(ce.baseFunc);
        if (!ChkExpr(*ce.baseFunc, isWeak)) {
            return false;
        }
        bool res = true;
        for (auto& arg : ce.args) {
            CJC_NULLPTR_CHECK(arg);
            CJC_NULLPTR_CHECK(arg->expr);
            res = ChkExpr(*arg->expr, isWeak) && res;
        }
        return res;
    }

    bool ChkLambdaExpr(const LambdaExpr& le)
    {
        CJC_NULLPTR_CHECK(le.funcBody);
        return ChkFuncBody(*le.funcBody);
    }

    bool ChkBinaryExpr(const BinaryExpr& be, bool isWeak)
    {
        CJC_NULLPTR_CHECK(be.leftExpr);
        CJC_NULLPTR_CHECK(be.rightExpr);
        bool res = ChkExpr(*be.leftExpr, isWeak);
        res = ChkExpr(*be.rightExpr, isWeak) && res;
        return res;
    }

    bool ChkIfExpr(const IfExpr& ie, bool isWeak)
    {
        CJC_NULLPTR_CHECK(ie.condExpr);
        CJC_NULLPTR_CHECK(ie.thenBody);
        bool res = ChkExpr(*ie.condExpr, isWeak);
        res = ChkBlock(*ie.thenBody, isWeak) && res;
        if (ie.elseBody) {
            res = ChkExpr(*ie.elseBody, isWeak) && res;
        }
        return res;
    }

    bool ChkMatchCase(const MatchCase& mc, bool isWeak)
    {
        if (mc.TestAnyAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN)) {
            return false;
        }
        bool res = true;
        if (mc.patternGuard) {
            res = ChkExpr(*mc.patternGuard, isWeak) && res;
        }
        CJC_NULLPTR_CHECK(mc.exprOrDecls);
        res = ChkBlock(*mc.exprOrDecls, isWeak) && res;
        return res;
    }

    bool ChkMatchCaseOther(const MatchCaseOther& mco, bool isWeak)
    {
        if (mco.TestAnyAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN)) {
            return false;
        }
        CJC_NULLPTR_CHECK(mco.matchExpr);
        CJC_NULLPTR_CHECK(mco.exprOrDecls);
        bool res = true;
        if (auto expr = DynamicCast<Expr*>(mco.matchExpr.get()); expr) {
            res = ChkExpr(*expr, isWeak) && res;
        }
        res = ChkBlock(*mco.exprOrDecls, isWeak) && res;
        return res;
    }

    // `match` expression with or without `selector` are both checked by this function.
    // We don't have to distinguish the semantics for `const` checking.
    bool ChkMatchExpr(const MatchExpr& me, bool isWeak)
    {
        bool res = true;
        if (me.selector) {
            res = ChkExpr(*me.selector, isWeak) && res;
        }
        for (auto& mc : me.matchCases) {
            CJC_NULLPTR_CHECK(mc);
            res = ChkMatchCase(*mc, isWeak) && res;
        }
        for (auto& mco : me.matchCaseOthers) {
            CJC_NULLPTR_CHECK(mco);
            res = ChkMatchCaseOther(*mco, isWeak) && res;
        }
        return res;
    }

    bool ChkTryExpr(const TryExpr& te, bool isWeak)
    {
        if (te.isDesugaredFromTryWithResources) {
            DiagExpectConstExpr(diag, te, isWeak);
            return false;
        }
        CJC_NULLPTR_CHECK(te.tryBlock);
        bool res = ChkBlock(*te.tryBlock, isWeak);
        for (auto& cb : te.catchBlocks) {
            CJC_NULLPTR_CHECK(cb);
            res = ChkBlock(*cb, isWeak) && res;
        }
        if (te.finallyBlock) {
            res = ChkBlock(*te.finallyBlock, isWeak) && res;
        }
        return res;
    }

    bool ChkMemberAccess(const MemberAccess& ma, bool isWeak)
    {
        CJC_NULLPTR_CHECK(ma.baseExpr);
        Ptr<const Decl> target = ma.GetTarget();
        if (target == nullptr) {
            // There are semantic errors, and these errors should have been reported. Just return false.
            return false;
        }
        Ptr<const Decl> baseTarget = ma.baseExpr->GetTarget();
        // Global/static `const` variables, and `enum` constructors are constant.
        if (baseTarget && (baseTarget->astKind == ASTKind::PACKAGE_DECL || baseTarget->IsTypeDecl()) &&
            ((target->IsConst() && (IsInstanceConstructor(*target) || target->IsStaticOrGlobal())) ||
                target->TestAttr(Attribute::ENUM_CONSTRUCTOR))) {
            return true;
        }
        if (ChkExpr(*ma.baseExpr, isWeak)) {
            // Member variables and constant member functions of constant expressions are constant.
            if (target->TestAttr(Attribute::ENUM_CONSTRUCTOR) || target->astKind == ASTKind::VAR_DECL ||
                (target->astKind == ASTKind::FUNC_DECL && target->IsConst())) {
                return true;
            }
        }
        DiagExpectConstExpr(diag, ma, isWeak);
        return false;
    }

    bool ChkSubscriptExpr(const SubscriptExpr& se, bool isWeak)
    {
        CJC_NULLPTR_CHECK(se.baseExpr);
        if (!Ty::IsTyCorrect(se.baseExpr->ty)) {
            // There are semantic errors, and these errors should have been reported. Just return false.
            return false;
        }
        if (!se.baseExpr->ty->IsTuple() && !Is<VArrayTy*>(se.baseExpr->ty)) {
            DiagExpectConstExpr(diag, se, isWeak);
            return false;
        }
        if (!ChkExpr(*se.baseExpr, isWeak)) {
            return false;
        }
        bool res = true;
        for (auto& idx : se.indexExprs) {
            CJC_NULLPTR_CHECK(idx);
            res = ChkExpr(*idx, isWeak) && res;
        }
        return res;
    }

    bool ChkBlock(const Block& block, bool isWeak)
    {
        if (block.TestAttr(Attribute::UNSAFE)) {
            DiagExpectConstExpr(diag, block, isWeak);
            return false;
        }
        bool res = true;
        for (auto& node : block.body) {
            CJC_NULLPTR_CHECK(node);
            if (auto decl = DynamicCast<Decl*>(node.get()); decl) {
                res = ChkConstDecl(*decl) && res;
            } else if (auto expr = DynamicCast<Expr*>(node.get()); expr) {
                res = ChkExpr(*expr, isWeak) && res;
            } else {
                CJC_ABORT();
            }
        }
        return res;
    }

    bool ChkAssignExpr(const AssignExpr& ae, bool isWeak)
    {
        CJC_NULLPTR_CHECK(ae.leftValue);
        CJC_NULLPTR_CHECK(ae.rightExpr);
        if (inInit && Utils::In<Ptr<const Decl>>(ae.leftValue->GetTarget(), structOrClassMembers)) {
            return ChkExpr(*ae.rightExpr, isWeak);
        }
        DiagExpectConstExpr(diag, ae, isWeak);
        return false;
    }

    bool ChkIncOrDecExpr(const IncOrDecExpr& ide)
    {
        CJC_NULLPTR_CHECK(ide.expr);
        if (inInit && Utils::In<Ptr<const Decl>>(ide.expr->GetTarget(), structOrClassMembers)) {
            return true;
        }
        DiagExpectConstExpr(diag, ide, true);
        return false;
    }

    template <typename T>
    std::function<bool(const Expr&, bool)> Proxy(bool (ConstEvaluationChecker::*chk)(const T&, bool))
    {
        return [this, chk](const Expr& expr, bool isWeak) { return (this->*chk)(StaticCast<const T&>(expr), isWeak); };
    }

    template <typename T> std::function<bool(const Expr&, bool)> Proxy(bool (ConstEvaluationChecker::*chk)(const T&))
    {
        return [this, chk](const Expr& expr, bool) { return (this->*chk)(StaticCast<const T&>(expr)); };
    }

    std::unordered_map<ASTKind, std::function<bool(const Expr&, bool)>> dispatchTable = {
        {ASTKind::WILDCARD_EXPR, [](const Expr&, bool) { return true; }},
        {ASTKind::LIT_CONST_EXPR, Proxy(&ConstEvaluationChecker::ChkLitConstExpr)},
        {ASTKind::RETURN_EXPR, Proxy(&ConstEvaluationChecker::ChkReturnExpr)},
        {ASTKind::THROW_EXPR, Proxy(&ConstEvaluationChecker::ChkInnerExpr<const ThrowExpr&>)},
        {ASTKind::PERFORM_EXPR, Proxy(&ConstEvaluationChecker::ChkInnerExpr<const PerformExpr&>)},
        {ASTKind::PAREN_EXPR, Proxy(&ConstEvaluationChecker::ChkInnerExpr<const ParenExpr&>)},
        {ASTKind::UNARY_EXPR, Proxy(&ConstEvaluationChecker::ChkInnerExpr<const UnaryExpr&>)},
        {ASTKind::IS_EXPR, Proxy(&ConstEvaluationChecker::ChkLeftExpr<const IsExpr&>)},
        {ASTKind::AS_EXPR, Proxy(&ConstEvaluationChecker::ChkLeftExpr<const AsExpr&>)},
        {ASTKind::ARRAY_LIT, Proxy(&ConstEvaluationChecker::ChkArrayLit)},
        {ASTKind::TUPLE_LIT, Proxy(&ConstEvaluationChecker::ChkChildren<const TupleLit&>)},
        {ASTKind::REF_EXPR, Proxy(&ConstEvaluationChecker::ChkRefExpr)},
        {ASTKind::CALL_EXPR, Proxy(&ConstEvaluationChecker::ChkCallExpr)},
        {ASTKind::BINARY_EXPR, Proxy(&ConstEvaluationChecker::ChkBinaryExpr)},
        {ASTKind::IF_EXPR, Proxy(&ConstEvaluationChecker::ChkIfExpr)},
        {ASTKind::MATCH_EXPR, Proxy(&ConstEvaluationChecker::ChkMatchExpr)},
        {ASTKind::TRY_EXPR, Proxy(&ConstEvaluationChecker::ChkTryExpr)},
        {ASTKind::MEMBER_ACCESS, Proxy(&ConstEvaluationChecker::ChkMemberAccess)},
        {ASTKind::SUBSCRIPT_EXPR, Proxy(&ConstEvaluationChecker::ChkSubscriptExpr)},
        {ASTKind::LAMBDA_EXPR, Proxy(&ConstEvaluationChecker::ChkLambdaExpr)},
        {ASTKind::BLOCK, Proxy(&ConstEvaluationChecker::ChkBlock)},
        {ASTKind::ASSIGN_EXPR, Proxy(&ConstEvaluationChecker::ChkAssignExpr)},
        {ASTKind::INC_OR_DEC_EXPR, Proxy(&ConstEvaluationChecker::ChkIncOrDecExpr)},
    };

    DiagnosticEngine& diag;
    Package& pkg;
    // Context states:
    std::unordered_set<Ptr<const Node>>
        inConstContext; // nodes in const contexts, contains `FuncBody`, `ReturnExpr` and `VarDeclAbstract`
    std::unordered_set<Ptr<const Decl>> inConstFunc;          // local variables of const function
    std::unordered_set<Ptr<const RefExpr>> constThisOrSuper;  // `this` and `super` that are `const`
    std::unordered_set<Ptr<const Decl>> structOrClassMembers; // member variables of a struct/class
    bool inInit = false;                                      // inside a constructor
};

void TypeChecker::TypeCheckerImpl::CheckConstEvaluation(Package& pkg)
{
    ConstEvaluationChecker(diag, pkg).Check();
}
} // namespace Cangjie
