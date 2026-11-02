// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Optimization/Devirtualization.h"

#include <optional>
#include <unordered_set>

#include "cangjie/CHIR/Analysis/DevirtualizationInfo.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/Utils/UserDefinedType.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/Utils/Visitor/Visitor.h"
#include "cangjie/CHIR/Optimization/BlockGroupCopyHelper.h"
#include "cangjie/Mangle/CHIRManglingUtils.h"
#include "cangjie/Modules/ModulesUtils.h"
namespace Cangjie::CHIR {
namespace {
static const std::unordered_map<BinaryExprKind, FuncInfo> BINARY_FUNC_MAP = {
    {BinaryExprKind::GT, FuncInfo(">", ANY_TYPE, {ANY_TYPE}, ANY_TYPE, CORE_PACKAGE_NAME)},
    {BinaryExprKind::LT, FuncInfo("<", ANY_TYPE, {ANY_TYPE}, ANY_TYPE, CORE_PACKAGE_NAME)},
    {BinaryExprKind::GE, FuncInfo(">=", ANY_TYPE, {ANY_TYPE}, ANY_TYPE, CORE_PACKAGE_NAME)},
    {BinaryExprKind::LE, FuncInfo("<=", ANY_TYPE, {ANY_TYPE}, ANY_TYPE, CORE_PACKAGE_NAME)},
    {BinaryExprKind::EQUAL, FuncInfo("==", ANY_TYPE, {ANY_TYPE}, ANY_TYPE, CORE_PACKAGE_NAME)},
    {BinaryExprKind::NOTEQUAL, FuncInfo("!=", ANY_TYPE, {ANY_TYPE}, ANY_TYPE, CORE_PACKAGE_NAME)},
};

std::vector<CustomTypeDef*> CollectAllDefs(const Type& type, CHIRBuilder& builder)
{
    std::vector<CustomTypeDef*> defs;
    if (auto customType = DynamicCast<const CustomType*>(&type)) {
        auto def = customType->GetCustomTypeDef();
        const auto& extends = def->GetExtends();
        defs.reserve(extends.size() + 1);
        defs.emplace_back(def);
        defs.insert(defs.end(), extends.begin(), extends.end());
    } else {
        auto builtinType = StaticCast<const BuiltinType*>(&type);
        const auto& extends = builtinType->GetExtends(&builder);
        defs.reserve(extends.size());
        defs.insert(defs.end(), extends.begin(), extends.end());
    }
    return defs;
}

Function* SearchRealCalleeInVtable(const Type& type, const DynamicDispatch& invoke, CHIRBuilder& builder)
{
    std::vector<CustomTypeDef*> allDefs = CollectAllDefs(type, builder);
    auto srcParentTypeInRT = invoke.GetInstSrcParentCustomTypeOfMethod(builder);
    std::unordered_map<const GenericType*, Type*> replaceTable;
    if (auto customType = DynamicCast<CustomType*>(&type)) {
        replaceTable = GetInstMapFromCurDefAndExDefToCurType(*customType);
    }
    for (auto def : allDefs) {
        for (const auto& vtable : def->GetDefVTable().GetTypeVTables()) {
            auto srcParentTypeInVtable = vtable.GetSrcParentType();
            if (srcParentTypeInVtable->GetClassDef() != srcParentTypeInRT->GetClassDef()) {
                continue;
            }
            auto instSrcParentTypeInVtable = ReplaceRawGenericArgType(*srcParentTypeInVtable, replaceTable, builder);
            if (instSrcParentTypeInVtable != srcParentTypeInRT) {
                continue;
            }
            auto methods = vtable.GetVirtualMethods();
            CJC_ASSERT(methods.size() > invoke.GetVirtualMethodOffset());
            auto tempTarget = methods[invoke.GetVirtualMethodOffset()].GetVirtualMethod();
            CJC_NULLPTR_CHECK(tempTarget);
            if (!tempTarget->IsPureAbstract()) {
                if (auto rawMethod = tempTarget->Get<WrappedRawMethod>()) {
                    return rawMethod;
                } else {
                    return tempTarget;
                }
            }
            break;
        }
    }
    return nullptr;
}

bool HasUnknownGenericType(const Type& knownType, const Type& unknownType)
{
    std::unordered_set<const GenericType*> knownGenericTypes;
    knownType.VisitTypeRecursively([&knownGenericTypes](const Type& type) {
        if (type.IsGeneric()) {
            knownGenericTypes.insert(StaticCast<const GenericType*>(&type));
        }
        return true;
    });
    bool hasUnknownGenericType = false;
    unknownType.VisitTypeRecursively([&knownGenericTypes, &hasUnknownGenericType](const Type& type) {
        if (auto genericType = DynamicCast<const GenericType*>(&type)) {
            if (knownGenericTypes.count(genericType) == 0) {
                hasUnknownGenericType = true;
                return false;
            }
        }
        return true;
    });
    return hasUnknownGenericType;
}

std::pair<Value*, std::vector<TypeCast*>> CollectUpstreamTypeCasts(LocalVar& tempThisObj)
{
    std::vector<TypeCast*> castExprs;
    Value* thisValue = &tempThisObj;
    std::function<void(LocalVar&)> findThisValue = [&findThisValue, &castExprs, &thisValue](LocalVar& tempVar) {
        auto expr = tempVar.GetExpr();
        if (Is<Box>(expr) || Is<ClassStaticCast>(expr)) {
            castExprs.emplace_back(StaticCast<TypeCast*>(expr));
            thisValue = StaticCast<TypeCast*>(expr)->GetSourceValue();
            if (auto localVar = DynamicCast<LocalVar*>(thisValue)) {
                findThisValue(*localVar);
            }
        }
    };
    findThisValue(tempThisObj);
    return std::make_pair(thisValue, castExprs);
}

Block* GetEntryBlock(const BlockGroup& oldBG, std::vector<Block*>& blocks)
{
    auto oldBlocks = oldBG.GetBlocks();
    CJC_ASSERT(oldBlocks.size() == blocks.size());
    CJC_ASSERT(!blocks.empty());
    for (size_t i = 0; i < oldBlocks.size(); ++i) {
        if (oldBlocks[i] == oldBG.GetEntryBlock()) {
            return blocks[i];
        }
    }
    CJC_ABORT();
    return blocks.front();
}

std::string CreateInstFuncMangleName(
    const std::string& oriIdentifer, Function& func, const FuncCallContext& context, CHIRBuilder& builder)
{
    // 1. get type args
    std::vector<Type*> genericTypes;
    if (auto customDef = func.GetParentCustomTypeDef(); customDef && customDef->IsGenericDef()) {
        auto instParentCustomTy = GetInstParentCustomTyOfCallee(func, context.args, context.thisType, builder);
        CJC_NULLPTR_CHECK(instParentCustomTy);
        genericTypes = instParentCustomTy->GetTypeArgs();
    }
    if (!context.instTypeArgs.empty()) {
        genericTypes.insert(genericTypes.end(), context.instTypeArgs.begin(), context.instTypeArgs.end());
    }
    // 2. get mangle
    return CHIRMangling::GenerateInstantiateFuncMangleName(oriIdentifer, genericTypes);
}

FuncType* GetInstFuncTypeFromContext(Function& func, const FuncCallContext& context, CHIRBuilder& builder)
{
    std::unordered_map<const GenericType*, Type*> replaceTable;
    if (auto customDef = func.GetParentCustomTypeDef(); customDef && customDef->IsGenericDef()) {
        auto instParentCustomTy = GetInstParentCustomTyOfCallee(func, context.args, context.thisType, builder);
        CJC_NULLPTR_CHECK(instParentCustomTy);
        auto [matched, tempTable] = customDef->GetType()->CalculateGenericTyMapping(*instParentCustomTy);
        CJC_ASSERT(matched);
        replaceTable = std::move(tempTable);
    }
    auto genericTypeParams = func.GetGenericTypeParams();
    CJC_ASSERT(genericTypeParams.size() == context.instTypeArgs.size());
    for (size_t i = 0; i < genericTypeParams.size(); i++) {
        replaceTable.emplace(genericTypeParams[i], context.instTypeArgs[i]);
    }
    return StaticCast<FuncType*>(ReplaceRawGenericArgType(*func.GetType(), replaceTable, builder));
}
}

Devirtualization::Devirtualization(TypeAnalysisWrapper* typeAnalysisWrapper, DevirtualizationInfo& devirtFuncInfo,
    CHIRBuilder& builder, const Package& package, const GlobalOptions& opts)
    : analysisWrapper(typeAnalysisWrapper),
      devirtFuncInfo(devirtFuncInfo),
      builder(builder),
      package(package),
      opts(opts)
{
}

bool Devirtualization::IsSubtypeSetComplete(const CustomTypeDef& def) const
{
    // True iff subtypeMap[def] lists every possible subtype for ResolveNonStaticCalleeAndThisType.
    auto relation = Modules::GetPackageRelation(def.GetPackageName(), package.GetName());
    if (def.TestAttr(Attribute::PUBLIC) || def.TestAttr(Attribute::PROTECTED)) {
        // Closed-world only for a true whole-program executable.
        // Do NOT use CompileExecutable(): --output-type=obj without --compile-target
        // defaults compileTarget to EXECUTABLE (SetupCompileTargetOptions), so a
        // per-package obj build of a library would also look "executable". Subtypes
        // defined in other packages that are linked later are invisible here;
        // treating the type as closed would let ResolveNonStaticCalleeAndThisType rewrite Invoke to a
        // unique local Apply and silently mis-dispatch at run time.
        if (opts.outputMode == GlobalOptions::OutputMode::EXECUTABLE &&
            relation == Modules::PackageRelation::SAME_PACKAGE) {
            return true;
        }
        return false;
    }
    if (def.TestAttr(Attribute::PRIVATE)) {
        return true;
    }
    if (def.TestAttr(Attribute::INTERNAL)) {
        if (relation != Modules::PackageRelation::CHILD && relation != Modules::PackageRelation::SAME_PACKAGE) {
            return true;
        }
        if (opts.noSubPkg) {
            return true;
        }
        return false;
    }
    return false;
}

void Devirtualization::RunOnFuncs(const std::vector<Function*>& funcs)
{
    rewriteInfos.clear();
    for (auto func : funcs) {
        RunOnFuncForInvoke(*func);
        RunOnFuncForInvokeStatic(*func);
    }
    RewriteToApply(rewriteInfos);
}

Function* Devirtualization::ResolveStaticCallee(InvokeStaticBase& invoke) const
{
    auto rttiStatic = DynamicCast<GetRTTIStatic*>(StaticCast<LocalVar>(invoke.GetRTTIValue())->GetExpr());
    // RTTI(value) or RTTIStatic(type), RTTI(value) means we get value's type in runtime,
    // so we can't rewrite it in compile time.
    if (rttiStatic == nullptr) {
        return nullptr;
    }
    auto thisType = invoke.GetThisType()->StripAllRefs();
    if (thisType->IsGeneric() || thisType->IsThis()) {
        return nullptr;
    }
    return SearchRealCalleeInVtable(*thisType, invoke, builder);
}

void Devirtualization::RunOnFuncForInvokeStatic(const Function& func)
{
    Visitor::Visit(func, [this](Expression& expr) {
        if (auto invokeStatic = DynamicCast<InvokeStaticBase*>(&expr)) {
            auto realCallee = ResolveStaticCallee(*invokeStatic);
            if (realCallee == nullptr) {
                return VisitResult::CONTINUE;
            }
            rewriteInfos.emplace_back(RewriteInfo{invokeStatic, realCallee, invokeStatic->GetThisType()});
        }
        return VisitResult::CONTINUE;
    });
}

void Devirtualization::RunOnFuncForInvoke(const Function& func)
{
    auto result = analysisWrapper->CheckFuncResult(&func);
    if (result == nullptr && frozenStates.count(&func) != 0) {
        result = frozenStates.at(&func).get();
    }
    if (result == nullptr) {
        return;
    }
    auto tryCollect = [this](const TypeDomain& state, InvokeBase* invoke) {
        auto object = invoke->GetObject();
        auto invokeAbsObject = state.CheckAbstractObjectRefBy(object);
        // Obtains the state information of the invoke operation object.
        auto resVal = state.CheckAbstractValue(invokeAbsObject);
        if (!resVal) {
            return;
        }
        auto [realCallee, thisType] = ResolveNonStaticCalleeAndThisType(resVal, *invoke);
        if (realCallee == nullptr || thisType == nullptr) {
            return;
        }
        rewriteInfos.emplace_back(RewriteInfo{invoke, realCallee, thisType});
    };

    const auto actionBeforeVisitExpr =
        [&tryCollect](const TypeDomain& state, Expression* expr, size_t) {
        if (auto invoke = DynamicCast<Invoke*>(expr)) {
            tryCollect(state, invoke);
        }
    };

    const auto actionAfterVisitExpr = [](const TypeDomain&, Expression*, size_t) {};
    const auto actionOnTerminator =
        [&tryCollect](const TypeDomain& state, Expression* expr, std::optional<Block*>) {
        if (auto tryInvoke = DynamicCast<TryInvoke*>(expr)) {
            tryCollect(state, tryInvoke);
        }
    };
    result->VisitWith(actionBeforeVisitExpr, actionAfterVisitExpr, actionOnTerminator);
}

bool Devirtualization::RewriteToBuiltinOp(const RewriteInfo& info)
{
    std::optional<BinaryExprKind> targetBinaryKind;
    for (auto& it : BINARY_FUNC_MAP) {
        if (IsExpectedFunction(*info.realCallee, it.second)) {
            targetBinaryKind = it.first;
            break;
        }
    }
    if (!targetBinaryKind.has_value()) {
        return false;
    }
    auto args = info.invoke->GetArgs();
    if (args.size() != 2U) {
        return false;
    }
    Value* leftOp = args[0];
    Value* rightOp = args[1];
    std::vector<TypeCast*> leftCastExprs;
    // after other optimizations, the left operand may be casted to a non-primitive type,
    // we need to find the original one and remove the cast expressions
    if (auto localVar = DynamicCast<LocalVar*>(leftOp)) {
        std::tie(leftOp, leftCastExprs) = CollectUpstreamTypeCasts(*localVar);
    }
    if (!leftOp->GetType()->IsPrimitive() || !rightOp->GetType()->IsPrimitive()) {
        return false;
    }
    auto nextBlock = info.invoke->GetSuccessAndErrorBlocks().first;
    auto loc = info.invoke->GetDebugLocation();
    auto retType = info.invoke->GetResultType();
    auto parent = info.invoke->GetParentBlock();
    auto binaryExpr =
        builder.CreateExpression<BinaryExpression>(loc, retType, *targetBinaryKind, leftOp, rightOp, parent);
    info.invoke->ReplaceWith(*binaryExpr);
    for (auto e : leftCastExprs) {
        if (e->GetResult()->GetUsers().empty()) {
            e->RemoveSelfFromBlock();
        }
    }
    if (nextBlock != nullptr) {
        auto goToExpr = builder.CreateTerminator<GoTo>(nextBlock, parent);
        goToExpr->MoveAfter(binaryExpr);
    }
    return true;
}

Type* Devirtualization::AddRefIfNeeded(Type& thisType, Function& callee)
{
    if (thisType.IsReferenceType()) {
        return builder.GetType<RefType>(&thisType);
    }
    if (thisType.IsValueType() &&
        (callee.TestAttr(Attribute::MUT) || callee.IsConstructor() || callee.IsInstanceVarInit())) {
        return builder.GetType<RefType>(&thisType);
    }
    return &thisType;
}

void Devirtualization::RewriteToApply(std::vector<RewriteInfo>& infos)
{
    for (auto rewriteInfo = infos.rbegin(); rewriteInfo != infos.rend(); ++rewriteInfo) {
        if (RewriteToBuiltinOp(*rewriteInfo)) {
            continue;
        }
        // 1. instantiate callee if possible
        auto realFunc = rewriteInfo->realCallee;
        auto thisType = AddRefIfNeeded(*rewriteInfo->thisType, *realFunc);
        auto invoke = rewriteInfo->invoke;
        auto args = invoke->GetArgs();
        auto context = FuncCallContext {
            .args = args,
            .instTypeArgs = rewriteInfo->invoke->GetInstantiatedTypeArgs(),
            .thisType = thisType
        };
        auto expectedRetTy = invoke->GetResultType();
        auto instRetTy = expectedRetTy;
        if (auto instFunc = CreateInstFuncIfPossible(realFunc, context)) {
            realFunc = instFunc;
            instRetTy = instFunc->GetFuncType()->GetReturnType();
        }

        // 2. get a correct type for args[0]
        auto parent = invoke->GetParentBlock();
        if (Is<InvokeBase>(invoke)) {
            // The declaring type has no modal; use the instantiated receiver parameter to preserve
            // the callee's modal and reference layers, including for instantiated global functions.
            auto instFuncType = GetInstFuncTypeFromContext(*realFunc, context, builder);
            auto instThisType = instFuncType->GetParamType(0);
            auto [typecastRes, newExprs] =
                TypeCastOrBoxIfNeeded(*context.args[0], *instThisType, builder, *parent);
            if (typecastRes != context.args[0]) {
                for (auto newExpr : newExprs) {
                    newExpr->MoveBefore(invoke);
                }
                context.args[0] = typecastRes;
            }
        }

        // 3. rewrite to apply
        Expression* newCall = nullptr;
        auto loc = invoke->GetDebugLocation();
        auto [successBlock, errorBlock] = invoke->GetSuccessAndErrorBlocks();
        if (successBlock && errorBlock) {
            newCall = builder.CreateExpression<TryApply>(
                loc, instRetTy, realFunc, context, successBlock, errorBlock, parent);
        } else {
            newCall = builder.CreateExpression<Apply>(loc, instRetTy, realFunc, context, parent);
        }
        invoke->ReplaceWith(*newCall);
        AddTypeCastForReturnVal(*newCall, *expectedRetTy, builder);
    }
}

const std::vector<Function*>& Devirtualization::GetFrozenInstFuns() const
{
    return frozenInstFuns;
}

void Devirtualization::AppendFrozenFuncState(const Function* func, std::unique_ptr<Results<TypeDomain>> analysisRes)
{
    frozenStates.emplace(func, std::move(analysisRes));
}

Function* Devirtualization::CreateInstFuncIfPossible(Function* func, FuncCallContext& context)
{
    auto canBeFullyInstantiated = [](const Type& thisType, const std::vector<Type*>& typeArgs) {
        if (thisType.IsGenericRelated()) {
            return false;
        }
        for (auto typeArg : typeArgs) {
            if (typeArg->IsGenericRelated()) {
                return false;
            }
        }
        return true;
    };
    if (func->GetBody() == nullptr || !func->IsInGenericContext() ||
        !canBeFullyInstantiated(*context.thisType, context.instTypeArgs)) {
        return nullptr;
    }
    auto newId = CreateInstFuncMangleName(func->GetIdentifierWithoutPrefix(), *func, context, builder);
    Function* newFunc = nullptr;
    if (frozenInstFuncMap.count(newId) != 0) {
        newFunc = frozenInstFuncMap.at(newId);
    } else {
        BlockGroupCopyHelper helper(builder);
        helper.GetInstMapFromFuncCall(*func, context.thisType, context.instTypeArgs, context.args);
        auto instFuncType = GetInstFuncTypeFromContext(*func, context, builder);
        newFunc = builder.CreateFunction(
            instFuncType, newId, func->GetSrcCodeIdentifier(), "", func->GetPackageName());
        newFunc->SetDebugLocation(func->GetDebugLocation());
        newFunc->AppendAttributeInfo(func->GetAttributeInfo());
        newFunc->DisableAttr(Attribute::GENERIC);
        newFunc->DisableAttr(Attribute::STATIC);
        if (!context.instTypeArgs.empty()) {
            newFunc->EnableAttr(Attribute::GENERIC_INSTANTIATED);
        }
        newFunc->Set<LinkTypeInfo>(Linkage::INTERNAL);
        newFunc->SetGenericDecl(*func);

        auto oriBlockGroup = func->GetBody();
        auto newBody = builder.CreateBlockGroup(*newFunc);
        newFunc->InitBody(*newBody);
        auto [newBlocks, newBlockGroupRetValue] = helper.CloneBlockGroup(*oriBlockGroup, *newBody);
        auto funcEntry = GetEntryBlock(*oriBlockGroup, newBlocks);
        newBody->SetEntryBlock(funcEntry);
        newFunc->SetReturnValue(*newBlockGroupRetValue);
        auto parameterType = instFuncType->GetParamTypes();
        CJC_ASSERT(parameterType.size() == func->GetParams().size());
        std::unordered_map<Value*, Value*> paramMap;
        for (size_t i = 0; i < parameterType.size(); i++) {
            auto arg = builder.CreateParameter(parameterType[i], func->GetParam(i)->GetDebugLocation(), *newFunc);
            paramMap.emplace(func->GetParam(i), arg);
        }
        helper.ReplaceExprOperands(newBlocks, paramMap);

        FixCastProblemAfterInst(newBlocks, builder);
        newFunc->SetReturnValue(*newBlockGroupRetValue);
        frozenInstFuns.push_back(newFunc);
        frozenInstFuncMap[newId] = newFunc;
    }
    // Instantiated callee no longer needs call-site type args.
    context.instTypeArgs.clear();
    context.thisType = nullptr;
    return newFunc;
}

std::vector<Type*> Devirtualization::CollectAllSubTypes(ClassType& specific) const
{
    // Only return a non-empty set when every inheritable type on the closure is
    // closed-world; otherwise subtypeMap may miss external subclasses.
    if (specific.CanBeInherited() && !IsSubtypeSetComplete(*specific.GetClassDef())) {
        return {};
    }
    std::vector<Type*> allSubTypes{&specific};
    std::unordered_set<Type*> visited{&specific};
    std::vector<ClassType*> workList{&specific};
    while (!workList.empty()) {
        auto cur = workList.back();
        workList.pop_back();
        auto it = devirtFuncInfo.subtypeMap.find(cur->GetClassDef());
        if (it == devirtFuncInfo.subtypeMap.end()) {
            continue;
        }
        for (auto& inheritInfo : it->second) {
            auto [matched, replaceTable] = inheritInfo.parentType->CalculateGenericTyMapping(*cur);
            if (!matched) {
                continue;
            }
            auto subtype = ReplaceRawGenericArgType(*inheritInfo.subType, replaceTable, builder);
            if (!visited.insert(subtype).second) {
                continue;
            }
            if (subtype->CanBeInherited()) {
                auto def = StaticCast<ClassType*>(subtype)->GetClassDef();
                if (!IsSubtypeSetComplete(*def)) {
                    return {};
                }
                workList.emplace_back(StaticCast<ClassType*>(subtype));
            }
            allSubTypes.emplace_back(subtype);
        }
    }
    return allSubTypes;
}

std::pair<Function*, Type*> Devirtualization::ResolveNonStaticCalleeAndThisType(
    const TypeValue* typeState, const InvokeBase& invoke) const
{
    auto typeStateKind = typeState->GetTypeKind();
    auto specificType = typeState->GetSpecificType();
    if (!specificType->CanBeInherited() || typeStateKind == DevirtualTyKind::EXACTLY) {
        // 1. final class, struct, enum or builtin type
        // 2. exact type, even if open class type
        auto target = SearchRealCalleeInVtable(*specificType, invoke, builder);
        CJC_NULLPTR_CHECK(target);
        return std::make_pair(target, specificType);
    } else {
        // 3. open class: unique impl among a closed-world subtype set may de-virt
        auto subTypes = CollectAllSubTypes(*StaticCast<ClassType*>(specificType));
        std::unordered_set<Function*> targets;
        for (auto subtype : subTypes) {
            auto target = SearchRealCalleeInVtable(*subtype, invoke, builder);
            if (target != nullptr) {
                targets.emplace(target);
            }
        }
        Function* finalCallee = targets.size() == 1 ? *targets.begin() : nullptr;
        if (finalCallee == nullptr) {
            return std::make_pair(nullptr, nullptr);
        }
        /*
         * EXACTLY / final (non-inheritable) receivers: keep TypeAnalysis's specificType as thisType so
         * instantiated args (e.g. Impl<Int64>) are not lost.
         * Matching the pre-refactor ResolveNonStaticCalleeAndThisType return of {callee, specificType}.
         *
         * SUBTYPE_OF unique-impl: instantiate callee's declaring type from the invoke's vtable parent.
         * If the subtype introduces free generics not present on the parent (e.g. CA<T> <: I), skip:
         *   interface I { func foo() }
         *   class CA<T> <: I { public func foo() {} }
         *   func goo(a: I) { a.foo() }  // unique candidate but free T — must not de-virt
         */
        auto srcParentType = invoke.GetInstSrcParentCustomTypeOfMethod(builder);
        auto genericSubType = finalCallee->GetParentCustomTypeOrExtendedType();
        CJC_NULLPTR_CHECK(genericSubType);
        auto thisType = GetInstSubType(*genericSubType, *srcParentType, builder);
        if (HasUnknownGenericType(*srcParentType, *thisType)) {
            return std::make_pair(finalCallee, nullptr);
        }
        return std::make_pair(finalCallee, thisType);
    }
}
}
