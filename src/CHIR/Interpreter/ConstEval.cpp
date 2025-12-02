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

Cangjie::CHIR::Constant* IVal2CHIR::TryConvertToConstant(Type& ty, const IVal& val, Block& parent)
{
    switch (ty.GetTypeKind()) {
        case Type::TYPE_INT8:
            return chirBuilder.CreateConstantExpression<IntLiteral>(
                &ty, &parent, static_cast<uint8_t>(IValUtils::Get<IInt8>(val).content));
        case Type::TYPE_INT16:
            return chirBuilder.CreateConstantExpression<IntLiteral>(
                &ty, &parent, static_cast<uint16_t>(IValUtils::Get<IInt16>(val).content));
        case Type::TYPE_INT32:
            return chirBuilder.CreateConstantExpression<IntLiteral>(
                &ty, &parent, static_cast<uint32_t>(IValUtils::Get<IInt32>(val).content));
        case Type::TYPE_INT64:
            return chirBuilder.CreateConstantExpression<IntLiteral>(
                &ty, &parent, static_cast<uint64_t>(IValUtils::Get<IInt64>(val).content));
        case Type::TYPE_INT_NATIVE:
            return chirBuilder.CreateConstantExpression<IntLiteral>(
                &ty, &parent, static_cast<uint64_t>(IValUtils::Get<IIntNat>(val).content));
        case Type::TYPE_UINT8:
            return chirBuilder.CreateConstantExpression<IntLiteral>(&ty, &parent, IValUtils::Get<IUInt8>(val).content);
        case Type::TYPE_UINT16:
            return chirBuilder.CreateConstantExpression<IntLiteral>(&ty, &parent, IValUtils::Get<IUInt16>(val).content);
        case Type::TYPE_UINT32:
            return chirBuilder.CreateConstantExpression<IntLiteral>(&ty, &parent, IValUtils::Get<IUInt32>(val).content);
        case Type::TYPE_UINT64:
            return chirBuilder.CreateConstantExpression<IntLiteral>(&ty, &parent, IValUtils::Get<IUInt64>(val).content);
        case Type::TYPE_UINT_NATIVE:
            return chirBuilder.CreateConstantExpression<IntLiteral>(
                &ty, &parent, IValUtils::Get<IUIntNat>(val).content);
        case Type::TYPE_FLOAT16:
            return chirBuilder.CreateConstantExpression<FloatLiteral>(
                &ty, &parent, IValUtils::Get<IFloat16>(val).content);
        case Type::TYPE_FLOAT32:
            return chirBuilder.CreateConstantExpression<FloatLiteral>(
                &ty, &parent, IValUtils::Get<IFloat32>(val).content);
        case Type::TYPE_FLOAT64:
            return chirBuilder.CreateConstantExpression<FloatLiteral>(
                &ty, &parent, IValUtils::Get<IFloat64>(val).content);
        case Type::TYPE_RUNE:
            return chirBuilder.CreateConstantExpression<RuneLiteral>(&ty, &parent, IValUtils::Get<IRune>(val).content);
        case Type::TYPE_BOOLEAN:
            return chirBuilder.CreateConstantExpression<BoolLiteral>(&ty, &parent, IValUtils::Get<IBool>(val).content);
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
    switch (ty.GetTypeKind()) {
        case Type::TYPE_TUPLE:
        case Type::TYPE_STRUCT: {
            auto tuple = IValUtils::Get<ITuple>(val);
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
            return ConvertArrayToChir(StaticCast<VArrayType&>(ty), IValUtils::Get<IArray>(val), insertExpr, parent);
        }
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
    auto& arrPtr = IValUtils::Get<IPointer>(val.content[0]);
    auto& arrData = IValUtils::Get<IArray>(*arrPtr.content).content;

    auto start = IValUtils::Get<IUInt32>(val.content[1]).content;
    CJC_ASSERT(start == static_cast<uint32_t>(0) && "const strings always start at zero");

    auto len = IValUtils::Get<IUInt32>(val.content[2]).content;

    stringVal.reserve(len);
    CJC_ASSERT(arrData.size() > static_cast<size_t>(len));

    for (uint32_t el = 0; el < len; el++) {
        stringVal.push_back(static_cast<char>(IValUtils::Get<IUInt8>(arrData[el + 1]).content));
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

Cangjie::CHIR::Value* IVal2CHIR::ConvertEnumToChir(
    EnumType& ty, const IVal& val, std::function<void(Expression*)>& insertExpr, Block& parent)
{
    auto selectorTyKind = GetSelectorType(*ty.GetEnumDef());
    auto selectorTy = chirBuilder.GetChirContext().ToSelectorType(selectorTyKind);
    if (ty.GetEnumDef()->IsAllCtorsTrivial()) {
        auto expr = chirBuilder.CreateConstantExpression<IntLiteral>(
            selectorTy, &parent, IValUtils::Get<IUInt32>(val).content);
        auto ret = chirBuilder.CreateExpression<TypeCast>(&ty, expr->GetResult(), &parent);
        insertExpr(expr);
        insertExpr(ret);
        return ret->GetResult();
    } else {
        auto elementVals = IValUtils::Get<ITuple>(val).content;
        std::vector<Value*> elements;
        uint64_t variantIndex;
        if (selectorTy->IsBoolean()) {
            variantIndex = static_cast<uint64_t>(IValUtils::Get<IBool>(elementVals[0]).content);
            elements.push_back(ConvertToChir(*selectorTy, elementVals[0], insertExpr, parent));
        } else {
            variantIndex = IValUtils::Get<IUInt32>(elementVals[0]).content;
            elements.push_back(ConvertToChir(*selectorTy, elementVals[0], insertExpr, parent));
        }
        auto elementTys = ty.GetConstructorInfos(chirBuilder)[variantIndex].funcType->GetParamTypes();
        CJC_ASSERT(elementVals.size() == elementTys.size() + 1);
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
}

Cangjie::CHIR::Value* IVal2CHIR::ConvertRefToChir(
    RefType& ty, const IVal& val, std::function<void(Expression*)>& insertExpr, Block& parent)
{
    auto referencedType = ty.GetBaseType();

    auto valContent = IValUtils::Get<IPointer>(val).content;
    switch (referencedType->GetTypeKind()) {
        case Type::TYPE_CLASS: {
            auto obj = IValUtils::GetIf<IObject>(valContent);

            // This is only partially implemeneted:
            // If a constant references another constant, then it should not create a deep copy of that constant.
            auto classType = StaticCast<ClassType*>(referencedType);
            auto& valClassName = bchir.GetClassTable().find(obj->classId)->second.mangledName;
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
            CJC_ASSERT(members.size() == obj->content.size());
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
                auto typeCast = chirBuilder.CreateExpression<TypeCast>(&ty, classVal, &parent);
                insertExpr(typeCast);
                return typeCast->GetResult();
            }
            return classVal;
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
    const std::vector<CHIR::FuncBase*>& initFuncsForConstVar, std::vector<Bchir>& bchirPackages)
{
    RunInterpreter(package, bchirPackages, initFuncsForConstVar,
        [this](auto& package, auto& interpreter, auto& linker) {
        ReplaceGlobalConstantInitializers(package, interpreter, linker);
    });
}

void ConstEvalPass::RunInterpreter(Package& package, std::vector<Bchir>& bchirPackages,
    const std::vector<CHIR::FuncBase*>& initFuncsForConstVar,
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
    auto allFuncs = package.GetGlobalFuncs();
    std::vector<Func*> funcsToBeRemoved;
    std::unordered_set<Expression*> expressionsToBeRemoved;
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
            auto users = (*it)->GetUsers();
            expressionsToBeRemoved.insert(users.begin(), users.end());
            funcsToBeRemoved.emplace_back(*it);
            it = allFuncs.erase(it);
        } else {
            (*optNewBody)
                ->GetEntryBlock()
                ->AppendExpression(builder.CreateTerminator<Exit>((*optNewBody)->GetEntryBlock()));

            (*it)->DestroySelf();
            (*it)->InitBody(**optNewBody);
            ++it;
        }
    }

    for (auto e : expressionsToBeRemoved) {
        e->RemoveSelfFromBlock();
    }
    package.SetGlobalFuncs(allFuncs);
}

std::optional<Cangjie::CHIR::BlockGroup*> ConstEvalPass::CreateNewInitializer(
    Func& oldInitializer, const BCHIRInterpreter& interpreter, const BCHIRLinker& linker, const Package& package)
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
            if (!location->IsGlobalVarInCurPackage()) {
                continue;
            }
            auto global = VirtualCast<GlobalVar*>(location);
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
    const DebugLocation& loc, const Func& oldInit, const std::optional<BlockGroup*>& newInit) const
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
