// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/*
 * @file
 *
 * This file implements the API ResolveMacroCall.
 */
#include "cangjie/Macro/MacroCall.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "cangjie/Macro/TokenSerialization.h"
#include "cangjie/Basic/DiagnosticEmitter.h"

namespace {
using namespace Cangjie;
using namespace AST;

/* Collect parentName for assertParentContext */
void CollectParentMacroName(MacroCall& mc)
{
    auto pmc = mc.parentMacroCall;
    if (!pmc) {
        return;
    }
    if (!pmc->parentNames.empty()) {
        mc.parentNames.insert(mc.parentNames.end(), pmc->parentNames.begin(), pmc->parentNames.end());
    }
    mc.parentNames.emplace_back(pmc->GetFullName());
    return;
}
bool InvokeMethod(MacroCall& macCall, CompilerInstance* ci)
{
    if (ci->invocation.globalOptions.enableMacroInLSP) {
        // Collect parent name for MacroContext in child process.
        CollectParentMacroName(macCall);
    }
    return macCall.FindMacroDefMethod(ci);
}

void FindMacroDefPkg(MacroCall& macCall, CompilerInstance* ci)
{
    auto macroDefFunc = macCall.GetDefinition();
    auto importedMacroPackages = ci->importManager.GetImportedStdMacroPackages();
    auto foundStdMacroPkg = std::find_if(importedMacroPackages.begin(), importedMacroPackages.end(),
        [&macroDefFunc](const std::string packageName) {
            return packageName == macroDefFunc->fullPackageName;
        });
    if (foundStdMacroPkg != importedMacroPackages.end()) {
        auto basePath = FileUtil::JoinPath(
            FileUtil::GetDirPath(ci->invocation.globalOptions.executablePath), "../runtime/lib");
        auto libName =
            "lib" + FileUtil::ConvertPackageNameToLibCangjieBaseFormat(*foundStdMacroPkg) + LIB_SUFFIX;
        macCall.libPath = FileUtil::JoinPath(
            FileUtil::JoinPath(basePath, ci->invocation.globalOptions.GetCangjieLibHostPathName()), libName);
    } else {
        auto libName = "lib-macro_" + FileUtil::ToCjoFileName(macroDefFunc->fullPackageName) + LIB_SUFFIX;
        auto names = Utils::SplitQualifiedName(macroDefFunc->fullPackageName);
        auto fileName = FileUtil::ToCjoFileName(names.front()) + DIR_SEPARATOR + libName;
        macCall.libPath = FileUtil::FindFileByName(fileName, ci->importManager.GetSearchPath()).value_or("");
        if (macCall.libPath.empty()) {
            // Temporarily, find file indirectly when the package is root package.
            macCall.libPath = FileUtil::FindFileByName(libName, ci->importManager.GetSearchPath()).value_or("");
        }
    }
}
}

namespace Cangjie {
bool MacroCall::GetAllDeclsForMacroName(const std::string &macroName, std::vector<Ptr<Decl>>& decls)
{
    auto file = node->curFile;
    CJC_NULLPTR_CHECK(file);
    if (macroName.find(".") != std::string::npos) {
        // We may have alias for package name: import p0.* as p1.* , p1 is the alias for p0.
        std::string pkgNameMaybeAlias = macroName.substr(0, macroName.rfind("."));
        auto [packageDecl, isConflicted] =
            ci->importManager.GetImportedPackageDecl(node, pkgNameMaybeAlias);
        if (packageDecl == nullptr) {
            (void)ci->diag.Diagnose(begin, DiagKind::macro_undefined_pkg_name, pkgNameMaybeAlias);
            return false;
        } else if (isConflicted) {
            (void)ci->diag.Diagnose(begin, DiagKind::sema_package_name_conflict, pkgNameMaybeAlias);
            return false;
        }
        auto foundDecls = ci->importManager.GetPackageMembersByName(*packageDecl->srcPackage, invocation->identifier);
        decls.insert(decls.end(), foundDecls.begin(), foundDecls.end());
    } else {
        decls = ci->importManager.GetImportedDeclsByName(*file, invocation->identifier);
    }
    return true;
}

bool MacroCall::GetValidFuncDecl(std::vector<Ptr<Decl>>& decls)
{
    std::vector<Ptr<FuncDecl>> fds;
    for (auto& dl : decls) {
        if (!(dl->TestAttr(Attribute::MACRO_FUNC)) || dl->astKind != ASTKind::FUNC_DECL) {
            if (invocation->hasParenthesis) {
                (void)ci->diag.Diagnose(begin, DiagKind::macro_expect_macro_definition, invocation->identifier);
                return false;
            }
        }
        // If macro decl is in non-macro package, we should report error in this function, otherwise it will have
        // some asserts in interpreter.
        if (!(dl->curFile && dl->curFile->curPackage->isMacroPackage)) {
            continue;
        }
        fds.push_back(RawStaticCast<FuncDecl*>(dl));
    }
    if (fds.empty()) {
        if (invocation->hasParenthesis && !invocation->IsIfAvailable()) {
            auto pos = invocation->macroNamePos;
            auto content = ci->diag.GetSourceManager().GetContentBetween(pos, pos + invocation->identifier.length());
            if (content == invocation->identifier) {
                (void)ci->diag.Diagnose(begin, DiagKind::macro_undeclared_identifier, invocation->identifier);
            }
        }
        return false;
    }

    if (fds.size() == 1 && fds[0]->funcBody->paramLists.size() > 0) {
        auto argsSize = fds[0]->funcBody->paramLists[0]->params.size(); // FuncParam
        if (HasAttribute()) {
            // Make sure fd has two input arguments: macro M(attr: Tokens, input: Tokens)
            if (argsSize != MACRO_ATTR_ARGS) {
                (void)ci->diag.Diagnose(begin, DiagKind::macro_expect_attributed_macro, invocation->identifier);
                return false;
            }
        } else {
            if (argsSize != MACRO_COMMON_ARGS) {
                (void)ci->diag.Diagnose(begin, DiagKind::macro_expect_plain_macro, invocation->identifier);
                return false;
            }
        }
        this->BindDefinition(fds[0]);
        return true;
    }
    if (fds.size() == MACRO_DEF_NUM &&
        fds[0]->funcBody->paramLists.size() > 0 && fds[1]->funcBody->paramLists.size() > 0) {
        auto argsSize1 = fds[0]->funcBody->paramLists[0]->params.size();
        auto argsSize2 = fds[1]->funcBody->paramLists[0]->params.size();
        if (argsSize1 == argsSize2) { // args should have the size of {1, 2}
            (void)ci->diag.Diagnose(begin, DiagKind::macro_ambiguous_match, invocation->identifier);
            return false;
        }
        if (HasAttribute()) {
            this->BindDefinition(argsSize1 == MACRO_ATTR_ARGS ? fds[0] : fds[1]);
        } else {
            this->BindDefinition(argsSize1 == MACRO_COMMON_ARGS ? fds[0] : fds[1]);
        }
        return true;
    }
    return true;
}

bool MacroCall::BindDefinition(const std::string &macroName)
{
    if (definition) {
        return true;
    }

    std::vector<Ptr<Decl>> decls;

    // get all decls match macroName
    bool ret = GetAllDeclsForMacroName(macroName, decls);
    if (!ret) {
        return ret;
    }
    // filter to get all valid Macro FuncDecl
    ret = GetValidFuncDecl(decls);
    if (!ret) {
        return ret;
    }
    if (definition) {
        return true;
    }
    (void)ci->diag.Diagnose(begin, DiagKind::macro_ambiguous_match, invocation->identifier);
    return false;
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
bool MacroCall::FindMacroDefMethod(CompilerInstance* instance)
{
    if (libPath.empty()) {
        // The following code will be deleted.
        for (auto& handle : InvokeRuntime::GetOpenedLibHandles()) {
            // Check whether this dynamic lib contains macro definition or not.
            invokeFunc = InvokeRuntime::GetMethod(handle, methodName.c_str());
            if (invokeFunc) {
                return true;
            }
        }
        return false;
    }
    // Without macrolib option.
    auto handle = InvokeRuntime::OpenSymbolTable(libPath);
    if (handle == nullptr) {
        status = MacroEvalStatus::FAIL;
        (void)instance->diag.Diagnose(DiagKind::can_not_open_macro_library, libPath);
        return false;
    }
    invokeFunc = InvokeRuntime::GetMethod(handle, methodName.c_str());
    InvokeRuntime::SetOpenedLibHandles(handle);
    return invokeFunc != nullptr;
}
#endif

bool MacroCall::BindInvokeFunc()
{
    CJC_ASSERT(definition);
    this->methodName = Utils::GetMacroFuncName(definition->fullPackageName,
        invocation->HasAttr(), definition->identifier);
    this->packageName = definition->fullPackageName;

    if (ci->invocation.globalOptions.macroLib.empty()) {
        (void)FindMacroDefPkg(*this, ci);
        return InvokeMethod(*this, ci);
    }

    return InvokeMethod(*this, ci);
}

bool MacroCall::ResolveMacroCall(CompilerInstance* instance)
{
    CJC_NULLPTR_CHECK(instance);
    this->ci = instance;

    std::string macCallFullName = invocation->fullName;
    if (Utils::In(macCallFullName, BUILD_IN_MACROS)) {
        return true;
    }
    // CustomAnnotation is valid on typealias decl, but MacroExpansion is not
    if (auto expr = DynamicCast<MacroExpandDecl>(node)) {
        if (Is<TypeAliasDecl>(expr->invocation.decl)) {
            return false;
        }
    }
    // @IfAvailable is not a macro, don't resolve
    if (auto expr = DynamicCast<MacroExpandExpr>(node)) {
        if (expr->invocation.IsIfAvailable()) {
            return false;
        }
    }
    bool findMacroDef = this->BindDefinition(macCallFullName);
    if (!findMacroDef) {
        return findMacroDef;
    }

    bool bindInvokeFunc = this->BindInvokeFunc();
    if (!bindInvokeFunc) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        (void)instance->diag.Diagnose(GetBeginPos(), DiagKind::macro_cannot_find_method, GetFullName());
        status = MacroEvalStatus::FAIL;
#endif
        return false;
    }
    return true;
}
}