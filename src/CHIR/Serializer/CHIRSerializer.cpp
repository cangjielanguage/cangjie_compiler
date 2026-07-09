// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Serializer/CHIRSerializer.h"
#include "CHIRSerializerImpl.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
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
#include "flatbuffers/StdxChirFormat_generated.h"
#include "flatbuffers/detached_buffer.h"

#include <algorithm>
#include <set>
#include <unordered_set>
#include <vector>

using namespace Cangjie::CHIR;

namespace {
CHIRFormat::OverflowStrategy SerializeOverflowStrategy(Cangjie::OverflowStrategy strategy)
{
    using Cangjie::OverflowStrategy;
    switch (strategy) {
        case OverflowStrategy::NA:
            return CHIRFormat::OverflowStrategy_NA;
        case OverflowStrategy::WRAPPING:
            return CHIRFormat::OverflowStrategy_WRAPPING;
        case OverflowStrategy::THROWING:
            return CHIRFormat::OverflowStrategy_THROWING;
        case OverflowStrategy::SATURATING:
            return CHIRFormat::OverflowStrategy_SATURATING;
        default:
            CJC_ABORT();
            return CHIRFormat::OverflowStrategy_NA;
    }
}
} // namespace

void CHIRSerializer::Serialize(const Package& package, const std::string filename, ToCHIR::Phase phase)
{
    Utils::ProfileRecorder recorder("CHIR", "serialization: " + PhaseToString(phase));
    CHIRSerializerImpl serializer(package);
    serializer.Initialize();
    serializer.Dispatch();
    serializer.Save(filename, phase);
}

flatbuffers::DetachedBuffer CHIRSerializer::Serialize(const Package& package)
{
    CHIRSerializerImpl serializer(package);
    serializer.Initialize();
    serializer.Dispatch();
    return serializer.ConvertToMemoryData();
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
flatbuffers::Offset<CHIRFormat::DebugLocation> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const DebugLocation& obj)
{
    auto beginPos = CHIRFormat::CreatePos(builder, obj.GetBeginPos().line, obj.GetBeginPos().column);
    auto endPos = CHIRFormat::CreatePos(builder, obj.GetEndPos().line, obj.GetEndPos().column);
    auto scope = obj.GetScopeInfo();
    return CHIRFormat::CreateDebugLocationDirect(
        builder, obj.GetAbsPath().c_str(), obj.GetFileID(), beginPos, endPos, &scope);
}

template <>
flatbuffers::Offset<CHIRFormat::AnnoInfo> CHIRSerializer::CHIRSerializerImpl::Serialize(const AnnoInfo& obj)
{
    std::vector<flatbuffers::Offset<CHIRFormat::CustomAnnoInstance>> instances;
    for (const auto& inst : obj.GetCustomAnnoInstances()) {
        std::vector<flatbuffers::Offset<flatbuffers::String>> argStrs;
        for (const auto& arg : inst.GetArgValues()) {
            argStrs.push_back(builder.CreateSharedString(arg));
        }
        auto locOff = Serialize<CHIRFormat::DebugLocation>(inst.GetDebugLocation());
        instances.push_back(CHIRFormat::CreateCustomAnnoInstanceDirect(builder,
            inst.GetAnnoClassName().data(), argStrs.empty() ? nullptr : &argStrs, locOff));
    }
    return CHIRFormat::CreateAnnoInfoDirect(builder, obj.GetAnnoFactoryFuncMangledName().data(),
        instances.empty() ? nullptr : &instances);
}

[[maybe_unused]] static void Empty(Annotation*)
{
}

template <> flatbuffers::Offset<CHIRFormat::Base> CHIRSerializer::CHIRSerializerImpl::Serialize(const Base& obj)
{
    auto annoTypes = std::vector<uint8_t>();
    auto annos = std::vector<flatbuffers::Offset<void>>();
    std::unordered_map<std::type_index, std::function<void(Annotation*)>> annoHandler;

    // NeedCheckArrayBound
    annoHandler[typeid(CHIR::NeedCheckArrayBound)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(CHIRFormat::Annotation::Annotation_needCheckArrayBound);
        annos.emplace_back(CHIRFormat::CreateNeedCheckArrayBound(
            builder, NeedCheckArrayBound::Extract(StaticCast<NeedCheckArrayBound*>(anno)))
                .Union());
    };

    // NeedCheckCast
    annoHandler[typeid(CHIR::NeedCheckCast)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(CHIRFormat::Annotation::Annotation_needCheckCast);
        annos.emplace_back(
            CHIRFormat::CreateNeedCheckCast(builder, NeedCheckCast::Extract(StaticCast<NeedCheckCast*>(anno)))
                .Union());
    };

    // DebugLocationInfoForWarning
    annoHandler[typeid(CHIR::DebugLocationInfoForWarning)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(CHIRFormat::Annotation::Annotation_debugLocationInfoForWarning);
        annos.emplace_back(Serialize<CHIRFormat::DebugLocation>(
            DebugLocationInfoForWarning::Extract(StaticCast<DebugLocationInfoForWarning*>(anno)))
                .Union());
    };

    // LinkTypeInfo
    annoHandler[typeid(CHIR::LinkTypeInfo)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(CHIRFormat::Annotation::Annotation_linkTypeInfo);
        annos.emplace_back(CHIRFormat::CreateLinkTypeInfo(
            builder, CHIRFormat::Linkage(LinkTypeInfo::Extract(StaticCast<CHIR::LinkTypeInfo*>(anno))))
                .Union());
    };

    // SkipCheck
    annoHandler[typeid(CHIR::SkipCheck)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(CHIRFormat::Annotation::Annotation_skipCheck);
        annos.emplace_back(CHIRFormat::CreateSkipCheck(
            builder, CHIRFormat::SkipKind(SkipCheck::Extract(StaticCast<CHIR::SkipCheck*>(anno))))
                .Union());
    };

    // WrappedRawMethod may be removed body when removeUnusedImported, do not serializer it
    auto wrapMethod = dynamic_cast<Function*>(obj.Get<CHIR::WrappedRawMethod>());
    if (wrapMethod != nullptr && !wrapMethod->GetBody()) {
        annoHandler[typeid(CHIR::WrappedRawMethod)] = Empty;
    } else {
        annoHandler[typeid(CHIR::WrappedRawMethod)] = [this, &annos, &annoTypes](Annotation* anno) {
            annoTypes.push_back(CHIRFormat::Annotation::Annotation_wrappedRawMethod);
            auto rawMethod =
                GetId<Value>(StaticCast<Value*>(WrappedRawMethod::Extract(StaticCast<CHIR::WrappedRawMethod*>(anno))));
            annos.emplace_back(CHIRFormat::CreateWrappedRawMethod(builder, rawMethod).Union());
        };
    }
    // NeverOverflowInfo
    annoHandler[typeid(CHIR::NeverOverflowInfo)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(CHIRFormat::Annotation::Annotation_neverOverflowInfo);
        annos.emplace_back(CHIRFormat::CreateNeverOverflowInfo(
            builder, NeverOverflowInfo::Extract(StaticCast<CHIR::NeverOverflowInfo*>(anno)))
                .Union());
    };

    // GeneratedFromForIn
    annoHandler[typeid(CHIR::GeneratedFromForIn)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(CHIRFormat::Annotation::Annotation_generatedFromForIn);
        annos.emplace_back(CHIRFormat::CreateGeneratedFromForIn(
            builder, GeneratedFromForIn::Extract(StaticCast<CHIR::GeneratedFromForIn*>(anno)))
                .Union());
    };

    // IsAutoEnvClass
    annoHandler[typeid(CHIR::IsAutoEnvClass)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(CHIRFormat::Annotation::Annotation_isAutoEnvClass);
        annos.emplace_back(CHIRFormat::CreateIsAutoEnvClass(
            builder, IsAutoEnvClass::Extract(StaticCast<CHIR::IsAutoEnvClass*>(anno)))
                .Union());
    };

    // IsCapturedClassInCC
    annoHandler[typeid(CHIR::IsCapturedClassInCC)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(CHIRFormat::Annotation::Annotation_isCapturedClassInCC);
        annos.emplace_back(CHIRFormat::CreateIsCapturedClassInCC(
            builder, IsCapturedClassInCC::Extract(StaticCast<CHIR::IsCapturedClassInCC*>(anno)))
                .Union());
    };

    // EnumCaseIndex
    annoHandler[typeid(CHIR::EnumCaseIndex)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(CHIRFormat::Annotation::Annotation_enumCaseIndex);
        auto index = EnumCaseIndex::Extract(StaticCast<CHIR::EnumCaseIndex*>(anno));
        int64_t indexNum = -1;
        if (index.has_value()) {
            indexNum = static_cast<int64_t>(index.value());
        }
        annos.emplace_back(CHIRFormat::CreateEnumCaseIndex(builder, indexNum).Union());
    };

    // VirMethodOffset
    annoHandler[typeid(CHIR::VirMethodOffset)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(CHIRFormat::Annotation::Annotation_virMethodOffset);
        auto offset = VirMethodOffset::Extract(StaticCast<CHIR::VirMethodOffset*>(anno));
        int64_t offsetNum = -1;
        if (offset.has_value()) {
            offsetNum = static_cast<int64_t>(offset.value());
        }
        annos.emplace_back(CHIRFormat::CreateVirMethodOffset(builder, offsetNum).Union());
    };

    // OverrideSrcFuncType
    annoHandler[typeid(CHIR::OverrideSrcFuncType)] = [this, &annos, &annoTypes](Annotation* anno) {
        annoTypes.push_back(CHIRFormat::Annotation::Annotation_overrideSrcFuncType);
        auto funcType = GetId<Type>(OverrideSrcFuncType::Extract(StaticCast<CHIR::OverrideSrcFuncType*>(anno)));
        annos.emplace_back(CHIRFormat::CreateOverrideSrcFuncType(builder, funcType).Union());
    };
    
    annoHandler[typeid(CHIR::AnnoFactoryInfo)] = Empty;

    for (auto& entry : obj.GetAnno().GetAnnos()) {
        if (annoHandler.count(entry.first) != 0) {
            annoHandler.at(entry.first)(entry.second.get());
        } else {
            CJC_ABORT();
        }
    }
    auto loc = Serialize<CHIRFormat::DebugLocation>(obj.Base::GetDebugLocation());
    auto attributes = obj.GetAttributeInfo().GetRawAttrs().to_ulong();
    return CHIRFormat::CreateBaseDirect(builder, &annoTypes, &annos, loc, attributes);
}

template <>
flatbuffers::Offset<CHIRFormat::Expression> CHIRSerializer::CHIRSerializerImpl::Serialize(const Expression& obj);

template <>
flatbuffers::Offset<CHIRFormat::MemberVarInfo> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const MemberVarInfo& obj)
{
    auto name = obj.name;
    auto rawMangledName = obj.rawMangledName;
    auto type = GetId<Type>(obj.type);
    auto attributes = obj.attributeInfo.GetRawAttrs().to_ulong();
    auto loc = Serialize<CHIRFormat::DebugLocation>(obj.loc);
    auto annoInfo = Serialize<CHIRFormat::AnnoInfo>(obj.annoInfo);
    auto initializerFunc = GetId<Value>(obj.initializerFunc);
    auto outerDef = GetId<CustomTypeDef>(obj.outerDef);
    return CHIRFormat::CreateMemberVarInfoDirect(
        builder, name.data(), rawMangledName.data(), type, attributes, loc, annoInfo, initializerFunc, outerDef);
}

template <>
flatbuffers::Offset<CHIRFormat::EnumCtorInfo> CHIRSerializer::CHIRSerializerImpl::Serialize(const EnumCtorInfo& obj)
{
    return CHIRFormat::CreateEnumCtorInfoDirect(
        builder, obj.name.data(), obj.mangledName.data(), GetId<Type>(obj.funcType));
}

// ========================== Type Serializers =================================

template <> flatbuffers::Offset<CHIRFormat::Type> CHIRSerializer::CHIRSerializerImpl::Serialize(const Type& obj)
{
    auto kind = CHIRFormat::CHIRTypeKind(obj.GetTypeKind());
    auto argTys = GetId<Type>(obj.GetTypeArgs());
    return CHIRFormat::CreateTypeDirect(builder, kind, argTys.empty() ? nullptr : &argTys);
}

template <>
flatbuffers::Offset<CHIRFormat::RawArrayType> CHIRSerializer::CHIRSerializerImpl::Serialize(const RawArrayType& obj)
{
    auto base = Serialize<CHIRFormat::Type>(static_cast<const Type&>(obj));
    auto dims = obj.GetDims();
    return CHIRFormat::CreateRawArrayType(builder, base, dims);
}

template <>
flatbuffers::Offset<CHIRFormat::VArrayType> CHIRSerializer::CHIRSerializerImpl::Serialize(const VArrayType& obj)
{
    auto base = Serialize<CHIRFormat::Type>(static_cast<const Type&>(obj));
    auto size = obj.GetSize();
    return CHIRFormat::CreateVArrayType(builder, base, size);
}

template <>
flatbuffers::Offset<CHIRFormat::FuncType> CHIRSerializer::CHIRSerializerImpl::Serialize(const FuncType& obj)
{
    auto base = Serialize<CHIRFormat::Type>(static_cast<const Type&>(obj));
    auto isCFuncType = obj.IsCFunc();
    auto hasVarArg = obj.HasVarArg();
    return CHIRFormat::CreateFuncType(builder, base, isCFuncType, hasVarArg);
}

template <>
flatbuffers::Offset<CHIRFormat::CustomType> CHIRSerializer::CHIRSerializerImpl::Serialize(const CustomType& obj)
{
    auto base = Serialize<CHIRFormat::Type>(static_cast<const Type&>(obj));
    auto customTypeDef = GetId<CustomTypeDef>(obj.GetCustomTypeDef());
    return CHIRFormat::CreateCustomType(builder, base, customTypeDef);
}

template <>
flatbuffers::Offset<CHIRFormat::GenericType> CHIRSerializer::CHIRSerializerImpl::Serialize(const GenericType& obj)
{
    auto base = Serialize<CHIRFormat::Type>(static_cast<const Type&>(obj));
    auto identifier = obj.GetIdentifier();
    auto srcCodeIndentifier = obj.GetSrcCodeIdentifier();
    auto upperBounds = GetId<Type>(obj.GetUpperBounds());
    return CHIRFormat::CreateGenericTypeDirect(builder, base, identifier.data(),
        srcCodeIndentifier.data(), upperBounds.empty() ? nullptr : &upperBounds);
}

// ======================= Value Serializers ===================================
template <> flatbuffers::Offset<CHIRFormat::Value> CHIRSerializer::CHIRSerializerImpl::Serialize(const Value& obj)
{
    auto base = Serialize<CHIRFormat::Base>(static_cast<const Base&>(obj));
    auto identifier = obj.GetIdentifier();
    auto type = GetId<Type>(obj.GetType());
    // Map C++ ValueKind to schema ValueKind (IMPORTED_* removed; use attrs for imported)
    CHIRFormat::ValueKind kind;
    switch (obj.GetValueKind()) {
        case Value::ValueKind::KIND_LITERAL:
            kind = CHIRFormat::ValueKind_LITERAL;
            break;
        case Value::ValueKind::KIND_GLOBALVAR:
            kind = CHIRFormat::ValueKind_GLOBALVAR;
            break;
        case Value::ValueKind::KIND_PARAMETER:
            kind = CHIRFormat::ValueKind_PARAMETER;
            break;
        case Value::ValueKind::KIND_LOCALVAR:
            kind = CHIRFormat::ValueKind_LOCALVAR;
            break;
        case Value::ValueKind::KIND_FUNC:
            kind = CHIRFormat::ValueKind_FUNC;
            break;
        case Value::ValueKind::KIND_BLOCK:
            kind = CHIRFormat::ValueKind_BLOCK;
            break;
        case Value::ValueKind::KIND_BLOCK_GROUP:
            kind = CHIRFormat::ValueKind_BLOCK_GROUP;
            break;
        default:
            CJC_ABORT();
    }

    return CHIRFormat::CreateValueDirect(builder, base, type, identifier.data(), kind);
}

template <>
flatbuffers::Offset<CHIRFormat::Parameter> CHIRSerializer::CHIRSerializerImpl::Serialize(const Parameter& obj)
{
    auto base = Serialize<CHIRFormat::Value>(static_cast<const Value&>(obj));
    auto ownedFunc = GetId<Value>(obj.GetOwnerFunc());
    auto ownedLambda = GetId<Expression>(obj.GetOwnerLambda());
    auto srcCodeIdentifier = obj.GetSrcCodeIdentifier();
    auto annoInfo = Serialize<CHIRFormat::AnnoInfo>(obj.GetAnnoInfo());
    return CHIRFormat::CreateParameterDirect(
        builder, base, ownedFunc, ownedLambda, srcCodeIdentifier.data(), annoInfo);
}

template <>
flatbuffers::Offset<CHIRFormat::LocalVar> CHIRSerializer::CHIRSerializerImpl::Serialize(const LocalVar& obj)
{
    auto base = Serialize<CHIRFormat::Value>(static_cast<const Value&>(obj));
    auto associatedExpr = GetId<Expression>(obj.GetExpr());
    auto isRetVal = obj.IsRetValue();
    auto srcCodeIdentifier = obj.GetSrcCodeIdentifier();
    return CHIRFormat::CreateLocalVarDirect(builder, base, associatedExpr, isRetVal, srcCodeIdentifier.data());
}

template <>
flatbuffers::Offset<CHIRFormat::GlobalValue> CHIRSerializer::CHIRSerializerImpl::Serialize(const GlobalValue& obj)
{
    auto valueOffset = Serialize<CHIRFormat::Value>(static_cast<const Value&>(obj));
    auto declaredParent = obj.GetParentCustomTypeDef() ? GetId<CustomTypeDef>(obj.GetParentCustomTypeDef()) : 0u;
    std::vector<flatbuffers::Offset<flatbuffers::String>> features;
    for (const auto& name : obj.GetFeatures()) {
        features.push_back(builder.CreateSharedString(name));
    }
    auto annoInfo = Serialize<CHIRFormat::AnnoInfo>(obj.GetAnnoInfo());
    return CHIRFormat::CreateGlobalValueDirect(builder, valueOffset,
        obj.GetSrcCodeIdentifier().data(), obj.GetRawMangledName().data(), obj.GetPackageName().data(),
        declaredParent, features.empty() ? nullptr : &features, annoInfo);
}

template <>
flatbuffers::Offset<CHIRFormat::GlobalVar> CHIRSerializer::CHIRSerializerImpl::Serialize(const GlobalVar& obj)
{
    auto globalSymbolOffset = Serialize<CHIRFormat::GlobalValue>(static_cast<const GlobalValue&>(obj));
    uint32_t initializerId = obj.GetInitializerValue() ? GetId<Value>(obj.GetInitializerValue()) : 0;
    return CHIRFormat::CreateGlobalVar(builder, globalSymbolOffset, initializerId);
}

template <> flatbuffers::Offset<CHIRFormat::Block> CHIRSerializer::CHIRSerializerImpl::Serialize(const Block& obj)
{
    auto base = Serialize<CHIRFormat::Value>(static_cast<const Value&>(obj));
    auto parentGroup = GetId<Value>(obj.GetParentBlockGroup());
    auto exprs = GetId<Expression>(obj.GetExpressions());
    auto predecessors = GetId<Value>(obj.GetPredecessors());
    auto exceptionCatchList = GetId<Type>(obj.IsLandingPadBlock() ? obj.GetExceptions() : std::vector<ClassType*>());
    return CHIRFormat::CreateBlockDirect(builder, base, parentGroup, exprs.empty() ? nullptr : &exprs,
        predecessors.empty() ? nullptr : &predecessors, obj.IsLandingPadBlock(),
        exceptionCatchList.empty() ? nullptr : &exceptionCatchList);
}

template <>
flatbuffers::Offset<CHIRFormat::BlockGroup> CHIRSerializer::CHIRSerializerImpl::Serialize(const BlockGroup& obj)
{
    CJC_ASSERT(obj.GetOwnerFunc() || obj.GetOwnerExpression());
    auto base = Serialize<CHIRFormat::Value>(static_cast<const Value&>(obj));
    auto entryBlock = GetId<Value>(obj.GetEntryBlock());
    auto blocks = GetId<Value>(obj.GetBlocks());
    auto ownedFunc = GetId<Value>(obj.GetOwnerFunc());
    auto ownedExpression = GetId<Expression>(obj.GetOwnerExpression());
    return CHIRFormat::CreateBlockGroupDirect(
        builder, base, entryBlock, blocks.empty() ? nullptr : &blocks, ownedFunc, ownedExpression);
}

template <>
flatbuffers::Offset<CHIRFormat::Function> CHIRSerializer::CHIRSerializerImpl::Serialize(const Function& obj)
{
    auto globalSymbolOffset = Serialize<CHIRFormat::GlobalValue>(static_cast<const GlobalValue&>(obj));

    // skip serializing genericDecl when it's imported (no body)
    uint32_t genericDecl = 0;
    if (auto gFunc = obj.GetGenericDecl(); gFunc && gFunc->IsFuncWithBody()) {
        genericDecl = GetId<Value>(gFunc);
    }

    auto funcKind = CHIRFormat::FuncKind(obj.GetFuncKind());
    flatbuffers::Offset<CHIRFormat::FuncSigInfo> originalLambdaInfoOffset = 0;
    if (obj.GetFuncKind() == LAMBDA && obj.originalLambdaInfo.funcType != nullptr) {
        auto funcName = obj.originalLambdaInfo.funcName;
        auto oriLambdaFuncTy = GetId<Type>(obj.originalLambdaInfo.funcType);
        auto oriLambdaGenericTypeParams = GetId<Type>(obj.originalLambdaInfo.genericTypeParams);
        originalLambdaInfoOffset = CHIRFormat::CreateFuncSigInfoDirect(builder, funcName.data(), oriLambdaFuncTy,
            oriLambdaGenericTypeParams.empty() ? nullptr : &oriLambdaGenericTypeParams);
    }
    auto genericTypeParams = GetId<Type>(obj.GetGenericTypeParams());
    auto paramDftValHostFunc = GetId<Value>(obj.GetParamDftValHostFunc());

    uint32_t body = 0;
    std::vector<uint32_t> params = GetId<Value>(obj.GetParams());
    uint32_t retVal = 0;
    auto propLoc = Serialize<CHIRFormat::DebugLocation>(obj.GetPropLocation());
    if (obj.IsFuncWithBody()) {
        CJC_NULLPTR_CHECK(obj.GetBody());
        body = GetId<Value>(obj.GetBody());
        retVal = GetId<Value>(obj.GetReturnValue());
    }

    return CHIRFormat::CreateFunctionDirect(builder, globalSymbolOffset, genericDecl, funcKind, obj.IsFastNative(),
        obj.IsCFFIWrapper(), originalLambdaInfoOffset, genericTypeParams.empty() ? nullptr : &genericTypeParams,
        paramDftValHostFunc, body, params.empty() ? nullptr : &params, retVal, propLoc, obj.localId, obj.blockId,
        obj.blockGroupId);
}

template <>
flatbuffers::Offset<CHIRFormat::LiteralValue> CHIRSerializer::CHIRSerializerImpl::Serialize(const LiteralValue& obj)
{
    auto base = Serialize<CHIRFormat::Value>(static_cast<const Value&>(obj));
    auto literalKind = CHIRFormat::ConstantValueKind(obj.GetConstantValueKind());
    return CHIRFormat::CreateLiteralValue(builder, base, literalKind);
}

// ======================= Literal Value Serializers ===========================
template <>
flatbuffers::Offset<CHIRFormat::BoolLiteral> CHIRSerializer::CHIRSerializerImpl::Serialize(const BoolLiteral& obj)
{
    auto base = Serialize<CHIRFormat::LiteralValue>(static_cast<const LiteralValue&>(obj));
    auto val = obj.GetVal();
    return CHIRFormat::CreateBoolLiteral(builder, base, val);
}

template <>
flatbuffers::Offset<CHIRFormat::RuneLiteral> CHIRSerializer::CHIRSerializerImpl::Serialize(const RuneLiteral& obj)
{
    auto base = Serialize<CHIRFormat::LiteralValue>(static_cast<const LiteralValue&>(obj));
    auto val = obj.GetVal();
    return CHIRFormat::CreateRuneLiteral(builder, base, val);
}

template <>
flatbuffers::Offset<CHIRFormat::StringLiteral> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const StringLiteral& obj)
{
    auto base = Serialize<CHIRFormat::LiteralValue>(static_cast<const LiteralValue&>(obj));
    auto val = builder.CreateSharedString(obj.GetVal());
    return CHIRFormat::CreateStringLiteral(builder, base, val);
}

template <>
flatbuffers::Offset<CHIRFormat::IntLiteral> CHIRSerializer::CHIRSerializerImpl::Serialize(const IntLiteral& obj)
{
    auto base = Serialize<CHIRFormat::LiteralValue>(static_cast<const LiteralValue&>(obj));
    auto val = obj.GetUnsignedVal();
    return CHIRFormat::CreateIntLiteral(builder, base, val);
}

template <>
flatbuffers::Offset<CHIRFormat::FloatLiteral> CHIRSerializer::CHIRSerializerImpl::Serialize(const FloatLiteral& obj)
{
    auto base = Serialize<CHIRFormat::LiteralValue>(static_cast<const LiteralValue&>(obj));
    auto val = obj.GetVal();
    return CHIRFormat::CreateFloatLiteral(builder, base, val);
}

template <>
flatbuffers::Offset<CHIRFormat::UnitLiteral> CHIRSerializer::CHIRSerializerImpl::Serialize(const UnitLiteral& obj)
{
    auto base = Serialize<CHIRFormat::LiteralValue>(static_cast<const LiteralValue&>(obj));
    return CHIRFormat::CreateUnitLiteral(builder, base);
}

template <>
flatbuffers::Offset<CHIRFormat::NullLiteral> CHIRSerializer::CHIRSerializerImpl::Serialize(const NullLiteral& obj)
{
    auto base = Serialize<CHIRFormat::LiteralValue>(static_cast<const LiteralValue&>(obj));
    return CHIRFormat::CreateNullLiteral(builder, base);
}

// ======================= Expression Serializers ==============================

static CHIRFormat::CHIRExprKind ToPackageExprKind(const Expression& expr)
{
    switch (expr.GetExprKind()) {
        case ExprKind::GOTO:                    return CHIRFormat::CHIRExprKind_Goto;
        case ExprKind::BRANCH:                  return CHIRFormat::CHIRExprKind_Branch;
        case ExprKind::MULTIBRANCH:             return CHIRFormat::CHIRExprKind_MultiBranch;
        case ExprKind::EXIT:                    return CHIRFormat::CHIRExprKind_Exit;
        case ExprKind::TRY_APPLY:               return CHIRFormat::CHIRExprKind_TryApply;
        case ExprKind::TRY_INVOKE:              return CHIRFormat::CHIRExprKind_TryInvoke;
        case ExprKind::TRY_INVOKESTATIC:        return CHIRFormat::CHIRExprKind_TryInvoke;
        case ExprKind::RAISE_EXCEPTION:         return CHIRFormat::CHIRExprKind_RaiseException;
        case ExprKind::TRY_NEG:                 return CHIRFormat::CHIRExprKind_TryNeg;
        case ExprKind::TRY_ADD:                 return CHIRFormat::CHIRExprKind_TryAdd;
        case ExprKind::TRY_SUB:                 return CHIRFormat::CHIRExprKind_TrySub;
        case ExprKind::TRY_MUL:                 return CHIRFormat::CHIRExprKind_TryMul;
        case ExprKind::TRY_DIV:                 return CHIRFormat::CHIRExprKind_TryDiv;
        case ExprKind::TRY_MOD:                 return CHIRFormat::CHIRExprKind_TryMod;
        case ExprKind::TRY_EXP:                 return CHIRFormat::CHIRExprKind_TryExp;
        case ExprKind::TRY_LSHIFT:              return CHIRFormat::CHIRExprKind_TryLShift;
        case ExprKind::TRY_RSHIFT:              return CHIRFormat::CHIRExprKind_TryRShift;
        case ExprKind::TRY_SPAWN:               return CHIRFormat::CHIRExprKind_TrySpawn;
        case ExprKind::TRY_NUMERIC_CAST:        return CHIRFormat::CHIRExprKind_TryNumericCast;
        case ExprKind::TRY_INTRINSIC:           return CHIRFormat::CHIRExprKind_TryIntrinsic;
        case ExprKind::TRY_ALLOCATE:            return CHIRFormat::CHIRExprKind_TryAllocate;
        case ExprKind::TRY_RAW_ARRAY_ALLOCATE:  return CHIRFormat::CHIRExprKind_TryRawArrayAllocate;
        case ExprKind::NEG:                     return CHIRFormat::CHIRExprKind_Neg;
        case ExprKind::NOT:                     return CHIRFormat::CHIRExprKind_Not;
        case ExprKind::BITNOT:                  return CHIRFormat::CHIRExprKind_BitNot;
        case ExprKind::ADD:                     return CHIRFormat::CHIRExprKind_Add;
        case ExprKind::SUB:                     return CHIRFormat::CHIRExprKind_Sub;
        case ExprKind::MUL:                     return CHIRFormat::CHIRExprKind_Mul;
        case ExprKind::DIV:                     return CHIRFormat::CHIRExprKind_Div;
        case ExprKind::MOD:                     return CHIRFormat::CHIRExprKind_Mod;
        case ExprKind::EXP:                     return CHIRFormat::CHIRExprKind_Exp;
        case ExprKind::LSHIFT:                  return CHIRFormat::CHIRExprKind_LShift;
        case ExprKind::RSHIFT:                  return CHIRFormat::CHIRExprKind_RShift;
        case ExprKind::BITAND:                  return CHIRFormat::CHIRExprKind_BitAnd;
        case ExprKind::BITOR:                   return CHIRFormat::CHIRExprKind_BitOr;
        case ExprKind::BITXOR:                  return CHIRFormat::CHIRExprKind_BitXor;
        case ExprKind::LT:                      return CHIRFormat::CHIRExprKind_LT;
        case ExprKind::GT:                      return CHIRFormat::CHIRExprKind_GT;
        case ExprKind::LE:                      return CHIRFormat::CHIRExprKind_LE;
        case ExprKind::GE:                      return CHIRFormat::CHIRExprKind_GE;
        case ExprKind::EQUAL:                   return CHIRFormat::CHIRExprKind_Equal;
        case ExprKind::NOTEQUAL:                return CHIRFormat::CHIRExprKind_NotEqual;
        case ExprKind::AND:                     return CHIRFormat::CHIRExprKind_And;
        case ExprKind::OR:                      return CHIRFormat::CHIRExprKind_Or;
        case ExprKind::CLASS_STATIC_CAST:       return CHIRFormat::CHIRExprKind_StaticCast;
        case ExprKind::NUMERIC_CAST:            return CHIRFormat::CHIRExprKind_NumericCast;
        case ExprKind::BOX:                     return CHIRFormat::CHIRExprKind_Box;
        case ExprKind::UNBOX_TO_VALUE:          return CHIRFormat::CHIRExprKind_UnboxToValue;
        case ExprKind::UNBOX_TO_REF:            return CHIRFormat::CHIRExprKind_UnboxToRef;
        case ExprKind::CAST_TO_GENERIC:         return CHIRFormat::CHIRExprKind_CastToGeneric;
        case ExprKind::CAST_TO_CONCRETE:        return CHIRFormat::CHIRExprKind_CastToConcrete;
        case ExprKind::ALLOCATE:                return CHIRFormat::CHIRExprKind_Allocate;
        case ExprKind::LOAD:                    return CHIRFormat::CHIRExprKind_Load;
        case ExprKind::STORE:                   return CHIRFormat::CHIRExprKind_Store;
        case ExprKind::GET_ELEMENT_BY_NAME:     return CHIRFormat::CHIRExprKind_GetElementByName;
        case ExprKind::GET_ELEMENT_REF:         return CHIRFormat::CHIRExprKind_GetElementRef;
        case ExprKind::STORE_ELEMENT_BY_NAME:   return CHIRFormat::CHIRExprKind_StoreElementByName;
        case ExprKind::STORE_ELEMENT_REF:       return CHIRFormat::CHIRExprKind_StoreElementRef;
        case ExprKind::FIELD:                   return CHIRFormat::CHIRExprKind_Field;
        case ExprKind::FIELD_BY_NAME:           return CHIRFormat::CHIRExprKind_FieldByName;
        case ExprKind::RAW_ARRAY_ALLOCATE:      return CHIRFormat::CHIRExprKind_RawArrayAllocate;
        case ExprKind::RAW_ARRAY_LITERAL_INIT:  return CHIRFormat::CHIRExprKind_RawArrayLiteralInit;
        case ExprKind::RAW_ARRAY_INIT_BY_VALUE: return CHIRFormat::CHIRExprKind_RawArrayInitByValue;
        case ExprKind::VARRAY:                  return CHIRFormat::CHIRExprKind_VArrayExpr;
        case ExprKind::VARRAY_BUILDER:          return CHIRFormat::CHIRExprKind_VArrayBuilder;
        case ExprKind::CONSTANT:                return CHIRFormat::CHIRExprKind_Constant;
        case ExprKind::DEBUGEXPR:               return CHIRFormat::CHIRExprKind_Debug;
        case ExprKind::TUPLE:                   return CHIRFormat::CHIRExprKind_Tuple;
        case ExprKind::INSTANCEOF:              return CHIRFormat::CHIRExprKind_InstanceOf;
        case ExprKind::GET_EXCEPTION:           return CHIRFormat::CHIRExprKind_GetException;
        case ExprKind::SPAWN:                   return CHIRFormat::CHIRExprKind_Spawn;
        case ExprKind::LAMBDA:                  return CHIRFormat::CHIRExprKind_Lambda;
        case ExprKind::GET_INSTANTIATE_VALUE:   return CHIRFormat::CHIRExprKind_GetInstantiateValue;
        case ExprKind::APPLY:                   return CHIRFormat::CHIRExprKind_Apply;
        case ExprKind::INVOKE:                  return CHIRFormat::CHIRExprKind_Invoke;
        case ExprKind::INVOKESTATIC:            return CHIRFormat::CHIRExprKind_Invoke;
        case ExprKind::INTRINSIC:               return CHIRFormat::CHIRExprKind_Intrinsic;
        case ExprKind::GET_RTTI:                return CHIRFormat::CHIRExprKind_GetRtti;
        case ExprKind::GET_RTTI_STATIC:         return CHIRFormat::CHIRExprKind_GetRttiStatic;
        case ExprKind::FORIN_RANGE:
        case ExprKind::FORIN_ITER:
        case ExprKind::FORIN_CLOSED_RANGE:
        case ExprKind::INVALID:
        case ExprKind::MAX_EXPR_KINDS:
        default:                                return CHIRFormat::CHIRExprKind_Invalid;
    }
}

template <>
flatbuffers::Offset<CHIRFormat::Expression> CHIRSerializer::CHIRSerializerImpl::Serialize(const Expression& obj)
{
    auto base = Serialize<CHIRFormat::Base>(static_cast<const Base&>(obj));
    auto kind = ToPackageExprKind(obj);
    auto operands = GetId<Value>(obj.GetOperands());
    auto blockGroups = GetId<Value>(obj.GetBlockGroups());
    auto owner = GetId<Value>(obj.GetParentBlock());
    auto resultLocalVar = GetId<Value>(obj.GetResult());
    auto resultTy = GetId<Type>(obj.GetResult() ? obj.GetResult()->GetType() : nullptr);
    return CHIRFormat::CreateExpressionDirect(builder, base, kind,
        operands.empty() ? nullptr : &operands, blockGroups.empty() ? nullptr : &blockGroups, owner,
        resultLocalVar, resultTy);
}

template <>
flatbuffers::Offset<CHIRFormat::UnaryExpressionBase> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const UnaryExpressionBase& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto overflowStrategy = SerializeOverflowStrategy(obj.GetOverflowStrategy());
    return CHIRFormat::CreateUnaryExpressionBase(builder, base, overflowStrategy);
}

template <>
flatbuffers::Offset<CHIRFormat::BinaryExpressionBase> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const BinaryExpressionBase& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto overflowStrategy = SerializeOverflowStrategy(obj.GetOverflowStrategy());
    return CHIRFormat::CreateBinaryExpressionBase(builder, base, overflowStrategy);
}

template <>
flatbuffers::Offset<CHIRFormat::AllocateBase> CHIRSerializer::CHIRSerializerImpl::Serialize(const AllocateBase& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto allocatedType = GetId<Type>(obj.GetType());
    return CHIRFormat::CreateAllocateBase(builder, base, allocatedType);
}

template <>
flatbuffers::Offset<CHIRFormat::GetElementRef> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const GetElementRef& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto path = obj.GetPath();
    return CHIRFormat::CreateGetElementRefDirect(builder, base, path.empty() ? nullptr : &path);
}

template <>
flatbuffers::Offset<CHIRFormat::GetElementByName> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const GetElementByName& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto names = builder.CreateVectorOfStrings(obj.GetNames());
    return CHIRFormat::CreateGetElementByName(builder, base, names);
}

template <>
flatbuffers::Offset<CHIRFormat::StoreElementRef> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const StoreElementRef& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto path = obj.GetPath();
    return CHIRFormat::CreateStoreElementRefDirect(builder, base, &path);
}

template <>
flatbuffers::Offset<CHIRFormat::StoreElementByName> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const StoreElementByName& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto names = builder.CreateVectorOfStrings(obj.GetNames());
    return CHIRFormat::CreateStoreElementByName(builder, base, names);
}

template <>
flatbuffers::Offset<CHIRFormat::ApplyBase> CHIRSerializer::CHIRSerializerImpl::Serialize(const ApplyBase& obj)
{
    // 1. serialize Expression
    auto exprBase = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));

    // 2. serialize FuncCall
    auto instTypeArgs = GetId<Type>(obj.GetInstantiatedTypeArgs());
    auto thisType = GetId<Type>(obj.GetThisType());
    auto funcCall = CHIRFormat::CreateFuncCallDirect(
        builder, exprBase, instTypeArgs.empty() ? nullptr : &instTypeArgs, thisType);

    // 3. serialize ApplyBase (only Apply may be a super call)
    auto isSuperCall = false;
    if (auto apply = DynamicCast<const Apply*>(&obj)) {
        isSuperCall = apply->IsSuperCall();
    }
    return CHIRFormat::CreateApplyBase(builder, funcCall, isSuperCall);
}

template <>
flatbuffers::Offset<CHIRFormat::FuncSigInfo> CHIRSerializer::CHIRSerializerImpl::Serialize(const FuncSigInfo& obj)
{
    auto tempTypes = GetId<Type>(obj.genericTypeParams);
    return CHIRFormat::CreateFuncSigInfoDirect(builder, obj.funcName.data(),
        GetId<Type>(obj.funcType), tempTypes.empty() ? nullptr : &tempTypes);
}

template <>
flatbuffers::Offset<CHIRFormat::InvokeBase> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const DynamicDispatch& obj)
{
    // 1. serialize Expression
    auto exprBase = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));

    // 2. serialize FuncCall
    auto instTypeArgs = GetId<Type>(obj.GetInstantiatedTypeArgs());
    auto thisType = GetId<Type>(obj.GetThisType());
    auto funcCall = CHIRFormat::CreateFuncCallDirect(
        builder, exprBase, instTypeArgs.empty() ? nullptr : &instTypeArgs, thisType);

    // 3. serialize InvokeBase (covers Invoke / TryInvoke / InvokeStatic / TryInvokeStatic)
    auto overflowStrategy = SerializeOverflowStrategy(obj.overflowStrategy);
    return CHIRFormat::CreateInvokeBase(builder, funcCall, overflowStrategy);
}

template <>
flatbuffers::Offset<CHIRFormat::NumericCastBase> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const NumericCastBase& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto overflowStrategy = SerializeOverflowStrategy(obj.GetOverflowStrategy());
    return CHIRFormat::CreateNumericCastBase(builder, base, overflowStrategy);
}

template <>
flatbuffers::Offset<CHIRFormat::InstanceOf> CHIRSerializer::CHIRSerializerImpl::Serialize(const InstanceOf& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto targetType = GetId<Type>(obj.GetType());
    return CHIRFormat::CreateInstanceOf(builder, base, targetType);
}

template <>
flatbuffers::Offset<CHIRFormat::Branch> CHIRSerializer::CHIRSerializerImpl::Serialize(const Branch& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto sourceExpr = obj.GetSourceExpr();
    return CHIRFormat::CreateBranch(builder, base, CHIRFormat::SourceExpr(sourceExpr));
}

template <>
flatbuffers::Offset<CHIRFormat::MultiBranch> CHIRSerializer::CHIRSerializerImpl::Serialize(const MultiBranch& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto caseVals = obj.GetCaseVals();
    return CHIRFormat::CreateMultiBranchDirect(builder, base, caseVals.empty() ? nullptr : &caseVals);
}

template <>
flatbuffers::Offset<CHIRFormat::GetInstantiateValue> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const GetInstantiateValue& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto instantiateTys = GetId<Type>(obj.GetInstantiateTypes());
    return CHIRFormat::CreateGetInstantiateValueDirect(
        builder, base, instantiateTys.empty() ? nullptr : &instantiateTys);
}

template <>
flatbuffers::Offset<CHIRFormat::GetRTTIStatic> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const GetRTTIStatic& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto rtti = GetId<Type>(obj.GetRTTIType());
    return CHIRFormat::CreateGetRTTIStatic(builder, base, rtti);
}

template <>
flatbuffers::Offset<CHIRFormat::Field> CHIRSerializer::CHIRSerializerImpl::Serialize(const Field& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto path = obj.GetPath();
    return CHIRFormat::CreateFieldDirect(builder, base, path.empty() ? nullptr : &path);
}

template <>
flatbuffers::Offset<CHIRFormat::FieldByName> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const FieldByName& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto names = builder.CreateVectorOfStrings(obj.GetNames());
    return CHIRFormat::CreateFieldByName(builder, base, names);
}

template <>
flatbuffers::Offset<CHIRFormat::RawArrayAllocateBase> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const RawArrayAllocateBase& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto elementType = GetId<Type>(obj.GetElementType());
    return CHIRFormat::CreateRawArrayAllocateBase(builder, base, elementType);
}

template <>
flatbuffers::Offset<CHIRFormat::IntrinsicBase> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const IntrinsicBase& obj)
{
    // 1. serialize Expression
    auto exprBase = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));

    // 2. serialize FuncCall
    auto instantiatedTypeArgs = GetId<Type>(obj.GetInstantiatedTypeArgs());
    auto funcCall = CHIRFormat::CreateFuncCallDirect(
        builder, exprBase, instantiatedTypeArgs.empty() ? nullptr : &instantiatedTypeArgs, 0);

    // 3. serialize IntrinsicBase
    auto intrinsicKind = CHIRFormat::IntrinsicKind(obj.GetIntrinsicKind());
    return CHIRFormat::CreateIntrinsicBase(builder, funcCall, intrinsicKind);
}

template <>
flatbuffers::Offset<CHIRFormat::Debug> CHIRSerializer::CHIRSerializerImpl::Serialize(const Debug& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto srcCodeIdentifier = obj.GetSrcCodeIdentifier();
    CJC_NULLPTR_CHECK(obj.GetValue());
    return CHIRFormat::CreateDebugDirect(builder, base, srcCodeIdentifier.data());
}

template <>
flatbuffers::Offset<CHIRFormat::SpawnBase> CHIRSerializer::CHIRSerializerImpl::Serialize(const SpawnBase& obj)
{
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto executeClosure = GetId<Value>(obj.GetExecuteClosure());
    return CHIRFormat::CreateSpawnBase(builder, base, executeClosure);
}

template <>
flatbuffers::Offset<CHIRFormat::Lambda> CHIRSerializer::CHIRSerializerImpl::Serialize(const Lambda& obj)
{
    CJC_ASSERT(obj.GetBlockGroups().size() == 1);
    CJC_ASSERT(obj.GetBody());
    auto base = Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj));
    auto funcTy = GetId<Type>(obj.GetFuncType());
    auto isLocalFunc = obj.IsLocalFunc();
    auto identifier = obj.GetIdentifier();
    auto srcCodeIdentifier = obj.GetSrcCodeIdentifier();
    auto params = GetId<Value>(obj.GetParams());
    auto genericTypeParams = GetId<Type>(obj.GetGenericTypeParams());
    auto body = GetId<Value>(obj.GetBody());
    auto retVal = GetId<Value>(obj.GetReturnValue());
    auto isConst = obj.IsCompileTimeValue();
    return CHIRFormat::CreateLambdaDirect(builder, base, funcTy, isLocalFunc, identifier.data(),
        srcCodeIdentifier.data(), params.empty() ? nullptr : &params,
        genericTypeParams.empty() ? nullptr : &genericTypeParams, body, retVal, isConst);
}

// ======================= Custom Type Def Serializers =========================
template <>
flatbuffers::Offset<CHIRFormat::VirtualMethodInfo> CHIRSerializer::CHIRSerializerImpl::Serialize(
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

    return CHIRFormat::CreateVirtualMethodInfoDirect(builder, funcName.data(), sigType,
        genericTypeParams.empty() ? nullptr : &genericTypeParams, funcPtr, attributes, originalType, parentType,
        returnType);
}

std::vector<flatbuffers::Offset<CHIRFormat::VTableInType>> CHIRSerializer::CHIRSerializerImpl::SerializeVTable(
    const VTableInDef& obj)
{
    std::vector<flatbuffers::Offset<CHIRFormat::VTableInType>> retval;
    for (const auto& elem : obj.GetTypeVTables()) {
        auto ty = GetId<Type>(elem.GetSrcParentType());
        auto info = SerializeVec<CHIRFormat::VirtualMethodInfo>(elem.GetVirtualMethods());
        retval.push_back(CHIRFormat::CreateVTableInTypeDirect(builder, ty, &info));
    }
    return retval;
}

template <>
flatbuffers::Offset<CHIRFormat::CustomTypeDef> CHIRSerializer::CHIRSerializerImpl::Serialize(
    const CustomTypeDef& obj)
{
    auto base = Serialize<CHIRFormat::Base>(static_cast<const Base&>(obj));
    auto kind = CHIRFormat::CustomDefKind(obj.GetCustomKind());
    auto customTypeDefID = GetId<CustomTypeDef>(&obj);
    auto srcCodeIdentifier = obj.GetSrcCodeIdentifier();
    auto identifier = obj.GetIdentifier();
    auto packageName = obj.GetPackageName();
    auto type = GetId<Type>(obj.CustomTypeDef::GetType());
    auto genericDecl = GetId<CustomTypeDef>(obj.GetGenericDecl());
    auto methods = GetId<Value>(obj.GetMethods());
    auto implementedInterfaces = GetId<Type>(obj.GetImplementedInterfaceTys());
    auto instanceMemberVars = obj.GetCustomKind() == CustomDefKind::TYPE_CLASS
        ? SerializeVec<CHIRFormat::MemberVarInfo>(StaticCast<const ClassDef&>(obj).GetDirectInstanceVars())
        : SerializeVec<CHIRFormat::MemberVarInfo>(obj.GetAllInstanceVars());
    auto staticMemberVars = GetId<Value>(obj.GetStaticMemberVars());
    auto annoInfo = Serialize<CHIRFormat::AnnoInfo>(obj.GetAnnoInfo());
    auto vtable = SerializeVTable(obj.GetDefVTable());
    auto varInitializationFunc = GetId<Value>(obj.GetVarInitializationFunc());
    return CHIRFormat::CreateCustomTypeDefDirect(builder, base, kind, customTypeDefID, srcCodeIdentifier.data(),
        identifier.data(), packageName.data(), type, genericDecl, methods.empty() ? nullptr : &methods,
        implementedInterfaces.empty() ? nullptr : &implementedInterfaces, &instanceMemberVars,
        staticMemberVars.empty() ? nullptr : &staticMemberVars, annoInfo, &vtable, varInitializationFunc);
}

template <>
flatbuffers::Offset<CHIRFormat::EnumDef> CHIRSerializer::CHIRSerializerImpl::Serialize(const EnumDef& obj)
{
    auto base = Serialize<CHIRFormat::CustomTypeDef>(static_cast<const CustomTypeDef&>(obj));
    auto ctors = SerializeVec<CHIRFormat::EnumCtorInfo>(obj.GetCtors());
    auto nonExhaustive = !obj.IsExhaustive();
    return CHIRFormat::CreateEnumDefDirect(builder, base, &ctors, nonExhaustive);
}

template <>
flatbuffers::Offset<CHIRFormat::StructDef> CHIRSerializer::CHIRSerializerImpl::Serialize(const StructDef& obj)
{
    auto base = Serialize<CHIRFormat::CustomTypeDef>(static_cast<const CustomTypeDef&>(obj));
    auto isCStruct = obj.IsCStruct();
    return CHIRFormat::CreateStructDef(builder, base, isCStruct);
}

template <>
flatbuffers::Offset<CHIRFormat::ClassDef> CHIRSerializer::CHIRSerializerImpl::Serialize(const ClassDef& obj)
{
    auto base = Serialize<CHIRFormat::CustomTypeDef>(static_cast<const CustomTypeDef&>(obj));
    std::vector<uint32_t> annotationTargetIds;
    const std::vector<uint32_t>* annotationTargetsVec = nullptr;
    if (obj.IsAnnotation()) {
        annotationTargetIds = GetId<Value>(obj.GetAnnotationTargets());
        annotationTargetsVec = &annotationTargetIds;
    }
    auto superClass = GetId<Type>(obj.GetSuperClassTy());
    return CHIRFormat::CreateClassDefDirect(
        builder, base, obj.IsClass(), obj.IsAnnotation(), annotationTargetsVec, superClass);
}

template <>
flatbuffers::Offset<CHIRFormat::ExtendDef> CHIRSerializer::CHIRSerializerImpl::Serialize(const ExtendDef& obj)
{
    auto base = Serialize<CHIRFormat::CustomTypeDef>(static_cast<const CustomTypeDef&>(obj));
    auto extendedType = GetId<Type>(obj.GetExtendedType());
    auto genericParams = GetId<Type>(obj.GetGenericTypeParams());
    return CHIRFormat::CreateExtendDefDirect(
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
        case Type::TypeKind::TYPE_FLOAT16:
        case Type::TypeKind::TYPE_FLOAT32:
        case Type::TypeKind::TYPE_FLOAT64:
        case Type::TypeKind::TYPE_RUNE:
        case Type::TypeKind::TYPE_BOOLEAN:
        case Type::TypeKind::TYPE_UNIT:
        case Type::TypeKind::TYPE_NOTHING:
        case Type::TypeKind::TYPE_VOID:
        case Type::TypeKind::TYPE_TUPLE:
        case Type::TypeKind::TYPE_CPOINTER:
        case Type::TypeKind::TYPE_CSTRING:
        case Type::TypeKind::TYPE_REFTYPE:
        case Type::TypeKind::TYPE_BOXTYPE:
        case Type::TypeKind::TYPE_THIS:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(CHIRFormat::TypeElem_Type);
            return Serialize<CHIRFormat::Type>(static_cast<const Type&>(obj)).Union();
        case Type::TypeKind::TYPE_STRUCT:
        case Type::TypeKind::TYPE_ENUM:
        case Type::TypeKind::TYPE_CLASS:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(CHIRFormat::TypeElem_CustomType);
            return Serialize<CHIRFormat::CustomType>(static_cast<const CustomType&>(obj)).Union();
        case Type::TypeKind::TYPE_FUNC:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(CHIRFormat::TypeElem_FuncType);
            return Serialize<CHIRFormat::FuncType>(static_cast<const FuncType&>(obj)).Union();
        case Type::TypeKind::TYPE_RAWARRAY:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(CHIRFormat::TypeElem_RawArrayType);
            return Serialize<CHIRFormat::RawArrayType>(static_cast<const RawArrayType&>(obj)).Union();
        case Type::TypeKind::TYPE_VARRAY:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(CHIRFormat::TypeElem_VArrayType);
            return Serialize<CHIRFormat::VArrayType>(static_cast<const VArrayType&>(obj)).Union();
        case Type::TypeKind::TYPE_GENERIC:
            typeKind[GetId<Type>(&obj) - 1] = static_cast<uint8_t>(CHIRFormat::TypeElem_GenericType);
            return Serialize<CHIRFormat::GenericType>(static_cast<const GenericType&>(obj)).Union();
        default:
            CJC_ABORT();
            return 0;
    }
}

template <> flatbuffers::Offset<void> CHIRSerializer::CHIRSerializerImpl::Dispatch(const LiteralValue& obj)
{
    switch (obj.GetConstantValueKind()) {
        case ConstantValueKind::KIND_BOOL:
            valueKind[GetId<Value>(&obj) - 1] = CHIRFormat::ValueElem_BoolLiteral;
            return Serialize<CHIRFormat::BoolLiteral>(static_cast<const BoolLiteral&>(obj)).Union();
        case ConstantValueKind::KIND_RUNE:
            valueKind[GetId<Value>(&obj) - 1] = CHIRFormat::ValueElem_RuneLiteral;
            return Serialize<CHIRFormat::RuneLiteral>(static_cast<const RuneLiteral&>(obj)).Union();
        case ConstantValueKind::KIND_INT:
            valueKind[GetId<Value>(&obj) - 1] = CHIRFormat::ValueElem_IntLiteral;
            return Serialize<CHIRFormat::IntLiteral>(static_cast<const IntLiteral&>(obj)).Union();
        case ConstantValueKind::KIND_FLOAT:
            valueKind[GetId<Value>(&obj) - 1] = CHIRFormat::ValueElem_FloatLiteral;
            return Serialize<CHIRFormat::FloatLiteral>(static_cast<const FloatLiteral&>(obj)).Union();
        case ConstantValueKind::KIND_STRING:
            valueKind[GetId<Value>(&obj) - 1] = CHIRFormat::ValueElem_StringLiteral;
            return Serialize<CHIRFormat::StringLiteral>(static_cast<const StringLiteral&>(obj)).Union();
        case ConstantValueKind::KIND_UNIT:
            valueKind[GetId<Value>(&obj) - 1] = CHIRFormat::ValueElem_UnitLiteral;
            return Serialize<CHIRFormat::UnitLiteral>(static_cast<const UnitLiteral&>(obj)).Union();
        case ConstantValueKind::KIND_NULL:
            valueKind[GetId<Value>(&obj) - 1] = CHIRFormat::ValueElem_NullLiteral;
            return Serialize<CHIRFormat::NullLiteral>(static_cast<const NullLiteral&>(obj)).Union();
        case ConstantValueKind::KIND_FUNC:
            return 0;
        default:
            CJC_ABORT();
            return 0;
    }
}

template <> flatbuffers::Offset<void> CHIRSerializer::CHIRSerializerImpl::Dispatch(const Value& obj)
{
    switch (obj.GetValueKind()) {
        case Value::ValueKind::KIND_LITERAL:
            return Dispatch<LiteralValue>(static_cast<const LiteralValue&>(obj)).Union();
        case Value::ValueKind::KIND_GLOBALVAR:
            valueKind[GetId<Value>(&obj) - 1] = CHIRFormat::ValueElem_GlobalVar;
            return Serialize<CHIRFormat::GlobalVar>(dynamic_cast<const GlobalVar&>(obj)).Union();
        case Value::ValueKind::KIND_PARAMETER:
            valueKind[GetId<Value>(&obj) - 1] = CHIRFormat::ValueElem_Parameter;
            return Serialize<CHIRFormat::Parameter>(static_cast<const Parameter&>(obj)).Union();
        case Value::ValueKind::KIND_LOCALVAR:
            valueKind[GetId<Value>(&obj) - 1] = CHIRFormat::ValueElem_LocalVar;
            return Serialize<CHIRFormat::LocalVar>(static_cast<const LocalVar&>(obj)).Union();
        case Value::ValueKind::KIND_FUNC:
            valueKind[GetId<Value>(&obj) - 1] = CHIRFormat::ValueElem_Function;
            return Serialize<CHIRFormat::Function>(dynamic_cast<const Function&>(obj)).Union();
        case Value::ValueKind::KIND_BLOCK:
            valueKind[GetId<Value>(&obj) - 1] = CHIRFormat::ValueElem_Block;
            return Serialize<CHIRFormat::Block>(static_cast<const Block&>(obj)).Union();
        case Value::ValueKind::KIND_BLOCK_GROUP:
            valueKind[GetId<Value>(&obj) - 1] = CHIRFormat::ValueElem_BlockGroup;
            return Serialize<CHIRFormat::BlockGroup>(static_cast<const BlockGroup&>(obj)).Union();
        default:
            CJC_ABORT();
            return 0;
    }
}

template <> flatbuffers::Offset<void> CHIRSerializer::CHIRSerializerImpl::Dispatch(const Expression& obj)
{
    switch (obj.GetExprKind()) {
        case ExprKind::ALLOCATE:
        case ExprKind::TRY_ALLOCATE:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_AllocateBase;
            return Serialize<CHIRFormat::AllocateBase>(StaticCast<const AllocateBase&>(obj)).Union();
        case ExprKind::APPLY:
        case ExprKind::TRY_APPLY:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_ApplyBase;
            return Serialize<CHIRFormat::ApplyBase>(StaticCast<const ApplyBase&>(obj)).Union();
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
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_BinaryExpressionBase;
            return Serialize<CHIRFormat::BinaryExpressionBase>(
                StaticCast<const BinaryExpressionBase&>(obj)).Union();
        case ExprKind::BRANCH:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_Branch;
            return Serialize<CHIRFormat::Branch>(static_cast<const Branch&>(obj)).Union();
        case ExprKind::DEBUGEXPR:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_Debug;
            return Serialize<CHIRFormat::Debug>(static_cast<const Debug&>(obj)).Union();
        case ExprKind::FIELD:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_Field;
            return Serialize<CHIRFormat::Field>(static_cast<const Field&>(obj)).Union();
        case ExprKind::FIELD_BY_NAME:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_FieldByName;
            return Serialize<CHIRFormat::FieldByName>(static_cast<const FieldByName&>(obj)).Union();
        case ExprKind::GET_ELEMENT_BY_NAME:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_GetElementByName;
            return Serialize<CHIRFormat::GetElementByName>(static_cast<const GetElementByName&>(obj)).Union();
        case ExprKind::GET_ELEMENT_REF:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_GetElementRef;
            return Serialize<CHIRFormat::GetElementRef>(static_cast<const GetElementRef&>(obj)).Union();
        case ExprKind::GET_INSTANTIATE_VALUE:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_GetInstantiateValue;
            return Serialize<CHIRFormat::GetInstantiateValue>(static_cast<const GetInstantiateValue&>(obj)).Union();
        case ExprKind::GET_RTTI_STATIC:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_GetRTTIStatic;
            return Serialize<CHIRFormat::GetRTTIStatic>(static_cast<const GetRTTIStatic&>(obj)).Union();
        case ExprKind::INSTANCEOF:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_InstanceOf;
            return Serialize<CHIRFormat::InstanceOf>(static_cast<const InstanceOf&>(obj)).Union();
        case ExprKind::TRY_NEG:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_UnaryExpressionBase;
            return Serialize<CHIRFormat::UnaryExpressionBase>(
                StaticCast<const UnaryExpressionBase&>(obj)).Union();
        case ExprKind::TRY_ADD:
        case ExprKind::TRY_SUB:
        case ExprKind::TRY_MUL:
        case ExprKind::TRY_DIV:
        case ExprKind::TRY_MOD:
        case ExprKind::TRY_EXP:
        case ExprKind::TRY_LSHIFT:
        case ExprKind::TRY_RSHIFT:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_BinaryExpressionBase;
            return Serialize<CHIRFormat::BinaryExpressionBase>(
                StaticCast<const BinaryExpressionBase&>(obj)).Union();
        case ExprKind::INTRINSIC:
        case ExprKind::TRY_INTRINSIC:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_IntrinsicBase;
            return Serialize<CHIRFormat::IntrinsicBase>(StaticCast<const IntrinsicBase&>(obj)).Union();
        case ExprKind::INVOKE:
        case ExprKind::TRY_INVOKE:
        case ExprKind::INVOKESTATIC:
        case ExprKind::TRY_INVOKESTATIC:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_InvokeBase;
            return Serialize<CHIRFormat::InvokeBase>(StaticCast<const DynamicDispatch&>(obj)).Union();
        case ExprKind::LAMBDA:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_Lambda;
            return Serialize<CHIRFormat::Lambda>(static_cast<const Lambda&>(obj)).Union();
        case ExprKind::MULTIBRANCH:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_MultiBranch;
            return Serialize<CHIRFormat::MultiBranch>(static_cast<const MultiBranch&>(obj)).Union();
        case ExprKind::CLASS_STATIC_CAST:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_Expression;
            return Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj)).Union();
        case ExprKind::NUMERIC_CAST:
        case ExprKind::TRY_NUMERIC_CAST:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_NumericCastBase;
            return Serialize<CHIRFormat::NumericCastBase>(StaticCast<const NumericCastBase&>(obj)).Union();
        case ExprKind::RAW_ARRAY_ALLOCATE:
        case ExprKind::TRY_RAW_ARRAY_ALLOCATE:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_RawArrayAllocateBase;
            return Serialize<CHIRFormat::RawArrayAllocateBase>(StaticCast<const RawArrayAllocateBase&>(obj)).Union();
        case ExprKind::SPAWN:
        case ExprKind::TRY_SPAWN:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_SpawnBase;
            return Serialize<CHIRFormat::SpawnBase>(StaticCast<const SpawnBase&>(obj)).Union();
        case ExprKind::STORE_ELEMENT_BY_NAME:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_StoreElementByName;
            return Serialize<CHIRFormat::StoreElementByName>(static_cast<const StoreElementByName&>(obj)).Union();
        case ExprKind::STORE_ELEMENT_REF:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_StoreElementRef;
            return Serialize<CHIRFormat::StoreElementRef>(static_cast<const StoreElementRef&>(obj)).Union();
        case ExprKind::NEG:
        case ExprKind::NOT:
        case ExprKind::BITNOT:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_UnaryExpressionBase;
            return Serialize<CHIRFormat::UnaryExpressionBase>(
                StaticCast<const UnaryExpressionBase&>(obj)).Union();
        case ExprKind::GOTO:
        case ExprKind::EXIT:
        case ExprKind::RAISE_EXCEPTION:
        case ExprKind::LOAD:
        case ExprKind::STORE:
        case ExprKind::CONSTANT:
        case ExprKind::FORIN_RANGE:
        case ExprKind::FORIN_ITER:
        case ExprKind::FORIN_CLOSED_RANGE:
        case ExprKind::TUPLE:
        case ExprKind::BOX:
        case ExprKind::UNBOX_TO_VALUE:
        case ExprKind::GET_EXCEPTION:
        case ExprKind::RAW_ARRAY_LITERAL_INIT:
        case ExprKind::RAW_ARRAY_INIT_BY_VALUE:
        case ExprKind::VARRAY:
        case ExprKind::VARRAY_BUILDER:
        case ExprKind::CAST_TO_GENERIC:
        case ExprKind::CAST_TO_CONCRETE:
        case ExprKind::UNBOX_TO_REF:
        case ExprKind::GET_RTTI:
            exprKind[GetId<Expression>(&obj) - 1] = CHIRFormat::ExpressionElem_Expression;
            return Serialize<CHIRFormat::Expression>(static_cast<const Expression&>(obj)).Union();
        case ExprKind::INVALID:
        case ExprKind::MAX_EXPR_KINDS:
        default:
            CJC_ABORT();
            return 0;
    }
}

template <> flatbuffers::Offset<void> CHIRSerializer::CHIRSerializerImpl::Dispatch(const CustomTypeDef& obj)
{
    switch (obj.GetCustomKind()) {
        case CustomDefKind::TYPE_STRUCT:
            defKind[GetId<CustomTypeDef>(&obj) - 1] = CHIRFormat::CustomTypeDefElem_StructDef;
            return Serialize<CHIRFormat::StructDef>(static_cast<const StructDef&>(obj)).Union();
        case CustomDefKind::TYPE_ENUM:
            defKind[GetId<CustomTypeDef>(&obj) - 1] = CHIRFormat::CustomTypeDefElem_EnumDef;
            return Serialize<CHIRFormat::EnumDef>(static_cast<const EnumDef&>(obj)).Union();
        case CustomDefKind::TYPE_CLASS:
            defKind[GetId<CustomTypeDef>(&obj) - 1] = CHIRFormat::CustomTypeDefElem_ClassDef;
            return Serialize<CHIRFormat::ClassDef>(static_cast<const ClassDef&>(obj)).Union();
        case CustomDefKind::TYPE_EXTEND:
            defKind[GetId<CustomTypeDef>(&obj) - 1] = CHIRFormat::CustomTypeDefElem_ExtendDef;
            return Serialize<CHIRFormat::ExtendDef>(static_cast<const ExtendDef&>(obj)).Union();
        default:
            CJC_ABORT();
            return 0;
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
    auto serializedPackage = CHIRFormat::CreateCHIRPackageDirect(builder, packageName.c_str(), "",
        CHIRFormat::PackageAccessLevel(accesslevel), &typeKind, &allType, &valueKind, &allValue, &exprKind,
        &allExpression, &defKind, &allCustomTypeDef, packageInitFunc, CHIRFormat::Phase(phase),
        packageLiteralInitFunc);

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
    for (auto value : package.GetGlobalVars()) {
        valueQueue.push_back(value);
    }
    for (auto value : package.GetGlobalFunctions()) {
        valueQueue.push_back(value);
    }
    for (auto def : package.GetAllCustomTypeDef()) {
        defQueue.push_back(def);
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

flatbuffers::DetachedBuffer CHIRSerializer::CHIRSerializerImpl::ConvertToMemoryData()
{
    auto accesslevel = package.GetPackageAccessLevel();
    auto packageName = package.GetName();
    auto serializedPackage = CHIRFormat::CreateCHIRPackageDirect(builder, packageName.c_str(), "",
        CHIRFormat::PackageAccessLevel(accesslevel), &typeKind, &allType, &valueKind, &allValue, &exprKind,
        &allExpression, &defKind, &allCustomTypeDef, packageInitFunc, CHIRFormat::Phase::Phase_RAW,
        packageLiteralInitFunc);

    builder.Finish(serializedPackage);
    return builder.Release();
}