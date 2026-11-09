// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the helper functions used for incremental semantic checking.
 */

#include "cangjie/Sema/IncrementalUtils.h"

#include "cangjie/AST/Utils.h"
#include "cangjie/AST/ASTCasting.h"
#include "cangjie/Mangle/ASTMangler.h"
#include "cangjie/Mangle/BaseMangler.h"

#include "Desugar/DesugarInTypeCheck.h"
#include "cangjie/IncrementalCompilation/IncrementalCompilationLogger.h"

namespace Cangjie::Sema {
using namespace Cangjie::AST;
namespace {
inline bool IsInstanceMemberVar(const Decl& decl)
{
    return decl.outerDecl && decl.astKind == ASTKind::VAR_DECL &&
            !decl.TestAnyAttr(Attribute::STATIC, Attribute::ENUM_CONSTRUCTOR);
}

inline bool IsStaticMemberVar(const Decl& decl)
{
    return decl.outerDecl && decl.astKind == ASTKind::VAR_DECL && decl.TestAttr(Attribute::STATIC);
}
} // namespace

void MarkIncrementalCheckForCtor(const std::unordered_set<Ptr<Decl>>& declsToBeReCompiled)
{
    std::unordered_set<Ptr<Decl>> typeOfModifiedCtors;
    std::unordered_set<Ptr<Decl>> typeOfModifiedStaticInits;
    auto collectTypes = [&typeOfModifiedCtors, &typeOfModifiedStaticInits](auto decl) {
        // 1. If a constructor needs to be compiled, all instance member variables also need to be compiled.
        // 2. If any of the instance member variable needs to be compiled,
        //    all other instance member variables also need to be compiled.
        if (IsInstanceConstructor(*decl) || IsInstanceMemberVar(*decl)) {
            CJC_NULLPTR_CHECK(decl->outerDecl);
            typeOfModifiedCtors.emplace(decl->outerDecl);
        } else if (IsStaticInitializer(*decl) || IsStaticMemberVar(*decl)) {
            // If any static initializer or static variables has been changed,
            // the static initializer and all static variables are need to be compiled.
            CJC_NULLPTR_CHECK(decl->outerDecl);
            typeOfModifiedStaticInits.emplace(decl->outerDecl);
        } else if (decl->astKind == ASTKind::CLASS_DECL) {
            // If classDecl is to be compiled, mark it's instance constructor also needs to be recompiled,
            // since its inherited super may changed (add/remove/modify super call).
            typeOfModifiedCtors.emplace(decl);
        }
    };
    std::for_each(declsToBeReCompiled.cbegin(), declsToBeReCompiled.cend(), collectTypes);
    for (auto& it : typeOfModifiedCtors) {
        for (auto& member : it->GetMemberDecls()) {
            if (IsInstanceConstructor(*member) || IsInstanceMemberVar(*member)) {
                member->toBeCompiled = true;
            }
        }
    }
    for (auto& it : typeOfModifiedStaticInits) {
        for (auto& member : it->GetMemberDecls()) {
            if (IsStaticInitializer(*member) || IsStaticMemberVar(*member)) {
                member->toBeCompiled = true;
            }
        }
    }
}

/**
 * Collect changed struct types which need to be regenerated in CodeGen.
 */
std::unordered_set<Ptr<const StructTy>> CollectChangedStructTypes(
    const Package& pkg, const std::unordered_set<Ptr<Decl>>& declsToBeReCompiled)
{
    std::unordered_set<Ptr<const StructTy>> tys;
    for (auto decl : declsToBeReCompiled) {
        if (decl->astKind != ASTKind::STRUCT_DECL || decl->TestAttr(Attribute::GENERIC)) {
            continue;
        }
        if (auto structTy = DynamicCast<StructTy>(decl->DataTy())) {
            (void)tys.emplace(structTy);
        }
    }
    for (auto& it : pkg.genericInstantiatedDecls) {
        if (it->toBeCompiled && it->astKind == ASTKind::STRUCT_DECL) {
            (void)tys.emplace(StaticCast<StructTy*>(it->DataTy()));
        }
    }
    return tys;
}

bool IsNeedRecompileCtor(Decl& decl)
{
    if (!decl.TestAttr(AST::Attribute::CONSTRUCTOR)) {
        return false;
    }
    auto initFunc = DynamicCast<FuncDecl*>(&decl);
    if (!initFunc) {
        return false;
    }
    // We can't reset the right body for unchanged init decl, so recompile it when it need body
    if (initFunc->isConst || initFunc->isFrozen) {
        return true;
    }
    return false;
}

/**
 * Desugar for unchanged primary ctor and update member variables into 'mangledName2DeclMap'.
 * NOTE: this only for incremental compilation.
 */
void HandleCtorForIncr(
    const AST::Package& pkg, std::map<std::string, Ptr<Decl>>& mangledName2DeclMap, SemanticInfo& usageCache)
{
    IterateToplevelDecls(pkg, [&mangledName2DeclMap, &usageCache](auto& decl) {
        if (decl->astKind != ASTKind::CLASS_DECL && decl->astKind != ASTKind::STRUCT_DECL) {
            return;
        }
        auto& members = decl->GetMemberDecls();
        Ptr<PrimaryCtorDecl> primaryCtor = nullptr;
        size_t index = members.size();
        for (auto& it : members) {
            if (auto pd = DynamicCast<PrimaryCtorDecl*>(it.get()); pd && !pd->toBeCompiled) {
                if (pd->isConst || pd->HasAnno(AnnotationKind::FROZEN)) {
                    // We can't desugar and reset the right body for unchanged primary constructor, so recompile it when
                    // it need body
                    pd->toBeCompiled = true;
                } else {
                    primaryCtor = pd;
                }
            } else if (!it->toBeCompiled && IsNeedRecompileCtor(*it)) {
                it->toBeCompiled = true;
            }
        }
        if (primaryCtor) {
            DesugarPrimaryCtor(*decl, *primaryCtor);
            // Desugar of primary ctor may insert new member variables into type decl.
            size_t i = index;
            CJC_ASSERT(i < members.size());
            for (; i < members.size(); ++i) {
                auto originDecl = mangledName2DeclMap[members[i]->rawMangleName];
                mangledName2DeclMap[members[i]->rawMangleName] = members[i].get();
                // Since decl has been desugared, we need to update sema cache to new decl.
                auto found = usageCache.usages.find(originDecl);
                if (found != usageCache.usages.end()) {
                    usageCache.usages.emplace(members[i].get(), found->second);
                    usageCache.usages.erase(originDecl);
                }
            }
        }
    });
}

std::string GetTypeRawMangleName(ModalTy ty)
{
    std::string name;
    auto boxDecl = Ty::GetDeclPtrOfTy(ty);
    if (boxDecl) {
        name = boxDecl->rawMangleName;
    } else {
        // NOTE: extend of function & tuple type is not supported now.
        CJC_ASSERT(ty->IsBuiltin());
        name = ASTMangler::MangleBuiltinType(Ty::KindName(ty.Kind()));
    }
    return name + MangleUtils::MangleTypeMode(ty.Mode());
}

std::string GetRawMangleOfBoxedType(const InheritableDecl& cd)
{
    CJC_ASSERT(cd.TestAttr(Attribute::OPEN) && cd.identifier.Val().find(BOX_DECL_PREFIX) != std::string::npos);
    for (auto& member : cd.GetMemberDecls()) {
        if (auto vd = DynamicCast<VarDecl>(member.get())) {
            CJC_ASSERT(vd->identifier == "$value");
            return GetTypeRawMangleName(vd->GetTy());
        }
    }
    InternalError("Found incorrect base box class: " + cd.identifier);
    return "";
}

void CollectImplicitAddedMembers(const AST::InheritableDecl& id, SemanticInfo& usageCache)
{
    auto rawMangleName = id.rawMangleName;
    if (auto cd = DynamicCast<ClassDecl>(&id); cd && cd->TestAttr(Attribute::IMPLICIT_ADD)) {
        // collect cd to baseBoxType
        if (cd->TestAttr(Attribute::OPEN)) { // Base box class
            rawMangleName = GetRawMangleOfBoxedType(*cd);
        } else {
            // box class with interfaces.
            auto sd = cd->GetSuperClassDecl();
            CJC_NULLPTR_CHECK(sd);
            rawMangleName = GetRawMangleOfBoxedType(*sd);
        }
        CJC_ASSERT(!rawMangleName.empty());
        usageCache.compilerAddedUsages[rawMangleName].emplace(cd->mangledName);
    } else {
        CJC_ASSERT(!rawMangleName.empty());
    }
    for (auto& member : id.GetMemberDecls()) {
        // collect compiler inserted static.init, normal init, copied default implementation, $toAny,
        // and members in boxed decl.
        if (!member->TestAttr(Attribute::IMPLICIT_ADD)) {
            continue;
        }
        if (auto pd = DynamicCast<PropDecl>(member.get())) {
            auto collectFunc = [&usageCache, &rawMangleName](auto& it) {
                usageCache.compilerAddedUsages[rawMangleName].emplace(it->mangledName);
            };
            std::for_each(pd->getters.cbegin(), pd->getters.cend(), collectFunc);
            std::for_each(pd->setters.cbegin(), pd->setters.cend(), collectFunc);
        } else {
            usageCache.compilerAddedUsages[rawMangleName].emplace(member->mangledName);
        }
    }
}

namespace {
void CollectAllNoninstantiatedDeclsOfTypeArgs(
    const Cangjie::AST::Decl& genericDecl, std::set<Ptr<const Decl>>& nonInsTypeDecls)
{
    if (genericDecl.GetTy() == nullptr) {
        return;
    }
    for (auto& tyArg : genericDecl.GetTy()->typeArgs) {
        auto tyDecl = Ty::GetDeclOfTy(tyArg);
        if (tyDecl == nullptr) {
            continue;
        }
        if (tyDecl->TestAttr(Attribute::GENERIC_INSTANTIATED)) {
            CollectAllNoninstantiatedDeclsOfTypeArgs(*tyDecl, nonInsTypeDecls);
        }
        nonInsTypeDecls.emplace(tyDecl);
    }
}
}

void CollectCompilerAddedDeclUsage(const AST::Package& pkg, SemanticInfo& usageCache)
{
    IterateToplevelDecls(pkg, [&usageCache](auto& decl) {
        if (auto md = DynamicCast<MacroDecl>(decl.get()); md && md->desugarDecl) {
            // original param 2 + compiler inserted 1 param == 3.
            auto isAttr = md->desugarDecl->funcBody->paramLists.front()->params.size() == MACRO_ATTR_ARGS;
            auto invokeFuncName = Utils::GetMacroFuncName(md->fullPackageName, isAttr, md->identifier);
            usageCache.compilerAddedUsages[md->rawMangleName].emplace(invokeFuncName);
        } else if (auto id = DynamicCast<InheritableDecl>(decl.get())) {
            CollectImplicitAddedMembers(*id, usageCache);
        }
    });
    for (auto it : pkg.srcImportedNonGenericDecls) {
        auto rawMangleName = it->rawMangleName;
        if (auto fd = DynamicCast<FuncDecl>(it); fd && fd->ownerFunc) {
            rawMangleName = fd->ownerFunc->rawMangleName;
        }
        if (rawMangleName.empty()) {
            continue;
        }
        usageCache.compilerAddedUsages[rawMangleName].emplace(it->mangledName);
    }
    for (const auto &it : pkg.genericInstantiatedDecls) {
        if (it->mangledName.empty()) {
            continue;
        }
        // collocet all decl (non-Instantiated) of type args  of generic instantiated decl
        std::set<Ptr<const Decl>> nonInsDeclsOfTypeArgs;
        CollectAllNoninstantiatedDeclsOfTypeArgs(*it, nonInsDeclsOfTypeArgs);
        for (auto& decl : nonInsDeclsOfTypeArgs) {
            if (decl->rawMangleName.empty()) {
                continue;
            }
            usageCache.compilerAddedUsages[decl->rawMangleName].emplace(it->mangledName);
        }
    }
}

void CollectRemovedMangles(
    const std::string& removed, SemanticInfo& semaInfo, std::unordered_set<std::string>& removedMangles)
{
    if (auto found = semaInfo.compilerAddedUsages.find(removed); found != semaInfo.compilerAddedUsages.end()) {
        auto& logger = IncrementalCompilationLogger::GetInstance();
        if (logger.IsEnable()) {
            logger.LogLn("remove for: " + removed);
        }
        for (auto it : found->second) {
            if (it.find("<init>") == std::string::npos) {
                removedMangles.insert(it);
                if (logger.IsEnable()) {
                    logger.LogLn("removed mangled: " + it);
                }
            }
        }
        semaInfo.compilerAddedUsages.erase(found);
    }
}

static std::optional<std::string> CollectDefaultCtorIfNeeded(const std::string& typeMangle, SemanticInfo& semaInfo,
    bool needRemoveCompilerAddCtor)
{
    auto found = semaInfo.compilerAddedUsages.find(typeMangle);
    if (found == semaInfo.compilerAddedUsages.end()) {
        return {};
    }
    // Every type decl must have at most one default constructor.
    auto foundCtor = std::find_if(found->second.cbegin(), found->second.cend(), [](auto it) {
        return it.find("<init>") != std::string::npos;
    });
    std::optional<std::string> ret;
    if (foundCtor != found->second.cend()) {
        if (needRemoveCompilerAddCtor) {
            ret = *foundCtor;
        }
        found->second.erase(foundCtor);
    }
    return ret;
}

void CollectRemovedManglesForReCompile(
    const Decl& changed, SemanticInfo& semaInfo, std::unordered_set<std::string>& removedMangles)
{
    if (changed.astKind == ASTKind::CLASS_DECL || changed.astKind == ASTKind::STRUCT_DECL) {
        auto& members = changed.GetMemberDecls();
        bool needRemoveCompilerAddCtor = std::any_of(members.cbegin(), members.cend(), [](auto& it) {
            return IsInstanceConstructor(*it) ||
                (it->toBeCompiled && it->astKind == ASTKind::VAR_DECL && !it->TestAttr(Attribute::STATIC));
        });
        auto& typeMangle = changed.rawMangleName;
        if (auto ctorMangle = CollectDefaultCtorIfNeeded(typeMangle, semaInfo, needRemoveCompilerAddCtor)) {
            IncrementalCompilationLogger::GetInstance().LogLn(
                "remove default ctor '" + *ctorMangle + "' for '" + typeMangle + "'");
            removedMangles.emplace(ctorMangle.value());
        }
    }
    CollectRemovedMangles(changed.rawMangleName, semaInfo, removedMangles);
}
} // namespace Cangjie::Sema
