// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/GenerateVTable/GenerateVTable.h"

#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/GenerateVTable/UpdateOperatorVTable.h"
#include "cangjie/CHIR/GenerateVTable/VTableGenerator.h"
#include "cangjie/CHIR/GenerateVTable/WrapMutFunc.h"
#include "cangjie/CHIR/GenerateVTable/WrapVirtualFunc.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/CHIR/Visitor/Visitor.h"
#include "cangjie/Mangle/CHIRManglingUtils.h"
#include "cangjie/Utils/ProfileRecorder.h"

using namespace Cangjie;
using namespace Cangjie::CHIR;

namespace {
bool CalleeIsMutFuncFromParent(Type* thisType, FuncBase* callee, const Func& topLevelFunc)
{
    // thisType must be Struct
    if (thisType == nullptr || !thisType->StripAllRefs()->IsStruct()) {
        return false;
    }
    // callee must be mut func
    if (callee == nullptr || !callee->TestAttr(Attribute::MUT)) {
        return false;
    }
    // callee's parent must be from interface
    if (!callee->GetParentCustomTypeOrExtendedType()->IsClass()) {
        return false;
    }
    // current Apply is not in wrapper func
    return topLevelFunc.Get<WrappedRawMethod>() == nullptr;
}
} // namespace

GenerateVTable::GenerateVTable(Package& pkg, CHIRBuilder& b, const Cangjie::GlobalOptions& opts)
    : package(pkg), builder(b), opts(opts)
{
}

void GenerateVTable::CreateVTable()
{
    Utils::ProfileRecorder recorder("GenerateVTable", "CreateVTable");
    auto vtableGenerator = VTableGenerator(builder);
    for (auto customDef : package.GetAllCustomTypeDef()) {
        if (customDef->TestAttr(Attribute::SKIP_ANALYSIS)) {
            continue;
        }
        vtableGenerator.GenerateVTable(*customDef);
    }
}

void GenerateVTable::UpdateOperatorVirFunc()
{
    UpdateOperatorVTable(package, builder).Update();
}

void GenerateVTable::CreateVirtualFuncWrapper(const IncreKind& kind, const CompilationCache& increCachedInfo,
    VirtualWrapperDepMap& curVirtFuncWrapDep, VirtualWrapperDepMap& delVirtFuncWrapForIncr)
{
    Utils::ProfileRecorder recorder("GenerateVTable", "CreateVirtualFuncWrapper");
    bool targetIsWin = opts.target.os == Triple::OSType::WINDOWS;
    IncreKind tempKind = opts.enIncrementalCompilation ? kind : IncreKind::INVALID;
    auto wrapper = WrapVirtualFunc(builder, increCachedInfo, tempKind, targetIsWin);
    for (auto customDef : package.GetAllCustomTypeDef()) {
        if (customDef->TestAttr(Attribute::SKIP_ANALYSIS)) {
            continue;
        }
        wrapper.CheckAndWrap(*customDef);
    }
    curVirtFuncWrapDep = wrapper.GetCurVirtFuncWrapDep();
    delVirtFuncWrapForIncr = wrapper.GetDelVirtFuncWrapForIncr();
}

void GenerateVTable::CreateMutFuncWrapper()
{
    Utils::ProfileRecorder recorder("GenerateVTable", "CreateMutFuncWrapper");
    auto wrapper = WrapMutFunc(builder);
    for (auto customDef : package.GetAllCustomTypeDef()) {
        if (customDef->TestAttr(Attribute::SKIP_ANALYSIS)) {
            continue;
        }
        wrapper.Run(*customDef);
    }
    mutFuncWrappers = wrapper.GetWrappers();
}

FuncBase* GenerateVTable::GetMutFuncWrapper(const Type& thisType, const std::vector<Value*>& args,
    const std::vector<Type*>& instTypeArgs, Type& retType, const FuncBase& callee)
{
    std::vector<Type*> paramTypes;
    for (auto arg : args) {
        paramTypes.emplace_back(arg->GetType());
    }
    auto funcCallType = FuncCallType {
        .funcName = callee.GetSrcCodeIdentifier(),
        .funcType = builder.GetType<FuncType>(paramTypes, &retType),
        .genericTypeArgs = instTypeArgs
    };
    auto vtableRes = GetFuncIndexInVTable(
        *thisType.StripAllRefs(), funcCallType, callee.TestAttr(Attribute::STATIC), builder);
    CJC_ASSERT(vtableRes.size() == 1);
    auto wrapperName = CHIRMangling::GenerateVirtualFuncMangleName(
        &callee, *vtableRes[0].originalDef, vtableRes[0].halfInstSrcParentType, false);
    auto it = mutFuncWrappers.find(wrapperName);
    CJC_ASSERT(it != mutFuncWrappers.end());
    return it->second;
}

void GenerateVTable::UpdateFuncCall()
{
    Utils::ProfileRecorder recorder("GenerateVTable", "UpdateFuncCall");
    std::vector<Apply*> applys;
    std::vector<ApplyWithException*> applyEs;
    std::function<VisitResult(Expression&)> preVisit = [this, &preVisit, &applys, &applyEs](Expression& e) {
        if (auto lambda = DynamicCast<Lambda*>(&e)) {
            Visitor::Visit(*lambda->GetBody(), preVisit);
        } else if (auto dyExpr = DynamicCast<DynamicDispatch*>(&e)) {
            e.Set<VirMethodOffset>(dyExpr->GetVirtualMethodOffset(&builder));
        } else if (auto dyExprE = DynamicCast<DynamicDispatchWithException*>(&e)) {
            e.Set<VirMethodOffset>(dyExprE->GetVirtualMethodOffset(&builder));
        } else if (auto apply = DynamicCast<Apply*>(&e)) {
            auto callee = DynamicCast<FuncBase*>(apply->GetCallee());
            if (CalleeIsMutFuncFromParent(apply->GetThisType(), callee, *e.GetTopLevelFunc())) {
                applys.emplace_back(apply);
            }
        } else if (auto applyE = DynamicCast<ApplyWithException*>(&e)) {
            auto callee = DynamicCast<FuncBase*>(applyE->GetCallee());
            if (CalleeIsMutFuncFromParent(applyE->GetThisType(), callee, *e.GetTopLevelFunc())) {
                applyEs.emplace_back(applyE);
            }
        }
        return VisitResult::CONTINUE;
    };
    for (auto func : package.GetGlobalFuncs()) {
        Visitor::Visit(*func, preVisit);
    }
    for (auto apply : applys) {
        auto callee = VirtualCast<FuncBase*>(apply->GetCallee());
        auto wrapperFunc = GetMutFuncWrapper(*apply->GetThisType(), apply->GetArgs(),
            apply->GetInstantiatedTypeArgs(), *apply->GetResult()->GetType(), *callee);
        apply->ReplaceOperand(callee, wrapperFunc);
    }
    for (auto apply : applyEs) {
        auto callee = VirtualCast<FuncBase*>(apply->GetCallee());
        auto wrapperFunc = GetMutFuncWrapper(*apply->GetThisType(), apply->GetArgs(),
            apply->GetInstantiatedTypeArgs(), *apply->GetResult()->GetType(), *callee);
        apply->ReplaceOperand(callee, wrapperFunc);
    }
}