// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Serializer/CHIRSerializer.h"
#include "CHIRSerializerImpl.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Annotation.h"
#include "cangjie/CHIR/IR/IntrinsicKind.h"
#include "cangjie/CHIR/Utils/ToStringUtils.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/EnumDef.h"
#include "cangjie/CHIR/IR/Type/ExtendDef.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/Utils/UserDefinedType.h"
#include "cangjie/Utils/ICEUtil.h"
#include "flatbuffers/PackageFormat_generated.h"
#include "flatbuffers/buffer.h"

#include <algorithm>
#include <set>
#include <unordered_set>
#include <vector>

using namespace Cangjie::CHIR;

void CHIRSerializer::Serialize(const Package& package, const std::string filename, ToCHIR::Phase phase)
{
    CHIRSerializerImpl serializer(package);
    serializer.Initialize();
    serializer.Dispatch();
    serializer.Save(filename, phase);
}

// ========================== ID Fetchers ==============================

template <typename T, typename E> std::vector<uint32_t> CHIRSerializer::CHIRSerializerImpl::GetId(std::vector<E*> vec)
{
    std::vector<uint32_t> indices;
    for (E* elem : vec) {
        uint32_t id = GetId<T>(static_cast<const T*>(elem));
        indices.emplace_back(id);
    }
    return indices;
}

template <typename T, typename E>
std::vector<uint32_t> CHIRSerializer::CHIRSerializerImpl::GetId(std::vector<Ptr<E>> vec)
{
    std::vector<uint32_t> indices;
    for (Ptr<E> elem : vec) {
        uint32_t id = GetId<T>(static_cast<const T*>(elem.get()));
        indices.emplace_back(id);
    }
    return indices;
}

template <typename T, typename E>
std::vector<uint32_t> CHIRSerializer::CHIRSerializerImpl::GetId(const std::unordered_set<E*>& set) const
{
    std::vector<uint32_t> indices;
    for (E* elem : set) {
        uint32_t id = GetId<T>(static_cast<const T*>(elem));
        indices.emplace_back(id);
    }
    return indices;
}

template <> uint32_t CHIRSerializer::CHIRSerializerImpl::GetId(const Value* obj)
{
    if (value2Id.count(obj) == 0) {
        value2Id[obj] = ++valueCount;
        allValue.emplace_back(0);
        valueKind.emplace_back(0);
        valueQueue.push_back(obj);
    }
    return value2Id[obj];
}

template <> uint32_t CHIRSerializer::CHIRSerializerImpl::GetId(const Type* obj)
{
    if (type2Id.count(obj) == 0) {
        type2Id[obj] = ++typeCount;
        allType.emplace_back(0);
        typeKind.emplace_back(0);
        typeQueue.push(obj);
    }
    return type2Id[obj];
}

template <> uint32_t CHIRSerializer::CHIRSerializerImpl::GetId(const Expression* obj)
{
    if (expr2Id.count(obj) == 0) {
        expr2Id[obj] = ++exprCount;
        allExpression.emplace_back(0);
        exprKind.emplace_back(0);
        exprQueue.push(obj);
    }
    return expr2Id[obj];
}

template <> uint32_t CHIRSerializer::CHIRSerializerImpl::GetId(const CustomTypeDef* obj)
{
    if (def2Id.count(obj) == 0) {
        def2Id[obj] = ++defCount;
        allCustomTypeDef.emplace_back(0);
        defKind.emplace_back(0);
        defQueue.push_back(obj);
    }
    return def2Id[obj];
}

// ========================== Helper Serializers ===============================
template <typename FBT, typename T>
std::vector<flatbuffers::Offset<FBT>> CHIRSerializer::CHIRSerializerImpl::SerializeSetToVec(
    const std::unordered_set<T>& set) const
{
    std::vector<flatbuffers::Offset<FBT>> retval;
    for (T elem : set) {
        retval.emplace_back(Serialize<FBT>(elem));
    }
    return retval;
}

template <typename FBT, typename T>
std::vector<flatbuffers::Offset<FBT>> CHIRSerializer::CHIRSerializerImpl::SerializeVec(const std::vector<T>& vec)
{
    std::vector<flatbuffers::Offset<FBT>> retval;
    for (T elem : vec) {
        retval.emplace_back(Serialize<FBT>(elem));
    }
    return retval;
}

template <>
flatbuffers::Offset<PackageFormat::DebugLocation> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const DebugLocation& obj)
{
    auto beginPos = PackageFormat::CreatePos(builder, obj.GetBeginPos().line, obj.GetBeginPos().column);
    auto endPos = PackageFormat::CreatePos(builder, obj.GetEndPos().line, obj.GetEndPos().column);
    auto scope = obj.GetScopeInfo();
    return PackageFormat::CreateDebugLocationDirect(
        builder, obj.GetAbsPath().c_str(), obj.GetFileID(), beginPos, endPos, &scope);
}

template <>
flatbuffers::Offset<PackageFormat::AnnoInfo> CHIRSerializer::CHIRSerializerImpl::Serialize(const AnnoInfo& obj)
{
    return PackageFormat::CreateAnnoInfoDirect(builder, obj.mangledName.data());
}

[[maybe_unused]] static void Empty(Annotation*)
{
}

template <> flatbuffers::Offset<PackageFormat::Base> CHIRSerializer::CHIRSerializerImpl::Serialize(const Base& obj)
{
    auto annoTypes = std::vector<uint8_t>();
    auto annos = std::vector<flatbuffers::Offset<void>>();
    std::unordered_map<std::type_index, std::function<void(Annotation*)>> annoHandler;

    // NeedCheckArrayBound
    annoHandler[typeid(CHIR::NeedCheckArrayBound)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(PackageFormat::Annotation::Annotation_needCheckArrayBound);
        annos.emplace_back(PackageFormat::CreateNeedCheckArrayBound(
            builder, NeedCheckArrayBound::Extract(StaticCast<NeedCheckArrayBound*>(anno)))
                .Union());
    };

    // NeedCheckCast
    annoHandler[typeid(CHIR::NeedCheckCast)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(PackageFormat::Annotation::Annotation_needCheckCast);
        annos.emplace_back(
            PackageFormat::CreateNeedCheckCast(builder, NeedCheckCast::Extract(StaticCast<NeedCheckCast*>(anno)))
                .Union());
    };

    // DebugLocationInfo
    annoTypes.emplace_back(PackageFormat::Annotation::Annotation_debugLocationInfo);
    annos.emplace_back(Serialize<PackageFormat::DebugLocation>(obj.Base::GetDebugLocation()).Union());

    // DebugLocationInfoForWarning
    annoHandler[typeid(CHIR::DebugLocationInfoForWarning)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(PackageFormat::Annotation::Annotation_debugLocationInfoForWarning);
        annos.emplace_back(Serialize<PackageFormat::DebugLocation>(
            DebugLocationInfoForWarning::Extract(StaticCast<DebugLocationInfoForWarning*>(anno)))
                .Union());
    };

    // LinkTypeInfo
    annoHandler[typeid(CHIR::LinkTypeInfo)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(PackageFormat::Annotation::Annotation_linkTypeInfo);
        annos.emplace_back(PackageFormat::CreateLinkTypeInfo(
            builder, PackageFormat::Linkage(LinkTypeInfo::Extract(StaticCast<CHIR::LinkTypeInfo*>(anno))))
                .Union());
    };

    // SkipCheck
    annoHandler[typeid(CHIR::SkipCheck)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(PackageFormat::Annotation::Annotation_skipCheck);
        annos.emplace_back(PackageFormat::CreateSkipCheck(
            builder, PackageFormat::SkipKind(SkipCheck::Extract(StaticCast<CHIR::SkipCheck*>(anno))))
                .Union());
    };

    // WrappedRawMethod may be removed body when removeUnusedImported, do not serializer it
    auto wrapMethod = dynamic_cast<Func*>(obj.Get<CHIR::WrappedRawMethod>());
    if (wrapMethod != nullptr && !wrapMethod->GetBody()) {
        annoHandler[typeid(CHIR::WrappedRawMethod)] = Empty;
    } else {
        annoHandler[typeid(CHIR::WrappedRawMethod)] = [this, &annos, &annoTypes](Annotation* anno) {
            annoTypes.push_back(PackageFormat::Annotation::Annotation_wrappedRawMethod);
            auto rawMethod =
                GetId<Value>(StaticCast<Value*>(WrappedRawMethod::Extract(StaticCast<CHIR::WrappedRawMethod*>(anno))));
            annos.emplace_back(PackageFormat::CreateWrappedRawMethod(builder, rawMethod).Union());
        };
    }
    // NeverOverflowInfo
    annoHandler[typeid(CHIR::NeverOverflowInfo)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(PackageFormat::Annotation::Annotation_neverOverflowInfo);
        annos.emplace_back(PackageFormat::CreateNeverOverflowInfo(
            builder, NeverOverflowInfo::Extract(StaticCast<CHIR::NeverOverflowInfo*>(anno)))
                .Union());
    };

    // GeneratedFromForIn
    annoHandler[typeid(CHIR::GeneratedFromForIn)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(PackageFormat::Annotation::Annotation_generatedFromForIn);
        annos.emplace_back(PackageFormat::CreateGeneratedFromForIn(
            builder, GeneratedFromForIn::Extract(StaticCast<CHIR::GeneratedFromForIn*>(anno)))
                .Union());
    };

    // IsAutoEnvClass
    annoHandler[typeid(CHIR::IsAutoEnvClass)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(PackageFormat::Annotation::Annotation_isAutoEnvClass);
        annos.emplace_back(PackageFormat::CreateIsAutoEnvClass(
            builder, IsAutoEnvClass::Extract(StaticCast<CHIR::IsAutoEnvClass*>(anno)))
                .Union());
    };

    // IsCapturedClassInCC
    annoHandler[typeid(CHIR::IsCapturedClassInCC)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(PackageFormat::Annotation::Annotation_isCapturedClassInCC);
        annos.emplace_back(PackageFormat::CreateIsCapturedClassInCC(
            builder, IsCapturedClassInCC::Extract(StaticCast<CHIR::IsCapturedClassInCC*>(anno)))
                .Union());
    };

    // EnumCaseIndex
    annoHandler[typeid(CHIR::EnumCaseIndex)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(PackageFormat::Annotation::Annotation_enumCaseIndex);
        auto index = EnumCaseIndex::Extract(StaticCast<CHIR::EnumCaseIndex*>(anno));
        int64_t indexNum = -1;
        if (index.has_value()) {
            indexNum = static_cast<int64_t>(index.value());
        }
        annos.emplace_back(PackageFormat::CreateEnumCaseIndex(builder, indexNum).Union());
    };

    // VirMethodOffset
    annoHandler[typeid(CHIR::VirMethodOffset)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(PackageFormat::Annotation::Annotation_virMethodOffset);
        auto offset = VirMethodOffset::Extract(StaticCast<CHIR::VirMethodOffset*>(anno));
        int64_t offsetNum = -1;
        if (offset.has_value()) {
            offsetNum = static_cast<int64_t>(offset.value());
        }
        annos.emplace_back(PackageFormat::CreateVirMethodOffset(builder, offsetNum).Union());
    };

    // OverrideSrcFuncType
    annoHandler[typeid(CHIR::OverrideSrcFuncType)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(PackageFormat::Annotation::Annotation_overrideSrcFuncType);
        auto funcType = GetId<Type>(OverrideSrcFuncType::Extract(StaticCast<CHIR::OverrideSrcFuncType*>(anno)));
        annos.emplace_back(PackageFormat::CreateOverrideSrcFuncType(builder, funcType).Union());
    };
    
    annoHandler[typeid(CHIR::AnnoFactoryInfo)] = Empty;

    for (auto& entry : obj.GetAnno().GetAnnos()) {
        if (annoHandler.count(entry.first) != 0) {
            annoHandler.at(entry.first)(entry.second.get());
        } else {
            CJC_ABORT();
        }
    }
    return PackageFormat::CreateBaseDirect(builder, &annoTypes, &annos);
}

template <>
flatbuffers::Offset<PackageFormat::Expression> CHIRSerializer::CHIRSerializerImpl::Serialize(const Expression& obj);

template <> flatbuffers::Offset<PackageFormat::Tuple> CHIRSerializer::CHIRSerializerImpl::Serialize(const Tuple& obj);

template <>
flatbuffers::Offset<PackageFormat::MemberVarInfo> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const MemberVarInfo& obj)
{
    auto name = obj.name;
    auto rawMangledName = obj.rawMangledName;
    auto type = GetId<Type>(obj.type);
    auto attributes = obj.attributeInfo.GetRawAttrs().to_ulong();
    auto loc = Serialize<PackageFormat::DebugLocation>(obj.loc);
    auto annoInfo = Serialize<PackageFormat::AnnoInfo>(obj.annoInfo);
    auto initializerFunc = GetId<Value>(obj.initializerFunc);
    auto outerDef = GetId<CustomTypeDef>(obj.outerDef);
    return PackageFormat::CreateMemberVarInfoDirect(
        builder, name.data(), rawMangledName.data(), type, attributes, loc, annoInfo, initializerFunc, outerDef);
}

template <>
flatbuffers::Offset<PackageFormat::EnumCtorInfo> CHIRSerializer::CHIRSerializerImpl::Serialize(const EnumCtorInfo& obj)
{
    return PackageFormat::CreateEnumCtorInfoDirect(
        builder, obj.name.data(), obj.mangledName.data(), GetId<Type>(obj.funcType));
}

template <>
flatbuffers::Offset<PackageFormat::AbstractMethodParam> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const AbstractMethodParam& obj)
{
    auto paramName = obj.paramName;
    auto paramType = GetId<Type>(obj.type);
    auto annoInfo = Serialize<PackageFormat::AnnoInfo>(obj.annoInfo);
    return PackageFormat::CreateAbstractMethodParamDirect(builder, paramName.data(), paramType, annoInfo);
}

template <>
flatbuffers::Offset<PackageFormat::AbstractMethodInfo> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const AbstractMethodInfo& obj)
{
    auto methodName = obj.methodName;
    auto mangledName = obj.GetASTMangledName();
    auto methodType = GetId<Type>(obj.methodTy);
    auto paramsInfo = SerializeVec<PackageFormat::AbstractMethodParam>(obj.paramInfos);
    auto attributes = obj.attributeInfo.GetRawAttrs().to_ulong();
    auto annoInfo = Serialize<PackageFormat::AnnoInfo>(obj.annoInfo);
    auto methodGenericTypeParams = GetId<Type>(obj.methodGenericTypeParams);
    auto hasBody = obj.hasBody;
    auto parent = GetId<CustomTypeDef>(obj.parent);
    return PackageFormat::CreateAbstractMethodInfoDirect(builder, methodName.data(), mangledName.data(), methodType,
        &paramsInfo, attributes, annoInfo, methodGenericTypeParams.empty() ? nullptr : &methodGenericTypeParams,
        hasBody, parent);
}

// ========================== Type Serializers =================================

template <> flatbuffers::Offset<PackageFormat::Type> CHIRSerializer::CHIRSerializerImpl::Serialize(const Type& obj)
{
    auto typeId = GetId<Type>(&obj);
    auto kind = PackageFormat::CHIRTypeKind(obj.GetTypeKind());
    auto argTys = GetId<Type>(obj.GetTypeArgs());
    return PackageFormat::CreateTypeDirect(builder, kind, typeId, argTys.empty() ? nullptr : &argTys);
}

template <>
flatbuffers::Offset<PackageFormat::RuneType> CHIRSerializer::CHIRSerializerImpl::Serialize(const RuneType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    return PackageFormat::CreateRuneTypeDirect(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::BooleanType> CHIRSerializer::CHIRSerializerImpl::Serialize(const BooleanType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    return PackageFormat::CreateBooleanTypeDirect(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::UnitType> CHIRSerializer::CHIRSerializerImpl::Serialize(const UnitType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    return PackageFormat::CreateUnitTypeDirect(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::NothingType> CHIRSerializer::CHIRSerializerImpl::Serialize(const NothingType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    return PackageFormat::CreateNothingTypeDirect(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::NumericType> CHIRSerializer::CHIRSerializerImpl::Serialize(const NumericType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    return PackageFormat::CreateNumericTypeDirect(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::TupleType> CHIRSerializer::CHIRSerializerImpl::Serialize(const TupleType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    return PackageFormat::CreateTupleType(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::RawArrayType> CHIRSerializer::CHIRSerializerImpl::Serialize(const RawArrayType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    auto dims = obj.GetDims();
    return PackageFormat::CreateRawArrayType(builder, base, dims);
}

template <>
flatbuffers::Offset<PackageFormat::VArrayType> CHIRSerializer::CHIRSerializerImpl::Serialize(const VArrayType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    auto size = obj.GetSize();
    return PackageFormat::CreateVArrayType(builder, base, size);
}

template <>
flatbuffers::Offset<PackageFormat::FuncType> CHIRSerializer::CHIRSerializerImpl::Serialize(const FuncType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    auto isCFuncType = obj.IsCFunc();
    auto hasVarArg = obj.HasVarArg();
    return PackageFormat::CreateFuncType(builder, base, isCFuncType, hasVarArg);
}

template <>
flatbuffers::Offset<PackageFormat::CustomType> CHIRSerializer::CHIRSerializerImpl::Serialize(const CustomType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    auto customTypeDef = GetId<CustomTypeDef>(obj.GetCustomTypeDef());
    return PackageFormat::CreateCustomType(builder, base, customTypeDef);
}

template <>
flatbuffers::Offset<PackageFormat::CStringType> CHIRSerializer::CHIRSerializerImpl::Serialize(const CStringType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    return PackageFormat::CreateCStringTypeDirect(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::CPointerType> CHIRSerializer::CHIRSerializerImpl::Serialize(const CPointerType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    return PackageFormat::CreateCPointerTypeDirect(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::GenericType> CHIRSerializer::CHIRSerializerImpl::Serialize(const GenericType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    auto identifier = obj.GetIdentifier();
    auto srcCodeIndentifier = obj.GetSrcCodeIdentifier();
    auto upperBounds = GetId<Type>(obj.GetUpperBounds());
    return PackageFormat::CreateGenericTypeDirect(builder, base, identifier.data(),
        srcCodeIndentifier.data(), upperBounds.empty() ? nullptr : &upperBounds);
}

template <>
flatbuffers::Offset<PackageFormat::RefType> CHIRSerializer::CHIRSerializerImpl::Serialize(const RefType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    return PackageFormat::CreateRefType(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::BoxType> CHIRSerializer::CHIRSerializerImpl::Serialize(const BoxType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    return PackageFormat::CreateBoxType(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::VoidType> CHIRSerializer::CHIRSerializerImpl::Serialize(const VoidType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    return PackageFormat::CreateVoidType(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::ThisType> CHIRSerializer::CHIRSerializerImpl::Serialize(const ThisType& obj)
{
    auto base = Serialize<PackageFormat::Type>(static_cast<const Type&>(obj));
    return PackageFormat::CreateThisType(builder, base);
}

// ========================== Numeric Type Serializers =========================

template <>
flatbuffers::Offset<PackageFormat::IntType> CHIRSerializer::CHIRSerializerImpl::Serialize(const IntType& obj)
{
    auto base = Serialize<PackageFormat::NumericType>(static_cast<const NumericType&>(obj));
    return PackageFormat::CreateIntType(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::FloatType> CHIRSerializer::CHIRSerializerImpl::Serialize(const FloatType& obj)
{
    auto base = Serialize<PackageFormat::NumericType>(static_cast<const NumericType&>(obj));
    return PackageFormat::CreateFloatType(builder, base);
}

// ========================== Custom Type Serializers ==========================

template <>
flatbuffers::Offset<PackageFormat::EnumType> CHIRSerializer::CHIRSerializerImpl::Serialize(const EnumType& obj)
{
    auto base = Serialize<PackageFormat::CustomType>(static_cast<const CustomType&>(obj));
    return PackageFormat::CreateEnumType(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::StructType> CHIRSerializer::CHIRSerializerImpl::Serialize(const StructType& obj)
{
    auto base = Serialize<PackageFormat::CustomType>(static_cast<const CustomType&>(obj));
    return PackageFormat::CreateStructType(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::ClassType> CHIRSerializer::CHIRSerializerImpl::Serialize(const ClassType& obj)
{
    auto base = Serialize<PackageFormat::CustomType>(static_cast<const CustomType&>(obj));
    return PackageFormat::CreateClassType(builder, base);
}

// ======================= Value Serializers ===================================
template <> flatbuffers::Offset<PackageFormat::Value> CHIRSerializer::CHIRSerializerImpl::Serialize(const Value& obj)
{
    auto base = Serialize<PackageFormat::Base>(static_cast<const Base&>(obj));
    auto valueId = GetId<Value>(&obj);
    auto identifier = obj.GetIdentifier();
    auto type = GetId<Type>(obj.GetType());
    auto kind = PackageFormat::ValueKind(obj.GetValueKind());
    auto attributes = obj.GetAttributeInfo().GetRawAttrs().to_ulong();
    auto annoInfo = Serialize<PackageFormat::AnnoInfo>(obj.GetAnnoInfo());
    return PackageFormat::CreateValueDirect(
        builder, base, type, identifier.data(), kind, valueId, attributes, annoInfo);
}

template <>
flatbuffers::Offset<PackageFormat::Parameter> CHIRSerializer::CHIRSerializerImpl::Serialize(const Parameter& obj)
{
    auto base = Serialize<PackageFormat::Value>(static_cast<const Value&>(obj));
    auto ownedFunc = GetId<Value>(obj.GetOwnerFunc());
    auto ownedLambda = GetId<Expression>(obj.GetOwnerLambda());
    return PackageFormat::CreateParameter(builder, base, ownedFunc, ownedLambda);
}

template <>
flatbuffers::Offset<PackageFormat::LocalVar> CHIRSerializer::CHIRSerializerImpl::Serialize(const LocalVar& obj)
{
    auto base = Serialize<PackageFormat::Value>(static_cast<const Value&>(obj));
    auto associatedExpr = GetId<Expression>(obj.GetExpr());
    auto isRetVal = obj.IsRetValue();
    return PackageFormat::CreateLocalVar(builder, base, associatedExpr, isRetVal);
}

template <>
flatbuffers::Offset<PackageFormat::GlobalVar> CHIRSerializer::CHIRSerializerImpl::Serialize(const GlobalVar& obj)
{
    auto base = Serialize<PackageFormat::Value>(static_cast<const Value&>(obj));
    auto srcCodeIdentifier = obj.GetSrcCodeIdentifier();
    auto packageName = obj.GetPackageName();
    auto rawMangledName = obj.GetRawMangledName();
    auto defaultInitVal = GetId<Value>(obj.GetInitializer());
    auto associatedInitFunc = GetId<Value>(obj.GetInitFunc());
    auto declaredParent = GetId<CustomTypeDef>(obj.GetParentCustomTypeDef());
    return PackageFormat::CreateGlobalVarDirect(builder, base, rawMangledName.data(), srcCodeIdentifier.data(),
        packageName.data(), defaultInitVal, associatedInitFunc, declaredParent);
}

template <> flatbuffers::Offset<PackageFormat::Block> CHIRSerializer::CHIRSerializerImpl::Serialize(const Block& obj)
{
    auto base = Serialize<PackageFormat::Value>(static_cast<const Value&>(obj));
    auto parentGroup = GetId<Value>(obj.GetParentBlockGroup());
    auto exprs = GetId<Expression>(obj.GetExpressions());
    auto predecessors = GetId<Value>(obj.GetPredecessors());
    auto exceptionCatchList = GetId<Type>(obj.IsLandingPadBlock() ? obj.GetExceptions() : std::vector<ClassType*>());
    return PackageFormat::CreateBlockDirect(builder, base, parentGroup, exprs.empty() ? nullptr : &exprs,
        predecessors.empty() ? nullptr : &predecessors, obj.IsLandingPadBlock(),
        exceptionCatchList.empty() ? nullptr : &exceptionCatchList);
}

template <>
flatbuffers::Offset<PackageFormat::BlockGroup> CHIRSerializer::CHIRSerializerImpl::Serialize(const BlockGroup& obj)
{
    CJC_ASSERT(obj.GetOwnerFunc() || obj.GetOwnerExpression());
    auto base = Serialize<PackageFormat::Value>(static_cast<const Value&>(obj));
    auto entryBlock = GetId<Value>(obj.GetEntryBlock());
    auto blocks = GetId<Value>(obj.GetBlocks());
    auto ownedFunc = GetId<Value>(obj.GetOwnerFunc());
    auto ownedExpression = GetId<Expression>(obj.GetOwnerExpression());
    return PackageFormat::CreateBlockGroupDirect(
        builder, base, entryBlock, blocks.empty() ? nullptr : &blocks, ownedFunc, ownedExpression);
}

template <> flatbuffers::Offset<PackageFormat::Func> CHIRSerializer::CHIRSerializerImpl::Serialize(const Func& obj)
{
    auto base = Serialize<PackageFormat::Value>(static_cast<const Value&>(obj));
    // FuncBase
    auto srcCodeIdentifier = obj.GetSrcCodeIdentifier();
    auto rawMangledName = obj.GetRawMangledName();
    auto packageName = obj.GetPackageName();
    auto declaredParent = GetId<CustomTypeDef>(obj.GetParentCustomTypeDef());
    bool skipGenericDecl = false;
    uint32_t genericDecl = 0;
    // genericFunc may be removed body when removeUnusedImported, do not serializer it
    if (auto genericFunc = DynamicCast<Func*>(obj.GetGenericDecl()); genericFunc && !genericFunc->GetBody()) {
        skipGenericDecl = true;
    }
    if (!skipGenericDecl) {
        genericDecl = GetId<Value>(obj.GetGenericDecl());
    }

    auto funcKind = PackageFormat::FuncKind(obj.GetFuncKind());
    uint32_t oriLambdaFuncTy = obj.GetFuncKind() == LAMBDA ? GetId<Type>(obj.GetOriginalLambdaType()) : 0;
    std::vector<uint32_t> oriLambdaGenericTypeParams{};
    if (obj.GetFuncKind() == LAMBDA) {
        oriLambdaGenericTypeParams = GetId<Type>(obj.GetOriginalGenericTypeParams());
    }
    auto genericTypeParams = GetId<Type>(obj.GetGenericTypeParams());
    auto paramDftValHostFunc = GetId<Value>(obj.GetParamDftValHostFunc());

    // FuncBody
    CJC_NULLPTR_CHECK(obj.GetBody());
    auto body = GetId<Value>(obj.GetBody());
    auto params = GetId<Value>(obj.GetParams());
    auto retVal = GetId<Value>(obj.GetReturnValue());

    auto propLoc = Serialize<PackageFormat::DebugLocation>(obj.GetPropLocation());
    auto localId = obj.localId;
    auto blockId = obj.blockId;
    auto blockGroupId = obj.blockGroupId;
    return PackageFormat::CreateFuncDirect(builder, base, srcCodeIdentifier.data(), rawMangledName.data(),
        packageName.data(), declaredParent, genericDecl, funcKind, obj.IsFastNative(), obj.IsCFFIWrapper(),
        oriLambdaFuncTy, oriLambdaGenericTypeParams.empty() ? nullptr : &oriLambdaGenericTypeParams,
        genericTypeParams.empty() ? nullptr : &genericTypeParams, paramDftValHostFunc, body,
        params.empty() ? nullptr : &params, retVal, propLoc, localId, blockId, blockGroupId);
}

template <>
flatbuffers::Offset<PackageFormat::ImportedValue> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const ImportedValue& obj)
{
    auto base = Serialize<PackageFormat::Value>(static_cast<const Value&>(obj));
    return PackageFormat::CreateImportedValue(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::LiteralValue> CHIRSerializer::CHIRSerializerImpl::Serialize(const LiteralValue& obj)
{
    auto base = Serialize<PackageFormat::Value>(static_cast<const Value&>(obj));
    auto literalKind = PackageFormat::ConstantValueKind(obj.GetConstantValueKind());
    return PackageFormat::CreateLiteralValue(builder, base, literalKind);
}

// ======================= Imported Value Serializers ===========================

template <>
flatbuffers::Offset<PackageFormat::ImportedFunc> CHIRSerializer::CHIRSerializerImpl::Serialize(const ImportedFunc& obj)
{
    auto base = Serialize<PackageFormat::ImportedValue>(static_cast<const ImportedValue&>(obj));
    // FuncBase
    auto srcCodeIdentifier = obj.GetSrcCodeIdentifier();
    auto rawMangledName = obj.GetRawMangledName();
    auto packageName = obj.GetPackageName();
    auto declaredParent = GetId<CustomTypeDef>(obj.GetParentCustomTypeDef());
    bool skipGenericDecl = false;
    uint32_t genericDecl = 0;
    // genericFunc may be removed body when removeUnusedImported, do not serializer it
    if (auto genericFunc = DynamicCast<Func*>(obj.GetGenericDecl()); genericFunc && !genericFunc->GetBody()) {
        skipGenericDecl = true;
    }
    if (!skipGenericDecl) {
        genericDecl = GetId<Value>(obj.GetGenericDecl());
    }
    auto funcKind = PackageFormat::FuncKind(obj.GetFuncKind());
    uint32_t oriLambdaFuncTy = obj.GetFuncKind() == LAMBDA ? GetId<Type>(obj.GetOriginalLambdaType()) : 0;
    std::vector<uint32_t> oriLambdaGenericTypeParams{};
    if (obj.GetFuncKind() == LAMBDA) {
        oriLambdaGenericTypeParams = GetId<Type>(obj.GetOriginalGenericTypeParams());
    }
    auto genericTypeParams = GetId<Type>(obj.GetGenericTypeParams());
    auto paramDftValHostFunc = GetId<Value>(obj.GetParamDftValHostFunc());

    auto paramInfo = SerializeVec<PackageFormat::AbstractMethodParam>(obj.GetParamInfo());

    return PackageFormat::CreateImportedFuncDirect(builder, base, srcCodeIdentifier.data(), rawMangledName.data(),
        packageName.data(), declaredParent, genericDecl, funcKind, obj.IsFastNative(), obj.IsCFFIWrapper(),
        oriLambdaFuncTy, oriLambdaGenericTypeParams.empty() ? nullptr : &oriLambdaGenericTypeParams,
        genericTypeParams.empty() ? nullptr : &genericTypeParams, paramDftValHostFunc, &paramInfo);
}

template <>
flatbuffers::Offset<PackageFormat::ImportedVar> CHIRSerializer::CHIRSerializerImpl::Serialize(const ImportedVar& obj)
{
    auto base = Serialize<PackageFormat::ImportedValue>(static_cast<const ImportedValue&>(obj));
    auto packageName = obj.GetPackageName();
    auto srcCodeIdentifier = obj.GetSrcCodeIdentifier();
    auto rawMangledName = obj.GetRawMangledName();
    auto declaredParent = GetId<CustomTypeDef>(obj.GetParentCustomTypeDef());
    return PackageFormat::CreateImportedVarDirect(
        builder, base, packageName.c_str(), srcCodeIdentifier.c_str(), rawMangledName.c_str(), declaredParent);
}

// ======================= Literal Value Serializers ===========================
template <>
flatbuffers::Offset<PackageFormat::BoolLiteral> CHIRSerializer::CHIRSerializerImpl::Serialize(const BoolLiteral& obj)
{
    auto base = Serialize<PackageFormat::LiteralValue>(static_cast<const LiteralValue&>(obj));
    auto val = obj.GetVal();
    return PackageFormat::CreateBoolLiteral(builder, base, val);
}

template <>
flatbuffers::Offset<PackageFormat::RuneLiteral> CHIRSerializer::CHIRSerializerImpl::Serialize(const RuneLiteral& obj)
{
    auto base = Serialize<PackageFormat::LiteralValue>(static_cast<const LiteralValue&>(obj));
    auto val = obj.GetVal();
    return PackageFormat::CreateRuneLiteral(builder, base, val);
}

template <>
flatbuffers::Offset<PackageFormat::StringLiteral> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const StringLiteral& obj)
{
    auto base = Serialize<PackageFormat::LiteralValue>(static_cast<const LiteralValue&>(obj));
    auto val = builder.CreateSharedString(obj.GetVal());
    return PackageFormat::CreateStringLiteral(builder, base, val);
}

template <>
flatbuffers::Offset<PackageFormat::IntLiteral> CHIRSerializer::CHIRSerializerImpl::Serialize(const IntLiteral& obj)
{
    auto base = Serialize<PackageFormat::LiteralValue>(static_cast<const LiteralValue&>(obj));
    auto val = obj.GetUnsignedVal();
    return PackageFormat::CreateIntLiteral(builder, base, val);
}

template <>
flatbuffers::Offset<PackageFormat::FloatLiteral> CHIRSerializer::CHIRSerializerImpl::Serialize(const FloatLiteral& obj)
{
    auto base = Serialize<PackageFormat::LiteralValue>(static_cast<const LiteralValue&>(obj));
    auto val = obj.GetVal();
    return PackageFormat::CreateFloatLiteral(builder, base, val);
}

template <>
flatbuffers::Offset<PackageFormat::UnitLiteral> CHIRSerializer::CHIRSerializerImpl::Serialize(const UnitLiteral& obj)
{
    auto base = Serialize<PackageFormat::LiteralValue>(static_cast<const LiteralValue&>(obj));
    return PackageFormat::CreateUnitLiteral(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::NullLiteral> CHIRSerializer::CHIRSerializerImpl::Serialize(const NullLiteral& obj)
{
    auto base = Serialize<PackageFormat::LiteralValue>(static_cast<const LiteralValue&>(obj));
    return PackageFormat::CreateNullLiteral(builder, base);
}

// ======================= Expression Serializers ==============================
template <>
flatbuffers::Offset<PackageFormat::Expression> CHIRSerializer::CHIRSerializerImpl::Serialize(const Expression& obj)
{
    auto base = Serialize<PackageFormat::Base>(static_cast<const Base&>(obj));
    auto expressionId = GetId<Expression>(&obj);
    auto kind = PackageFormat::CHIRExprKind(obj.GetExprKind());
    auto operands = GetId<Value>(obj.GetOperands());
    auto blockGroups = GetId<Value>(obj.GetBlockGroups());
    auto parentBlock = GetId<Value>(obj.GetParentBlock());
    auto resultLocalVar = GetId<Value>(obj.GetResult());
    auto resultTy = GetId<Type>(obj.GetResult() ? obj.GetResult()->GetType() : nullptr);
    return PackageFormat::CreateExpressionDirect(builder, base, kind, expressionId,
        operands.empty() ? nullptr : &operands, blockGroups.empty() ? nullptr : &blockGroups, parentBlock,
        resultLocalVar, resultTy);
}

template <>
flatbuffers::Offset<PackageFormat::UnaryExpression> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const UnaryExpression& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto overflowStrategy = PackageFormat::OverflowStrategy(obj.GetOverflowStrategy());
    return PackageFormat::CreateUnaryExpression(builder, base, overflowStrategy);
}

template <>
flatbuffers::Offset<PackageFormat::BinaryExpression> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const BinaryExpression& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto overflowStrategy = PackageFormat::OverflowStrategy(obj.GetOverflowStrategy());
    return PackageFormat::CreateBinaryExpression(builder, base, overflowStrategy);
}

template <>
flatbuffers::Offset<PackageFormat::Constant> CHIRSerializer::CHIRSerializerImpl::Serialize(const Constant& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateConstant(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::Allocate> CHIRSerializer::CHIRSerializerImpl::Serialize(const Allocate& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto targetType = GetId<Type>(obj.GetType());
    return PackageFormat::CreateAllocate(builder, base, targetType);
}

template <> flatbuffers::Offset<PackageFormat::Load> CHIRSerializer::CHIRSerializerImpl::Serialize(const Load& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateLoad(builder, base);
}

template <> flatbuffers::Offset<PackageFormat::Store> CHIRSerializer::CHIRSerializerImpl::Serialize(const Store& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateStore(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::GetElementRef> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const GetElementRef& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto path = obj.GetPath();
    return PackageFormat::CreateGetElementRefDirect(builder, base, path.empty() ? nullptr : &path);
}

template <>
flatbuffers::Offset<PackageFormat::GetElementByName> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const GetElementByName& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto names = builder.CreateVectorOfStrings(obj.GetNames());
    return PackageFormat::CreateGetElementByName(builder, base, names);
}

template <>
flatbuffers::Offset<PackageFormat::StoreElementRef> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const StoreElementRef& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto path = obj.GetPath();
    return PackageFormat::CreateStoreElementRefDirect(builder, base, &path);
}

template <>
flatbuffers::Offset<PackageFormat::StoreElementByName> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const StoreElementByName& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto names = builder.CreateVectorOfStrings(obj.GetNames());
    return PackageFormat::CreateStoreElementByName(builder, base, names);
}

template <> flatbuffers::Offset<PackageFormat::Apply> CHIRSerializer::CHIRSerializerImpl::Serialize(const Apply& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto instTypeArgs = GetId<Type>(obj.GetInstantiatedTypeArgs());
    auto isSuperCall = obj.IsSuperCall();
    auto thisType = GetId<Type>(obj.GetThisType());
    return PackageFormat::CreateApplyDirect(
        builder, base, isSuperCall, instTypeArgs.empty() ? nullptr : &instTypeArgs, thisType);
}

template <> flatbuffers::Offset<PackageFormat::Invoke> CHIRSerializer::CHIRSerializerImpl::Serialize(const Invoke& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto instTypeArgs = GetId<Type>(obj.GetInstantiatedTypeArgs());
    auto thisType = GetId<Type>(obj.GetThisType());
    auto tempTypes = GetId<Type>(obj.GetGenericTypeParams());
    auto virMethodCtx = PackageFormat::CreateVirMethodContextDirect(builder,
        obj.GetMethodName().data(), GetId<Type>(obj.GetMethodType()), tempTypes.empty() ? nullptr : &tempTypes);
    return PackageFormat::CreateInvokeDirect(
        builder, base, instTypeArgs.empty() ? nullptr : &instTypeArgs, thisType, virMethodCtx);
}

template <>
flatbuffers::Offset<PackageFormat::InvokeStatic> CHIRSerializer::CHIRSerializerImpl::Serialize(const InvokeStatic& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto instTypeArgs = GetId<Type>(obj.GetInstantiatedTypeArgs());
    auto thisType = GetId<Type>(obj.GetThisType());
    auto tempTypes = GetId<Type>(obj.GetGenericTypeParams());
    auto virMethodCtx = PackageFormat::CreateVirMethodContextDirect(builder,
        obj.GetMethodName().data(), GetId<Type>(obj.GetMethodType()), tempTypes.empty() ? nullptr : &tempTypes);
    return PackageFormat::CreateInvokeStaticDirect(
        builder, base, instTypeArgs.empty() ? nullptr : &instTypeArgs, thisType, virMethodCtx);
}

template <>
flatbuffers::Offset<PackageFormat::TypeCast> CHIRSerializer::CHIRSerializerImpl::Serialize(const TypeCast& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto overflowStrategy = PackageFormat::OverflowStrategy(obj.GetOverflowStrategy());
    return PackageFormat::CreateTypeCast(builder, base, overflowStrategy);
}

template <>
flatbuffers::Offset<PackageFormat::InstanceOf> CHIRSerializer::CHIRSerializerImpl::Serialize(const InstanceOf& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto targetType = GetId<Type>(obj.GetType());
    return PackageFormat::CreateInstanceOf(builder, base, targetType);
}

template <> flatbuffers::Offset<PackageFormat::Box> CHIRSerializer::CHIRSerializerImpl::Serialize(const Box& expr)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(expr));
    return PackageFormat::CreateBox(builder, base);
}

template <> flatbuffers::Offset<PackageFormat::UnBox> CHIRSerializer::CHIRSerializerImpl::Serialize(const UnBox& expr)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(expr));
    return PackageFormat::CreateUnBox(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::Terminator> CHIRSerializer::CHIRSerializerImpl::Serialize(const Terminator& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto successors = GetId<Value>(obj.GetSuccessors());
    return PackageFormat::CreateTerminatorDirect(builder, base, successors.empty() ? nullptr : &successors);
}

template <> flatbuffers::Offset<PackageFormat::GoTo> CHIRSerializer::CHIRSerializerImpl::Serialize(const GoTo& obj)
{
    auto base = Serialize<PackageFormat::Terminator>(static_cast<const Terminator&>(obj));
    return PackageFormat::CreateGoTo(builder, base);
}

template <> flatbuffers::Offset<PackageFormat::Branch> CHIRSerializer::CHIRSerializerImpl::Serialize(const Branch& obj)
{
    auto base = Serialize<PackageFormat::Terminator>(static_cast<const Terminator&>(obj));
    auto sourceExpr = obj.GetSourceExpr();
    return PackageFormat::CreateBranch(builder, base, PackageFormat::SourceExpr(sourceExpr));
}

template <>
flatbuffers::Offset<PackageFormat::MultiBranch> CHIRSerializer::CHIRSerializerImpl::Serialize(const MultiBranch& obj)
{
    auto base = Serialize<PackageFormat::Terminator>(static_cast<const Terminator&>(obj));
    auto caseVals = obj.GetCaseVals();
    return PackageFormat::CreateMultiBranchDirect(builder, base, caseVals.empty() ? nullptr : &caseVals);
}

template <> flatbuffers::Offset<PackageFormat::Exit> CHIRSerializer::CHIRSerializerImpl::Serialize(const Exit& obj)
{
    auto base = Serialize<PackageFormat::Terminator>(static_cast<const Terminator&>(obj));
    return PackageFormat::CreateExit(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::RaiseException> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const RaiseException& obj)
{
    auto base = Serialize<PackageFormat::Terminator>(static_cast<const Terminator&>(obj));
    return PackageFormat::CreateRaiseException(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::ApplyWithException> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const ApplyWithException& obj)
{
    CJC_ASSERT(!obj.GetOperands().empty());
    auto base = Serialize<PackageFormat::Terminator>(static_cast<const Terminator&>(obj));
    auto thisType = GetId<Type>(obj.GetThisType());
    auto instantiateArgs = GetId<Type>(obj.GetInstantiatedTypeArgs());
    return PackageFormat::CreateApplyWithExceptionDirect(
        builder, base, thisType, instantiateArgs.empty() ? nullptr : &instantiateArgs);
}

template <>
flatbuffers::Offset<PackageFormat::InvokeWithException> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const InvokeWithException& obj)
{
    CJC_ASSERT(!obj.GetOperands().empty());
    auto base = Serialize<PackageFormat::Terminator>(static_cast<const Terminator&>(obj));
    auto instTypeArgs = GetId<Type>(obj.GetInstantiatedTypeArgs());
    auto thisType = GetId<Type>(obj.GetThisType());
    auto tempTypes = GetId<Type>(obj.GetGenericTypeParams());
    auto virMethodCtx = PackageFormat::CreateVirMethodContextDirect(builder,
        obj.GetMethodName().data(), GetId<Type>(obj.GetMethodType()), tempTypes.empty() ? nullptr : &tempTypes);
    return PackageFormat::CreateInvokeWithExceptionDirect(
        builder, base, instTypeArgs.empty() ? nullptr : &instTypeArgs, thisType, virMethodCtx);
}

template <>
flatbuffers::Offset<PackageFormat::InvokeStaticWithException> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const InvokeStaticWithException& obj)
{
    auto base = Serialize<PackageFormat::Terminator>(static_cast<const Terminator&>(obj));
    auto instTypeArgs = GetId<Type>(obj.GetInstantiatedTypeArgs());
    auto thisType = GetId<Type>(obj.GetThisType());
    auto tempTypes = GetId<Type>(obj.GetGenericTypeParams());
    auto virMethodCtx = PackageFormat::CreateVirMethodContextDirect(builder,
        obj.GetMethodName().data(), GetId<Type>(obj.GetMethodType()), tempTypes.empty() ? nullptr : &tempTypes);
    return PackageFormat::CreateInvokeStaticWithExceptionDirect(
        builder, base, instTypeArgs.empty() ? nullptr : &instTypeArgs, thisType, virMethodCtx);
}

template <>
flatbuffers::Offset<PackageFormat::GetInstantiateValue> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const GetInstantiateValue& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto instantiateTys = GetId<Type>(obj.GetInstantiateTypes());
    return PackageFormat::CreateGetInstantiateValueDirect(
        builder, base, instantiateTys.empty() ? nullptr : &instantiateTys);
}

template <>
flatbuffers::Offset<PackageFormat::TransformToConcrete> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const TransformToConcrete& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateTransformToConcrete(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::TransformToGeneric> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const TransformToGeneric& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateTransformToGeneric(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::UnBoxToRef> CHIRSerializer::CHIRSerializerImpl::Serialize(const UnBoxToRef& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateUnBoxToRef(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::GetRTTI> CHIRSerializer::CHIRSerializerImpl::Serialize(const GetRTTI& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateGetRTTI(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::GetRTTIStatic> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const GetRTTIStatic& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto rtti = GetId<Type>(obj.GetRTTIType());
    return PackageFormat::CreateGetRTTIStatic(builder, base, rtti);
}

template <>
flatbuffers::Offset<PackageFormat::IntOpWithException> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const IntOpWithException& obj)
{
    auto base = Serialize<PackageFormat::Terminator>(static_cast<const Terminator&>(obj));
    auto opKind = PackageFormat::CHIRExprKind(obj.GetOpKind());
    auto overflowStrategy = PackageFormat::OverflowStrategy(obj.GetOverflowStrategy());
    return PackageFormat::CreateIntOpWithException(builder, base, opKind, overflowStrategy);
}

template <>
flatbuffers::Offset<PackageFormat::TypeCastWithException> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const TypeCastWithException& obj)
{
    auto base = Serialize<PackageFormat::Terminator>(static_cast<const Terminator&>(obj));
    return PackageFormat::CreateTypeCastWithException(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::IntrinsicWithException> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const IntrinsicWithException& obj)
{
    auto base = Serialize<PackageFormat::Terminator>(static_cast<const Terminator&>(obj));
    auto intrinsicKind = PackageFormat::IntrinsicKind(obj.GetIntrinsicKind());
    auto instantiatedTypeArgs = GetId<Type>(obj.GetInstantiatedTypeArgs());
    return PackageFormat::CreateIntrinsicWithExceptionDirect(
        builder, base, intrinsicKind, instantiatedTypeArgs.empty() ? nullptr : &instantiatedTypeArgs);
}

template <>
flatbuffers::Offset<PackageFormat::AllocateWithException> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const AllocateWithException& obj)
{
    auto base = Serialize<PackageFormat::Terminator>(static_cast<const Terminator&>(obj));
    auto targetType = GetId<Type>(obj.GetType());
    return PackageFormat::CreateAllocateWithException(builder, base, targetType);
}

template <>
flatbuffers::Offset<PackageFormat::RawArrayAllocateWithException> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const RawArrayAllocateWithException& obj)
{
    auto base = Serialize<PackageFormat::Terminator>(static_cast<const Terminator&>(obj));
    auto elementType = GetId<Type>(obj.GetElementType());
    return PackageFormat::CreateRawArrayAllocateWithException(builder, base, elementType);
}

template <>
flatbuffers::Offset<PackageFormat::SpawnWithException> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const SpawnWithException& obj)
{
    auto base = Serialize<PackageFormat::Terminator>(static_cast<const Terminator&>(obj));
    auto executeClosure = GetId<Value>(obj.GetExecuteClosure());
    return PackageFormat::CreateSpawnWithException(builder, base, executeClosure);
}

template <> flatbuffers::Offset<PackageFormat::Tuple> CHIRSerializer::CHIRSerializerImpl::Serialize(const Tuple& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateTuple(builder, base);
}

template <> flatbuffers::Offset<PackageFormat::Field> CHIRSerializer::CHIRSerializerImpl::Serialize(const Field& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto path = obj.GetPath();
    return PackageFormat::CreateFieldDirect(builder, base, path.empty() ? nullptr : &path);
}

template <> flatbuffers::Offset<PackageFormat::FieldByName> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const FieldByName& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto names = builder.CreateVectorOfStrings(obj.GetNames());
    return PackageFormat::CreateFieldByName(builder, base, names);
}

template <>
flatbuffers::Offset<PackageFormat::RawArrayAllocate> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const RawArrayAllocate& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto elementType = GetId<Type>(obj.GetElementType());
    return PackageFormat::CreateRawArrayAllocate(builder, base, elementType);
}

template <>
flatbuffers::Offset<PackageFormat::RawArrayLiteralInit> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const RawArrayLiteralInit& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateRawArrayLiteralInit(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::RawArrayInitByValue> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const RawArrayInitByValue& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateRawArrayInitByValue(builder, base);
}

template <> flatbuffers::Offset<PackageFormat::VArray> CHIRSerializer::CHIRSerializerImpl::Serialize(const VArray& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateVArray(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::VArrayBd> CHIRSerializer::CHIRSerializerImpl::Serialize(const VArrayBuilder& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateVArrayBd(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::GetException> CHIRSerializer::CHIRSerializerImpl::Serialize(const GetException& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateGetException(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::Intrinsic> CHIRSerializer::CHIRSerializerImpl::Serialize(const Intrinsic& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto intrinsicKind = PackageFormat::IntrinsicKind(obj.GetIntrinsicKind());
    auto instantiatedTypeArgs = GetId<Type>(obj.GetInstantiatedTypeArgs());
    return PackageFormat::CreateIntrinsicDirect(
        builder, base, intrinsicKind, instantiatedTypeArgs.empty() ? nullptr : &instantiatedTypeArgs);
}

template <> flatbuffers::Offset<PackageFormat::If> CHIRSerializer::CHIRSerializerImpl::Serialize(const If& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateIf(builder, base);
}

template <> flatbuffers::Offset<PackageFormat::Loop> CHIRSerializer::CHIRSerializerImpl::Serialize(const Loop& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateLoop(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::ForInRange> CHIRSerializer::CHIRSerializerImpl::Serialize(const ForInRange& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateForInRange(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::ForInClosedRange> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const ForInClosedRange& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateForInClosedRange(builder, base);
}

template <>
flatbuffers::Offset<PackageFormat::ForInIter> CHIRSerializer::CHIRSerializerImpl::Serialize(const ForInIter& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    return PackageFormat::CreateForInIter(builder, base);
}

template <> flatbuffers::Offset<PackageFormat::Debug> CHIRSerializer::CHIRSerializerImpl::Serialize(const Debug& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto srcCodeIdentifier = obj.GetSrcCodeIdentifier();
    CJC_ASSERT(!obj.GetOperands().empty());
    return PackageFormat::CreateDebugDirect(builder, base, srcCodeIdentifier.data());
}

template <> flatbuffers::Offset<PackageFormat::Spawn> CHIRSerializer::CHIRSerializerImpl::Serialize(const Spawn& obj)
{
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto executeClosure = GetId<Value>(obj.GetExecuteClosure());
    return PackageFormat::CreateSpawn(builder, base, executeClosure);
}

template <> flatbuffers::Offset<PackageFormat::Lambda> CHIRSerializer::CHIRSerializerImpl::Serialize(const Lambda& obj)
{
    CJC_ASSERT(obj.GetBlockGroups().size() == 1);
    CJC_ASSERT(obj.GetBody());
    auto base = Serialize<PackageFormat::Expression>(static_cast<const Expression&>(obj));
    auto funcTy = GetId<Type>(obj.GetFuncType()); // use typeID
    auto isLocalFunc = obj.IsLocalFunc();
    auto identifier = obj.GetIdentifier();
    auto srcCodeIdentifier = obj.GetSrcCodeIdentifier();
    auto params = GetId<Value>(obj.GetParams());
    auto genericTypeParams = GetId<Type>(obj.GetGenericTypeParams());
    auto body = GetId<Value>(obj.GetBody());
    auto retVal = GetId<Value>(obj.GetReturnValue());
    auto isConst = obj.IsCompileTimeValue();
    return PackageFormat::CreateLambdaDirect(builder, base, funcTy, isLocalFunc, identifier.data(),
        srcCodeIdentifier.data(), params.empty() ? nullptr : &params,
        genericTypeParams.empty() ? nullptr : &genericTypeParams, body, retVal, isConst);
}

// ======================= Custom Type Def Serializers =========================
template <>
flatbuffers::Offset<PackageFormat::VirtualMethodInfo> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const VirtualMethodInfo& obj)
{
    // condition
    std::string funcName = obj.GetMethodName();
    auto sigType = GetId<Type>(obj.GetMethodSigType());
    auto genericTypeParams = GetId<Type>(obj.GetGenericTypeParams());
    // result
    uint32_t funcPtr = GetId<Value>(obj.GetVirtualMethod());
    auto attributes = obj.GetAttributeInfo().GetRawAttrs().to_ulong();
    auto originalType = GetId<Type>(obj.GetOriginalFuncType());
    auto parentType = GetId<Type>(obj.GetInstParentType());
    auto returnType = GetId<Type>(obj.GetMethodInstRetType());

    return PackageFormat::CreateVirtualMethodInfoDirect(builder, funcName.data(), sigType,
        genericTypeParams.empty() ? nullptr : &genericTypeParams, funcPtr, attributes, originalType, parentType,
        returnType);
}

std::vector<flatbuffers::Offset<PackageFormat::VTableInType>> CHIRSerializer::CHIRSerializerImpl::SerializeVTable(
    const VTableInDef& obj)
{
    std::vector<flatbuffers::Offset<PackageFormat::VTableInType>> retval;
    for (const auto& elem : obj.GetTypeVTables()) {
        auto ty = GetId<Type>(elem.GetSrcParentType());
        auto info = SerializeVec<PackageFormat::VirtualMethodInfo>(elem.GetVirtualMethods());
        retval.push_back(PackageFormat::CreateVTableInTypeDirect(builder, ty, &info));
    }
    return retval;
}

template <>
flatbuffers::Offset<PackageFormat::CustomTypeDef> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const CustomTypeDef& obj)
{
    auto base = Serialize<PackageFormat::Base>(static_cast<const Base&>(obj));
    auto kind = PackageFormat::CustomDefKind(obj.GetCustomKind());
    auto customTypeDefID = GetId<CustomTypeDef>(&obj);
    auto srcCodeIdentifier = obj.GetSrcCodeIdentifier();
    auto identifier = obj.GetIdentifier();
    auto packageName = obj.GetPackageName();
    auto type = GetId<Type>(obj.CustomTypeDef::GetType());
    auto genericDecl = GetId<CustomTypeDef>(obj.GetGenericDecl());
    auto methods = GetId<Value>(obj.GetMethods());
    auto implementedInterfaces = GetId<Type>(obj.GetImplementedInterfaceTys());
    auto instanceMemberVars = obj.GetCustomKind() == CustomDefKind::TYPE_CLASS
        ? SerializeVec<PackageFormat::MemberVarInfo>(StaticCast<const ClassDef&>(obj).GetDirectInstanceVars())
        : SerializeVec<PackageFormat::MemberVarInfo>(obj.GetAllInstanceVars());
    auto staticMemberVars = GetId<Value>(obj.GetStaticMemberVars());
    auto attributes = obj.GetAttributeInfo().GetRawAttrs().to_ulong();
    auto annoInfo = Serialize<PackageFormat::AnnoInfo>(obj.GetAnnoInfo());
    auto vtable = SerializeVTable(obj.GetDefVTable());
    auto varInitializationFunc = GetId<Value>(obj.GetVarInitializationFunc());
    return PackageFormat::CreateCustomTypeDefDirect(builder, base, kind, customTypeDefID, srcCodeIdentifier.data(),
        identifier.data(), packageName.data(), type, genericDecl, methods.empty() ? nullptr : &methods,
        implementedInterfaces.empty() ? nullptr : &implementedInterfaces, &instanceMemberVars,
        staticMemberVars.empty() ? nullptr : &staticMemberVars, attributes, annoInfo, &vtable, nullptr,
        varInitializationFunc);
}

template <>
flatbuffers::Offset<PackageFormat::EnumDef> CHIRSerializer::CHIRSerializerImpl::Serialize(const EnumDef& obj)
{
    auto base = Serialize<PackageFormat::CustomTypeDef>(static_cast<const CustomTypeDef&>(obj));
    auto ctors = SerializeVec<PackageFormat::EnumCtorInfo>(obj.GetCtors());
    auto nonExhaustive = !obj.IsExhaustive();
    return PackageFormat::CreateEnumDefDirect(builder, base, &ctors, nonExhaustive);
}

template <>
flatbuffers::Offset<PackageFormat::StructDef> CHIRSerializer::CHIRSerializerImpl::Serialize(const StructDef& obj)
{
    auto base = Serialize<PackageFormat::CustomTypeDef>(static_cast<const CustomTypeDef&>(obj));
    auto isCStruct = obj.IsCStruct();
    return PackageFormat::CreateStructDef(builder, base, isCStruct);
}

template <>
flatbuffers::Offset<PackageFormat::ClassDef> CHIRSerializer::CHIRSerializerImpl::Serialize(const ClassDef& obj)
{
    auto base = Serialize<PackageFormat::CustomTypeDef>(static_cast<const CustomTypeDef&>(obj));
    auto kind = obj.IsInterface() ? PackageFormat::ClassDefKind::ClassDefKind_INTERFACE
                                  : PackageFormat::ClassDefKind::ClassDefKind_CLASS;
    auto isAnnotation = obj.IsAnnotation();
    auto superClass = GetId<Type>(obj.GetSuperClassTy());
    auto abstractMethods = SerializeVec<PackageFormat::AbstractMethodInfo>(obj.GetAbstractMethods());
    std::vector<flatbuffers::Offset<flatbuffers::String>> namesVec;
    for (const auto& name : obj.GetAllMethodMangledNames()) {
        namesVec.push_back(builder.CreateSharedString(name));
    }
    return PackageFormat::CreateClassDefDirect(builder, base, kind, isAnnotation,
        superClass, &abstractMethods, &namesVec);
}

template <>
flatbuffers::Offset<PackageFormat::ExtendDef> CHIRSerializer::CHIRSerializerImpl::Serialize(const ExtendDef& obj)
{
    auto base = Serialize<PackageFormat::CustomTypeDef>(static_cast<const CustomTypeDef&>(obj));
    auto extendedType = GetId<Type>(obj.GetExtendedType());
    auto genericParams = GetId<Type>(obj.GetGenericTypeParams());
    return PackageFormat::CreateExtendDefDirect(
        builder, base, extendedType, genericParams.empty() ? nullptr : &genericParams);
}

// ========================== Dispatchers ===============================

template <> flatbuffers::Offset<void> CHIRSerializer::CHIRSerializerImpl::Dispatch(const Type& obj)
{
    switch (obj.GetTypeKind()) {
        case Type::TypeKind::TYPE_INT8:
        case Type::TypeKind::TYPE_INT16:
        case Type::TypeKind::TYPE_INT32:
        case Type::TypeKind::TYPE_INT64:
        case Type::TypeKind::TYPE_INT_NATIVE:
        case Type::TypeKind::TYPE_UINT8:
        case Type::TypeKind::TYPE_UINT16:
        case Type::TypeKind::TYPE_UINT32:
        case Type::TypeKind::TYPE_UINT64:
        case Type::TypeKind::TYPE_UINT_NATIVE:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_IntType);
            return Serialize<PackageFormat::IntType>(static_cast<const IntType&>(obj)).Union();
        case Type::TypeKind::TYPE_FLOAT16:
        case Type::TypeKind::TYPE_FLOAT32:
        case Type::TypeKind::TYPE_FLOAT64:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_FloatType);
            return Serialize<PackageFormat::FloatType>(static_cast<const FloatType&>(obj)).Union();
        case Type::TypeKind::TYPE_RUNE:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_RuneType);
            return Serialize<PackageFormat::RuneType>(static_cast<const RuneType&>(obj)).Union();
        case Type::TypeKind::TYPE_BOOLEAN:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_BooleanType);
            return Serialize<PackageFormat::BooleanType>(static_cast<const BooleanType&>(obj)).Union();
        case Type::TypeKind::TYPE_UNIT:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_UnitType);
            return Serialize<PackageFormat::UnitType>(static_cast<const UnitType&>(obj)).Union();
        case Type::TypeKind::TYPE_NOTHING:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_NothingType);
            return Serialize<PackageFormat::NothingType>(static_cast<const NothingType&>(obj)).Union();
        case Type::TypeKind::TYPE_VOID:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_VoidType);
            return Serialize<PackageFormat::VoidType>(static_cast<const VoidType&>(obj)).Union();
        case Type::TypeKind::TYPE_TUPLE:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_TupleType);
            return Serialize<PackageFormat::TupleType>(static_cast<const TupleType&>(obj)).Union();
        case Type::TypeKind::TYPE_STRUCT:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_StructType);
            return Serialize<PackageFormat::StructType>(static_cast<const StructType&>(obj)).Union();
        case Type::TypeKind::TYPE_ENUM:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_EnumType);
            return Serialize<PackageFormat::EnumType>(static_cast<const EnumType&>(obj)).Union();
        case Type::TypeKind::TYPE_FUNC:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_FuncType);
            return Serialize<PackageFormat::FuncType>(static_cast<const FuncType&>(obj)).Union();
        case Type::TypeKind::TYPE_CLASS:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_ClassType);
            return Serialize<PackageFormat::ClassType>(static_cast<const ClassType&>(obj)).Union();
        case Type::TypeKind::TYPE_RAWARRAY:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_RawArrayType);
            return Serialize<PackageFormat::RawArrayType>(static_cast<const RawArrayType&>(obj)).Union();
        case Type::TypeKind::TYPE_VARRAY:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_VArrayType);
            return Serialize<PackageFormat::VArrayType>(static_cast<const VArrayType&>(obj)).Union();
        case Type::TypeKind::TYPE_CPOINTER:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_CPointerType);
            return Serialize<PackageFormat::CPointerType>(static_cast<const CPointerType&>(obj)).Union();
        case Type::TypeKind::TYPE_CSTRING:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_CStringType);
            return Serialize<PackageFormat::CStringType>(static_cast<const CStringType&>(obj)).Union();
        case Type::TypeKind::TYPE_GENERIC:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_GenericType);
            return Serialize<PackageFormat::GenericType>(static_cast<const GenericType&>(obj)).Union();
        case Type::TypeKind::TYPE_REFTYPE:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_RefType);
            return Serialize<PackageFormat::RefType>(static_cast<const RefType&>(obj)).Union();
        case Type::TypeKind::TYPE_BOXTYPE:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_BoxType);
            return Serialize<PackageFormat::BoxType>(static_cast<const BoxType&>(obj)).Union();
        case Type::TypeKind::TYPE_THIS:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(PackageFormat::TypeElem_ThisType);
            return Serialize<PackageFormat::ThisType>(static_cast<const ThisType&>(obj)).Union();
        case Type::TypeKind::TYPE_INVALID:
        case Type::TypeKind::MAX_TYPE_KIND:
            CJC_ABORT();
            return 0;
    }
}

template <> flatbuffers::Offset<void> CHIRSerializer::CHIRSerializerImpl::Dispatch(const LiteralValue& obj)
{
    switch (obj.GetConstantValueKind()) {
        case ConstantValueKind::KIND_BOOL:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_BoolLiteral;
            return Serialize<PackageFormat::BoolLiteral>(static_cast<const BoolLiteral&>(obj)).Union();
        case ConstantValueKind::KIND_RUNE:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_RuneLiteral;
            return Serialize<PackageFormat::RuneLiteral>(static_cast<const RuneLiteral&>(obj)).Union();
        case ConstantValueKind::KIND_INT:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_IntLiteral;
            return Serialize<PackageFormat::IntLiteral>(static_cast<const IntLiteral&>(obj)).Union();
        case ConstantValueKind::KIND_FLOAT:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_FloatLiteral;
            return Serialize<PackageFormat::FloatLiteral>(static_cast<const FloatLiteral&>(obj)).Union();
        case ConstantValueKind::KIND_STRING:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_StringLiteral;
            return Serialize<PackageFormat::StringLiteral>(static_cast<const StringLiteral&>(obj)).Union();
        case ConstantValueKind::KIND_UNIT:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_UnitLiteral;
            return Serialize<PackageFormat::UnitLiteral>(static_cast<const UnitLiteral&>(obj)).Union();
        case ConstantValueKind::KIND_NULL:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_NullLiteral;
            return Serialize<PackageFormat::NullLiteral>(static_cast<const NullLiteral&>(obj)).Union();
        case ConstantValueKind::KIND_FUNC:
            return 0;
    }
}

template <> flatbuffers::Offset<void> CHIRSerializer::CHIRSerializerImpl::Dispatch(const Value& obj)
{
    switch (obj.GetValueKind()) {
        case Value::ValueKind::KIND_LITERAL:
            return Dispatch<LiteralValue>(static_cast<const LiteralValue&>(obj)).Union();
        case Value::ValueKind::KIND_GLOBALVAR:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_GlobalVar;
            return Serialize<PackageFormat::GlobalVar>(dynamic_cast<const GlobalVar&>(obj)).Union();
        case Value::ValueKind::KIND_PARAMETER:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_Parameter;
            return Serialize<PackageFormat::Parameter>(static_cast<const Parameter&>(obj)).Union();
        case Value::ValueKind::KIND_IMP_FUNC:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_ImportedFunc;
            return Serialize<PackageFormat::ImportedFunc>(dynamic_cast<const ImportedFunc&>(obj)).Union();
        case Value::ValueKind::KIND_IMP_VAR:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_ImportedVar;
            return Serialize<PackageFormat::ImportedVar>(dynamic_cast<const ImportedVar&>(obj)).Union();
        case Value::ValueKind::KIND_LOCALVAR:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_LocalVar;
            return Serialize<PackageFormat::LocalVar>(static_cast<const LocalVar&>(obj)).Union();
        case Value::ValueKind::KIND_FUNC:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_Func;
            return Serialize<PackageFormat::Func>(dynamic_cast<const Func&>(obj)).Union();
        case Value::ValueKind::KIND_BLOCK:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_Block;
            return Serialize<PackageFormat::Block>(static_cast<const Block&>(obj)).Union();
        case Value::ValueKind::KIND_BLOCK_GROUP:
            valueKind[GetId<Value>(&obj) - 1] = PackageFormat::ValueElem_BlockGroup;
            return Serialize<PackageFormat::BlockGroup>(static_cast<const BlockGroup&>(obj)).Union();
    }
}

template <> flatbuffers::Offset<void> CHIRSerializer::CHIRSerializerImpl::Dispatch(const Expression& obj)
{
    switch (obj.GetExprKind()) {
        case ExprKind::GOTO:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_GoTo;
            return Serialize<PackageFormat::GoTo>(static_cast<const GoTo&>(obj)).Union();
        case ExprKind::BRANCH:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Branch;
            return Serialize<PackageFormat::Branch>(static_cast<const Branch&>(obj)).Union();
        case ExprKind::MULTIBRANCH:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_MultiBranch;
            return Serialize<PackageFormat::MultiBranch>(static_cast<const MultiBranch&>(obj)).Union();
        case ExprKind::EXIT:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Exit;
            return Serialize<PackageFormat::Exit>(static_cast<const Exit&>(obj)).Union();
        case ExprKind::APPLY_WITH_EXCEPTION:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_ApplyWithException;
            return Serialize<PackageFormat::ApplyWithException>(static_cast<const ApplyWithException&>(obj)).Union();
        case ExprKind::INVOKE_WITH_EXCEPTION:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_InvokeWithException;
            return Serialize<PackageFormat::InvokeWithException>(static_cast<const InvokeWithException&>(obj)).Union();
        case ExprKind::RAISE_EXCEPTION:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_RaiseException;
            return Serialize<PackageFormat::RaiseException>(static_cast<const RaiseException&>(obj)).Union();
        case ExprKind::INT_OP_WITH_EXCEPTION:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_IntOpWithException;
            return Serialize<PackageFormat::IntOpWithException>(static_cast<const IntOpWithException&>(obj)).Union();
        case ExprKind::SPAWN_WITH_EXCEPTION:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_SpawnWithException;
            return Serialize<PackageFormat::SpawnWithException>(static_cast<const SpawnWithException&>(obj)).Union();
        case ExprKind::TYPECAST_WITH_EXCEPTION:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_TypeCastWithException;
            return Serialize<PackageFormat::TypeCastWithException>(static_cast<const TypeCastWithException&>(obj))
                .Union();
        case ExprKind::INTRINSIC_WITH_EXCEPTION:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_IntrinsicWithException;
            return Serialize<PackageFormat::IntrinsicWithException>(static_cast<const IntrinsicWithException&>(obj))
                .Union();
        case ExprKind::ALLOCATE_WITH_EXCEPTION:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_AllocateWithException;
            return Serialize<PackageFormat::AllocateWithException>(static_cast<const AllocateWithException&>(obj))
                .Union();
        case ExprKind::INVOKESTATIC_WITH_EXCEPTION:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_InvokeStaticWithException;
            return Serialize<PackageFormat::InvokeStaticWithException>(
                static_cast<const InvokeStaticWithException&>(obj))
                .Union();
        case ExprKind::RAW_ARRAY_ALLOCATE_WITH_EXCEPTION:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_RawArrayAllocateWithException;
            return Serialize<PackageFormat::RawArrayAllocateWithException>(
                static_cast<const RawArrayAllocateWithException&>(obj))
                .Union();
        case ExprKind::NEG:
        case ExprKind::NOT:
        case ExprKind::BITNOT:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_UnaryExpression;
            return Serialize<PackageFormat::UnaryExpression>(static_cast<const UnaryExpression&>(obj)).Union();
        case ExprKind::ADD:
        case ExprKind::SUB:
        case ExprKind::MUL:
        case ExprKind::DIV:
        case ExprKind::MOD:
        case ExprKind::EXP:
        case ExprKind::LSHIFT:
        case ExprKind::RSHIFT:
        case ExprKind::BITAND:
        case ExprKind::BITOR:
        case ExprKind::BITXOR:
        case ExprKind::LT:
        case ExprKind::GT:
        case ExprKind::LE:
        case ExprKind::GE:
        case ExprKind::EQUAL:
        case ExprKind::NOTEQUAL:
        case ExprKind::AND:
        case ExprKind::OR:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_BinaryExpression;
            return Serialize<PackageFormat::BinaryExpression>(static_cast<const BinaryExpression&>(obj)).Union();
        case ExprKind::ALLOCATE:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Allocate;
            return Serialize<PackageFormat::Allocate>(static_cast<const Allocate&>(obj)).Union();
        case ExprKind::LOAD:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Load;
            return Serialize<PackageFormat::Load>(static_cast<const Load&>(obj)).Union();
        case ExprKind::STORE:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Store;
            return Serialize<PackageFormat::Store>(static_cast<const Store&>(obj)).Union();
        case ExprKind::GET_ELEMENT_REF:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_GetElementRef;
            return Serialize<PackageFormat::GetElementRef>(static_cast<const GetElementRef&>(obj)).Union();
        case ExprKind::GET_ELEMENT_BY_NAME:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_GetElementByName;
            return Serialize<PackageFormat::GetElementByName>(static_cast<const GetElementByName&>(obj)).Union();
        case ExprKind::STORE_ELEMENT_REF:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_StoreElementRef;
            return Serialize<PackageFormat::StoreElementRef>(static_cast<const StoreElementRef&>(obj)).Union();
        case ExprKind::STORE_ELEMENT_BY_NAME:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_StoreElementByName;
            return Serialize<PackageFormat::StoreElementByName>(static_cast<const StoreElementByName&>(obj)).Union();
        case ExprKind::IF:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_If;
            return Serialize<PackageFormat::If>(static_cast<const If&>(obj)).Union();
        case ExprKind::LOOP:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Loop;
            return Serialize<PackageFormat::Loop>(static_cast<const Loop&>(obj)).Union();
        case ExprKind::FORIN_RANGE:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_ForInRange;
            return Serialize<PackageFormat::ForInRange>(static_cast<const ForInRange&>(obj)).Union();
        case ExprKind::FORIN_ITER:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_ForInIter;
            return Serialize<PackageFormat::ForInIter>(static_cast<const ForInIter&>(obj)).Union();
        case ExprKind::FORIN_CLOSED_RANGE:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_ForInClosedRange;
            return Serialize<PackageFormat::ForInClosedRange>(static_cast<const ForInClosedRange&>(obj)).Union();
        case ExprKind::LAMBDA:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Lambda;
            return Serialize<PackageFormat::Lambda>(static_cast<const Lambda&>(obj)).Union();
        case ExprKind::CONSTANT:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Constant;
            return Serialize<PackageFormat::Constant>(static_cast<const Constant&>(obj)).Union();
        case ExprKind::DEBUGEXPR:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Debug;
            return Serialize<PackageFormat::Debug>(static_cast<const Debug&>(obj)).Union();
        case ExprKind::TUPLE:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Tuple;
            return Serialize<PackageFormat::Tuple>(static_cast<const Tuple&>(obj)).Union();
        case ExprKind::FIELD:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Field;
            return Serialize<PackageFormat::Field>(static_cast<const Field&>(obj)).Union();
        case ExprKind::FIELD_BY_NAME:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_FieldByName;
            return Serialize<PackageFormat::FieldByName>(static_cast<const FieldByName&>(obj)).Union();
        case ExprKind::APPLY:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Apply;
            return Serialize<PackageFormat::Apply>(static_cast<const Apply&>(obj)).Union();
        case ExprKind::INVOKE:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Invoke;
            return Serialize<PackageFormat::Invoke>(static_cast<const Invoke&>(obj)).Union();
        case ExprKind::INVOKESTATIC:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_InvokeStatic;
            return Serialize<PackageFormat::InvokeStatic>(static_cast<const InvokeStatic&>(obj)).Union();
        case ExprKind::INSTANCEOF:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_InstanceOf;
            return Serialize<PackageFormat::InstanceOf>(static_cast<const InstanceOf&>(obj)).Union();
        case ExprKind::BOX:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Box;
            return Serialize<PackageFormat::Box>(static_cast<const Box&>(obj)).Union();
        case ExprKind::UNBOX:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_UnBox;
            return Serialize<PackageFormat::UnBox>(static_cast<const UnBox&>(obj)).Union();
        case ExprKind::TYPECAST:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_TypeCast;
            return Serialize<PackageFormat::TypeCast>(static_cast<const TypeCast&>(obj)).Union();
        case ExprKind::GET_EXCEPTION:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_GetException;
            return Serialize<PackageFormat::GetException>(static_cast<const GetException&>(obj)).Union();
        case ExprKind::RAW_ARRAY_ALLOCATE:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_RawArrayAllocate;
            return Serialize<PackageFormat::RawArrayAllocate>(static_cast<const RawArrayAllocate&>(obj)).Union();
        case ExprKind::RAW_ARRAY_LITERAL_INIT:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_RawArrayLiteralInit;
            return Serialize<PackageFormat::RawArrayLiteralInit>(static_cast<const RawArrayLiteralInit&>(obj)).Union();
        case ExprKind::RAW_ARRAY_INIT_BY_VALUE:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_RawArrayInitByValue;
            return Serialize<PackageFormat::RawArrayInitByValue>(static_cast<const RawArrayInitByValue&>(obj)).Union();
        case ExprKind::VARRAY:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_VArray;
            return Serialize<PackageFormat::VArray>(static_cast<const VArray&>(obj)).Union();
        case ExprKind::VARRAY_BUILDER:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_VArrayBd;
            return Serialize<PackageFormat::VArrayBd>(static_cast<const VArrayBuilder&>(obj)).Union();
        case ExprKind::INTRINSIC:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Intrinsic;
            return Serialize<PackageFormat::Intrinsic>(static_cast<const Intrinsic&>(obj)).Union();
        case ExprKind::SPAWN:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_Spawn;
            return Serialize<PackageFormat::Spawn>(static_cast<const Spawn&>(obj)).Union();
        case ExprKind::GET_INSTANTIATE_VALUE:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_GetInstantiateValue;
            return Serialize<PackageFormat::GetInstantiateValue>(static_cast<const GetInstantiateValue&>(obj)).Union();
        case ExprKind::TRANSFORM_TO_GENERIC:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_TransformToGeneric;
            return Serialize<PackageFormat::TransformToGeneric>(static_cast<const TransformToGeneric&>(obj)).Union();
        case ExprKind::TRANSFORM_TO_CONCRETE:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_TransformToConcrete;
            return Serialize<PackageFormat::TransformToConcrete>(static_cast<const TransformToConcrete&>(obj)).Union();
        case ExprKind::UNBOX_TO_REF:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_UnBoxToRef;
            return Serialize<PackageFormat::UnBoxToRef>(static_cast<const UnBoxToRef&>(obj)).Union();
        case ExprKind::GET_RTTI:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_GetRTTI;
            return Serialize<PackageFormat::GetRTTI>(static_cast<const GetRTTI&>(obj)).Union();
        case ExprKind::GET_RTTI_STATIC:
            exprKind[GetId<Expression>(&obj) - 1] = PackageFormat::ExpressionElem_GetRTTIStatic;
            return Serialize<PackageFormat::GetRTTIStatic>(static_cast<const GetRTTIStatic&>(obj)).Union();
        case ExprKind::INVALID:
        case ExprKind::MAX_EXPR_KINDS:
            CJC_ABORT();
            return 0;
    }
}

template <> flatbuffers::Offset<void> CHIRSerializer::CHIRSerializerImpl::Dispatch(const CustomTypeDef& obj)
{
    switch (obj.GetCustomKind()) {
        case CustomDefKind::TYPE_STRUCT:
            defKind[GetId<CustomTypeDef>(&obj) - 1] = PackageFormat::CustomTypeDefElem_StructDef;
            return Serialize<PackageFormat::StructDef>(static_cast<const StructDef&>(obj)).Union();
        case CustomDefKind::TYPE_ENUM:
            defKind[GetId<CustomTypeDef>(&obj) - 1] = PackageFormat::CustomTypeDefElem_EnumDef;
            return Serialize<PackageFormat::EnumDef>(static_cast<const EnumDef&>(obj)).Union();
        case CustomDefKind::TYPE_CLASS:
            defKind[GetId<CustomTypeDef>(&obj) - 1] = PackageFormat::CustomTypeDefElem_ClassDef;
            return Serialize<PackageFormat::ClassDef>(static_cast<const ClassDef&>(obj)).Union();
        case CustomDefKind::TYPE_EXTEND:
            defKind[GetId<CustomTypeDef>(&obj) - 1] = PackageFormat::CustomTypeDefElem_ExtendDef;
            return Serialize<PackageFormat::ExtendDef>(static_cast<const ExtendDef&>(obj)).Union();
    }
}

void CHIRSerializer::CHIRSerializerImpl::Dispatch()
{
    while (!(typeQueue.empty() && valueQueue.empty() && exprQueue.empty() && defQueue.empty())) {
        while (!typeQueue.empty()) {
            auto type = typeQueue.front();
            allType[GetId<Type>(type) - 1] = Dispatch<Type>(*type);
            typeQueue.pop();
        }
        while (!valueQueue.empty()) {
            auto value = valueQueue.front();
            allValue[GetId<Value>(value) - 1] = Dispatch<Value>(*value);
            valueQueue.pop_front();
        }
        while (!exprQueue.empty()) {
            auto expr = exprQueue.front();
            allExpression[GetId<Expression>(expr) - 1] = Dispatch<Expression>(*expr);
            exprQueue.pop();
        }
        while (!defQueue.empty()) {
            auto def = defQueue.front();
            allCustomTypeDef[GetId<CustomTypeDef>(def) - 1] = Dispatch<CustomTypeDef>(*def);
            defQueue.pop_front();
        }
    }
    packageInitFunc = GetId<Value>(package.GetPackageInitFunc());
    packageLiteralInitFunc = GetId<Value>(package.GetPackageLiteralInitFunc());
}

// ========================== Utilities ==========================================

void CHIRSerializer::CHIRSerializerImpl::Save(const std::string& filename, ToCHIR::Phase phase)
{
    auto accesslevel = package.GetPackageAccessLevel();
    auto packageName = package.GetName();
    auto serializedPackage = PackageFormat::CreateCHIRPackageDirect(builder, packageName.c_str(), "",
        PackageFormat::PackageAccessLevel(accesslevel), &typeKind, &allType, &valueKind, &allValue, &exprKind,
        &allExpression, &defKind, &allCustomTypeDef, packageInitFunc, PackageFormat::Phase(phase),
        packageLiteralInitFunc, maxImportedValueId, maxImportedStructId, maxImportedClassId, maxImportedEnumId,
        maxImportedExtendId);

    builder.Finish(serializedPackage);
    const uint8_t* buf = builder.GetBufferPointer();
    auto size = builder.GetSize();
    std::ofstream output(filename, std::ios::out | std::ofstream::binary);
    CJC_ASSERT(output.is_open());
    output.write(reinterpret_cast<const char*>(buf), static_cast<long>(size));
    output.close();
}

void CHIRSerializer::CHIRSerializerImpl::Initialize()
{
    // imports
    for (auto value : package.GetImportedVarAndFuncs()) {
        valueQueue.push_back(value);
    }
    maxImportedValueId = static_cast<uint32_t>(valueQueue.size());
    for (auto def : package.GetImportedStructs()) {
        defQueue.push_back(def);
    }
    maxImportedStructId = static_cast<uint32_t>(defQueue.size());
    for (auto def : package.GetImportedClasses()) {
        defQueue.push_back(def);
    }
    maxImportedClassId = static_cast<uint32_t>(defQueue.size());
    for (auto def : package.GetImportedEnums()) {
        defQueue.push_back(def);
    }
    maxImportedEnumId = static_cast<uint32_t>(defQueue.size());
    for (auto def : package.GetImportedExtends()) {
        defQueue.push_back(def);
    }
    maxImportedExtendId = static_cast<uint32_t>(defQueue.size());
    // current package
    for (auto value : package.GetGlobalVars()) {
        valueQueue.push_back(value);
    }

    for (auto def : package.GetStructs()) {
        defQueue.push_back(def);
    }
    for (auto def : package.GetClasses()) {
        defQueue.push_back(def);
    }
    for (auto def : package.GetEnums()) {
        defQueue.push_back(def);
    }
    for (auto def : package.GetExtends()) {
        defQueue.push_back(def);
    }

    for (auto value : package.GetGlobalFuncs()) {
        valueQueue.push_back(value);
    }

    // allocate def id earlier
    for (auto def : std::as_const(defQueue)) {
        def2Id[def] = ++defCount;
        allCustomTypeDef.emplace_back(0);
        defKind.emplace_back(0);
    }

    // allocate value id earlier
    for (auto obj : std::as_const(valueQueue)) {
        value2Id[obj] = ++valueCount;
        allValue.emplace_back(0);
        valueKind.emplace_back(0);
    }
}
