// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements initialization checking.
 */

#include "InitializationChecker.h"

#include <queue>
#include <unordered_set>

#include "Diags.h"
#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;
using namespace Sema;
using namespace TypeCheckUtil;

namespace {
inline bool IsFuncOrProp(const Node& node)
{
    return node.astKind == ASTKind::FUNC_DECL || node.astKind == ASTKind::PROP_DECL;
}

bool IsInitialized(MemberAccess& ma)
{
    bool init = false;
    auto visit = [&init](Ptr<Node> node) {
        if (auto target = node->GetTarget(); target && target->TestAttr(Attribute::INITIALIZED)) {
            init = true;
            return VisitAction::STOP_NOW;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(&ma, visit).Walk();
    return init;
}

inline bool IsInStructDecl(const VarDecl& vd)
{
    return vd.outerDecl && vd.outerDecl->IsNominalDecl();
}

inline bool IsDeclaredInSameFile(const Decl& d1, const Decl& d2)
{
    return d1.curFile && d2.curFile && d1.curFile->fileHash == d2.curFile->fileHash;
}

inline bool IsInitializedButUnReachable(const VarDecl& vd)
{
    return vd.TestAttr(Attribute::INITIALIZED) && vd.initializer && vd.initializer->TestAttr(Attribute::UNREACHABLE);
}

bool IsAllInitialized(const Node& node)
{
    if (node.astKind == ASTKind::CLASS_DECL || node.astKind == ASTKind::STRUCT_DECL) {
        auto decls = RawStaticCast<const Decl*>(&node)->GetMemberDeclPtrs();
        for (auto& it : decls) {
            if (it != nullptr && it->astKind == ASTKind::VAR_DECL && !it->TestAttr(Attribute::INITIALIZED)) {
                return false;
            }
        }
    }
    return true;
}

bool IsRelatedTypeDecl(const Decl& curComposite, const Decl& outerDeclOfTarget)
{
    if (curComposite.astKind == ASTKind::CLASS_DECL) {
        auto currentClass = StaticCast<ClassDecl>(&curComposite);
        while (currentClass != nullptr) {
            if (currentClass == &outerDeclOfTarget) {
                return true;
            }
            currentClass = currentClass->GetSuperClassDecl();
        }
    }
    return &curComposite == &outerDeclOfTarget;
}

bool IsUsedInInitFunction(const ASTContext& ctx, const Expr& expr)
{
    Ptr<Decl> decl = expr.GetTarget();
    bool inInitFunction = false;
    if (decl != nullptr && decl->TestAnyAttr(Attribute::IN_STRUCT, Attribute::IN_CLASSLIKE)) {
        Symbol* symOfVdStruct = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, decl->scopeName);
        Symbol* symOfExprStruct = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, expr.scopeName);
        Symbol* symOfExprFunc = ScopeManager::GetOutMostSymbol(ctx, SymbolKind::FUNC, expr.scopeName);
        if (symOfExprStruct != nullptr && symOfVdStruct != nullptr && symOfExprFunc != nullptr) {
            auto structOfExpr = symOfExprStruct->node;
            auto structOfVd = symOfVdStruct->node;
            CJC_NULLPTR_CHECK(symOfExprFunc->node);
            auto currentFunc = symOfExprFunc->node;
            bool sameStaticStatus = currentFunc->TestAttr(Attribute::STATIC) == decl->TestAttr(Attribute::STATIC);
            if (currentFunc->TestAttr(Attribute::CONSTRUCTOR) && sameStaticStatus &&
                IsRelatedTypeDecl(*StaticCast<Decl>(structOfExpr), *StaticCast<Decl>(structOfVd))) {
                inInitFunction = true;
            }
        }
    }
    return inInitFunction;
}

void GetInitsInExpr(Ptr<Node> node, std::unordered_set<Ptr<Decl>>& initedNodes)
{
    if (!node) {
        return;
    }
    auto visitor = [&initedNodes](Ptr<Node> n) {
        auto target = n->GetTarget();
        if (auto expr = DynamicCast<Expr*>(n);
            expr && expr->IsReferenceExpr() && target && target->TestAttr(Attribute::INITIALIZED)) {
            initedNodes.insert(target);
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(node, visitor);
    walker.Walk();
}

void GetUnInitsInExpr(Ptr<Node> node, std::unordered_set<Ptr<Decl>>& unInitedNodes)
{
    auto visitor = [&unInitedNodes](Ptr<Node> n) {
        auto target = n->GetTarget();
        if (auto expr = DynamicCast<Expr*>(n);
            expr && expr->IsReferenceExpr() && target && !target->TestAttr(Attribute::INITIALIZED)) {
            unInitedNodes.insert(target);
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(node, visitor);
    walker.Walk();
}

void GetLocalUnInitsInExpr(const Ptr<Node>& node, std::unordered_set<Ptr<Decl>>& localUnInits)
{
    Walker(node, [&localUnInits](auto node) {
        if (auto vda = DynamicCast<VarDeclAbstract*>(node); vda && !vda->TestAttr(Attribute::INITIALIZED)) {
            localUnInits.emplace(vda);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

inline bool NotAssignableVariable(const VarDecl& vd, bool inInitFunction)
{
    return !vd.isVar &&
        (vd.TestAnyAttr(Attribute::GLOBAL, Attribute::INITIALIZED, Attribute::ENUM_CONSTRUCTOR) ||
            (vd.TestAnyAttr(Attribute::IN_STRUCT, Attribute::IN_CLASSLIKE) && !inInitFunction));
}

inline bool CanSkipInitCheck(const Node& node)
{
    return node.TestAnyAttr(Attribute::IMPORTED, Attribute::FOREIGN, Attribute::ENUM_CONSTRUCTOR);
}

// Check whether member variable is used in member function/property except constructor.
bool IsMemberUseOutsideCtor(const ASTContext& ctx, const Expr& expr, const Decl& decl)
{
    bool isNotMemberVar = !decl.outerDecl || !decl.outerDecl->IsNominalDecl() || decl.astKind != ASTKind::VAR_DECL;
    if (isNotMemberVar) {
        return false;
    }
    auto outerMostSym = ScopeManager::GetOutMostSymbol(ctx, SymbolKind::FUNC, expr.scopeName);
    auto outerMostFunc = outerMostSym ? StaticCast<FuncDecl*>(outerMostSym->node) : nullptr;
    return outerMostFunc && !outerMostFunc->TestAttr(Attribute::CONSTRUCTOR);
}

bool IsInDifferentFunction(const ASTContext& ctx, const Expr& usage, const Decl& target)
{
    auto usageSym = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, usage.scopeName);
    if (usageSym == nullptr || IsGlobalOrStaticVar(target)) {
        return false;
    }
    auto declSym = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, target.scopeName);
    if (declSym != nullptr) {
        // Local variable usage.
        return usageSym != declSym;
    }
    if (target.outerDecl == nullptr) {
        // Local variable declared in the initializer of a global variable.
        return true;
    }
    if (auto usageDecl = DynamicCast<FuncDecl*>(usageSym->node);
        usageDecl && !usageDecl->TestAttr(Attribute::CONSTRUCTOR)) {
        // Member variable usage.
        return target.outerDecl != usageDecl->outerDecl && usageSym != declSym;
    }
    // Used in lambda which means capture the member variable.
    return usageSym->node->astKind == ASTKind::LAMBDA_EXPR;
}

bool IsAssignLetDefinedOuterLoop(const ASTContext& ctx, const VarDecl& vd, const Expr& curExpr)
{
    if (vd.isVar) {
        return false;
    }
    // When 'vd' is not initialized and the 'curExpr' is compound assignment
    // or target decl is also referenced in the right hand,
    // the error of used before intialization has higher priority than assign immutable, return false here.
    auto ae = DynamicCast<AssignExpr>(&curExpr);
    if (ae && !vd.TestAttr(Attribute::INITIALIZED)) {
        if (ae->isCompound) {
            return false;
        }
        bool occurredInRight = false;
        Walker(ae->rightExpr.get(), [&occurredInRight, &vd](auto sub) {
            if (sub->GetTarget() == &vd) {
                occurredInRight = true;
                return VisitAction::STOP_NOW;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        if (occurredInRight) {
            return false;
        }
    }
    auto outerLoop = ScopeManager::GetRefLoopSymbol(ctx, curExpr);
    if (outerLoop && outerLoop->node->begin > vd.begin) {
        return true;
    }
    return false;
}

void CollectDeclsFromPropDecl(const OwnedPtr<Decl>& decl, const PropDecl& pd, std::vector<Ptr<FuncDecl>>& funcDecls,
    std::vector<Ptr<Decl>>& staticFuncDecls)
{
    if (!pd.getters.empty()) {
        for (auto& getter : pd.getters) {
            if (decl->TestAttr(Attribute::STATIC)) {
                staticFuncDecls.emplace_back(getter.get());
            } else {
                funcDecls.emplace_back(getter.get());
            }
        }
    }
    if (!pd.setters.empty()) {
        for (auto& setter : pd.setters) {
            if (decl->TestAttr(Attribute::STATIC)) {
                staticFuncDecls.emplace_back(setter.get());
            } else {
                funcDecls.emplace_back(setter.get());
            }
        }
    }
}

void UpdateContextVaraiables(std::unordered_set<Ptr<const AST::VarDecl>>& contextVariables, const Decl& d)
{
    if (d.TestAnyAttr(Attribute::GLOBAL, Attribute::STATIC)) {
        return;
    }
    if (auto vd = DynamicCast<VarDecl>(&d)) {
        contextVariables.emplace(vd);
    } else if (auto vpd = DynamicCast<VarWithPatternDecl>(&d)) {
        for (auto it : FlattenVarWithPatternDecl(*vpd)) {
            if (auto vp = DynamicCast<VarPattern>(it)) {
                contextVariables.emplace(vp->varDecl.get());
            }
        }
    }
}

bool MayBeStructTy(const VarDecl& target)
{
    if (!Ty::IsTyCorrect(target.ty)) {
        return false;
    }
    if (target.ty->IsStruct()) {
        return true;
    }
    if (target.ty->IsGeneric()) {
        auto gTy = RawStaticCast<GenericsTy*>(target.ty);
        for (auto& ub : std::as_const(gTy->upperBounds)) {
            // The upper bounds of GenericsTy can only be Classes or Interfaces.
            CJC_ASSERT(ub->IsClassLike());
            if (ub->IsInterface()) {
                return true;
            }
        }
    }
    return false;
}
} // namespace

// Only update scope is terminated for control flow expr.
void InitializationChecker::UpdateScopeStatus(const Node& node)
{
    CJC_ASSERT(Utils::In(node.astKind, {ASTKind::RETURN_EXPR, ASTKind::JUMP_EXPR, ASTKind::THROW_EXPR}));
    auto scopeGate = ScopeManagerApi::GetScopeGateName(node.scopeName);
    bool mayNotGoOn = optionalCtxDepth != 0 && (node.astKind != ASTKind::THROW_EXPR || tryDepth == 0);
    if (scopeGate.empty() || mayNotGoOn) {
        return;
    }
    // Only update scope info when encountered the control flow expr in same scope for the first time.
    if (auto [it, success] =
            variablesBeforeTeminatedScope.try_emplace(scopeGate, std::unordered_set<Ptr<const AST::Decl>>{});
        success) {
        it->second.reserve(contextVariables.size());
        for (auto& [_, vars] : contextVariables) {
            it->second.insert(vars.cbegin(), vars.cend());
        }
        scopeTerminationKinds[scopeGate] = node.astKind;
    }
    // When meeting return expr inside constructor, we need to record current uninitialized member variables.
    if (node.astKind == ASTKind::RETURN_EXPR) {
        Symbol* funcSym = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, node.scopeName);
        if (!funcSym || !IsInstanceConstructor(*funcSym->node)) {
            return;
        }
        auto ctor = StaticCast<FuncDecl>(funcSym->node);
        if (auto [it, success] = ctorUninitVarsMap.try_emplace(ctor, std::unordered_set<Ptr<const AST::Decl>>{});
            success) {
            auto typeDecl = ctor->outerDecl;
            CJC_NULLPTR_CHECK(typeDecl);
            for (auto& d : typeDecl->GetMemberDecls()) {
                if (d->astKind == ASTKind::VAR_DECL && !d->TestAttr(Attribute::STATIC) &&
                    !d->TestAttr(Attribute::INITIALIZED)) {
                    it->second.emplace(d.get());
                }
            }
        }
    }
}

void InitializationChecker::UpdateInitializationStatus(const AST::AssignExpr& assign, Decl& decl)
{
    // Only set initialized when the accessed decl is not defined before termination of current scope.
    // Special case: When terminated expr is throwExpr, we only skip the initialization set when assignment is in try.
    auto searchingScope = ScopeManagerApi::GetScopeGateName(assign.scopeName);
    auto found = variablesBeforeTeminatedScope.find(searchingScope);
    if (found != variablesBeforeTeminatedScope.end() && found->second.count(&decl) != 0) {
        if (scopeTerminationKinds[searchingScope] != ASTKind::THROW_EXPR || tryDepth != 0) {
            initVarsAfterTerminator[searchingScope].emplace(&decl);
            return;
        }
    }
    decl.EnableAttr(Attribute::INITIALIZED);
}

void InitializationChecker::CheckInitialization(Ptr<AST::Node> n)
{
    if (!n || n->TestAttr(Attribute::HAS_BROKEN)) {
        return;
    }
    if (n->TestAttr(Attribute::GLOBAL, Attribute::INITIALIZED)) {
        return;
    }
    auto preVisiter = [this](Ptr<Node> n) {
        if (n->astKind == AST::ASTKind::MACRO_EXPAND_DECL || n->TestAttr(Attribute::INITIALIZATION_CHECKED)) {
            return VisitAction::SKIP_CHILDREN;
        }
        n->EnableAttr(Attribute::INITIALIZATION_CHECKED);
        if (auto decl = DynamicCast<Decl>(n)) {
            UpdateContextVaraiables(contextVariables[ScopeManagerApi::GetScopeGateName(decl->scopeName)], *decl);
        }
        switch (n->astKind) {
            case ASTKind::FUNC_PARAM:
                n->EnableAttr(Attribute::INITIALIZED);
                CheckInitInExpr(StaticCast<FuncParam>(n)->assignment.get());
                return VisitAction::SKIP_CHILDREN;
            case ASTKind::VAR_DECL:
                CheckInitInVarDecl(StaticCast<VarDecl>(*n));
                return VisitAction::SKIP_CHILDREN;
            case ASTKind::VAR_WITH_PATTERN_DECL:
                CheckInitInVarWithPatternDecl(StaticCast<VarWithPatternDecl>(*n));
                return VisitAction::SKIP_CHILDREN;
            case ASTKind::FUNC_DECL: {
                auto& fd = StaticCast<FuncDecl>(*n);
                if (fd.TestAttr(Attribute::HAS_INITIAL) && fd.ownerFunc) {
                    // Do not check initialization status for compiler add default param function.
                    // Initialization status will be checked in original function param assignment.
                    return VisitAction::SKIP_CHILDREN;
                }
                if (fd.funcBody) {
                    fd.EnableAttr(Attribute::INITIALIZED);
                    CheckInitInFuncBody(*fd.funcBody);
                }
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::EXTEND_DECL:
                CheckInitInExtendDecl(StaticCast<ExtendDecl>(*n));
                return VisitAction::WALK_CHILDREN;
            case ASTKind::INTERFACE_DECL: {
                auto& id = StaticCast<InterfaceDecl>(*n);
                for (auto& si : id.inheritedTypes) {
                    CheckInitialization(si->GetTarget());
                }
                CheckInitInTypeDecl(id.body->decls);
                id.EnableAttr(Attribute::INITIALIZED);
                return VisitAction::WALK_CHILDREN;
            }
            case ASTKind::CLASS_DECL: {
                auto& cd = StaticCast<ClassDecl>(*n);
                if (cd.TestAttr(Attribute::IMPORTED)) {
                    return VisitAction::SKIP_CHILDREN;
                }
                CheckInitInClassDecl(cd);
                cd.EnableAttr(Attribute::INITIALIZED);
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::ENUM_DECL: {
                auto& ed = StaticCast<EnumDecl>(*n);
                for (auto& si : ed.inheritedTypes) {
                    CheckInitialization(si->GetTarget());
                }
                std::for_each(ed.constructors.begin(), ed.constructors.end(),
                    [](auto& it) { it->EnableAttr(Attribute::INITIALIZED); });
                CheckInitInTypeDecl(ed.members);
                ed.EnableAttr(Attribute::INITIALIZED);
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::STRUCT_DECL: {
                auto& sd = StaticCast<StructDecl>(*n);
                CheckStaticInitForTypeDecl(sd);
                CheckInitInTypeDecl(sd.body->decls);
                sd.EnableAttr(Attribute::INITIALIZED);
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::BLOCK:
                for (auto& node : StaticCast<Block>(n)->body) {
                    CheckInitialization(node.get());
                }
                return VisitAction::SKIP_CHILDREN;
            case ASTKind::PACKAGE:
                for (auto& file : StaticCast<Package>(n)->files) {
                    if (file == nullptr) {
                        continue;
                    }
                    for (auto& decl : file->decls) {
                        CheckInitialization(decl.get());
                    }
                }
                return VisitAction::SKIP_CHILDREN;
            default:
                if (auto expr = DynamicCast<Expr>(n); expr) {
                    if (expr->desugarExpr) {
                        CheckInitInExpr(expr->desugarExpr.get());
                    } else {
                        CheckInitInExpr(expr);
                    }
                    return VisitAction::SKIP_CHILDREN;
                }
                return VisitAction::WALK_CHILDREN;
        }
    };
    auto postVisiter = [this](Ptr<Node> n) {
        // Current node's 'scopeName' is a scopeGate which means the boundary of scope.
        if (n->scopeName.find("_") != std::string::npos) {
            // When quit the node, clear the context info.
            ClearScopeStatus(n->scopeName);
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(n, preVisiter, postVisiter).Walk();
}

void InitializationChecker::CheckStaticInitForTypeDecl(const InheritableDecl& id)
{
    Ptr<FuncDecl> staticInit = nullptr;
    std::vector<Ptr<Decl>> staticMembers;
    for (auto& member : id.GetMemberDeclPtrs()) {
        if (!member->TestAttr(Attribute::STATIC)) {
            continue;
        }
        if (member->astKind == ASTKind::VAR_DECL) {
            CheckInitialization(member);
            (void)staticMembers.emplace_back(member);
        } else if (auto fd = DynamicCast<FuncDecl*>(member); fd && IsStaticInitializer(*fd)) {
            staticInit = fd;
        }
    }
    if (staticInit) {
        CheckInitialization(staticInit);
        if (staticInit->funcBody == nullptr) {
            return; // If the function is broken, we can quit now.
        }

        // If the static initializer is terminated by 'ThrowExpr' directly,
        // the initialization check can be ignored, since the global initialization will fail.
        auto found = scopeTerminationKinds.find(staticInit->funcBody->scopeName);
        if (found != scopeTerminationKinds.end() && found->second == ASTKind::THROW_EXPR) {
            return;
        }
    }
    for (auto decl : staticMembers) {
        // any variable in .cj.d is correct to be not initialised
        if (decl->TestAnyAttr(Attribute::INITIALIZED, Attribute::TOOL_ADD) || opts.compileCjd) {
            continue;
        }
        auto builder =
            diag.DiagnoseRefactor(DiagKindRefactor::sema_type_uninitialized_static_field, *decl, decl->identifier);
        std::string note = "you may add an initializer inline or ";
        if (staticInit) {
            builder.AddNote(note + "initialize the variable in the 'static init'");
            builder.AddNote(*staticInit, MakeRange(staticInit->identifier), "static init defined here");
        } else {
            builder.AddNote(note + "define a 'static init' to perform initialization");
        }
    }
}

void InitializationChecker::CheckInitInClassDecl(const ClassDecl& cd)
{
    std::vector<Ptr<Decl>> superClassNonFuncDecls;
    std::set<Ptr<Decl>> superClasses;
    for (auto& it : cd.inheritedTypes) {
        CJC_NULLPTR_CHECK(it);
        if (auto superClass = DynamicCast<ClassDecl*>(it->GetTarget()); superClass) {
            if (IsDeclaredInSameFile(cd, *superClass)) {
                // Since static variables in same file are initialized in the definition order,
                // if the super class is defined in same file,
                // static init of current class should be checked before checking super class.
                CheckStaticInitForTypeDecl(cd);
            }
            CheckInitialization(superClass);
            GetNonFuncDeclsInSuperClass(*superClass, superClassNonFuncDecls, superClasses);
        }
        if (auto superInterface = DynamicCast<InterfaceDecl*>(it->GetTarget()); superInterface) {
            CheckInitialization(superInterface);
        }
    }
    if (cd.body) {
        CheckStaticInitForTypeDecl(cd);
        CheckInitInTypeDecl(cd.body->decls, superClassNonFuncDecls);
    }
}

void InitializationChecker::CheckInitInExtendDecl(const ExtendDecl& ed)
{
    CJC_NULLPTR_CHECK(ed.extendedType);
    if (!Ty::IsTyCorrect(ed.extendedType->ty)) {
        return;
    }
    // Get the not declared-initialized member variables.
    std::vector<Ptr<Decl>> unInitNonFuncDecls;
    if (auto typeDecl = Ty::GetDeclPtrOfTy(ed.extendedType->ty); typeDecl && typeDecl->IsStructOrClassDecl()) {
        for (auto& decl : typeDecl->GetMemberDecls()) {
            CJC_NULLPTR_CHECK(decl);
            if (decl->astKind == ASTKind::VAR_DECL && !decl->TestAttr(Attribute::INITIALIZED)) {
                unInitNonFuncDecls.emplace_back(decl.get());
                decl->EnableAttr(Attribute::INITIALIZED);
            }
        }
    }
    CheckInitInTypeDecl(ed.members);
    std::for_each(unInitNonFuncDecls.begin(), unInitNonFuncDecls.end(),
        [](Ptr<Decl> decl) { decl->DisableAttr(Attribute::INITIALIZED); });
}

void InitializationChecker::CheckInitInFuncBody(const FuncBody& fb)
{
    if (fb.generic) {
        for (auto& it : fb.generic->typeParameters) {
            CheckInitialization(it.get());
        }
        for (auto& it : fb.generic->genericConstraints) {
            CheckInitialization(it.get());
        }
    }
    for (auto& paramList : fb.paramLists) {
        CheckInitialization(paramList.get());
    }
    bool needCheckInLoop = !fb.funcDecl ||
        (fb.funcDecl->symbol && ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, fb.funcDecl->scopeName));
    if (needCheckInLoop) {
        CheckInitInLoop(fb.body.get());
    } else {
        CheckInitialization(fb.body.get());
    }
}

void InitializationChecker::CheckInitInVarWithPatternDecl(VarWithPatternDecl& vpd)
{
    if (vpd.initializer) {
        CheckInitInExpr(vpd.initializer.get());
        vpd.EnableAttr(Attribute::INITIALIZED);
    }
    if (vpd.irrefutablePattern) {
        CheckInitInExpr(vpd.irrefutablePattern.get());
        Walker(vpd.irrefutablePattern, [&vpd](auto node) {
            if (auto vd = DynamicCast<VarDecl*>(node)) {
                if (vpd.initializer == nullptr) {
                    vd->DisableAttr(Attribute::INITIALIZED);
                }
                vd->EnableAttr(Attribute::INITIALIZATION_CHECKED);
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
}

void InitializationChecker::CheckInitInVarDecl(VarDecl& vd)
{
    if (vd.initializer) {
        CheckInitInExpr(vd.initializer.get());
        vd.EnableAttr(Attribute::INITIALIZED);
    } else if (vd.TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        vd.EnableAttr(Attribute::INITIALIZED);
    }
}

void InitializationChecker::CheckLetFlag(const Expr& ae, const Expr& expr)
{
    bool inInitFunction = IsUsedInInitFunction(ctx, expr);
    switch (expr.astKind) {
        case ASTKind::REF_EXPR:
            if (auto vd = DynamicCast<VarDecl*>(expr.GetTarget());
                vd && (NotAssignableVariable(*vd, inInitFunction) || IsAssignLetDefinedOuterLoop(ctx, *vd, ae))) {
                DiagCannotAssignToImmutable(diag, ae, expr);
            }
            break;
        case ASTKind::MEMBER_ACCESS:
            CheckLetFlagInMemberAccess(ae, StaticCast<MemberAccess>(expr), inInitFunction);
            break;
        case ASTKind::SUBSCRIPT_EXPR: {
            auto& se = StaticCast<SubscriptExpr>(expr);
            if (!se.baseExpr || !Is<VArrayTy>(se.baseExpr->ty)) {
                return;
            }
            // Multi-level VArray nested assignment expressions cannot be modified. Because the return value of
            // subscripted accessor function can't be modified.
            if (se.baseExpr->astKind == ASTKind::SUBSCRIPT_EXPR || se.baseExpr->astKind == ASTKind::CALL_EXPR) {
                DiagCannotAssignToImmutable(diag, ae, *se.baseExpr);
            } else {
                CheckLetFlag(ae, *se.baseExpr);
            }
        }
        default:
            break;
    }
}

void InitializationChecker::CheckLetFlagInMemberAccess(const Expr& ae, const MemberAccess& ma, bool inInitFunction)
{
    if (ma.baseExpr == nullptr) {
        return;
    }
    if (auto vd = DynamicCast<VarDecl*>(ma.target); vd) {
        auto& realBaseExpr = ma.baseExpr->desugarExpr ? ma.baseExpr->desugarExpr : ma.baseExpr;
        if (realBaseExpr == nullptr) {
            return;
        }
        bool isStructBase = realBaseExpr->ty && realBaseExpr->ty->IsStruct();
        if (NotAssignableVariable(*vd, inInitFunction)) {
            DiagCannotAssignToImmutable(diag, ae, ma);
        }
        if (Utils::In(realBaseExpr->astKind, {ASTKind::CALL_EXPR, ASTKind::SUBSCRIPT_EXPR}) && isStructBase) {
            DiagCannotAssignToImmutable(diag, ae, *realBaseExpr);
        }
        if (auto bma = DynamicCast<MemberAccess*>(ma.baseExpr.get()); bma) {
            bool invalidPropAccess = bma->target && bma->target->astKind == ASTKind::PROP_DECL && isStructBase;
            if (invalidPropAccess) {
                DiagCannotAssignToImmutable(diag, ae, *bma);
            }
        }
    }
    // Do not allow assignments of fields of 'let' struct instance
    if (auto re = DynamicCast<RefExpr*>(ma.baseExpr.get()); re) {
        if (auto vd = DynamicCast<VarDecl*>(re->ref.target); vd && !vd->isVar && MayBeStructTy(*vd)) {
            DiagCannotAssignToImmutable(diag, ae, *re);
        }
    }
    if (auto m = DynamicCast<MemberAccess*>(ma.baseExpr.get()); m) {
        if (auto vd = DynamicCast<VarDecl*>(m->target); vd && MayBeStructTy(*vd)) {
            if (!vd->isVar) {
                DiagCannotAssignToImmutable(diag, ae, ma);
            } else {
                CheckLetFlag(ae, *ma.baseExpr);
            }
        }
    }
}

bool InitializationChecker::CheckInitInRefExpr(const RefExpr& re)
{
    auto target = TypeCheckUtil::GetRealTarget(re.ref.target);
    if (target == nullptr || target->astKind == ASTKind::GENERIC_PARAM_DECL) {
        return true;
    }
    bool invalidUseOfThis = re.isThis && re.isAlone && target->IsStructOrClassDecl() && !IsAllInitialized(*target);
    if (invalidUseOfThis) {
        diag.Diagnose(re, DiagKind::sema_illegal_usage_of_member, "this");
        return false;
    }
    if (CanSkipInitCheck(*target) || !IsOrderRelated(re, *target, target->IsNominalDecl())) {
        return true;
    }
    Symbol* toplevelSymOfTarget = ScopeManager::GetCurSymbolByKind(SymbolKind::TOPLEVEL, ctx, target->scopeName);
    if (toplevelSymOfTarget != nullptr && toplevelSymOfTarget->node != nullptr) {
        if (CanSkipInitCheck(*toplevelSymOfTarget->node)) {
            return true;
        }
        Symbol* toplevelSymOfRe = ScopeManager::GetCurSymbolByKind(SymbolKind::TOPLEVEL, ctx, re.scopeName);
        if (toplevelSymOfRe != nullptr && toplevelSymOfRe->node != nullptr) {
            // If accessing non-static instance member inside nominal struct declaration, check for legality.
            bool referenceInside = toplevelSymOfRe->node->IsStructOrClassDecl() && target->outerDecl &&
                target->outerDecl->IsNominalDecl() && !target->TestAttr(Attribute::CONSTRUCTOR) &&
                !target->TestAttr(Attribute::STATIC);
            if (referenceInside && !CheckIllegalRefExprAccess(re, *toplevelSymOfRe, *toplevelSymOfTarget)) {
                return false;
            }
            bool inDiffScope = toplevelSymOfRe != toplevelSymOfTarget && !target->TestAttr(Attribute::STATIC);
            if (inDiffScope) {
                return true;
            }
        }
    }
    // Do not report use before initialization when:
    // 1. target is funcparam;
    // 2. target is function or property;
    // 3. target is instance or static member variable used outside constructor,
    //    which will be checked separately in constructor.
    if (Is<FuncParam>(target) || IsFuncOrProp(*target) || IsMemberUseOutsideCtor(ctx, re, *target)) {
        return true;
    }
    if (re.ShouldDiagnose(true)) {
        if (target->TestAttr(Attribute::GLOBAL) && target->begin.fileID == re.begin.fileID &&
            re.begin < target->begin) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_undefined_variable, re, re.ref.identifier.Val());
            return false;
        } else if (!target->TestAttr(Attribute::INITIALIZED)) {
            if (initVarsAfterTerminator.count(ScopeManagerApi::GetScopeGateName(re.scopeName)) != 0) {
                return true;
            } else if (IsInDifferentFunction(ctx, re, *target)) {
                // Definition and usage is not in same declaration (usage is not in constructor/member decl).
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_capture_before_initialization, re, target->identifier.Val());
            } else if (!re.TestAttr(Attribute::LEFT_VALUE)) {
                diag.Diagnose(re, DiagKind::sema_used_before_initialization, re.ref.identifier.Val());
            }
            return false;
        }
    }
    return target->TestAttr(Attribute::INITIALIZED);
}

bool InitializationChecker::CheckInitInMemberAccess(MemberAccess& ma)
{
    bool res = CheckInitInExpr(ma.baseExpr.get());
    if (!ma.target) {
        return res;
    }
    auto curStructOfMemberAccess = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, ma.scopeName);
    if (auto re = DynamicCast<RefExpr*>(ma.baseExpr.get()); re && ma.target->outerDecl &&
        !ma.target->TestAttr(Attribute::STATIC) && curStructOfMemberAccess &&
        curStructOfMemberAccess->node->IsStructOrClassDecl()) {
        // Check illegal `this.member` access
        if (re->isThis && re->ref.target && re->ref.target->IsStructOrClassDecl()) {
            if (!CheckIllegalMemberAccess(ma, *ma.target, *re->ref.target)) {
                return false;
            }
        }
        // Check illegal `super.memberFunc` access
        if (re->isSuper && IsFuncOrProp(*ma.target)) {
            CJC_NULLPTR_CHECK(curStructOfMemberAccess->node); // Symbol must have non-null 'node'.
            if (!CheckIllegalMemberAccess(ma, *ma.target, *curStructOfMemberAccess->node)) {
                return false;
            }
        }
    }
    // Do not report use before initialization when:
    // 1. target is function or property;
    // 2. target is instance or static member variable which will be checked separately in constructor.
    if (IsFuncOrProp(*ma.target) || IsMemberUseOutsideCtor(ctx, ma, *ma.target)) {
        return res;
    }
    bool isUndefinedVar = ma.target->TestAttr(Attribute::GLOBAL) && ma.target->begin.fileID == ma.begin.fileID &&
        ma.begin < ma.target->begin;
    if (isUndefinedVar) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_undefined_variable, ma, ma.target->identifier.Val());
        return false;
    }
    auto curStructOfTarget = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, ma.target->scopeName);
    bool isInSameDecl = curStructOfMemberAccess && curStructOfMemberAccess == curStructOfTarget;
    if (isInSameDecl && !IsInitialized(ma)) {
        (void)diag.Diagnose(ma, DiagKind::sema_used_before_initialization, ma.target->identifier.Val());
        return false;
    }
    return true;
}

bool InitializationChecker::CheckInitInAssignExpr(const AssignExpr& ae)
{
    if (ae.leftValue == nullptr || ae.rightExpr == nullptr) {
        return false; // Ignore invalid node.
    }
    // Check usage of leftValue and rightExpr.
    CheckLetFlag(ae, *ae.leftValue);
    if (ae.isCompound) {
        ae.leftValue->DisableAttr(Attribute::LEFT_VALUE);
        (void)CheckInitInExpr(ae.leftValue.get());
        ae.leftValue->EnableAttr(Attribute::LEFT_VALUE);
    }
    (void)CheckInitInExpr(ae.rightExpr.get());
    // Check for possible un-initialized left target.
    auto target = ae.leftValue->GetTarget();
    // If leftvalue is not initialized, then initialize. leftvalue is reference expression.
    if (target) {
        if (ae.leftValue->astKind == ASTKind::MEMBER_ACCESS) {
            auto& ma = StaticCast<MemberAccess&>(*ae.leftValue);
            CheckInitInExpr(ma.baseExpr.get());
        }
        // 1. Top-level/non-variable cannot be accessed before all members initialized in decl,
        //    so if the target is not variable, we should check initialization status here.
        // 2. For reference of local variable, we need to gurantee that it is initialized in same function scope.
        bool needCheck = target->scopeLevel == 0 ||
            (target->astKind != ASTKind::VAR_DECL && target->outerDecl && target->outerDecl->IsNominalDecl()) ||
            (ae.leftValue->astKind == ASTKind::REF_EXPR && !target->TestAttr(Attribute::INITIALIZED));
        if (!ae.isCompound && needCheck) {
            (void)CheckInitInExpr(ae.leftValue.get());
        }
        // AssignExpr cannot inialize captured variable.
        if (target->TestAttr(Attribute::INITIALIZATION_CHECKED) && !IsInDifferentFunction(ctx, ae, *target)) {
            UpdateInitializationStatus(ae, *target);
        }
        return true;
    }
    return true;
}

bool InitializationChecker::CheckInitInExpr(Ptr<Node> node)
{
    if (!node) {
        return true;
    }
    if (auto expr = DynamicCast<Expr*>(node); expr) {
        if (expr->desugarExpr) {
            return CheckInitInExpr(expr->desugarExpr.get());
        }
    }
    switch (node->astKind) {
        case ASTKind::REF_EXPR:
            return CheckInitInRefExpr(StaticCast<RefExpr>(*node));
        case ASTKind::MEMBER_ACCESS:
            return CheckInitInMemberAccess(StaticCast<MemberAccess>(*node));
        case ASTKind::ASSIGN_EXPR:
            return CheckInitInAssignExpr(StaticCast<AssignExpr>(*node));
        case ASTKind::RETURN_EXPR: {
            auto ret = CheckInitInExpr(StaticCast<ReturnExpr>(node)->expr.get());
            UpdateScopeStatus(*node);
            return ret;
        }
        case ASTKind::TRY_EXPR:
            return CheckInitInTryExpr(StaticCast<TryExpr>(*node));
        case ASTKind::MATCH_EXPR:
            return CheckInitInMatchExpr(StaticCast<MatchExpr>(*node));
        case ASTKind::FOR_IN_EXPR:
            return CheckInitInForInExpr(StaticCast<ForInExpr>(*node));
        case ASTKind::IF_EXPR:
            return CheckInitInIfExpr(StaticCast<IfExpr>(*node));
        case ASTKind::WHILE_EXPR:
            return CheckInitInWhileExpr(StaticCast<WhileExpr>(*node));
        case ASTKind::DO_WHILE_EXPR: {
            auto& dwe = StaticCast<DoWhileExpr>(*node);
            // do {
            //     if (true) {
            //         break
            //     }
            //     a = 1
            // } while (a == 0)
            // in this case, we don't check 'a=1' and 'a==0', CHIR will do
            CheckInitInLoop(dwe.body.get(), false); // Do-while's body is always executed.
            return CheckInitInExpr(dwe.condExpr.get());
        }
        case ASTKind::INC_OR_DEC_EXPR: {
            auto& ide = StaticCast<IncOrDecExpr>(*node);
            CheckLetFlag(ide, *ide.expr);
            return CheckInitInExpr(ide.expr.get());
        }
        case ASTKind::UNARY_EXPR:
            return CheckInitInExpr(StaticCast<UnaryExpr>(node)->expr.get());
        case ASTKind::BINARY_EXPR:
            return CheckInitInBinaryExpr(StaticCast<BinaryExpr>(*node));
        case ASTKind::RANGE_EXPR: {
            auto& re = StaticCast<RangeExpr>(*node);
            bool result = CheckInitInExpr(re.startExpr.get());
            result = result && CheckInitInExpr(re.stopExpr.get());
            result = result && CheckInitInExpr(re.stepExpr.get());
            return result;
        }
        case ASTKind::SUBSCRIPT_EXPR: {
            auto& se = StaticCast<SubscriptExpr>(*node);
            bool result = CheckInitInExpr(se.baseExpr.get());
            for (auto& expr : se.indexExprs) {
                result = result && CheckInitInExpr(expr.get());
            }
            return result;
        }
        case ASTKind::CALL_EXPR: {
            auto& ce = StaticCast<CallExpr>(*node);
            bool result = CheckInitInExpr(ce.baseFunc.get());
            if (auto& args = ce.desugarArgs) {
                for (auto arg : *args) {
                    result = result && CheckInitInExpr(arg->expr.get());
                }
            } else {
                for (auto& arg : ce.args) {
                    result = result && CheckInitInExpr(arg->expr.get());
                }
            }
            return result;
        }
        case ASTKind::PAREN_EXPR:
            return CheckInitInExpr(StaticCast<ParenExpr>(node)->expr.get());
        case ASTKind::LAMBDA_EXPR:
            CheckInitInFuncBody(*StaticCast<LambdaExpr>(node)->funcBody.get());
            return true;
        case ASTKind::ARRAY_LIT:
            return std::all_of(StaticCast<ArrayLit>(node)->children.begin(), StaticCast<ArrayLit>(node)->children.end(),
                [this](auto& expr) { return CheckInitInExpr(expr.get()); });
        case ASTKind::ARRAY_EXPR: {
            bool result = true;
            for (auto& it : StaticCast<ArrayExpr>(node)->args) {
                result = CheckInitInExpr(it->expr.get()) && result;
            }
            return result;
        }
        case ASTKind::POINTER_EXPR: {
            auto& cptrExpr = StaticCast<PointerExpr>(*node);
            if (cptrExpr.arg) {
                return CheckInitInExpr(cptrExpr.arg->expr.get());
            }
            return true;
        }
        case ASTKind::TUPLE_LIT:
            return std::all_of(StaticCast<TupleLit>(node)->children.begin(), StaticCast<TupleLit>(node)->children.end(),
                [this](auto& expr) { return CheckInitInExpr(expr.get()); });
        case ASTKind::TYPE_CONV_EXPR:
            return CheckInitInExpr(StaticCast<TypeConvExpr>(node)->expr.get());
        case ASTKind::IF_AVAILABLE_EXPR: {
            auto ie = StaticCast<IfAvailableExpr>(node);
            bool res = true;
            res = CheckInitInExpr(ie->GetArg()) && res;
            res = CheckInitInExpr(ie->GetLambda1()) && res;
            res = CheckInitInExpr(ie->GetLambda2()) && res;
            return res;
        }
        case ASTKind::LIT_CONST_EXPR: {
            auto& lce = StaticCast<LitConstExpr>(*node);
            if (!lce.siExpr) {
                return true;
            }
            bool result = true;
            for (auto& spe : lce.siExpr->strPartExprs) {
                if (auto ie = DynamicCast<InterpolationExpr*>(spe.get())) {
                    result = result && CheckInitInExpr(ie->block.get());
                }
            }
            return result;
        }
        case ASTKind::THROW_EXPR: {
            auto ret = CheckInitInExpr(StaticCast<ThrowExpr>(node)->expr.get());
            UpdateScopeStatus(*node);
            return ret;
        }
        case ASTKind::JUMP_EXPR:
            UpdateScopeStatus(*node);
            return true;
        case ASTKind::LET_PATTERN_DESTRUCTOR: {
            auto& lpd = StaticCast<LetPatternDestructor>(*node);
            return std::all_of(lpd.patterns.cbegin(), lpd.patterns.cend(),
                [this](const OwnedPtr<Pattern>& p) {
                    return CheckInitInExpr(p.get());
                }) && CheckInitInExpr(lpd.initializer.get());
        }
        case ASTKind::VAR_PATTERN:
            StaticCast<VarPattern>(node)->varDecl->EnableAttr(Attribute::INITIALIZED);
            return true;
        case ASTKind::TUPLE_PATTERN: {
            bool result = true;
            for (auto& pattern : StaticCast<TuplePattern>(node)->patterns) {
                result = result && CheckInitInExpr(pattern.get());
            }
            return result;
        }
        case ASTKind::ENUM_PATTERN: {
            bool result = true;
            for (auto& pattern : StaticCast<EnumPattern>(node)->patterns) {
                result = result && CheckInitInExpr(pattern.get());
            }
            return result;
        }
        case ASTKind::VAR_OR_ENUM_PATTERN:
            return CheckInitInExpr(StaticCast<VarOrEnumPattern>(node)->pattern.get());
        case ASTKind::TYPE_PATTERN:
            return CheckInitInExpr(StaticCast<TypePattern>(node)->pattern.get());
        case ASTKind::EXCEPT_TYPE_PATTERN:
            return CheckInitInExpr(StaticCast<ExceptTypePattern>(node)->pattern.get());
        case ASTKind::BLOCK:
            CheckInitialization(node);
            return true;
        case ASTKind::SPAWN_EXPR: {
            bool result = CheckInitInExpr(StaticCast<SpawnExpr>(node)->task.get());
            result = CheckInitInExpr(StaticCast<SpawnExpr>(node)->arg.get()) && result;
            return result;
        }
        case ASTKind::SYNCHRONIZED_EXPR: {
            bool result = CheckInitInExpr(StaticCast<SynchronizedExpr>(node)->mutex.get());
            result = CheckInitInExpr(StaticCast<SynchronizedExpr>(node)->body.get()) && result;
            return result;
        }
        case ASTKind::IS_EXPR:
            return CheckInitInExpr(StaticCast<IsExpr>(node)->leftExpr.get());
        case ASTKind::AS_EXPR:
            return CheckInitInExpr(StaticCast<AsExpr>(node)->leftExpr.get());
        default:
            return true;
    }
}

bool InitializationChecker::CheckInitInWhileExpr(const WhileExpr& we)
{
    bool result = CheckInitInCondition(*we.condExpr);
    CJC_NULLPTR_CHECK(we.condExpr);
    CheckInitInLoop(we.body.get());
    return result;
}

bool InitializationChecker::CheckInitInForInExpr(const ForInExpr& fie)
{
    bool result = CheckInitInExpr(fie.pattern.get());
    result = result && CheckInitInExpr(fie.inExpression.get());
    result = result && CheckInitInExpr(fie.patternGuard.get());
    CheckInitInLoop(fie.body.get());
    return result;
}

bool InitializationChecker::CheckInitInTryExpr(const TryExpr& te)
{
    bool result = true;
    if (!te.resourceSpec.empty()) {
        for (auto& re : te.resourceSpec) {
            CheckInitialization(re.get());
        }
    }
    ++tryDepth;
    CheckInitialization(te.tryBlock.get());
    --tryDepth;
    if (!te.catchBlocks.empty()) {
        for (auto& catchPattern : te.catchPatterns) {
            result = result && CheckInitInExpr(catchPattern.get());
        }
        for (auto& catchBlock : te.catchBlocks) {
            CheckInitialization(catchBlock.get());
        }
    }
    if (te.finallyBlock) {
        CheckInitialization(te.finallyBlock.get());
    }
    return result;
}

namespace {
/**
 * Check whether there is a control-transfer expression (throw, return, break and continue) in the @p node
 * @return Return true when there is a control-transfer expression in @p node
 */
bool MetControlTransferExpr(const Node& node)
{
    if (Utils::In(node.astKind, {ASTKind::RETURN_EXPR, ASTKind::JUMP_EXPR, ASTKind::THROW_EXPR})) {
        return true;
    } else if (node.astKind == ASTKind::BLOCK) {
        auto& block = StaticCast<const Block&>(node);
        return std::any_of(
            block.body.begin(), block.body.end(), [](auto& n) { return n && MetControlTransferExpr(*n); });
    } else if (auto ae = DynamicCast<const AssignExpr*>(&node); ae && ae->rightExpr) {
        return MetControlTransferExpr(*ae->rightExpr);
    } else if (auto dwe = DynamicCast<const DoWhileExpr*>(&node); dwe && dwe->body) {
        return MetControlTransferExpr(*dwe->body);
    } else {
        return false;
    }
}

class GetPatternVarsImpl {
    std::vector<VarDecl*> res;

    void Visit(const LetPatternDestructor& let)
    {
        for (auto& p : let.patterns) {
            Visit(*p);
        }
    }
    void Visit(const Expr& expr)
    {
        if (auto let = DynamicCast<LetPatternDestructor>(&expr)) {
            for (auto& p : let->patterns) {
                Visit(*p);
            }
        }
        if (auto bin = DynamicCast<BinaryExpr>(&expr); bin && IsCondition(*bin) && bin->op == TokenKind::AND) {
            Visit(*bin->leftExpr);
            Visit(*bin->rightExpr);
        }
        if (auto paren = DynamicCast<ParenExpr>(&expr)) {
            Visit(*paren->expr);
        }
    }

    void Visit(const Pattern& p)
    {
        if (auto tuple = DynamicCast<TuplePattern>(&p)) {
            return Visit(*tuple);
        }
        if (auto enm = DynamicCast<EnumPattern>(&p)) {
            return Visit(*enm);
        }
        if (auto var = DynamicCast<VarOrEnumPattern>(&p)) {
            if (var->pattern) {
                return Visit(*var->pattern);
            }
        }
        if (auto var = DynamicCast<VarPattern>(&p)) {
            if (var->varDecl) {
                res.push_back(var->varDecl.get());
            }
        }
        if (auto ty = DynamicCast<TypePattern>(&p)) {
            if (ty->desugarVarPattern) {
                return Visit(*ty->desugarVarPattern);
            }
            return Visit(*ty->pattern);
        }
    }

    void Visit(const TuplePattern& p)
    {
        for (auto& v : p.patterns) {
            Visit(*v);
        }
    }

    void Visit(const EnumPattern& p)
    {
        for (auto& v : p.patterns) {
            Visit(*v);
        }
    }

public:
    std::vector<VarDecl*> GetDefinedVars(const Expr& e) &&
    {
        Visit(e);
        return std::move(res);
    }
};
} // namespace

std::unordered_set<Ptr<Decl>> InitializationChecker::CheckAndGetConditionalInitDecls(
    Expr& expr, const std::unordered_set<Ptr<Decl>>& uninitsDecls)
{
    CheckInitInCondition(expr);
    auto inited = std::set<Decl*>{};
    for (auto var : GetPatternVarsImpl{}.GetDefinedVars(expr)) {
        inited.insert(var);
    }
    std::unordered_set<Ptr<Decl>> conditionalInitialized;
    GetInitsInExpr(&expr, conditionalInitialized);
    // Reset status as before.
    for (auto decl : uninitsDecls) {
        if (conditionalInitialized.find(decl) != conditionalInitialized.end() && inited.count(decl) == 0) {
            decl->DisableAttr(Attribute::INITIALIZED);
        }
    }
    return conditionalInitialized;
}

void InitializationChecker::CheckInitInCondBlock(Expr& expr, const std::unordered_set<Ptr<Decl>>& uninitsDecls,
    std::unordered_set<Ptr<Decl>>& commonInitedDeclsOfBranches, bool& firstInitBranchInited)
{
    std::unordered_set<Ptr<Decl>> initedInIfBlock = CheckAndGetConditionalInitDecls(expr, uninitsDecls);
    // If the checked scope is terminated by controflow expression,
    // the update of common inited decls should be ignored for strict checking.
    if (MetControlTransferExpr(expr)) {
        return;
    }
    if (!firstInitBranchInited) {
        commonInitedDeclsOfBranches = initedInIfBlock;
        firstInitBranchInited = true;
    } else {
        // calculate the intersection
        std::unordered_set<Ptr<Decl>> tmpInitedInIfBlock;
        for (auto decl : initedInIfBlock) {
            if (commonInitedDeclsOfBranches.find(decl) != commonInitedDeclsOfBranches.end()) {
                tmpInitedInIfBlock.insert(decl);
            }
        }
        commonInitedDeclsOfBranches = tmpInitedInIfBlock;
    }
}

namespace {
inline void UpdateInitCondition(
    const std::unordered_set<Ptr<Decl>>& commonInitedDecls, const std::unordered_set<Ptr<Decl>>& uninitsDecls)
{
    for (auto decl : commonInitedDecls) {
        if (uninitsDecls.find(decl) != uninitsDecls.end()) {
            decl->EnableAttr(Attribute::INITIALIZED);
        }
    }
}

std::unordered_set<Ptr<Decl>> GetReferencedDeclsBeforeJumpExpr(const Block& b)
{
    std::unordered_set<Ptr<Decl>> decls;
    ConstWalker(&b, [&decls](auto n) {
        auto target = n->GetTarget();
        if (Is<FuncBody>(n) || Is<WhileExpr>(n) || Is<DoWhileExpr>(n) || Is<ForInExpr>(n)) {
            return VisitAction::SKIP_CHILDREN;
        } else if (Is<JumpExpr>(n)) {
            return VisitAction::STOP_NOW;
        } else if (auto expr = DynamicCast<Expr*>(n); expr && expr->IsReferenceExpr() && target) {
            decls.insert(target);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    return decls;
}
} // namespace

bool InitializationChecker::CheckInitInBinaryExpr(const BinaryExpr& be)
{
    if (be.desugarExpr) {
        return CheckInitInExpr(be.desugarExpr.get());
    }
    if (be.op != TokenKind::AND && be.op != TokenKind::OR && be.op != TokenKind::COALESCING) {
        return CheckInitInExpr(be.leftExpr.get()) && CheckInitInExpr(be.rightExpr.get());
    }
    std::unordered_set<Ptr<Decl>> leftUninitsDecls;
    GetUnInitsInExpr(be.leftExpr, leftUninitsDecls);
    std::unordered_set<Ptr<Decl>> rightUninitsDecls;
    GetUnInitsInExpr(be.rightExpr, rightUninitsDecls);
    auto res = CheckInitInExpr(be.leftExpr.get());
    ++optionalCtxDepth;
    res = res && CheckInitInExpr(be.rightExpr.get());
    --optionalCtxDepth;

    // Because of short-circuit, variables in rightExpr is not guaranteed to be initialized.
    for (auto decl : rightUninitsDecls) {
        if (!Utils::In(decl, leftUninitsDecls)) {
            decl->DisableAttr(Attribute::INITIALIZED);
        }
    }
    return res;
}

bool InitializationChecker::CheckInitInCondition(Expr& e)
{
    if (!IsCondition(e)) {
        return CheckInitInExpr(&e);
    }
    if (auto let = DynamicCast<LetPatternDestructor>(&e)) {
        auto res = CheckInitInExpr(let->initializer);
        for (auto var : GetPatternVarsImpl{}.GetDefinedVars(*let)) {
            var->EnableAttr(Attribute::INITIALIZED);
        }
        return res;
    }
    if (auto bin = DynamicCast<BinaryExpr>(&e)) {
        if (bin->op == TokenKind::AND) {
            bool res = CheckInitInCondition(*bin->leftExpr);
            res = CheckInitInCondition(*bin->rightExpr) && res;
            for (auto var : GetPatternVarsImpl{}.GetDefinedVars(*bin)) {
                var->EnableAttr(Attribute::INITIALIZED);
            }
            return res;
        }
    }
    if (auto paren = DynamicCast<ParenExpr>(&e)) {
        return CheckInitInCondition(*paren->expr);
    }
    return CheckInitInExpr(&e);
}

bool InitializationChecker::CheckInitInMatchExpr(MatchExpr& me)
{
    bool result = CheckInitInExpr(me.selector.get());
    // get all uninitialized decls in the match case
    std::unordered_set<Ptr<Decl>> uninitsDecls;
    GetUnInitsInExpr(&me, uninitsDecls);
    std::unordered_set<Ptr<Decl>> commonInitedDeclsOfBranches;
    bool firstInitBranchInited{false};
    for (auto& mc : me.matchCases) {
        if (mc == nullptr || mc->TestAttr(Attribute::UNREACHABLE)) {
            continue;
        }
        for (auto& pattern : mc->patterns) {
            result = result && CheckInitInExpr(pattern.get());
        }
        result = result && CheckInitInExpr(mc->patternGuard.get());
        if (result && mc->exprOrDecls) {
            CheckInitInCondBlock(*mc->exprOrDecls, uninitsDecls, commonInitedDeclsOfBranches, firstInitBranchInited);
        }
    }
    for (auto& mco : me.matchCaseOthers) {
        if (mco == nullptr || mco->TestAttr(Attribute::UNREACHABLE)) {
            continue;
        }
        result = result && CheckInitInExpr(mco->matchExpr.get());
        if (result && mco->exprOrDecls) {
            CheckInitInCondBlock(*mco->exprOrDecls, uninitsDecls, commonInitedDeclsOfBranches, firstInitBranchInited);
        }
    }
    UpdateInitCondition(commonInitedDeclsOfBranches, uninitsDecls);
    return result;
}

bool InitializationChecker::CheckInitInIfExpr(IfExpr& ie)
{
    // The first if condition must be evaluated.
    bool result = CheckInitInCondition(*ie.condExpr);
    std::unordered_set<Ptr<Decl>> uninitsDecls;
    GetUnInitsInExpr(&ie, uninitsDecls);
    bool firstInitBranchInited{false};
    std::unordered_set<Ptr<Decl>> commonInitedDeclsOfBranches;
    Ptr<IfExpr> visitingIe = &ie;
    auto curUninits = uninitsDecls;
    while (visitingIe->thenBody != nullptr) {
        CJC_NULLPTR_CHECK(visitingIe->condExpr);
        if (visitingIe != &ie) {
            auto initedInCondition = CheckAndGetConditionalInitDecls(*visitingIe->condExpr, curUninits);
            Utils::EraseIf(curUninits, [&initedInCondition](auto it) { return initedInCondition.count(it) != 0; });
        }
        CheckInitInCondBlock(*visitingIe->thenBody, curUninits, commonInitedDeclsOfBranches, firstInitBranchInited);
        if (!visitingIe->hasElse) {
            commonInitedDeclsOfBranches.clear();
            break;
        }
        // Get the next else body
        if (auto eif = DynamicCast<IfExpr*>(visitingIe->elseBody.get()); eif) {
            visitingIe = eif;
        } else {
            // last else block without condition
            if (auto elseBlock = DynamicCast<Block*>(visitingIe->elseBody.get()); elseBlock) {
                CheckInitInCondBlock(*elseBlock, curUninits, commonInitedDeclsOfBranches, firstInitBranchInited);
            }
            break;
        }
    }
    UpdateInitCondition(commonInitedDeclsOfBranches, uninitsDecls);
    return result;
}

void InitializationChecker::CheckInitInLoop(Ptr<Block> block, bool shouldUnset)
{
    if (!block) {
        return;
    }
    std::unordered_set<Ptr<Decl>> uninitedDecls;
    for (auto& n : block->body) {
        GetUnInitsInExpr(n.get(), uninitedDecls);
    }
    std::unordered_set<Ptr<Decl>> uninitedLocalDecls;
    GetLocalUnInitsInExpr(block, uninitedLocalDecls);
    CheckInitialization(block);
    std::unordered_set<Ptr<Decl>> initedDecls;
    for (auto& n : block->body) {
        GetInitsInExpr(n.get(), initedDecls);
    }
    if (shouldUnset) {
        for (auto decl : uninitedDecls) {
            if (initedDecls.count(decl) != 0) {
                decl->DisableAttr(Attribute::INITIALIZED);
            }
        }
    } else {
        auto declsBeforeJump = GetReferencedDeclsBeforeJumpExpr(*block);
        for (auto decl : uninitedDecls) {
            if (declsBeforeJump.count(decl) == 0) {
                decl->DisableAttr(Attribute::INITIALIZED);
            }
        }
    }
}

bool InitializationChecker::CheckIllegalMemberAccess(const Expr& expr, const Decl& target, const Node& targetStruct)
{
    // This check for the rule that before all member variables have been initialized:
    //   1. 'this' cannot be captured by nested function or lambda;
    //   2. member function or property is not allowed to be used.
    CJC_NULLPTR_CHECK(expr.curFile);
    auto curFuncBody = GetCurFuncBody(ctx, expr.scopeName);
    if (curFuncBody == nullptr) {
        if (IsFuncOrProp(target)) {
            // Member function or property is not allowed to be used inside other member variable's initializer.
            diag.Diagnose(expr, DiagKind::sema_illegal_usage_of_member, target.identifier.Val());
            return false;
        }
        return true;
    }
    auto outerMostFuncSym = ScopeManager::GetOutMostSymbol(ctx, SymbolKind::FUNC, expr.scopeName);
    auto outerMostFunc = outerMostFuncSym ? StaticCast<FuncDecl*>(outerMostFuncSym->node) : nullptr;
    bool notInMemberFunc = !outerMostFunc || !outerMostFunc->outerDecl || !outerMostFunc->outerDecl->IsNominalDecl();
    if (notInMemberFunc) {
        // Member is captured by lambda or function which defined inside other member variable's initializer.
        diag.Diagnose(expr, DiagKind::sema_illegal_usage_of_member, target.identifier.Val());
        return false;
    }

    bool leftVaraible = expr.TestAttr(Attribute::LEFT_VALUE) && target.astKind == ASTKind::VAR_DECL;
    if (IsAllInitialized(targetStruct) || leftVaraible) {
        return true;
    }
    // Not all member variables have been initialized, check for forbidden usages.
    // Since ''symOfExprCurFunc' not null, outer most func must exist.
    if (!IsInstanceConstructor(*outerMostFunc)) {
        // When using member in normal instance function/property,
        // the object must be created that all members should be initialized.
        return true;
    }
    // 1. member function or property is not allowed to be used.
    // 2. when current function is not the outer most function, it indicates 'this' has been captured.
    if (IsFuncOrProp(target) || curFuncBody->funcDecl != outerMostFunc) {
        diag.Diagnose(expr, DiagKind::sema_illegal_usage_of_member, target.identifier.Val());
        return false;
    }
    return true;
}

bool InitializationChecker::CheckIllegalRefExprAccess(
    const RefExpr& re, const Symbol& toplevelSymOfRe, const Symbol& toplevelSymOfTarget)
{
    CJC_NULLPTR_CHECK(toplevelSymOfRe.node);
    CJC_NULLPTR_CHECK(toplevelSymOfTarget.node);
    if (&toplevelSymOfRe == &toplevelSymOfTarget) {
        // Check current class/struct member access
        CJC_NULLPTR_CHECK(re.ref.target);
        return CheckIllegalMemberAccess(re, *re.ref.target, *toplevelSymOfRe.node);
    }
    // Check super class/interface member access
    Symbol* symOfExprFunc = ScopeManager::GetOutMostSymbol(ctx, SymbolKind::FUNC_LIKE, re.scopeName);
    if (re.ref.target != nullptr && re.ref.target->astKind == ASTKind::VAR_DECL) {
        if (symOfExprFunc == nullptr || symOfExprFunc->astKind != AST::ASTKind::FUNC_DECL ||
            (symOfExprFunc->astKind == AST::ASTKind::FUNC_DECL &&
                !Is<InheritableDecl>(StaticAs<ASTKind::FUNC_DECL>(symOfExprFunc->node)->outerDecl))) {
            diag.Diagnose(re, DiagKind::sema_illegal_usage_of_super_member, re.ref.identifier.Val());
            return false;
        }
    } else if (re.ref.target && IsFuncOrProp(*re.ref.target) &&
        !CheckIllegalMemberAccess(re, *re.ref.target, *toplevelSymOfRe.node)) {
        return false;
    }
    return true;
}

void InitializationChecker::CheckInitInTypeDecl(
    const std::vector<OwnedPtr<Decl>>& decls, const std::vector<Ptr<Decl>>& superClassNonFuncDecls)
{
    CollectDeclsInfo info{};

    for (auto& d : decls) {
        CollectDecls(d, info);
    }

    // 1. non funcs
    for (auto nfd : info.nonFuncDecls) {
        CheckInitialization(nfd);
    }
    // get the not declared-initialized member variables
    std::vector<Ptr<Decl>> unInitNonFuncDecls;
    for (auto decl : info.nonFuncDecls) {
        if (auto vd = DynamicCast<VarDecl*>(decl); vd && IsInitializedButUnReachable(*vd)) {
            unInitNonFuncDecls.emplace_back(decl);
        } else if (!decl->TestAttr(Attribute::INITIALIZED)) {
            unInitNonFuncDecls.emplace_back(decl); // class.uninits
            decl->EnableAttr(Attribute::INITIALIZED);
        }
    }
    // get the not declared-initialized member variables in super class
    for (auto decl : superClassNonFuncDecls) {
        if (!decl->TestAttr(Attribute::INITIALIZED)) {
            unInitNonFuncDecls.emplace_back(decl);
            decl->EnableAttr(Attribute::INITIALIZED);
        }
    }
    // 2. static functions   (include static properties)
    for (auto sfd : info.staticFuncDecls) {
        CheckInitialization(sfd);
    }
    // 3. constructors
    for (auto fd : info.initFuncDecls) {
        if (fd == nullptr || fd->TestAttr(Attribute::INITIALIZATION_CHECKED)) {
            continue;
        }
        CheckInitInConstructors(*fd, unInitNonFuncDecls);
        ctorUninitVarsMap.erase(fd);
    }
    // 4. non-static functions (include no-static properties)
    for (auto fd : info.funcDecls) {
        CheckInitialization(fd);
    }
}

void InitializationChecker::CheckInitInConstructors(FuncDecl& fd, const std::vector<Ptr<Decl>>& unInitNonFuncDecls)
{
    if (fd.constructorCall != ConstructorCall::OTHER_INIT) {
        std::for_each(unInitNonFuncDecls.begin(), unInitNonFuncDecls.end(),
            [](Ptr<Decl> decl) { decl->DisableAttr(Attribute::INITIALIZED); });
    }
    CheckInitialization(&fd);
    if (fd.funcBody == nullptr || opts.compileCjd) {
        return; // If the function is broken, we can quit now.
    }
    if (fd.constructorCall == ConstructorCall::OTHER_INIT ||
        (fd.funcBody->parentClassLike &&
            (fd.funcBody->parentClassLike->TestAttr(Attribute::FOREIGN) ||
                HasJavaAttr(*fd.funcBody->parentClassLike)))) {
        for (auto decl : unInitNonFuncDecls) {
            decl->EnableAttr(Attribute::INITIALIZED);
        }
        return;
    }
    // If the constructor is terminated by 'ThrowExpr' directly,
    // the initialization check can be ignored, since the object will not be created.
    auto found = scopeTerminationKinds.find(fd.funcBody->scopeName);
    if (found != scopeTerminationKinds.end() && found->second == ASTKind::THROW_EXPR) {
        return;
    }
    auto definitelyUninitVars = ctorUninitVarsMap[&fd];
    for (auto decl : unInitNonFuncDecls) {
        if (decl->TestAttr(Attribute::INITIALIZED) && definitelyUninitVars.count(decl) == 0) {
            continue;
        }
        if (fd.TestAttr(Attribute::COMPILER_ADD, Attribute::PRIMARY_CONSTRUCTOR)) {
            diag.Diagnose(fd, DiagKind::sema_class_uninitialized_field, decl->identifier.Val());
        } else {
            diag.Diagnose(fd.TestAttr(Attribute::COMPILER_ADD) ? *decl : fd, DiagKind::sema_class_uninitialized_field,
                decl->identifier.Val());
        }
    }
}

void InitializationChecker::CollectDecls(const OwnedPtr<Decl>& decl, CollectDeclsInfo& info)
{
    if (decl->astKind == AST::ASTKind::PRIMARY_CTOR_DECL || decl->astKind == AST::ASTKind::MACRO_EXPAND_DECL) {
        return;
    }
    if (auto fd = DynamicCast<FuncDecl*>(decl.get()); fd) {
        fd->EnableAttr(Attribute::INITIALIZED);
        if (fd->TestAttr(Attribute::STATIC)) {
            info.staticFuncDecls.emplace_back(fd);
        } else {
            if (fd->TestAttr(Attribute::CONSTRUCTOR)) {
                info.initFuncDecls.emplace_back(fd);
            } else {
                info.funcDecls.emplace_back(fd);
            }
        }
    } else if (auto pd = DynamicCast<PropDecl*>(decl.get()); pd) {
        pd->EnableAttr(Attribute::INITIALIZED);
        CollectDeclsFromPropDecl(decl, *pd, info.funcDecls, info.staticFuncDecls);
    } else {
        if (decl->TestAttr(Attribute::STATIC)) {
            CheckInitialization(decl.get());
        } else {
            info.nonFuncDecls.emplace_back(decl.get());
        }
    }
}

void InitializationChecker::GetNonFuncDeclsInSuperClass(
    const ClassDecl& cd, std::vector<Ptr<Decl>>& superClassNonFuncDecls, std::set<Ptr<Decl>>& superClasses)
{
    if (cd.body) {
        for (auto& decl : cd.body->decls) {
            if (auto vd = DynamicCast<VarDecl*>(decl.get());
                vd && !vd->TestAttr(Attribute::PRIVATE) && vd->astKind != ASTKind::PROP_DECL) {
                superClassNonFuncDecls.emplace_back(decl.get());
            }
        }
    }
    for (auto& it : cd.inheritedTypes) {
        if (auto superClass = DynamicCast<ClassDecl*>(it->GetTarget()); superClass) {
            if (superClasses.find(superClass) != superClasses.end()) {
                superClasses.insert(superClass);
                GetNonFuncDeclsInSuperClass(*superClass, superClassNonFuncDecls, superClasses);
            } else {
                return;
            }
        }
    }
}

bool InitializationChecker::IsVarUsedBeforeDefinition(const Node& checkNode, Node& targetNode) const
{
    bool validNodes = checkNode.begin != INVALID_POSITION && targetNode.begin != INVALID_POSITION &&
        checkNode.curFile && targetNode.curFile;
    if (validNodes) {
        auto isSameFile = checkNode.astKind != ASTKind::FILE && targetNode.astKind == ASTKind::VAR_DECL &&
            targetNode.curFile->fileHash == checkNode.curFile->fileHash;
        if (!isSameFile) {
            return false;
        }
        if (checkNode.IsStaticOrGlobal() && targetNode.IsStaticOrGlobal() && checkNode.begin < targetNode.begin) {
            return true;
        }
        if (!IsNode1ScopeVisibleForNode2(targetNode, checkNode)) {
            return false;
        }

        auto isConflictInSameScope =
            checkNode.scopeLevel == targetNode.scopeLevel && checkNode.begin < targetNode.begin;
        if (isConflictInSameScope) {
            return true;
        }

        auto isInDiffScope =
            checkNode.scopeLevel > targetNode.scopeLevel && !IsInStructDecl(*StaticAs<ASTKind::VAR_DECL>(&targetNode));
        if (!isInDiffScope) {
            return false;
        }
        auto outerScope = ScopeManager::GetCurOuterDeclOfScopeLevelX(ctx, checkNode, targetNode.scopeLevel);
        auto outerTry = outerScope ? As<ASTKind::TRY_EXPR>(outerScope->node) : nullptr;
        auto isCompilerAddTryExpr = outerTry && outerTry->isDesugaredFromTryWithResources;
        // See CreateInnerFinallyBlock in the desugar.cpp for the reasons.
        if (outerScope && outerScope->node->astKind == ASTKind::BLOCK) {
            auto b = StaticAs<ASTKind::BLOCK>(outerScope->node);
            if (b->body.size() > 1) {
                auto innerTry = As<ASTKind::TRY_EXPR>(b->body.at(0).get());
                isCompilerAddTryExpr = isCompilerAddTryExpr || (innerTry && innerTry->isDesugaredFromTryWithResources);
            }
        }
        auto isConflictInDiffScope = outerScope && outerScope->node->begin < targetNode.begin;
        return isConflictInDiffScope && !isCompilerAddTryExpr;
    }
    return false;
}

bool InitializationChecker::IsOrderRelated(const Node& checkNode, Node& targetNode, bool isClassLikeOrStruct) const
{
    if (IsVarUsedBeforeDefinition(checkNode, targetNode)) {
        return true;
    }
    // Toplevel(except for var to var) or static decls are not order related.
    return !(targetNode.scopeLevel == 0 || isClassLikeOrStruct);
}
