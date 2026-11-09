// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/GenerateVTable/GenerateVTable.h"

#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/Transformation/GenerateVTable/UpdateOperatorVTable.h"
#include "cangjie/CHIR/Transformation/GenerateVTable/VTableGenerator.h"
#include "cangjie/CHIR/Transformation/GenerateVTable/WrapMutFunc.h"
#include "cangjie/CHIR/Transformation/GenerateVTable/WrapVirtualFunc.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/Utils/Visitor/Visitor.h"
#include "cangjie/Mangle/CHIRManglingUtils.h"
#include "cangjie/Utils/ProfileRecorder.h"
#include "cangjie/Utils/TaskQueue.h"

using namespace Cangjie;
using namespace Cangjie::CHIR;

namespace {
bool CalleeIsMutFuncFromParent(Type* thisType, Function* callee, const Function& topLevelFunc)
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

GenerateVTable::GenerateVTable(Package& pkg, const std::vector<CustomTypeDef*>& defs,
    CHIRBuilder& b, const Cangjie::GlobalOptions& opts, const std::string& passName)
    : package(pkg), candidateDefs(defs), builder(b), opts(opts), passName(passName)
{
}

void GenerateVTable::CreateVTable()
{
    Utils::ProfileRecorder recorder(passName, "CreateVTable");
    size_t threadNum = opts.GetJobs();
    if (threadNum == 1) {
        auto vtableGenerator = VTableGenerator(builder);
        for (auto customDef : candidateDefs) {
            vtableGenerator.GenerateVTable(*customDef);
        }
    } else {
        Utils::TaskQueue taskQueue(threadNum);
        std::vector<std::unique_ptr<VTableGenerator>> vtableGeneratorList;
        
        for (auto customDef : candidateDefs) {
            auto vtableGenerator = std::make_unique<VTableGenerator>(builder);
            VTableGenerator* generatorPtr = vtableGenerator.get();
            vtableGeneratorList.emplace_back(std::move(vtableGenerator));
            
            taskQueue.AddTask<void>([generatorPtr, customDef]() {
                generatorPtr->GenerateVTable(*customDef);
            });
        }

        taskQueue.RunAndWaitForAllTasksCompleted();
    }
}

void GenerateVTable::UpdateOperatorVirFunc()
{
    Utils::ProfileRecorder recorder(passName, "UpdateOperatorVirFunc");
    UpdateOperatorVTable(package, builder).Update();
}

void GenerateVTable::CreateVirtualFuncWrapper(const IncreKind& kind, const CompilationCache& increCachedInfo,
    VirtualWrapperDepMap& curVirtFuncWrapDep, VirtualWrapperDepMap& delVirtFuncWrapForIncr)
{
    Utils::ProfileRecorder recorder(passName, "CreateVirtualFuncWrapper");
    bool targetIsWin = opts.target.os == Triple::OSType::WINDOWS;
    IncreKind tempKind = opts.enIncrementalCompilation ? kind : IncreKind::INVALID;
    auto wrapper = WrapVirtualFunc(builder, increCachedInfo, tempKind, targetIsWin);
    for (auto customDef : candidateDefs) {
        if (customDef->TestAttr(Attribute::SKIP_ANALYSIS)) {
            continue;
        }
        wrapper.CheckAndWrap(*customDef);
    }
    curVirtFuncWrapDep = wrapper.GetCurVirtFuncWrapDep();
    delVirtFuncWrapForIncr = wrapper.GetDelVirtFuncWrapForIncr();
}

void GenerateVTable::SetSrcFuncType() const
{
    Utils::ProfileRecorder recorder(passName, "SetSrcFuncType");
    auto getSrcFuncType = [](const ClassDef& parentDef, size_t index) -> FuncType* {
        const auto& vtable = parentDef.GetDefVTable().GetExpectedTypeVTable(*parentDef.GetType());
        CJC_ASSERT(!vtable.IsEmpty());
        CJC_ASSERT(index < vtable.GetMethodNum());
        auto srcFuncType = vtable.GetVirtualMethods()[index].GetOriginalFuncType();
        CJC_NULLPTR_CHECK(srcFuncType);
        return srcFuncType;
    };
    for (auto customTypeDef : candidateDefs) {
        const auto& vtable = customTypeDef->GetDefVTable().GetTypeVTables();
        for (const auto& it : vtable) {
            for (size_t i = 0; i < it.GetMethodNum(); ++i) {
                auto& funcInfo = it.GetVirtualMethods()[i];
                auto instance = funcInfo.GetVirtualMethod();
                if (!instance->IsPureAbstract()) {
                    instance->Set<OverrideSrcFuncType>(getSrcFuncType(*it.GetSrcParentType()->GetClassDef(), i));
                }
            }
        }
    }
}

void GenerateVTable::CreateMutFuncWrapper()
{
    Utils::ProfileRecorder recorder(passName, "CreateMutFuncWrapper");
    auto wrapper = WrapMutFunc(builder);
    for (auto customDef : candidateDefs) {
        if (customDef->TestAttr(Attribute::SKIP_ANALYSIS)) {
            continue;
        }
        wrapper.Run(*customDef);
    }
    mutFuncWrappers = wrapper.GetWrappers();
}

Function* GenerateVTable::GetMutFuncWrapper(const Type& thisType, const std::vector<Value*>& args,
    const std::vector<Type*>& instTypeArgs, Type& retType, const Function& callee)
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
    auto vtableRes = GetFuncIndexInVTable(*thisType.StripAllRefs(), funcCallType, builder);
    CJC_ASSERT(vtableRes.size() == 1);
    auto wrapperName = CHIRMangling::GenerateVirtualFuncMangleName(
        &callee, *vtableRes[0].originalDef, vtableRes[0].halfInstSrcParentType, false);
    auto it = mutFuncWrappers.find(wrapperName);
    CJC_ASSERT(it != mutFuncWrappers.end());
    return it->second;
}

void GenerateVTable::UpdateFuncCall()
{
    Utils::ProfileRecorder recorder(passName, "UpdateFuncCall");
    auto preVisit1 = [](Expression& e) {
        auto dyExpr = DynamicCast<DynamicDispatch*>(&e);
        if (dyExpr == nullptr) {
            return VisitResult::CONTINUE;
        }
        auto callee = dyExpr->GetCallee();
        auto expectedCallee = GetCalleeInSrcParentType(
            *callee, dyExpr->GetMethodName(), dyExpr->Get<VirMethodOffset>());
        dyExpr->ReplaceOperand(callee, expectedCallee);
        return VisitResult::CONTINUE;
    };
    for (auto func : package.GetGlobalFuncsWithBody()) {
        if (func->TestAttr(Attribute::DESERIALIZED)) {
            Visitor::Visit(*func, preVisit1);
        }
    }

    auto preVisit2 = [this](Expression& e) {
        if (auto dyExpr = DynamicCast<DynamicDispatch*>(&e)) {
            e.Set<VirMethodOffset>(dyExpr->GetVirtualMethodOffset());
        } else if (auto apply = DynamicCast<ApplyBase*>(&e)) {
            auto callee = DynamicCast<Function*>(apply->GetCallee());
            if (CalleeIsMutFuncFromParent(apply->GetThisType(), callee, *e.GetTopLevelFunc())) {
                auto wrapperFunc = GetMutFuncWrapper(*apply->GetThisType(), apply->GetArgs(),
                    apply->GetInstantiatedTypeArgs(), *apply->GetResult()->GetType(), *callee);
                apply->ReplaceOperand(callee, wrapperFunc);
            }
        }
        return VisitResult::CONTINUE;
    };
    for (auto func : package.GetGlobalFuncsWithBody()) {
        Visitor::Visit(*func, preVisit2);
    }
}