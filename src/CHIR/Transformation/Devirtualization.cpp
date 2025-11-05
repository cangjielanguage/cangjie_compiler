// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/Devirtualization.h"

#include "cangjie/CHIR/Transformation/DeadCodeElimination.h"
#include "cangjie/CHIR/Analysis/DevirtualizationInfo.h"
#include "cangjie/CHIR/Analysis/Engine.h"
#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/UserDefinedType.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/CHIR/Transformation/BlockGroupCopyHelper.h"
#include "cangjie/Mangle/CHIRManglingUtils.h"
namespace Cangjie::CHIR {

Devirtualization::Devirtualization(
    TypeAnalysisWrapper* typeAnalysisWrapper, DevirtualizationInfo& devirtFuncInfo)
    : analysisWrapper(typeAnalysisWrapper), devirtFuncInfo(devirtFuncInfo)
{
}

void Devirtualization::RunOnFuncs(const std::vector<Func*>& funcs, CHIRBuilder& builder, bool isDebug)
{
    rewriteInfos.clear();
    for (auto func : funcs) {
        RunOnFunc(func, builder);
    }
    RewriteToApply(builder, rewriteInfos, isDebug);
    InstantiateFuncIfPossible(builder, rewriteInfos);
}

void Devirtualization::RunOnFunc(const Func* func, CHIRBuilder& builder)
{
    auto result = analysisWrapper->CheckFuncResult(func);
    if (result == nullptr && frozenStates.count(func) != 0) {
        result = frozenStates.at(func).get();
    }
    if (result == nullptr) {
        return;
    }
    const auto actionBeforeVisitExpr = [this, &builder](const TypeDomain& state, Expression* expr, size_t) {
        if (expr->GetExprKind() != ExprKind::INVOKE) {
            return;
        }
        auto invoke = StaticCast<Invoke*>(expr);
        auto object = invoke->GetObject();
        auto invokeAbsObject = state.CheckAbstractObjectRefBy(object);
        // Obtains the state information of the invoke operation object.
        auto resVal = state.CheckAbstractValue(invokeAbsObject);
        if (!resVal) {
            return;
        }
        std::vector<Type*> paramTys;
        for (auto param : invoke->GetOperands()) {
            paramTys.emplace_back(param->GetType());
        }
        // Grab the function from the classMap.
        auto [realCallee, thisType] = FindRealCallee(builder, resVal,
            {invoke->GetMethodName(), std::move(paramTys), invoke->GetInstantiatedTypeArgs()});
        if (!realCallee) {
            return;
        }
        rewriteInfos.emplace_back(RewriteInfo{invoke, realCallee, thisType, invoke->GetInstantiatedTypeArgs()});
    };

    const auto actionAfterVisitExpr = [](const TypeDomain&, Expression*, size_t) {};
    const auto actionOnTerminator = [](const TypeDomain&, Terminator*, std::optional<Block*>) {};
    result->VisitWith(actionBeforeVisitExpr, actionAfterVisitExpr, actionOnTerminator);
}

namespace {
struct BuiltinOpInfo {
    BuiltinOpInfo(FuncInfo info, ExprKind exprKind, size_t operandsNum)
        : funcInfo(std::move(info)), targetExprKind(exprKind), operandsNum(operandsNum) {}

    FuncInfo funcInfo;
    ExprKind targetExprKind{ExprKind::INVALID};
    size_t operandsNum{0};
};

static const std::vector<BuiltinOpInfo> COMPARABLE_FUNC_LISTS = {
    {FuncInfo(">", NOT_CARE, {NOT_CARE}, ANY_TYPE, "std.core"),    ExprKind::GT,       2U},
    {FuncInfo("<", NOT_CARE, {NOT_CARE}, ANY_TYPE, "std.core"),    ExprKind::LT,       2U},
    {FuncInfo(">=", NOT_CARE, {NOT_CARE}, ANY_TYPE, "std.core"),   ExprKind::GE,       2U},
    {FuncInfo("<=", NOT_CARE, {NOT_CARE}, ANY_TYPE, "std.core"),   ExprKind::LE,       2U},
    {FuncInfo("==", NOT_CARE, {NOT_CARE}, ANY_TYPE, "std.core"),   ExprKind::EQUAL,    2U},
    {FuncInfo("!=", NOT_CARE, {NOT_CARE}, ANY_TYPE, "std.core"),   ExprKind::NOTEQUAL, 2U},
    {FuncInfo("next", NOT_CARE, {NOT_CARE}, ANY_TYPE, "std.core"), ExprKind::APPLY,    2U}
};

Ptr<Apply> BuiltinOpCreateNewApply(CHIRBuilder& builder, const Invoke& oriInvoke, Ptr<FuncBase> func,
    const Ptr<Value>& thisValue, const std::vector<Value*>& args)
{
    auto instRetTy = oriInvoke.GetResultType();
    std::vector<Value*> applyArgs{thisValue};
    applyArgs.insert(applyArgs.end(), args.begin(), args.end());
    auto thisType = builder.GetType<RefType>(thisValue->GetType());
    return builder.CreateExpression<Apply>(instRetTy, func, FuncCallContext{
        .args = applyArgs,
        .thisType = thisType}, oriInvoke.GetParentBlock());
}

Ptr<BinaryExpression> BuiltinOpCreateNewBinary(CHIRBuilder& builder, const Invoke& oriInvoke, ExprKind kind,
    const Ptr<Value>& thisValue, const std::vector<Value*>& args)
{
    CJC_ASSERT(args.size() == 1U);
    auto instRetTy = oriInvoke.GetResultType();
    auto parent = oriInvoke.GetParentBlock();
    return builder.CreateExpression<BinaryExpression>(
        oriInvoke.GetDebugLocation(), instRetTy, kind, thisValue, args[0], parent);
}
}

bool Devirtualization::RewriteToBuiltinOp(CHIRBuilder& builder, const RewriteInfo& info, bool isDebug)
{
    auto invoke = info.invoke;
    auto func = info.realCallee->Get<WrappedRawMethod>() ? info.realCallee->Get<WrappedRawMethod>() : info.realCallee;

    ExprKind targetExprKind{ExprKind::INVALID};
    size_t operandsNum = 0;
    for (auto& it : COMPARABLE_FUNC_LISTS) {
        if (IsExpectedFunction(*func, it.funcInfo)) {
            targetExprKind = it.targetExprKind;
            operandsNum = it.operandsNum;
        }
    }
    auto args = invoke->GetArgs();
    args.erase(args.begin());  // remove `this`
    if (targetExprKind == ExprKind::INVALID || args.size() != operandsNum - 1U) {
        return false;
    }
    for (auto& arg : args) {
        if (!arg->GetType()->IsPrimitive()) {
            return false;
        }
    }
    auto thisValue = invoke->GetObject();
    std::vector<Expression*> castExprs;
    std::function<void(LocalVar&)> findThisValue = [&thisValue, &castExprs, &findThisValue](LocalVar& tempVar) {
        auto expr = tempVar.GetExpr();
        if (expr->GetExprKind() == ExprKind::BOX || expr->GetExprKind() == ExprKind::TYPECAST) {
            castExprs.emplace_back(expr);
            thisValue = expr->GetOperand(0);
            if (auto localVar = DynamicCast<LocalVar*>(thisValue)) {
                findThisValue(*localVar);
            }
        }
    };
    if (thisValue->IsLocalVar() && !thisValue->GetType()->IsPrimitive()) {
        findThisValue(*StaticCast<LocalVar*>(thisValue));
    }
    if (!thisValue->GetType()->IsPrimitive()) {
        return false;
    }
    Ptr<Expression> op;
    if (targetExprKind == ExprKind::APPLY) {
        op = BuiltinOpCreateNewApply(builder, *invoke, func, thisValue, args);
    } else {
        op = BuiltinOpCreateNewBinary(builder, *invoke, targetExprKind, thisValue, args);
    }
    invoke->ReplaceWith(*op);
    for (auto e : castExprs) {
        if (e->GetResult()->GetUsers().empty()) {
            e->RemoveSelfFromBlock();
        }
    }
    if (isDebug) {
        std::string callName =
            targetExprKind == ExprKind::APPLY ? func->GetSrcCodeIdentifier() : op->GetExprKindName();
        std::string message = "[Devirtualization] The function call to " + invoke->GetMethodName() +
                              ToPosInfo(invoke->GetDebugLocation()) + " was optimized to builtin op " + callName + ".";
        std::cout << message << std::endl;
    }
    return true;
}

void Devirtualization::RewriteToApply(CHIRBuilder& builder, std::vector<RewriteInfo>& rewriteInfos, bool isDebug)
{
    for (auto rewriteInfo = rewriteInfos.rbegin(); rewriteInfo != rewriteInfos.rend(); ++rewriteInfo) {
        if (RewriteToBuiltinOp(builder, *rewriteInfo, isDebug)) {
            continue;
        }
        auto invoke = rewriteInfo->invoke;
        auto parent = invoke->GetParentBlock();

        // get this type from rewrite info
        Type* thisType = builder.GetType<RefType>(rewriteInfo->thisType);
        auto& realFunc = rewriteInfo->realCallee;

        if ((rewriteInfo->thisType->IsStruct() || rewriteInfo->thisType->IsEnum()) &&
            rewriteInfo->realCallee->Get<WrappedRawMethod>()) {
            thisType = rewriteInfo->thisType;
            bool isMutFunc =
                realFunc->TestAttr(Attribute::MUT) || IsConstructor(*realFunc) || IsInstanceVarInit(*realFunc);
            if (isMutFunc) {
                thisType = builder.GetType<RefType>(thisType);
            }
            realFunc = rewriteInfo->realCallee->Get<WrappedRawMethod>();
        }
        auto args = invoke->GetOperands();
        auto instThisType = thisType;
        if (rewriteInfo->thisType->IsBuiltinType()) {
            instThisType = builder.GetType<RefType>(builder.GetAnyTy());
        }
        auto instRetTy = invoke->GetResultType();
        auto typecastRes = TypeCastOrBoxIfNeeded(*args[0], *instThisType, builder, *parent, INVALID_LOCATION);
        if (typecastRes != args[0]) {
            StaticCast<LocalVar*>(typecastRes)->GetExpr()->MoveBefore(invoke);
            args[0] = typecastRes;
        }
        auto loc = invoke->GetDebugLocation();
        auto apply = builder.CreateExpression<Apply>(loc, instRetTy, realFunc, FuncCallContext{
            .args = args,
            .instTypeArgs = rewriteInfo->typeArgs,
            .thisType = thisType}, invoke->GetParentBlock());
        rewriteInfo->newApply = apply;
        invoke->ReplaceWith(*apply);
        invoke->GetResult()->ReplaceWith(*apply->GetResult(), parent->GetParentBlockGroup());
        if (isDebug) {
            std::string message = "[Devirtualization] The function call to " + invoke->GetMethodName() +
                ToPosInfo(invoke->GetDebugLocation()) + " was optimized.";
            std::cout << message << std::endl;
        }
    }
}

const std::vector<Func*>& Devirtualization::GetFrozenInstFuns() const
{
    return frozenInstFuns;
}

void Devirtualization::AppendFrozenFuncState(const Func* func, std::unique_ptr<Results<TypeDomain>> analysisRes)
{
    frozenStates.emplace(func, std::move(analysisRes));
}

static std::string CreateInstFuncMangleName(const std::string& oriIdentifer, const Apply& apply, CHIRBuilder& builder)
{
    // 1. get type args
    std::vector<Type*> genericTypes;
    auto func = VirtualCast<FuncBase*>(apply.GetCallee());
    if (auto customDef = func->GetParentCustomTypeDef(); customDef != nullptr && customDef->IsGenericDef()) {
        auto funcInCustomType = apply.GetInstParentCustomTyOfCallee(builder);
        while (funcInCustomType->IsRef()) {
            funcInCustomType = StaticCast<RefType*>(funcInCustomType)->GetBaseType();
        }
        genericTypes = funcInCustomType->GetTypeArgs();
    }
    auto funcArgs = apply.GetInstantiatedTypeArgs();
    if (!funcArgs.empty()) {
        genericTypes.insert(genericTypes.end(), funcArgs.begin(), funcArgs.end());
    }
    // 2. get mangle
    return CHIRMangling::GenerateInstantiateFuncMangleName(oriIdentifer, genericTypes);
}

void Devirtualization::InstantiateFuncIfPossible(CHIRBuilder& builder, std::vector<RewriteInfo>& rewriteInfoList)
{
    for (auto rewriteInfo = rewriteInfoList.rbegin(); rewriteInfo != rewriteInfoList.rend(); ++rewriteInfo) {
        auto callee = DynamicCast<Func*>(rewriteInfo->realCallee);
        if (callee == nullptr || !callee->IsInGenericContext() || callee->Get<WrappedRawMethod>() != nullptr) {
            continue;
        }
        auto apply = rewriteInfo->newApply;
        std::vector<Type*> parameterType;
        for (auto param : apply->GetArgs()) {
            parameterType.emplace_back(param->GetType());
        }
        auto retType = apply->GetResultType();
        auto instFuncType = builder.GetType<FuncType>(parameterType, retType);

        if (instFuncType->IsGenericRelated()) {
            continue;
        }
        // 2. create new inst func if needed
        auto newId = CreateInstFuncMangleName(callee->GetIdentifierWithoutPrefix(), *apply, builder);
        Func* newFunc;
        if (frozenInstFuncMap.count(newId) != 0) {
            newFunc = frozenInstFuncMap.at(newId);
        } else {
            newFunc = builder.CreateFunc(callee->GetDebugLocation(), instFuncType, newId,
                callee->GetSrcCodeIdentifier(), callee->GetRawMangledName(), callee->GetPackageName());
            
            newFunc->AppendAttributeInfo(callee->GetAttributeInfo());
            newFunc->DisableAttr(Attribute::GENERIC);
            if (!apply->GetInstantiatedTypeArgs().empty()) {
                newFunc->EnableAttr(Attribute::GENERIC_INSTANTIATED);
            }
            newFunc->Set<LinkTypeInfo>(Linkage::INTERNAL);

            auto oriBlockGroup = callee->GetBody();
            BlockGroupCopyHelper helper(builder);
            helper.GetInstMapFromApply(*apply);
            auto [newGroup, newBlockGroupRetValue] = helper.CloneBlockGroup(*oriBlockGroup, *newFunc);
            newFunc->InitBody(*newGroup);
            newFunc->SetReturnValue(*newBlockGroupRetValue);

            std::vector<Value*> args;
            CJC_ASSERT(parameterType.size() == callee->GetParams().size());
            std::unordered_map<Value*, Value*> paramMap;
            for (size_t i = 0; i < parameterType.size(); i++) {
                auto arg = builder.CreateParameter(parameterType[i], callee->GetParam(i)->GetDebugLocation(), *newFunc);
                args.push_back(arg);
                paramMap.emplace(callee->GetParam(i), arg);
            }
            helper.SubstituteValue(newGroup, paramMap);

            FixCastProblemAfterInst(newGroup, builder);
            newFunc->SetReturnValue(*newBlockGroupRetValue);
            frozenInstFuns.push_back(newFunc);
            frozenInstFuncMap[newId] = newFunc;
        }
        // replace apply callee with new inst func
        auto applyParent = apply->GetParentBlock();
        auto loc = apply->GetDebugLocation();
        auto instApply = builder.CreateExpression<Apply>(
            loc, retType, newFunc, FuncCallContext{.args = apply->GetArgs()}, applyParent);
        apply->ReplaceWith(*instApply);
    }
}

namespace {
void BuildOrphanTypeReplaceTable(
    const Cangjie::CHIR::Type* mayBeGeneric, const std::unordered_map<const GenericType*, Type*>& replaceTable)
{
    auto genericTypeArgs = mayBeGeneric->GetTypeArgs();
    for (size_t i = 0; i < genericTypeArgs.size(); ++i) {
        BuildOrphanTypeReplaceTable(genericTypeArgs[i], replaceTable);
    }
}

FuncBase* FindFunctionInVtable(const ClassType* parentTy, const std::vector<VirtualFuncInfo>& infos,
    const Devirtualization::FuncSig& method, CHIRBuilder& builder)
{
    std::unordered_map<const GenericType*, Type*> parentReplaceTable;
    auto paramTypes = method.types;
    paramTypes.erase(paramTypes.begin());
    if (!parentTy->GetTypeArgs().empty()) {
        auto instParentTypeArgs = parentTy->GetTypeArgs();
        auto genericParentTypeArgs = parentTy->GetCustomTypeDef()->GetGenericTypeParams();
        for (size_t i = 0; i < genericParentTypeArgs.size(); ++i) {
            parentReplaceTable.emplace(genericParentTypeArgs[i], instParentTypeArgs[i]);
        }
    }

    for (auto& info : infos) {
        if (info.srcCodeIdentifier != method.name) {
            continue;
        }
        auto sigParamTys = info.typeInfo.sigType->GetParamTypes();
        if (sigParamTys.size() != paramTypes.size()) {
            continue;
        }
        if (info.typeInfo.methodGenericTypeParams.size() != method.typeArgs.size()) {
            continue;
        }
        bool isSigSame = true;
        std::unordered_map<const GenericType*, Type*> freeGenericReplaceTable;
        for (size_t i = 0; i < sigParamTys.size(); ++i) {
            BuildOrphanTypeReplaceTable(sigParamTys[i], freeGenericReplaceTable);
            BuildOrphanTypeReplaceTable(paramTypes[i], freeGenericReplaceTable);
        }
        auto& methodGenerics = info.typeInfo.methodGenericTypeParams;
        for (size_t i{0}; i < methodGenerics.size(); ++i) {
            freeGenericReplaceTable.emplace(methodGenerics[i], method.typeArgs[i]);
        }
        for (size_t i = 0; i < paramTypes.size(); ++i) {
            if (isSigSame) {
                auto lhs = ReplaceRawGenericArgType(*sigParamTys[i], freeGenericReplaceTable, builder);
                auto rhs = ReplaceRawGenericArgType(*paramTypes[i], parentReplaceTable, builder);
                rhs = ReplaceRawGenericArgType(*rhs, freeGenericReplaceTable, builder);
                isSigSame = lhs == rhs;
            } else {
                break;
            }
        }
        if (!isSigSame) {
            continue;
        }
        return info.instance;
    }
    return nullptr;
}
} // namespace

bool Devirtualization::IsInstantiationOf(
    CHIRBuilder& builder, const GenericType* generic, const Type* instantiated) const
{
    if (generic->GetUpperBounds().empty()) {
        return true;
    }
    std::unordered_set<Type*> possibleParentTys;
    for (auto def : devirtFuncInfo.defsMap[instantiated]) {
        for (auto parentTy : def->GetSuperTypesInCurDef()) {
            auto inheritLists = parentTy->GetSuperTypesRecusively(builder);
            possibleParentTys.insert(inheritLists.begin(), inheritLists.end());
        }
    }
    for (auto upperBound : generic->GetUpperBounds()) {
        if (possibleParentTys.find(upperBound) == possibleParentTys.end()) {
            return false;
        }
    }

    return true;
}

bool Devirtualization::IsValidSubType(CHIRBuilder& builder, const Type* expected, Type* specific,
    std::unordered_map<const GenericType*, Type*>& replaceTable) const
{
    if (expected->GetTypeKind() != specific->GetTypeKind() && !expected->IsGeneric()) {
        return false;
    }
    if (expected->IsGeneric()) {
        auto generic = Cangjie::StaticCast<const GenericType*>(expected);
        if (IsInstantiationOf(builder, generic, specific)) {
            replaceTable.emplace(generic, specific);
            return true;
        }
        return false;
    }
    if (expected->IsNominal()) {
        const CustomType* specificCustomTy = Cangjie::StaticCast<const CustomType*>(specific);
        if (!specificCustomTy->IsEqualOrSubTypeOf(*expected, builder)) {
            return false;
        }
    }
    auto argsOfExpected = expected->GetTypeArgs();
    auto argsOfSpecific = specific->GetTypeArgs();
    if (argsOfExpected.size() != argsOfSpecific.size()) {
        return false;
    }
    for (size_t i = 0; i < argsOfExpected.size(); ++i) {
        if (!IsValidSubType(
            builder, argsOfExpected[i]->StripAllRefs(), argsOfSpecific[i]->StripAllRefs(), replaceTable)) {
            return false;
        }
    }
    return true;
}

std::pair<FuncBase*, Type*> Devirtualization::FindRealCallee(
    CHIRBuilder& builder, const TypeValue* typeState, const FuncSig& method) const
{
    auto typeStateKind = typeState->GetTypeKind();
    auto specificType = typeState->GetSpecificType();
    if (!specificType->IsClass() || typeStateKind == DevirtualTyKind::EXACTLY) {
        std::vector<CustomTypeDef*> extendsOrImplements{};
        if (auto customType = DynamicCast<const CustomType*>(specificType)) {
            extendsOrImplements = devirtFuncInfo.defsMap[customType->GetCustomTypeDef()->GetType()];
        } else {
            extendsOrImplements = devirtFuncInfo.defsMap[specificType];
        }

        FuncBase* target = nullptr;
        for (auto def : extendsOrImplements) {
            auto [typeMatched, replaceTable] = def->GetType()->CalculateGenericTyMapping(*specificType);
            if (!typeMatched) {
                continue;
            }
            auto funcType = builder.GetType<FuncType>(method.types, builder.GetUnitTy());
            FuncCallType funcCallType{method.name, funcType, method.typeArgs};
            auto res = def->GetFuncIndexInVTable(funcCallType, false, replaceTable, builder);
            if (!res.empty() && res[0].instance != nullptr) {
                target = res[0].instance;
                break;
            }
        }
        CJC_NULLPTR_CHECK(target);
        return {target, specificType};
    } else {
        // The specific type is an interface or a class, and the state kind is SUBCLASS_OF.
        ClassType* specificType1 = StaticCast<ClassType*>(typeState->GetSpecificType());
        std::pair<FuncBase*, Type*> res{nullptr, nullptr};
        CollectCandidates(builder, specificType1, res, method);
        return res;
    }
}

static bool IsOpenClass(const ClassDef& def)
{
    if (def.IsInterface() || def.IsAbstract()) {
        return true;
    }
    return def.TestAttr(Attribute::VIRTUAL);
}

FuncBase* Devirtualization::GetCandidateFromSpecificType(
    CHIRBuilder& builder, ClassType& specific, const FuncSig& method) const
{
    auto specificDef = specific.GetClassDef();
    if (specificDef->IsAbstract() || specificDef->IsInterface()) {
        return nullptr;
    }
    auto customType = specific.GetClassDef()->GetType();
    auto extendsOrImplements = devirtFuncInfo.defsMap[customType];
    for (auto oriDef : extendsOrImplements) {
        auto genericDef = oriDef->GetGenericDecl() != nullptr ? oriDef->GetGenericDecl() : oriDef;
        for (auto [parentTy, infos] : genericDef->GetVTable()) {
            if (auto target = FindFunctionInVtable(parentTy, infos, method, builder)) {
                return target;
            }
        }
    }
    return nullptr;
}

void Devirtualization::CollectCandidates(
    CHIRBuilder& builder, ClassType* specific, std::pair<FuncBase*, Type*>& res, const FuncSig& method) const
{
    auto specificDef = specific->GetClassDef();
    if (IsOpenClass(*specificDef) && !devirtFuncInfo.CheckCustomTypeInternal(*specificDef)) {
        // skip open classes with external linkage
        return;
    }
    // 1. Get candidate from this type
    auto targetFromSpecificType = GetCandidateFromSpecificType(builder, *specific, method);
    if (targetFromSpecificType != nullptr) {
        res = {targetFromSpecificType, specific};
    }
    if (!IsOpenClass(*specificDef)) {
        // non-open class do not need try its subtype
        return;
    }
    auto& subtypeMap = devirtFuncInfo.GetSubtypeMap();
    auto it = subtypeMap.find(specificDef);
    if (it == subtypeMap.end()) {
        // return if has no subtype
        return;
    }
    // 2. Get candidate from subtypes
    for (auto& inheritInfo : it->second) {
        auto expected = inheritInfo.parentInstType;
        std::unordered_map<const GenericType*, Type*> replaceTable;
        if (!IsValidSubType(builder, expected, specific, replaceTable)) {
            continue;
        }
        auto subtype = ReplaceRawGenericArgType(*(inheritInfo.subInstType), replaceTable, builder);
        auto subtypeClass = DynamicCast<ClassType*>(subtype);
        if (!subtypeClass ||
            (!subtypeClass->GetClassDef()->IsInterface() &&
                !subtypeClass->GetClassDef()->TestAttr(Attribute::ABSTRACT))) {
            auto extendsOrImplements = devirtFuncInfo.defsMap[subtypeClass];
            for (auto oriDef : extendsOrImplements) {
                auto def = oriDef->GetGenericDecl() != nullptr ? oriDef->GetGenericDecl() : oriDef;
                for (auto [parentTy, infos] : def->GetVTable()) {
                    if (!expected->IsEqualOrSubTypeOf(*parentTy->StripAllRefs(), builder)) {
                        continue;
                    }
                    if (auto target = FindFunctionInVtable(parentTy, infos, method, builder)) {
                        if (res.first == nullptr) {
                            res = {target, subtypeClass};
                        } else if (res.first != target) {
                            res = {nullptr, nullptr};
                            return;
                        }
                    }
                }
            }
        }
        if (subtypeClass) {
            CollectCandidates(builder, subtypeClass, res, method);
        }
    }
}

bool Devirtualization::CheckFuncHasInvoke(const BlockGroup& bg)
{
    std::vector<Block*> blocks = bg.GetBlocks();
    for (auto bb : blocks) {
        auto exprs = bb->GetNonTerminatorExpressions();
        for (size_t i = 0; i < exprs.size(); ++i) {
            if (exprs[i]->GetExprKind() == ExprKind::LAMBDA) {
                if (CheckFuncHasInvoke(*StaticCast<Lambda*>(exprs[i])->GetBody())) {
                    return true;
                }
            }
            if (exprs[i]->GetExprKind() == ExprKind::INVOKE) {
                return true;
            }
        }
    }
    return false;
}

std::vector<Func*> Devirtualization::CollectContainInvokeExprFuncs(const Ptr<const Package>& package)
{
    std::vector<Func*> funcs;
    // Collect functions that contain the invoke statement.
    for (auto func : package->GetGlobalFuncs()) {
        if (CheckFuncHasInvoke(*func->GetBody())) {
            funcs.emplace_back(func);
        }
    }
    return funcs;
}
}
