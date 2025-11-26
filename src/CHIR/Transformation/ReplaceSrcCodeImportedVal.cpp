// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/ReplaceSrcCodeImportedVal.h"

#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/CHIR.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Visitor/Visitor.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ProfileRecorder.h"

using namespace Cangjie::CHIR;
namespace {
void ReplaceCustomTypeDefVtable(CustomTypeDef& def, const std::unordered_map<Value*, Value*>& symbol)
{
    auto& vtable = def.GetModifiableDefVTable().GetModifiableTypeVTables();
    for (auto& vtableIt : vtable) {
        for (auto& it : vtableIt.GetModifiableVirtualMethods()) {
            auto res = symbol.find(it.GetVirtualMethod());
            if (res != symbol.end()) {
                it.SetVirtualMethod(Cangjie::VirtualCast<FuncBase*>(res->second));
            }
        }
    }
}

void ReplaceCustomTypeDefAndExtendVtable(CustomTypeDef& def, const std::unordered_map<Value*, Value*>& symbol)
{
    ReplaceCustomTypeDefVtable(def, symbol);
    for (auto exDef : def.GetExtends()) {
        ReplaceCustomTypeDefVtable(*exDef, symbol);
    }
}

void ReplaceParentAndSubClassVtable(CustomTypeDef& def, const std::unordered_map<Value*, Value*>& symbol,
    const std::unordered_map<ClassDef*, std::unordered_set<CustomTypeDef*>>& subClasses)
{
    // replace self vtable
    ReplaceCustomTypeDefAndExtendVtable(def, symbol);

    if (!def.IsClassLike()) {
        return;
    }
    auto& classDef = Cangjie::StaticCast<ClassDef&>(def);
    auto it = subClasses.find(&classDef);
    if (it == subClasses.end()) {
        return;
    }
    // replace sub class vtable
    for (auto subClass : it->second) {
        ReplaceCustomTypeDefAndExtendVtable(*subClass, symbol);
    }
}

void ReplaceMethodAndStaticVar(
    const std::unordered_map<CustomTypeDef*, std::unordered_map<Value*, Value*>>& replaceTable,
    const std::unordered_map<ClassDef*, std::unordered_set<CustomTypeDef*>>& subClasses)
{
    for (auto& it : replaceTable) {
        auto methods = it.first->GetMethods();
        for (size_t i = 0; i < methods.size(); ++i) {
            auto res = it.second.find(methods[i]);
            if (res != it.second.end()) {
                methods[i] = Cangjie::VirtualCast<FuncBase*>(res->second);
            }
        }
        it.first->SetMethods(methods);
        ReplaceParentAndSubClassVtable(*it.first, it.second, subClasses);

        auto staticVars = it.first->GetStaticMemberVars();
        for (size_t i = 0; i < staticVars.size(); ++i) {
            auto res = it.second.find(staticVars[i]);
            if (res != it.second.end()) {
                staticVars[i] = Cangjie::VirtualCast<GlobalVarBase*>(res->second);
            }
        }
        it.first->SetStaticMemberVars(staticVars);

        for (auto& it2 : it.second) {
            if (auto func = Cangjie::DynamicCast<Func*>(it2.first)) {
                func->DestroySelf();
            } else {
                Cangjie::VirtualCast<GlobalVarBase*>(it2.first)->DestroySelf();
            }
        }
    }
}

bool IsEmptyInitFunc(Func& func)
{
    if (func.GetFuncKind() != Cangjie::CHIR::FuncKind::GLOBALVAR_INIT) {
        return false;
    }
    bool isEmpty = true;
    auto preVisit = [&isEmpty](Expression& e) {
        if (e.GetExprKind() != Cangjie::CHIR::ExprKind::EXIT && e.GetExprKind() != Cangjie::CHIR::ExprKind::RAISE_EXCEPTION) {
            isEmpty = false;
        }
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(func, preVisit);
    return isEmpty;
}

std::unordered_map<ClassDef*, std::unordered_set<CustomTypeDef*>> CollectSubClasses(
    const Package& pkg, CHIRBuilder& builder)
{
    //                 parent     sub
    std::unordered_map<ClassDef*, std::unordered_set<CustomTypeDef*>> subClasses;
    for (auto def : pkg.GetAllCustomTypeDef()) {
        for (auto parentType : def->GetSuperTypesRecusively(builder)) {
            subClasses[parentType->GetClassDef()].emplace(def);
        }
    }
    return subClasses;
}

void RemoveFromPackage(Package& package,
    const std::unordered_set<Func*>& toBeRemovedFuncs, const std::unordered_set<GlobalVar*>& toBeRemovedVars)
{
    std::vector<Func*> globalFuncs;
    for (auto func : package.GetGlobalFuncs()) {
        if (toBeRemovedFuncs.find(func) != toBeRemovedFuncs.end()) {
            continue;
        } else if (IsEmptyInitFunc(*func)) {
            for (auto user : func->GetUsers()) {
                user->RemoveSelfFromBlock();
            }
            func->DestroySelf();
            continue;
        }
        globalFuncs.emplace_back(func);
    }
    package.SetGlobalFuncs(globalFuncs);

    std::vector<GlobalVar*> globalVars;
    for (auto var : package.GetGlobalVars()) {
        if (toBeRemovedVars.find(var) == toBeRemovedVars.end()) {
            globalVars.emplace_back(var);
        }
    }
    package.SetGlobalVars(std::move(globalVars));
}
}

ReplaceSrcCodeImportedVal::ReplaceSrcCodeImportedVal(
    Package& package, std::unordered_map<std::string, FuncBase*>& implicitFuncs, CHIRBuilder& builder)
    : package(package), implicitFuncs(implicitFuncs), builder(builder)
{
}

void ReplaceSrcCodeImportedVal::CreateSrcImportedFuncSymbol(Func& fn)
{
    auto genericParamTy = fn.GetGenericTypeParams();
    auto pkgName = fn.GetPackageName();
    auto funcTy = fn.GetType();
    auto mangledName = fn.GetIdentifierWithoutPrefix();
    auto srcCodeName = fn.GetSrcCodeIdentifier();
    auto rawMangledName = fn.GetRawMangledName();
    auto importedFunc = builder.CreateImportedVarOrFunc<ImportedFunc>(
        funcTy, mangledName, srcCodeName, rawMangledName, pkgName, genericParamTy);
    importedFunc->AppendAttributeInfo(fn.GetAttributeInfo());
    importedFunc->SetFuncKind(fn.GetFuncKind());
    if (auto hostFunc = fn.GetParamDftValHostFunc()) {
        importedFunc->SetParamDftValHostFunc(*hostFunc);
    }
    importedFunc->SetFastNative(fn.IsFastNative());
    importedFunc->Set<LinkTypeInfo>(Linkage::EXTERNAL);
    srcCodeImportedFuncMap.emplace(&fn, importedFunc);
}

void ReplaceSrcCodeImportedVal::CreateSrcImportedVarSymbol(GlobalVar& gv)
{
    auto mangledName = gv.GetIdentifierWithoutPrefix();
    auto srcCodeName = gv.GetSrcCodeIdentifier();
    auto rawMangledName = gv.GetRawMangledName();
    auto packageName = gv.GetPackageName();
    auto ty = gv.GetType();
    auto importedVar =
        builder.CreateImportedVarOrFunc<ImportedVar>(ty, mangledName, srcCodeName, rawMangledName, packageName);
    importedVar->AppendAttributeInfo(gv.GetAttributeInfo());
    importedVar->Set<LinkTypeInfo>(gv.Get<LinkTypeInfo>());
    srcCodeImportedVarMap.emplace(&gv, importedVar);
}

void ReplaceSrcCodeImportedVal::CreateSrcImpotedValueSymbol(const std::unordered_set<Func*>& srcCodeImportedFuncs,
    const std::unordered_set<GlobalVar*>& srcCodeImportedVars)
{
    for (auto func : srcCodeImportedFuncs) {
        CreateSrcImportedFuncSymbol(*func);
    }
    for (auto func : builder.GetCurPackage()->GetGlobalFuncs()) {
        if (auto genericDecl = func->GetGenericDecl(); genericDecl && genericDecl->IsFuncWithBody()) {
            auto importedFunc = srcCodeImportedFuncMap.find(StaticCast<Func*>(genericDecl));
            if (importedFunc != srcCodeImportedFuncMap.end()) {
                func->SetGenericDecl(*importedFunc->second);
            }
        }
    }
    for (auto& it : srcCodeImportedFuncMap) {
        if (auto hostFunc = it.second->GetParamDftValHostFunc()) {
            auto importedFunc = srcCodeImportedFuncMap.find(StaticCast<Func*>(hostFunc));
            CJC_ASSERT(importedFunc != srcCodeImportedFuncMap.end());
            it.second->SetParamDftValHostFunc(*importedFunc->second);
        }
    }
    for (auto gv : builder.GetCurPackage()->GetGlobalVars()) {
        CJC_NULLPTR_CHECK(gv);
        if (srcCodeImportedVars.find(gv) != srcCodeImportedVars.end()) {
            CreateSrcImportedVarSymbol(*gv);
        }
    }
}

std::unordered_set<Func*> ReplaceSrcCodeImportedVal::RemoveUselessDefFromCC(
    const std::unordered_set<ClassDef*>& uselessClasses, const std::unordered_set<Func*>& uselessLambda)
{
    std::unordered_set<Func*> toBeRemovedFuncs;
    for (auto lambda : uselessLambda) {
        for (auto user : lambda->GetUsers()) {
            user->RemoveSelfFromBlock();
        }
        lambda->DestroySelf();
        toBeRemovedFuncs.emplace(lambda);
    }

    for (auto def : uselessClasses) {
        for (auto func : def->GetMethods()) {
            for (auto user : func->GetUsers()) {
                user->RemoveSelfFromBlock();
            }
            auto funcWithBody = StaticCast<Func*>(func);
            funcWithBody->DestroySelf();
            toBeRemovedFuncs.emplace(funcWithBody);
        }
    }
    std::vector<ClassDef*> newClasses;
    auto classes = package.GetClasses();
    for (auto def : classes) {
        if (uselessClasses.find(def) == uselessClasses.end()) {
            newClasses.emplace_back(def);
        }
    }
    package.SetClasses(std::move(newClasses));
    return toBeRemovedFuncs;
}

void ReplaceSrcCodeImportedVal::ReplaceSrcCodeImportedFuncUsers(std::unordered_set<Func*>& toBeRemovedFuncs,
    std::unordered_map<CustomTypeDef*, std::unordered_map<Value*, Value*>>& replaceTable)
{
    for (auto& it : srcCodeImportedFuncMap) {
        auto funcWithBody = it.first;
        auto importedSymbol = it.second;
        // Attributes may be added in the chir phase. For example, 'final' is added when a virtual table is created. In
        // this case, you need to append the attributes again.
        importedSymbol->AppendAttributeInfo(funcWithBody->GetAttributeInfo());
        for (auto user : funcWithBody->GetUsers()) {
            user->ReplaceOperand(funcWithBody, importedSymbol);
        }
        if (auto parentDef = funcWithBody->GetParentCustomTypeDef()) {
            replaceTable[parentDef][funcWithBody] = importedSymbol;
        }
        toBeRemovedFuncs.emplace(funcWithBody);
        auto implicitIt = implicitFuncs.find(funcWithBody->GetIdentifierWithoutPrefix());
        if (implicitIt != implicitFuncs.end()) {
            implicitIt->second = importedSymbol;
        }
    }
}

void ReplaceSrcCodeImportedVal::ReplaceSrcCodeImportedVarUsers(
    std::unordered_set<Func*>& toBeRemovedFuncs, std::unordered_set<GlobalVar*>& toBeRemovedVars,
    std::unordered_map<CustomTypeDef*, std::unordered_map<Value*, Value*>>& replaceTable)
{
    for (auto& it : srcCodeImportedVarMap) {
        auto varWithInit = it.first;
        auto importedSymbol = it.second;
        // Attributes may be added in the chir phase. For example, 'final' is added when a virtual table is created. In
        // this case, you need to append the attributes again.
        importedSymbol->AppendAttributeInfo(varWithInit->GetAttributeInfo());
        if (auto initFunc = varWithInit->GetInitFunc()) {
            for (auto user : initFunc->GetUsers()) {
                user->RemoveSelfFromBlock();
            }
            initFunc->DestroySelf();
            toBeRemovedFuncs.emplace(initFunc);
        }
        for (auto user : varWithInit->GetUsers()) {
            user->ReplaceOperand(varWithInit, importedSymbol);
        }
        if (auto parentDef = varWithInit->GetParentCustomTypeDef()) {
            replaceTable[parentDef][varWithInit] = importedSymbol;
        }
        toBeRemovedVars.emplace(varWithInit);
    }
}

void ReplaceSrcCodeImportedVal::Run(const std::unordered_set<Func*>& srcCodeImportedFuncs,
    const std::unordered_set<GlobalVar*>& srcCodeImportedVars, const std::unordered_set<ClassDef*>& uselessClasses,
    const std::unordered_set<Func*>& uselessLambda)
{
    // 1. create imported value symbol
    CreateSrcImpotedValueSymbol(srcCodeImportedFuncs, srcCodeImportedVars);

    // 2. remove useless def that created in closure conversion
    auto toBeRemovedFuncs = RemoveUselessDefFromCC(uselessClasses, uselessLambda);

    // 3. replace src code imported func and vars' users with imported symbol
    std::unordered_map<CustomTypeDef*, std::unordered_map<Value*, Value*>> replaceTable;
    ReplaceSrcCodeImportedFuncUsers(toBeRemovedFuncs, replaceTable);
    std::unordered_set<GlobalVar*> toBeRemovedVars;
    ReplaceSrcCodeImportedVarUsers(toBeRemovedFuncs, toBeRemovedVars, replaceTable);
    
    // 4. replace method and static vars' pointer in custom type def
    auto subClasses = CollectSubClasses(package, builder);
    ReplaceMethodAndStaticVar(replaceTable, subClasses);

    // 5. remove src code imported func and vars from package
    RemoveFromPackage(package, toBeRemovedFuncs, toBeRemovedVars);
}
