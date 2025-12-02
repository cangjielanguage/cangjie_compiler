// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CGTYPE_H
#define CANGJIE_CGTYPE_H

#include <optional>

#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"

#include "cangjie/Basic/UGTypeKind.h"
#include "cangjie/CHIR/IR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie {
namespace CHIR {
class Value;
class CHIRBuilder;
class FuncBase;
} // namespace CHIR
namespace CodeGen {
class CGContext;
class CGModule;
class CGType;
class CGGenericType;
class CGTypeMgr;
class DIBuilder;
class CGExtensionDef;

enum class CGTypeKind {
    OTHERS,
    CG_PRIMITIVE,
    CG_FUNCTION,
    CG_ENUM,
    CG_CLASS,
    CG_REF,
    CG_TI,
};

class CGType {
    friend class CGTypeMgr;
    friend class DIBuilder;

public:
    CGType() = delete;
    virtual ~CGType()
    {
    }

    bool operator==(const CGType* that) const
    {
        return llvmType == that->llvmType;
    }

    bool operator==(const CGType& that) const
    {
        return llvmType == that.llvmType;
    }

    CGContext& GetCGContext() const
    {
        return cgCtx;
    }

    llvm::Type* GetLLVMType() const
    {
        CJC_ASSERT(llvmType != nullptr);
        return llvmType;
    }

    virtual llvm::GlobalVariable* GetOrCreateTypeInfo();
    llvm::GlobalVariable* GetOrCreateTypeTemplate();
    llvm::GlobalVariable* GetOrCreateGenericCustomTypeInfo();
    llvm::Constant* GenTypeArgsOfGenericCustomTypeInfo();

    const CHIR::Type& GetOriginal() const
    {
        return chirType;
    }

    bool IsFloatType() const
    {
        return llvmType->isFloatTy();
    }

    bool IsBooleanType() const
    {
        return llvmType->isIntegerTy(1);
    }

    bool IsPointerType(unsigned int addrSpace = 0) const
    {
        return chirType.IsRef() ||
            (!chirType.IsFunc() && llvmType->isPointerTy() && llvmType->getPointerAddressSpace() == addrSpace);
    }

    bool IsStructType() const
    {
        return GetLLVMType()->isStructTy();
    }

    bool IsVArrayType() const
    {
        return GetLLVMType()->isArrayTy();
    }

    bool IsStructPtrType() const
    {
        return IsPointerType() && GetPointerElementType()->GetLLVMType()->isStructTy();
    }

    bool IsRefType() const
    {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        return llvmType->isPointerTy() && 1U == llvmType->getPointerAddressSpace();
#endif
    }

    bool IsReference() const;

    bool IsFuncPtrType() const
    {
        return IsPointerType() && GetPointerElementType()->GetLLVMType()->isFunctionTy();
    }

    bool IsCGEnum() const
    {
        return cgTypeKind == CGTypeKind::CG_ENUM;
    }

    bool IsOptionLikeRef() const;

    bool IsCGRef() const
    {
        return cgTypeKind == CGTypeKind::CG_REF;
    }

    bool IsCGFunction() const
    {
        return cgTypeKind == CGTypeKind::CG_FUNCTION;
    }

    bool IsCGTI() const
    {
        return cgTypeKind == CGTypeKind::CG_TI;
    }

    inline bool IsLoadablePtrType() const
    {
        if (!IsPointerType()) {
            return false;
        }
        // Without the semantic type, it is difficult to distinguish whether the codegen type of the CPointer is a
        // rvalue. Need to check whether any non-type generates i8**. If no, the semantic dependency can be deleted.
        if (chirType.IsCPointer() || chirType.IsCString()) {
            return IsLoadableCPointer();
        }
        if (IsFuncPtrType()) {
            return false;
        }

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        // pointer types of `structure pointer ty`/`ref type`/`varray type` can't be loaded in LLVM.
        // (They are passed by pointer in LLVM).
        if (IsStructPtrType() || IsRefType() || IsVArrayPtrType()) {
            return false;
        }
#endif
        return true;
    }

    bool IsLoadableCPointer() const
    {
        return llvmType && IsPointerType() && GetPointerElementType()->IsPointerType();
    }

    bool IsStructPtrTypeWithAddrSpace1() const
    {
        return IsStructPtrType() && GetLLVMType()->getPointerAddressSpace() == 1;
    }

    bool IsVArrayPtrType() const
    {
        return IsPointerType() && GetPointerElementType()->GetLLVMType()->isArrayTy();
    }

    const CGType* GetPointerElementType() const
    {
        CJC_ASSERT(llvmType->isPointerTy());
        if (chirType.IsGeneric()) {
            return this;
        }

        auto tmp = GetContainedTypes();
        CJC_ASSERT(tmp.size() == 1);
        return tmp[0];
    }

    const CGType* GetContainedTypeAt(size_t idx) const
    {
        auto tmp = GetContainedTypes();
        CJC_ASSERT(idx < tmp.size());
        return tmp[idx];
    }

    std::vector<CGType*> GetContainedTypes() const
    {
        if (containedCGTypes.empty()) {
            const_cast<CGType*>(this)->GenContainedCGTypes();
        }
        return containedCGTypes;
    }

    unsigned GetAddrspace() const
    {
        return addrspace;
    }

    std::optional<uint32_t> GetSize() const;

    std::optional<uint32_t> GetAlign() const
    {
        return align;
    }

    inline bool IsConcrete() const
    {
        return cgGenericKind == CGGenericKind::CONCRETE;
    }

    inline bool IsStaticGI() const
    {
        return cgGenericKind == CGGenericKind::STATIC_GI;
    }

    inline bool IsDynamicGI() const
    {
        return cgGenericKind == CGGenericKind::DYNAMIC_GI;
    }

    void GenTypeInfo();

    void SetCGExtensionDef(CGExtensionDef* ed)
    {
        cgExtensionDef = ed;
    }

    virtual llvm::Constant* GenSizeOfTypeInfo();
    virtual llvm::Constant* GenAlignOfTypeInfo();
    virtual void CalculateSizeAndAlign() = 0;
    virtual void GenTypeTemplate()
    {
    }

    static llvm::Type* GetUnitType(llvm::LLVMContext& llvmCtx);
    static llvm::Type* GetRefType(llvm::LLVMContext& llvmCtx);
    static llvm::Type* GetLandingPadType(llvm::LLVMContext& llvmCtx);
    static llvm::Type* GetArrayBaseType(CGContext& cgCtx);
    static llvm::Type* GetBitMapType(llvm::LLVMContext& llvmCtx);
    static llvm::FunctionType* GetCodeGenFunctionType(
        llvm::LLVMContext& llvmCtx, llvm::Type* retType, std::vector<llvm::Type*>& params);
    static llvm::StructType* GetOrCreateTypeInfoType(llvm::LLVMContext& llvmCtx);
    static llvm::PointerType* GetOrCreateTypeInfoPtrType(llvm::LLVMContext& llvmCtx);
    static llvm::StructType* GetOrCreateTypeTemplateType(llvm::LLVMContext& llvmCtx);
    static llvm::StructType* GetOrCreateExtensionDefType(llvm::LLVMContext& llvmCtx);
    static llvm::PointerType* GetOrCreateExtensionDefPtrType(llvm::LLVMContext& llvmCtx);
    static llvm::StructType* GetOrCreateEnumCtorLayoutType(CGModule& cgModule, const CHIR::EnumType& enumTy,
        std::size_t ctorIndex, const std::vector<CHIR::Type*>& paramTypes);

    /// To get a RefType of chir
    static CHIR::RefType* GetRefTypeOf(CHIR::CHIRBuilder& chirBuilder, const CHIR::Type& typeOfCHIR);
    static CHIR::RefType* GetRefTypeOfCHIRInt8(CHIR::CHIRBuilder& chirBuilder);

    static CGType* GetCGTI(CGModule& cgModule);

    // for reflection
    static llvm::StructType* GetOrCreateGenericTypeInfoType(llvm::LLVMContext& llvmCtx);
    static llvm::StructType* GetOrCreateGenericCustomTypeInfoType(llvm::LLVMContext& llvmCtx);

    struct TypeExtraInfo {
        size_t addrspace = 0U;
        bool isMethod = false;
        bool isStaticMethod = false;
        bool allowBasePtr = true;
        bool forWrapper = false;
        std::vector<CHIR::Type*> instantiatedParamTypes;
        TypeExtraInfo()
        {
        }
        TypeExtraInfo(size_t addrspace, bool isMethod, bool isStaticMethod, bool allowBasePtr,
            const std::vector<CHIR::Type*>& instantiatedParamTypes)
            : addrspace(addrspace),
              isMethod(isMethod),
              isStaticMethod(isStaticMethod),
              allowBasePtr(allowBasePtr),
              instantiatedParamTypes(instantiatedParamTypes)
        {
        }
        TypeExtraInfo(size_t addrspace) : addrspace(addrspace)
        {
        }
        bool operator==(const TypeExtraInfo& rhs) const
        {
            bool ret = instantiatedParamTypes.size() == rhs.instantiatedParamTypes.size() &&
                addrspace == rhs.addrspace && isMethod == rhs.isMethod && isStaticMethod == rhs.isStaticMethod &&
                allowBasePtr == rhs.allowBasePtr && forWrapper == rhs.forWrapper;
            if (ret) {
                for (std::size_t idx = 0; idx < instantiatedParamTypes.size(); ++idx) {
                    if (instantiatedParamTypes[idx] != rhs.instantiatedParamTypes[idx]) {
                        return false;
                    }
                }
            }
            return ret;
        }
    };

    struct TypeExtraInfoHasher {
        size_t operator()(const TypeExtraInfo& extraInfo) const
        {
            size_t ret = (extraInfo.addrspace << 4U) & (static_cast<size_t>(extraInfo.isMethod) << 3U) &
                (static_cast<size_t>(extraInfo.isStaticMethod) << 2U) &
                static_cast<size_t>(extraInfo.allowBasePtr << 1U) & static_cast<size_t>(extraInfo.forWrapper);
            std::string tmp = std::to_string(ret);
            for (auto paramType : extraInfo.instantiatedParamTypes) {
                tmp += paramType->ToString();
            }
            return std::hash<std::string>{}(tmp);
        }
    };

    enum class CGGenericKind {
        UNKNOWN,    /**< Not a valid kind */
        STATIC_GI,  /**< Generic instantiated at compile-time */
        DYNAMIC_GI, /**< Generic instantiated at runtime */
        CONCRETE,   /**< Others */
    };
    static CGGenericKind GetCGGenericKind(const CHIR::Type& chirType);

    /// For any CHIRType
    static CGType* GetOrCreate(CGModule& cgModule, const CHIR::Type* chirTy, const TypeExtraInfo& extraInfo = {});
    /// For getting CGType of chirFunc, the properties(`mut`, method or not, .etc) of the function
    /// are additionally considered.
    static CGType* GetOrCreateWithNode(
        CGModule& cgModule, const CHIR::Value* chirNode, bool allowBasePtr = true, bool forWrapper = false);

    static CGType* GetObjectCGType(CGModule& cgMod);
    /// For specified numeric type
    static CGType* GetInt8CGType(CGModule& cgMod);
    static CGType* GetInt16CGType(CGModule& cgMod);
    static CGType* GetInt32CGType(CGModule& cgMod);
    static CGType* GetInt64CGType(CGModule& cgMod);
    static CGType* GetUInt8CGType(CGModule& cgMod);
    static CGType* GetUInt16CGType(CGModule& cgMod);
    static CGType* GetUInt32CGType(CGModule& cgMod);
    static CGType* GetUInt64CGType(CGModule& cgMod);
    static CGType* GetFloat16CGType(CGModule& cgMod);
    static CGType* GetFloat32CGType(CGModule& cgMod);
    static CGType* GetFloat64CGType(CGModule& cgMod);

    static CGType* GetZeroSizedCGType(CGModule& cgMod);
    static CGType* GetUnitCGType(CGModule& cgMod);
    static CGType* GetNothingCGType(CGModule& cgMod);
    static CGType* GetBoolCGType(CGModule& cgMod);

    static CGType* GetCStringCGType(CGModule& cgMod);
    static CGType* GetInt8PtrCGType(CGModule& cgMod);

    static std::string GetNameOfTypeInfoGV(const CHIR::Type& chirType);
    static std::string GetNameOfTypeTemplateGV(const CHIR::Type& chirType);
    static llvm::StructType* GetClassTTExtTypeVer1(llvm::LLVMContext& llvmCtx);

protected:
    CGType(CGModule& cgMod, CGContext& cgCtx, const CHIR::Type& chirType, CGTypeKind cgTypeKind = CGTypeKind::OTHERS);

    llvm::Constant* GenNameOfTypeInfo();
    llvm::Constant* GenInheritedClassNumOfTypeInfo() const;
    llvm::Constant* GenExtensionDefPtrOfTypeInfo() const;
    llvm::Constant* GenMTableOfTypeInfo();
    llvm::Constant* GenKindOfTypeInfo();
    llvm::Constant* GenFlagOfTypeInfo();

    virtual llvm::Type* GenLLVMType() = 0;
    virtual void GenContainedCGTypes() = 0;

    virtual void PreActionOfGenTypeInfo()
    {
    }
    virtual void PostActionOfGenTypeInfo()
    {
    }
    virtual llvm::Constant* GenFieldsNumOfTypeInfo();
    virtual llvm::Constant* GenFieldsOfTypeInfo();
    virtual llvm::Constant* GenSourceGenericOfTypeInfo();
    virtual llvm::Constant* GenTypeArgsNumOfTypeInfo();
    virtual llvm::Constant* GenOffsetsOfTypeInfo();
    virtual llvm::Constant* GenTypeArgsOfTypeInfo();
    virtual llvm::Constant* GenSuperOfTypeInfo();
    virtual llvm::Constant* GenReflectionOfTypeInfo();

protected:
    CGModule& cgMod;
    CGContext& cgCtx;
    const CHIR::Type& chirType;
    llvm::Type* llvmType{nullptr};
    llvm::StructType* layoutType{nullptr}; /**< For type contains fields, it saves the layout of its fields. */
    llvm::GlobalVariable* typeInfo{nullptr};
    llvm::GlobalVariable* typeTemplate{nullptr};
    std::vector<CGType*> containedCGTypes;
    CGTypeKind cgTypeKind;
    unsigned addrspace = 0U;
    std::optional<uint32_t> size{std::nullopt};
    std::optional<uint16_t> align{std::nullopt};

    std::vector<CGGenericType*> genericTypeArgs;

private:
    CGGenericKind cgGenericKind = CGGenericKind::UNKNOWN;
    // 1) If inheritedClassNum is less than 2^14, the higher 2 bits are reserved for runtime optimization flags:
    //      - The 16th bit is 1, indicating that the runtime can be optimized using bitmap.
    //      - The 15th bit is 1, indicating that the runtime can be optimized using compressed bitmap.
    //    In this case, to get the inherited class number, you should take the lower 14 bits.
    // 2) If inheritedClassNum is equal to or more than 2^14, bitmap will be set null,
    uint16_t inheritedClassNum = 0U;
    CGExtensionDef* cgExtensionDef{nullptr};
};

class CGTIType : public CGType {
    friend class CGTypeMgr;

public:
    static CGTIType* Get(CGModule& cgMod)
    {
        static CGTIType ins = CGTIType(cgMod);
        return &ins;
    }

protected:
    llvm::Type* GenLLVMType() override;
    void GenContainedCGTypes() override{};

private:
    explicit CGTIType(CGModule& cgMod);
    CGTIType() = delete;
    void CalculateSizeAndAlign() override
    {
        size = std::nullopt;
        align = std::nullopt;
    };
};

class CGTypeMgr {
public:
    static CGType* GetConcreteCGTypeFor(CGModule& cgMod, const CHIR::Type& chirType,
        const CGType::TypeExtraInfo& extraInfo = {}, const CHIR::FuncBase* chirFunc = nullptr);
};
} // namespace CodeGen
} // namespace Cangjie
#endif // CANGJIE_CGTYPE_H
