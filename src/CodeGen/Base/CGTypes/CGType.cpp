// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/CGTypes/CGType.h"

#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"

#include "Base/CGTypes/CGBoxType.h"
#include "Base/CGTypes/CGCPointerType.h"
#include "Base/CGTypes/CGCStringType.h"
#include "Base/CGTypes/CGClassType.h"
#include "Base/CGTypes/CGEnumType.h"
#include "Base/CGTypes/CGFunctionType.h"
#include "Base/CGTypes/CGGenericType.h"
#include "Base/CGTypes/CGPrimitiveType.h"
#include "Base/CGTypes/CGRefType.h"
#include "Base/CGTypes/CGStructType.h"
#include "Base/CGTypes/CGThisType.h"
#include "Base/CGTypes/CGTupleType.h"
#include "Base/CGTypes/CGVArrayType.h"
#include "CGContext.h"
#include "CGContextImpl.h"
#include "CGModule.h"
#include "CJNative/CGTypes/CGExtensionDef.h"
#include "IRBuilder.h"
#include "Utils/CGCommonDef.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Annotation.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/Mangle/CHIRTypeManglingUtils.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie {
namespace CodeGen {
namespace {
bool HasGenericTypeArg(const CHIR::Type& type)
{
    if (type.IsGeneric() || type.IsThis()) {
        return true;
    }
    for (auto ga : type.GetTypeArgs()) {
        if (HasGenericTypeArg(*ga)) {
            return true;
        }
    }
    return false;
}

bool CanMallocUseFixedSize(const CHIR::Type& chirType)
{
    if (!chirType.IsClass()) {
        return true;
    }
    auto classDef = StaticCast<CHIR::ClassType>(chirType).GetClassDef();
    return classDef->TestAttr(CHIR::Attribute::VIRTUAL) || !classDef->TestAttr(CHIR::Attribute::PUBLIC);
}
} // namespace

CGType* CGTypeMgr::GetConcreteCGTypeFor(CGModule& cgMod, const CHIR::Type& chirType,
    const CGType::TypeExtraInfo& extraInfo, const CHIR::FuncBase* chirFunc)
{
    CGType* cgType = nullptr;
    auto& cgCtx = cgMod.GetCGContext();
    switch (chirType.GetTypeKind()) {
        case CHIR::Type::TypeKind::TYPE_INT8:
        case CHIR::Type::TypeKind::TYPE_UINT8:
        case CHIR::Type::TypeKind::TYPE_INT16:
        case CHIR::Type::TypeKind::TYPE_UINT16:
        case CHIR::Type::TypeKind::TYPE_INT32:
        case CHIR::Type::TypeKind::TYPE_UINT32:
        case CHIR::Type::TypeKind::TYPE_INT64:
        case CHIR::Type::TypeKind::TYPE_UINT64:
        case CHIR::Type::TypeKind::TYPE_INT_NATIVE:
        case CHIR::Type::TypeKind::TYPE_UINT_NATIVE:
        case CHIR::Type::TypeKind::TYPE_FLOAT16:
        case CHIR::Type::TypeKind::TYPE_FLOAT32:
        case CHIR::Type::TypeKind::TYPE_FLOAT64:
        case CHIR::Type::TypeKind::TYPE_RUNE:
        case CHIR::Type::TypeKind::TYPE_BOOLEAN:
        case CHIR::Type::TypeKind::TYPE_UNIT:
        case CHIR::Type::TypeKind::TYPE_NOTHING:
        case CHIR::Type::TypeKind::TYPE_VOID: {
            cgType = new CGPrimitiveType(cgMod, cgCtx, chirType);
            break;
        }

        case Cangjie::CHIR::Type::TypeKind::TYPE_CSTRING: {
            cgType = new CGCStringType(cgMod, cgCtx, chirType);
            break;
        }
        case CHIR::Type::TypeKind::TYPE_TUPLE: {
            cgType = new CGTupleType(cgMod, cgCtx, chirType, extraInfo);
            break;
        }
        case CHIR::Type::TypeKind::TYPE_STRUCT: {
            cgType = new CGStructType(cgMod, cgCtx, chirType);
            break;
        }
        case CHIR::Type::TypeKind::TYPE_ENUM: {
            cgType = new CGEnumType(cgMod, cgCtx, chirType);
            break;
        }
        case CHIR::Type::TypeKind::TYPE_FUNC: {
            cgType = chirFunc
                ? new CGFunctionType(cgMod, cgCtx, *chirFunc, extraInfo)
                : new CGFunctionType(cgMod, cgCtx, StaticCast<const CHIR::FuncType&>(chirType), extraInfo);
            break;
        }
        case CHIR::Type::TypeKind::TYPE_CLASS: {
            cgType = new CGClassType(cgMod, cgCtx, StaticCast<const CHIR::ClassType&>(chirType));
            break;
        }
        case CHIR::Type::TypeKind::TYPE_VARRAY: {
            cgType = new CGVArrayType(cgMod, cgCtx, StaticCast<const CHIR::VArrayType&>(chirType));
            break;
        }
        case CHIR::Type::TypeKind::TYPE_RAWARRAY: {
            cgType = new CGArrayType(cgMod, cgCtx, StaticCast<const CHIR::RawArrayType&>(chirType));
            break;
        }
        case CHIR::Type::TypeKind::TYPE_REFTYPE: {
            cgType = new CGRefType(cgMod, cgCtx, chirType, static_cast<unsigned>(extraInfo.addrspace));
            break;
        }
        case CHIR::Type::TypeKind::TYPE_BOXTYPE: {
            cgType = new CGBoxType(cgMod, cgCtx, StaticCast<const CHIR::BoxType&>(chirType));
            break;
        }
        case CHIR::Type::TypeKind::TYPE_CPOINTER: {
            cgType = new CGCPointerType(cgMod, cgCtx, StaticCast<const CHIR::CPointerType&>(chirType));
            break;
        }
        case CHIR::Type::TypeKind::TYPE_GENERIC: {
            cgType = new CGGenericType(cgMod, cgCtx, StaticCast<const CHIR::GenericType&>(chirType));
            break;
        }
        case CHIR::Type::TypeKind::TYPE_THIS: {
            cgType = new CGThisType(cgMod, cgCtx, StaticCast<const CHIR::ThisType&>(chirType));
            break;
        }
        default:
            CJC_ASSERT(false && "Should not reach here: not a contract type from chir.");
            return nullptr;
    }
    cgMod.GetCGContext().Add2CGTypePool(cgType);
    return cgType;
}

CGType* CGType::GetCGTI(CGModule& cgModule)
{
    return CGTIType::Get(cgModule);
}

CGType* CGType::GetOrCreate(CGModule& cgModule, const Cangjie::CHIR::Type* chirTy, const TypeExtraInfo& extraInfo)
{
    CJC_NULLPTR_CHECK(chirTy);
    if (chirTy->IsAutoEnvInstBase()) {
        chirTy = const_cast<CHIR::ClassType*>(dynamic_cast<const CHIR::ClassType*>(chirTy))
                       ->GetSuperClassTy(&cgModule.GetCGContext().GetCHIRBuilder());
        CJC_ASSERT(chirTy->IsAutoEnvGenericBase());
    }
    CGContext& cgContext = cgModule.GetCGContext();
    auto& cache = cgContext.impl->chirType2CGTypeMap[extraInfo];
    if (auto it = cache.find(chirTy); it != cache.end() && !IsLitStructPtrType(it->second->llvmType)) {
        return it->second;
    }
    /// This is a defense mechanism to report a memory management problem on CHIR.
    /// Release mode: this problem on CHIR will be covered by codegen, the program won't behave incorrectly.
    /// Debug mode: the program will abort and an error will be reported.
    auto& secondCache = cgContext.impl->chirTypeName2CGTypeMap;
    if (auto it = secondCache.find(chirTy->ToString());
        it != secondCache.end() && !IsLitStructPtrType(it->second->llvmType)) {
        auto [iter, success] = cache.emplace(chirTy, it->second);
        CJC_ASSERT(success);
        CJC_ASSERT(false && "It must be a bug on CHIR: an AST type corresponds to different memory address");
        return it->second;
    }
    // This `cgType` will be released in the de-constructor of `CGContextImpl`.
    CGType* cgType = CGTypeMgr::GetConcreteCGTypeFor(cgModule, *chirTy, extraInfo);
    // Add the incomplete `cgType` to prevent infinite recursion into this program point.
    auto [iter, success] = cache.emplace(chirTy, cgType);
    if (!success) {
        cache[chirTy] = cgType;
        secondCache[chirTy->ToString()] = cgType;
    }
    // Complete the `cgType`.
    cgType->llvmType = cgType->GenLLVMType();
    cgType->CalculateSizeAndAlign();
    return cgType;
}
CGType* CGType::GetOrCreateWithNode(CGModule& cgModule, const CHIR::Value* chirNode, bool allowBasePtr,
    bool forWrapper)
{
    bool flag = chirNode->IsFunc() && chirNode->Get<CHIR::OverrideSrcFuncType>();
    auto chirTy = flag ? chirNode->Get<CHIR::OverrideSrcFuncType>() : chirNode->GetType();
    CGType* cgType = nullptr;
    if (DynamicCast<const CHIR::LocalVar*>(chirNode)) {
        cgType = CGTypeMgr::GetConcreteCGTypeFor(cgModule, *chirTy, TypeExtraInfo{0, true, false, false, {}});
    } else if (auto fb = DynamicCast<const CHIR::FuncBase*>(chirNode)) {
        TypeExtraInfo typeExtraInfo(0, false, false, allowBasePtr, {});
        typeExtraInfo.forWrapper = forWrapper;
        for (auto gt : fb->GetGenericTypeParams()) {
            typeExtraInfo.instantiatedParamTypes.emplace_back(gt);
        }
        cgType = CGTypeMgr::GetConcreteCGTypeFor(cgModule, *chirTy, typeExtraInfo, fb);
    } else {
        return GetOrCreate(cgModule, chirTy);
    }
    // Complete the `cgType`.
    cgType->llvmType = cgType->GenLLVMType();
    cgType->GenContainedCGTypes();
    return cgType;
}

CGType::CGGenericKind CGType::GetCGGenericKind(const CHIR::Type& chirType)
{
    bool hasGenericTypeArg = HasGenericTypeArg(chirType);
    if (hasGenericTypeArg) {
        return CGGenericKind::DYNAMIC_GI;
    } else if (chirType.GetTypeArgs().empty()) {
        if (auto ct = dynamic_cast<const CHIR::CustomType*>(&chirType); ct && !ct->GetGenericArgs().empty()) {
            CJC_ASSERT(ct->GetCustomTypeDef()->TestAttr(CHIR::Attribute::GENERIC_INSTANTIATED));
            return CGGenericKind::STATIC_GI;
        }
        return CGGenericKind::CONCRETE;
    } else {
        return CGGenericKind::STATIC_GI;
    }
}

CGType::CGType(CGModule& cgMod, CGContext& cgCtx, const CHIR::Type& chirType, CGTypeKind cgTypeKind)
    : cgMod(cgMod), cgCtx(cgCtx), chirType(chirType), cgTypeKind(cgTypeKind)
{
    if (chirType.IsClass() && !chirType.IsAutoEnv()) {
        inheritedClassNum =
            cgCtx.GetVTableSizeOf(const_cast<CHIR::ClassType*>(&static_cast<const CHIR::ClassType&>(chirType)));
    } else {
        inheritedClassNum = 0U;
    }
    cgGenericKind = CGType::GetCGGenericKind(chirType);
}

llvm::GlobalVariable* CGType::GetOrCreateTypeInfo()
{
    CJC_ASSERT(!chirType.IsThis() && "Should not get typeinfo of ThisType.");
    CJC_ASSERT(!chirType.IsRef() && "Should not get typeinfo of RefType.");
    if (typeInfo) {
        return typeInfo;
    }
    const auto typeInfoType = CGType::GetOrCreateTypeInfoType(cgMod.GetLLVMContext());
    auto tiName = CGType::GetNameOfTypeInfoGV(chirType);
    if (cgGenericKind == CGGenericKind::STATIC_GI) {
        if (!cgMod.GetLLVMModule()->getNamedGlobal(tiName)) {
            cgCtx.RegisterStaticGIName(tiName);
        }
        typeInfo = llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(tiName, typeInfoType));
    } else if (cgGenericKind == CGGenericKind::CONCRETE) {
        typeInfo = llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(tiName, typeInfoType));
    } else if (cgGenericKind == CGGenericKind::DYNAMIC_GI) {
        return GetOrCreateGenericCustomTypeInfo();
    }
    cgMod.GetCGContext().AddCodeGenAddedFuncsOrVars(chirType, tiName);
    CJC_ASSERT(typeInfo && "This type does not have a typeinfo GV, please check the caller.");
    typeInfo->addAttribute(GC_KLASS_ATTR);
    if (CanMallocUseFixedSize(chirType)) {
        typeInfo->addAttribute(GC_CAN_MALLOC_WITH_FIXED_SIZE);
    }
    if (!typeInfo->hasInitializer()) {
        cgMod.DelayGenTypeInfo(this);
    }
    return typeInfo;
}

llvm::GlobalVariable* CGType::GetOrCreateTypeTemplate()
{
    if (typeTemplate) {
        return typeTemplate;
    }
    bool needTT = chirType.IsTuple() || chirType.IsVArray() || chirType.IsRawArray() || chirType.IsCPointer() ||
        chirType.IsBox() || chirType.IsCFunc() || chirType.IsFunc();
    if (auto ct = dynamic_cast<const CHIR::CustomType*>(&chirType)) {
        needTT = needTT || !ct->GetGenericArgs().empty();
    }
    if (needTT) {
        auto typeTemplateType = CGType::GetOrCreateTypeTemplateType(cgMod.GetLLVMContext());
        auto ttName = CGType::GetNameOfTypeTemplateGV(chirType);
        typeTemplate =
            llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(ttName, typeTemplateType));
        cgMod.GetCGContext().AddCodeGenAddedFuncsOrVars(chirType, ttName);
    }
    CJC_ASSERT(typeTemplate && "This type does not have a type-template GV, please check the caller.");
    cgMod.DelayGenTypeTemplate(this);
    return typeTemplate;
}

CGType* CGType::GetObjectCGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetObjectTy());
}

CGType* CGType::GetInt8CGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetInt8Ty());
}

CGType* CGType::GetInt8PtrCGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, GetRefTypeOfCHIRInt8(cgMod.GetCGContext().GetCHIRBuilder()));
}

CGType* CGType::GetInt16CGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetInt16Ty());
}

CGType* CGType::GetInt32CGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetInt32Ty());
}

CGType* CGType::GetInt64CGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetInt64Ty());
}

CGType* CGType::GetUInt8CGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetUInt8Ty());
}

CGType* CGType::GetUInt16CGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetUInt16Ty());
}

CGType* CGType::GetUInt32CGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetUInt32Ty());
}

CGType* CGType::GetUInt64CGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetUInt64Ty());
}

CGType* CGType::GetFloat16CGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetFloat16Ty());
}

CGType* CGType::GetFloat32CGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetFloat32Ty());
}

CGType* CGType::GetFloat64CGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetFloat64Ty());
}

CGType* CGType::GetUnitCGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetUnitTy());
}

CGType* CGType::GetNothingCGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetNothingType());
}

CGType* CGType::GetZeroSizedCGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetZeroSizedTy());
}

CGType* CGType::GetBoolCGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetBoolTy());
}

CGType* CGType::GetCStringCGType(CGModule& cgMod)
{
    return CGType::GetOrCreate(cgMod, cgMod.GetCGContext().GetCHIRBuilder().GetCStringTy());
}

llvm::Type* CGType::GetUnitType(llvm::LLVMContext& llvmCtx)
{
    auto unitType = llvm::StructType::getTypeByName(llvmCtx, UNIT_TYPE_STR);
    if (!unitType) {
        unitType = llvm::StructType::create(llvmCtx, {}, UNIT_TYPE_STR);
    }
    return unitType;
}

llvm::Type* CGType::GetRefType(llvm::LLVMContext& llvmCtx)
{
    return llvm::Type::getInt8PtrTy(llvmCtx, 1U);
}

llvm::Type* CGType::GetLandingPadType(llvm::LLVMContext& llvmCtx)
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    return llvm::Type::getTokenTy(llvmCtx);
#endif
}

llvm::Type* CGType::GetArrayBaseType(CGContext& cgCtx)
{
    auto& llvmCtx = cgCtx.GetLLVMContext();
    auto arrBaseType = llvm::StructType::getTypeByName(llvmCtx, "ArrayBase");
    if (arrBaseType == nullptr) {
        arrBaseType = llvm::StructType::create(llvmCtx, "ArrayBase");
        std::vector<llvm::Type*> bodyVec(static_cast<size_t>(ARRAY_BASE_FIELDS_NUM));
        bodyVec[static_cast<size_t>(ARRAY_BASE_ARRAY_LEN)] = llvm::Type::getInt64Ty(llvmCtx);
        SetStructTypeBody(arrBaseType, bodyVec);
    }
    return arrBaseType;
}

llvm::Type* CGType::GetBitMapType(llvm::LLVMContext& llvmCtx)
{
    auto bitMapType = llvm::StructType::getTypeByName(llvmCtx, "BitMap");
    if (bitMapType == nullptr) {
        bitMapType = llvm::StructType::create(llvmCtx, "BitMap");
        std::vector<llvm::Type*> bodyVec(static_cast<size_t>(BITMAP_FIELDS_NUM));
        bodyVec[static_cast<size_t>(BITMAP_NUMS)] = llvm::Type::getInt32Ty(llvmCtx);
        bodyVec[static_cast<size_t>(BITMAP_BITMAP)] = llvm::ArrayType::get(llvm::Type::getInt8Ty(llvmCtx), 0);
        SetStructTypeBody(bitMapType, bodyVec);
    }
    return bitMapType;
}

llvm::FunctionType* CGType::GetCodeGenFunctionType(
    llvm::LLVMContext& llvmCtx, llvm::Type* retType, std::vector<llvm::Type*>& params)
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (IsPassedByReference(retType)) {
        params.insert(params.begin(), retType->getPointerTo());
        return llvm::FunctionType::get(llvm::Type::getVoidTy(llvmCtx), params, false);
    }
#endif
    return llvm::FunctionType::get(retType, params, false);
}

// ObjLayout.EnumCase<associatedTypes> = type { i32, ... }
// ObjLayout.OptionLike<> = type { i1 }
llvm::StructType* CGType::GetOrCreateEnumCtorLayoutType(CGModule& cgModule, const CHIR::EnumType& enumTy,
    std::size_t ctorIndex, const std::vector<CHIR::Type*>& paramTypes)
{
    auto className = enumTy.GetEnumDef()->GetPackageName() + ':' + GetCustomTypeDefIdentifier(*enumTy.GetEnumDef()) +
        ':' + std::to_string(ctorIndex);
    if (!enumTy.GetTypeArgs().empty()) {
        className = className + '<' + GetTypeArgsQualifiedName(enumTy.GetTypeArgs()) + '>';
    }
    auto enumCtorLayoutType =
        llvm::StructType::getTypeByName(cgModule.GetLLVMContext(), GetClassObjLayoutName(className));
    if (enumCtorLayoutType) {
        return enumCtorLayoutType;
    }
    // insert size of constructor at the beginning
    std::vector<CHIR::Type*> fieldsTypes;
    StaticCast<CGEnumType*>(CGType::GetOrCreate(cgModule, &enumTy))->IsOptionLike()
        ? fieldsTypes.emplace_back(cgModule.GetCGContext().GetCHIRBuilder().GetBoolTy())
        : fieldsTypes.emplace_back(cgModule.GetCGContext().GetCHIRBuilder().GetInt32Ty());
    fieldsTypes.insert(fieldsTypes.end(), paramTypes.cbegin(), paramTypes.cend());
    return GetLLVMStructType(cgModule, fieldsTypes, GetClassObjLayoutName(className));
}

CHIR::RefType* CGType::GetRefTypeOf(CHIR::CHIRBuilder& chirBuilder, const CHIR::Type& typeOfCHIR)
{
    return chirBuilder.GetType<CHIR::RefType>(const_cast<CHIR::Type*>(&typeOfCHIR));
}

CHIR::RefType* CGType::GetRefTypeOfCHIRInt8(CHIR::CHIRBuilder& chirBuilder)
{
    return GetRefTypeOf(chirBuilder, *chirBuilder.GetInt8Ty());
}

llvm::StructType* CGType::GetOrCreateTypeInfoType(llvm::LLVMContext& llvmCtx)
{
    auto typeInfoType = llvm::StructType::getTypeByName(llvmCtx, "TypeInfo");
    if (typeInfoType == nullptr) {
        auto i8Ty = llvm::Type::getInt8Ty(llvmCtx);
        auto i16Ty = llvm::Type::getInt16Ty(llvmCtx);
        auto i32Ty = llvm::Type::getInt32Ty(llvmCtx);
        auto i8PtrTy = llvm::Type::getInt8PtrTy(llvmCtx);

        typeInfoType = llvm::StructType::create(llvmCtx, "TypeInfo");
        std::vector<llvm::Type*> bodyVec(static_cast<size_t>(TYPE_INFO_FIELDS_NUM));
        bodyVec[static_cast<size_t>(TYPEINFO_NAME)] = i8PtrTy;
        bodyVec[static_cast<size_t>(TYPEINFO_TYPE_KIND)] = i8Ty;
        bodyVec[static_cast<size_t>(TYPEINFO_FIELDS_NUM)] = i16Ty;
        bodyVec[static_cast<size_t>(TYPEINFO_FIELDS)] = i8PtrTy;
        bodyVec[static_cast<size_t>(TYPEINFO_SIZE)] = i32Ty;
        bodyVec[static_cast<size_t>(TYPEINFO_UUID)] = i32Ty;
        bodyVec[static_cast<size_t>(TYPEINFO_ALIGN)] = i8Ty;
        bodyVec[static_cast<size_t>(TYPEINFO_SOURCE_GENERIC)] = i8PtrTy;
        bodyVec[static_cast<size_t>(TYPEINFO_TYPE_ARGS_NUM)] = i8Ty;
        bodyVec[static_cast<size_t>(TYPEINFO_INHERITED_CLASS_NUM)] = i16Ty;
        bodyVec[static_cast<size_t>(TYPEINFO_OFFSETS)] = i32Ty->getPointerTo();
        bodyVec[static_cast<size_t>(TYPEINFO_TYPE_ARGS)] = i8PtrTy;
        bodyVec[static_cast<size_t>(TYPEINFO_SUPER)] = typeInfoType->getPointerTo();
        bodyVec[static_cast<size_t>(TYPEINFO_EXTENSIONDEF_PTR)] =
            CGType::GetOrCreateExtensionDefPtrType(llvmCtx)->getPointerTo();
        bodyVec[static_cast<size_t>(TYPEINFO_MTABLE)] = i8PtrTy;
        bodyVec[static_cast<size_t>(TYPEINFO_REFLECTION)] = i8PtrTy;
        bodyVec[static_cast<size_t>(TYPEINFO_GC_TIB)] = CGType::GetBitMapType(llvmCtx)->getPointerTo();
        bodyVec[static_cast<size_t>(TYPEINFO_FLAG)] = i8Ty;
        SetStructTypeBody(typeInfoType, bodyVec);
    }
    return typeInfoType;
}

llvm::PointerType* CGType::GetOrCreateTypeInfoPtrType(llvm::LLVMContext& llvmCtx)
{
    return CGType::GetOrCreateTypeInfoType(llvmCtx)->getPointerTo();
}

llvm::StructType* CGType::GetOrCreateTypeTemplateType(llvm::LLVMContext& llvmCtx)
{
    auto typeTemplateType = llvm::StructType::getTypeByName(llvmCtx, "TypeTemplate");
    if (typeTemplateType == nullptr) {
        auto i8Ty = llvm::Type::getInt8Ty(llvmCtx);
        auto i16Ty = llvm::Type::getInt16Ty(llvmCtx);
        auto i8PtrTy = llvm::Type::getInt8PtrTy(llvmCtx);

        typeTemplateType = llvm::StructType::create(llvmCtx, "TypeTemplate");
        std::vector<llvm::Type*> bodyVec(static_cast<size_t>(TYPE_TEMPLATE_FIELDS_NUM));
        bodyVec[static_cast<size_t>(TYPETEMPLATE_NAME)] = i8PtrTy;
        bodyVec[static_cast<size_t>(TYPETEMPLATE_TYPE_KIND)] = i8Ty;
        bodyVec[static_cast<size_t>(TYPETEMPLATE_TYPE_ARGS_NUM)] = i16Ty;
        bodyVec[static_cast<size_t>(TYPETEMPLATE_FIELDS_NUM)] = i16Ty;
        bodyVec[static_cast<size_t>(TYPETEMPLATE_FIELDS_FNS)] = i8PtrTy;
        bodyVec[static_cast<size_t>(TYPETEMPLATE_SUPER_FN)] = i8PtrTy;
        bodyVec[static_cast<size_t>(TYPETEMPLATE_FINALIZER)] = i8PtrTy;
        bodyVec[static_cast<size_t>(TYPETEMPLATE_REFLECTION)] = i8PtrTy;
        bodyVec[static_cast<size_t>(TYPETEMPLATE_FLAG)] = i8Ty;
        bodyVec[static_cast<size_t>(TYPETEMPLATE_EXTENSIONDEF_PTR)] =
            CGType::GetOrCreateExtensionDefPtrType(llvmCtx)->getPointerTo();
        bodyVec[static_cast<size_t>(TYPETEMPLATE_INHERITED_CLASS_NUM)] = i16Ty;
        SetStructTypeBody(typeTemplateType, bodyVec);
    }
    return typeTemplateType;
}

llvm::StructType* CGType::GetOrCreateExtensionDefType(llvm::LLVMContext& llvmCtx)
{
    auto extensionDefType = llvm::StructType::getTypeByName(llvmCtx, "ExtensionDef");
    if (extensionDefType == nullptr) {
        auto i32Ty = llvm::Type::getInt32Ty(llvmCtx);
        auto i8PtrTy = llvm::Type::getInt8PtrTy(llvmCtx);
        extensionDefType = llvm::StructType::create(llvmCtx, "ExtensionDef");
        std::vector<llvm::Type*> bodyVec(static_cast<size_t>(EXTENSION_DEF_FIELDS_NUM));
        bodyVec[static_cast<size_t>(TYPE_PARAM_COUNT)] = i32Ty;
        bodyVec[static_cast<size_t>(IS_INTERFACE_TI)] = llvm::Type::getInt8Ty(llvmCtx);
        bodyVec[static_cast<size_t>(TARGET_TYPE)] = i8PtrTy;
        bodyVec[static_cast<size_t>(INTERFACE_FN_OR_INTERFACE_TI)] = i8PtrTy;
        bodyVec[static_cast<size_t>(FUNC_TABLE)] = i8PtrTy;
        bodyVec[static_cast<size_t>(WHERE_CONDITION_FN)] = i8PtrTy;
        SetStructTypeBody(extensionDefType, bodyVec);
    }
    return extensionDefType;
}

llvm::PointerType* CGType::GetOrCreateExtensionDefPtrType(llvm::LLVMContext& llvmCtx)
{
    return GetOrCreateExtensionDefType(llvmCtx)->getPointerTo();
}

std::string CGType::GetNameOfTypeInfoGV(const CHIR::Type& chirType)
{
    CJC_ASSERT(!chirType.IsRef() && "RefType should not reach here.");
    return GetTypeQualifiedName(chirType) + ".ti";
}

std::string CGType::GetNameOfTypeTemplateGV(const CHIR::Type& chirType)
{
    CJC_ASSERT(!chirType.IsRef() && "RefType should not reach here.");
    if (chirType.IsTuple()) {
        return "Tuple.tt";
    } else if (chirType.IsRawArray()) {
        return "RawArray.tt";
    } else if (chirType.IsVArray()) {
        return "VArray.tt";
    } else if (chirType.IsCPointer()) {
        return "CPointer.tt";
    } else if (auto ct = dynamic_cast<const CHIR::CustomType*>(&chirType)) {
        if (ct->IsAutoEnvBase()) {
            CJC_ASSERT(!ct->IsAutoEnvInstBase());
            return "Closure.tt";
        }
        auto def = ct->GetCustomTypeDef();
        auto packageName = def->GetGenericDecl() ? def->GetGenericDecl()->GetPackageName() : def->GetPackageName();
        auto defShortName = CHIR::GetCustomTypeIdentifier(*ct);
        return packageName + ":" + defShortName + ".tt";
    } else if (chirType.IsCFunc() || chirType.IsFunc()) {
        return "CFunc.tt";
    } else if (chirType.IsBox()) {
        return "Box.tt";
    }
    CJC_ASSERT(false && "Unsupported");
    return "Unsupported";
}

void CGType::GenTypeInfo()
{
    CJC_ASSERT(!IsDynamicGI() && "Dynamic type should not reach here.");
    CJC_ASSERT(typeInfo);

    PreActionOfGenTypeInfo();

    auto& llvmCtx = cgMod.GetLLVMContext();
    if (layoutType && !typeInfo->hasMetadata(GC_TYPE_META_NAME)) {
        auto structName = layoutType->getStructName();
        auto meta = llvm::MDTuple::get(llvmCtx, {llvm::MDString::get(llvmCtx, structName)});
        typeInfo->setMetadata(GC_TYPE_META_NAME, meta);
        cgCtx.AddGeneratedStructType(structName.str());
    }

    if (typeInfo->hasInitializer()) {
        return;
    }

    if (chirType.IsPrimitive() || chirType.IsVoid() || chirType.IsCString()) {
        return;
    }

    if (IsConcrete() && cgMod.GetCGContext().IsCustomTypeOfOtherLLVMModule(chirType)) {
        auto customType = dynamic_cast<const CHIR::CustomType*>(&chirType);
        if (customType && customType->GetCustomTypeDef()->Get<CHIR::LinkTypeInfo>() == Linkage::EXTERNAL_WEAK) {
            typeInfo->setLinkage(llvm::GlobalValue::ExternalWeakLinkage);
        }
        return;
    }

    std::vector<llvm::Constant*> typeInfoVec(TYPE_INFO_FIELDS_NUM);
    typeInfoVec[static_cast<size_t>(TYPEINFO_NAME)] = GenNameOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_TYPE_KIND)] = GenKindOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_FIELDS_NUM)] = GenFieldsNumOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_FIELDS)] = GenFieldsOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_SIZE)] = GenSizeOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_UUID)] = llvm::Constant::getNullValue(llvm::Type::getInt32Ty(llvmCtx));
    typeInfoVec[static_cast<size_t>(TYPEINFO_ALIGN)] = GenAlignOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_SOURCE_GENERIC)] = GenSourceGenericOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_TYPE_ARGS_NUM)] = GenTypeArgsNumOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_OFFSETS)] = GenOffsetsOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_TYPE_ARGS)] = GenTypeArgsOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_SUPER)] = GenSuperOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_EXTENSIONDEF_PTR)] = GenExtensionDefPtrOfTypeInfo();
    // TYPEINFO_MTABLE must before TYPEINFO_INHERITED_CLASS_NUM
    typeInfoVec[static_cast<size_t>(TYPEINFO_MTABLE)] = GenMTableOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_INHERITED_CLASS_NUM)] = GenInheritedClassNumOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_REFLECTION)] = GenReflectionOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_GC_TIB)] =
        llvm::ConstantPointerNull::get(CGType::GetBitMapType(llvmCtx)->getPointerTo());
    typeInfoVec[static_cast<size_t>(TYPEINFO_FLAG)] = GenFlagOfTypeInfo();

    typeInfo->setInitializer(llvm::ConstantStruct::get(CGType::GetOrCreateTypeInfoType(llvmCtx), typeInfoVec));
    if (IsStaticGI()) {
        typeInfo->setLinkage(llvm::GlobalValue::InternalLinkage);
    } else { // For Concrete type:
        // Note: The chirType that enters this branch is expected to be of CustomType.
        auto customType = dynamic_cast<const CHIR::CustomType*>(&chirType);
        CJC_ASSERT(customType && "The chirType that enters this branch is expected to be of CustomType.");
        auto linkageType = CHIRLinkage2LLVMLinkage(customType->GetCustomTypeDef()->Get<CHIR::LinkTypeInfo>());
        AddLinkageTypeMetadata(*typeInfo, linkageType, cgMod.GetCGContext().IsCGParallelEnabled());
    }
    PostActionOfGenTypeInfo();
}

llvm::Constant* CGType::GenInheritedClassNumOfTypeInfo() const
{
    return llvm::ConstantInt::get(
        llvm::Type::getInt16Ty(cgCtx.GetLLVMContext()), inheritedClassNum | INHERITED_CLASS_NUM_FE_FLAG);
}

llvm::Constant* CGType::GenExtensionDefPtrOfTypeInfo() const
{
    auto& llvmCtx = cgCtx.GetLLVMContext();
    if (typeTemplate && typeTemplate->hasInitializer()) {
        return llvm::cast<llvm::ConstantStruct>(typeTemplate->getInitializer())
            ->getAggregateElement(TYPETEMPLATE_EXTENSIONDEF_PTR);
    }
    if (!cgExtensionDef) {
        return llvm::ConstantPointerNull::get(CGType::GetOrCreateExtensionDefPtrType(llvmCtx)->getPointerTo());
    }
    auto extensionDefs = cgMod.GetLLVMModule()->getNamedGlobal(
        CodeGen::IsExternalDefinedType(chirType) ? "ExternalExtensionDefs" : "NonExternalExtensionDefs");
    llvm::Constant* indices[] = {llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvmCtx), 0),
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvmCtx), cgExtensionDef->GetStartIdx())};
    return llvm::ConstantExpr::getInBoundsGetElementPtr(extensionDefs->getValueType(), extensionDefs, indices);
}

llvm::Constant* CGType::GenMTableOfTypeInfo()
{
    auto& llvmCtx = cgCtx.GetLLVMContext();
    auto targetDefType = chirType.IsNominal()
        ? static_cast<const CHIR::CustomType&>(chirType).GetCustomTypeDef()->GetType()
        : &chirType;
    cgExtensionDef = CGType::GetOrCreate(cgMod, targetDefType)->cgExtensionDef;
    if ((inheritedClassNum >> 14U) != 0 || !cgExtensionDef) {
        return llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(llvmCtx));
    }
    auto& extendInterfaces = cgExtensionDef->GetExtendInterfaces();
    constexpr size_t bitsPerByte = 8U;
    if (cgCtx.GetCompileOptions().target.arch != Triple::ArchType::ARM32 &&
        extendInterfaces.size() < sizeof(void*) * bitsPerByte - 1U) {
        std::string flag(extendInterfaces.size(), '0');
        if (chirType.GetTypeArgs().empty()) {
            flag = std::string(extendInterfaces.size(), '1');
        } else {
            uint8_t idx = 0U;
            bool foundFirstCandidate = false;
            for (auto& it : extendInterfaces) {
                if (it.second == nullptr || chirType.IsEqualOrInstantiatedTypeOf(*it.second, cgCtx.GetCHIRBuilder())) {
                    flag[idx] = '1';
                    foundFirstCandidate = true;
                }
                if (foundFirstCandidate) {
                    ++idx;
                } else {
                    ++inheritedClassNum;
                }
            }
        }
        return cgMod.GetOrInsertBitMap(flag);
    }
    return llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(llvmCtx));
}

llvm::Constant* CGType::GenNameOfTypeInfo()
{
    return cgMod.GenerateTypeNameConstantString(GetTypeQualifiedName(chirType, true), false);
}

llvm::Constant* CGType::GenKindOfTypeInfo()
{
    static const std::unordered_map<CHIR::Type::TypeKind, unsigned> chirType2TypeInfoKind = {
        {CHIR::Type::TypeKind::TYPE_RAWARRAY, UGTypeKind::UG_RAWARRAY},
        {CHIR::Type::TypeKind::TYPE_FUNC, UGTypeKind::UG_CFUNC},
        {CHIR::Type::TypeKind::TYPE_VOID, UGTypeKind::UG_UNIT},
        {CHIR::Type::TypeKind::TYPE_UNIT, UGTypeKind::UG_UNIT},
        {CHIR::Type::TypeKind::TYPE_BOOLEAN, UGTypeKind::UG_BOOLEAN},
        {CHIR::Type::TypeKind::TYPE_RUNE, UGTypeKind::UG_RUNE},
        {CHIR::Type::TypeKind::TYPE_UINT8, UGTypeKind::UG_UINT8},
        {CHIR::Type::TypeKind::TYPE_UINT16, UGTypeKind::UG_UINT16},
        {CHIR::Type::TypeKind::TYPE_UINT32, UGTypeKind::UG_UINT32},
        {CHIR::Type::TypeKind::TYPE_UINT64, UGTypeKind::UG_UINT64},
        {CHIR::Type::TypeKind::TYPE_UINT_NATIVE, UGTypeKind::UG_UINT_NATIVE},
        {CHIR::Type::TypeKind::TYPE_INT8, UGTypeKind::UG_INT8},
        {CHIR::Type::TypeKind::TYPE_INT16, UGTypeKind::UG_INT16},
        {CHIR::Type::TypeKind::TYPE_INT32, UGTypeKind::UG_INT32},
        {CHIR::Type::TypeKind::TYPE_INT64, UGTypeKind::UG_INT64},
        {CHIR::Type::TypeKind::TYPE_INT_NATIVE, UGTypeKind::UG_INT_NATIVE},
        {CHIR::Type::TypeKind::TYPE_FLOAT16, UGTypeKind::UG_FLOAT16},
        {CHIR::Type::TypeKind::TYPE_FLOAT32, UGTypeKind::UG_FLOAT32},
        {CHIR::Type::TypeKind::TYPE_FLOAT64, UGTypeKind::UG_FLOAT64},
        {CHIR::Type::TypeKind::TYPE_NOTHING, UGTypeKind::UG_NOTHING},
        {CHIR::Type::TypeKind::TYPE_TUPLE, UGTypeKind::UG_TUPLE},
        {CHIR::Type::TypeKind::TYPE_STRUCT, UGTypeKind::UG_STRUCT},
        {CHIR::Type::TypeKind::TYPE_ENUM, UGTypeKind::UG_ENUM},
        {CHIR::Type::TypeKind::TYPE_VARRAY, UGTypeKind::UG_VARRAY},
        {CHIR::Type::TypeKind::TYPE_CPOINTER, UGTypeKind::UG_CPOINTER},
        {CHIR::Type::TypeKind::TYPE_CSTRING, UGTypeKind::UG_CSTRING},
        {CHIR::Type::TypeKind::TYPE_BOXTYPE, UGTypeKind::UG_CLASS},
        {CHIR::Type::TypeKind::TYPE_GENERIC, UGTypeKind::UG_GENERIC}};
    CJC_ASSERT(!chirType.IsRef() && "Unexpected CHIR type to generate typeinfo.");
    unsigned typeInfoKind;
    if (chirType.IsAutoEnvBase()) {
        typeInfoKind = UGTypeKind::UG_FUNC;
    } else if (chirType.IsClass()) {
        auto classDef = StaticCast<const CHIR::ClassType&>(chirType).GetClassDef();
        if (IsWeakRefClass(*classDef)) {
            typeInfoKind = UGTypeKind::UG_WEAKREF;
        } else {
            typeInfoKind = classDef->IsClass() ? UGTypeKind::UG_CLASS : UGTypeKind::UG_INTERFACE;
        }
    } else if (chirType.IsEnum()) {
        typeInfoKind =
            static_cast<CGEnumType*>(this)->IsCommonEnum() ? UGTypeKind::UG_COMMON_ENUM : UGTypeKind::UG_ENUM;
    } else {
        typeInfoKind = chirType2TypeInfoKind.at(chirType.GetTypeKind());
    }
    return llvm::ConstantInt::get(llvm::Type::getInt8Ty(cgMod.GetLLVMContext()), typeInfoKind);
}

llvm::Constant* CGType::GenFieldsNumOfTypeInfo()
{
    return llvm::ConstantInt::get(llvm::Type::getInt16Ty(cgMod.GetLLVMContext()), 0);
}

llvm::Constant* CGType::GenFieldsOfTypeInfo()
{
    return llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext()));
}

llvm::Constant* CGType::GenSizeOfTypeInfo()
{
    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(cgMod.GetLLVMContext()), GetSize().value_or(0));
}

llvm::Constant* CGType::GenAlignOfTypeInfo()
{
    return llvm::ConstantInt::get(llvm::Type::getInt8Ty(cgMod.GetLLVMContext()), GetAlign().value_or(0));
}

llvm::Constant* CGType::GenSourceGenericOfTypeInfo()
{
    auto p0i8 = llvm::Type::getInt8PtrTy(cgCtx.GetLLVMContext());
    if (IsStaticGI()) {
        return llvm::ConstantExpr::getBitCast(GetOrCreateTypeTemplate(), p0i8);
    }
    return llvm::ConstantPointerNull::get(p0i8);
}

llvm::Constant* CGType::GenTypeArgsNumOfTypeInfo()
{
    return llvm::ConstantInt::get(llvm::Type::getInt8Ty(cgMod.GetLLVMContext()), 0);
}

llvm::Constant* CGType::GenOffsetsOfTypeInfo()
{
    return llvm::ConstantPointerNull::get(llvm::Type::getInt32PtrTy(cgMod.GetLLVMContext()));
}

llvm::Constant* CGType::GenTypeArgsOfTypeInfo()
{
    return llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext()));
}

llvm::Constant* CGType::GenSuperOfTypeInfo()
{
    return llvm::ConstantPointerNull::get(CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
}

llvm::Constant* CGType::GenFlagOfTypeInfo()
{
    return llvm::ConstantInt::get(llvm::Type::getInt8Ty(cgMod.GetLLVMContext()), 0);
}

llvm::Constant* CGType::GenReflectionOfTypeInfo()
{
    return llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext()));
}

CGTIType::CGTIType(CGModule& cgMod)
    : CGType(cgMod, cgMod.GetCGContext(), *CGType::GetRefTypeOfCHIRInt8(cgMod.GetCGContext().GetCHIRBuilder()),
          CGTypeKind::CG_TI)
{
    llvmType = CGType::GetOrCreateTypeInfoType(cgMod.GetLLVMContext());
}

llvm::Type* CGTIType::GenLLVMType()
{
    return llvmType;
}

std::optional<uint32_t> CGType::GetSize() const
{
    return size;
}

bool CGType::IsOptionLikeRef() const
{
    return IsCGEnum() && StaticCast<const CGEnumType*>(this)->IsOptionLikeRef();
}

bool CGType::IsReference() const
{
    if (chirType.IsRef() &&
        (chirType.GetTypeArgs()[0]->IsClass() || chirType.GetTypeArgs()[0]->IsRawArray() ||
            chirType.GetTypeArgs()[0]->IsBox())) {
        return true;
    }
    if (chirType.IsBox() || chirType.IsClass() || chirType.IsRawArray()) {
        return true;
    }
    if (chirType.IsEnum()) {
        return static_cast<const CGEnumType*>(this)->IsCommonEnum();
    }
    if (chirType.IsGeneric()) {
        auto& gt = static_cast<const CHIR::GenericType&>(chirType);
        for (auto upperBound : gt.GetUpperBounds()) {
            if (CGType::GetOrCreate(cgMod, upperBound)->IsReference()) {
                return true;
            }
        }
    }
    return false;
}


// for reflection only
llvm::StructType* CGType::GetOrCreateGenericTypeInfoType(llvm::LLVMContext& llvmCtx)
{
    auto genericTypeInfoType = llvm::StructType::getTypeByName(llvmCtx, "GenericTypeInfo");
    if (genericTypeInfoType == nullptr) {
        genericTypeInfoType = llvm::StructType::create(llvmCtx, "GenericTypeInfo");
        std::vector<llvm::Type*> bodyVec(static_cast<size_t>(GENERIC_TYPE_INFO_FIELDS_NUM));
        bodyVec[static_cast<size_t>(GENERIC_TYPEINFO_NAME)] = llvm::Type::getInt8PtrTy(llvmCtx);
        bodyVec[static_cast<size_t>(GENERIC_TYPEINFO_TYPE_KIND)] = llvm::Type::getInt8Ty(llvmCtx);
        bodyVec[static_cast<size_t>(GENERIC_TYPEINFO_UPPERBOUNDS_NUM)] = llvm::Type::getInt32Ty(llvmCtx);
        bodyVec[static_cast<size_t>(GENERIC_TYPEINFO_UPPERBOUNDS)] = llvm::Type::getInt8PtrTy(llvmCtx);
        SetStructTypeBody(genericTypeInfoType, bodyVec);
    }
    return genericTypeInfoType;
}

llvm::StructType* CGType::GetOrCreateGenericCustomTypeInfoType(llvm::LLVMContext& llvmCtx)
{
    auto genericCustomTypeInfoType = llvm::StructType::getTypeByName(llvmCtx, "GenericCustomTypeInfo");
    if (genericCustomTypeInfoType == nullptr) {
        genericCustomTypeInfoType = llvm::StructType::create(llvmCtx, "GenericCustomTypeInfo");
        std::vector<llvm::Type*> bodyVec(static_cast<size_t>(GENERIC_CUSTOM_TYPE_INFO_FIELDS_NUM));
        auto i8PtrTy = llvm::Type::getInt8PtrTy(llvmCtx);
        auto i8Ty = llvm::Type::getInt8Ty(llvmCtx);
        bodyVec[static_cast<size_t>(GENERIC_CUSTOM_TYPEINFO_NAME)] = i8PtrTy;
        bodyVec[static_cast<size_t>(GENERIC_CUSTOM_TYPEINFO_TYPE_KIND)] = i8Ty;
        bodyVec[static_cast<size_t>(GENERIC_CUSTOM_TYPEINFO_TYPE_ARGS_NUM)] = i8Ty;
        bodyVec[static_cast<size_t>(GENERIC_CUSTOM_TYPEINFO_TYPE_ARGS)] = i8PtrTy;
        bodyVec[static_cast<size_t>(GENERIC_CUSTOM_TYPEINFO_SOURCE_GENERIC)] = i8PtrTy;
        SetStructTypeBody(genericCustomTypeInfoType, bodyVec);
    }
    return genericCustomTypeInfoType;
}

namespace {
using ChirTypeKind = Cangjie::CHIR::Type::TypeKind;
std::string GetTypeQualifiedNameForReflect(CGModule& cgMod, const CHIR::Type& t, bool forNameFieldOfTi);
std::string GetTypeArgsQualifiedNameForReflect(
    CGModule& cgMod, const std::vector<CHIR::Type*>& typeArgs, bool forNameFieldOfTi)
{
    std::string res;
    for (auto typeArg : typeArgs) {
        res += GetTypeQualifiedNameForReflect(cgMod, *typeArg, forNameFieldOfTi) + ',';
    }
    if (!typeArgs.empty()) {
        res.pop_back(); // The last ',' is redundant.
    }
    return res;
}

std::string GetTypeQualifiedNameOfCustomTypeForReflect(
    CGModule& cgMod, const CHIR::CustomType& type, bool forNameFieldOfTi)
{
    CJC_ASSERT(!type.IsAutoEnvInstBase());
    std::stringstream ss;
    if (type.IsAutoEnvGenericBase()) {
        auto typeArgs = type.GetGenericArgs();
        CJC_ASSERT(!typeArgs.empty());
        auto retIter = typeArgs.end() - 1U;
        std::vector<CHIR::Type*> paramType(typeArgs.begin(), retIter);
        ss << CHIR::MANGLE_CLOSURE_STR << '<';
        ss << '(' << GetTypeArgsQualifiedNameForReflect(cgMod, paramType, true) << ")->"
           << GetTypeQualifiedNameForReflect(cgMod, **retIter, true);
        ss << '>';
    } else {
        auto def = type.GetCustomTypeDef();
        auto packageName = def->GetGenericDecl() ? def->GetGenericDecl()->GetPackageName() : def->GetPackageName();
        ss << packageName << ':' << GetCustomTypeIdentifier(type);
        if (auto typeArgs = type.GetGenericArgs(); !typeArgs.empty()) {
            ss << '<' << GetTypeArgsQualifiedNameForReflect(cgMod, typeArgs, forNameFieldOfTi) << '>';
        }
    }
    return ss.str();
}

std::string GetTypeQualifiedNameForReflect(CGModule& cgMod, const CHIR::Type& t, bool forNameFieldOfTi = false)
{
    if (t.IsPrimitive()) {
        return t.ToString();
    }
    auto k = t.GetTypeKind();
    CJC_ASSERT(k != ChirTypeKind::TYPE_INVALID);
    switch (k) {
        case ChirTypeKind::TYPE_RAWARRAY:
            return CHIR::MANGLE_RAWARR_STR +
                GetTypeQualifiedNameForReflect(cgMod, *t.GetTypeArgs()[0], forNameFieldOfTi) + '>';
        case ChirTypeKind::TYPE_REFTYPE:
            return GetTypeQualifiedNameForReflect(
                cgMod, *static_cast<const CHIR::RefType&>(t).GetBaseType(), forNameFieldOfTi);
        case ChirTypeKind::TYPE_VARRAY: {
            auto& varrType = StaticCast<const CHIR::VArrayType&>(t);
            return CHIR::MANGLE_VARR_STR +
                GetTypeQualifiedNameForReflect(cgMod, *varrType.GetElementType(), forNameFieldOfTi) + ',' +
                std::to_string(varrType.GetSize()) + '>';
        }
        case ChirTypeKind::TYPE_CPOINTER:
            return CHIR::MANGLE_CPTR_STR +
                GetTypeQualifiedNameForReflect(cgMod, *t.GetTypeArgs()[0], forNameFieldOfTi) + '>';
        case ChirTypeKind::TYPE_CSTRING:
            return CHIR::MANGLE_CSTRING_STR;
        case ChirTypeKind::TYPE_TUPLE: {
            auto& type = static_cast<const CHIR::TupleType&>(t);
            return CHIR::MANGLE_TUPLE_STR +
                GetTypeArgsQualifiedNameForReflect(cgMod, type.GetElementTypes(), forNameFieldOfTi) + '>';
        }
        case ChirTypeKind::TYPE_STRUCT:
        case ChirTypeKind::TYPE_ENUM:
        case ChirTypeKind::TYPE_CLASS: {
            auto& type = static_cast<const CHIR::CustomType&>(t);
            return GetTypeQualifiedNameOfCustomTypeForReflect(cgMod, type, forNameFieldOfTi);
        }
        case ChirTypeKind::TYPE_FUNC: {
            auto& ft = static_cast<const CHIR::FuncType&>(t);
            std::string name = '(' + GetTypeArgsQualifiedNameForReflect(cgMod, ft.GetParamTypes(), forNameFieldOfTi) +
                ")->" + GetTypeQualifiedNameForReflect(cgMod, *ft.GetReturnType(), forNameFieldOfTi);
            return name;
        }
        case ChirTypeKind::TYPE_GENERIC: {
            auto genericTypeName = StaticCast<const CHIR::GenericType&>(t).GetSrcCodeIdentifier();
            auto upperBounds = StaticCast<const CHIR::GenericType&>(t).GetUpperBounds();
            return cgMod.GetCGContext().GetGenericTypeUniqueName(genericTypeName, upperBounds);
        }
        case ChirTypeKind::TYPE_VOID:
            return CHIR::MANGLE_UNIT_STR;
        case ChirTypeKind::TYPE_BOXTYPE: {
            auto& type = static_cast<const CHIR::BoxType&>(t);
            auto typeQualifiedName = GetTypeQualifiedNameForReflect(cgMod, *type.GetBaseType(), forNameFieldOfTi);
            return forNameFieldOfTi ? typeQualifiedName : (CHIR::MANGLE_BOX_STR + typeQualifiedName + ">");
        }
        default:
            CJC_ASSERT(false && "Should not reach here.");
            return "";
    }
}
} // namespace

llvm::GlobalVariable* CGType::GetOrCreateGenericCustomTypeInfo()
{
    auto qualifiedName = GetTypeQualifiedNameForReflect(cgMod, chirType, true);
    if (auto found = cgMod.GetLLVMModule()->getNamedGlobal(qualifiedName + ".ti"); found) {
        return found;
    }

    auto genericCustomTypeInfoType = GetOrCreateGenericCustomTypeInfoType(cgMod.GetLLVMContext());
    auto genericCustomTypeInfo = llvm::cast<llvm::GlobalVariable>(
        cgMod.GetLLVMModule()->getOrInsertGlobal(qualifiedName + ".ti", genericCustomTypeInfoType));
    auto i8PtrTy = llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext());
    unsigned typeInfoKind = UGTypeKind::UG_GENERIC_CUSTOM;
    std::vector<llvm::Constant*> typeInfoVec(GENERIC_CUSTOM_TYPE_INFO_FIELDS_NUM);
    typeInfoVec[static_cast<size_t>(GENERIC_CUSTOM_TYPEINFO_NAME)] =
        cgMod.GenerateTypeNameConstantString(GetTypeQualifiedName(chirType, true), false);
    typeInfoVec[static_cast<size_t>(GENERIC_CUSTOM_TYPEINFO_TYPE_KIND)] =
        llvm::ConstantInt::get(llvm::Type::getInt8Ty(cgMod.GetLLVMContext()), typeInfoKind);
    typeInfoVec[static_cast<size_t>(GENERIC_CUSTOM_TYPEINFO_TYPE_ARGS_NUM)] = GenTypeArgsNumOfTypeInfo();
    typeInfoVec[static_cast<size_t>(GENERIC_CUSTOM_TYPEINFO_TYPE_ARGS)] = GenTypeArgsOfGenericCustomTypeInfo();
    typeInfoVec[static_cast<size_t>(GENERIC_CUSTOM_TYPEINFO_SOURCE_GENERIC)] =
        llvm::ConstantExpr::getBitCast(GetOrCreateTypeTemplate(), i8PtrTy);
    genericCustomTypeInfo->setInitializer(llvm::ConstantStruct::get(genericCustomTypeInfoType, typeInfoVec));
    genericCustomTypeInfo->setLinkage(llvm::GlobalValue::PrivateLinkage);
    genericCustomTypeInfo->addAttribute(GENERIC_TYPEINFO_ATTR);
    return genericCustomTypeInfo;
}

llvm::Constant* CGType::GenTypeArgsOfGenericCustomTypeInfo()
{
    auto p0i8 = llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext());
    auto genericArgs = static_cast<const CHIR::CustomType&>(chirType).GetGenericArgs();
    if (genericArgs.empty()) {
        return llvm::ConstantPointerNull::get(p0i8);
    }

    std::vector<llvm::Constant*> constants;
    for (auto typeArg : genericArgs) {
        auto cgTypeOfTypeArg = CGType::GetOrCreate(cgMod, DeRef(*typeArg));
        auto genericArgsTypeInfo = cgTypeOfTypeArg->GetOrCreateTypeInfo();
        constants.emplace_back(llvm::ConstantExpr::getBitCast(genericArgsTypeInfo, p0i8));
    }
    auto typeOfGenericArgsGV = llvm::ArrayType::get(p0i8, constants.size());
    auto typeInfoOfGenericArgs = llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(
        GetTypeQualifiedNameForReflect(cgMod, chirType) + ".ti.typeArgs", typeOfGenericArgsGV));
    typeInfoOfGenericArgs->setInitializer(llvm::ConstantArray::get(typeOfGenericArgsGV, constants));
    typeInfoOfGenericArgs->setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
    return llvm::ConstantExpr::getBitCast(typeInfoOfGenericArgs, p0i8);
}

} // namespace CodeGen
} // namespace Cangjie
