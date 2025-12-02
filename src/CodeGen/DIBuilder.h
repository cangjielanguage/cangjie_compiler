// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_DIBUILDER_H
#define CANGJIE_DIBUILDER_H

#include "llvm/IR/DIBuilder.h"

#include "Base/CGTypes/CGClassType.h"
#include "Base/CGTypes/CGEnumType.h"
#include "Base/CHIRExprWrapper.h"
#include "CGModule.h"
#include "CJNative/CGTypes/EnumCtorTIOrTTGenerator.h"
#include "Utils/CGCommonDef.h"
#include "cangjie/CHIR/DebugLocation.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/Type/EnumDef.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie {
namespace CHIR {
class Type;
class GlobalVar;
} // namespace CHIR

namespace CodeGen {
class CGModule;
class CGType;
using LLVMDIBuilder = llvm::DIBuilder;

/* llvm::SmallVector has improved performance when No. of elements is smaller than declared,
 * so some commonly used sizes of SmallVectors are declared below for easy use.
 */
using CodeGenDIVector3 = llvm::SmallVector<llvm::Metadata*, 3>;   // A vector of 3, Array.Type has 3 elements.
using CodeGenDIVector4 = llvm::SmallVector<llvm::Metadata*, 4>;   // A vector of 4, commonly for 4 enum elements.
using CodeGenDIVector16 = llvm::SmallVector<llvm::Metadata*, 16>; // A vector of 16, for even larger use.

class DIBuilder : public LLVMDIBuilder {
public:
    explicit DIBuilder(CGModule& cgMod);

    void Finalize();
    void CreateGlobalVar(const CHIR::GlobalVar& variable);
    void CreateAnonymousTypeForGenericType(llvm::GlobalVariable* typeTemplate, const CHIR::Type& ty);

    void SetSubprogram(const CHIR::Func* func, llvm::Function* function);
    void FinalizeSubProgram(llvm::Function& function);
    void EmitDeclare(const CHIR::Debug& debugNode, llvm::BasicBlock& curBB, bool pointerWrapper = false);
    void EmitUnBoxDeclare(const CHIR::Expression& exprNode, const CHIR::Type& ty, const std::string& name,
        llvm::Value* value, llvm::BasicBlock& curBB);
    void EmitGenericDeclare(
        const CHIR::GenericType& genericTy, llvm::Value* arg, llvm::BasicBlock& curBB, size_t index);
    llvm::DILocation* CreateDILoc(const CHIRExprWrapper& chirNode, const bool removable = false);
    llvm::DILocation* CreateDILoc(llvm::DIScope* currentScope, const CHIR::Position& position);

    void CreateCompileUnit(const std::string& pkgName);
    void CreateNameSpace(const std::string& pkgName);

private:
    bool enabled = false;
    bool enableLineInfo = false;
    bool needSubprogram = false;
    size_t genericID = 0;
    llvm::DINamespace* diNamespace{nullptr};
    llvm::DICompileUnit* diCompileUnit{nullptr};
    llvm::DIType* objDIType{nullptr};
    llvm::DIType* refDIType = nullptr;
    CGModule& cgMod;
    size_t paramNo = 0;
    llvm::DIFile* defaultFile{nullptr};
    Position curLocation = {0, 0};
    llvm::DIScope* curScope{nullptr};
    std::vector<const CHIR::Type*> recursiveChains;
    std::map<const std::vector<int>, llvm::DIScope*> lexicalBlocks;
    llvm::DenseMap<const CHIR::Type*, llvm::TrackingMDRef> typeCache;
    llvm::DenseMap<const CHIR::Type*, llvm::TrackingMDRef> enumCtorCache;

    llvm::DIType* CreateDIType(const CHIR::Type& ty);
    llvm::DIScope* GetOrCreateScope(const CHIR::DebugLocation& position, llvm::BasicBlock& currentBB);
    llvm::DIScope* GetOrCreateLexicalScope(
        const CHIR::DebugLocation& position, llvm::BasicBlock& currentBB, std::vector<int>& scopeInfo);
    llvm::DILocation* HandleDefaultParamLocation(const CHIRExprWrapper& chirNode, llvm::DIScope* scope);
    llvm::DIFile* GetOrCreateFile(const CHIR::DebugLocation& position);
    llvm::DIType* GetOrCreateType(const CHIR::Type& ty, bool isReadOnly = false);
    void CreateParameter(const CHIR::Debug& debugNode, llvm::BasicBlock& curBB, bool pointerWrapper);
    llvm::DIType* CreateUnitType();
    llvm::DIType* CreateRefType();
    llvm::DIType* CreatePointerType(llvm::DIType* diType, uint64_t sizeInBits = 64uL);
    llvm::DIType* GetOrCreateObjectClassType();
    llvm::DIType* CreateClosureType(const CHIR::Type& ty);
    llvm::DIType* CreateVarFuncType(const CHIR::FuncType& funcTy);
    llvm::DISubroutineType* CreateFuncType(const CHIR::FuncType* func, bool isImported = false, bool hasThis = false);
    llvm::DISubroutineType* CreateDefaultFunctionType();
    llvm::DIType* CreateTupleType(const CHIR::TupleType& tupleTy, const CHIR::Type* boxTy);
    llvm::DIType* CreateArrayType(const CHIR::RawArrayType& arrTy);
    void CreateArrayMemberType(const CHIR::RawArrayType& arrTy, llvm::DICompositeType* fwdDecl,
        CodeGenDIVector3& elements, llvm::Type* arrayLayout);
    llvm::DIType* CreateVArrayType(const CHIR::VArrayType& vArrTy);
    llvm::DIType* CreateCPointerType(const CHIR::CPointerType& ptrTy);
    llvm::DIType* CreateCStringType(const CHIR::CStringType& cStringType);
    llvm::DIType* CreateTypeInfoMember(llvm::DICompositeType* fwdDecl, llvm::DIFile* diFile);
    void CreateInheritedInterface(
        CodeGenDIVector16& elements, llvm::DICompositeType* fwdDecl, const CHIR::CustomType& customTy);
    void CreateMethodType(
        const CHIR::CustomTypeDef& customDef, CodeGenDIVector16& elements, llvm::DICompositeType* fwdDecl);
    llvm::DIType* CreateClassType(const CHIR::ClassType& classTy);
    llvm::DIType* CreateGenericClassType(const CHIR::ClassType& classTy);
    llvm::DIType* CreateCapturedVars(const CHIR::ClassType& classTy);
    void CreateClassMemberType(const CHIR::ClassType& classTy, llvm::Type* classLayout, CodeGenDIVector16& elements,
        llvm::DICompositeType* fwdDecl);
    llvm::DIType* CreateStructType(const CHIR::StructType& structTy, const CHIR::Type* boxTy);
    void CreateStructMemberType(const CHIR::StructType& structTy, llvm::Type* cgType, CodeGenDIVector16& elements,
        llvm::DICompositeType* fwdDecl, const CHIR::Type* boxTy);
    void CreateCustomTypeMember(std::vector<CHIR::MemberVarInfo> members, CodeGenDIVector16& elements,
        llvm::DICompositeType* fwdDecl, llvm::DIFile* diFile);
    void CreateGetGenericFunc(
        const CHIR::CustomType& customTy, llvm::DICompositeType* fwdDecl, CodeGenDIVector16& elements);
    llvm::DIType* CreateEnumType(const CHIR::EnumType& enumTy, const CHIR::Type* boxTy);
    llvm::DICompositeType* GetOrCreateEnumCtorType(const CHIR::EnumType& enumTy);
    llvm::DICompositeType* CreateEnumOptionType(
        const CHIR::EnumType& enumTy, CodeGenDIVector16& enumMembers, const CHIR::Type* boxTy);
    llvm::DICompositeType* CreateBoxSelfOptionType(const CHIR::EnumType& enumTy, CodeGenDIVector16& enumMembers);
    llvm::DICompositeType* CreateNestedOptionType(
        const CHIR::EnumType& enumTy, CodeGenDIVector16& enumMembers, const CHIR::Type* boxTy);
    llvm::DICompositeType* CreateNonRefOptionType(
        const CHIR::EnumType& enumTy, CodeGenDIVector16& enumMembers, const CHIR::Type* boxTy);
    llvm::DICompositeType* CreateEnumWithArgsType(
        const CHIR::EnumType& enumTy, CodeGenDIVector16& enumMembers, const CHIR::Type* boxTy);
    llvm::DICompositeType* CreateTrivial(const CHIR::EnumType& enumTy, CodeGenDIVector16& enumMembers);
    llvm::DICompositeType* CreateEnumWithNonRefArgsType(const CHIR::EnumType& enumTy, CodeGenDIVector16& enumMembers);
    uint64_t GetBasicTypeSize(const CHIR::Type& ty) const;
    uint64_t GetTypeSize(llvm::Type* llvmType) const;
    uint64_t GetSizeInBits(llvm::DIType* type) const;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    llvm::DIType* CreateInterfaceType(const CHIR::ClassType& interfaceTy);
    llvm::DICompositeType* CreateEnumClassType(const CHIR::EnumType& enumTy, const CHIR::EnumCtorInfo& constructor,
        llvm::DICompositeType* subEnumType, std::size_t ctorIndex);
    llvm::DIType* CreateGenericType(const CHIR::GenericType& genericTy);
    uint64_t GetClassSize(llvm::Type* classLayout) const;
#endif

    bool IsOptionOrOptionLike(const CHIR::Type& ty)
    {
        if (!ty.IsEnum()) {
            return false;
        }
        return StaticCast<const CGEnumType*>(CGType::GetOrCreate(cgMod, &ty))->IsOptionLike();
    }

    bool IsOption(const CHIR::Type& ty)
    {
        if (!ty.IsEnum()) {
            return false;
        }
        return StaticCast<const CHIR::EnumType&>(ty).IsOption();
    }

    static std::string GenerateGenericFuncName(
        const std::string& funcName, const std::vector<CHIR::GenericType*>& genericTypeParams)
    {
        std::string typeName(funcName);
        if (!genericTypeParams.empty()) {
            typeName += "<";
            for (auto ty : genericTypeParams) {
                typeName += ty->GetSrcCodeIdentifier();
                typeName += ",";
            }
            typeName.pop_back();
            typeName += ">";
        }
        return typeName;
    }

    std::string GenerateTypeName(const CHIR::Type& type)
    {
        if (type.IsPrimitive()) {
            return type.ToString();
        }
        switch (type.GetTypeKind()) {
            case CHIR::Type::TypeKind::TYPE_RAWARRAY:
                return GenerateRawArrayName(StaticCast<const CHIR::RawArrayType&>(type));
            case CHIR::Type::TypeKind::TYPE_TUPLE:
                return GenerateTupleName(StaticCast<const CHIR::TupleType&>(type));
            case CHIR::Type::TypeKind::TYPE_FUNC:
                return GenerateFuncName(StaticCast<const CHIR::FuncType&>(type));
            case CHIR::Type::TypeKind::TYPE_VARRAY:
                return GenerateVArrayName(StaticCast<const CHIR::VArrayType&>(type));
            case CHIR::Type::TypeKind::TYPE_CPOINTER:
                return GenerateCPointerName(StaticCast<const CHIR::CPointerType&>(type));
            case CHIR::Type::TypeKind::TYPE_CSTRING:
                return type.ToString();
            case CHIR::Type::TypeKind::TYPE_CLASS:
            case CHIR::Type::TypeKind::TYPE_STRUCT:
            case CHIR::Type::TypeKind::TYPE_ENUM:
                return GenerateCustomTypeName(StaticCast<const CHIR::CustomType&>(type));
            case CHIR::Type::TypeKind::TYPE_GENERIC:
                return GenerateGenericTypeName(StaticCast<const CHIR::GenericType&>(type));
            case CHIR::Type::TypeKind::TYPE_REFTYPE:
                return GenerateTypeName(*StaticCast<const CHIR::RefType&>(type).GetBaseType());
            case CHIR::Type::TypeKind::TYPE_VOID:
            case CHIR::Type::TypeKind::TYPE_NOTHING:
            case CHIR::Type::TypeKind::TYPE_UNIT:
                return "()";
            default:
                CJC_ABORT();
                return "";
        }
    }

    std::string GenerateRawArrayName(const CHIR::RawArrayType& type)
    {
        std::stringstream typeName;
        for (auto i = 0u; i < type.GetDims(); i++) {
            typeName << "RawArray<";
        }
        typeName << GenerateTypeName(*type.GetElementType());
        std::string suffix(type.GetDims(), '>');
        typeName << suffix;
        return typeName.str();
    }

    std::string GenerateTupleName(const CHIR::TupleType& type)
    {
        std::string typeName = "Tuple<";
        for (auto arg : type.GetTypeArgs()) {
            typeName += GenerateTypeName(*arg);
            typeName += ",";
        }
        typeName.pop_back();
        typeName += ">";
        return typeName;
    }

    std::string GenerateFuncName(const CHIR::FuncType& type)
    {
        std::string typeName = "(";
        if (!type.GetParamTypes().empty()) {
            for (auto param : type.GetParamTypes()) {
                typeName += GenerateTypeName(*param);
                typeName += ",";
            }
            typeName.pop_back();
        }
        typeName += ")->";
        typeName += GenerateTypeName(*type.GetReturnType());
        return typeName;
    }

    std::string GenerateVArrayName(const CHIR::VArrayType& type)
    {
        std::stringstream typeName;
        typeName << "VArray<";
        typeName << GenerateTypeName(*StaticCast<const CHIR::VArrayType&>(type).GetElementType());
        typeName << ",$" << type.GetSize() << ">";
        return typeName.str();
    }

    std::string GenerateCPointerName(const CHIR::CPointerType& type)
    {
        std::string typeName = "CPointer<";
        for (auto arg : type.GetTypeArgs()) {
            typeName += GenerateTypeName(*arg);
            typeName += ",";
        }
        typeName.pop_back();
        typeName += ">";
        return typeName;
    }

    std::string GenerateCustomTypeName(const CHIR::CustomType& type, bool needPkgPrefix = true)
    {
        if (type.IsAutoEnvBase()) {
            return GenerateClosureTypeName(StaticCast<const CHIR::ClassType&>(type));
        }
        auto pkgName = type.GetCustomTypeDef()->GetPackageName();
        std::string typeName = (needPkgPrefix ? (pkgName + "::") :"") + CHIR::GetCustomTypeIdentifier(type);
        if (!type.GetTypeArgs().empty()) {
            typeName += "<";
            for (auto arg : type.GetTypeArgs()) {
                typeName += GenerateTypeName(*arg);
                typeName += ",";
            }
            typeName.pop_back();
            typeName += ">";
        }
        return typeName;
    }

    std::string GenerateClosureTypeName(const CHIR::ClassType& type)
    {
        std::string typeName = "(";
        auto [paramTypes, retType] = GetFuncTypeWithoutThisPtrFromAutoEnvBaseType(type);
        if (!paramTypes.empty()) {
            for (auto ty : paramTypes) {
                typeName += GenerateTypeName(*ty);
                typeName += ",";
            }
            typeName.pop_back();
        }
        typeName += ")->";
        typeName += GenerateTypeName(*retType);
        return typeName;
    }

    std::string GenerateGenericTypeName(const CHIR::GenericType& type)
    {
        return GENERIC_PREFIX + type.GetSrcCodeIdentifier();
    }
};
} // namespace CodeGen
} // namespace Cangjie
#endif // CANGJIE_DIBUILDER_H
