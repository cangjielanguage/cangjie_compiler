// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Desugar functions used after instantiation step.
 */

#include <memory>
#include <utility>
#include <vector>

#include "AutoBoxing.h"
#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;
using namespace Meta;
using namespace TypeCheckUtil;

// Perform desugar after generic instantiation.
void TypeChecker::PerformDesugarAfterInstantiation(ASTContext& ctx, Package& pkg) const
{
    impl->PerformDesugarAfterInstantiation(ctx, pkg);
}

namespace {
void UpdateDeclAttributes(Package& pkg, bool exportForTest)
{
    Walker(&pkg, [&exportForTest](auto node) {
        if (auto vd = DynamicCast<VarDecl*>(node); vd && vd->initializer) {
            vd->EnableAttr(Attribute::DEFAULT);
        }
        if (exportForTest) {
            if (auto fd = DynamicCast<FuncDecl*>(node); fd && !fd->TestAttr(Attribute::PRIVATE)) {
                auto isExtend = Is<ExtendDecl>(fd->outerDecl);
                auto isForeignFunc = fd->TestAttr(Attribute::FOREIGN);
                if (!isExtend && !isForeignFunc) {
                    return VisitAction::WALK_CHILDREN;
                }
                // Skip declarations added by the compiler because they wouldn't be accessible in tests anyway
                if (isExtend && fd->outerDecl->TestAttr(Attribute::COMPILER_ADD)) {
                    return VisitAction::WALK_CHILDREN;
                }
                fd->linkage = Linkage::EXTERNAL;
                if (fd->propDecl) {
                    fd->propDecl->linkage = Linkage::EXTERNAL;
                }
                if (isExtend) {
                    fd->outerDecl->EnableAttr(Attribute::FOR_TEST);
                } else {
                    fd->EnableAttr(Attribute::FOR_TEST);
                }
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

/**
 * For compiled with the `--coverage` option,
 * Clear line info for:
 *   - Compiler add by generic instantiation.
 */
void ClearLineInfoAfterSema(Package& pkg)
{
    std::function<VisitAction(Ptr<Node>)> clearGenericInst = [](Ptr<Node> node) -> VisitAction {
        node->begin.line = 0;
        node->begin.column = 0;
        node->begin.Mark(PositionStatus::IGNORE);
        return VisitAction::WALK_CHILDREN;
    };
    for (auto& decl : pkg.genericInstantiatedDecls) {
        Walker(decl, clearGenericInst).Walk();
    }
}

void IterateTyWithArgs(ModalTy ty, const std::function<void(ModalTy)>& func)
{
    if (!Ty::IsTyCorrect(ty)) {
        return;
    }
    func(ty);
    for (auto& tyArg : ty->typeArgs) {
        IterateTyWithArgs(tyArg, func);
    }
}
} // namespace

VisitAction TypeChecker::TypeCheckerImpl::MarkUsedDecl(Ptr<Decl> decl, unsigned walkerId)
{
    // Used generic instantiated declarations and it's original generic declaration should be marked as used.
    if (decl->TestAttr(Attribute::GENERIC_INSTANTIATED) && decl->genericDecl &&
        decl->genericDecl->TestAttr(Attribute::IMPORTED)) {
        MarkUsedNode(decl->genericDecl, walkerId);
    }
    // Imported declarations should be marked as used.
    if (!decl->TestAttr(Attribute::IMPORTED)) {
        return VisitAction::WALK_CHILDREN;
    }
    // Already marked declarations should be skipped to avoid infinite loop.
    if (decl->isUsedImports) {
        return VisitAction::SKIP_CHILDREN;
    }
    decl->isUsedImports = true;
    // Property declarations of function declarations should be marked as used.
    if (auto fd = DynamicCast<FuncDecl>(decl); fd && fd->propDecl) {
        fd->propDecl->isUsedImports = true;
    }
    // If a member is used, then it's enclosing type should be marked as used.
    if (decl->outerDecl) {
        MarkUsedNode(decl->outerDecl, walkerId);
    }
    if (!decl->IsNominalDecl()) {
        return VisitAction::WALK_CHILDREN;
    }
    // Extends of nominal types should be marked as used.
    auto extends = typeManager.GetAllExtendsByTy(*decl->GetTy());
    for (auto extend : extends) {
        if (extend == decl || !extend->TestAttr(Attribute::IMPORTED)) {
            continue;
        }
        MarkUsedNode(extend, walkerId);
    }
    return VisitAction::WALK_CHILDREN;
}

void TypeChecker::TypeCheckerImpl::MarkUsedNode(Ptr<Node> node, unsigned walkerId)
{
    std::function<VisitAction(Ptr<Node>)> collectUsed = [this, &walkerId, &collectUsed](Ptr<Node> node) -> VisitAction {
        auto procTy = [&walkerId, &collectUsed](ModalTy ty) {
            if (auto decl = Ty::GetDeclOfTy(ty)) {
                Walker(decl, walkerId, collectUsed).Walk();
            }
        };
        IterateTyWithArgs(node->GetTy(), procTy);
        if (auto type = DynamicCast<Type>(node); type && !Ty::IsInitialTy(type->aliasTy)) {
            IterateTyWithArgs(type->aliasTy, procTy);
        } else if (auto decl = DynamicCast<Decl>(node)) {
            return MarkUsedDecl(decl, walkerId);
        } else if (auto ae = DynamicCast<ArrayExpr>(node); ae && ae->initFunc) {
            Walker(ae->initFunc, walkerId, collectUsed).Walk();
        }
        Ptr<Decl> target = node->GetTarget();
        if (auto ce = DynamicCast<CallExpr>(node); ce && ce->resolvedFunction) {
            target = ce->resolvedFunction;
        }
        if (!target) {
            return VisitAction::WALK_CHILDREN;
        }
        Walker(target, walkerId, collectUsed).Walk();
        return VisitAction::WALK_CHILDREN;
    };
    Walker(node, walkerId, collectUsed).Walk();
}
/*
 * Mark used declarations of imported package.
 * 1. Uesd global variables.
 * 2. Uesd global functions.
 * 3. Uesd custom type and all members.
 * 4. Uesd custom type's extends and all members.
 * 5. Built-in type's extends and all members.
 * 6. Uesd type alias decl and it's target.
 *
 * All imported declarations without 'Attribute::USED_IMPORTS' will be ignored on mangling and chir.
 */
void TypeChecker::TypeCheckerImpl::MarkUsedPackageInFile(Ptr<Package> pkg)
{
    const std::unordered_set<std::string_view> implicitUsedDecls = {"CJ_CORE_ExecAtexitCallbacks", "getCommandLineArgs",
        "eprintln", "handleException", "NegativeArraySizeException", "createOverflowExceptionMsg",
        "createArithmeticExceptionMsg", "SpawnException"};
    unsigned walkerId = Walker::GetNextWalkerID();
    // Step 1: Mark implicit used declarations.
    for (auto implicitUsedDecl : implicitUsedDecls) {
        auto decl = importManager.GetCoreDecl(std::string(implicitUsedDecl));
        MarkUsedNode(decl, walkerId);
    }
    // Step 2: Mark used extend of builtin types.
    for (auto& [_, builtinExtends] : typeManager.builtinTyToExtendMap) {
        for (auto& extend : builtinExtends) {
            MarkUsedNode(extend, walkerId);
        }
    }
    // Step 3: Mark used declarations in files.
    for (auto& file : pkg->files) {
        MarkUsedNode(file.get(), walkerId);
    }
    // Step 4: Mark used declarations of generic instantiated declarations and it's dependencies.
    for (auto& instDecl : pkg->genericInstantiatedDecls) {
        MarkUsedNode(instDecl.get(), walkerId);
    }
    // Delete unused imported declarations.
    auto isUnusedDecls = [](Ptr<Decl> decl) { return !decl->isUsedImports; };
    Utils::EraseIf(pkg->srcImportedNonGenericDecls, isUnusedDecls);
    Utils::EraseIf(pkg->inlineFuncDecls, isUnusedDecls);
}

void TypeChecker::TypeCheckerImpl::PerformDesugarAfterInstantiation([[maybe_unused]] ASTContext& ctx, Package& pkg)
{
    if (pkg.files.empty()) {
        return;
    }
    PerformRecursiveTypesElimination();
    UpdateDeclAttributes(pkg, ci->invocation.globalOptions.exportForTest);
    if (ci->invocation.globalOptions.enableCoverage) {
        ClearLineInfoAfterSema(pkg);
    }
    Utils::ProfileRecorder markUnusedRecord("PerformDesugarAfterInstantiation", "MarkUsedPackageInFile");
    MarkUsedPackageInFile(&pkg);
}

bool AutoBoxing::NeedBoxOption(ModalTy child, ModalTy target)
{
    if (Ty::IsInitialTy(child.Ty()) || Ty::IsInitialTy(target.Ty()) ||
        (typeManager.CheckTypeCompatibility(child, target, false, target->IsGeneric()) !=
            TypeCompatibility::INCOMPATIBLE) ||
        child->kind == TypeKind::TYPE_NOTHING || target->kind != TypeKind::TYPE_ENUM) {
        return false;
    }
    auto lCnt = CountOptionNestedLevel(child.Ty());
    auto rCnt = CountOptionNestedLevel(target.Ty());
    // If type contains generic ty, current is node inside @Java class. Otherwise, incompatible types need to be boxed.
    if (lCnt == rCnt && child->HasGeneric()) {
        return false;
    }
    auto enumTy = RawStaticCast<EnumTy*>(target.Ty());
    if (enumTy->declPtr->fullPackageName != CORE_PACKAGE_NAME || enumTy->declPtr->identifier != STD_LIB_OPTION) {
        return false;
    }
    return true;
}

// Option Box happens twice before and after instantiation, and must before extend box.
void AutoBoxing::TryOptionBox(EnumTy& target, Expr& expr)
{
    if (expr.GetTy() && target.typeArgs[0] && NeedBoxOption(expr.GetTy(), target.typeArgs[0])) {
        TryOptionBox(*RawStaticCast<EnumTy*>(target.typeArgs[0].Ty()), expr);
    }
    auto ed = target.decl;
    Ptr<FuncDecl> optionDecl = nullptr;
    for (auto& it : ed->constructors) {
        if (it->identifier == OPTION_VALUE_CTOR) {
            optionDecl = StaticCast<FuncDecl*>(it.get());
            break;
        }
    }
    if (optionDecl == nullptr) {
        return;
    }

    auto baseFunc = CreateRefExpr(OPTION_VALUE_CTOR);
    baseFunc->EnableAttr(Attribute::IMPLICIT_ADD);
    baseFunc->ref.target = optionDecl;
    baseFunc->SetTy(typeManager.GetInstantiatedTy(optionDecl->GetTy(), GenerateTypeMapping(*ed, target.TyArgs())));

    std::vector<OwnedPtr<FuncArg>> arg;
    if (expr.desugarExpr) {
        arg.emplace_back(CreateFuncArg(std::move(expr.desugarExpr)));
    } else {
        arg.emplace_back(CreateFuncArg(ASTCloner::Clone(Ptr(&expr))));
    }

    auto ce = CreateCallExpr(std::move(baseFunc), std::move(arg));
    ce->callKind = AST::CallKind::CALL_DECLARED_FUNCTION;
    ce->SetTy({&target});
    ce->resolvedFunction = optionDecl;
    if (expr.astKind == ASTKind::BLOCK) {
        // For correct deserialization, we need to keep type of block.
        auto b = MakeOwnedNode<Block>();
        b->SetTy(ce->GetTy());
        b->body.emplace_back(std::move(ce));
        expr.desugarExpr = std::move(b);
    } else {
        expr.desugarExpr = std::move(ce);
    }
    AddCurFile(*expr.desugarExpr, expr.curFile);
    expr.SetTy(expr.desugarExpr->GetTy());
}

/**
 * Option Box happens before type check finished with no errors.
 * All nodes and sema types should be valid.
 */
void AutoBoxing::AddOptionBox(Package& pkg)
{
    std::function<VisitAction(Ptr<Node>)> preVisit = [this](Ptr<Node> node) -> VisitAction {
        return match(*node)([this](const VarDecl& vd) { return AddOptionBoxHandleVarDecl(vd); },
            [this](const AssignExpr& ae) { return AddOptionBoxHandleAssignExpr(ae); },
            [this](CallExpr& ce) { return AddOptionBoxHandleCallExpr(ce); },
            [this](const IfExpr& ie) { return AddOptionBoxHandleIfExpr(ie); },
            [this](TryExpr& te) { return AddOptionBoxHandleTryExpr(te); },
            [this](const ReturnExpr& re) { return AddOptionBoxHandleReturnExpr(re); },
            [this](ArrayLit& lit) { return AddOptionBoxHandleArrayLit(lit); },
            [this](MatchExpr& me) { return AddOptionBoxHandleMatchExpr(me); },
            [this](const TupleLit& tl) { return AddOptionBoxHandleTupleList(tl); },
            [this](ArrayExpr& ae) { return AddOptionBoxHandleArrayExpr(ae); },
            []() { return VisitAction::WALK_CHILDREN; });
    };
    Walker walker(&pkg, preVisit);
    walker.Walk();
}

VisitAction AutoBoxing::AddOptionBoxHandleTupleList(const TupleLit& tl)
{ // Tuple literal allows element been boxed.
    auto tupleTy = DynamicCast<TupleTy>(tl.DataTy());
    if (tupleTy == nullptr) {
        return VisitAction::WALK_CHILDREN;
    }
    auto typeArgs = tupleTy->TyArgs();
    for (size_t i = 0; i < typeArgs.size(); ++i) {
        if (tl.children[i]->GetTy() && typeArgs[i] && NeedBoxOption(tl.children[i]->GetTy(), typeArgs[i])) {
            TryOptionBox(*RawStaticCast<EnumTy*>(typeArgs[i]), *tl.children[i]);
        }
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction AutoBoxing::AddOptionBoxHandleMatchExpr(MatchExpr& me)
{
    for (auto& single : me.matchCases) {
        CJC_ASSERT(me.GetTy() && single->exprOrDecls);
        AddOptionBoxHandleBlock(*single->exprOrDecls, me.GetTy());
    }
    for (auto& caseOther : me.matchCaseOthers) {
        CJC_ASSERT(me.GetTy() && caseOther->exprOrDecls);
        AddOptionBoxHandleBlock(*caseOther->exprOrDecls, me.GetTy());
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction AutoBoxing::AddOptionBoxHandleArrayLit(ArrayLit& lit)
{
    if (Ty::IsInitialTy(lit.DataTy()) || !lit.GetTy()->IsStructArray()) {
        return VisitAction::WALK_CHILDREN;
    }

    if (lit.GetTy()->typeArgs.size() == 1) {
        auto targetTy = lit.GetTy()->TyArg(0);
        CJC_NULLPTR_CHECK(targetTy);
        for (auto& child : lit.children) {
            if (child->GetTy() && NeedBoxOption(child->GetTy(), targetTy)) {
                TryOptionBox(*RawStaticCast<EnumTy*>(targetTy), *child);
            }
        }
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction AutoBoxing::AddOptionBoxHandleIfExpr(const IfExpr& ie)
{
    if (!ie.GetTy().IsCorrect() || ie.GetTy()->IsUnitOrNothing() || !ie.thenBody) {
        return VisitAction::WALK_CHILDREN;
    }
    AddOptionBoxHandleBlock(*ie.thenBody, ie.GetTy());
    if (ie.hasElse && ie.elseBody) {
        if (auto block = DynamicCast<Block*>(ie.elseBody.get()); block) {
            AddOptionBoxHandleBlock(*block, ie.GetTy());
        } else if (auto elseIfExpr = DynamicCast<IfExpr*>(ie.elseBody.get());
            elseIfExpr && elseIfExpr->GetTy().IsCorrect() && NeedBoxOption(elseIfExpr->GetTy(), ie.GetTy())) {
            TryOptionBox(*RawStaticCast<EnumTy*>(ie.DataTy()), *elseIfExpr);
            elseIfExpr->SetTy(ie.GetTy());
        }
    }
    return VisitAction::WALK_CHILDREN;
}

void AutoBoxing::AddOptionBoxHandleBlock(Block& block, ModalTy ty)
{
    // If the block is empty or end with declaration, the last type is 'Unit',
    // otherwise the last type is the type of last expression.
    auto lastExprOrDecl = block.GetLastExprOrDecl();
    ModalTy lastTy = {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)};
    if (auto expr = DynamicCast<Expr*>(lastExprOrDecl)) {
        lastTy = expr->GetTy();
    }
    if (!lastTy || !NeedBoxOption(lastTy, ty)) {
        return;
    }
    // If the block is empty or end with declaration, we need to insert a unitExpr for box.
    if (Is<Decl>(lastExprOrDecl) || block.body.empty()) {
        auto unitExpr = CreateUnitExpr({TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
        unitExpr->curFile = block.curFile;
        lastExprOrDecl = unitExpr.get();
        block.body.emplace_back(std::move(unitExpr));
    }

    if (auto lastExpr = DynamicCast<Expr*>(lastExprOrDecl)) {
        TryOptionBox(StaticCast<EnumTy>(*ty), *lastExpr);
        block.SetTy(lastExpr->GetTy());
    }
}

VisitAction AutoBoxing::AddOptionBoxHandleTryExpr(TryExpr& te)
{
    if (!te.GetTy().IsCorrect()) {
        return VisitAction::WALK_CHILDREN;
    }
    if (te.tryBlock) {
        AddOptionBoxHandleBlock(*te.tryBlock, te.GetTy());
    }
    for (auto& ce : te.catchBlocks) {
        AddOptionBoxHandleBlock(*ce, te.GetTy());
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction AutoBoxing::AddOptionBoxHandleArrayExpr(ArrayExpr& ae)
{
    bool ignore = !ae.GetTy().IsCorrect() || ae.initFunc || ae.args.size() < 1;
    if (ignore) {
        return VisitAction::WALK_CHILDREN;
    }
    auto targetTy = typeManager.GetTypeArgs(*ae.GetTy())[0];

    Ptr<FuncArg> arg = nullptr;
    if (ae.isValueArray) {
        // For VArray only one argument, and it need option box.
        arg = ae.args[0].get();
    } else if (ae.args.size() > 1) {
        // For RawArray(size, item:T) boxing argIndex is 1, only this case may need option box.
        arg = ae.args[1].get();
    }
    if (arg && arg->expr && arg->expr->GetTy() && targetTy && NeedBoxOption(arg->expr->GetTy(), ModalTy{targetTy})) {
        TryOptionBox(*RawStaticCast<EnumTy*>(targetTy.get()), *arg->expr);
        arg->SetTy(arg->expr->GetTy());
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction AutoBoxing::AddOptionBoxHandleCallExpr(CallExpr& ce)
{
    bool ignored = !ce.baseFunc || !ce.baseFunc->GetTy() || ce.baseFunc->TyKind() != TypeKind::TYPE_FUNC;
    if (ignored) {
        return VisitAction::WALK_CHILDREN;
    }
    auto funcTy = RawStaticCast<FuncTy*>(ce.baseFunc->DataTy());
    unsigned count = 0;
    auto callCheck = [&count, &funcTy, this](auto begin, auto end) {
        for (auto it = begin; it != end; ++it) {
            if (count >= funcTy->paramTys.size()) {
                break;
            }
            auto paramTy = funcTy->paramTys[count];
            // It's possible that childs have different box type, so does not break after match.
            if ((*it)->expr && (*it)->expr->GetTy() && paramTy && NeedBoxOption((*it)->expr->GetTy(), paramTy)) {
                TryOptionBox(*RawStaticCast<EnumTy*>(paramTy.Ty()), *(*it)->expr);
                (*it)->SetTy((*it)->expr->GetTy());
            }
            ++count;
        }
    };
    if (ce.desugarArgs.has_value()) {
        callCheck(ce.desugarArgs->begin(), ce.desugarArgs->end());
    } else {
        callCheck(ce.args.begin(), ce.args.end());
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction AutoBoxing::AddOptionBoxHandleAssignExpr(const AssignExpr& ae)
{
    if (ae.desugarExpr) {
        return VisitAction::WALK_CHILDREN;
    }
    if (ae.rightExpr->GetTy() && ae.leftValue->GetTy() && NeedBoxOption(ae.rightExpr->GetTy(), ae.leftValue->GetTy())) {
        TryOptionBox(*RawStaticCast<EnumTy*>(ae.leftValue->DataTy()), *ae.rightExpr);
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction AutoBoxing::AddOptionBoxHandleVarDecl(const VarDecl& vd)
{
    if (vd.initializer && vd.initializer->GetTy() && vd.GetTy() && NeedBoxOption(vd.initializer->GetTy(), vd.GetTy())) {
        TryOptionBox(*RawStaticCast<EnumTy*>(vd.DataTy()), *vd.initializer);
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction AutoBoxing::AddOptionBoxHandleReturnExpr(const ReturnExpr& re)
{
    if (re.expr && re.refFuncBody && re.refFuncBody->GetTy() && re.refFuncBody->TyKind() == TypeKind::TYPE_FUNC) {
        auto funcTy = RawStaticCast<FuncTy*>(re.refFuncBody->DataTy());
        if (re.expr->GetTy() && funcTy->retTy && NeedBoxOption(re.expr->GetTy(), funcTy->retTy)) {
            auto expr = re.desugarExpr ? re.desugarExpr.get() : re.expr.get();
            TryOptionBox(*RawStaticCast<EnumTy*>(funcTy->retTy.Ty()), *expr);
        }
    }
    return VisitAction::WALK_CHILDREN;
}
