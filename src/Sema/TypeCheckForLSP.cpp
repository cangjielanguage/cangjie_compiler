// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the TypeChecker related api for lsp.
 */

#include "TypeCheckerImpl.h"

#include <limits>

#include "DiagSuppressor.h"
#include "JoinAndMeet.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;

namespace Cangjie {
template <typename T> struct RetValue {
    using Type = T;
    T value;
    template <typename... Args> explicit RetValue(Args&&... args) : value(std::forward<Args>(args)...)
    {
    }
};

/**
 * A base class to restrict the valid type of nodes in expression chain which supporting dot completion of LSP.
 * Using all pure virtual function for AST nodes to restrict sub-class defining all related behavior.
 */
template <typename RetT> class RefNodeWalker {
public:
    typename RetT::Type operator()()
    {
        auto ret = VisitNode(expr);
        return ret.value;
    }
    virtual ~RefNodeWalker() = default;

protected:
    explicit RefNodeWalker(Expr& expr) : expr(&expr) {};
    virtual RetT Visit(RefExpr& re) = 0;
    virtual RetT Visit(MemberAccess& ma) = 0;
    virtual RetT Visit(PrimitiveTypeExpr& pte) = 0;
    virtual RetT Visit(LitConstExpr& lce) = 0;
    virtual RetT Visit(CallExpr& ce) = 0;
    virtual RetT Visit(SubscriptExpr& se) = 0;
    virtual RetT Visit(OptionalExpr& oe) = 0;
    virtual RetT Visit(TrailingClosureExpr& tce) = 0;
    virtual RetT Visit(ArrayLit& al) = 0;

    RetT Visit(const OptionalChainExpr& oce)
    {
        // 'OptionalChainExpr' is only a wrapper node, should not be implemented by sub-class.
        // NOTE: 'Synthesizer' is used for LSP dot completion, so for code like 'a?.' or 'a?b.'
        // the wrapped 'OptionalChainExpr' will not be counted as part of base expression.
        // This type of expression is actually should not existing.
        return VisitNode(oce.expr.get());
    }

    RetT VisitNode(Ptr<Node> node)
    {
        ASTKind kind = node ? node->astKind : ASTKind::INVALID_EXPR;
        switch (kind) {
            case ASTKind::REF_EXPR:
                return Visit(*StaticAs<ASTKind::REF_EXPR>(node));
            case ASTKind::MEMBER_ACCESS:
                return Visit(*StaticAs<ASTKind::MEMBER_ACCESS>(node));
            case ASTKind::PRIMITIVE_TYPE_EXPR:
                return Visit(*StaticAs<ASTKind::PRIMITIVE_TYPE_EXPR>(node));
            case ASTKind::LIT_CONST_EXPR:
                return Visit(*StaticAs<ASTKind::LIT_CONST_EXPR>(node));
            case ASTKind::CALL_EXPR:
                return Visit(*StaticAs<ASTKind::CALL_EXPR>(node));
            case ASTKind::SUBSCRIPT_EXPR:
                return Visit(*StaticAs<ASTKind::SUBSCRIPT_EXPR>(node));
            case ASTKind::OPTIONAL_CHAIN_EXPR:
                return Visit(*StaticAs<ASTKind::OPTIONAL_CHAIN_EXPR>(node));
            case ASTKind::OPTIONAL_EXPR:
                return Visit(*StaticAs<ASTKind::OPTIONAL_EXPR>(node));
            case ASTKind::TRAIL_CLOSURE_EXPR:
                return Visit(*StaticAs<ASTKind::TRAIL_CLOSURE_EXPR>(node));
            case ASTKind::ARRAY_LIT:
                return Visit(*StaticAs<ASTKind::ARRAY_LIT>(node));
            default:
                return RetT();
        }
    }

    Ptr<Expr> const expr;
};

/**
 * Get the base reference name of given expression.
 * eg: 'a.b.c' -- return 'a'
 *     'a().b.c' or 'a[0].b.c' -- return 'a'
 * NOTE: if the base expression is not name reference, return empty string.
 *       eg: if (true) {1} else {0}.toString() -- return empty string.
 */
using BaseName = RetValue<std::string>;
class BaseNameFinder : public RefNodeWalker<BaseName> {
public:
    explicit BaseNameFinder(Expr& expr) : RefNodeWalker(expr)
    {
    }
    ~BaseNameFinder() override = default;

protected:
    BaseName Visit(RefExpr& re) override
    {
        return BaseName(re.ref.identifier);
    }

    BaseName Visit(MemberAccess& ma) override
    {
        return VisitNode(ma.baseExpr.get());
    }

    BaseName Visit(PrimitiveTypeExpr& pte) override
    {
        // Just for placeholder, indicate current expr is also an alternative type of name reference.
        (void)pte;
        return BaseName("$type");
    }

    BaseName Visit(LitConstExpr& lce) override
    {
        // Consistent with 'CollectLitConstExpr'.
        if (lce.kind == LitConstKind::STRING) {
            return BaseName("String");
        } else if (lce.kind == LitConstKind::JSTRING) {
            return BaseName("JString");
        }
        // Can't be empty.
        return BaseName(lce.stringValue);
    }

    BaseName Visit(CallExpr& ce) override
    {
        return VisitNode(ce.baseFunc.get());
    }

    BaseName Visit(SubscriptExpr& se) override
    {
        return VisitNode(se.baseExpr.get());
    }

    BaseName Visit(OptionalExpr& oe) override
    {
        return VisitNode(oe.baseExpr.get());
    }

    BaseName Visit(TrailingClosureExpr& tce) override
    {
        return VisitNode(tce.expr.get());
    }

    BaseName Visit(ArrayLit& al) override
    {
        std::stringstream arrayStr;
        arrayStr << "[";
        for (auto& child : al.children) {
            arrayStr << VisitNode(child.get()).value;
            if (child != al.children.back()) {
                arrayStr << ", ";
            }
        }
        arrayStr << "]";
        return BaseName(arrayStr.str());
    }
};

/**
 * Set the scopeName for expressions in whitelist.
 */
using EmptyRet = RetValue<uint8_t>;
class ScopeNameSetter : public RefNodeWalker<EmptyRet> {
public:
    ScopeNameSetter(Expr& expr, const std::string& scopeName) : RefNodeWalker(expr), scopeName(scopeName)
    {
    }
    ~ScopeNameSetter() override = default;

protected:
    EmptyRet Visit(RefExpr& re) override
    {
        re.scopeName = scopeName;
        for (auto& type : re.typeArguments) {
            Walker(type.get(), [this](auto node) {
                node->scopeName = scopeName;
                return VisitAction::WALK_CHILDREN;
            }).Walk();
        }
        return EmptyRet();
    }

    EmptyRet Visit(MemberAccess& ma) override
    {
        ma.scopeName = scopeName;
        for (auto& type : ma.typeArguments) {
            Walker(type.get(), [this](auto node) {
                node->scopeName = scopeName;
                return VisitAction::WALK_CHILDREN;
            }).Walk();
        }
        return VisitNode(ma.baseExpr.get());
    }

    EmptyRet Visit(PrimitiveTypeExpr& pte) override
    {
        // ScopeName can be omitted for 'PrimitiveTypeExpr'.
        (void)pte;
        return EmptyRet();
    }

    EmptyRet Visit(LitConstExpr& lce) override
    {
        // ScopeName can be omitted for 'LitConstExpr'.
        (void)lce;
        return EmptyRet();
    }

    EmptyRet Visit(CallExpr& ce) override
    {
        ce.scopeName = scopeName;
        return VisitNode(ce.baseFunc.get());
    }

    EmptyRet Visit(SubscriptExpr& se) override
    {
        se.scopeName = scopeName;
        return VisitNode(se.baseExpr.get());
    }

    EmptyRet Visit(OptionalExpr& oe) override
    {
        // ScopeName can be omitted for 'OptionalExpr'.
        return VisitNode(oe.baseExpr.get());
    }

    EmptyRet Visit(TrailingClosureExpr& tce) override
    {
        // ScopeName can be omitted for 'TrailingClosureExpr'.
        return VisitNode(tce.expr.get());
    }

    EmptyRet Visit(ArrayLit& al) override
    {
        al.scopeName = scopeName;
        for (auto& child : al.children) {
            VisitNode(child.get());
        }
        return EmptyRet();
    }

private:
    std::string scopeName;
};

/**
 * Get the base reference name of given expression.
 * eg: 'a.b.c' -- return 'a'
 *     'a().b.c' or 'a[0].b.c' -- return 'a'
 * NOTE: if the base expression is not name reference, return empty string.
 *       eg: if (true) {1} else {0}.toString() -- return empty string.
 *       This should be a friend class of TypeChecker.
 */
using SynResult = RetValue<Candidate>;
class TypeChecker::Synthesizer : public RefNodeWalker<SynResult> {
public:
    Synthesizer(TypeChecker::TypeCheckerImpl& typeChecker, ASTContext& ctx, Expr& expr, const Position& pos)
        : RefNodeWalker(expr), checker(typeChecker), ctx(ctx), pos(pos)
    {
    }
    ~Synthesizer() override = default;

protected:
    SynResult Visit(RefExpr& re) override
    {
        re.begin = pos; // Update position for declaration founding.
        // Reference checking requires the validation of 'typeArguments'.
        auto instTys = checker.GetTyFromASTType(ctx, re.typeArguments);
        if (ctxExprs.empty()) {
            checker.InferRefExpr(ctx, re);
            if (Ty::IsTyCorrect(re.ty) && (re.isThis || re.isSuper)) {
                std::unordered_set<Ptr<Ty>> tys{re.ty};
                return SynResult(tys);
            }
        } else {
            auto parentExpr = ctxExprs.top();
            re.isAlone = false;
            re.callOrPattern = DynamicCast<CallExpr*>(parentExpr);
            if (parentExpr->IsReferenceExpr()) {
                return SynResult(re.ref.targets);
            }
            checker.InferRefExpr(ctx, re);
            if (!instTys.empty() && !re.ref.targets.empty()) {
                std::unordered_set<Ptr<Ty>> tys;
                for (auto decl : re.ref.targets) {
                    auto typeMapping = TypeCheckUtil::GenerateTypeMapping(*decl, instTys);
                    tys.emplace(checker.typeManager.GetInstantiatedTy(decl->ty, typeMapping));
                }
                // Update current decl status for parent expr when return type result.
                wasTypeCall = re.callOrPattern && re.ref.target && re.ref.target->IsTypeDecl();
                return SynResult(tys);
            }
        }
        return SynResult(re.ref.targets);
    }

    // Convert decls to tys with type instantiation.
    std::unordered_set<Ptr<Ty>> GetCandidateTysForMemberAccess(
        Ty& baseTy, const std::vector<Ptr<AST::Ty>>& typeArgs, const std::vector<Ptr<Decl>>& candidates)
    {
        MultiTypeSubst mts;
        checker.typeManager.GenerateGenericMapping(mts, baseTy);
        auto baseMapping = TypeCheckUtil::MultiTypeSubstToTypeSubst(mts);
        std::unordered_set<Ptr<Ty>> tys;
        for (auto decl : candidates) {
            auto typeMapping = TypeCheckUtil::GenerateTypeMapping(*decl, typeArgs);
            typeMapping.insert(baseMapping.begin(), baseMapping.end());
            auto instTy = checker.typeManager.GetInstantiatedTy(decl->ty, typeMapping);
            if (auto fTy = DynamicCast<FuncTy>(instTy); fTy && Is<ClassThisTy>(fTy->retTy)) {
                instTy = checker.typeManager.GetFunctionTy(fTy->paramTys, &baseTy);
            }
            tys.emplace(instTy);
        }
        return tys;
    }

    Candidate GetCandidateMembers(const std::unordered_set<Ptr<Ty>>& tys, const std::string& field, Ptr<File> curFile)
    {
        std::unordered_set<Ptr<Ty>> resultTys;
        auto ma = CreateMemberAccess(CreateRefExpr("$dummy"), field);
        // Avoid diagnostic engine to throw internal error.
        ma->begin = DEFAULT_POSITION;
        ma->end = DEFAULT_POSITION;
        AddCurFile(*ma, curFile);
        for (auto ty : tys) {
            std::vector<Ptr<Decl>> curTargets;
            ma->baseExpr->ty = ty;
            ma->targets.clear();
            // Set dummy target type to skip inference checking.
            ctx.targetTypeMap[ma.get()] = ty;
            auto target = checker.GetObjMemberAccessTarget(ctx, *ma, *ty);
            target && ma->targets.empty() ? curTargets.insert(curTargets.end(), target)
                                          : curTargets.insert(curTargets.end(), ma->targets.begin(), ma->targets.end());
            // Convert decls to tys for further non-reference synthesizing.
            auto foundTys = GetCandidateTysForMemberAccess(*ty, {}, curTargets);
            resultTys.merge(foundTys);
        }
        return Candidate(resultTys);
    }

    SynResult Visit(MemberAccess& ma) override
    {
        Ptr<Expr> parentExpr = nullptr;
        if (!ctxExprs.empty()) {
            parentExpr = ctxExprs.top();
            ma.isAlone = false;
            ma.callOrPattern = DynamicCast<CallExpr*>(parentExpr);
        }
        SynCtx synCtx(this, &ma);
        auto base = VisitNode(ma.baseExpr.get());
        // Set type of 'typeArguments' if existed.
        std::vector<Ptr<AST::Ty>> instTys = checker.GetTyFromASTType(ctx, ma.typeArguments);
        // If baseExpr is combined with pure references, decide whether syn current ma with parentExpr state.
        // a.b  or  a().b
        if (wasPureReference) {
            std::vector<Ptr<Decl>> candidates;
            // If parent expression is not reference expression, synthesize current expression's target.
            if (parentExpr == nullptr || !parentExpr->IsReferenceExpr()) {
                checker.InferMemberAccess(ctx, ma);
            }
            (ma.target && ma.targets.empty())
                ? candidates.insert(candidates.end(), ma.target)
                : candidates.insert(candidates.end(), ma.targets.begin(), ma.targets.end());
            CJC_NULLPTR_CHECK(ma.baseExpr);
            // If the 'parentExpr' existed and the 'candidates' is not empty, convert result to tys.
            if (parentExpr != nullptr && !candidates.empty() && ma.baseExpr->ty) {
                // Update current decl status for parent expr when return type result.
                wasTypeCall = ma.callOrPattern && ma.target && ma.target->IsTypeDecl();
                auto tys = GetCandidateTysForMemberAccess(*ma.baseExpr->ty, instTys, candidates);
                return SynResult(tys);
            }
            return SynResult(candidates);
        }
        std::unordered_set<Ptr<Ty>> tys;
        if (base.value.hasDecl) {
            // Field accessing like 'call().a.b'
            for (auto it : base.value.decls) {
                if (Ty::IsTyCorrect(it->ty)) {
                    tys.emplace(it->ty);
                }
            }
        } else {
            // Field accessing like 'call().a().b'
            tys = base.value.tys;
        }
        auto result = GetCandidateMembers(tys, ma.field, ma.curFile);
        return SynResult(result);
    }

    SynResult Visit(PrimitiveTypeExpr& pte) override
    {
        // Another kind of reference, only valid for base of MemberAccess.
        // If ctx is empty, return primitive type, otherwise nothing to do here.
        std::unordered_set<Ptr<Ty>> tys;
        if (ctxExprs.empty()) {
            tys.emplace(TypeManager::GetPrimitiveTy(pte.typeKind));
        }
        return SynResult(tys);
    }

    SynResult Visit(LitConstExpr& lce) override
    {
        Ptr<Ty> ty = TypeManager::GetInvalidTy();
        if (lce.siExpr || lce.kind == LitConstKind::STRING) {
            auto stringDecl = checker.importManager.GetCoreDecl<InheritableDecl>(STD_LIB_STRING);
            if (stringDecl) {
                ty = stringDecl->ty;
            }
        } else {
            ty = checker.SynLitConstExpr(ctx, lce);
        }
        std::unordered_set<Ptr<Ty>> tys;
        if (Ty::IsTyCorrect(ty)) {
            tys.emplace(ty);
        }
        return SynResult(tys);
    }

    std::unordered_set<Ptr<Ty>> GetCandidateCallResults(const std::unordered_set<Ptr<Ty>>& tys,
        const std::string& field, Ptr<File> curFile, bool isInTrailingClosure = false)
    {
        std::unordered_set<Ptr<Ty>> results;
        auto candidates = GetCandidateMembers(tys, field, curFile);
        CJC_ASSERT(!candidates.hasDecl);
        for (auto ty : candidates.tys) {
            if (auto fTy = DynamicCast<FuncTy*>(ty);
                fTy && (!isInTrailingClosure || IsViableTypeForTrailingClosure(*fTy))) {
                results.emplace(fTy->retTy);
            }
        }
        return results;
    }

    SynResult Visit(CallExpr& ce) override
    {
        bool isInTrailingClosure = !ctxExprs.empty() && ctxExprs.top()->astKind == ASTKind::TRAIL_CLOSURE_EXPR;
        std::unordered_set<Ptr<Ty>> tys;
        SynCtx synCtx(this, &ce);
        auto base = VisitNode(ce.baseFunc.get());
        // NOTE: will not filter candidate by arguments, since the user may not given correct arguments.
        if (base.value.hasDecl) {
            std::unordered_set<Ptr<Ty>> varTys;
            for (auto decl : base.value.decls) {
                if (!decl || !Ty::IsTyCorrect(decl->ty)) {
                    continue;
                }
                if (decl->IsTypeDecl()) {
                    // Handle case of constructor call.
                    tys.emplace(decl->ty);
                } else if (auto fTy = DynamicCast<FuncTy*>(decl->ty)) {
                    if (!isInTrailingClosure || IsViableDeclForTrailingClosure(*decl, *fTy)) {
                        tys.emplace(fTy->retTy);
                    }
                } else {
                    varTys.emplace(decl->ty);
                }
            }
            // Handle case of operator '()' overloading.
            tys.merge(GetCandidateCallResults(varTys, "()", ce.curFile, isInTrailingClosure));
        } else {
            // For 'this()', 'super()'.
            if (ce.baseFunc && IsThisOrSuper(*ce.baseFunc)) {
                return SynResult(base.value.tys);
            }
            // eg: funcCall()(), calling returned value of another function call.
            std::unordered_set<Ptr<Ty>> varTys;
            for (auto ty : base.value.tys) {
                if (wasTypeCall) {
                    // Handle case of constructor call.
                    tys.emplace(ty);
                    wasTypeCall = false;
                } else if (auto fTy = DynamicCast<FuncTy*>(ty)) {
                    if (!isInTrailingClosure || IsViableTypeForTrailingClosure(*fTy)) {
                        tys.emplace(fTy->retTy);
                    }
                } else {
                    varTys.emplace(ty);
                }
            }
            // Handle case of operator '()' overloading.
            tys.merge(GetCandidateCallResults(varTys, "()", ce.curFile, isInTrailingClosure));
        }
        return SynResult(tys);
    }

    std::unordered_set<Ptr<Ty>> GetTupleAccessTys(const std::unordered_set<Ptr<Ty>>& tupleTys, Expr& index)
    {
        // Tuple can only accessed by numeric literal.
        std::unordered_set<Ptr<Ty>> tys;
        if (auto lce = DynamicCast<LitConstExpr*>(&index); lce && lce->kind == LitConstKind::INTEGER) {
            auto indexTy = checker.SynLitConstExpr(ctx, *lce);
            if (!indexTy->IsInteger() || lce->constNumValue.asInt.IsOutOfRange() ||
                lce->constNumValue.asInt.IsNegativeNum()) {
                return tys;
            }
            size_t idx = lce->constNumValue.asInt.Uint64();
            for (auto ty : tupleTys) {
                if (idx < ty->typeArgs.size()) {
                    tys.emplace(ty->typeArgs[idx]);
                }
            }
        }
        return tys;
    }

    SynResult Visit(SubscriptExpr& se) override
    {
        SynCtx synCtx(this, &se);
        auto base = VisitNode(se.baseExpr.get());
        std::unordered_set<Ptr<Ty>> baseTys;
        std::unordered_set<Ptr<Ty>> tupleTys;
        if (base.value.hasDecl) {
            // eg: a[0] -- only can be tuple or user defined type vardecl.
            for (auto decl : base.value.decls) {
                if (decl && Ty::IsTyCorrect(decl->ty)) {
                    decl->ty->IsTuple() ? tupleTys.emplace(decl->ty) : baseTys.emplace(decl->ty);
                }
            }
        } else {
            // eg: a[0][1] -- tuple or user defined type.
            for (auto ty : base.value.tys) {
                ty->IsTuple() ? tupleTys.emplace(ty) : baseTys.emplace(ty);
            }
        }
        auto tys = GetCandidateCallResults(baseTys, "[]", se.curFile);
        if (!se.indexExprs.empty() && se.indexExprs[0] != nullptr) {
            tys.merge(GetTupleAccessTys(tupleTys, *se.indexExprs[0]));
        }
        return SynResult(tys);
    }

    SynResult Visit(OptionalExpr& oe) override
    {
        std::unordered_set<Ptr<Ty>> tys;
        SynCtx synCtx(this, &oe);
        auto base = VisitNode(oe.baseExpr.get());
        if (base.value.hasDecl) {
            for (auto decl : base.value.decls) {
                if (decl && Ty::IsTyCorrect(decl->ty)) {
                    tys.emplace(decl->ty);
                }
            }
        } else {
            tys = base.value.tys;
        }
        // Unpack option type.
        std::unordered_set<Ptr<Ty>> results;
        for (auto ty : tys) {
            if (ty->IsCoreOptionType()) {
                results.emplace(ty->typeArgs[0]);
            }
        }
        return SynResult(results);
    }

    static bool IsViableDeclForTrailingClosure(const Decl& decl, const FuncTy& funcTy)
    {
        if (auto fd = DynamicCast<FuncDecl>(&decl)) {
            CJC_ASSERT(fd->funcBody && !fd->funcBody->paramLists.empty());
            Ptr<Decl> lastTrivialParam = nullptr;
            // Only function whose last unnamed parameter has a function type can be used for trailing closure.
            for (auto& param : fd->funcBody->paramLists[0]->params) {
                if (param->isNamedParam) {
                    break;
                }
                lastTrivialParam = param.get();
                CJC_NULLPTR_CHECK(lastTrivialParam->ty);
            }
            return lastTrivialParam && lastTrivialParam->ty->IsFunc();
        }
        return IsViableTypeForTrailingClosure(funcTy);
    }

    static bool IsViableTypeForTrailingClosure(const FuncTy& funcTy)
    {
        // To be used in trailing closure, there must exist a function type of paramTy.
        // NOTE: Fuzzy checking, do not restrict the funcTy be last param type.
        return !funcTy.paramTys.empty() && Utils::In(funcTy.paramTys, [](auto& it) { return it->IsFunc(); });
    }

    SynResult Visit(TrailingClosureExpr& tce) override
    {
        SynCtx synCtx(this, &tce);
        CJC_NULLPTR_CHECK(tce.expr);
        if (tce.expr->astKind != ASTKind::CALL_EXPR) {
            auto tmpCall = CreateCallExpr(std::move(tce.expr), {});
            return VisitNode(tmpCall.get());
        }
        return VisitNode(tce.expr.get());
    }

    SynResult Visit(ArrayLit& al) override
    {
        std::unordered_set<Ptr<Ty>> tys;
        std::set<Ptr<Ty>> arrayElemTys;
        for (auto& child : al.children) {
            auto childResult = VisitNode(child.get()).value;
            Ptr<Ty> childTy = TypeManager::GetInvalidTy();
            if (childResult.hasDecl || !childResult.tys.empty()) {
                childTy = childResult.hasDecl ? childResult.decls[0]->ty : *childResult.tys.begin();
            }
            if (!Ty::IsTyCorrect(childTy)) {
                tys.emplace(TypeManager::GetInvalidTy());
                return SynResult(tys);
            }
            arrayElemTys.insert(childTy);
        }
        auto arrayStruct = checker.importManager.GetCoreDecl<StructDecl>("Array");
        if (arrayElemTys.empty() || arrayStruct == nullptr) {
            tys.emplace(TypeManager::GetInvalidTy());
            return SynResult(tys);
        }
        auto joinRes =
            JoinAndMeet(checker.typeManager, arrayElemTys, {}, &checker.importManager, al.curFile).JoinAsVisibleTy();
        if (auto ty = std::get_if<Ptr<Ty>>(&joinRes)) {
            al.ty = checker.typeManager.GetStructTy(*arrayStruct, {*ty});
        } else {
            tys.emplace(TypeManager::GetInvalidTy());
            return SynResult(tys);
        }
        tys.emplace(al.ty);
        return SynResult(tys);
    }

    class SynCtx {
    public:
        SynCtx(Synthesizer* syn, Ptr<Expr> current) : synthesizer(syn)
        {
            synthesizer->ctxExprs.push(current);
        }
        ~SynCtx() noexcept
        {
#ifndef CANGJIE_ENABLE_GCOV
            try {
#endif
                if (synthesizer->wasPureReference) {
                    synthesizer->wasPureReference = synthesizer->ctxExprs.top()->IsReferenceExpr();
                }
                synthesizer->ctxExprs.pop();
                synthesizer = nullptr;
#ifndef CANGJIE_ENABLE_GCOV
            } catch (...) {
                // Avoid using exceptions in destructors.
            }
#endif
        }

    private:
        Synthesizer* synthesizer;
    };

private:
    TypeChecker::TypeCheckerImpl& checker;
    ASTContext& ctx;
    Position pos;
    std::stack<Ptr<Expr>> ctxExprs;
    // Checking is performed from inner to outer, this status is also updated from child to parent.
    // Whether the previous children is in form "a" or "a.b" "a.b.c".
    bool wasPureReference = true;
    // Whether the previous checking base expression is a type identifier.
    bool wasTypeCall = false;
};
} // namespace Cangjie

namespace {
/**
 * Get the relative postion of reference name @p baseName with given @p scopeName and the status indicating
 * whether the searching reference is existing as local declaration.
 * @return the relative postion.
 */
Position GetRelativePos(
    const ASTContext& ctx, const std::string& scopeName, const std::string& baseName, bool hasLocalDecl)
{
    // Default position is at the line with the largest line number, which can accessing all of the global variables.
    Position pos = {0, std::numeric_limits<int>::max(), 0};
    if (hasLocalDecl) {
        // Search for "scope_name == 'scopeName' && name == 'reference' &&
        //             (ast_kind == 'var_decl' || ast_kind == 'func_decl')"
        // when 'hasLocalDecl' is true.
        Query q(Operator::AND);
        q.left = std::make_unique<Query>("scope_name", scopeName);
        q.right = std::make_unique<Query>(Operator::AND);
        q.right->left = std::make_unique<Query>("name", baseName);
        q.right->right = std::make_unique<Query>(Operator::OR);
        q.right->right->left = std::make_unique<Query>("ast_kind", "var_decl");
        q.right->right->right = std::make_unique<Query>("ast_kind", "func_decl");
        auto syms = ctx.searcher->Search(ctx, &q);
        if (!syms.empty()) {
            CJC_NULLPTR_CHECK(syms[0]->node);
            // Set searching position just behind local variable.
            pos = syms[0]->node->end + Position{0, 0, 1};
        }
    } else {
        // If the scope gate can be found, set position just behind scope gate's begin position.
        auto sym = ScopeManagerApi::GetScopeGate(ctx, ScopeManagerApi::GetScopeGateName(scopeName));
        if (sym && sym->node) {
            pos = sym->node->begin + Position{0, 0, 1};
        }
    }
    return pos;
}
} // namespace

/**
 * Verifying the 'expr' is only combined with named reference / call / subscript accessing.
 * If 'hasLocalDecl' is true, found local declaration of base name.
 * Searching for each expression hierarchy result and filter candidates by accessed member name.
 *
 * From inner most to outer, if previous level is name reference, do synthesizing
 * else filter by calling argument or subscript index, check whether they contain any expression
 * which is needing desugar before typechecking.
 * After filtration, if 'candidates' is empty, failed to found result. Otherwise continue searching.
 *
 * The return value will contains all possible candidate decls or types.
 */
Candidate TypeChecker::SynReferenceSeparately(
    ASTContext& ctx, const std::string& scopeName, Expr& expr, bool hasLocalDecl) const
{
    return impl->SynReferenceSeparately(ctx, scopeName, expr, hasLocalDecl);
}

void TypeChecker::RemoveTargetNotMeetExtendConstraint(const Ptr<AST::Ty> baseTy, std::vector<Ptr<AST::Decl>>& targets)
{
    return impl->RemoveTargetNotMeetExtendConstraint(baseTy, targets);
}

Candidate TypeChecker::TypeCheckerImpl::SynReferenceSeparately(
    ASTContext& ctx, const std::string& scopeName, Expr& expr, bool hasLocalDecl)
{
    if (ctx.curPackage->files.empty()) {
        return {};
    }
    std::string baseName = BaseNameFinder(expr)();
    if (baseName.empty()) {
        return {}; // If failed to get 'baseName', directly return -- no result found.
    }
    // Clone the expr using for 'Synthesizer', avoid direct modified the parameter.
    auto cloned = ASTCloner::Clone<Expr>(&expr);
    // Ensure the expression has 'scopeName'.
    auto scope = scopeName.empty() ? TOPLEVEL_SCOPE_NAME : scopeName;
    ScopeNameSetter(*cloned, scope)();
    auto ds = DiagSuppressor(diag);
    Position pos = GetRelativePos(ctx, scope, baseName, hasLocalDecl);
    // Previous checking guarantees the expresion only combined with name reference accessing/calling/subscripting.
    auto results = Synthesizer(*this, ctx, *cloned, pos)();
    return results;
}
