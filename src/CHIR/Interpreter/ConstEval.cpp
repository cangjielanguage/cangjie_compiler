// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements a translation from CHIR to BCHIR for atomic operations.
 */

#include <numeric>

#include <cangjie/CHIR/Interpreter/ConstEval.h>

#include <cangjie/CHIR/Interpreter/BCHIR.h>
#include <cangjie/CHIR/Interpreter/BCHIRInterpreter.h>
#include <cangjie/CHIR/Interpreter/BCHIRLinker.h>
#include <cangjie/CHIR/Interpreter/CHIR2BCHIR.h>
#include <cangjie/CHIR/IR/Value/LiteralValue.h>
#include <cangjie/CHIR/IR/Type/ClassDef.h>
#include <cangjie/CHIR/IR/Type/EnumDef.h>
#include <cangjie/Utils/ProfileRecorder.h>

using Cangjie::CHIR::Interpreter::ConstEvalPass;
using Cangjie::CHIR::Interpreter::IVal2CHIR;

namespace {
// A value decoded from a foreign cjo can carry a representation that does not
// match the type kind recorded next to it; such a value is not constant-foldable
// here, so every extraction is checked and yields null instead of an unchecked
// std::get abort.

// Builds an integer constant from the matching primitive content; the narrowed
// cast keeps the zero/sign-extension the type kind defines.
template <class IType, typename Narrowed>
Cangjie::CHIR::Constant* TryIntLiteral(Cangjie::CHIR::CHIRBuilder& builder, Cangjie::CHIR::Type& ty,
    Cangjie::CHIR::Block& parent, const Cangjie::CHIR::Interpreter::IVal& val)
{
    const auto* value = Cangjie::CHIR::Interpreter::IValUtils::GetIf<IType>(&val);
    if (value == nullptr) {
        return nullptr;
    }
    return builder.CreateConstantExpression<Cangjie::CHIR::IntLiteral>(
        &ty, &parent, static_cast<Narrowed>(value->content));
}

// Builds a non-integer primitive constant (float, rune or boolean) from the
// matching primitive content.
template <class IType, class Literal>
Cangjie::CHIR::Constant* TryValueLiteral(Cangjie::CHIR::CHIRBuilder& builder, Cangjie::CHIR::Type& ty,
    Cangjie::CHIR::Block& parent, const Cangjie::CHIR::Interpreter::IVal& val)
{
    const auto* value = Cangjie::CHIR::Interpreter::IValUtils::GetIf<IType>(&val);
    if (value == nullptr) {
        return nullptr;
    }
    return builder.CreateConstantExpression<Literal>(&ty, &parent, value->content);
}
} // namespace

Cangjie::CHIR::Constant* IVal2CHIR::TryConvertToConstant(Type& ty, const IVal& val, Block& parent)
{
    switch (ty.GetTypeKind()) {
        case Type::TYPE_INT8:
            return TryIntLiteral<IInt8, uint8_t>(chirBuilder, ty, parent, val);
        case Type::TYPE_INT16:
            return TryIntLiteral<IInt16, uint16_t>(chirBuilder, ty, parent, val);
        case Type::TYPE_INT32:
            return TryIntLiteral<IInt32, uint32_t>(chirBuilder, ty, parent, val);
        case Type::TYPE_INT64:
            return TryIntLiteral<IInt64, uint64_t>(chirBuilder, ty, parent, val);
        case Type::TYPE_INT_NATIVE:
            return TryIntLiteral<IIntNat, uint64_t>(chirBuilder, ty, parent, val);
        case Type::TYPE_UINT8:
            return TryIntLiteral<IUInt8, uint8_t>(chirBuilder, ty, parent, val);
        case Type::TYPE_UINT16:
            return TryIntLiteral<IUInt16, uint16_t>(chirBuilder, ty, parent, val);
        case Type::TYPE_UINT32:
            return TryIntLiteral<IUInt32, uint32_t>(chirBuilder, ty, parent, val);
        case Type::TYPE_UINT64:
            return TryIntLiteral<IUInt64, uint64_t>(chirBuilder, ty, parent, val);
        case Type::TYPE_UINT_NATIVE:
            return TryIntLiteral<IUIntNat, uint64_t>(chirBuilder, ty, parent, val);
        case Type::TYPE_FLOAT16:
            return TryValueLiteral<IFloat16, FloatLiteral>(chirBuilder, ty, parent, val);
        case Type::TYPE_FLOAT32:
            return TryValueLiteral<IFloat32, FloatLiteral>(chirBuilder, ty, parent, val);
        case Type::TYPE_FLOAT64:
            return TryValueLiteral<IFloat64, FloatLiteral>(chirBuilder, ty, parent, val);
        case Type::TYPE_RUNE:
            return TryValueLiteral<IRune, RuneLiteral>(chirBuilder, ty, parent, val);
        case Type::TYPE_BOOLEAN:
            return TryValueLiteral<IBool, BoolLiteral>(chirBuilder, ty, parent, val);
        default:
            if (IValUtils::GetIf<INullptr>(&val)) {
                return chirBuilder.CreateConstantExpression<NullLiteral>(&ty, &parent);
            }
            return nullptr;
    }
}

Cangjie::CHIR::Value* IVal2CHIR::ConvertToChir(
    Type& ty, const IVal& val, std::function<void(Expression*)>& insertExpr, Block& parent)
{
    auto constant = TryConvertToConstant(ty, val, parent);
    if (constant) {
        insertExpr(constant);
        return constant->GetResult();
    }
    return ConvertNonConstantToChir(ty, val, insertExpr, parent);
}

// Dispatch for the kinds that carry a constant representation beyond the plain
// literals of TryConvertToConstant. Kinds without one return nullptr.
Cangjie::CHIR::Value* IVal2CHIR::ConvertNonConstantToChir(
    Type& ty, const IVal& val, std::function<void(Expression*)>& insertExpr, Block& parent)
{
    switch (ty.GetTypeKind()) {
        case Type::TYPE_TUPLE:
        case Type::TYPE_STRUCT: {
            // A value from a cjo written by an older compiler can carry a representation this compiler does not
            // model (for example the array-backed String of an older standard library). An unchecked access aborts
            // with std::bad_variant_access; such a value is simply not constant-foldable here.
            auto tuplePtr = IValUtils::GetIf<ITuple>(&val);
            if (tuplePtr == nullptr) {
                return nullptr;
            }
            auto& tuple = *tuplePtr;
            if (ty.IsString()) {
                // Codegen doesn't support strings in constant initializers, so
                // we have strings here and not in TryConvertToConstant.
                return ConvertStringToChir(ty, tuple, insertExpr, parent);
            }
            return ConvertTupleToChir(ty, tuple, insertExpr, parent);
        }
        case Type::TYPE_ENUM: {
            return ConvertEnumToChir(StaticCast<EnumType&>(ty), val, insertExpr, parent);
        }
        case Type::TYPE_REFTYPE: {
            return ConvertRefToChir(StaticCast<RefType&>(ty), val, insertExpr, parent);
        }
        case Type::TYPE_UNIT: {
            // Unit is not supported as a global variable initializer
            auto expr = chirBuilder.CreateConstantExpression<UnitLiteral>(&ty, &parent);
            insertExpr(expr);
            return expr->GetResult();
        }
        case Type::TYPE_VARRAY: {
            auto arrayVal = IValUtils::GetIf<IArray>(&val);
            if (arrayVal == nullptr) {
                return nullptr;
            }
            return ConvertArrayToChir(StaticCast<VArrayType&>(ty), *arrayVal, insertExpr, parent);
        }
        default:
            return ConvertUnfoldablePrimitiveToChir(ty);
    }
}

// Primitive types are handled by TryConvertToConstant. A value whose
// representation does not match the type was rejected there, so it is
// not foldable; it is not an unsupported type kind.
Cangjie::CHIR::Value* IVal2CHIR::ConvertUnfoldablePrimitiveToChir(Type& ty)
{
    switch (ty.GetTypeKind()) {
        case Type::TYPE_INT8:
        case Type::TYPE_INT16:
        case Type::TYPE_INT32:
        case Type::TYPE_INT64:
        case Type::TYPE_INT_NATIVE:
        case Type::TYPE_UINT8:
        case Type::TYPE_UINT16:
        case Type::TYPE_UINT32:
        case Type::TYPE_UINT64:
        case Type::TYPE_UINT_NATIVE:
        case Type::TYPE_FLOAT16:
        case Type::TYPE_FLOAT32:
        case Type::TYPE_FLOAT64:
        case Type::TYPE_RUNE:
        case Type::TYPE_BOOLEAN:
            return nullptr;
        default:
            return ConvertUnfoldableOtherToChir(ty);
    }
}

Cangjie::CHIR::Value* IVal2CHIR::ConvertUnfoldableOtherToChir(Type& ty)
{
    switch (ty.GetTypeKind()) {
        // Should always be behind RefType.
        case Type::TYPE_CLASS:
        // Not supported as constants.
        case Type::TYPE_FUNC:
        case Type::TYPE_GENERIC:
        case Type::TYPE_NOTHING:
        case Type::TYPE_RAWARRAY:
        case Type::TYPE_CPOINTER:
        case Type::TYPE_CSTRING:
        case Type::TYPE_VOID:
        case Type::TYPE_INVALID:
            return nullptr;
        default:
            Cangjie::InternalError("unsupported type kind");
            return nullptr;
    }
}

Cangjie::CHIR::Value* IVal2CHIR::ConvertStringToChir(
    Type& ty, const ITuple& val, const std::function<void(Expression*)>& insertExpr, Block& parent)
{
    std::string stringVal;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    // The String layout assumed below is the one this compiler emits. A value decoded from a cjo written by an older
    // compiler can keep String data in a shape this compiler does not model (an object rather than an array);
    // report that the value is not foldable instead of aborting on std::bad_variant_access.
    // The translated String layout is [array, start, length]; fewer fields
    // mean the cjo predates this layout and the value is not foldable.
    constexpr size_t STRING_LAYOUT_FIELD_COUNT = 3;
    if (val.content.size() < STRING_LAYOUT_FIELD_COUNT) {
        return nullptr;
    }
    auto arrPtr = IValUtils::GetIf<IPointer>(&val.content[0]);
    auto startPtr = IValUtils::GetIf<IUInt32>(&val.content[1]);
    auto lenPtr = IValUtils::GetIf<IUInt32>(&val.content[2]);
    if (arrPtr == nullptr || arrPtr->content == nullptr || startPtr == nullptr || lenPtr == nullptr) {
        return nullptr;
    }
    auto arrDataPtr = IValUtils::GetIf<IArray>(arrPtr->content);
    if (arrDataPtr == nullptr) {
        return nullptr;
    }
    auto& arrData = arrDataPtr->content;

    auto start = startPtr->content;
    if (start != static_cast<uint32_t>(0)) {
        return nullptr;
    }

    auto len = lenPtr->content;

    if (arrData.size() <= static_cast<size_t>(len)) {
        return nullptr;
    }

    stringVal.reserve(len);

    for (uint32_t el = 0; el < len; el++) {
        auto byte = IValUtils::GetIf<IUInt8>(&arrData[el + 1]);
        if (byte == nullptr) {
            return nullptr;
        }
        stringVal.push_back(static_cast<char>(byte->content));
    }
#endif

    auto expr = chirBuilder.CreateConstantExpression<StringLiteral>(&ty, &parent, stringVal);
    insertExpr(expr);
    return expr->GetResult();
}

Cangjie::CHIR::Value* IVal2CHIR::ConvertTupleToChir(
    Type& ty, const ITuple& val, std::function<void(Expression*)>& insertExpr, Block& parent)
{
    std::vector<Type*> elementTys;
    if (ty.GetTypeKind() == Type::TYPE_STRUCT) {
        elementTys = StaticCast<StructType&>(ty).GetInstantiatedMemberTys(chirBuilder);
    } else {
        elementTys = ty.GetTypeArgs();
    }
    auto elementVals = val.content;
    std::vector<Value*> elements;
    CJC_ASSERT(elementVals.size() == elementTys.size());
    for (std::size_t idx = 0; idx < elementVals.size(); idx++) {
        auto elementVal = ConvertToChir(*elementTys[idx], elementVals[idx], insertExpr, parent);
        if (!elementVal) {
            return nullptr;
        }
        elements.push_back(elementVal);
    }
    if (ty.IsStruct()) {
        // Codegen doesn't support the use of tuples for structs, so we need to allocate the struct, and assign its
        // fields
        auto refTy = chirBuilder.GetType<RefType>(&ty);
        auto allocate = chirBuilder.CreateExpression<Allocate>(refTy, &ty, &parent);
        insertExpr(allocate);
        for (size_t i = 0; i < elements.size(); ++i) {
            auto store = chirBuilder.CreateExpression<StoreElementRef>(
                chirBuilder.GetUnitTy(), elements[i], allocate->GetResult(), std::vector<std::uint64_t>{i}, &parent);
            insertExpr(store);
        }
        auto load = chirBuilder.CreateExpression<Load>(&ty, allocate->GetResult(), &parent);
        insertExpr(load);
        return load->GetResult();
    }
    auto expr = chirBuilder.CreateExpression<Tuple>(&ty, elements, &parent);
    insertExpr(expr);
    return expr->GetResult();
}

// An all-trivial enum value carries only the selector: it maps to one integer
// constant cast to the enum type. A non-matching value representation is not
// foldable.
Cangjie::CHIR::Value* IVal2CHIR::ConvertTrivialEnumToChir(EnumType& ty, Type* selectorTy, Block& parent,
    const IVal& val, std::function<void(Expression*)>& insertExpr)
{
    auto selectorVal = IValUtils::GetIf<IUInt32>(&val);
    if (selectorVal == nullptr) {
        return nullptr;
    }
    auto expr = chirBuilder.CreateConstantExpression<IntLiteral>(selectorTy, &parent, selectorVal->content);
    auto ret = chirBuilder.CreateExpression<ClassStaticCast>(&ty, expr->GetResult(), &parent);
    insertExpr(expr);
    insertExpr(ret);
    return ret->GetResult();
}

// A payload enum value decodes to one tuple: the selector element names the
// constructor variant, every other element maps one constructor parameter
// type. Any shape mismatch or non-matching element representation is not
// foldable.
Cangjie::CHIR::Value* IVal2CHIR::ConvertPayloadEnumToChir(EnumType& ty, Type* selectorTy, Block& parent,
    const IVal& val, std::function<void(Expression*)>& insertExpr)
{
    auto tupleVal = IValUtils::GetIf<ITuple>(&val);
    if (tupleVal == nullptr || tupleVal->content.empty()) {
        return nullptr;
    }
    auto& elementVals = tupleVal->content;
    uint64_t variantIndex = 0;
    if (selectorTy->IsBoolean()) {
        auto flagVal = IValUtils::GetIf<IBool>(&elementVals[0]);
        if (flagVal == nullptr) {
            return nullptr;
        }
        variantIndex = static_cast<uint64_t>(flagVal->content);
    } else {
        auto indexVal = IValUtils::GetIf<IUInt32>(&elementVals[0]);
        if (indexVal == nullptr) {
            return nullptr;
        }
        variantIndex = indexVal->content;
    }
    const auto ctorInfos = ty.GetConstructorInfos(chirBuilder);
    if (variantIndex >= ctorInfos.size() || ctorInfos[variantIndex].funcType == nullptr) {
        return nullptr;
    }
    auto selectorChir = ConvertToChir(*selectorTy, elementVals[0], insertExpr, parent);
    if (selectorChir == nullptr) {
        return nullptr;
    }
    std::vector<Value*> elements;
    elements.push_back(selectorChir);
    auto elementTys = ctorInfos[variantIndex].funcType->GetParamTypes();
    if (elementVals.size() != elementTys.size() + 1) {
        return nullptr;
    }
    for (std::size_t idx = 0; idx < elementTys.size(); idx++) {
        auto elementVal = ConvertToChir(*elementTys[idx], elementVals[idx + 1], insertExpr, parent);
        if (!elementVal) {
            return nullptr;
        }
        elements.push_back(elementVal);
    }
    auto expr = chirBuilder.CreateExpression<Tuple>(&ty, elements, &parent);
    insertExpr(expr);
    return expr->GetResult();
}

Cangjie::CHIR::Value* IVal2CHIR::ConvertEnumToChir(
    EnumType& ty, const IVal& val, std::function<void(Expression*)>& insertExpr, Block& parent)
{
    auto selectorTyKind = GetSelectorType(*ty.GetEnumDef());
    auto selectorTy = chirBuilder.GetChirContext().ToSelectorType(selectorTyKind);
    if (ty.GetEnumDef()->IsAllCtorsTrivial()) {
        return ConvertTrivialEnumToChir(ty, selectorTy, parent, val, insertExpr);
    }
    return ConvertPayloadEnumToChir(ty, selectorTy, parent, val, insertExpr);
}

// Constant of a class reference: allocate the dynamic class value named by
// the stored object, store every converted member into it and cast back to
// the reference's declared type when the dynamic type differs. Returns
// nullptr for a non-decodable object or an unresolvable class.
Cangjie::CHIR::Value* IVal2CHIR::ConvertClassRefToChir(
    RefType& ty, const IVal& valContent, std::function<void(Expression*)>& insertExpr, Block& parent)
{
    auto referencedType = ty.GetBaseType();
    auto obj = IValUtils::GetIf<IObject>(&valContent);
    if (obj == nullptr) {
        return nullptr;
    }
    auto classIt = bchir.GetClassTable().find(obj->classId);
    if (classIt == bchir.GetClassTable().end()) {
        return nullptr;
    }

    // This is only partially implemeneted:
    // If a constant references another constant, then it should not create a deep copy of that constant.
    auto classType = StaticCast<ClassType*>(referencedType);
    auto& valClassName = classIt->second.mangledName;
    auto refType = &ty;
    auto needCast = false;

    if (valClassName != classType->GetClassDef()->GetIdentifierWithoutPrefix()) {
        needCast = true;
        classType = FindClassType(valClassName);
        if (classType == nullptr) {
            return nullptr;
        }
        refType = chirBuilder.GetType<RefType>(classType);
    }

    auto allocExpr = chirBuilder.CreateExpression<Allocate>(refType, classType, &parent);
    insertExpr(allocExpr);
    auto classVal = allocExpr->GetResult();
    auto members = classType->GetClassDef()->GetAllInstanceVars();
    if (members.size() != obj->content.size()) {
        return nullptr;
    }
    for (std::uint64_t idx = 0; idx < members.size(); idx++) {
        auto memberTy = members[idx].type;
        auto memberVal = ConvertToChir(*memberTy, obj->content[idx], insertExpr, parent);
        if (!memberVal) {
            return nullptr;
        }
        insertExpr(chirBuilder.CreateExpression<StoreElementRef>(
            chirBuilder.GetUnitTy(), memberVal, classVal, std::vector<std::uint64_t>{idx}, &parent));
    }
    if (needCast) {
        auto typeCast = chirBuilder.CreateExpression<ClassStaticCast>(&ty, classVal, &parent);
        insertExpr(typeCast);
        return typeCast->GetResult();
    }
    return classVal;
}

Cangjie::CHIR::Value* IVal2CHIR::ConvertRefToChir(
    RefType& ty, const IVal& val, std::function<void(Expression*)>& insertExpr, Block& parent)
{
    auto referencedType = ty.GetBaseType();

    const auto* pointer = IValUtils::GetIf<IPointer>(&val);
    if (pointer == nullptr) {
        return nullptr;
    }
    switch (referencedType->GetTypeKind()) {
        case Type::TYPE_CLASS: {
            return ConvertClassRefToChir(ty, *pointer->content, insertExpr, parent);
        }
        default: {
            // Other types not supported by const eval
            return nullptr;
        }
    }
}

Cangjie::CHIR::ClassType* IVal2CHIR::FindClassType(const std::string& mangledName)
{
    // Object was upcast, try to locate the dynamic type.
    auto classes = package.GetClasses();
    auto result = std::find_if(classes.begin(), classes.end(),
        [&mangledName](const ClassDef* classDef) { return mangledName == classDef->GetIdentifierWithoutPrefix(); });
    ClassDef* resultClassDef;
    if (result != classes.end()) {
        resultClassDef = *result;
    } else {
        auto imports = package.GetImportedClasses();
        auto importResult = std::find_if(imports.begin(), imports.end(),
            [&mangledName](const ClassDef* classDef) { return mangledName == classDef->GetIdentifierWithoutPrefix(); });
        if (importResult == imports.end()) {
            return nullptr;
        }
        resultClassDef = *importResult;
    }
    if (resultClassDef->GetGenericTypeParams().size() != 0) {
        return nullptr;
    }
    return chirBuilder.GetType<ClassType>(resultClassDef);
}

Cangjie::CHIR::Value* IVal2CHIR::ConvertArrayToChir(
    VArrayType& ty, const IArray& val, std::function<void(Expression*)>& insertExpr, Block& parent)
{
    auto& elementVals = val.content;
    std::vector<Value*> elements;
    elements.reserve(elementVals.size());
    auto elementTy = ty.GetElementType();
    for (std::size_t idx = 0; idx < elementVals.size(); idx++) {
        auto elementVal = ConvertToChir(*elementTy, elementVals[idx], insertExpr, parent);
        if (!elementVal) {
            return nullptr;
        }
        elements.push_back(elementVal);
    }
    auto expr = chirBuilder.CreateExpression<VArray>(&ty, elements, &parent);
    insertExpr(expr);
    return expr->GetResult();
}

void ConstEvalPass::RunOnPackage(Package& package,
    const std::vector<CHIR::Function*>& initFuncsForConstVar, std::vector<Bchir>& bchirPackages)
{
    RunInterpreter(package, bchirPackages, initFuncsForConstVar,
        [this](auto& package, auto& interpreter, auto& linker) {
        ReplaceGlobalConstantInitializers(package, interpreter, linker);
    });
}

void ConstEvalPass::RunInterpreter(Package& package, std::vector<Bchir>& bchirPackages,
    const std::vector<CHIR::Function*>& initFuncsForConstVar,
    std::function<void(Package&, BCHIRInterpreter&, BCHIRLinker&)> onSuccess)
{
    Utils::ProfileRecorder::Start("Constant Evaluation", "CHIR2BCHIR for const-eval");
    auto printBchir = opts.PrintBchir(GlobalOptions::PrintBCHIROption::CE_CHIR2BCHIR);
    auto& packageBchir = bchirPackages.emplace_back();
    CHIR2BCHIR::CompileToBCHIR<true>(package,
        packageBchir, initFuncsForConstVar, sourceManager, opts, printBchir, ci.kind == IncreKind::INCR);
    Utils::ProfileRecorder::Stop("Constant Evaluation", "CHIR2BCHIR for const-eval");

    Utils::ProfileRecorder::Start("Constant Evaluation", "BCHIR linker for const-eval");
    Bchir linkedBchir;
    BCHIRLinker linker(linkedBchir);
    printBchir = opts.PrintBchir(GlobalOptions::PrintBCHIROption::CE_LINKED);
    auto gVarInitIVals = linker.Run<true>(bchirPackages, opts);
    Utils::ProfileRecorder::Stop("Constant Evaluation", "BCHIR linker for const-eval");

    auto fePlayground = linkedBchir.GetLinkedByteCode().Size();
    auto interpPlayground = fePlayground + BCHIRInterpreter::EXTERNAL_PLAYGROUND_SIZE;

    const size_t extraReqSpace =
        BCHIRInterpreter::INTERNAL_PLAYGROUND_SIZE + BCHIRInterpreter::EXTERNAL_PLAYGROUND_SIZE;
    // resize vector for the playground
    linkedBchir.Resize(fePlayground + extraReqSpace);

    std::unordered_map<std::string, void*> dyHandles{};

    BCHIRInterpreter interpreter(linkedBchir, diag, dyHandles, static_cast<unsigned>(fePlayground),
        static_cast<unsigned>(interpPlayground), true);
#ifndef NDEBUG
    interpreter.PrepareRuntimeDebug(opts);
#endif
    interpreter.SetGlobalVars(std::move(gVarInitIVals));
    gVarInitIVals = {};

    Utils::ProfileRecorder::Start("Constant Evaluation", "Evaluate global vars");
    auto res = interpreter.Run(0, false);
    Utils::ProfileRecorder::Stop("Constant Evaluation", "Evaluate global vars");
    if (std::holds_alternative<INotRun>(res)) {
        onSuccess(package, interpreter, linker);
    } else if (std::holds_alternative<IException>(res)) {
        // Suppress error, no way to know whether exception is legitimate
    }
}

void ConstEvalPass::ReplaceGlobalConstantInitializers(
    Package& package, BCHIRInterpreter& interpreter, BCHIRLinker& linker)
{
    Utils::ProfileRecorder recorder("Constant Evaluation", "Replace Global Constants");
    auto allFuncs = package.GetGlobalFunctions();
    auto it = allFuncs.begin();
    while (it != allFuncs.end()) {
        if ((*it)->GetFuncKind() != FuncKind::GLOBALVAR_INIT || !(*it)->TestAttr(Attribute::CONST)) {
            ++it;
            continue;
        }
        auto optNewBody = CreateNewInitializer(**it, interpreter, linker, package);
        if (opts.chirDebugOptimizer) {
            auto& pos = (*it)->GetDebugLocation();
            PrintDebugMessage(pos, **it, optNewBody);
        }

        if (!optNewBody.has_value()) {
            ++it;
            continue;
        } else if (optNewBody.value() == nullptr) {
            for (auto user : (*it)->GetUsers()) {
                user->RemoveSelfFromBlock();
            }
            (*it)->DestroySelf();
            it = allFuncs.erase(it);
        } else {
            (*optNewBody)
                ->GetEntryBlock()
                ->AppendExpression(builder.CreateTerminator<Exit>((*optNewBody)->GetEntryBlock()));

            (*it)->ReplaceBody(**optNewBody);
            ++it;
        }
    }
    package.SetAllGlobalFuncs(std::move(allFuncs));
}

std::optional<Cangjie::CHIR::BlockGroup*> ConstEvalPass::CreateNewInitializer(
    Function& oldInitializer, const BCHIRInterpreter& interpreter, const BCHIRLinker& linker, const Package& package)
{
    BlockGroup* newBody = nullptr;
    if (ci.invocation.globalOptions.enIncrementalCompilation) {
        newBody = builder.CreateBlockGroup(oldInitializer);
        newBody->SetOwnerFunc(&oldInitializer);
        newBody->SetEntryBlock(builder.CreateBlock(newBody));
    }
    for (auto block : oldInitializer.GetBody()->GetBlocks()) {
        for (auto expr : block->GetExpressions()) {
            if (expr->GetExprKind() != ExprKind::STORE) {
                continue;
            }
            auto location = StaticCast<const Store*>(expr)->GetLocation();
            if (!location->IsGlobalVarWithInitializer()) {
                continue;
            }
            auto global = StaticCast<GlobalVar*>(location);
            auto varId = linker.GetGVARId(global->GetIdentifierWithoutPrefix());
            CJC_ASSERT(varId != -1);
            auto& val = interpreter.PeekValueOfGlobal(static_cast<unsigned>(varId));
            IVal2CHIR val2chir(builder, interpreter.GetBchir(), package);
            auto varRefType = global->GetType();
            CJC_ASSERT(varRefType->IsRef());
            auto varType = varRefType->GetTypeArgs()[0];
            auto constant = val2chir.TryConvertToConstant(*varType, val, *block);
            if (constant) {
                block->AppendExpression(constant);
            }
            if (constant && !constant->IsConstantNull()) {
                global->SetInitializer(*constant->GetValue());
            } else {
                if (!newBody) {
                    newBody = builder.CreateBlockGroup(oldInitializer);
                    newBody->SetOwnerFunc(&oldInitializer);
                    newBody->SetEntryBlock(builder.CreateBlock(newBody));
                }

                auto newBlock = newBody->GetEntryBlock();

                std::function<void(Expression*)> insertFunction = [newBlock](Expression* expr) {
                    newBlock->AppendExpression(expr);
                };
                auto constValue = val2chir.ConvertToChir(*varType, val, insertFunction, *newBlock);
                if (!constValue) {
                    return std::nullopt;
                }
                newBlock->AppendExpression(
                    builder.CreateExpression<Store>(builder.GetUnitTy(), constValue, global, newBlock));
            }
        }
    }

    return newBody;
}

void ConstEvalPass::PrintDebugMessage(
    const DebugLocation& loc, const Function& oldInit, const std::optional<BlockGroup*>& newInit) const
{
    auto file = FileUtil::GetFileName(loc.GetAbsPath());
    std::string begin =
        file + ":" + std::to_string(loc.GetBeginPos().line) + ":" + std::to_string(loc.GetBeginPos().column);

    std::string end = file + ":" + std::to_string(loc.GetEndPos().line) + ":" + std::to_string(loc.GetEndPos().column);

    if (!newInit.has_value()) {
        std::cout << "debug: consteval at " << begin << " - " << end << " function `" << oldInit.GetSrcCodeIdentifier()
                  << "` not evaluated successfully." << std::endl;
    } else if (newInit.value() == nullptr) {
        std::cout << "debug: consteval at " << begin << " - " << end << " replaced initializer function `"
                  << oldInit.GetSrcCodeIdentifier() << "` with initializer constant(s)." << std::endl;
    } else {
        auto oldBody = oldInit.GetBody()->GetBlocks();
        auto newBody = (*newInit)->GetBlocks();
        std::cout << "debug: consteval at " << begin << " - " << end << " evaluated initializer function `"
                  << oldInit.GetSrcCodeIdentifier() << "` of "
                  << std::accumulate(oldBody.cbegin(), oldBody.cend(), static_cast<size_t>(0),
                         [](auto acc, auto& block) { return acc + block->GetExpressions().size(); })
                  << " expressions to one of "
                  << std::accumulate(newBody.cbegin(), newBody.cend(), static_cast<size_t>(0),
                         [](auto acc, auto& block) { return acc + block->GetExpressions().size(); })
                  << " expressions." << std::endl;
    }
}
