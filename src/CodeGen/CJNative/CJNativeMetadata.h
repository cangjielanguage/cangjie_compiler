// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the generator for metadata.
 */

#ifndef CANGJIE_METADATAGEN_H
#define CANGJIE_METADATAGEN_H

#include "CGContext.h"
#include "CGModule.h"
#include "CJNative/CHIRSplitter.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/EnumDef.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"

namespace Cangjie {
namespace CodeGen {
enum MetadataKind : uint8_t {
    PKG_METADATA = 0,
    STRUCT_METADATA,
    CLASS_METADATA,
    ENUM_METADATA,
    GV_METADATA, // Global variables
    GF_METADATA, // Global functions
    COUNT
};

enum GenReflectMode : uint8_t {
    NO_REFLECT = 0,
    FULL_REFLECT
};

enum class ExtraAttribute {
    MUTABLE_FIELD,
    IMMUTABLE_FIELD,
    INTERFACE,
    CLASS,
    STRUCT,
    ENUM,
    METHOD,
    BOX_CLASS,
    METHOD_FROM_INTERFACE
};

enum SRetMode : uint8_t {
    SRET_NON_GENERIC = 0,            // has SRet but retTy is neither 'T' nor 'struct<T>'
    SRET_GENERIC = 1,                // has SRet and retTy is 'T'
    SRET_KNOWN_GENERIC_CUSTOM = 2,   // has SRet and retTy is 'struct<T>' and this struct has no field with generic type
    SRET_UNKNOWN_GENERIC_CUSTOM = 3, // has SRet and retTy is 'struct<T>' and this struct has fields with generic type
    NO_SRET = 4                      // no SRet
};

const std::vector<std::string> PRIMITIVE_TIS = {"Nothing.ti", "Unit.ti", "Bool.ti", "Rune.ti", "UInt8.ti", "UInt16.ti",
    "UInt32.ti", "UInt64.ti", "UIntNative.ti", "Int8.ti", "Int16.ti", "Int32.ti", "Int64.ti", "IntNative.ti",
    "Float16.ti", "Float32.ti", "Float64.ti", "CString.ti"};
const std::vector<std::string> PRIMITIVE_TTS = {
    "Tuple.tt", "VArray.tt", "RawArray.tt", "CPointer.tt", "CFunc.tt", "Closure.tt"};

class MetadataVector {
public:
    explicit MetadataVector(llvm::LLVMContext& llvmCtx) : llvmCtx(llvmCtx)
    {
    }
    MetadataVector(const MetadataVector&) = delete;
    MetadataVector& operator=(const MetadataVector&) = delete;
    MetadataVector(MetadataVector&&) = delete;
    MetadataVector& operator=(MetadataVector&&) = delete;
    MetadataVector& Concat(llvm::Metadata* md)
    {
        (void)data.emplace_back(md);
        return *this;
    }

    MetadataVector& Concat(const std::string& rawStr)
    {
        (void)data.emplace_back(llvm::MDString::get(llvmCtx, rawStr));
        return *this;
    }

    MetadataVector& Concat(const size_t num)
    {
        (void)data.emplace_back(
            llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvmCtx), num)));
        return *this;
    }

    MetadataVector& AddSubItem(MetadataVector& mdVec)
    {
        (void)data.emplace_back(mdVec.CreateMDTuple());
        return *this;
    }

    llvm::MDTuple* CreateMDTuple()
    {
        return llvm::MDTuple::get(llvmCtx, data);
    }

private:
    std::vector<llvm::Metadata*> data{};
    llvm::LLVMContext& llvmCtx;
};

struct MetadataTypeItem {
    llvm::MDString* name;
    llvm::MDString* declaredGenericTi; // {} for enum
    llvm::MDTuple* instanceFields;     // ctors for enum
    llvm::MDTuple* staticFields;       // {} for enum
    llvm::MDTuple* instanceMethods;
    llvm::MDTuple* staticMethods;
    llvm::MDTuple* typeAttrs;
    explicit MetadataTypeItem(llvm::MDString* name, llvm::MDString* declaredGenericTi, llvm::MDTuple* instanceFields,
        llvm::MDTuple* staticFields, llvm::MDTuple* instanceMethods, llvm::MDTuple* staticMethods,
        llvm::MDTuple* typeAttrs)
        : name(name),
          declaredGenericTi(declaredGenericTi),
          instanceFields(instanceFields),
          staticFields(staticFields),
          instanceMethods(instanceMethods),
          staticMethods(staticMethods),
          typeAttrs(typeAttrs)
    {
    }

    llvm::MDTuple* CreateMDTuple(llvm::LLVMContext& ctx, bool isEnum = false)
    {
        if (isEnum) {
            return llvm::MDTuple::get(ctx, {name, instanceFields, instanceMethods, staticMethods, typeAttrs});
        } else {
            return llvm::MDTuple::get(ctx,
                {name, declaredGenericTi, instanceFields, staticFields, instanceMethods, staticMethods, typeAttrs});
        }
    }
};

class MetadataInfo {
public:
    explicit MetadataInfo(CGModule& cgMod, const SubCHIRPackage& subCHIRPkg, uint8_t reflectionMode)
        : module(cgMod), subCHIRPkg(subCHIRPkg), reflectionMode(reflectionMode)
    {
    }
    virtual ~MetadataInfo() = default;
    virtual void Gen() = 0;

protected:
    llvm::MDTuple* GenerateInstanceFieldMetadata(const CHIR::MemberVarInfo& field, size_t idx);
    llvm::MDTuple* GenerateStaticFieldMetadata(const CHIR::GlobalVarBase& staticField);
    llvm::MDTuple* GenerateMethodMetadata(const CHIR::FuncBase& method, bool isFromInterface = false);
    llvm::MDTuple* GenerateAttrsMetadata(const CHIR::AttributeInfo& attrs, ExtraAttribute extraAttr,
        const std::string& gettingAnnotationMethod, uint8_t hasSRetMode = SRetMode::NO_SRET,
        const std::string& enumKind = "") const;
    llvm::MDTuple* GenerateParametersMetadata(
        const std::vector<CHIR::Parameter*>& argsInfo, bool hasThis = false) const;
    llvm::MDTuple* GenerateParametersMetadata(
        const std::vector<CHIR::AbstractMethodParam>& paramInfos, bool hasThis = false) const;
    llvm::MDTuple* GenerateParametersMetadata(const std::vector<CHIR::GenericType*>& genericParamInfos) const;
    std::string GetTiName(const CHIR::Type& ty) const;
    CGModule& module;
    const SubCHIRPackage& subCHIRPkg;
    uint8_t reflectionMode;
};

class StructMetadataInfo : public MetadataInfo {
public:
    StructMetadataInfo(CGModule& cgMod, const SubCHIRPackage& subCHIRPkg, uint8_t reflectionMode)
        : MetadataInfo(cgMod, subCHIRPkg, reflectionMode)
    {
    }
    void Gen() override
    {
        return GenerateAllStructsMetadata();
    }

private:
    void GenerateAllStructsMetadata();
    void GenerateStructMetadata(const CHIR::StructDef& sd, std::set<std::string>& doneSet);
    llvm::MDTuple* GenerateStructFieldMetadata(const CHIR::StructDef& sd);
    llvm::MDTuple* GenerateStructStaticFieldMetadata(const CHIR::StructDef& sd);
    void GenerateStructMethodMetadata(const CHIR::StructDef& sd, std::vector<llvm::Metadata*>& methodsVec,
        std::vector<llvm::Metadata*>& staticMethodsVec);
};

class ClassMetadataInfo : public MetadataInfo {
public:
    ClassMetadataInfo(CGModule& cgMod, const SubCHIRPackage& subCHIRPkg, uint8_t reflectionMode)
        : MetadataInfo(cgMod, subCHIRPkg, reflectionMode)
    {
    }

    void Gen() override
    {
        return GenerateAllClassesMetadata();
    }

private:
    void GenerateAllClassesMetadata();
    void GenerateClassLikeMetadata(const CHIR::ClassDef& cd);
    llvm::MDTuple* GenerateClassLikeFieldMetadata(const CHIR::ClassDef& cd);
    llvm::MDTuple* GenerateClassLikeStaticFieldMetadata(const CHIR::ClassDef& cd);
    void GenerateClassLikeMethodMetadata(const CHIR::ClassDef& cd, std::vector<llvm::Metadata*>& methodsVec,
        std::vector<llvm::Metadata*>& staticMethodsVec);
    llvm::MDTuple* GenerateClassLikeTypeAttrsMetadata(const CHIR::ClassDef& cd) const;
    llvm::MDNode* GenerateClassAbsMethodMetadata(const CHIR::AbstractMethodInfo& absMethod);
};

class EnumMetadataInfo : public MetadataInfo {
public:
    EnumMetadataInfo(CGModule& cgMod, const SubCHIRPackage& subCHIRPkg, uint8_t reflectionMode)
        : MetadataInfo(cgMod, subCHIRPkg, reflectionMode)
    {
    }

    void Gen() override
    {
        return GenerateAllEnumsMetadata();
    }

private:
    void GenerateAllEnumsMetadata();
    void GenerateEnumMetadata(const CHIR::EnumDef& ed);
    llvm::MDTuple* GenerateEnumConstructorMetadata(const CHIR::EnumDef& ed);
    void GenerateEnumMethodMetadata(const CHIR::EnumDef& ed, std::vector<llvm::Metadata*>& methodsVec,
        std::vector<llvm::Metadata*>& staticMethodsVec);
    std::string GenerateCtorFn(const CHIR::EnumDef& enumDef, size_t index, const std::string& qualifiedName);
};

class GVMetadataInfo : public MetadataInfo {
public:
    GVMetadataInfo(CGModule& cgMod, const SubCHIRPackage& subCHIRPkg, uint8_t reflectionMode)
        : MetadataInfo(cgMod, subCHIRPkg, reflectionMode)
    {
    }

    void Gen() override
    {
        return GenerateGlobalVariablesMetadata();
    }

private:
    void GenerateGlobalVariablesMetadata();
    llvm::MDNode* GenerateVariableMetadata(const CHIR::GlobalVar& variable);
};

class GFMetadataInfo : public MetadataInfo {
public:
    GFMetadataInfo(CGModule& cgMod, const SubCHIRPackage& subCHIRPkg, uint8_t reflectionMode)
        : MetadataInfo(cgMod, subCHIRPkg, reflectionMode)
    {
    }

    void Gen() override
    {
        return GenerateAllFunctionsMetadata();
    }

private:
    void GenerateAllFunctionsMetadata();
};

class PkgMetadataInfo : public MetadataInfo {
public:
    PkgMetadataInfo(CGModule& cgMod, const SubCHIRPackage& subCHIRPkg, uint8_t reflectionMode)
        : MetadataInfo(cgMod, subCHIRPkg, reflectionMode)
    {
    }

    void Gen() override
    {
        GeneratePkgMetadata();
    }

private:
    void GeneratePkgMetadata() const;
    void AddPrimitiveTypeInfoToCorePkgInfo() const;
};

class CGMetadata {
public:
    CGMetadata(CGModule& module, const SubCHIRPackage& subCHIRPkg)
        : module(module), subCHIRPkg(subCHIRPkg)
    {
        auto globalOptions = module.GetCGContext().GetCGPkgContext().GetGlobalOptions();
        if (globalOptions.disableReflection) {
            reflectionMode = GenReflectMode::NO_REFLECT;
        } else {
            reflectionMode = GenReflectMode::FULL_REFLECT;
        }
    }

    CGMetadata* Needs(MetadataKind mdKind)
    {
        (void)mdInfoFlags.set(static_cast<size_t>(mdKind));
        return this;
    }

    void Gen()
    {
        for (uint8_t i = 0; i < MetadataKind::COUNT; ++i) {
            if (mdInfoFlags.test(i)) {
                mdCtors.at(static_cast<MetadataKind>(i))()->Gen();
            }
        }
    }

private:
    std::bitset<MetadataKind::COUNT> mdInfoFlags;
    CGModule& module;
    const SubCHIRPackage& subCHIRPkg;
    uint8_t reflectionMode;
    const std::unordered_map<MetadataKind, std::function<std::unique_ptr<MetadataInfo>()>> mdCtors = {
        {MetadataKind::STRUCT_METADATA,
            [this]() { return std::make_unique<StructMetadataInfo>(module, subCHIRPkg, reflectionMode); }},
        {MetadataKind::CLASS_METADATA,
            [this]() { return std::make_unique<ClassMetadataInfo>(module, subCHIRPkg, reflectionMode); }},
        {MetadataKind::ENUM_METADATA,
            [this]() { return std::make_unique<EnumMetadataInfo>(module, subCHIRPkg, reflectionMode); }},
        {MetadataKind::GV_METADATA,
            [this]() { return std::make_unique<GVMetadataInfo>(module, subCHIRPkg, reflectionMode); }},
        {MetadataKind::GF_METADATA,
            [this]() { return std::make_unique<GFMetadataInfo>(module, subCHIRPkg, reflectionMode); }},
        {MetadataKind::PKG_METADATA,
            [this]() { return std::make_unique<PkgMetadataInfo>(module, subCHIRPkg, reflectionMode); }},
    };
};
} // namespace CodeGen
} // namespace Cangjie
#endif
