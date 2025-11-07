// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/CHIR.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Visitor/Visitor.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ProfileRecorder.h"

namespace Cangjie::CHIR {
namespace {
void CreateNewExtendDef(Package& package, CustomTypeDef& curDef, ClassType& parentType,
    std::vector<VirtualMethodInfo>& virtualMethods, CHIRBuilder& builder)
{
    auto mangledName = "extend_" + curDef.GetIdentifier() + "_p_" + parentType.ToString();
    auto genericParams = curDef.GetGenericTypeParams();
    auto extendDef = builder.CreateExtend(
        INVALID_LOCATION, mangledName, package.GetName(), false, genericParams);
    extendDef->SetExtendedType(*curDef.GetType());
    extendDef->AddImplementedInterfaceTy(parentType);
    extendDef->EnableAttr(Attribute::COMPILER_ADD);
    if (curDef.TestAttr(Attribute::GENERIC)) {
        extendDef->EnableAttr(Attribute::GENERIC);
    }

    VTableInDef vtable;
    vtable.AddNewItemToTypeVTable(parentType, std::move(virtualMethods));
    extendDef->SetVTable(std::move(vtable));
}

void CreateExtendDefForImportedCustomTypeDef(Package& package, CHIRBuilder& builder, bool incr)
{
    if (incr) {
        return;
    }
    /*  codegen will create extension def according to CHIR's vtable, in order not to create duplicate
        extension def, codegen won't visit vtable from imported CustomTypeDef, these vtables are assumed that
        must be created in imported package. but there is a special case:
        ================ package A ================
        public interface I {}
        open public class A {}

        ================ package B ================
        import package A
        public class B <: A {} // extension def B_ed_A will be created in codegen

        ================ package C ================
        import package A
        extend A <: I {} // extension def A_ed_I will be created in codegen

        ================ package D ================
        import package A, B, C
        // extension def B_ed_I is needed, but there isn't in imported packages

        so we need to create extension def B_ed_I in current package, in order to deal with this case,
        a compiler added extend def is needed:
        [COMPILER_ADD] extend B <: I {}
        this def is create in current package, so extension def B_ed_I will be created in codegen
    */
    for (auto def : package.GetAllImportedCustomTypeDef()) {
        if (def->IsExtend()) {
            continue;
        }
        for (auto& it : def->GetModifiableDefVTable().GetModifiableTypeVTables()) {
            if (ParentDefIsFromExtend(*def, *(it.GetSrcParentType()->GetClassDef()))) {
                CreateNewExtendDef(package, *def, *it.GetSrcParentType(), it.GetModifiableVirtualMethods(), builder);
                continue;
            }
        }
    }
}

void ReplaceCustomTypeDefVtable(CustomTypeDef& def, const std::unordered_map<Value*, Value*>& symbol)
{
    auto& vtable = def.GetModifiableDefVTable().GetModifiableTypeVTables();
    for (auto& vtableIt : vtable) {
        for (auto& it : vtableIt.GetModifiableVirtualMethods()) {
            auto res = symbol.find(it.GetVirtualMethod());
            if (res != symbol.end()) {
                it.SetVirtualMethod(VirtualCast<FuncBase*>(res->second));
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
    auto& classDef = StaticCast<ClassDef&>(def);
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
                methods[i] = VirtualCast<FuncBase*>(res->second);
            }
        }
        it.first->SetMethods(methods);
        ReplaceParentAndSubClassVtable(*it.first, it.second, subClasses);

        auto staticVars = it.first->GetStaticMemberVars();
        for (size_t i = 0; i < staticVars.size(); ++i) {
            auto res = it.second.find(staticVars[i]);
            if (res != it.second.end()) {
                staticVars[i] = VirtualCast<GlobalVarBase*>(res->second);
            }
        }
        it.first->SetStaticMemberVars(staticVars);

        for (auto& it2 : it.second) {
            if (auto func = DynamicCast<Func*>(it2.first)) {
                func->DestroySelf();
            } else {
                VirtualCast<GlobalVarBase*>(it2.first)->DestroySelf();
            }
        }
    }
}

bool IsEmptyInitFunc(Func& func)
{
    if (func.GetFuncKind() != CHIR::FuncKind::GLOBALVAR_INIT) {
        return false;
    }
    bool isEmpty = true;
    auto preVisit = [&isEmpty](Expression& e) {
        if (e.GetExprKind() != CHIR::ExprKind::EXIT && e.GetExprKind() != CHIR::ExprKind::RAISE_EXCEPTION) {
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

void CreateSrcImportedFuncSymbol(
    CHIRBuilder& builder, Func& fn, std::unordered_map<Func*, ImportedFunc*>& srcCodeImportedFuncMap)
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

void CreateSrcImportedVarSymbol(
    CHIRBuilder& builder, Value& gv, std::unordered_map<GlobalVar*, ImportedVar*>& srcCodeImportedVarMap)
{
    auto globalVar = VirtualCast<GlobalVar*>(&gv);
    auto mangledName = globalVar->GetIdentifierWithoutPrefix();
    auto srcCodeName = globalVar->GetSrcCodeIdentifier();
    auto rawMangledName = globalVar->GetRawMangledName();
    auto packageName = globalVar->GetPackageName();
    auto ty = globalVar->GetType();
    auto importedVar =
        builder.CreateImportedVarOrFunc<ImportedVar>(ty, mangledName, srcCodeName, rawMangledName, packageName);
    importedVar->AppendAttributeInfo(globalVar->GetAttributeInfo());
    importedVar->Set<LinkTypeInfo>(gv.Get<LinkTypeInfo>());
    srcCodeImportedVarMap.emplace(globalVar, importedVar);
}

void CreateSrcImpotedValueSymbol(const std::unordered_set<Func*>& srcCodeImportedFuncs,
    const std::unordered_set<GlobalVar*>& srcCodeImportedVars, CHIRBuilder& builder,
    std::unordered_map<Func*, ImportedFunc*>& srcCodeImportedFuncMap,
    std::unordered_map<GlobalVar*, ImportedVar*>& srcCodeImportedVarMap)
{
    for (auto func : srcCodeImportedFuncs) {
        CreateSrcImportedFuncSymbol(builder, *func, srcCodeImportedFuncMap);
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
            CreateSrcImportedVarSymbol(builder, *gv, srcCodeImportedVarMap);
        }
    }
}
}

void ToCHIR::ReplaceSrcCodeImportedValueWithSymbol()
{
    std::unordered_set<Func*> toBeRemovedFuncs;
    std::unordered_set<GlobalVar*> toBeRemovedVars;
    std::unordered_map<Func*, ImportedFunc*> srcCodeImportedFuncMap;
    std::unordered_map<GlobalVar*, ImportedVar*> srcCodeImportedVarMap;
    CreateSrcImpotedValueSymbol(
        srcCodeImportedFuncs, srcCodeImportedVars, builder, srcCodeImportedFuncMap, srcCodeImportedVarMap);
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
    auto classes = chirPkg->GetClasses();
    for (auto def : classes) {
        if (uselessClasses.find(def) == uselessClasses.end()) {
            newClasses.emplace_back(def);
        }
    }
    chirPkg->SetClasses(std::move(newClasses));

    std::unordered_map<CustomTypeDef*, std::unordered_map<Value*, Value*>> replaceTable;
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
    auto subClasses = CollectSubClasses(*chirPkg, builder);
    ReplaceMethodAndStaticVar(replaceTable, subClasses);

    std::vector<Func*> globalFuncs;
    for (auto func : chirPkg->GetGlobalFuncs()) {
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
    chirPkg->SetGlobalFuncs(globalFuncs);

    std::vector<GlobalVar*> globalVars;
    for (auto var : chirPkg->GetGlobalVars()) {
        if (toBeRemovedVars.find(var) == toBeRemovedVars.end()) {
            globalVars.emplace_back(var);
        }
    }
    chirPkg->SetGlobalVars(std::move(globalVars));
}

void ToCHIR::RemoveUnusedImports()
{
    Utils::ProfileRecorder r{"CHIR", "RemoveUnusedImports"};
    ReplaceSrcCodeImportedValueWithSymbol();
    CreateExtendDefForImportedCustomTypeDef(*GetPackage(), builder, kind == IncreKind::INCR);
    DumpCHIRToFile("RemoveUnusedImports");
}
}  // namespace Cangjie::CHIR
