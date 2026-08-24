// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <algorithm>
#include <cstddef>
#include <type_traits>

#include "cangjie/CHIR/IR/Expression/Expression.h"
#include "cangjie/CHIR/IR/Value/LiteralValue.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/IR/Annotation.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/IR/Type/EnumDef.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/Utils/UserDefinedType.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/ICEUtil.h"
#include "cangjie/Basic/Version.h"
#include "CHIRDeserializerImpl.h"
#include "flatbuffers/PackageFormat_generated.h"
#include "cangjie/CHIR/Serializer/CHIRDeserializer.h"

using namespace Cangjie::CHIR;

namespace {
Cangjie::OverflowStrategy DeserializeOverflowStrategy(PackageFormat::OverflowStrategy strategy)
{
    using Cangjie::OverflowStrategy;
    switch (strategy) {
        case PackageFormat::OverflowStrategy_NA:
            return OverflowStrategy::NA;
        case PackageFormat::OverflowStrategy_WRAPPING:
            return OverflowStrategy::WRAPPING;
        case PackageFormat::OverflowStrategy_THROWING:
            return OverflowStrategy::THROWING;
        case PackageFormat::OverflowStrategy_SATURATING:
            return OverflowStrategy::SATURATING;
        default:
            CJC_ABORT();
            return OverflowStrategy::NA;
    }
}
} // namespace

// explicit specialization
template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::EnumDef* buffer, EnumDef& obj);
template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::StructDef* buffer, StructDef& obj);
template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::ClassDef* buffer, ClassDef& obj);
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

bool CHIRDeserializer::Deserialize(uint8_t* data, int64_t size, CHIRBuilder& chirBuilder)
{
    bool succeed = true;
#ifndef CANGJIE_ENABLE_GCOV
    try {
#endif
        // Disable max depth and max tables verification.
        flatbuffers::Verifier::Options options;
        options.max_depth = std::numeric_limits<::flatbuffers::uoffset_t>::max();
        options.max_tables = std::numeric_limits<::flatbuffers::uoffset_t>::max();
        flatbuffers::Verifier verifier(data, static_cast<size_t>(size), options);
        if (!verifier.VerifyBuffer<PackageFormat::CHIRPackage>()) {
            printf("validation of 'CHIRPackage' failed\n");
            return false;
        }
        const PackageFormat::CHIRPackage* package = PackageFormat::GetCHIRPackage(data);
        CHIRDeserializerImpl deserializer(chirBuilder, false);
        deserializer.Run(package);
#ifndef CANGJIE_ENABLE_GCOV
    } catch (...) {
        succeed = false;
    }
#endif
    return succeed;
}

template <typename T, typename FBT>
std::vector<T> CHIRDeserializer::CHIRDeserializerImpl::Create(const flatbuffers::Vector<FBT>* vec)
{
    if (vec == nullptr) {
        return {};
    }
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
    if (identifier[0] == '@') {
        return identifier.substr(1);
    } else {
        return identifier;
    }
}

std::set<std::string> GetFeatures(const PackageFormat::GlobalValue* val)
{
    std::set<std::string> features = {};
    if (val->features() == nullptr) {
        return features;
    }
    for (unsigned int i = 0; i < val->features()->size(); ++i) {
        features.insert(val->features()->Get(i)->str());
    }
    
    return features;
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
    CJC_NULLPTR_CHECK(obj);
    CJC_NULLPTR_CHECK(obj->mangledName());
    std::string mangledName = obj->mangledName()->str();
    std::vector<CustomAnnoInstance> instances;
    if (obj->annoInstances()) {
        for (auto inst : *obj->annoInstances()) {
            CJC_NULLPTR_CHECK(inst);
            std::vector<std::string> argValues;
            if (inst->argValues()) {
                for (auto arg : *inst->argValues()) {
                    argValues.push_back(arg->str());
                }
            }
            DebugLocation loc;
            if (inst->loc()) {
                loc = Create<DebugLocation>(inst->loc());
            }
            std::string className = inst->annoClassName() ? inst->annoClassName()->str() : std::string{};
            instances.emplace_back(className, std::move(argValues), loc);
        }
    }
    return AnnoInfo(mangledName, std::move(instances));
}

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
    auto initializerFunc = GetValue<Function>(obj->initializerFunc());
    auto outerDef = GetCustomTypeDef<CustomTypeDef>(obj->outerDef());
    return MemberVarInfo{name, rawMangledName, type, attributeInfo, loc, annoInfo, initializerFunc, outerDef};
}

template <> EnumCtorInfo CHIRDeserializer::CHIRDeserializerImpl::Create(const PackageFormat::EnumCtorInfo* obj)
{
    auto name = obj->srcCodeName()->str();
    auto mangledName = obj->mangledName()->str();
    auto funcType = GetType<FuncType>(obj->funcType());
    return EnumCtorInfo{name, mangledName, funcType};
}

template <>
VirtualMethodInfo CHIRDeserializer::CHIRDeserializerImpl::Create(const PackageFormat::VirtualMethodInfo* obj)
{
    auto condition = FuncSigInfo {
        .funcName = obj->funcName()->str(),
        .funcType = GetType<FuncType>(obj->sigType()),
        .genericTypeParams = GetType<GenericType>(obj->methodGenericTypeParams())
    };
    auto funcPtr = GetValue<Function>(obj->instance());
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
    if (obj == nullptr) {
        return vtableInDef;
    }
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
    auto identifier = obj->base()->identifier()->str();
    if (compilePlatform) {
        if (auto exist = builder.GetCurPackage()->TryGetEnumDef(identifier)) {
            exist->EnableAttr(Attribute::PREVIOUSLY_DESERIALIZED);
            return exist;
        }
    }
    auto srcCodeIdentifier = obj->base()->srcCodeIdentifier()->str();
    auto packageName = obj->base()->packageName()->str();
    auto attrs = CreateAttr(obj->base()->base()->attributes());
    auto imported = attrs.TestAttr(CHIR::Attribute::IMPORTED);
    auto result = builder.CreateEnum(DebugLocation(), srcCodeIdentifier, GetMangleNameFromIdentifier(identifier),
        packageName, imported, obj->nonExhaustive());
    if (compilePlatform) {
        result->EnableAttr(CHIR::Attribute::DESERIALIZED);
    }
    return result;
}

template <> StructDef* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::StructDef* obj)
{
    auto identifier = obj->base()->identifier()->str();
    if (compilePlatform) {
        if (auto exist = builder.GetCurPackage()->TryGetStructDef(identifier)) {
            exist->EnableAttr(Attribute::PREVIOUSLY_DESERIALIZED);
            return exist;
        }
    }
    auto srcCodeIdentifier = obj->base()->srcCodeIdentifier()->str();
    auto packageName = obj->base()->packageName()->str();
    auto attrs = CreateAttr(obj->base()->base()->attributes());
    auto imported = attrs.TestAttr(CHIR::Attribute::IMPORTED);
    auto result = builder.CreateStruct(
        DebugLocation(), srcCodeIdentifier, GetMangleNameFromIdentifier(identifier), packageName, imported);
    if (compilePlatform) {
        result->EnableAttr(CHIR::Attribute::DESERIALIZED);
    }
    return result;
}

template <> ClassDef* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::ClassDef* obj)
{
    auto identifier = obj->base()->identifier()->str();
    if (compilePlatform) {
        if (auto exist = builder.GetCurPackage()->TryGetClassDef(identifier)) {
            exist->EnableAttr(Attribute::PREVIOUSLY_DESERIALIZED);
            return exist;
        }
    }
    auto srcCodeIdentifier = obj->base()->srcCodeIdentifier()->str();
    auto packageName = obj->base()->packageName()->str();
    auto isClass = obj->isClass();
    auto attrs = CreateAttr(obj->base()->base()->attributes());
    auto imported = attrs.TestAttr(CHIR::Attribute::IMPORTED);
    auto result = builder.CreateClass(
        DebugLocation(), srcCodeIdentifier, GetMangleNameFromIdentifier(identifier), packageName, isClass, imported);
    if (compilePlatform) {
        result->EnableAttr(CHIR::Attribute::DESERIALIZED);
    }
    return result;
}

template <> ExtendDef* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::ExtendDef* obj)
{
    auto identifier = obj->base()->identifier()->str();
    if (compilePlatform) {
        if (auto exist = builder.GetCurPackage()->TryGetExtendDef(identifier)) {
            exist->EnableAttr(Attribute::PREVIOUSLY_DESERIALIZED);
            return exist;
        }
    }
    auto srcCodeIdentifier = obj->base()->srcCodeIdentifier()->str();
    auto packageName = obj->base()->packageName()->str();
    auto attrs = CreateAttr(obj->base()->base()->attributes());
    auto imported = attrs.TestAttr(CHIR::Attribute::IMPORTED);
    auto genericParams = GetType<GenericType>(obj->genericParams());
    auto result = builder.CreateExtend(
        DebugLocation(), GetMangleNameFromIdentifier(identifier), packageName, imported, genericParams);
    if (compilePlatform) {
        result->EnableAttr(CHIR::Attribute::DESERIALIZED);
    }
    // we have to append attributes here because extend def's mangled name may be duplicated in one package
    // we don't know the extend def's mangled name shouldn't be duplicated or cjmp can't use extend def's mangled name
    // it must be fixed by mangle owner or cjmp owner
    result->AppendAttributeInfo(attrs);
    return result;
}

// =========================== Type Deserializer ==============================

namespace {
ModalInfo ReadModalInfo(const PackageFormat::Type* base)
{
    if (base->modal() != nullptr) {
        return ModalInfo(static_cast<Mode>(base->modal()->local()));
    }
    return ModalInfo{};
}
} // namespace

template <> Type* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Type* obj)
{
    CJC_NULLPTR_CHECK(obj);
    auto kind = static_cast<Type::TypeKind>(obj->kind());
    auto modal = ReadModalInfo(obj);
    switch (kind) {
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
            return builder.GetType<IntType>(kind, modal);
        case Type::TYPE_FLOAT16:
        case Type::TYPE_FLOAT32:
        case Type::TYPE_FLOAT64:
            return builder.GetType<FloatType>(kind, modal);
        case Type::TYPE_RUNE:
            return builder.GetType<RuneType>(modal);
        case Type::TYPE_BOOLEAN:
            return builder.GetType<BooleanType>(modal);
        case Type::TYPE_UNIT:
            return builder.GetType<UnitType>(modal);
        case Type::TYPE_NOTHING:
            return builder.GetType<NothingType>(modal);
        case Type::TYPE_VOID:
            return builder.GetType<VoidType>(modal);
        case Type::TYPE_TUPLE:
            return builder.GetType<TupleType>(GetType<Type>(obj->argTys()), modal);
        case Type::TYPE_CPOINTER:
            CJC_NULLPTR_CHECK(obj->argTys());
            CJC_ASSERT(obj->argTys()->size() >= 1);
            return builder.GetType<CPointerType>(GetType<Type>(obj->argTys()->Get(0)), modal);
        case Type::TYPE_CSTRING:
            return builder.GetType<CStringType>(modal);
        case Type::TYPE_REFTYPE:
            CJC_NULLPTR_CHECK(obj->argTys());
            CJC_ASSERT(obj->argTys()->size() >= 1);
            return builder.GetType<RefType>(GetType<Type>(obj->argTys()->Get(0)));
        case Type::TYPE_BOXTYPE:
            CJC_NULLPTR_CHECK(obj->argTys());
            CJC_ASSERT(obj->argTys()->size() >= 1);
            return builder.GetType<BoxType>(GetType<Type>(obj->argTys()->Get(0)));
        case Type::TYPE_THIS:
            return builder.GetType<ThisType>(modal);
        case Type::TYPE_STRUCT:
        case Type::TYPE_ENUM:
        case Type::TYPE_CLASS:
        case Type::TYPE_FUNC:
        case Type::TYPE_RAWARRAY:
        case Type::TYPE_VARRAY:
        case Type::TYPE_GENERIC:
        case Type::TYPE_INVALID:
        case Type::MAX_TYPE_KIND:
            CJC_ABORT();
            return nullptr;
        default:
            CJC_ABORT();
            return nullptr;
    }
}

template <> RawArrayType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::RawArrayType* obj)
{
    CJC_NULLPTR_CHECK(obj->base()->argTys());
    auto elemTy = GetType<Type>(obj->base()->argTys()->Get(0));
    auto dims = obj->dims();
    auto modal = ReadModalInfo(obj->base());
    return builder.GetType<RawArrayType>(elemTy, static_cast<unsigned>(dims), modal);
}

template <> VArrayType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::VArrayType* obj)
{
    CJC_NULLPTR_CHECK(obj->base()->argTys());
    auto elemTy = GetType<Type>(obj->base()->argTys()->Get(0));
    auto size = obj->size();
    auto modal = ReadModalInfo(obj->base());
    return builder.GetType<VArrayType>(elemTy, size, modal);
}

template <> FuncType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::FuncType* obj)
{
    CJC_NULLPTR_CHECK(obj->base()->argTys());
    auto argTys = obj->base()->argTys();
    auto retTy = GetType<Type>(argTys->Get(argTys->size() - 1));
    auto hasVarLenParam = obj->hasVarArg();
    auto isCFuncType = obj->isCFuncType();
    auto modal = ReadModalInfo(obj->base());
    std::vector<Type*> paramTys;
    CJC_ASSERT(argTys->size() > 0);
    for (size_t i = 0; i < argTys->size() - 1; ++i) {
        paramTys.emplace_back(GetType<Type>(argTys->Get(static_cast<unsigned>(i))));
    }
    return builder.GetType<FuncType>(paramTys, retTy, hasVarLenParam, isCFuncType, modal);
}

template <> CustomType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::CustomType* obj)
{
    auto kind = Type::TypeKind(obj->base()->kind());
    auto def = GetCustomTypeDef<CustomTypeDef>(obj->customTypeDef());
    auto typeArgs = GetType<Type>(obj->base()->argTys());
    auto modal = ReadModalInfo(obj->base());
    if (kind == Type::TypeKind::TYPE_CLASS) {
        return builder.GetType<ClassType>(StaticCast<ClassDef*>(def), typeArgs, modal);
    } else if (kind == Type::TypeKind::TYPE_ENUM) {
        return builder.GetType<EnumType>(StaticCast<EnumDef*>(def), typeArgs, modal);
    } else if (kind == Type::TypeKind::TYPE_STRUCT) {
        return builder.GetType<StructType>(StaticCast<StructDef*>(def), typeArgs, modal);
    } else {
        CJC_ABORT();
        return nullptr;
    }
}

template <> GenericType* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::GenericType* obj)
{
    auto identifier = obj->identifier()->str();
    auto srcCodeIndentifier = obj->srcCodeIdentifier()->str();
    auto modal = ReadModalInfo(obj->base());
    auto genericType = builder.GetType<GenericType>(identifier, srcCodeIndentifier, modal);
    genericTypeConfig.emplace_back(genericType, obj);
    return genericType;
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

template <> BlockGroup* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::BlockGroup* obj)
{
    BlockGroup* blockGroup = nullptr;
    if (obj->ownedFunc() != 0) {
        auto ownedFunc = GetValue<Function>(obj->ownedFunc());
        blockGroup = builder.CreateBlockGroup(*ownedFunc);
        blockGroup->SetOwnerFunc(ownedFunc);
    } else if (obj->ownedExpression() != 0) {
        auto ownedExpression = GetExpression<Expression>(obj->ownedExpression());
        CJC_NULLPTR_CHECK(ownedExpression);
        blockGroup = builder.CreateBlockGroup(*ownedExpression->GetTopLevelFunc());
        if (auto lambda = DynamicCast<Lambda*>(ownedExpression)) {
            lambda->InitBody(*blockGroup);
        } else if (auto exclave = DynamicCast<Exclave*>(ownedExpression)) {
            exclave->InitBody(*blockGroup);
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
    if (auto ownedFunc = GetValue<Function>(obj->ownedFunc())) {
        result = builder.CreateParameter(type, INVALID_LOCATION, *ownedFunc);
    } else if (auto ownedLambda = GetExpression<Lambda>(obj->ownedLambda())) {
        result = builder.CreateParameter(type, INVALID_LOCATION, *ownedLambda);
    } else {
        CJC_ABORT();
    }
    result->SetSrcCodeIdentifier(obj->srcCodeIdentifier()->data());
    if (obj->annoInfo()) {
        result->SetAnnoInfo(Create<AnnoInfo>(obj->annoInfo()));
    }
    return result;
}

template <> LocalVar* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::LocalVar* obj)
{
    auto associatedExpr = GetExpression<Expression>(obj->associatedExpr());
    CJC_NULLPTR_CHECK(associatedExpr);
    auto result = associatedExpr->GetResult();
    result->SetSrcCodeIdentifier(obj->srcCodeIdentifier()->data());
    result->identifier = obj->base()->identifier()->str();
    if (obj->isRetVal()) {
        result->SetRetValue(true);
    }
    return result;
}

template <> GlobalVar* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::GlobalVar* obj)
{
    auto globalValue = obj->base();
    auto valueBase = globalValue->base();
    auto identifier = valueBase->identifier()->str();
    if (compilePlatform) {
        if (auto exist = builder.GetCurPackage()->TryGetGlobalVar(identifier)) {
            exist->EnableAttr(Attribute::PREVIOUSLY_DESERIALIZED);
            return exist;
        }
    }
    auto type = GetType<RefType>(valueBase->type());
    auto srcCodeIdentifier = globalValue->srcCodeIdentifier()->str();
    auto packageName = globalValue->packageName()->str();
    auto rawMangledName = globalValue->rawMangledName()->str();
    auto result = builder.CreateGlobalVar(
        type, GetMangleNameFromIdentifier(identifier), srcCodeIdentifier, rawMangledName, packageName);
    result->SetFeatures(GetFeatures(globalValue));
    if (compilePlatform) {
        result->EnableAttr(Attribute::DESERIALIZED);
    }
    return result;
}

template <> Function* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Function* obj)
{
    auto* globalValue = obj->base();
    auto* valueBase = globalValue->base();
    auto type = StaticCast<FuncType*>(GetType<Type>(valueBase->type()));
    auto identifier = valueBase->identifier()->str();
    if (compilePlatform) {
        if (auto exist = builder.GetCurPackage()->TryGetGlobalFunc(identifier)) {
            exist->EnableAttr(Attribute::PREVIOUSLY_DESERIALIZED);
            return exist;
        }
    }
    auto srcCodeIdentifier = globalValue->srcCodeIdentifier()->str();
    auto rawMangledName = globalValue->rawMangledName()->str();
    auto packageName = globalValue->packageName()->str();
    auto genericTypeParams = GetType<GenericType>(obj->genericTypeParams());
    auto result = builder.CreateFunction(type, GetMangleNameFromIdentifier(identifier),
        srcCodeIdentifier, rawMangledName, packageName, genericTypeParams);
    result->SetFeatures(GetFeatures(globalValue));
    result->SetFastNative(obj->isFastNative());
    result->SetCFFIWrapper(obj->isCFFIWrapper());
    result->SetFuncKind(static_cast<FuncKind>(obj->funcKind()));
    if (compilePlatform) {
        result->EnableAttr(Attribute::DESERIALIZED);
    }
    return result;
}
// =========================== Expression Deserializer ==============================

static std::pair<ExprKind, bool> CHIRExprKindToExprKind(PackageFormat::CHIRExprKind kind)
{
    using FK = PackageFormat::CHIRExprKind;
    switch (kind) {
        // terminators
        case FK::CHIRExprKind_Goto:               return {ExprKind::GOTO, false};
        case FK::CHIRExprKind_Branch:             return {ExprKind::BRANCH, false};
        case FK::CHIRExprKind_MultiBranch:        return {ExprKind::MULTIBRANCH, false};
        case FK::CHIRExprKind_Exit:               return {ExprKind::EXIT, false};
        case FK::CHIRExprKind_Exclave:            return {ExprKind::EXCLAVE, false};
        case FK::CHIRExprKind_TryApply:           return {ExprKind::TRY_APPLY, true};
        case FK::CHIRExprKind_TryInvoke:          return {ExprKind::TRY_INVOKE, true};
        case FK::CHIRExprKind_TryIntrinsic:       return {ExprKind::TRY_INTRINSIC, true};
        case FK::CHIRExprKind_RaiseException:     return {ExprKind::RAISE_EXCEPTION, false};
        case FK::CHIRExprKind_TrySpawn:           return {ExprKind::TRY_SPAWN, true};
        case FK::CHIRExprKind_TryNumericCast:     return {ExprKind::TRY_NUMERIC_CAST, true};
        case FK::CHIRExprKind_TryAllocate:        return {ExprKind::TRY_ALLOCATE, true};
        case FK::CHIRExprKind_TryRawArrayAllocate: return {ExprKind::TRY_RAW_ARRAY_ALLOCATE, true};
        case FK::CHIRExprKind_TryNeg:             return {ExprKind::NEG, true};
        case FK::CHIRExprKind_TryAdd:             return {ExprKind::ADD, true};
        case FK::CHIRExprKind_TrySub:             return {ExprKind::SUB, true};
        case FK::CHIRExprKind_TryMul:             return {ExprKind::MUL, true};
        case FK::CHIRExprKind_TryDiv:             return {ExprKind::DIV, true};
        case FK::CHIRExprKind_TryMod:             return {ExprKind::MOD, true};
        case FK::CHIRExprKind_TryExp:             return {ExprKind::EXP, true};
        case FK::CHIRExprKind_TryLShift:          return {ExprKind::LSHIFT, true};
        case FK::CHIRExprKind_TryRShift:          return {ExprKind::RSHIFT, true};
        // unary expr
        case FK::CHIRExprKind_Neg:                return {ExprKind::NEG, false};
        case FK::CHIRExprKind_Not:                return {ExprKind::NOT, false};
        case FK::CHIRExprKind_BitNot:             return {ExprKind::BITNOT, false};
        // binary expr
        case FK::CHIRExprKind_Add:                return {ExprKind::ADD, false};
        case FK::CHIRExprKind_Sub:                return {ExprKind::SUB, false};
        case FK::CHIRExprKind_Mul:                return {ExprKind::MUL, false};
        case FK::CHIRExprKind_Div:                return {ExprKind::DIV, false};
        case FK::CHIRExprKind_Mod:                return {ExprKind::MOD, false};
        case FK::CHIRExprKind_Exp:                return {ExprKind::EXP, false};
        case FK::CHIRExprKind_LShift:             return {ExprKind::LSHIFT, false};
        case FK::CHIRExprKind_RShift:             return {ExprKind::RSHIFT, false};
        case FK::CHIRExprKind_BitAnd:             return {ExprKind::BITAND, false};
        case FK::CHIRExprKind_BitOr:              return {ExprKind::BITOR, false};
        case FK::CHIRExprKind_BitXor:             return {ExprKind::BITXOR, false};
        case FK::CHIRExprKind_LT:                 return {ExprKind::LT, false};
        case FK::CHIRExprKind_GT:                 return {ExprKind::GT, false};
        case FK::CHIRExprKind_LE:                 return {ExprKind::LE, false};
        case FK::CHIRExprKind_GE:                 return {ExprKind::GE, false};
        case FK::CHIRExprKind_Equal:              return {ExprKind::EQUAL, false};
        case FK::CHIRExprKind_NotEqual:           return {ExprKind::NOTEQUAL, false};
        case FK::CHIRExprKind_And:                return {ExprKind::AND, false};
        case FK::CHIRExprKind_Or:                 return {ExprKind::OR, false};
        // type cast
        case FK::CHIRExprKind_StaticCast:         return {ExprKind::CLASS_STATIC_CAST, false};
        case FK::CHIRExprKind_Box:                return {ExprKind::BOX, false};
        case FK::CHIRExprKind_UnboxToValue:       return {ExprKind::UNBOX_TO_VALUE, false};
        case FK::CHIRExprKind_UnboxToRef:         return {ExprKind::UNBOX_TO_REF, false};
        case FK::CHIRExprKind_NumericCast:        return {ExprKind::NUMERIC_CAST, false};
        case FK::CHIRExprKind_CastToConcrete:     return {ExprKind::CAST_TO_CONCRETE, false};
        case FK::CHIRExprKind_CastToGeneric:      return {ExprKind::CAST_TO_GENERIC, false};
        // memory expr
        case FK::CHIRExprKind_Allocate:           return {ExprKind::ALLOCATE, false};
        case FK::CHIRExprKind_Load:               return {ExprKind::LOAD, false};
        case FK::CHIRExprKind_Store:              return {ExprKind::STORE, false};
        case FK::CHIRExprKind_GetElementByName:   return {ExprKind::GET_ELEMENT_BY_NAME, false};
        case FK::CHIRExprKind_GetElementRef:      return {ExprKind::GET_ELEMENT_REF, false};
        case FK::CHIRExprKind_StoreElementByName: return {ExprKind::STORE_ELEMENT_BY_NAME, false};
        case FK::CHIRExprKind_StoreElementRef:    return {ExprKind::STORE_ELEMENT_REF, false};
        case FK::CHIRExprKind_Field:              return {ExprKind::FIELD, false};
        case FK::CHIRExprKind_FieldByName:        return {ExprKind::FIELD_BY_NAME, false};
        // array
        case FK::CHIRExprKind_RawArrayAllocate:      return {ExprKind::RAW_ARRAY_ALLOCATE, false};
        case FK::CHIRExprKind_RawArrayLiteralInit:   return {ExprKind::RAW_ARRAY_LITERAL_INIT, false};
        case FK::CHIRExprKind_RawArrayInitByValue:   return {ExprKind::RAW_ARRAY_INIT_BY_VALUE, false};
        case FK::CHIRExprKind_VArrayExpr:            return {ExprKind::VARRAY, false};
        case FK::CHIRExprKind_VArrayBuilder:         return {ExprKind::VARRAY_BUILDER, false};
        // others
        case FK::CHIRExprKind_Constant:           return {ExprKind::CONSTANT, false};
        case FK::CHIRExprKind_Debug:              return {ExprKind::DEBUGEXPR, false};
        case FK::CHIRExprKind_Tuple:              return {ExprKind::TUPLE, false};
        case FK::CHIRExprKind_InstanceOf:         return {ExprKind::INSTANCEOF, false};
        case FK::CHIRExprKind_GetException:       return {ExprKind::GET_EXCEPTION, false};
        case FK::CHIRExprKind_Spawn:              return {ExprKind::SPAWN, false};
        case FK::CHIRExprKind_Lambda:             return {ExprKind::LAMBDA, false};
        case FK::CHIRExprKind_GetInstantiateValue: return {ExprKind::GET_INSTANTIATE_VALUE, false};
        case FK::CHIRExprKind_Apply:              return {ExprKind::APPLY, false};
        case FK::CHIRExprKind_Invoke:             return {ExprKind::INVOKE, false};
        case FK::CHIRExprKind_Intrinsic:          return {ExprKind::INTRINSIC, false};
        case FK::CHIRExprKind_GetRtti:            return {ExprKind::GET_RTTI, false};
        case FK::CHIRExprKind_GetRttiStatic:      return {ExprKind::GET_RTTI_STATIC, false};
        case FK::CHIRExprKind_StartRegion:        return {ExprKind::START_REGION, false};
        case FK::CHIRExprKind_EndRegion:          return {ExprKind::END_REGION, false};
        default:                     return {ExprKind::GOTO, false}; // unreachable
    }
}

template <>
Expression* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::UnaryExpressionBase* obj)
{
    auto operand = GetValue<Value>(obj->base()->operands()->Get(0));
    auto parentBlock = GetValue<Block>(obj->base()->owner());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto [kind, isException] = CHIRExprKindToExprKind(obj->base()->kind());
    if (isException) {
        auto normalBlock = StaticCast<Block*>(GetValue<Value>(obj->base()->operands()->Get(1)));
        auto exceptionBlock = StaticCast<Block*>(GetValue<Value>(obj->base()->operands()->Get(2)));
        return builder.CreateExpression<TryUnaryExpression>(
            resultTy, ExprKindMgr::ToUnaryExprKind(kind), operand, normalBlock, exceptionBlock, parentBlock);
    } else {
        auto ofs = DeserializeOverflowStrategy(obj->overflowStrategy());
        return builder.CreateExpression<UnaryExpression>(
            resultTy, ExprKindMgr::ToUnaryExprKind(kind), operand, ofs, parentBlock);
    }
}

template <>
Expression* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::BinaryExpressionBase* obj)
{
    auto lhs = GetValue<Value>(obj->base()->operands()->Get(0));
    auto rhs = GetValue<Value>(obj->base()->operands()->Get(1));
    auto parentBlock = GetValue<Block>(obj->base()->owner());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto ofs = DeserializeOverflowStrategy(obj->overflowStrategy());
    auto [kind, isException] = CHIRExprKindToExprKind(obj->base()->kind());
    if (isException) {
        auto normalBlock = StaticCast<Block*>(GetValue<Value>(obj->base()->operands()->Get(2)));
        auto exceptionBlock = StaticCast<Block*>(GetValue<Value>(obj->base()->operands()->Get(3)));
        return builder.CreateExpression<TryBinaryExpression>(
            resultTy, ExprKindMgr::ToBinaryExprKind(kind), lhs, rhs, ofs, normalBlock, exceptionBlock, parentBlock);
    } else {
        return builder.CreateExpression<BinaryExpression>(
            resultTy, ExprKindMgr::ToBinaryExprKind(kind), lhs, rhs, ofs, parentBlock);
    }
}

template <> Expression* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Expression* obj)
{
    auto operands = GetValue<Value>(obj->operands());
    auto owner = GetValue<Block>(obj->owner());
    auto resultTy = GetType<Type>(obj->resultTy());
    auto [kind, _] = CHIRExprKindToExprKind(obj->kind());
    switch (kind) {
        case ExprKind::BOX:
            CJC_ASSERT(operands.size() == 1);
            return builder.CreateExpression<Box>(resultTy, operands[0], owner);
        case ExprKind::CONSTANT:
            CJC_ASSERT(operands.size() == 1);
            return DeserializeConstant(*resultTy, *StaticCast<LiteralValue*>(operands[0]), *owner);
        case ExprKind::EXIT:
            return builder.CreateTerminator<Exit>(owner);
        case ExprKind::EXCLAVE:
            return builder.CreateTerminator<Exclave>(owner);
        case ExprKind::GET_EXCEPTION:
            return builder.CreateExpression<GetException>(resultTy, owner);
        case ExprKind::START_REGION:
            return builder.CreateExpression<StartRegion>(resultTy, owner);
        case ExprKind::END_REGION:
            return builder.CreateExpression<EndRegion>(resultTy, owner);
        case ExprKind::GET_RTTI:
            CJC_ASSERT(operands.size() == 1);
            return builder.CreateExpression<GetRTTI>(resultTy, operands[0], owner);
        case ExprKind::GOTO:
            CJC_ASSERT(operands.size() == 1);
            return builder.CreateTerminator<GoTo>(StaticCast<Block*>(operands[0]), owner);
        case ExprKind::LOAD:
            CJC_ASSERT(operands.size() == 1);
            return builder.CreateExpression<Load>(resultTy, operands[0], owner);
        case ExprKind::RAISE_EXCEPTION:
            if (operands.size() == 2) {
                return builder.CreateTerminator<RaiseException>(operands[0], StaticCast<Block*>(operands[1]), owner);
            } else {
                CJC_ASSERT(operands.size() == 1);
                return builder.CreateTerminator<RaiseException>(operands[0], owner);
            }
        case ExprKind::RAW_ARRAY_INIT_BY_VALUE:
            CJC_ASSERT(operands.size() == 3);
            return builder.CreateExpression<RawArrayInitByValue>(
                resultTy, operands[0], operands[1], operands[2], owner);
        case ExprKind::RAW_ARRAY_LITERAL_INIT: {
            CJC_ASSERT(operands.size() >= 1);
            auto rawArrayMemory = operands[0];
            operands.erase(operands.begin());
            return builder.CreateExpression<RawArrayLiteralInit>(resultTy, rawArrayMemory, operands, owner);
        }
        case ExprKind::STORE:
            CJC_ASSERT(operands.size() == 2);
            return builder.CreateExpression<Store>(resultTy, operands[0], operands[1], owner);
        case ExprKind::CAST_TO_CONCRETE:
            CJC_ASSERT(operands.size() == 1);
            return builder.CreateExpression<CastToConcrete>(resultTy, operands[0], owner);
        case ExprKind::CAST_TO_GENERIC:
            CJC_ASSERT(operands.size() == 1);
            return builder.CreateExpression<CastToGeneric>(resultTy, operands[0], owner);
        case ExprKind::TUPLE:
            return builder.CreateExpression<Tuple>(resultTy, operands, owner);
        case ExprKind::UNBOX_TO_VALUE:
            CJC_ASSERT(operands.size() == 1);
            return builder.CreateExpression<UnBoxToValue>(resultTy, operands[0], owner);
        case ExprKind::UNBOX_TO_REF:
            CJC_ASSERT(operands.size() == 1);
            return builder.CreateExpression<UnBoxToRef>(resultTy, operands[0], owner);
        case ExprKind::CLASS_STATIC_CAST:
            CJC_ASSERT(operands.size() == 1);
            return builder.CreateExpression<ClassStaticCast>(resultTy, operands[0], owner);
        case ExprKind::VARRAY:
            CJC_ASSERT(!operands.empty());
            return builder.CreateExpression<VArray>(resultTy, operands, owner);
        case ExprKind::VARRAY_BUILDER:
            CJC_ASSERT(operands.size() == 3);
            return builder.CreateExpression<VArrayBuilder>(resultTy, operands[0], operands[1], operands[2], owner);
        default:
            CJC_ABORT();
            return nullptr;
    }
}

Constant* CHIRDeserializer::CHIRDeserializerImpl::DeserializeConstant(Type& resultTy, LiteralValue& val, Block& parent)
{
    if (val.IsBoolLiteral()) {
        auto& litVal = StaticCast<BoolLiteral&>(val);
        return builder.CreateConstantExpression<BoolLiteral>(&resultTy, &parent, litVal.GetVal());
    } else if (val.IsFloatLiteral()) {
        auto& litVal = StaticCast<FloatLiteral&>(val);
        return builder.CreateConstantExpression<FloatLiteral>(&resultTy, &parent, litVal.GetVal());
    } else if (val.IsIntLiteral()) {
        auto& litVal = StaticCast<IntLiteral&>(val);
        return builder.CreateConstantExpression<IntLiteral>(&resultTy, &parent, litVal.GetUnsignedVal());
    } else if (val.IsNullLiteral()) {
        return builder.CreateConstantExpression<NullLiteral>(&resultTy, &parent);
    } else if (val.IsRuneLiteral()) {
        auto& litVal = StaticCast<RuneLiteral&>(val);
        return builder.CreateConstantExpression<RuneLiteral>(&resultTy, &parent, litVal.GetVal());
    } else if (val.IsStringLiteral()) {
        auto& litVal = StaticCast<StringLiteral&>(val);
        return builder.CreateConstantExpression<StringLiteral>(&resultTy, &parent, litVal.GetVal());
    } else if (val.IsUnitLiteral()) {
        return builder.CreateConstantExpression<UnitLiteral>(&resultTy, &parent);
    } else {
        CJC_ABORT();
    }
    return nullptr;
}

template <> Expression* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::AllocateBase* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->owner());
    auto targetType = GetType<Type>(obj->allocatedType());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    if (CHIRExprKindToExprKind(obj->base()->kind()).second) {
        CJC_NULLPTR_CHECK(obj->base()->operands());
        CJC_ASSERT(obj->base()->operands()->size() == 2);
        auto operands = GetValue<Value>(obj->base()->operands());
        auto normalBlock = StaticCast<Block>(operands[0]);
        auto exceptionBlock = StaticCast<Block>(operands[1]);
        return builder.CreateExpression<TryAllocate>(resultTy, targetType, normalBlock, exceptionBlock, parentBlock);
    } else {
        return builder.CreateExpression<Allocate>(resultTy, targetType, parentBlock);
    }
}

template <> GetElementRef* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::GetElementRef* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->owner());
    auto location = GetValue<Value>(obj->base()->operands()->Get(0));
    CJC_NULLPTR_CHECK(obj->path());
    auto path = std::vector<uint64_t>(obj->path()->begin(), obj->path()->end());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<GetElementRef>(resultTy, location, path, parentBlock);
}

template <> GetElementByName* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(
    const PackageFormat::GetElementByName* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->owner());
    auto location = GetValue<Value>(obj->base()->operands()->Get(0));
    std::vector<std::string> names;
    names.reserve(obj->fieldNames()->size());
    for (const auto& name : *obj->fieldNames()) {
        names.emplace_back(name->str());
    }
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<GetElementByName>(resultTy, location, names, parentBlock);
}

template <>
StoreElementRef* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::StoreElementRef* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->owner());
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
    auto parentBlock = GetValue<Block>(obj->base()->owner());
    auto value = GetValue<Value>(obj->base()->operands()->Get(0));
    auto location = GetValue<Value>(obj->base()->operands()->Get(1));
    std::vector<std::string> names;
    names.reserve(obj->fieldNames()->size());
    for (const auto& name : *obj->fieldNames()) {
        names.emplace_back(name->str());
    }
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<StoreElementByName>(resultTy, value, location, names, parentBlock);
}

template <> Expression* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::ApplyBase* obj)
{
    auto funcCallBase = obj->base();
    auto objType = GetType<Type>(funcCallBase->objType());
    auto instantiatedTypeArgs = GetType<Type>(funcCallBase->instantiatedTypeArgs());

    auto base = funcCallBase->base();
    auto owner = GetValue<Block>(base->owner());
    auto operands = GetValue<Value>(base->operands());
    CJC_ASSERT(!operands.empty());
    auto callee = operands[0];
    operands.erase(operands.begin());
    auto resultTy = GetType<Type>(base->resultTy());

    if (CHIRExprKindToExprKind(base->kind()).second) {
        auto exceptionBlock = StaticCast<Block*>(operands.back());
        operands.pop_back();
        auto normalBlock = StaticCast<Block*>(operands.back());
        operands.pop_back();
        auto callCtx = FuncCallContext {
            .args = operands,
            .instTypeArgs = instantiatedTypeArgs,
            .thisType = objType
        };
        return builder.CreateExpression<TryApply>(resultTy, callee, callCtx, normalBlock, exceptionBlock, owner);
    } else {
        auto callCtx = FuncCallContext {
            .args = operands,
            .instTypeArgs = instantiatedTypeArgs,
            .thisType = objType
        };
        auto expr = builder.CreateExpression<Apply>(resultTy, callee, callCtx, owner);
        if (obj->isSuperCall()) {
            expr->SetSuperCall();
        }
        return expr;
    }
}

template <> Expression* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::InvokeBase* obj)
{
    auto funcCallBase = obj->base();
    auto base = funcCallBase->base();
    auto owner = GetValue<Block>(base->owner());
    auto operands = GetValue<Value>(base->operands());
    CJC_ASSERT(operands.size() >= 2);
    auto method = StaticCast<Function*>(operands[0]);
    operands.erase(operands.begin());
    auto caller = operands[0];
    operands.erase(operands.begin());
    auto resultTy = GetType<Type>(base->resultTy());
    auto [kind, withException] = CHIRExprKindToExprKind(base->kind());
    Block* exceptionBlock = nullptr;
    Block* normalBlock = nullptr;
    if (withException) {
        exceptionBlock = StaticCast<Block*>(operands.back());
        operands.pop_back();
        normalBlock = StaticCast<Block*>(operands.back());
        operands.pop_back();
    }
    auto invokeInfo = InvokeCallContext {
        .method = method,
        .caller = caller,
        .funcCallCtx = FuncCallContext {
            .args = operands,
            .instTypeArgs = GetType<Type>(funcCallBase->instantiatedTypeArgs()),
            .thisType = GetType<Type>(funcCallBase->objType())
        },
        .overflowStrategy = DeserializeOverflowStrategy(obj->overflowStrategy())
    };
    auto isStatic = [](Value& o) {
        if (!o.IsLocalVar()) {
            return false;
        }
        auto k = StaticCast<LocalVar&>(o).GetExpr()->GetExprKind();
        return k == ExprKind::GET_RTTI_STATIC || k == ExprKind::GET_RTTI;
    };
    if (kind == CHIR::ExprKind::INVOKE) {
        if (isStatic(*caller)) {
            return builder.CreateExpression<InvokeStatic>(resultTy, invokeInfo, owner);
        } else {
            return builder.CreateExpression<Invoke>(resultTy, invokeInfo, owner);
        }
    } else {
        if (isStatic(*caller)) {
            return builder.CreateExpression<TryInvokeStatic>(resultTy, invokeInfo, normalBlock, exceptionBlock, owner);
        } else {
            return builder.CreateExpression<TryInvoke>(resultTy, invokeInfo, normalBlock, exceptionBlock, owner);
        }
    }
}

template <>
GetInstantiateValue* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::GetInstantiateValue* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->owner());
    auto val = GetValue<Value>(obj->base()->operands()->Get(0));
    auto insTypes = GetType<Type>(obj->instantiateTypes());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<GetInstantiateValue>(resultTy, val, insTypes, parentBlock);
}

template <> Expression* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::NumericCastBase* obj)
{
    auto base = obj->base();
    auto owner = GetValue<Block>(base->owner());
    auto operand = GetValue<Value>(base->operands()->Get(0));
    auto resultTy = GetType<Type>(base->resultTy());
    auto ofs = DeserializeOverflowStrategy(obj->overflowStrategy());
    if (CHIRExprKindToExprKind(base->kind()).second) {
        auto normalBlock = StaticCast<Block*>(GetValue<Value>(base->operands()->Get(1)));
        auto exceptionBlock = StaticCast<Block*>(GetValue<Value>(base->operands()->Get(2)));
        return builder.CreateExpression<TryNumericCast>(resultTy, operand, normalBlock, exceptionBlock, owner);
    } else {
        return builder.CreateExpression<NumericCast>(resultTy, operand, ofs, owner);
    }
}

template <> InstanceOf* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::InstanceOf* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->owner());
    auto operand = GetValue<Value>(obj->base()->operands()->Get(0));
    auto targetType = GetType<Type>(obj->targetType());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<InstanceOf>(resultTy, operand, targetType, parentBlock);
}

template <> Field* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Field* obj)
{
    auto val = GetValue<Value>(obj->base()->operands()->Get(0));
    CJC_NULLPTR_CHECK(obj->path());
    auto indexes = std::vector<uint64_t>(obj->path()->begin(), obj->path()->end());
    auto parentBlock = GetValue<Block>(obj->base()->owner());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<Field>(resultTy, val, indexes, parentBlock);
}

template <> FieldByName* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::FieldByName* obj)
{
    auto val = GetValue<Value>(obj->base()->operands()->Get(0));
    std::vector<std::string> names;
    names.reserve(obj->fieldNames()->size());
    for (const auto& name : *obj->fieldNames()) {
        names.emplace_back(name->str());
    }
    auto parentBlock = GetValue<Block>(obj->base()->owner());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    return builder.CreateExpression<FieldByName>(resultTy, val, names, parentBlock);
}

template <> Debug* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Debug* obj)
{
    CJC_ASSERT(obj->base()->operands()->size() == 1);
    auto local = GetValue<Value>(obj->base()->operands()->Get(0));
    auto srcCodeIdentifier = obj->srcCodeName()->str();
    auto parentBlock = GetValue<Block>(obj->base()->owner());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto result = builder.CreateExpression<Debug>(resultTy, local, srcCodeIdentifier, parentBlock);
    return result;
}

template <> Lambda* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Lambda* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->owner());
    auto funcTy = GetType<FuncType>(obj->funcTy());
    auto isLocalFunc = obj->isLocalFunc();
    auto identifier = obj->identifier()->str();
    auto srcCodeIdentifier = obj->srcCodeName()->str();
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

template <> Branch* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::Branch* obj)
{
    auto parentBlock = GetValue<Block>(obj->base()->owner());
    auto cond = GetValue<Value>(obj->base()->operands()->Get(0));
    auto trueBlock = GetValue<Block>(obj->base()->operands()->Get(1));
    auto falseBlock = GetValue<Block>(obj->base()->operands()->Get(2));
    auto sourceExpr = CHIR::SourceExpr(obj->sourceExpr());
    auto result = builder.CreateTerminator<Branch>(cond, trueBlock, falseBlock, parentBlock);
    result->SetSourceExpr(sourceExpr);
    return result;
}

template <> MultiBranch* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::MultiBranch* obj)
{
    auto operands = GetValue<Value>(obj->base()->operands());
    auto cond = operands.front();
    operands.erase(operands.begin());
    auto defaultBlock = StaticCast<Block*>(operands.front());
    operands.erase(operands.begin());
    std::vector<Block*> caseBlocks;
    for (auto op : operands) {
        caseBlocks.emplace_back(StaticCast<Block*>(op));
    }
    auto owner = GetValue<Block>(obj->base()->owner());
    auto caseVals = std::vector<uint64_t>(obj->caseValues()->begin(), obj->caseValues()->end());
    return builder.CreateTerminator<MultiBranch>(cond, defaultBlock, caseVals, caseBlocks, owner);
}

template <>
Expression* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::IntrinsicBase* obj)
{
    auto funcCallBase = obj->base();
    auto base = funcCallBase->base();
    auto kind = CHIR::IntrinsicKind(obj->intrinsicKind());
    auto operands = GetValue<Value>(base->operands());
    auto resultTy = GetType<Type>(base->resultTy());
    auto owner = GetValue<Block>(base->owner());
    if (CHIRExprKindToExprKind(base->kind()).second) {
        auto exceptionBranch = StaticCast<Block*>(operands.back());
        operands.pop_back();
        auto normalBlock = StaticCast<Block*>(operands.back());
        operands.pop_back();
        auto callContext = IntrisicCallContext {
            .kind = kind,
            .args = operands,
            .instTypeArgs = GetType<Type>(funcCallBase->instantiatedTypeArgs())
        };
        return builder.CreateExpression<TryIntrinsic>(resultTy, callContext, normalBlock, exceptionBranch, owner);
    } else {
        auto callContext = IntrisicCallContext {
            .kind = kind,
            .args = operands,
            .instTypeArgs = GetType<Type>(funcCallBase->instantiatedTypeArgs())
        };
        return builder.CreateExpression<Intrinsic>(resultTy, callContext, owner);
    }
}

template <>
Expression* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(
    const PackageFormat::RawArrayAllocateBase* obj)
{
    auto elementType = GetType<Type>(obj->elementType());
    auto operands = GetValue<Value>(obj->base()->operands());
    auto size = operands[0];
    operands.erase(operands.begin());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto owner = GetValue<Block>(obj->base()->owner());
    if (CHIRExprKindToExprKind(obj->base()->kind()).second) {
        auto exceptionBranch = StaticCast<Block*>(operands.back());
        operands.pop_back();
        auto normalBlock = StaticCast<Block*>(operands.back());
        operands.pop_back();
        return builder.CreateExpression<TryRawArrayAllocate>(
            resultTy, elementType, size, normalBlock, exceptionBranch, owner);
    } else {
        return builder.CreateExpression<RawArrayAllocate>(resultTy, elementType, size, owner);
    }
}

template <>
Expression* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::SpawnBase* obj)
{
    auto operands = GetValue<Value>(obj->base()->operands());
    auto val = operands[0];
    operands.erase(operands.begin());
    auto func = GetValue<Function>(obj->executeClosure());
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto owner = GetValue<Block>(obj->base()->owner());
    if (CHIRExprKindToExprKind(obj->base()->kind()).second) {
        auto exceptionBranch = StaticCast<Block*>(operands.back());
        operands.pop_back();
        auto normalBlock = StaticCast<Block*>(operands.back());
        operands.pop_back();
        TrySpawn* expr = nullptr;
        if (operands.empty()) {
            expr = builder.CreateExpression<TrySpawn>(resultTy, val, normalBlock, exceptionBranch, owner);
        } else {
            expr = builder.CreateExpression<TrySpawn>(resultTy, val, operands[0], normalBlock, exceptionBranch, owner);
        }
        if (func) {
            expr->SetExecuteClosure(*func);
        }
        return expr;
    } else {
        Spawn* expr = nullptr;
        if (operands.empty()) {
            expr = builder.CreateExpression<Spawn>(resultTy, val, owner);
        } else {
            expr = builder.CreateExpression<Spawn>(resultTy, val, operands[0], owner);
        }
        if (func) {
            expr->SetExecuteClosure(*func);
        }
        return expr;
    }
}

template <> GetRTTIStatic* CHIRDeserializer::CHIRDeserializerImpl::Deserialize(const PackageFormat::GetRTTIStatic* obj)
{
    auto resultTy = GetType<Type>(obj->base()->resultTy());
    auto rttiType = GetType<Type>(obj->rttiType());
    auto parentBlock = GetValue<Block>(obj->base()->owner());
    return builder.CreateExpression<GetRTTIStatic>(resultTy, rttiType, parentBlock);
}

// =========================== Configuration ==============================

void CHIRDeserializer::CHIRDeserializerImpl::ConfigBase(const PackageFormat::Base* buffer, Base& obj)
{
    CJC_NULLPTR_CHECK(buffer);
    auto annos = buffer->annos();
    auto annoTypes = buffer->annos_type();
    for (unsigned i = 0; i < annos->size(); ++i) {
        auto anno = annos->Get(i);
        switch (annoTypes->Get(i)) {
            case PackageFormat::Annotation::Annotation_needCheckArrayBound:
                obj.Set<NeedCheckArrayBound>(static_cast<const PackageFormat::NeedCheckArrayBound*>(anno)->need());
                break;
            case PackageFormat::Annotation::Annotation_needCheckCast:
                obj.Set<NeedCheckCast>(static_cast<const PackageFormat::NeedCheckCast*>(anno)->need());
                break;
            case PackageFormat::Annotation::Annotation_debugLocationInfoForWarning:
                obj.Set<DebugLocationInfoForWarning>(
                    Create<DebugLocation>(static_cast<const PackageFormat::DebugLocation*>(anno)));
                break;
            case PackageFormat::Annotation::Annotation_linkTypeInfo:
                obj.Set<CHIR::LinkTypeInfo>(
                    Cangjie::Linkage(static_cast<const PackageFormat::LinkTypeInfo*>(anno)->linkage()));
                break;
            case PackageFormat::Annotation::Annotation_skipCheck:
                obj.Set<CHIR::SkipCheck>(
                    CHIR::SkipKind(static_cast<const PackageFormat::SkipCheck*>(anno)->skipKind()));
                break;
            case PackageFormat::Annotation::Annotation_neverOverflowInfo:
                obj.Set<CHIR::NeverOverflowInfo>(
                    static_cast<const PackageFormat::NeverOverflowInfo*>(anno)->neverOverflow());
                break;
            case PackageFormat::Annotation::Annotation_generatedFromForIn:
                obj.Set<CHIR::GeneratedFromForIn>(
                    static_cast<const PackageFormat::GeneratedFromForIn*>(anno)->value());
                break;
            case PackageFormat::Annotation::Annotation_isAutoEnvClass:
                obj.Set<CHIR::IsAutoEnvClass>(static_cast<const PackageFormat::IsAutoEnvClass*>(anno)->value());
                break;
            case PackageFormat::Annotation::Annotation_isCapturedClassInCC:
                obj.Set<CHIR::IsCapturedClassInCC>(
                    static_cast<const PackageFormat::IsCapturedClassInCC*>(anno)->value());
                break;
            case PackageFormat::Annotation::Annotation_enumCaseIndex: {
                int64_t index = static_cast<const PackageFormat::EnumCaseIndex*>(anno)->index();
                if (index != -1) {
                    obj.Set<CHIR::EnumCaseIndex>(static_cast<size_t>(index));
                }
                break;
            }
            case PackageFormat::Annotation::Annotation_virMethodOffset: {
                int64_t offset = static_cast<const PackageFormat::VirMethodOffset*>(anno)->offset();
                if (offset != -1) {
                    obj.Set<CHIR::VirMethodOffset>(static_cast<size_t>(offset));
                }
                break;
            }
            case PackageFormat::Annotation::Annotation_wrappedRawMethod:
                obj.Set<CHIR::WrappedRawMethod>(
                    GetValue<Function>(static_cast<const PackageFormat::WrappedRawMethod*>(anno)->rawMethod()));
                break;
            case PackageFormat::Annotation::Annotation_overrideSrcFuncType:
                obj.Set<CHIR::OverrideSrcFuncType>(
                    GetType<FuncType>(static_cast<const PackageFormat::OverrideSrcFuncType*>(anno)->type()));
                break;
            default:
                continue;
        }
    }
    if (buffer->loc()) {
        obj.SetDebugLocation(Create<DebugLocation>(buffer->loc()));
    }
    obj.AppendAttributeInfo(CreateAttr(buffer->attributes()));
}

void CHIRDeserializer::CHIRDeserializerImpl::ConfigValue(const PackageFormat::Value* buffer, Value& obj)
{
    CJC_NULLPTR_CHECK(buffer);
    ConfigBase(buffer->base(), obj);
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

    auto declaredMethods = GetValue<Function>(buffer->methods());
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
    auto staticMemberVars = GetValue<GlobalVar>(buffer->staticMemberVars());
    for (auto var : staticMemberVars) {
        CJC_NULLPTR_CHECK(var);
        obj.AddStaticMemberVar(var);
    }
    if (compilePlatform) {
        obj.EnableAttr(Attribute::DESERIALIZED);
    }
    obj.SetAnnoInfo(Create<AnnoInfo>(buffer->annoInfo()));
    auto vtable =
        Create<VTableInDef, flatbuffers::Vector<flatbuffers::Offset<PackageFormat::VTableInType>>>(buffer->vtable());
    obj.SetVTable(std::move(vtable));
    obj.SetVarInitializationFunc(GetValue<Function>(buffer->instanceVarInitFunc()));
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

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::Function* buffer, Function& obj)
{
    if (obj.TestAttr(Attribute::PREVIOUSLY_DESERIALIZED)) {
        return;
    }
    ConfigBase(buffer->base()->base()->base(), obj);
    if (buffer->paramDftValHostFunc() != 0) {
        auto paramDftValHostFunc = GetValue<Function>(buffer->paramDftValHostFunc());
        CJC_NULLPTR_CHECK(paramDftValHostFunc);
        obj.SetParamDftValHostFunc(*paramDftValHostFunc);
    }
    if (buffer->genericDecl() != 0) {
        auto genericDecl = GetValue<Function>(buffer->genericDecl());
        CJC_NULLPTR_CHECK(genericDecl);
        obj.SetGenericDecl(*genericDecl);
    }
    if (buffer->base()->annoInfo()) {
        obj.SetAnnoInfo(Create<AnnoInfo>(buffer->base()->annoInfo()));
    }
    // params: signature params for imported func, body params for func with body
    auto params = GetValue<Parameter>(buffer->params());
    obj.RemoveParams();
    for (auto p : params) {
        CJC_NULLPTR_CHECK(p);
        obj.AddParam(*p);
    }
    if (buffer->body() != 0) {
        // func with body
        auto body = GetValue<BlockGroup>(buffer->body());
        if (body) {
            obj.InitBody(*body);
        } else {
            CJC_ASSERT(obj.TestAttr(Attribute::COMMON));
        }
        obj.SetFuncKind(FuncKind(buffer->funcKind()));
        if (buffer->retVal() != 0) {
            obj.SetReturnValue(*GetValue<LocalVar>(buffer->retVal()));
        }
        obj.SetRawMangledName(buffer->base()->rawMangledName()->str());
        if (compilePlatform) {
            obj.EnableAttr(Attribute::DESERIALIZED);
        }
        obj.SetPropLocation(Create<DebugLocation>(buffer->propLoc()));
        if (buffer->originalLambdaInfo()) {
            auto* ori = buffer->originalLambdaInfo();
            FuncSigInfo funcSig;
            if (ori->funcName()) {
                funcSig.funcName = ori->funcName()->str();
            }
            if (ori->funcType() != 0) {
                funcSig.funcType = GetType<FuncType>(ori->funcType());
            }
            funcSig.genericTypeParams = GetType<GenericType>(ori->genericTypeParams());
            obj.SetOriginalLambdaInfo(funcSig);
        }
        if (auto* retVal = GetValue<LocalVar>(buffer->retVal()); retVal != nullptr) {
            obj.SetReturnValue(*retVal);
        }
    }
}

template <>
void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::BlockGroup* buffer, BlockGroup& obj)
{
    ConfigValue(buffer->base(), obj);
    GetValue<Block>(buffer->blocks());
    obj.SetEntryBlock(GetValue<Block>(buffer->entryBlock()));
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::Block* buffer, Block& obj)
{
    ConfigValue(buffer->base(), obj);
    if (buffer->isLandingPadBlock()) {
        obj.SetExceptions(GetType<ClassType>(buffer->exceptionCatchList()));
    }
    obj.AppendExpressions(GetExpression<Expression>(buffer->exprs()));
    obj.predecessors.clear();
    obj.predecessors = GetValue<Block>(buffer->predecessors());
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::GlobalVar* buffer, GlobalVar& obj)
{
    if (obj.TestAttr(Attribute::PREVIOUSLY_DESERIALIZED)) {
        return;
    }
    ConfigBase(buffer->base()->base()->base(), obj);
    if (buffer->base()->annoInfo()) {
        obj.SetAnnoInfo(Create<AnnoInfo>(buffer->base()->annoInfo()));
    }
    if (buffer->initializer() != 0) {
        auto* initVal = GetValue<Value>(buffer->initializer());
        if (auto* literal = DynamicCast<LiteralValue*>(initVal)) {
            obj.SetInitializer(*literal);
        } else if (auto* initFunc = DynamicCast<Function*>(initVal)) {
            obj.SetInitFunc(*initFunc);
        }
    }
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::EnumDef* buffer, EnumDef& obj)
{
    if (obj.TestAttr(Attribute::PREVIOUSLY_DESERIALIZED)) {
        return;
    }
    ConfigCustomTypeDef(buffer->base(), obj);
    for (auto info : Create<EnumCtorInfo>(buffer->ctors())) {
        obj.AddCtor(info);
    }
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::StructDef* buffer, StructDef& obj)
{
    if (obj.TestAttr(Attribute::PREVIOUSLY_DESERIALIZED)) {
        return;
    }
    ConfigCustomTypeDef(buffer->base(), obj);
    obj.SetCStruct(buffer->isCStruct());
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::ClassDef* buffer, ClassDef& obj)
{
    if (obj.TestAttr(Attribute::PREVIOUSLY_DESERIALIZED)) {
        return;
    }
    ConfigCustomTypeDef(buffer->base(), obj);
    if (buffer->isAnnotation()) {
        auto targets = GetValue<GlobalVar>(buffer->annotationTargets());
        obj.SetAnnotationTargets(std::move(targets));
    }
    auto superClass = GetType<ClassType>(buffer->superClass());
    if (superClass) {
        obj.SetSuperClassTy(*superClass);
    }
}

template <> void CHIRDeserializer::CHIRDeserializerImpl::Config(const PackageFormat::ExtendDef* buffer, ExtendDef& obj)
{
    if (obj.TestAttr(Attribute::PREVIOUSLY_DESERIALIZED)) {
        return;
    }
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
                ConfigValue(
                    static_cast<const PackageFormat::LocalVar*>(pool->values()->Get(id - 1))->base(),
                    *StaticCast<LocalVar*>(id2Value[id]));
                break;
            case PackageFormat::ValueElem_GlobalVar:
                id2Value[id] =
                    Deserialize<GlobalVar>(static_cast<const PackageFormat::GlobalVar*>(pool->values()->Get(id - 1)));
                break;
            case PackageFormat::ValueElem_Function:
                id2Value[id] =
                    Deserialize<Function>(static_cast<const PackageFormat::Function*>(pool->values()->Get(id - 1)));
                break;
            case PackageFormat::ValueElem_Block:
                id2Value[id] =
                    Deserialize<Block>(static_cast<const PackageFormat::Block*>(pool->values()->Get(id - 1)));
                ConfigValue(
                    static_cast<const PackageFormat::Block*>(pool->values()->Get(id - 1))->base(),
                    *StaticCast<Block*>(id2Value[id]));
                break;
            case PackageFormat::ValueElem_BlockGroup:
                id2Value[id] =
                    Deserialize<BlockGroup>(static_cast<const PackageFormat::BlockGroup*>(pool->values()->Get(id - 1)));
                ConfigValue(
                    static_cast<const PackageFormat::BlockGroup*>(pool->values()->Get(id - 1))->base(),
                    *StaticCast<BlockGroup*>(id2Value[id]));
                break;
            case PackageFormat::ValueElem_NONE:
                InternalError("Unsupported value type.");
                break;
            default:
                CJC_ABORT();
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
            case PackageFormat::TypeElem_Type:
                id2Type[id] =
                    Deserialize<Type>(static_cast<const PackageFormat::Type*>(pool->types()->Get(id - 1)));
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
            case PackageFormat::TypeElem_GenericType:
                id2Type[id] = Deserialize<GenericType>(
                    static_cast<const PackageFormat::GenericType*>(pool->types()->Get(id - 1)));
                break;
            case PackageFormat::TypeElem_NONE:
                id2Type[id] = nullptr;
                break;
            default:
                CJC_ABORT();
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
            case PackageFormat::ExpressionElem_AllocateBase:
                id2Expression[id] = Deserialize<Expression>(
                    static_cast<const PackageFormat::AllocateBase*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::AllocateBase*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_ApplyBase:
                id2Expression[id] =
                    Deserialize<Expression>(static_cast<const PackageFormat::ApplyBase*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::ApplyBase*>(pool->exprs()->Get(id - 1))->base()->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_BinaryExpressionBase:
                id2Expression[id] = Deserialize<Expression>(
                    static_cast<const PackageFormat::BinaryExpressionBase*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::BinaryExpressionBase*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Branch:
                id2Expression[id] =
                    Deserialize<Branch>(static_cast<const PackageFormat::Branch*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::Branch*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Debug:
                id2Expression[id] =
                    Deserialize<Debug>(static_cast<const PackageFormat::Debug*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::Debug*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Expression:
                id2Expression[id] = Deserialize<Expression>(
                    static_cast<const PackageFormat::Expression*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::Expression*>(pool->exprs()->Get(id - 1)),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Field:
                id2Expression[id] =
                    Deserialize<Field>(static_cast<const PackageFormat::Field*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::Field*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_FieldByName:
                id2Expression[id] = Deserialize<FieldByName>(
                    static_cast<const PackageFormat::FieldByName*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::FieldByName*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_GetElementByName:
                id2Expression[id] = Deserialize<GetElementByName>(
                    static_cast<const PackageFormat::GetElementByName*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::GetElementByName*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_GetElementRef:
                id2Expression[id] = Deserialize<GetElementRef>(
                    static_cast<const PackageFormat::GetElementRef*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::GetElementRef*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_GetInstantiateValue:
                id2Expression[id] = Deserialize<GetInstantiateValue>(
                    static_cast<const PackageFormat::GetInstantiateValue*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::GetInstantiateValue*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_GetRTTIStatic: {
                auto v = static_cast<const PackageFormat::GetRTTIStatic*>(pool->exprs()->Get(id - 1));
                id2Expression[id] = Deserialize<GetRTTIStatic>(v);
                ConfigExpression(v->base(), *id2Expression[id]);
                break;
            }
            case PackageFormat::ExpressionElem_InstanceOf:
                id2Expression[id] =
                    Deserialize<InstanceOf>(static_cast<const PackageFormat::InstanceOf*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::InstanceOf*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_IntrinsicBase:
                id2Expression[id] = Deserialize<Expression>(
                    static_cast<const PackageFormat::IntrinsicBase*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::IntrinsicBase*>(pool->exprs()->Get(id - 1))->base()->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_InvokeBase:
                id2Expression[id] =
                    Deserialize<Expression>(static_cast<const PackageFormat::InvokeBase*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::InvokeBase*>(pool->exprs()->Get(id - 1))->base()->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_Lambda:
                id2Expression[id] =
                    Deserialize<Lambda>(static_cast<const PackageFormat::Lambda*>(pool->exprs()->Get(id - 1)));
                Config(
                    static_cast<const PackageFormat::Lambda*>(pool->exprs()->Get(id - 1)), *GetExpression<Lambda>(id));
                break;
            case PackageFormat::ExpressionElem_MultiBranch:
                id2Expression[id] = Deserialize<MultiBranch>(
                    static_cast<const PackageFormat::MultiBranch*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::MultiBranch*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_NumericCastBase:
                id2Expression[id] = Deserialize<Expression>(
                    static_cast<const PackageFormat::NumericCastBase*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::NumericCastBase*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_RawArrayAllocateBase:
                id2Expression[id] = Deserialize<Expression>(
                    static_cast<const PackageFormat::RawArrayAllocateBase*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::RawArrayAllocateBase*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_SpawnBase:
                id2Expression[id] = Deserialize<Expression>(
                    static_cast<const PackageFormat::SpawnBase*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::SpawnBase*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_StoreElementByName:
                id2Expression[id] = Deserialize<StoreElementByName>(
                    static_cast<const PackageFormat::StoreElementByName*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::StoreElementByName*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_StoreElementRef:
                id2Expression[id] = Deserialize<StoreElementRef>(
                    static_cast<const PackageFormat::StoreElementRef*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::StoreElementRef*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            case PackageFormat::ExpressionElem_UnaryExpressionBase:
                id2Expression[id] = Deserialize<Expression>(
                    static_cast<const PackageFormat::UnaryExpressionBase*>(pool->exprs()->Get(id - 1)));
                ConfigExpression(
                    static_cast<const PackageFormat::UnaryExpressionBase*>(pool->exprs()->Get(id - 1))->base(),
                    *id2Expression[id]);
                break;
            default:
                CJC_ABORT();
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
            default:
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

// =========================== Entry ==================================
void CHIRDeserializer::CHIRDeserializerImpl::Run(const PackageFormat::CHIRPackage* package)
{
    pool = package;
    if (!builder.GetCurPackage()) {
        builder.CreatePackage(pool->name()->str());
        builder.GetCurPackage()->SetPackageAccessLevel(Package::AccessLevel(pool->pkgAccessLevel()));
    }
    // 1. deserialize CustomTypeDef, for order and CustomType
    //    CustomType will be used in Value, we need to prepare in order first
    for (unsigned id = 1; id <= pool->defs()->size(); ++id) {
        GetCustomTypeDef<CustomTypeDef>(id);
    }

    // 2. deserialize global function and global variable, only for order
    for (unsigned id = 1; id <= pool->values()->size(); ++id) {
        auto kind = PackageFormat::ValueElem(pool->values_type()->Get(id - 1));
        if (kind == PackageFormat::ValueElem_Function || kind == PackageFormat::ValueElem_GlobalVar) {
            GetValue<Value>(id);
        }
    }

    // 3. fill in CustomTypeDef, its member methods, member vars and so on
    //    this step must before filling in function, because CustomTypeDef's member var may be used in func body
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

    // 4. fill in global var, function, block and block group
    //    when fill in function, we only set parameter and func body, not fill in func body,
    //    when fill in block group, we only set sub blocks, not fill in sub blocks,
    //    that means we only fill in sub structure, not recursively, because loop structures can be
    //    constructed from blocks and expressions
    for (unsigned id = 1; id <= pool->values()->size(); ++id) {
        switch (pool->values_type()->Get(id - 1)) {
            case PackageFormat::ValueElem_GlobalVar:
                Config(static_cast<const PackageFormat::GlobalVar*>(pool->values()->Get(id - 1)),
                    *GetValue<GlobalVar>(id));
                break;
            case PackageFormat::ValueElem_Function:
                Config(static_cast<const PackageFormat::Function*>(pool->values()->Get(id - 1)),
                    *GetValue<Function>(id));
                break;
            case PackageFormat::ValueElem_Block:
                Config(static_cast<const PackageFormat::Block*>(pool->values()->Get(id - 1)), *GetValue<Block>(id));
                break;
            case PackageFormat::ValueElem_BlockGroup:
                Config(static_cast<const PackageFormat::BlockGroup*>(pool->values()->Get(id - 1)),
                    *GetValue<BlockGroup>(id));
                break;
            default:
                // do nothing
                break;
        }
    }

    // 5. update LocalVar's information and Function's inner id
    //    if local var is used in expression, its information has been updaetd while deserializing expression
    //    if local var isn't used in expression, its information need to be updated here
    for (unsigned id = 1; id <= pool->values()->size(); ++id) {
        auto kind = PackageFormat::ValueElem(pool->values_type()->Get(id - 1));
        if (kind == PackageFormat::ValueElem_LocalVar) {
            GetValue<Value>(id);
        } else if (kind == PackageFormat::ValueElem_Function) {
            auto buffer = static_cast<const PackageFormat::Function*>(pool->values()->Get(id - 1));
            auto obj = GetValue<Function>(id);
            obj->SetLocalId(buffer->localId());
            obj->SetBlockId(buffer->blockId());
            obj->SetBlockGroupId(buffer->blockGroupId());
        }
    }

    // 6. fill in upper bounds for genric types, we don't set upper bounds while deserializing GenericType,
    //    because we need to keep the CustomTypeDef's order
    for (unsigned i = 0; i < genericTypeConfig.size(); i++) {
        // for-loop is used because genericTypeConfig can be modified on the fly
        auto genericType = genericTypeConfig[i].first;
        auto rawGenericType = genericTypeConfig[i].second;
        // Fill in sub-fields
        auto upperBounds = GetType<Type>(rawGenericType->upperBounds());
        genericType->SetUpperBounds(upperBounds);
    }

    // 7. fill in Package's information
    auto initFunc = GetValue<Function>(pool->packageInitFunc());
    CJC_ASSERT(initFunc != nullptr);
    builder.GetCurPackage()->SetPackageInitFunc(initFunc);

    auto reInitFunc = GetValue<Function>(pool->packageLiteralInitFunc());
    CJC_ASSERT(reInitFunc != nullptr);
    builder.GetCurPackage()->SetPackageLiteralInitFunc(reInitFunc);
}
