// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the function is inline.
 */

#include "TypeCheckerImpl.h"

#include <memory>
#include <queue>
#include <unordered_set>

#include "TypeCheckUtil.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie;
using namespace AST;
using namespace std::placeholders;

namespace {
constexpr size_t MAX_NODE_NUMBER = 32;

bool CanExportForInline(const FuncDecl& fd)
{
    bool ret = true;
    if (fd.outerDecl) {
        if (!fd.outerDecl->IsExportedDecl()) {
            ret = false;
        }
    }
    auto checkFd = fd.ownerFunc ? fd.ownerFunc.get() : &fd;
    return checkFd->IsExportedDecl() && ret;
}

bool CanCountedRefExprOrMemberExpr(const Expr& expr)
{
    Ptr<Decl> target = expr.GetTarget();
    if (target == nullptr) {
        return true;
    }
    if (target->astKind == ASTKind::VAR_DECL) {
        // If variable is a global definition, it must be external.
        // If variable is a static member decl, it must be public or protected member in exported decl.
        // NOTE: instance member variable can always be accessed from object no matter is public or private.
        if (target->TestAttr(Attribute::GLOBAL) && !target->IsExportedDecl()) {
            return false;
        } else if (target->TestAttr(Attribute::STATIC) && target->outerDecl && target->outerDecl->IsNominalDecl() &&
            (!target->outerDecl->IsExportedDecl() || !target->IsExportedDecl())) {
            return false;
        }
    } else if (target->astKind == ASTKind::FUNC_DECL) {
        auto funcDecl = RawStaticCast<FuncDecl*>(target);
        if (funcDecl->TestAttr(Attribute::INTRINSIC)) {
            return true;
        }
        if (funcDecl->TestAnyAttr(Attribute::FOREIGN, Attribute::C) || !CanExportForInline(*funcDecl)) {
            return false;
        }
    }
    return true;
}

bool ContainsInternalType(const Ptr<Ty>& ty)
{
    if (!Ty::IsTyCorrect(ty)) {
        return true;
    }
    // If decl is not external and not generic, it is internal used type decl.
    if (auto decl = Ty::GetDeclPtrOfTy(ty);
        decl && !decl->IsExportedDecl() && !decl->TestAttr(Attribute::GENERIC)) {
        return true;
    }
    for (auto it : ty->typeArgs) {
        if (ContainsInternalType(it)) {
            return true;
        }
    }
    return false;
}

VisitAction CountNodeNumber(const Expr& expr, bool& result, size_t& nodeNum)
{
    if (nodeNum >= MAX_NODE_NUMBER) {
        result = false;
        return VisitAction::STOP_NOW;
    }
    if (expr.desugarExpr != nullptr) {
        return VisitAction::WALK_CHILDREN;
    }
    if (ContainsInternalType(expr.ty)) {
        result = false;
        return VisitAction::STOP_NOW;
    }
    switch (expr.astKind) {
        case ASTKind::BLOCK:
            return VisitAction::WALK_CHILDREN;
        case ASTKind::LAMBDA_EXPR: {
            result = false;
            return VisitAction::STOP_NOW;
        }
        case ASTKind::REF_EXPR:
        case ASTKind::MEMBER_ACCESS: {
            if (!CanCountedRefExprOrMemberExpr(expr)) {
                result = false;
                return VisitAction::STOP_NOW;
            }
            nodeNum++;
            return VisitAction::WALK_CHILDREN;
        }
        default:
            nodeNum++;
            return VisitAction::WALK_CHILDREN;
    }
}

bool IsInlineFunction(const FuncDecl& fd)
{
    if (!CanExportForInline(fd)) {
        return false;
    }
    // Enum constructor not have function body, do not export.
    // The constructor source code does not contain the default value.
    if (fd.TestAnyAttr(Attribute::ENUM_CONSTRUCTOR, Attribute::CONSTRUCTOR)) {
        return false;
    }
    if (auto pd = fd.propDecl; pd && !pd->HasAnno(AnnotationKind::FROZEN) && !pd->isConst) {
        return false;
    } else if (auto of = fd.ownerFunc; of && !of->HasAnno(AnnotationKind::FROZEN) && !of->isConst) {
        return false;
    } else if (!fd.isFrozen && !fd.isConst) {
        return false;
    }
    if (fd.ownerFunc && fd.ownerFunc->TestAttr(Attribute::CONSTRUCTOR)) {
        return false;
    }
    if (fd.TestAnyAttr(Attribute::C, Attribute::FOREIGN)) {
        return false;
    }
    if (fd.TestAttr(Attribute::OPEN)) {
        return false;
    }
    if (fd.TestAttr(Attribute::INTRINSIC)) {
        return false;
    }
    if (fd.TestAttr(Attribute::ABSTRACT) && (fd.isGetter || fd.isSetter)) {
        return false;
    }
    if (fd.TestAttr(AST::Attribute::MAIN_ENTRY) || fd.identifier == MAIN_INVOKE || fd.identifier == TEST_ENTRY_NAME) {
        return false;
    }
    if (fd.outerDecl != nullptr && fd.outerDecl->identifier == CPOINTER_NAME) {
        return false;
    }

    bool result = true;
    size_t nodeNum = 0;
    auto walkFunc = [&result, &nodeNum](Ptr<Node> node) -> VisitAction {
        if (Is<FuncArg>(node) && node->TestAttr(Attribute::HAS_INITIAL)) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (auto ex = DynamicCast<Expr>(node); ex) {
            return CountNodeNumber(*ex, result, nodeNum);
        } else if (node->astKind == ASTKind::FUNC_DECL && !node->TestAttr(Attribute::HAS_INITIAL)) {
            // NOTE: parameter default function is not treated as nested function.
            result = false;
            return VisitAction::STOP_NOW;
        } else {
            return VisitAction::WALK_CHILDREN;
        }
    };
    Walker walker(fd.funcBody.get(), walkFunc);
    walker.Walk();
    return result;
}

void CheckDefaultParameterFunctionIsInline(const FuncDecl& fd)
{
    auto& params = fd.funcBody->paramLists[0]->params;
    for (auto& param : params) {
        if (param->desugarDecl != nullptr) {
            param->desugarDecl->isInline = param->desugarDecl->ownerFunc->isInline;
        }
    }
}

void ProcessFuncDeclWithInline(FuncDecl& fd)
{
    if (fd.TestAttr(Attribute::GENERIC) || fd.TestAttr(Attribute::MACRO_FUNC)) {
        return;
    }
    auto fdIsInline = IsInlineFunction(fd);
    fd.isInline = fdIsInline;
    if (CanExportForInline(fd) && !fd.TestAttr(Attribute::GENERIC) && !fd.TestAttr(Attribute::COMPILER_ADD)) {
        CheckDefaultParameterFunctionIsInline(fd);
    }
}

void CheckFuncDeclIsInline(Package& pkg)
{
    // Using ordered set to keep functions in definition order for bep.
    auto walkFunc = [](Ptr<Node> node) -> VisitAction {
        switch (node->astKind) {
            case ASTKind::FUNC_DECL: {
                auto fd = RawStaticCast<FuncDecl*>(node);
                ProcessFuncDeclWithInline(*fd);
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::VAR_DECL:
            case ASTKind::INTERFACE_DECL:
                return VisitAction::SKIP_CHILDREN;
            default:
                return VisitAction::WALK_CHILDREN;
        }
    };
    Walker walker(&pkg, walkFunc);
    walker.Walk();
}
} // namespace

void TypeChecker::TypeCheckerImpl::CheckInlineFunctions(const std::vector<Ptr<Package>>& pkgs) const
{
    std::vector<Ptr<FuncDecl>> allImportInlineFunctions;
    for (auto& pkg : pkgs) {
        for (auto it = pkg->srcImportedNonGenericDecls.begin(); it != pkg->srcImportedNonGenericDecls.end();) {
            if ((*it)->astKind != ASTKind::FUNC_DECL) {
                ++it;
                continue;
            }

            auto func = StaticCast<FuncDecl*>(*it);
            allImportInlineFunctions.emplace_back(func);
            it = pkg->srcImportedNonGenericDecls.erase(it);
        }
    }
    auto opts = ci->invocation.globalOptions;
    if (!opts.chirLLVM || opts.enableCompileTest || opts.enableHotReload || opts.mock == MockMode::ON) {
        return; // If current compilation is not supporting inlining, quit here.
    }
    // 2. Collect all inline functions defined in source package and all called imported inline functions.
    for (auto pkg : pkgs) {
        if (pkg->TestAttr(Attribute::IMPORTED) || pkg->isMacroPackage) {
            continue;
        }
        // Add flags to functions that match being inlined
        CheckFuncDeclIsInline(*pkg);
    }
    // 3. Copy inline functions back to the 'srcImportedNonGenericDecls' which is used for genericInstantiation.
    for (auto pkg : pkgs) {
        // Sort final 'inlineFuncDecls' for bep.
        std::sort(pkg->inlineFuncDecls.begin(), pkg->inlineFuncDecls.end(), CompNodeByPos);
        // Only needs to copy collected inline functions for source package.
        if (!pkg->TestAttr(Attribute::IMPORTED)) {
            pkg->inlineFuncDecls.insert(
                pkg->inlineFuncDecls.end(), allImportInlineFunctions.begin(), allImportInlineFunctions.end());
            std::copy_if(pkg->inlineFuncDecls.begin(), pkg->inlineFuncDecls.end(),
                std::back_inserter(pkg->srcImportedNonGenericDecls),
                [](auto it) { return it->TestAttr(Attribute::IMPORTED); });
        }
    }
}
