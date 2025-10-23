// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <algorithm>
#include <type_traits>

#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/GeneratedFromForIn.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/Type/EnumDef.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/UserDefinedType.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/ICEUtil.h"
#include "cangjie/Basic/Version.h"
#include "CHIRDeserializerImpl.h"
#include "cangjie/CHIR/Serializer/CHIRDeserializer.h"

using namespace Cangjie::CHIR;

// explicit specialization
template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::EnumDef* buffer, EnumDef& obj);
template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::StructDef* buffer, StructDef& obj);
template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::ClassDef* buffer, ClassDef& obj);
template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::ExtendDef* buffer, ExtendDef& obj);
template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::ExtendDef* buffer, ExtendDef& obj);

// =========================== Generic Deserializer ==============================

bool CHIRDeserializer::Deserialize(const std::string& fileName, Cangjie::CHIR::CHIRBuilder& chirBuilder,
    Cangjie::CHIR::ToCHIR::Phase& phase, bool compilePlatform)
{
    if (FileUtil::IsDir(fileName)) {
        Errorln(fileName, " is a directory.");
        return false;
    }
    if (!FileUtil::FileExist(fileName)) {
        Errorln(fileName, " not exist.");
        return false;
    }
    CHIRDeserializerImpl deserializer(chirBuilder, compilePlatform);
    std::vector<uint8_t> serializationInfo;
    std::string failedReason;
    if (!FileUtil::ReadBinaryFileToBuffer(fileName, serializationInfo, failedReason)) {
        Errorln(failedReason, ".");
        return false;
    }
    // Disable max depth and max tables verification.
    flatbuffers::Verifier::Options options;
    options.max_depth = std::numeric_limits<::flatbuffers::uoffset_t>::max();
    options.max_tables = std::numeric_limits<::flatbuffers::uoffset_t>::max();
    flatbuffers::Verifier verifier(serializationInfo.data(), serializationInfo.size(), options);
    if (!verifier.VerifyBuffer<PackageFormat::CHIRPackage>()) {
        Errorln("validation of '", fileName, "' failed, please confirm it was created by compiler whose version is '",
            CANGJIE_VERSION, "'.");
        return false;
    }
    const PackageFormat::CHIRPackage* package = PackageFormat::GetCHIRPackage(serializationInfo.data());
    deserializer.Run(package);
    phase = Cangjie::CHIR::ToCHIR::Phase(package->phase());
    return true;
}

template <typename T, typename FBT>
std::vector<T> CHIRDeserializer::CHIRDeserializerImpl::Create(const flatbuffers::Vector<FBT>* vec)
{
    std::vector<T> retval;
    for (auto obj : *vec) {
        retval.emplace_back(Create<T>(obj));
    }
    return retval;
}

template <typename T>
std::vector<T*> CHIRDeserializer::CHIRDeserializerImpl::GetValue(const flatbuffers::Vector<uint32_t>* vec)
{
    if (vec == nullptr) {
        return {};
    }
    std::vector<T*> retval;
    for (auto obj : *vec) {
        if (auto elem = GetValue<T>(obj)) {
            retval.emplace_back(elem);
        }
    }
    return retval;
}

template <typename T>
std::vector<T*> CHIRDeserializer::CHIRDeserializerImpl::GetType(const flatbuffers::Vector<uint32_t>* vec)
{
    if (vec == nullptr) {
        return {};
    }
    std::vector<T*> retval;
    for (auto obj : *vec) {
        if (auto elem = GetType<T>(obj)) {
            retval.emplace_back(elem);
        }
    }
    return retval;
}

template <typename T>
std::vector<T*> CHIRDeserializer::CHIRDeserializerImpl::GetExpression(const flatbuffers::Vector<uint32_t>* vec)
{
    if (vec == nullptr) {
        return {};
    }
    std::vector<T*> retval;
    for (auto obj : *vec) {
        if (auto elem = GetExpression<T>(obj)) {
            retval.emplace_back(elem);
        }
    }
    return retval;
}

template <typename T>
std::vector<T*> CHIRDeserializer::CHIRDeserializerImpl::GetCustomTypeDef(const flatbuffers::Vector<uint32_t>* vec)
{
    if (vec == nullptr) {
        return {};
    }
    std::vector<T*> retval;
    for (auto obj : *vec) {
        if (auto elem = GetCustomTypeDef<T>(obj)) {
            retval.emplace_back(elem);
        }
    }
    return retval;
}

// =========================== Helper Deserializer ==============================

namespace {
AttributeInfo CreateAttr(const uint64_t attrs)
{
    return AttributeInfo(attrs);
}

std::string GetMangleNameFromIdentifier(std::string& identifier)
{
    CJC_ASSERT(!identifier.empty());
    if (identifier.empty()) {
        return "";
    }
    if (identifier[0] == '@') {
        return identifier.substr(1);
    } else {
        return identifier;
    }
}
} // namespace

template <> Position CHIRDeserializer::CHIRDeserializerImpl::Create(const PackageFormat::Pos* obj)
{
    return Position{static_cast<unsigned>(obj->line()), static_cast<unsigned>(obj->column())};
}

template <> DebugLocation CHIRDeserializer::CHIRDeserializerImpl::Create(const PackageFormat::DebugLocation* obj)
{
    auto filePath = obj->filePath()->str();
    auto fileId = obj->fileId();
    auto beginPos = Create<Position>(obj->beginPos());
    auto endPos = Create<Position>(obj->endPos());
    auto scope = std::vector<int>(obj->scope()->begin(), obj->scope()->end());

    if (builder.GetChirContext().GetSourceFileName(fileId) == INVALID_NAME) {
        builder.GetChirContext().RegisterSourceFileName(fileId, filePath);
    }
    // DebugLocation stores a string pointer, so here can't just pass filePath.
    return DebugLocation(builder.GetChirContext().GetSourceFileName(fileId), fileId, beginPos, endPos, scope);
}

template <> AnnoInfo CHIRDeserializer::CHIRDeserializerImpl::Create(const PackageFormat::AnnoInfo* obj)
{
    auto mangledName = obj->mangledName()->str();
    return AnnoInfo{mangledName};
}

template <> Tuple* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Tuple* obj);

template <> MemberVarInfo CHIRDeserializer::CHIRDeserializerImpl::Create(const PackageFormat::MemberVarInfo* obj)
{
    auto name = obj->name()->str();
    auto rawMangledName = obj->rawMangledName()->str();
    auto type = GetType<Type>(obj->type());
    auto attributeInfo = CreateAttr(obj->attributes());
    if (compilePlatform) {
        attributeInfo.SetAttr(Attribute::DESERIALIZED, true);
    }
    auto loc = Create<DebugLocation>(obj->loc());
    auto annoInfo = Create<AnnoInfo>(obj->annoInfo());
    auto initializerFunc = GetValue<FuncBase>(obj->initializerFunc());
    auto outerDef = GetCustomTypeDef<CustomTypeDef>(obj->outerDef());
    return MemberVarInfo{name, rawMangledName, type, attributeInfo, loc, annoInfo, initializerFunc, outerDef};
}

template <> EnumCtorInfo CHIRDeserializer::CHIRDeserializerImpl::Create(const PackageFormat::EnumCtorInfo* obj)
{
    auto name = obj->identifier()->str();
    auto mangledName = obj->mangledName()->str();
    auto funcType = GetType<FuncType>(obj->funcType());
    return EnumCtorInfo{name, mangledName, funcType};
}

template <>
AbstractMethodParam CHIRDeserializer::CHIRDeserializerImpl::Create(const PackageFormat::AbstractMethodParam* obj)
{
    auto paramName = obj->paramName()->str();
    auto paramType = GetType<Type>(obj->paramType());
    auto annoInfo = Create<AnnoInfo>(obj->annoInfo());
    return AbstractMethodParam{paramName, paramType, annoInfo};
}

template <>
AbstractMethodInfo CHIRDeserializer::CHIRDeserializerImpl::Create(const PackageFormat::AbstractMethodInfo* obj)
{
    auto name = obj->methodName()->str();
    auto mangledName = obj->mangledName()->str();
    auto methodTy = GetType<Type>(obj->methodType());
    auto paramInfos = Create<AbstractMethodParam>(obj->paramsInfo());
    auto attributeInfo = CreateAttr(obj->attributes());
    auto annoInfo = Create<AnnoInfo>(obj->annoInfo());
    auto methodGenericTypeParams = GetType<GenericType>(obj->methodGenericTypeParams());
    auto hasBody = obj->hasBody();
    auto parent = GetCustomTypeDef<ClassDef>(obj->parent());
    return AbstractMethodInfo{
        name, mangledName, methodTy, paramInfos, attributeInfo, annoInfo, methodGenericTypeParams, hasBody, parent};
}

template <>
VirtualMethodInfo CHIRDeserializer::CHIRDeserializerImpl::Create(const PackageFormat::VirtualMethodInfo* obj)
{
    auto condition = FuncSigInfo {
        .funcName = obj->funcName()->str(),
        .funcType = GetType<FuncType>(obj->sigType()),
        .genericTypeParams = GetType<GenericType>(obj->methodGenericTypeParams())
    };
    auto funcPtr = GetValue<FuncBase>(obj->instance());
    auto attributeInfo = CreateAttr(obj->attributes());
    auto originalType = GetType<FuncType>(obj->originalType());
    auto parentType = GetType<Type>(obj->parentType());
    auto returnType = GetType<Type>(obj->returnType());
    return VirtualMethodInfo(std::move(condition), funcPtr, attributeInfo, *originalType, *parentType, *returnType);
}

template <>
VTableInDef CHIRDeserializer::CHIRDeserializerImpl::Create(
    const flatbuffers::Vector<flatbuffers::Offset<PackageFormat::VTableInType>>* obj)
{
    VTableInDef vtableInDef;
    for (auto item : *obj) {
        auto ty = GetType<ClassType>(item->srcParentType());
        std::vector<VirtualMethodInfo> info = Create<VirtualMethodInfo>(item->virtualMethods());
        vtableInDef.AddNewItemToTypeVTable(*ty, std::move(info));
    }
    return vtableInDef;
}

// =========================== Custom Type Define Deserializer ==============================

template <> EnumDef* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::EnumDef* obj)
{
    auto srcCodeIdentifier = obj->base()->srcCodeIdentifier()->str();
    auto identifier = obj->base()->identifier()->str();
    auto packageName = obj->base()->packageName()->str();
    auto attrs = CreateAttr(obj->base()->attributes());
    if (compilePlatform) {
        attrs.SetAttr(CHIR::Attribute::DESERIALIZED, true);
    }
    auto imported = attrs.TestAttr(CHIR::Attribute::IMPORTED);

    auto result = builder.CreateEnum(DebugLocation(), srcCodeIdentifier, GetMangleNameFromIdentifier(identifier),
        packageName, imported, obj->nonExhaustive());
    result->AppendAttributeInfo(attrs);
    return result;
}

template <> StructDef* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::StructDef* obj)
{
    auto srcCodeIdentifier = obj->base()->srcCodeIdentifier()->str();
    auto identifier = obj->base()->identifier()->str();
    auto packageName = obj->base()->packageName()->str();
    auto attrs = CreateAttr(obj->base()->attributes());
    if (compilePlatform) {
        attrs.SetAttr(CHIR::Attribute::DESERIALIZED, true);
    }
    auto imported = attrs.TestAttr(CHIR::Attribute::IMPORTED);

    auto result = builder.CreateStruct(
        DebugLocation(), srcCodeIdentifier, GetMangleNameFromIdentifier(identifier), packageName, imported);
    result->AppendAttributeInfo(attrs);
    return result;
}

template <> ClassDef* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::ClassDef* obj)
{
    auto srcCodeIdentifier = obj->base()->srcCodeIdentifier()->str();
    auto identifier = obj->base()->identifier()->str();
    auto packageName = obj->base()->packageName()->str();
    auto isClass = obj->kind() == PackageFormat::ClassDefKind::ClassDefKind_CLASS;
    auto attrs = CreateAttr(obj->base()->attributes());
    if (compilePlatform) {
        attrs.SetAttr(CHIR::Attribute::DESERIALIZED, true);
    }
    auto imported = attrs.TestAttr(CHIR::Attribute::IMPORTED);

    auto result = builder.CreateClass(
        DebugLocation(), srcCodeIdentifier, GetMangleNameFromIdentifier(identifier), packageName, isClass, imported);
    result->AppendAttributeInfo(attrs);
    return result;
}

template <> ExtendDef* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::ExtendDef* obj)
{
    auto srcCodeIndentifier = obj->base()->srcCodeIdentifier()->str();
    auto identifier = obj->base()->identifier()->str();
    auto packageName = obj->base()->packageName()->str();
    auto attrs = CreateAttr(obj->base()->attributes());
    auto imported = attrs.TestAttr(CHIR::Attribute::IMPORTED);
    if (compilePlatform) {
        attrs.SetAttr(CHIR::Attribute::DESERIALIZED, true);
    }
    auto genericParams = GetType<GenericType>(obj->genericParams());
    auto result = builder.CreateExtend(
        DebugLocation(), GetMangleNameFromIdentifier(identifier), packageName, imported, genericParams);
    result->AppendAttributeInfo(attrs);
    return result;
}

// =========================== Type Deserializer ==============================

template <>
RuneType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize([[maybe_unused]] const PackageFormat::RuneType* obj)
{
    return builder.GetType<RuneType>();
}

template <>
BooleanType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize([[maybe_unused]] const PackageFormat::BooleanType* obj)
{
    return builder.GetType<BooleanType>();
}

template <>
UnitType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize([[maybe_unused]] const PackageFormat::UnitType* obj)
{
    return builder.GetType<UnitType>();
}

template <>
NothingType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize([[maybe_unused]] const PackageFormat::NothingType* obj)
{
    return builder.GetType<NothingType>();
}

template <> TupleType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::TupleType* obj)
{
    CJC_NULLPTR_CHECK(obj->base()->argTys());
    auto argTys = GetType<Type>(obj->base()->argTys());
    return builder.GetType<TupleType>(argTys);
}

template <> RawArrayType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::RawArrayType* obj)
{
    CJC_NULLPTR_CHECK(obj->base()->argTys());
    auto elemTy = GetType<Type>(obj->base()->argTys()->Get(0));
    auto dims = obj->dims();
    return builder.GetType<RawArrayType>(elemTy, static_cast<unsigned>(dims));
}

template <> VArrayType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::VArrayType* obj)
{
    CJC_NULLPTR_CHECK(obj->base()->argTys());
    auto elemTy = GetType<Type>(obj->base()->argTys()->Get(0));
    auto size = obj->size();
    return builder.GetType<VArrayType>(elemTy, size);
}

template <> FuncType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::FuncType* obj)
{
    CJC_NULLPTR_CHECK(obj->base()->argTys());
    auto argTys = obj->base()->argTys();
    auto retTy = GetType<Type>(argTys->Get(argTys->size() - 1));
    auto hasVarLenParam = obj->hasVarArg();
    auto isCFuncType = obj->isCFuncType();
    std::vector<Type*> paramTys;
    CJC_ASSERT(argTys->size() > 0);
    for (size_t i = 0; i < argTys->size() - 1; ++i) {
        paramTys.emplace_back(GetType<Type>(argTys->Get(static_cast<unsigned>(i))));
    }
    return builder.GetType<FuncType>(paramTys, retTy, hasVarLenParam, isCFuncType);
}

template <> CustomType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::CustomType* obj)
{
    auto kind = Type::TypeKind(obj->base()->kind());
    auto def = GetCustomTypeDef<CustomTypeDef>(obj->customTypeDef());
    auto typeArgs = GetType<Type>(obj->base()->argTys());
    return builder.GetType<CustomType>(kind, def, typeArgs);
}

template <>
CStringType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize([[maybe_unused]] const PackageFormat::CStringType* obj)
{
    return builder.GetType<CStringType>();
}

template <> CPointerType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::CPointerType* obj)
{
    CJC_NULLPTR_CHECK(obj->base()->argTys());
    auto elemTy = GetType<Type>(obj->base()->argTys()->Get(0));
    return builder.GetType<CPointerType>(elemTy);
}

template <> GenericType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::GenericType* obj)
{
    auto identifier = obj->identifier()->str();
    auto srcCodeIndentifier = obj->srcCodeIdentifier()->str();
    auto genericType = builder.GetType<GenericType>(identifier, srcCodeIndentifier);
    genericType->orphanFlag = obj->orphanFlag();
    genericType->skipCheck = obj->skipCheck();
    genericTypeConfig.emplace_back(genericType, obj);
    return genericType;
}

template <> RefType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::RefType* obj)
{
    CJC_NULLPTR_CHECK(obj->base()->argTys());
    auto baseType = GetType<Type>(obj->base()->argTys()->Get(0));
    return builder.GetType<RefType>(baseType);
}

template <> BoxType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::BoxType* obj)
{
    CJC_NULLPTR_CHECK(obj->base()->argTys());
    auto baseType = GetType<Type>(obj->base()->argTys()->Get(0));
    return builder.GetType<BoxType>(baseType);
}

template <>
ThisType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize([[maybe_unused]] const PackageFormat::ThisType* obj)
{
    return builder.GetType<ThisType>();
}

template <>
VoidType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize([[maybe_unused]] const PackageFormat::VoidType* obj)
{
    return builder.GetType<VoidType>();
}

// =========================== Custom Type Deserializer ==============================

template <> EnumType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::EnumType* obj)
{
    auto def = GetCustomTypeDef<EnumDef>(obj->base()->customTypeDef());
    auto argTys = GetType<Type>(obj->base()->base()->argTys());
    return builder.GetType<EnumType>(def, argTys);
}

template <> StructType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::StructType* obj)
{
    auto def = GetCustomTypeDef<StructDef>(obj->base()->customTypeDef());
    auto argTys = GetType<Type>(obj->base()->base()->argTys());
    return builder.GetType<StructType>(def, argTys);
}

template <> ClassType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::ClassType* obj)
{
    [[maybe_unused]] auto tyId = obj->base()->base()->typeID();
    auto def = GetCustomTypeDef<ClassDef>(obj->base()->customTypeDef());
    auto argTys = GetType<Type>(obj->base()->base()->argTys());
    return builder.GetType<ClassType>(def, argTys);
}

// =========================== Numeric Type Deserializer ==============================

template <> IntType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::IntType* obj)
{
    auto kind = Type::TypeKind(obj->base()->base()->kind());
    return builder.GetType<IntType>(kind);
}

template <> FloatType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::FloatType* obj)
{
    auto kind = Type::TypeKind(obj->base()->base()->kind());
    return builder.GetType<FloatType>(kind);
}

// =========================== Value Deserializer ==============================

template <> BoolLiteral* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::BoolLiteral* obj)
{
    auto type = GetType<Type>(obj->base()->base()->type());
    auto val = obj->val();
    return builder.CreateLiteralValue<BoolLiteral>(type, val);
}

template <> RuneLiteral* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::RuneLiteral* obj)
{
    auto type = GetType<Type>(obj->base()->base()->type());
    auto val = obj->val();
    return builder.CreateLiteralValue<RuneLiteral>(type, static_cast<char32_t>(val));
}

template <> StringLiteral* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::StringLiteral* obj)
{
    auto type = GetType<Type>(obj->base()->base()->type());
    auto val = obj->val()->str();
    return builder.CreateLiteralValue<StringLiteral>(type, val);
}

template <> IntLiteral* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::IntLiteral* obj)
{
    auto type = GetType<Type>(obj->base()->base()->type());
    auto val = obj->val();
    return builder.CreateLiteralValue<IntLiteral>(type, val);
}

template <> FloatLiteral* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::FloatLiteral* obj)
{
    auto type = GetType<Type>(obj->base()->base()->type());
    auto val = obj->val();
    return builder.CreateLiteralValue<FloatLiteral>(type, val);
}

template <> UnitLiteral* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::UnitLiteral* obj)
{
    auto type = GetType<Type>(obj->base()->base()->type());
    return builder.CreateLiteralValue<UnitLiteral>(type);
}

template <> NullLiteral* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::NullLiteral* obj)
{
    auto type = GetType<Type>(obj->base()->base()->type());
    return builder.CreateLiteralValue<NullLiteral>(type);
}

template <> Func* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Func* obj)
{
    auto funcTy = GetType<FuncType>(obj->base()->type());
    auto identifier = obj->base()->identifier()->str();
    auto srcCodeIdentifier = obj->srcCodeIdentifier()->str();
    auto rawMangledName = obj->rawMangledName()->str();
    auto packageName = obj->packageName()->str();
    auto genericTypeParams = GetType<GenericType>(obj->genericTypeParams());
    auto result = builder.CreateFunc(DebugLocation(), funcTy, GetMangleNameFromIdentifier(identifier),
        srcCodeIdentifier, rawMangledName, packageName, genericTypeParams);
    // Indexes below are needed to modify correctly function body when merging CJMP initializers
    result->SetLocalId(obj->localId());
    result->SetBlockId(obj->localId());
    result->SetBlockGroupId(obj->localId());
    return result;
}

template <> BlockGroup* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::BlockGroup* obj)
{
    BlockGroup* blockGroup = nullptr;
    if (obj->ownedFunc() != 0) {
        if (auto ownedFunc = GetValue<Func>(obj->ownedFunc())) {
            blockGroup = builder.CreateBlockGroup(*ownedFunc);
            blockGroup->SetOwnerFunc(ownedFunc);
        } else {
            CJC_ABORT();
        }
    } else if (obj->ownedExpression() != 0) {
        auto exprType = PackageFormat::ExpressionElem(pool->exprs_type()->Get(obj->ownedExpression() - 1));
        switch (exprType) {
            case PackageFormat::ExpressionElem_Lambda: {
                auto ownedLambda = GetExpression<Lambda>(obj->ownedExpression());
                CJC_NULLPTR_CHECK(ownedLambda);
                if (ownedLambda->GetBody() != nullptr) {
                    return ownedLambda->GetBody();
                }
                CJC_NULLPTR_CHECK(ownedLambda->GetTopLevelFunc());
                blockGroup = builder.CreateBlockGroup(*ownedLambda->GetTopLevelFunc());
                ownedLambda->InitBody(*blockGroup);
                break;
            }
            case PackageFormat::ExpressionElem_ForInRange: {
                auto ownedForInRange = GetExpression<ForInRange>(obj->ownedExpression());
                CJC_NULLPTR_CHECK(ownedForInRange);
                blockGroup = builder.CreateBlockGroup(*ownedForInRange->GetTopLevelFunc());
                break;
            }
            case PackageFormat::ExpressionElem_ForInClosedRange: {
                auto ownedForInClosedRange = GetExpression<ForInClosedRange>(obj->ownedExpression());
                CJC_NULLPTR_CHECK(ownedForInClosedRange);
                blockGroup = builder.CreateBlockGroup(*ownedForInClosedRange->GetTopLevelFunc());
                break;
            }
            case PackageFormat::ExpressionElem_ForInIter: {
                auto ownedForInIter = GetExpression<ForInIter>(obj->ownedExpression());
                CJC_NULLPTR_CHECK(ownedForInIter);
                blockGroup = builder.CreateBlockGroup(*ownedForInIter->GetTopLevelFunc());
                break;
            }
            default:
                CJC_ABORT();
        }
    }
    return blockGroup;
}

template <> Block* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Block* obj)
{
    auto parentGroup = GetValue<BlockGroup>(obj->parentGroup());
    return builder.CreateBlock(parentGroup);
}

template <> Parameter* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Parameter* obj)
{
    CJC_ASSERT(obj->base()->kind() == PackageFormat::ValueKind_PARAMETER);
    auto type = GetType<Type>(obj->base()->type());
    Parameter* result = nullptr;
    if (auto ownedFunc = GetValue<Func>(obj->ownedFunc())) {
        result = builder.CreateParameter(type, INVALID_LOCATION, *ownedFunc);
    } else if (auto ownedLambda = GetExpression<Lambda>(obj->ownedLambda())) {
        result = builder.CreateParameter(type, INVALID_LOCATION, *ownedLambda);
    } else {
        CJC_ABORT();
    }
    result->SetAnnoInfo(Create<AnnoInfo>(obj->base()->annoInfo()));
    StaticCast<Value*>(result)->identifier = obj->base()->identifier()->str();
    return result;
}

template <> LocalVar* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::LocalVar* obj)
{
    auto associatedExpr = GetExpression<Expression>(obj->associatedExpr());
    CJC_NULLPTR_CHECK(associatedExpr);
    auto result = associatedExpr->GetResult();
    return result;
}

template <> GlobalVar* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::GlobalVar* obj)
{
    auto type = GetType<RefType>(obj->base()->type());
    auto identifier = obj->base()->identifier()->str();
    auto srcCodeIdentifier = obj->srcCodeIdentifier()->str();
    auto packageName = obj->packageName()->str();
    auto rawMangledName = obj->rawMangledName()->str();
    auto attrs = CreateAttr(obj->base()->attributes());
    if (compilePlatform) {
        attrs.SetAttr(Attribute::DESERIALIZED, true);
    }
    auto result = builder.CreateGlobalVar(
        DebugLocation(), type, GetMangleNameFromIdentifier(identifier), srcCodeIdentifier, rawMangledName, packageName);
    result->AppendAttributeInfo(attrs);
    return result;
}

template <> ImportedFunc* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::ImportedFunc* obj)
{
    auto type = GetType<Type>(obj->base()->base()->type());
    auto identifier = obj->base()->base()->identifier()->str();
    auto srcCodeIdentifier = obj->srcCodeIdentifier()->str();
    auto rawMangledName = obj->rawMangledName()->str();
    auto packageName = obj->packageName()->str();
    auto genericTypeParams = GetType<GenericType>(obj->genericTypeParams());
    auto attrs = CreateAttr(obj->base()->base()->attributes());
    if (compilePlatform) {
        attrs.SetAttr(Attribute::DESERIALIZED, true);
    }
    auto importedFunc = builder.CreateImportedVarOrFunc<ImportedFunc>(type, GetMangleNameFromIdentifier(identifier),
        srcCodeIdentifier, rawMangledName, packageName, genericTypeParams);
    // Object configuration
    importedFunc->AppendAttributeInfo(attrs);
    importedFunc->SetFuncKind(static_cast<FuncKind>(obj->funcKind()));
    importedFunc->SetRawMangledName(obj->rawMangledName()->str());
    importedFunc->SetParamInfo(Create<AbstractMethodParam>(obj->paramInfo()));
    importedFunc->SetAnnoInfo(Create<AnnoInfo>(obj->base()->base()->annoInfo()));
    importedFunc->SetFastNative(obj->isFastNative());
    importedFunc->SetCFFIWrapper(obj->isCFFIWrapper());
    return importedFunc;
}

template <> ImportedVar* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::ImportedVar* obj)
{
    auto type = GetType<RefType>(obj->base()->base()->type());
    auto identifier = obj->base()->base()->identifier()->str();
    auto srcCodeIdentifier = obj->srcCodeIdentifier()->str();
    auto rawMangledName = obj->rawMangledName()->str();
    auto packageName = obj->packageName()->str();
    auto attrs = CreateAttr(obj->base()->base()->attributes());
    if (compilePlatform) {
        attrs.SetAttr(Attribute::DESERIALIZED, true);
    }
    auto importedVar = builder.CreateImportedVarOrFunc<ImportedVar>(
        type, GetMangleNameFromIdentifier(identifier), srcCodeIdentifier, rawMangledName, packageName);
    // Object configuration
    importedVar->AppendAttributeInfo(attrs);
    importedVar->SetAnnoInfo(Create<AnnoInfo>(obj->base()->base()->annoInfo()));
    return importedVar;
}
// =========================== Expression Deserializer ==============================

template <>
UnaryExpression* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::UnaryExpression* obj)
{
    auto kind = ExprKind(obj->base()->kind());
    auto operand = GetValue<Value>(obj->base()->operands()->Get(0));
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto ofs = OverflowStrategy(obj->overflowStrategy());
    return builder.CreateExpression<UnaryExpression>(resultTy, kind, operand, ofs, parentBlock);
}

template <>
BinaryExpression* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::BinaryExpression* obj)
{
    auto kind = ExprKind(obj->base()->kind());
    auto lhs = GetValue<Value>(obj->base()->operands()->Get(0));
    auto rhs = GetValue<Value>(obj->base()->operands()->Get(1));
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto ofs = OverflowStrategy(obj->overflowStrategy());
    return builder.CreateExpression<BinaryExpression>(resultTy, kind, lhs, rhs, ofs, parentBlock);
}

template <> Constant* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Constant* obj)
{
    auto val = static_cast<LiteralValue*>(GetValue<Value>(obj->base()->operands()->Get(0)));
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    if (val->IsBoolLiteral()) {
        auto litVal = StaticCast<BoolLiteral*>(val);
        return builder.CreateConstantExpression<BoolLiteral>(resultTy, parentBlock, litVal->GetVal());
    } else if (val->IsFloatLiteral()) {
        auto litVal = StaticCast<FloatLiteral*>(val);
        return builder.CreateConstantExpression<FloatLiteral>(resultTy, parentBlock, litVal->GetVal());
    } else if (val->IsIntLiteral()) {
        auto litVal = StaticCast<IntLiteral*>(val);
        return builder.CreateConstantExpression<IntLiteral>(resultTy, parentBlock, litVal->GetUnsignedVal());
    } else if (val->IsNullLiteral()) {
        return builder.CreateConstantExpression<NullLiteral>(resultTy, parentBlock);
    } else if (val->IsRuneLiteral()) {
        auto litVal = StaticCast<RuneLiteral*>(val);
        return builder.CreateConstantExpression<RuneLiteral>(resultTy, parentBlock, litVal->GetVal());
    } else if (val->IsStringLiteral()) {
        auto litVal = StaticCast<StringLiteral*>(val);
        return builder.CreateConstantExpression<StringLiteral>(
            resultTy, parentBlock, litVal->GetVal());
    } else if (val->IsUnitLiteral()) {
        return builder.CreateConstantExpression<UnitLiteral>(resultTy, parentBlock);
    } else {
        CJC_ABORT();
    }
    return nullptr;
}

template <> Allocate* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Allocate* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto targetType = GetType<Type>(obj->targetType());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<Allocate>(resultTy, targetType, parentBlock);
}

template <> Cangjie::CHIR::Load* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Load* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto location = GetValue<Value>(obj->base()->operands()->Get(0));
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<Cangjie::CHIR::Load>(resultTy, location, parentBlock);
}

template <> Store* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Store* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto value = GetValue<Value>(obj->base()->operands()->Get(0));
    auto location = GetValue<Value>(obj->base()->operands()->Get(1));
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<Store>(resultTy, value, location, parentBlock);
}

template <> GetElementRef* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::GetElementRef* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto location = GetValue<Value>(obj->base()->operands()->Get(0));
    CJC_NULLPTR_CHECK(obj->path());
    auto path = std::vector<uint64_t>(obj->path()->begin(), obj->path()->end());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<GetElementRef>(resultTy, location, path, parentBlock);
}

template <> GetElementByName* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(
    const PackageFormat::GetElementByName* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto location = GetValue<Value>(obj->base()->operands()->Get(0));
    std::vector<std::string> names;
    names.reserve(obj->names()->size());
    for (const auto& name : *obj->names()) {
        names.emplace_back(name->str());
    }
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<GetElementByName>(resultTy, location, names, parentBlock);
}

template <>
StoreElementRef* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::StoreElementRef* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto value = GetValue<Value>(obj->base()->operands()->Get(0));
    auto location = GetValue<Value>(obj->base()->operands()->Get(1));
    CJC_NULLPTR_CHECK(obj->path());
    auto path = std::vector<uint64_t>(obj->path()->begin(), obj->path()->end());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<StoreElementRef>(resultTy, value, location, path, parentBlock);
}

template <>
StoreElementByName* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::StoreElementByName* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto value = GetValue<Value>(obj->base()->operands()->Get(0));
    auto location = GetValue<Value>(obj->base()->operands()->Get(1));
    std::vector<std::string> names;
    names.reserve(obj->names()->size());
    for (const auto& name : *obj->names()) {
        names.emplace_back(name->str());
    }
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<StoreElementByName>(resultTy, value, location, names, parentBlock);
}

template <> Apply* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Apply* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    CJC_NULLPTR_CHECK(obj->base()->operands());
    auto operands = GetValue<Value>(obj->base()->operands());
    auto callee = operands[0];
    auto args = std::vector<Value*>(operands.begin() + 1, operands.end());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto instantiatedTypeArgs = GetType<Type>(obj->instantiatedTypeArgs());
    auto thisType = GetType<Type>(obj->thisType());
    std::vector<Type*> genericInfo;
    std::transform(instantiatedTypeArgs.begin(), instantiatedTypeArgs.end(), std::back_inserter(genericInfo),
        [](Type* type) { return Ptr<Type>(type); });
    auto result = builder.CreateExpression<Apply>(resultTy, callee, FuncCallContext{
        .args = args,
        .instTypeArgs = genericInfo,
        .thisType = thisType}, parentBlock);
    if (obj->isSuperCall()) {
        result->SetSuperCall();
    }
    return result;
}

template <> Invoke* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Invoke* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    CJC_NULLPTR_CHECK(obj->base()->operands());
    auto operands = GetValue<Value>(obj->base()->operands());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto tempTypes = GetType<Type>(obj->virMethodCtx()->genericTypeParams());
    std::vector<GenericType*> genericTypes;
    for (auto ty : tempTypes) {
        genericTypes.emplace_back(Cangjie::StaticCast<GenericType*>(ty));
    }
    auto invokeInfo = InvokeCallContext {
        .caller = operands[0],
        .funcCallCtx = FuncCallContext {
            .args = std::vector<Value*>(operands.begin() + 1, operands.end()),
            .instTypeArgs = GetType<Type>(obj->instantiatedTypeArgs()),
            .thisType = GetType<Type>(obj->thisType())
        },
        .virMethodCtx = VirMethodContext {
            .srcCodeIdentifier = obj->virMethodCtx()->srcCodeIdentifier()->data(),
            .originalFuncType = GetType<FuncType>(obj->virMethodCtx()->originalFuncType()),
            .genericTypeParams = std::move(genericTypes)
        }
    };
    return builder.CreateExpression<Invoke>(resultTy, invokeInfo, parentBlock);
}

template <> InvokeStatic* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::InvokeStatic* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto argsSize = obj->base()->operands()->size();
    std::vector<Value*> args;
    for (unsigned i{1}; i < argsSize; ++i) {
        args.push_back(GetValue(obj->base()->operands()->Get(i)));
    }
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto tempTypes = GetType<Type>(obj->virMethodCtx()->genericTypeParams());
    std::vector<GenericType*> genericTypes;
    for (auto ty : tempTypes) {
        genericTypes.emplace_back(Cangjie::StaticCast<GenericType*>(ty));
    }
    auto invokeInfo = InvokeCallContext {
        .caller = GetValue(obj->base()->operands()->Get(0)),
        .funcCallCtx = FuncCallContext {
            .args = args,
            .instTypeArgs = GetType<Type>(obj->instantiatedTypeArgs()),
            .thisType = GetType<Type>(obj->thisType())
        },
        .virMethodCtx = VirMethodContext {
            .srcCodeIdentifier = obj->virMethodCtx()->srcCodeIdentifier()->data(),
            .originalFuncType = GetType<FuncType>(obj->virMethodCtx()->originalFuncType()),
            .genericTypeParams = std::move(genericTypes)
        }
    };
    return builder.CreateExpression<InvokeStatic>(resultTy, invokeInfo, parentBlock);
}

template <>
InvokeStaticWithException* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(
    const PackageFormat::InvokeStaticWithException* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->base()->parentBlock());
    auto argsSize = obj->base()->base()->operands()->size();
    std::vector<Value*> args;
    for (unsigned i{1}; i < argsSize; ++i) {
        args.push_back(GetValue(obj->base()->base()->operands()->Get(i)));
    }
    auto resultTy = GetType<Type>(obj->base()->base()->resultTy());
    auto sucBlock = GetValue<Block>(obj->base()->successors()->Get(0));
    auto errBlock = GetValue<Block>(obj->base()->successors()->Get(1));
    auto tempTypes = GetType<Type>(obj->virMethodCtx()->genericTypeParams());
    std::vector<GenericType*> genericTypes;
    for (auto ty : tempTypes) {
        genericTypes.emplace_back(Cangjie::StaticCast<GenericType*>(ty));
    }
    auto invokeInfo = InvokeCallContext {
        .caller = GetValue(obj->base()->base()->operands()->Get(0)),
        .funcCallCtx = FuncCallContext {
            .args = args,
            .instTypeArgs = GetType<Type>(obj->instantiatedTypeArgs()),
            .thisType = GetType<Type>(obj->thisType())
        },
        .virMethodCtx = VirMethodContext {
            .srcCodeIdentifier = obj->virMethodCtx()->srcCodeIdentifier()->data(),
            .originalFuncType = GetType<FuncType>(obj->virMethodCtx()->originalFuncType()),
            .genericTypeParams = std::move(genericTypes)
        }
    };
    return builder.CreateExpression<InvokeStaticWithException>(resultTy, invokeInfo, sucBlock, errBlock, parentBlock);
}

template <>
GetInstantiateValue* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::GetInstantiateValue* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto val = GetValue<Value>(obj->base()->operands()->Get(0));
    auto insTypes = GetType<Type>(obj->instantiateTys());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<GetInstantiateValue>(resultTy, val, insTypes, parentBlock);
}

template <>
TransformToConcrete* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::TransformToConcrete* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto operand = GetValue<Value>(obj->base()->operands()->Get(0));
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<TransformToConcrete>(resultTy, operand, parentBlock);
}

template <>
TransformToGeneric* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::TransformToGeneric* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto operand = GetValue<Value>(obj->base()->operands()->Get(0));
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<TransformToGeneric>(resultTy, operand, parentBlock);
}

template <> UnBoxToRef* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::UnBoxToRef* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto operand = GetValue<Value>(obj->base()->operands()->Get(0));
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<UnBoxToRef>(resultTy, operand, parentBlock);
}

template <> TypeCast* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::TypeCast* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto operand = GetValue<Value>(obj->base()->operands()->Get(0));
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto ofs = OverflowStrategy(obj->overflowStrategy());
    return builder.CreateExpression<TypeCast>(resultTy, operand, ofs, parentBlock);
}

template <> InstanceOf* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::InstanceOf* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto operand = GetValue<Value>(obj->base()->operands()->Get(0));
    auto targetType = GetType<Type>(obj->targetType());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<InstanceOf>(resultTy, operand, targetType, parentBlock);
}

template <> Box* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Box* expr)
{
    auto parentBlock = GetValue<Block>(expr->base()->parentBlock());
    auto operand = GetValue<Value>(expr->base()->operands()->Get(0));
    auto resultTy = GetType<Type>(expr->base()->resultTy());
    return builder.CreateExpression<Box>(resultTy, operand, parentBlock);
}

template <> UnBox* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::UnBox* expr)
{
    auto parentBlock = GetValue<Block>(expr->base()->parentBlock());
    auto operand = GetValue<Value>(expr->base()->operands()->Get(0));
    auto resultTy = GetType<Type>(expr->base()->resultTy());
    return builder.CreateExpression<UnBox>(resultTy, operand, parentBlock);
}
template <> Tuple* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Tuple* obj)
{
    auto operands = GetValue<Value>(obj->base()->operands());
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<Tuple>(resultTy, operands, parentBlock);
}

template <> Field* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Field* obj)
{
    auto val = GetValue<Value>(obj->base()->operands()->Get(0));
    CJC_NULLPTR_CHECK(obj->path());
    auto indexes = std::vector<uint64_t>(obj->path()->begin(), obj->path()->end());
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<Field>(resultTy, val, indexes, parentBlock);
}

template <> FieldByName* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::FieldByName* obj)
{
    auto val = GetValue<Value>(obj->base()->operands()->Get(0));
    std::vector<std::string> names;
    names.reserve(obj->names()->size());
    for (const auto& name : *obj->names()) {
        names.emplace_back(name->str());
    }
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<FieldByName>(resultTy, val, names, parentBlock);
}

template <>
RawArrayAllocate* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::RawArrayAllocate* obj)
{
    auto size = GetValue<Value>(obj->base()->operands()->Get(0));
    auto elementType = GetType<Type>(obj->elementType());
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<RawArrayAllocate>(resultTy, elementType, size, parentBlock);
}

template <>
RawArrayLiteralInit* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::RawArrayLiteralInit* obj)
{
    auto operands = GetValue<Value>(obj->base()->operands());
    auto raw = Ptr(operands[0]);
    auto elements = std::vector<Value*>(operands.begin() + 1, operands.end());
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<RawArrayLiteralInit>(resultTy, raw, elements, parentBlock);
}

template <>
RawArrayInitByValue* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::RawArrayInitByValue* obj)
{
    auto raw = GetValue<Value>(obj->base()->operands()->Get(0));
    auto size = GetValue<Value>(obj->base()->operands()->Get(1));
    auto initVal = GetValue<Value>(obj->base()->operands()->Get(2));
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<RawArrayInitByValue>(resultTy, raw, size, initVal, parentBlock);
}

template <> VArray* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::VArray* obj)
{
    auto operands = GetValue<Value>(obj->base()->operands());
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<VArray>(resultTy, operands, parentBlock);
}

template <> VArrayBuilder* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::VArrayBd* obj)
{
    auto size = GetValue<Value>(obj->base()->operands()->Get(0));
    auto item = GetValue<Value>(obj->base()->operands()->Get(1));
    auto initFunc = GetValue<Value>(obj->base()->operands()->Get(2));
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<VArrayBuilder>(resultTy, size, item, initFunc, parentBlock);
}

template <> GetException* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::GetException* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<GetException>(resultTy, parentBlock);
}

template <> Intrinsic* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Intrinsic* obj)
{
    auto kind = CHIR::IntrinsicKind(obj->intrinsicKind());
    auto args = GetValue<Value>(obj->base()->operands());
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto callContext = IntrisicCallContext {
        .kind = kind,
        .args = args,
        .instTypeArgs = GetType<Type>(obj->instantiatedTypeArgs())
    };
    return builder.CreateExpression<Intrinsic>(resultTy, callContext, parentBlock);
}

template <> If* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::If* obj)
{
    auto cond = GetValue<Value>(obj->base()->operands()->Get(0));
    auto thenBody = GetValue<BlockGroup>(obj->base()->blockGroups()->Get(0));
    auto elseBody = GetValue<BlockGroup>(obj->base()->blockGroups()->Get(1));
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<If>(resultTy, cond, thenBody, elseBody, parentBlock);
}

template <> Loop* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Loop* obj)
{
    auto loopBody = GetValue<BlockGroup>(obj->base()->blockGroups()->Get(0));
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<Loop>(resultTy, loopBody, parentBlock);
}

template <> ForInRange* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::ForInRange* obj)
{
    auto inductionVar = GetValue<Value>(obj->base()->operands()->Get(0));
    auto loopCondVar = GetValue<Value>(obj->base()->operands()->Get(1));
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<ForInRange>(resultTy, inductionVar, loopCondVar, parentBlock);
}

template <>
ForInClosedRange* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::ForInClosedRange* obj)
{
    auto inductionVar = GetValue<Value>(obj->base()->operands()->Get(0));
    auto loopCondVar = GetValue<Value>(obj->base()->operands()->Get(1));
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<ForInClosedRange>(
        resultTy, inductionVar, loopCondVar, parentBlock);
}

template <> ForInIter* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::ForInIter* obj)
{
    auto inductionVar = GetValue<Value>(obj->base()->operands()->Get(0));
    auto loopCondVar = GetValue<Value>(obj->base()->operands()->Get(1));
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<ForInIter>(resultTy, inductionVar, loopCondVar, parentBlock);
}

template <> Debug* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Debug* obj)
{
    CJC_ASSERT(obj->base()->operands()->size() == 1);
    auto local = GetValue<Value>(obj->base()->operands()->Get(0));
    auto srcCodeIdentifier = obj->srcCodeIdentifier()->str();
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto result = builder.CreateExpression<Debug>(resultTy, local, srcCodeIdentifier, parentBlock);
    return result;
}

template <> Spawn* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Spawn* obj)
{
    auto operands = GetValue<Value>(obj->base()->operands());
    auto val = operands[0];
    auto func = GetValue<FuncBase>(obj->executeClosure());
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    Spawn* spawn = nullptr;
    if (operands.size() > 1) {
        auto arg = operands[1];
        spawn = builder.CreateExpression<Spawn>(resultTy, val, arg, parentBlock);
    } else {
        spawn = builder.CreateExpression<Spawn>(resultTy, val, parentBlock);
    }
    if (func) {
        spawn->SetExecuteClosure(*func);
    }
    return spawn;
}

template <> Lambda* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Lambda* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    auto funcTy = GetType<FuncType>(obj->funcTy());
    auto isLocalFunc = obj->isLocalFunc();
    auto identifier = obj->identifier()->str();
    auto srcCodeIdentifier = obj->srcCodeIdentifier()->str();
    auto genericTypeParams = GetType<GenericType>(obj->genericTypeParams());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto lambda = builder.CreateExpression<Lambda>(
        resultTy, funcTy, parentBlock, isLocalFunc, identifier, srcCodeIdentifier, genericTypeParams);
    if (obj->isCompileTimeValue()) {
        lambda->SetCompileTimeValue();
    }
    return lambda;
}

// =========================== Terminator Deserializer ==============================

template <> GoTo* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::GoTo* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->base()->parentBlock());
    auto destination = GetValue<Block>(obj->base()->successors()->Get(0));
    return builder.CreateTerminator<GoTo>(destination, parentBlock);
}

template <> Branch* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Branch* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->base()->parentBlock());
    auto cond = GetValue<Value>(obj->base()->base()->operands()->Get(0));
    auto trueBlock = GetValue<Block>(obj->base()->successors()->Get(0));
    auto falseBlock = GetValue<Block>(obj->base()->successors()->Get(1));
    auto sourceExpr = CHIR::SourceExpr(obj->sourceExpr());
    auto result = builder.CreateTerminator<Branch>(cond, trueBlock, falseBlock, parentBlock);
    result->SetSourceExpr(sourceExpr);
    return result;
}

template <> MultiBranch* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::MultiBranch* obj)
{
    auto cond = GetValue<Value>(obj->base()->base()->operands()->Get(0));
    auto successors = GetValue<Block>(obj->base()->successors());
    auto defaultBlock = successors[0];
    auto succs = std::vector<Block*>(successors.begin() + 1, successors.end());
    CJC_NULLPTR_CHECK(obj->caseVals());
    auto caseVals = std::vector<uint64_t>(obj->caseVals()->begin(), obj->caseVals()->end());
    auto parentBlock = GetValue<Block>(obj->base()->base()->parentBlock());
    return builder.CreateTerminator<MultiBranch>(cond, defaultBlock, caseVals, succs, parentBlock);
}

template <> Exit* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Exit* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->base()->parentBlock());
    return builder.CreateTerminator<Exit>(parentBlock);
}

template <>
RaiseException* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::RaiseException* obj)
{
    auto value = GetValue<Value>(obj->base()->base()->operands()->Get(0));
    auto parentBlock = GetValue<Block>(obj->base()->base()->parentBlock());
    if (!obj->base()->successors() || obj->base()->successors()->size() == 0) {
        return builder.CreateTerminator<RaiseException>(value, parentBlock);
    } else {
        auto succs = GetValue<Block>(obj->base()->successors());
        CJC_ASSERT(succs.size() == 1);
        return builder.CreateTerminator<RaiseException>(value, succs[0], parentBlock);
    }
}

template <>
ApplyWithException* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::ApplyWithException* obj)
{
    auto operands = GetValue<Value>(obj->base()->base()->operands());
    auto callee = operands[0];
    auto args = std::vector<Value*>(operands.begin() + 1, operands.end());
    // Exceptions
    auto sucBlock = GetValue<Block>(obj->base()->successors()->Get(0));
    auto errBlock = GetValue<Block>(obj->base()->successors()->Get(1));
    auto parentBlock = GetValue<Block>(obj->base()->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->base()->resultTy());
    auto thisType = GetType<Type>(obj->thisType());
    auto instantiatedTypeArgs = GetType<Type>(obj->instantiatedTypeArgs());
    auto result = builder.CreateExpression<ApplyWithException>(resultTy, callee, FuncCallContext{
        .args = args,
        .instTypeArgs = instantiatedTypeArgs,
        .thisType = thisType}, sucBlock, errBlock, parentBlock);
    return result;
}

template <>
InvokeWithException* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::InvokeWithException* obj)
{
    auto operands = GetValue<Value>(obj->base()->base()->operands());
    auto args = std::vector<Value*>(operands.begin() + 1, operands.end());
    // Exceptions
    auto sucBlock = GetValue<Block>(obj->base()->successors()->Get(0));
    auto errBlock = GetValue<Block>(obj->base()->successors()->Get(1));
    auto parentBlock = GetValue<Block>(obj->base()->base()->parentBlock());
    auto resultTy = GetType<Type>(obj->base()->base()->resultTy());
    auto tempTypes = GetType<Type>(obj->virMethodCtx()->genericTypeParams());
    std::vector<GenericType*> genericTypes;
    for (auto ty : tempTypes) {
        genericTypes.emplace_back(Cangjie::StaticCast<GenericType*>(ty));
    }
    auto invokeInfo = InvokeCallContext {
        .caller = operands[0],
        .funcCallCtx = FuncCallContext {
            .args = std::vector<Value*>(operands.begin() + 1, operands.end()),
            .instTypeArgs = GetType<Type>(obj->instantiatedTypeArgs()),
            .thisType = GetType<Type>(obj->thisType())
        },
        .virMethodCtx = VirMethodContext {
            .srcCodeIdentifier = obj->virMethodCtx()->srcCodeIdentifier()->data(),
            .originalFuncType = GetType<FuncType>(obj->virMethodCtx()->originalFuncType()),
            .genericTypeParams = std::move(genericTypes)
        }
    };
    return builder.CreateExpression<InvokeWithException>(resultTy, invokeInfo, sucBlock, errBlock, parentBlock);
}

template <>
IntOpWithException* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::IntOpWithException* obj)
{
    auto opKind = obj->opKind();
    auto lhs = GetValue<Value>(obj->base()->base()->operands()->Get(0));
    auto resultTy = GetType<Type>(obj->base()->base()->resultTy());
    // Exceptions
    auto sucBlock = GetValue<Block>(obj->base()->successors()->Get(0));
    auto errBlock = GetValue<Block>(obj->base()->successors()->Get(1));
    auto parentBlock = GetValue<Block>(obj->base()->base()->parentBlock());
    if (obj->base()->base()->operands()->size() == 1) {
        return builder.CreateExpression<IntOpWithException>(
            resultTy, ExprKind(opKind), lhs, sucBlock, errBlock, parentBlock);
    }
    auto rhs = GetValue<Value>(obj->base()->base()->operands()->Get(1));
    auto intOpWithExcept = builder.CreateExpression<IntOpWithException>(
        resultTy, ExprKind(opKind), lhs, rhs, sucBlock, errBlock, parentBlock);
    return intOpWithExcept;
}

template <>
TypeCastWithException* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(
    const PackageFormat::TypeCastWithException* obj)
{
    auto operand = GetValue<Value>(obj->base()->base()->operands()->Get(0));
    auto resultTy = GetType<Type>(obj->base()->base()->resultTy());
    // Exceptions
    auto sucBlock = GetValue<Block>(obj->base()->successors()->Get(0));
    auto errBlock = GetValue<Block>(obj->base()->successors()->Get(1));
    auto parentBlock = GetValue<Block>(obj->base()->base()->parentBlock());
    return builder.CreateExpression<TypeCastWithException>(resultTy, operand, sucBlock, errBlock, parentBlock);
}

template <>
IntrinsicWithException* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(
    const PackageFormat::IntrinsicWithException* obj)
{
    auto kind = CHIR::IntrinsicKind(obj->intrinsicKind());
    auto args = GetValue<Value>(obj->base()->base()->operands());
    auto resultTy = GetType<Type>(obj->base()->base()->resultTy());
    // Exceptions
    auto sucBlock = GetValue<Block>(obj->base()->successors()->Get(0));
    auto errBlock = GetValue<Block>(obj->base()->successors()->Get(1));
    auto parentBlock = GetValue<Block>(obj->base()->base()->parentBlock());

    auto callContext = IntrisicCallContext {
        .kind = kind,
        .args = args,
        .instTypeArgs = GetType<Type>(obj->instantiatedTypeArgs())
    };
    return builder.CreateExpression<IntrinsicWithException>(resultTy, callContext, sucBlock, errBlock, parentBlock);
}

template <>
AllocateWithException* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(
    const PackageFormat::AllocateWithException* obj)
{
    auto targetType = GetType<Type>(obj->targetType());
    auto resultTy = GetType<Type>(obj->base()->base()->resultTy());
    // Exceptions
    auto sucBlock = GetValue<Block>(obj->base()->successors()->Get(0));
    auto errBlock = GetValue<Block>(obj->base()->successors()->Get(1));
    auto parentBlock = GetValue<Block>(obj->base()->base()->parentBlock());
    return builder.CreateExpression<AllocateWithException>(resultTy, targetType, sucBlock, errBlock, parentBlock);
}

template <>
RawArrayAllocateWithException* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(
    const PackageFormat::RawArrayAllocateWithException* obj)
{
    auto elementType = GetType<Type>(obj->elementType());
    auto size = GetValue<Value>(obj->base()->base()->operands()->Get(0));
    auto resultTy = GetType<Type>(obj->base()->base()->resultTy());
    // Exceptions
    auto sucBlock = GetValue<Block>(obj->base()->successors()->Get(0));
    auto errBlock = GetValue<Block>(obj->base()->successors()->Get(1));
    auto parentBlock = GetValue<Block>(obj->base()->base()->parentBlock());
    return builder.CreateExpression<RawArrayAllocateWithException>(
        resultTy, elementType, size, sucBlock, errBlock, parentBlock);
}

template <>
SpawnWithException* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::SpawnWithException* obj)
{
    auto operands = GetValue<Value>(obj->base()->base()->operands());
    auto val = operands[0];
    auto func = GetValue<FuncBase>(obj->executeClosure());
    auto resultTy = GetType<Type>(obj->base()->base()->resultTy());
    // Exceptions
    auto sucBlock = GetValue<Block>(obj->base()->successors()->Get(0));
    auto errBlock = GetValue<Block>(obj->base()->successors()->Get(1));
    auto parentBlock = GetValue<Block>(obj->base()->base()->parentBlock());
    SpawnWithException* spawn = nullptr;
    if (operands.size() > 1) {
        auto arg = operands[1];
        spawn = builder.CreateExpression<SpawnWithException>(
            resultTy, val, arg, sucBlock, errBlock, parentBlock);
    } else {
        spawn = builder.CreateExpression<SpawnWithException>(
            resultTy, val, sucBlock, errBlock, parentBlock);
    }
    if (func) {
        spawn->SetExecuteClosure(*func);
    }
    return spawn;
}

template <> GetRTTI* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::GetRTTI* obj)
{
    auto operands = GetValue<Value>(obj->base()->operands());
    auto val = operands[0];
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    return builder.CreateExpression<GetRTTI>(resultTy, val, parentBlock);
}

template <> GetRTTIStatic* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::GetRTTIStatic* obj)
{
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto rttiType = GetType<Type>(obj->rttiType());
    auto parentBlock = GetValue<Block>(obj->base()->parentBlock());
    return builder.CreateExpression<GetRTTIStatic>(resultTy, rttiType, parentBlock);
}

// =========================== Configuration ==============================

void CHIRDeserializer::CHIRDeserializerImpl::ConfigBase(const PackageFormat::Base* buffer, Base& obj)
{
    CJC_NULLPTR_CHECK(buffer);
    Base base;
    auto annos = buffer->annos();
    auto annoTypes = buffer->annos_type();
    for (unsigned i = 0; i < annos->size(); ++i) {
        auto anno = annos->Get(i);
        switch (annoTypes->Get(i)) {
            case PackageFormat::Annotation::Annotation_needCheckArrayBound:
                base.Set<NeedCheckArrayBound>(static_cast<const PackageFormat::NeedCheckArrayBound*>(anno)->need());
                break;
            case PackageFormat::Annotation::Annotation_needCheckCast:
                base.Set<NeedCheckCast>(static_cast<const PackageFormat::NeedCheckCast*>(anno)->need());
                break;
            case PackageFormat::Annotation::Annotation_debugLocationInfo:
                base.SetDebugLocation(Create<DebugLocation>(static_cast<const PackageFormat::DebugLocation*>(anno)));
                break;
            case PackageFormat::Annotation::Annotation_debugLocationInfoForWarning:
                base.Set<DebugLocationInfoForWarning>(
                    Create<DebugLocation>(static_cast<const PackageFormat::DebugLocation*>(anno)));
                break;
            case PackageFormat::Annotation::Annotation_linkTypeInfo:
                base.Set<CHIR::LinkTypeInfo>(
                    Cangjie::Linkage(static_cast<const PackageFormat::LinkTypeInfo*>(anno)->linkage()));
                break;
            case PackageFormat::Annotation::Annotation_skipCheck:
                base.Set<CHIR::SkipCheck>(
                    CHIR::SkipKind(static_cast<const PackageFormat::SkipCheck*>(anno)->skipKind()));
                break;
            case PackageFormat::Annotation::Annotation_neverOverflowInfo:
                base.Set<CHIR::NeverOverflowInfo>(
                    static_cast<const PackageFormat::NeverOverflowInfo*>(anno)->neverOverflow());
                break;
            case PackageFormat::Annotation::Annotation_generatedFromForIn:
                base.Set<CHIR::GeneratedFromForIn>(
                    static_cast<const PackageFormat::GeneratedFromForIn*>(anno)->value());
                break;
            case PackageFormat::Annotation::Annotation_isAutoEnvClass:
                base.Set<CHIR::IsAutoEnvClass>(static_cast<const PackageFormat::IsAutoEnvClass*>(anno)->value());
                break;
            case PackageFormat::Annotation::Annotation_isCapturedClassInCC:
                base.Set<CHIR::IsCapturedClassInCC>(
                    static_cast<const PackageFormat::IsCapturedClassInCC*>(anno)->value());
                break;
            case PackageFormat::Annotation::Annotation_enumCaseIndex: {
                int64_t index = static_cast<const PackageFormat::EnumCaseIndex*>(anno)->index();
                if (index != -1) {
                    base.Set<CHIR::EnumCaseIndex>(static_cast<size_t>(index));
                }
                break;
            }
            case PackageFormat::Annotation::Annotation_virMethodOffset: {
                int64_t offset = static_cast<const PackageFormat::VirMethodOffset*>(anno)->offset();
                if (offset != -1) {
                    base.Set<CHIR::VirMethodOffset>(static_cast<size_t>(offset));
                }
                break;
            }
            case PackageFormat::Annotation::Annotation_wrappedRawMethod:
                base.Set<CHIR::WrappedRawMethod>(
                    GetValue<FuncBase>(static_cast<const PackageFormat::WrappedRawMethod*>(anno)->rawMethod()));
                break;
            case PackageFormat::Annotation::Annotation_overrideSrcFuncType:
                base.Set<CHIR::OverrideSrcFuncType>(
                    GetType<FuncType>(static_cast<const PackageFormat::OverrideSrcFuncType*>(anno)->type()));
                break;
            default:
                continue;
        }
    }
    obj.CopyAnnotationMapFrom(base);
}

void CHIRDeserializer::CHIRDeserializerImpl::ConfigValue(const PackageFormat::Value* buffer, Value& obj)
{
    CJC_NULLPTR_CHECK(buffer);
    ConfigBase(buffer->base(), obj);
    obj.AppendAttributeInfo(CreateAttr(buffer->attributes()));
    if (buffer->identifier()) {
        obj.identifier = buffer->identifier()->str();
    }
}

void CHIRDeserializer::CHIRDeserializerImpl::ConfigCustomTypeDef(
    const PackageFormat::CustomTypeDef* buffer, CustomTypeDef& obj)
{
    CJC_NULLPTR_CHECK(buffer);
    ConfigBase(buffer->base(), obj);
    // kind is setted when create classDef/structDef...
    auto srcCodeIdentifiers = buffer->srcCodeIdentifier()->str();
    obj.srcCodeIdentifier = srcCodeIdentifiers;
    auto identifier = buffer->identifier()->str();
    obj.identifier = identifier;
    auto packageNames = buffer->packageName()->str();
    obj.packageName = packageNames;
    auto type = GetType<CustomType>(buffer->type());
    obj.type = type;
    if (buffer->genericDecl() != 0) {
        auto genericDecl = GetCustomTypeDef(buffer->genericDecl());
        CJC_NULLPTR_CHECK(genericDecl);
        obj.SetGenericDecl(*genericDecl);
    }

    auto declaredMethods = GetValue<FuncBase>(buffer->methods());
    for (auto declaredMethod : declaredMethods) {
        CJC_NULLPTR_CHECK(declaredMethod);
        obj.AddMethod(declaredMethod);
    }
    auto implementedInterfaces = GetType<ClassType>(buffer->implementedInterfaces());
    for (auto implementedInterface : implementedInterfaces) {
        CJC_NULLPTR_CHECK(implementedInterface);
        obj.AddImplementedInterfaceTy(*implementedInterface);
    }
    auto instanceMemberVars = Create<MemberVarInfo>(buffer->instanceMemberVars());
    for (auto var : instanceMemberVars) {
        obj.AddInstanceVar(var);
    }
    auto staticMemberVars = GetValue<GlobalVarBase>(buffer->staticMemberVars());
    for (auto var : staticMemberVars) {
        CJC_NULLPTR_CHECK(var);
        obj.AddStaticMemberVar(var);
    }
    obj.AppendAttributeInfo(CreateAttr(buffer->attributes()));
    if (compilePlatform) {
        obj.EnableAttr(Attribute::DESERIALIZED);
    }
    obj.SetAnnoInfo(Create<AnnoInfo>(buffer->annoInfo()));
    auto vtable =
        Create<VTableInDef, flatbuffers::Vector<flatbuffers::Offset<PackageFormat::VTableInType>>>(buffer->vtable());
    obj.SetVTable(std::move(vtable));
    auto varInitializationFunc = GetValue<FuncBase>(buffer->varInitializationFunc());
    if (varInitializationFunc) {
        obj.SetVarInitializationFunc(varInitializationFunc);
    }
}

void CHIRDeserializer::CHIRDeserializerImpl::ConfigExpression(const PackageFormat::Expression* buffer, Expression& obj)
{
    CJC_NULLPTR_CHECK(buffer);
    ConfigBase(buffer->base(), obj);
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::Lambda* buffer, Lambda& obj)
{
    CJC_NULLPTR_CHECK(buffer);
    ConfigExpression(buffer->base(), obj);
    // the parameter will be inserted into Lambda when Parameter is created.
    GetValue<Parameter>(buffer->params());
    if (auto *retVal = GetValue<LocalVar>(buffer->retVal()); retVal != nullptr) {
        obj.SetReturnValue(*retVal);
    }
}

template <>
void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::ForInRange* buffer, ForInRange& obj)
{
    CJC_NULLPTR_CHECK(buffer);
    CJC_NULLPTR_CHECK(buffer->base());
    ConfigExpression(buffer->base(), obj);
    auto body = GetValue<BlockGroup>(buffer->base()->blockGroups()->Get(0));
    auto latch = GetValue<BlockGroup>(buffer->base()->blockGroups()->Get(1));
    auto cond = GetValue<BlockGroup>(buffer->base()->blockGroups()->Get(2));
    CJC_NULLPTR_CHECK(body);
    CJC_NULLPTR_CHECK(latch);
    CJC_NULLPTR_CHECK(cond);
    obj.InitBlockGroups(*body, *latch, *cond);
}

template <>
void CHIRDeserializer::CHIRDeserializerImpl::Config(
    const PackageFormat::ForInClosedRange* buffer, ForInClosedRange& obj)
{
    CJC_NULLPTR_CHECK(buffer);
    CJC_NULLPTR_CHECK(buffer->base());
    ConfigExpression(buffer->base(), obj);
    auto body = GetValue<BlockGroup>(buffer->base()->blockGroups()->Get(0));
    auto latch = GetValue<BlockGroup>(buffer->base()->blockGroups()->Get(1));
    auto cond = GetValue<BlockGroup>(buffer->base()->blockGroups()->Get(2));
    CJC_NULLPTR_CHECK(body);
    CJC_NULLPTR_CHECK(latch);
    CJC_NULLPTR_CHECK(cond);
    obj.InitBlockGroups(*body, *latch, *cond);
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::ForInIter* buffer, ForInIter& obj)
{
    CJC_NULLPTR_CHECK(buffer);
    CJC_NULLPTR_CHECK(buffer->base());
    ConfigExpression(buffer->base(), obj);
    auto body = GetValue<BlockGroup>(buffer->base()->blockGroups()->Get(0));
    auto latch = GetValue<BlockGroup>(buffer->base()->blockGroups()->Get(1));
    auto cond = GetValue<BlockGroup>(buffer->base()->blockGroups()->Get(2));
    CJC_NULLPTR_CHECK(body);
    CJC_NULLPTR_CHECK(latch);
    CJC_NULLPTR_CHECK(cond);
    obj.InitBlockGroups(*body, *latch, *cond);
}

template <>
void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::ImportedFunc* buffer, ImportedFunc& obj)
{
    ConfigBase(buffer->base()->base()->base(), obj);
    if (buffer->paramDftValHostFunc() != 0) {
        auto paramDftValHostFunc = GetValue<FuncBase>(buffer->paramDftValHostFunc());
        CJC_NULLPTR_CHECK(paramDftValHostFunc);
        obj.SetParamDftValHostFunc(*paramDftValHostFunc);
    }
    if (buffer->genericDecl() != 0) {
        auto genericDecl = GetValue<FuncBase>(buffer->genericDecl());
        CJC_NULLPTR_CHECK(genericDecl);
        obj.SetGenericDecl(*genericDecl);
    }
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::Func* buffer, Func& obj)
{
    ConfigBase(buffer->base()->base(), obj);
    // the parameter will be inserted into Func when Parameter is created.
    auto params = GetValue<Parameter>(buffer->params());
    obj.RemoveParams();
    for (auto p : params) {
        CJC_NULLPTR_CHECK(p);
        obj.AddParam(*p);
    }
    auto body = GetValue<BlockGroup>(buffer->body());
    if (body) {
        obj.InitBody(*body);
    } else {
        // 'common' function can be without body
        CJC_ASSERT(obj.TestAttr(Attribute::COMMON));
    }
    obj.SetFuncKind(FuncKind(buffer->funcKind()));
    if (buffer->retVal() != 0) {
        obj.SetReturnValue(*GetValue<LocalVar>(buffer->retVal()));
    }
    obj.SetRawMangledName(buffer->rawMangledName()->str());
    obj.SetAnnoInfo(Create<AnnoInfo>(buffer->base()->annoInfo()));
    obj.SetParentRawMangledName(buffer->parentName()->str());
    obj.AppendAttributeInfo(CreateAttr(buffer->base()->attributes()));
    if (compilePlatform) {
        obj.EnableAttr(Attribute::DESERIALIZED);
    }
    obj.SetPropLocation(Create<DebugLocation>(buffer->propLoc()));
    if (buffer->originalLambdaFuncType() != 0) {
        FuncSigInfo funcSig;
        funcSig.funcName = obj.GetSrcCodeIdentifier();
        funcSig.funcType = GetType<FuncType>(buffer->originalLambdaFuncType());
        funcSig.genericTypeParams = GetType<GenericType>(buffer->originalLambdaGenericTypeParams());
        obj.SetOriginalLambdaInfo(funcSig);
    }

    if (buffer->paramDftValHostFunc() != 0) {
        auto paramDftValHostFunc = GetValue<FuncBase>(buffer->paramDftValHostFunc());
        CJC_NULLPTR_CHECK(paramDftValHostFunc);
        obj.SetParamDftValHostFunc(*paramDftValHostFunc);
    }
    obj.SetFastNative(buffer->isFastNative());
    obj.SetCFFIWrapper(buffer->isCFFIWrapper());
    if (auto* retVal = GetValue<LocalVar>(buffer->retVal()); retVal != nullptr) {
        obj.SetReturnValue(*retVal);
    }
    if (buffer->genericDecl() != 0) {
        auto genericDecl = GetValue<FuncBase>(buffer->genericDecl());
        CJC_NULLPTR_CHECK(genericDecl);
        obj.SetGenericDecl(*genericDecl);
    }
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::LocalVar* buffer, LocalVar& obj)
{
    ConfigValue(buffer->base(), obj);

    // set identifier for convenient comparision.
    obj.identifier = buffer->base()->identifier()->str();
    if (buffer->isRetVal()) {
        obj.SetRetValue();
    }
}

template <>
void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::BlockGroup* buffer, BlockGroup& obj)
{
    ConfigBase(buffer->base()->base(), obj);
    GetValue<Block>(buffer->blocks());
    obj.SetEntryBlock(GetValue<Block>(buffer->entryBlock()));
    obj.identifier = buffer->base()->identifier()->str();
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::Block* buffer, Block& obj)
{
    ConfigBase(buffer->base()->base(), obj);
    obj.AppendAttributeInfo(CreateAttr(buffer->base()->attributes()));
    if (buffer->isLandingPadBlock()) {
        obj.SetExceptions(GetType<ClassType>(buffer->exceptionCatchList()));
    }
    obj.AppendExpressions(GetExpression<Expression>(buffer->exprs()));
    obj.identifier = buffer->base()->identifier()->str();
    obj.predecessors.clear();
    obj.predecessors = GetValue<Block>(buffer->predecessors());
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::GlobalVar* buffer, GlobalVar& obj)
{
    ConfigBase(buffer->base()->base(), obj);
    obj.SetAnnoInfo(Create<AnnoInfo>(buffer->base()->annoInfo()));
    if (auto initializer = DynamicCast<LiteralValue*>(GetValue<Value>(buffer->defaultInitVal()))) {
        obj.SetInitializer(*initializer);
    } else if (auto initFunc = GetValue<Func>(buffer->associatedInitFunc())) {
        obj.SetInitFunc(*initFunc);
    }
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::EnumDef* buffer, EnumDef& obj)
{
    ConfigCustomTypeDef(buffer->base(), obj);
    for (auto info : Create<EnumCtorInfo>(buffer->ctors())) {
        obj.AddCtor(info);
    }
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::StructDef* buffer, StructDef& obj)
{
    ConfigCustomTypeDef(buffer->base(), obj);
    obj.SetCStruct(buffer->isCStruct());
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::ClassDef* buffer, ClassDef& obj)
{
    ConfigCustomTypeDef(buffer->base(), obj);
    obj.SetAnnotation(buffer->isAnnotation());
    auto superClass = GetType<ClassType>(buffer->superClass());
    if (superClass) {
        obj.SetSuperClassTy(*superClass);
    }
    if (buffer->abstractMethods()) {
        for (auto info : Create<AbstractMethodInfo>(buffer->abstractMethods())) {
            obj.AddAbstractMethod(info);
        }
    }
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::ExtendDef* buffer, ExtendDef& obj)
{
    ConfigCustomTypeDef(buffer->base(), obj);
    auto info = GetType<Type>(buffer->extendedType());
    if (info) {
        obj.SetExtendedType(*info);
    }
}

// =========================== Fetch by ID ==============================

Value* CHIRDeserializer::CHIRDeserializerImpl::GetValue(uint32_t id)
{
    if (id2Value.count(id) == 0) {
        switch (PackageFormat::ValueElem(pool->values_type()->Get(id - 1))) {
            case PackageFormat::ValueElem_BoolLiteral:
                id2Value[id] = Deserialize<BoolLiteral>(
                    static_cast<const PackageFormat::BoolLiteral*>(pool->values()->Get(id - 1)));
                ConfigValue(static_cast<const PackageFormat::BoolLiteral*>(pool->values()->Get(id - 1))->base()->base(),
                    *id2Value[id]);
                break;
            case PackageFormat::ValueElem_RuneLiteral:
                id2Value[id] = Deserialize<RuneLiteral>(
                    static_cast<const PackageFormat::RuneLiteral*>(pool->values()->Get(id - 1)));
                ConfigValue(static_cast<const PackageFormat::RuneLiteral*>(pool->values()->Get(id - 1))->base()->base(),
                    *id2Value[id]);
                break;
            case PackageFormat::ValueElem_StringLiteral:
                id2Value[id] = Deserialize<StringLiteral>(
                    static_cast<const PackageFormat::StringLiteral*>(pool->values()->Get(id - 1)));
                ConfigValue(
                    static_cast<const PackageFormat::StringLiteral*>(pool->values()->Get(id - 1))->base()->base(),
                    *id2Value[id]);
                break;
            case PackageFormat::ValueElem_IntLiteral:
                id2Value[id] =
                    Deserialize<IntLiteral>(static_cast<const PackageFormat::IntLiteral*>(pool->values()->Get(id - 1)));
                ConfigValue(static_cast<const PackageFormat::IntLiteral*>(pool->values()->Get(id - 1))->base()->base(),
                    *id2Value[id]);
                break;
            case PackageFormat::ValueElem_FloatLiteral:
                id2Value[id] = Deserialize<FloatLiteral>(
                    static_cast<const PackageFormat::FloatLiteral*>(pool->values()->Get(id - 1)));
                ConfigValue(
                    static_cast<const PackageFormat::FloatLiteral*>(pool->values()->Get(id - 1))->base()->base(),
                    *id2Value[id]);
                break;
            case PackageFormat::ValueElem_UnitLiteral:
                id2Value[id] = Deserialize<UnitLiteral>(
                    static_cast<const PackageFormat::UnitLiteral*>(pool->values()->Get(id - 1)));
                ConfigValue(static_cast<const PackageFormat::UnitLiteral*>(pool->values()->Get(id - 1))->base()->base(),
                    *id2Value[id]);
                break;
            case PackageFormat::ValueElem_NullLiteral:
                id2Value[id] = Deserialize<NullLiteral>(
                    static_cast<const PackageFormat::NullLiteral*>(pool->values()->Get(id - 1)));
                ConfigValue(static_cast<const PackageFormat::NullLiteral*>(pool->values()->Get(id - 1))->base()->base(),
                    *id2Value[id]);
                break;
            case PackageFormat::ValueElem_Parameter:
                id2Value[id] =
                    Deserialize<Parameter>(static_cast<const PackageFormat::Parameter*>(pool->values()->Get(id - 1)));
                ConfigValue(
                    static_cast<const PackageFormat::Parameter*>(pool->values()->Get(id - 1))->base(), *id2Value[id]);
                break;
            case PackageFormat::ValueElem_LocalVar:
                id2Value[id] =
                    Deserialize<LocalVar>(static_cast<const PackageFormat::LocalVar*>(pool->values()->Get(id - 1)));
                Config(static_cast<const PackageFormat::LocalVar*>(pool->values()->Get(id - 1)),
                    *StaticCast<LocalVar*>(id2Value[id]));
                break;
            case PackageFormat::ValueElem_GlobalVar:
                id2Value[id] =
                    Deserialize<GlobalVar>(static_cast<const PackageFormat::GlobalVar*>(pool->values()->Get(id - 1)));
                ConfigValue(static_cast<const PackageFormat::GlobalVar*>(pool->values()->Get(id - 1))->base(),
                    *id2Value[id]);
                break;
            case PackageFormat::ValueElem_Func:
                id2Value[id] = Deserialize<Func>(static_cast<const PackageFormat::Func*>(pool->values()->Get(id - 1)));
                break;
            case PackageFormat::ValueElem_ImportedVar:
                id2Value[id] = Deserialize<ImportedVar>(
                    static_cast<const PackageFormat::ImportedVar*>(pool->values()->Get(id - 1)));
                ConfigValue(static_cast<const PackageFormat::ImportedVar*>(pool->values()->Get(id - 1))->base()->base(),
                    *id2Value[id]);
                break;
            case PackageFormat::ValueElem_ImportedFunc:
                id2Value[id] = Deserialize<ImportedFunc>(
                    static_cast<const PackageFormat::ImportedFunc*>(pool->values()->Get(id - 1)));
                break;
            case PackageFormat::ValueElem_Block:
                id2Value[id] =
                    Deserialize<Block>(static_cast<const PackageFormat::Block*>(pool->values()->Get(id - 1)));
                ConfigValue(
                    static_cast<const PackageFormat::Block*>(pool->values()->Get(id - 1))->base(), *id2Value[id]);
                break;
            case PackageFormat::ValueElem_BlockGroup:
                id2Value[id] =
                    Deserialize<BlockGroup>(static_cast<const PackageFormat::BlockGroup*>(pool->values()->Get(id - 1)));
                ConfigValue(
                    static_cast<const PackageFormat::BlockGroup*>(pool->values()->Get(id - 1))->base(), *id2Value[id]);
                break;
            case PackageFormat::ValueElem_NONE:
                InternalError("Unsupported value type.");
                break;
        }
    }
    return id2Value[id];
}

template <typename T> T* CHIRDeserializer::CHIRDeserializerImpl::GetValue(uint32_t id)
{
    if (id == 0) {
        return nullptr;
    }
    return DynamicCast<T*>(GetValue(id));
}

Type* CHIRDeserializer::CHIRDeserializerImpl::GetType(uint32_t id)
{
    if (id2Type.count(id) == 0) {
        switch (PackageFormat::TypeElem(pool->types_type()->Get(id - 1))) {
            case PackageFormat::TypeElem_RuneType:
                id2Type[id] =
                    Deserialize<RuneType>(static_cast<const PackageFormat::RuneType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_BooleanType:
                id2Type[id] = Deserialize<BooleanType>(
                    static_cast<const PackageFormat::BooleanType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_UnitType:
                id2Type[id] =
                    Deserialize<UnitType>(static_cast<const PackageFormat::UnitType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_NothingType:
                id2Type[id] = Deserialize<NothingType>(
                    static_cast<const PackageFormat::NothingType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_IntType:
                id2Type[id] =
                    Deserialize<IntType>(static_cast<const PackageFormat::IntType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_FloatType:
                id2Type[id] =
                    Deserialize<FloatType>(static_cast<const PackageFormat::FloatType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_TupleType:
                id2Type[id] =
                    Deserialize<TupleType>(static_cast<const PackageFormat::TupleType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_RawArrayType:
                id2Type[id] = Deserialize<RawArrayType>(
                    static_cast<const PackageFormat::RawArrayType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_VArrayType:
                id2Type[id] =
                    Deserialize<VArrayType>(static_cast<const PackageFormat::VArrayType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_FuncType:
                id2Type[id] =
                    Deserialize<FuncType>(static_cast<const PackageFormat::FuncType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_CustomType:
                id2Type[id] =
                    Deserialize<CustomType>(static_cast<const PackageFormat::CustomType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_EnumType:
                id2Type[id] =
                    Deserialize<EnumType>(static_cast<const PackageFormat::EnumType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_StructType:
                id2Type[id] =
                    Deserialize<StructType>(static_cast<const PackageFormat::StructType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_ClassType:
                id2Type[id] =
                    Deserialize<ClassType>(static_cast<const PackageFormat::ClassType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_CStringType:
                id2Type[id] = Deserialize<CStringType>(
                    static_cast<const PackageFormat::CStringType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_CPointerType:
                id2Type[id] = Deserialize<CPointerType>(
                    static_cast<const PackageFormat::CPointerType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_GenericType:
                id2Type[id] = Deserialize<GenericType>(
                    static_cast<const PackageFormat::GenericType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_RefType:
                id2Type[id] =
                    Deserialize<RefType>(static_cast<const PackageFormat::RefType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_BoxType:
                id2Type[id] =
                    Deserialize<BoxType>(static_cast<const PackageFormat::BoxType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_VoidType:
                id2Type[id] =
                    Deserialize<VoidType>(static_cast<const PackageFormat::VoidType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_ThisType:
                id2Type[id] =
                    Deserialize<ThisType>(static_cast<const PackageFormat::ThisType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_NONE:
                id2Type[id] = nullptr;
                break;
        }
    }
    return id2Type[id];
}

template <typename T> T* CHIRDeserializer::CHIRDeserializerImpl::GetType(uint32_t id)
{
    if (id == 0) {
        return nullptr;
    }
    return StaticCast<T*>(GetType(id));
}

Expression* CHIRDeserializer::CHIRDeserializerImpl::GetExpression(uint32_t id)
{
    if (id2Expression.count(id) == 0) {
        switch (PackageFormat::ExpressionElem(pool->exprs_type()->Get(id - 1))) {
            case PackageFormat::ExpressionElem_UnaryExpression:
                id2Expression[id] = Deserialize<UnaryExpression>(
                    static_cast<const PackageFormat::UnaryExpression*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::UnaryExpression*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_BinaryExpression:
                id2Expression[id] = Deserialize<BinaryExpression>(
                    static_cast<const PackageFormat::BinaryExpression*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::BinaryExpression*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Constant:
                id2Expression[id] =
                    Deserialize<Constant>(static_cast<const PackageFormat::Constant*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::Constant*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Allocate:
                id2Expression[id] =
                    Deserialize<Allocate>(static_cast<const PackageFormat::Allocate*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::Allocate*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Load:
                id2Expression[id] = Deserialize<Cangjie::CHIR::Load>(
                    static_cast<const PackageFormat::Load*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::Load*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Store:
                id2Expression[id] =
                    Deserialize<Store>(static_cast<const PackageFormat::Store*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::Store*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_GetElementRef:
                id2Expression[id] = Deserialize<GetElementRef>(
                    static_cast<const PackageFormat::GetElementRef*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::GetElementRef*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_GetElementByName:
                id2Expression[id] = Deserialize<GetElementByName>(
                    static_cast<const PackageFormat::GetElementByName*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::GetElementByName*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_StoreElementRef:
                id2Expression[id] = Deserialize<StoreElementRef>(
                    static_cast<const PackageFormat::StoreElementRef*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::StoreElementRef*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_StoreElementByName:
                id2Expression[id] = Deserialize<StoreElementByName>(
                    static_cast<const PackageFormat::StoreElementByName*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::StoreElementByName*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Apply:
                id2Expression[id] =
                    Deserialize<Apply>(static_cast<const PackageFormat::Apply*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::Apply*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Invoke:
                id2Expression[id] =
                    Deserialize<Invoke>(static_cast<const PackageFormat::Invoke*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::Invoke*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_TypeCast:
                id2Expression[id] =
                    Deserialize<TypeCast>(static_cast<const PackageFormat::TypeCast*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::TypeCast*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_InstanceOf:
                id2Expression[id] =
                    Deserialize<InstanceOf>(static_cast<const PackageFormat::InstanceOf*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::InstanceOf*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Box:
                id2Expression[id] =
                    Deserialize<Box>(static_cast<const PackageFormat::Box*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::Box*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_UnBox:
                id2Expression[id] =
                    Deserialize<UnBox>(static_cast<const PackageFormat::UnBox*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::UnBox*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_GoTo:
                id2Expression[id] =
                    Deserialize<GoTo>(static_cast<const PackageFormat::GoTo*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::GoTo*>(pool->exprs()->Get(id - 1))->base()->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Branch:
                id2Expression[id] =
                    Deserialize<Branch>(static_cast<const PackageFormat::Branch*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::Branch*>(pool->exprs()->Get(id - 1))->base()->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_MultiBranch:
                id2Expression[id] = Deserialize<MultiBranch>(
                    static_cast<const PackageFormat::MultiBranch*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::MultiBranch*>(pool->exprs()->Get(id - 1))->base()->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Exit:
                id2Expression[id] =
                    Deserialize<Exit>(static_cast<const PackageFormat::Exit*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::Exit*>(pool->exprs()->Get(id - 1))->base()->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_RaiseException:
                id2Expression[id] = Deserialize<RaiseException>(
                    static_cast<const PackageFormat::RaiseException*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::RaiseException*>(pool->exprs()->Get(id - 1))
                               ->base()
                               ->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_ApplyWithException:
                id2Expression[id] = Deserialize<ApplyWithException>(
                    static_cast<const PackageFormat::ApplyWithException*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::ApplyWithException*>(pool->exprs()->Get(id - 1))
                               ->base()
                               ->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_InvokeWithException:
                id2Expression[id] = Deserialize<InvokeWithException>(
                    static_cast<const PackageFormat::InvokeWithException*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::InvokeWithException*>(pool->exprs()->Get(id - 1))
                               ->base()
                               ->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_InvokeStatic:
                id2Expression[id] = Deserialize<InvokeStatic>(
                    static_cast<const PackageFormat::InvokeStatic*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::InvokeStatic*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_InvokeStaticWithException:
                id2Expression[id] = Deserialize<InvokeStaticWithException>(
                    static_cast<const PackageFormat::InvokeStaticWithException*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::InvokeStaticWithException*>(pool->exprs()->Get(id - 1))
                               ->base()
                               ->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_IntOpWithException:
                id2Expression[id] = Deserialize<IntOpWithException>(
                    static_cast<const PackageFormat::IntOpWithException*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::IntOpWithException*>(pool->exprs()->Get(id - 1))
                               ->base()
                               ->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_TypeCastWithException:
                id2Expression[id] = Deserialize<TypeCastWithException>(
                    static_cast<const PackageFormat::TypeCastWithException*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::TypeCastWithException*>(pool->exprs()->Get(id - 1))
                               ->base()
                               ->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_IntrinsicWithException:
                id2Expression[id] = Deserialize<IntrinsicWithException>(
                    static_cast<const PackageFormat::IntrinsicWithException*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::IntrinsicWithException*>(pool->exprs()->Get(id - 1))
                               ->base()
                               ->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_AllocateWithException:
                id2Expression[id] = Deserialize<AllocateWithException>(
                    static_cast<const PackageFormat::AllocateWithException*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::AllocateWithException*>(pool->exprs()->Get(id - 1))
                               ->base()
                               ->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_RawArrayAllocateWithException:
                id2Expression[id] = Deserialize<RawArrayAllocateWithException>(
                    static_cast<const PackageFormat::RawArrayAllocateWithException*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::RawArrayAllocateWithException*>(pool->exprs()->Get(id - 1))
                               ->base()
                               ->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_SpawnWithException:
                id2Expression[id] = Deserialize<SpawnWithException>(
                    static_cast<const PackageFormat::SpawnWithException*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::SpawnWithException*>(pool->exprs()->Get(id - 1))
                               ->base()
                               ->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Tuple:
                id2Expression[id] =
                    Deserialize<Tuple>(static_cast<const PackageFormat::Tuple*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::Tuple*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Field:
                id2Expression[id] =
                    Deserialize<Field>(static_cast<const PackageFormat::Field*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::Field*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_FieldByName:
                id2Expression[id] = Deserialize<FieldByName>(
                    static_cast<const PackageFormat::FieldByName*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::FieldByName*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_RawArrayAllocate:
                id2Expression[id] = Deserialize<RawArrayAllocate>(
                    static_cast<const PackageFormat::RawArrayAllocate*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::RawArrayAllocate*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_RawArrayLiteralInit:
                id2Expression[id] = Deserialize<RawArrayLiteralInit>(
                    static_cast<const PackageFormat::RawArrayLiteralInit*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::RawArrayLiteralInit*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_RawArrayInitByValue:
                id2Expression[id] = Deserialize<RawArrayInitByValue>(
                    static_cast<const PackageFormat::RawArrayInitByValue*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::RawArrayInitByValue*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_VArray:
                id2Expression[id] =
                    Deserialize<VArray>(static_cast<const PackageFormat::VArray*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::VArray*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_VArrayBd:
                id2Expression[id] =
                    Deserialize<VArrayBuilder>(static_cast<const PackageFormat::VArrayBd*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::VArrayBd*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_GetException:
                id2Expression[id] = Deserialize<GetException>(
                    static_cast<const PackageFormat::GetException*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::GetException*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Intrinsic:
                id2Expression[id] =
                    Deserialize<Intrinsic>(static_cast<const PackageFormat::Intrinsic*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::Intrinsic*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_If:
                id2Expression[id] = Deserialize<If>(static_cast<const PackageFormat::If*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::If*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Loop:
                id2Expression[id] =
                    Deserialize<Loop>(static_cast<const PackageFormat::Loop*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::Loop*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_ForInRange:
                id2Expression[id] =
                    Deserialize<ForInRange>(static_cast<const PackageFormat::ForInRange*>(pool->exprs()->Get(id - 1)));
                Config(static_cast<const PackageFormat::ForInRange*>(pool->exprs()->Get(id - 1)),
                    *GetExpression<ForInRange>(id));
                break;
            case PackageFormat::ExpressionElem_ForInIter:
                id2Expression[id] =
                    Deserialize<ForInIter>(static_cast<const PackageFormat::ForInIter*>(pool->exprs()->Get(id - 1)));
                Config(static_cast<const PackageFormat::ForInIter*>(pool->exprs()->Get(id - 1)),
                    *GetExpression<ForInIter>(id));
                break;
            case PackageFormat::ExpressionElem_ForInClosedRange:
                id2Expression[id] = Deserialize<ForInClosedRange>(
                    static_cast<const PackageFormat::ForInClosedRange*>(pool->exprs()->Get(id - 1)));
                Config(static_cast<const PackageFormat::ForInClosedRange*>(pool->exprs()->Get(id - 1)),
                    *GetExpression<ForInClosedRange>(id));
                break;
            case PackageFormat::ExpressionElem_Debug:
                id2Expression[id] =
                    Deserialize<Debug>(static_cast<const PackageFormat::Debug*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::Debug*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Spawn:
                id2Expression[id] =
                    Deserialize<Spawn>(static_cast<const PackageFormat::Spawn*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::Spawn*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Lambda:
                id2Expression[id] =
                    Deserialize<Lambda>(static_cast<const PackageFormat::Lambda*>(pool->exprs()->Get(id - 1)));
                Config(
                    static_cast<const PackageFormat::Lambda*>(pool->exprs()->Get(id - 1)), *GetExpression<Lambda>(id));
                break;
            case PackageFormat::ExpressionElem_GetInstantiateValue:
                id2Expression[id] = Deserialize<GetInstantiateValue>(
                    static_cast<const PackageFormat::GetInstantiateValue*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::GetInstantiateValue*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_TransformToConcrete:
                id2Expression[id] = Deserialize<TransformToConcrete>(
                    static_cast<const PackageFormat::TransformToConcrete*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::TransformToConcrete*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_TransformToGeneric:
                id2Expression[id] = Deserialize<TransformToGeneric>(
                    static_cast<const PackageFormat::TransformToGeneric*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::TransformToGeneric*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_UnBoxToRef:
                id2Expression[id] =
                    Deserialize<UnBoxToRef>(static_cast<const PackageFormat::UnBoxToRef*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(static_cast<const PackageFormat::UnBoxToRef*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_GetRTTI: {
                auto v = static_cast<const PackageFormat::GetRTTI*>(pool->exprs()->Get(id - 1));
                id2Expression[id] = Deserialize<GetRTTI>(v);
                ConfigExpression(v->base(), *id2Expression[id]);
                break;
            }
            case PackageFormat::ExpressionElem_GetRTTIStatic: {
                auto v = static_cast<const PackageFormat::GetRTTIStatic*>(pool->exprs()->Get(id - 1));
                id2Expression[id] = Deserialize<GetRTTIStatic>(v);
                ConfigExpression(v->base(), *id2Expression[id]);
                break;
            }
            case PackageFormat::ExpressionElem_NONE:
                InternalError("unsupported expression type in chir deserialization.");
                break;
        }
    }
    return id2Expression[id];
}

template <typename T> T* CHIRDeserializer::CHIRDeserializerImpl::GetExpression(uint32_t id)
{
    if (id == 0) {
        return nullptr;
    }
    return StaticCast<T*>(GetExpression(id));
}

CustomTypeDef* CHIRDeserializer::CHIRDeserializerImpl::GetCustomTypeDef(uint32_t id)
{
    if (id2CustomTypeDef.count(id) == 0) {
        switch (PackageFormat::CustomTypeDefElem(pool->defs_type()->Get(id - 1))) {
            case PackageFormat::CustomTypeDefElem_EnumDef:
                id2CustomTypeDef[id] =
                    Deserialize<EnumDef>(static_cast<const PackageFormat::EnumDef*>(pool->defs()->Get(id - 1)));
                break;
            case PackageFormat::CustomTypeDefElem_StructDef:
                id2CustomTypeDef[id] =
                    Deserialize<StructDef>(static_cast<const PackageFormat::StructDef*>(pool->defs()->Get(id - 1)));
                break;
            case PackageFormat::CustomTypeDefElem_ClassDef:
                id2CustomTypeDef[id] =
                    Deserialize<ClassDef>(static_cast<const PackageFormat::ClassDef*>(pool->defs()->Get(id - 1)));
                break;
            case PackageFormat::CustomTypeDefElem_ExtendDef:
                id2CustomTypeDef[id] =
                    Deserialize<ExtendDef>(static_cast<const PackageFormat::ExtendDef*>(pool->defs()->Get(id - 1)));
                break;
            case PackageFormat::CustomTypeDefElem_NONE:
                CJC_ABORT();
                id2CustomTypeDef[id] = nullptr;
                break;
        }
    }
    return id2CustomTypeDef[id];
}

template <typename T> T* CHIRDeserializer::CHIRDeserializerImpl::GetCustomTypeDef(uint32_t id)
{
    if (id == 0) {
        return nullptr;
    }
    return StaticCast<T*>(GetCustomTypeDef(id));
}

void CHIRDeserializer::CHIRDeserializerImpl::ResetImportedValuesUnderPackage()
{
    CJC_NULLPTR_CHECK(pool);
    auto package = pool;
    std::vector<ImportedValue*> importedVarAndFuncs;        // store import var and decls
    for (uint32_t i = 1; i <= package->maxImportedValueId(); i++) {
        auto imported = GetValue<ImportedValue>(i);
        CJC_NULLPTR_CHECK(imported);
        importedVarAndFuncs.emplace_back(imported);
    }

    builder.GetCurPackage()->SetImportedVarAndFuncs(std::move(importedVarAndFuncs));
}

void CHIRDeserializer::CHIRDeserializerImpl::ResetImportedDefsUnderPackage()
{
    CJC_NULLPTR_CHECK(pool);
    auto package = pool;
    std::vector<StructDef*> importedStructs;
    std::vector<ClassDef*> importedClasses;
    std::vector<EnumDef*> importedEnums;
    std::vector<ExtendDef*> importedExtends;
    for (uint32_t i = 1; i <= package->maxImportedStructId(); i++) {
        auto def = GetCustomTypeDef<StructDef>(i);
        CJC_NULLPTR_CHECK(def);
        importedStructs.emplace_back(def);
    }
    for (uint32_t i = package->maxImportedStructId() + 1; i <= package->maxImportedClassId(); i++) {
        auto def = GetCustomTypeDef<ClassDef>(i);
        CJC_NULLPTR_CHECK(def);
        importedClasses.emplace_back(def);
    }
    for (uint32_t i = package->maxImportedClassId() + 1; i <= package->maxImportedEnumId(); i++) {
        auto def = GetCustomTypeDef<EnumDef>(i);
        CJC_NULLPTR_CHECK(def);
        importedEnums.emplace_back(def);
    }
    for (uint32_t i = package->maxImportedEnumId() + 1; i <= package->maxImportedExtendId(); i++) {
        auto def = GetCustomTypeDef<ExtendDef>(i);
        CJC_NULLPTR_CHECK(def);
        importedExtends.emplace_back(def);
    }
    builder.GetCurPackage()->SetImportedStructs(std::move(importedStructs));
    builder.GetCurPackage()->SetImportedClasses(std::move(importedClasses));
    builder.GetCurPackage()->SetImportedEnums(std::move(importedEnums));
    builder.GetCurPackage()->SetImportedExtends(std::move(importedExtends));
}

// =========================== Entry ==================================
void CHIRDeserializer::CHIRDeserializerImpl::Run(const PackageFormat::CHIRPackage* package)
{
    pool = package;
    builder.CreatePackage(pool->name()->str());
    builder.GetCurPackage()->SetPackageAccessLevel(Package::AccessLevel(pool->pkgAccessLevel()));
    // To keep order, get CustomTypeDef first
    for (unsigned id = 1; id <= pool->defs()->size(); ++id) {
        GetCustomTypeDef<CustomTypeDef>(id);
    }

    // deserialize top level and local var for order
    for (unsigned id = 1; id <= pool->values()->size(); ++id) {
        auto valueElemKind = PackageFormat::ValueElem(pool->values_type()->Get(id - 1));
        if (valueElemKind != PackageFormat::ValueElem_Func && valueElemKind != PackageFormat::ValueElem_ImportedFunc &&
            valueElemKind != PackageFormat::ValueElem_GlobalVar &&
            valueElemKind != PackageFormat::ValueElem_ImportedVar &&
            valueElemKind != PackageFormat::ValueElem_LocalVar) {
            continue;
        }
        // NOTE: GetValue also save result in Package via Builder
        GetValue<Value>(id);
    }

    for (unsigned id = 1; id <= pool->values()->size(); ++id) {
        // lazy config Func to keep order
        switch (pool->values_type()->Get(id - 1)) {
            case PackageFormat::ValueElem_Block:
                Config(static_cast<const PackageFormat::Block*>(pool->values()->Get(id - 1)), *GetValue<Block>(id));
                break;
            case PackageFormat::ValueElem_BlockGroup:
                Config(static_cast<const PackageFormat::BlockGroup*>(pool->values()->Get(id - 1)),
                    *GetValue<BlockGroup>(id));
                break;
            case PackageFormat::ValueElem_GlobalVar:
                Config(static_cast<const PackageFormat::GlobalVar*>(pool->values()->Get(id - 1)),
                    *GetValue<GlobalVar>(id));
                break;
            case PackageFormat::ValueElem_Func:
                Config(static_cast<const PackageFormat::Func*>(pool->values()->Get(id - 1)), *GetValue<Func>(id));
                break;
            case PackageFormat::ValueElem_ImportedFunc:
                Config(static_cast<const PackageFormat::ImportedFunc*>(pool->values()->Get(id - 1)),
                    *GetValue<ImportedFunc>(id));
                break;
            default:
                // do nothing
                break;
        }
    }

    // Config CustomTypeDef
    for (unsigned id = 1; id <= pool->defs()->size(); ++id) {
        switch (pool->defs_type()->Get(id - 1)) {
            case PackageFormat::CustomTypeDefElem_EnumDef:
                Config(static_cast<const PackageFormat::EnumDef*>(pool->defs()->Get(id - 1)),
                    *GetCustomTypeDef<EnumDef>(id));
                break;
            case PackageFormat::CustomTypeDefElem_StructDef:
                Config(static_cast<const PackageFormat::StructDef*>(pool->defs()->Get(id - 1)),
                    *GetCustomTypeDef<StructDef>(id));
                break;
            case PackageFormat::CustomTypeDefElem_ClassDef:
                Config(static_cast<const PackageFormat::ClassDef*>(pool->defs()->Get(id - 1)),
                    *GetCustomTypeDef<ClassDef>(id));
                break;
            case PackageFormat::CustomTypeDefElem_ExtendDef:
                Config(static_cast<const PackageFormat::ExtendDef*>(pool->defs()->Get(id - 1)),
                    *GetCustomTypeDef<ExtendDef>(id));
                break;
            default:
                break;
        }
    }

    // Config generic types
    for (unsigned i = 0; i < genericTypeConfig.size(); i++) {
        // for-loop is used because genericTypeConfig can be modified on the fly
        auto genericType = genericTypeConfig[i].first;
        auto rawGenericType = genericTypeConfig[i].second;
        // Fill in sub-fields
        auto upperBounds = GetType<Type>(rawGenericType->upperBounds());
        genericType->SetUpperBounds(upperBounds);
    }
    // Package self's member
    builder.GetCurPackage()->SetPackageInitFunc(GetValue<Func>(pool->packageInitFunc()));
    auto* reInitFunc = GetValue<Func>(pool->packageLiteralInitFunc());
    CJC_ASSERT(reInitFunc != nullptr);
    builder.GetCurPackage()->SetPackageLiteralInitFunc(reInitFunc);
    // Reset Imported values and defs under Package
    ResetImportedValuesUnderPackage();
    ResetImportedDefsUnderPackage();
}
