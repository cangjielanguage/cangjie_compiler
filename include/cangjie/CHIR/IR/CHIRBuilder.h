// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the CHIRBuilder class in CHIR.
 */

#ifndef CANGJIE_CHIR_CHIRBUILDER_H
#define CANGJIE_CHIR_CHIRBUILDER_H

#include "cangjie/CHIR/IR/CHIRContext.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/CHIR/Utils/ConstantUtils.h"

namespace Cangjie::CHIR {
class CHIRBuilder {
    friend class CHIRContext;
    CHIRBuilder& operator=(const CHIRBuilder&) = delete;
    CHIRBuilder() = delete;

public:
    explicit CHIRBuilder(CHIRContext& context, size_t threadIdx = 0);
    CHIRBuilder(const CHIRBuilder& builder)
        : context(builder.context), markAsCompileTimeValue(builder.markAsCompileTimeValue), threadIdx(builder.threadIdx)
    {
    }
    ~CHIRBuilder();

    // ===--------------------------------------------------------------------===//
    // Type API
    // ===--------------------------------------------------------------------===//
    template <typename TType, typename... Args> TType* GetType(Args&&... args)
    {
        return context.GetType<TType>(args...);
    }

    StructType* GetStructType(
        const std::string& package, const std::string& name, const std::vector<std::string>& genericType = {}) const
    {
        return context.GetStructType(package, name, genericType);
    }

    StructType* GetStringTy()
    {
        return context.GetStringTy();
    }

    VArrayType* GetZeroSizedTy()
    {
        return GetType<VArrayType>(GetInt8Ty(), 0U);
    }
    NothingType* GetNothingType() const
    {
        return context.GetNothingType();
    }
    UnitType* GetUnitTy() const
    {
        return context.GetUnitTy();
    }
    BooleanType* GetBoolTy() const
    {
        return context.GetBoolTy();
    }
    RuneType* GetRuneTy() const
    {
        return context.GetRuneTy();
    }
    IntType* GetInt8Ty() const
    {
        return context.GetInt8Ty();
    }
    IntType* GetInt16Ty() const
    {
        return context.GetInt16Ty();
    }
    IntType* GetInt32Ty() const
    {
        return context.GetInt32Ty();
    }
    IntType* GetInt64Ty() const
    {
        return context.GetInt64Ty();
    }
    IntType* GetIntNativeTy() const
    {
        return context.GetIntNativeTy();
    }
    IntType* GetUInt8Ty() const
    {
        return context.GetUInt8Ty();
    }
    IntType* GetUInt16Ty() const
    {
        return context.GetUInt16Ty();
    }
    IntType* GetUInt32Ty() const
    {
        return context.GetUInt32Ty();
    }
    IntType* GetUInt64Ty() const
    {
        return context.GetUInt64Ty();
    }
    IntType* GetUIntNativeTy() const
    {
        return context.GetUIntNativeTy();
    }
    FloatType* GetFloat16Ty() const
    {
        return context.GetFloat16Ty();
    }
    FloatType* GetFloat32Ty() const
    {
        return context.GetFloat32Ty();
    }
    FloatType* GetFloat64Ty() const
    {
        return context.GetFloat64Ty();
    }
    CStringType* GetCStringTy() const
    {
        return context.GetCStringTy();
    }
    VoidType* GetVoidTy() const
    {
        return context.GetVoidTy();
    }

    // Need refactor: object may be a new type and not inherited from Class
    void SetObjectTy(ClassType* ty)
    {
        context.SetObjectTy(ty);
    }
    ClassType* GetObjectTy() const
    {
        return context.GetObjectTy();
    }

    void SetAnyTy(ClassType* ty)
    {
        context.SetAnyTy(ty);
    }
    ClassType* GetAnyTy() const
    {
        return context.GetAnyTy();
    }

    // ===--------------------------------------------------------------------===//
    // BlockGroup API
    // ===--------------------------------------------------------------------===//
    BlockGroup* CreateBlockGroup(Func& func);

    // ===--------------------------------------------------------------------===//
    // Basic Block API
    // ===--------------------------------------------------------------------===//
    Block* CreateBlock(BlockGroup* parentGroup);
    std::pair<Block*, Block*> SplitBlock(const Expression& separator);

    // ===--------------------------------------------------------------------===//
    // Value API
    // ===--------------------------------------------------------------------===//
    // Note: we should be able to automatically infer the `TArgVal` here
    template <typename TLitVal, typename... Args> TLitVal* CreateLiteralValue(Args&&... args)
    {
        TLitVal* litVal = new TLitVal(std::forward<Args>(args)...);
        this->allocatedValues.push_back(litVal);
        return litVal;
    }
    Parameter* CreateParameter(Type* ty, const DebugLocation& loc, Func& parentFunc);
    Parameter* CreateParameter(Type* ty, const DebugLocation& loc, Lambda& parentLambda);
    GlobalVar* CreateGlobalVar(const DebugLocation& loc, RefType* ty, const std::string& mangledName,
        const std::string& srcCodeIdentifier, const std::string& rawMangledName, const std::string& packageName);
    // ===--------------------------------------------------------------------===//
    // Expression API
    // ===--------------------------------------------------------------------===//
    /** @brief Return a Expression.*/
    template <typename TExpr, typename... Args> TExpr* CreateExpression(Type* resultTy, Args&&... args)
    {
        TExpr* expr = new TExpr(std::forward<Args>(args)...);
        this->allocatedExprs.push_back(expr);
        CJC_NULLPTR_CHECK(expr->GetTopLevelFunc());
        std::string idStr = "%" + std::to_string(expr->GetTopLevelFunc()->GenerateLocalId());
        LocalVar* res = new LocalVar(resultTy, idStr, expr);
        this->allocatedValues.push_back(res);
        return expr;
    }

    template <typename TExpr, typename... Args>
    TExpr* CreateExpression(const DebugLocation& loc, Type* resultTy, Args&&... args)
    {
        auto expr = CreateExpression<TExpr>(resultTy, args...);
        expr->SetDebugLocation(loc);
        return expr;
    }

    template <typename TExpr, typename... Args>
    TExpr* CreateExpression(
        const DebugLocation& locForWarning, const DebugLocation& loc, Type* resultTy, Args&&... args)
    {
        auto expr = CreateExpression<TExpr>(resultTy, args...);
        expr->SetDebugLocation(loc);
        expr->template Set<DebugLocationInfoForWarning>(locForWarning);
        return expr;
    }

    /** @brief Return a Terminator.*/
    template <typename TExpr, typename... Args> TExpr* CreateTerminator(Args&&... args)
    {
        static_assert(std::is_base_of_v<Terminator, TExpr>);
        TExpr* expr = new TExpr(std::forward<Args>(args)...);
        this->allocatedExprs.push_back(expr);
        return expr;
    }

    template <typename TExpr, typename... Args> TExpr* CreateTerminator(const DebugLocation& loc, Args&&... args)
    {
        auto expr = CreateTerminator<TExpr>(std::forward<Args>(args)...);
        expr->SetDebugLocation(loc);
        return expr;
    }
    template <typename TExpr, typename... Args> TExpr* CreateTerminator(DebugLocation& loc, Args&&... args)
    {
        return CreateTerminator<TExpr>(std::as_const(loc), std::forward<Args>(args)...);
    }
    template <typename TExpr, typename... Args> TExpr* CreateTerminator(DebugLocation&& loc, Args&&... args)
    {
        auto expr = CreateTerminator<TExpr>(std::forward<Args>(args)...);
        expr->SetDebugLocation(std::move(loc));
        return expr;
    }

    template <typename TLitVal, typename... Args>
    Constant* CreateConstantExpression(Type* resultTy, Block* parentBlock, Args&&... args)
    {
        TLitVal* litVal = CreateLiteralValue<TLitVal>(resultTy, std::forward<Args>(args)...);
        Constant* expr = new Constant(litVal, parentBlock);
        this->allocatedExprs.push_back(expr);
        CJC_NULLPTR_CHECK(parentBlock->GetTopLevelFunc());
        std::string idStr = "%" + std::to_string(parentBlock->GetTopLevelFunc()->GenerateLocalId());
        LocalVar* res = new LocalVar(resultTy, idStr, expr);
        this->allocatedValues.push_back(res);
        return expr;
    }

    template <typename TLitVal, typename... Args>
    Constant* CreateConstantExpression(const DebugLocation& loc, Type* resultTy, Block* parentBlock, Args&&... args)
    {
        auto expr = CreateConstantExpression<TLitVal>(resultTy, parentBlock, args...);
        expr->SetDebugLocation(loc);
        return expr;
    }

    Func* CreateFunc(const DebugLocation& loc, FuncType* funcTy, const std::string& mangledName,
        const std::string& srcCodeIdentifier, const std::string& rawMangledName, const std::string& packageName,
        const std::vector<GenericType*>& genericTypeParams = {});
    // ===--------------------------------------------------------------------===//
    // StructDef API
    // ===--------------------------------------------------------------------===//
    StructDef* CreateStruct(const DebugLocation& loc, const std::string& srcCodeIdentifier,
        const std::string& mangledName, const std::string& pkgName, bool isImported);
    // ===--------------------------------------------------------------------===//
    // ClassDef API
    // ===--------------------------------------------------------------------===//
    ClassDef* CreateClass(const DebugLocation& loc,
        const std::string& srcCodeIdentifier, const std::string& mangledName, const std::string& pkgName, bool isClass,
        bool isImported);
    // ===--------------------------------------------------------------------===//
    // EnumDef API
    // ===--------------------------------------------------------------------===//
    EnumDef* CreateEnum(const DebugLocation& loc, const std::string& srcCodeIdentifier, const std::string& mangledName,
        const std::string& pkgName, bool isImported, bool isNonExhaustive);
    // ===--------------------------------------------------------------------===//
    // ExtendDef API
    // ===--------------------------------------------------------------------===//
    ExtendDef* CreateExtend(const DebugLocation& loc, const std::string& mangledName,
    const std::string& pkgName, bool isImported,
        const std::vector<GenericType*> genericParams = {});
    // ===--------------------------------------------------------------------===//
    // Package API
    // ===--------------------------------------------------------------------===//
    Package* CreatePackage(const std::string& name);
    Package* GetCurPackage() const;

    template <typename T>
    T* CreateImportedVarOrFunc(Type* ty, const std::string& mangledName, const std::string& srcCodeIdentifier,
        const std::string& rawMangledName, const std::string& srcPackageName,
        const std::vector<GenericType*>& genericTypeParams = {}, bool addToIR = true)
    {
        T* importDecl = nullptr;
        if constexpr (std::is_same_v<T, ImportedFunc>) {
            importDecl = new ImportedFunc(ty, GLOBAL_VALUE_PREFIX + mangledName,
                srcCodeIdentifier, rawMangledName, srcPackageName, genericTypeParams);
        } else {
            importDecl = new ImportedVar(ty, GLOBAL_VALUE_PREFIX + mangledName,
                srcCodeIdentifier, rawMangledName, srcPackageName);
        }
        CJC_NULLPTR_CHECK(importDecl);
        importDecl->EnableAttr(Attribute::IMPORTED);
        this->allocatedValues.push_back(importDecl);
        if (context.GetCurPackage() != nullptr && addToIR) {
            context.GetCurPackage()->AddImportedVarAndFunc(importDecl);
        }
        return importDecl;
    }

    void SetCompileTimeValueMark(bool val)
    {
        markAsCompileTimeValue = val;
    }

    bool GetCompileTimeValueMark() const
    {
        return markAsCompileTimeValue;
    }

    CHIRContext& GetChirContext()
    {
        return context;
    }

    size_t GetAllNodesNum() const
    {
        return context.GetAllNodesNum();
    }

    size_t GetTypesNum() const
    {
        return context.dynamicAllocatedTys.size();
    }

    void MergeAllocatedInstance()
    {
        context.GetAllocatedExprs().insert(
            context.GetAllocatedExprs().end(), allocatedExprs.begin(), allocatedExprs.end());
        context.GetAllocatedValues().insert(
            context.GetAllocatedValues().end(), allocatedValues.begin(), allocatedValues.end());
        context.GetAllocatedBlockGroups().insert(context.GetAllocatedBlockGroups().end(),
            allocatedBlockGroups.begin(), allocatedBlockGroups.end());
        context.GetAllocatedBlocks().insert(
            context.GetAllocatedBlocks().end(), allocatedBlocks.begin(), allocatedBlocks.end());
        context.GetAllocatedStructs().insert(
            context.GetAllocatedStructs().end(), allocatedStructs.begin(), allocatedStructs.end());
        context.GetAllocatedClasses().insert(
            context.GetAllocatedClasses().end(), allocatedClasses.begin(), allocatedClasses.end());
        context.GetAllocatedEnums().insert(
            context.GetAllocatedEnums().end(), allocatedEnums.begin(), allocatedEnums.end());
        context.GetAllocatedExtends().insert(
            context.GetAllocatedExtends().end(), allocatedExtends.begin(), allocatedExtends.end());
        allocatedExprs.clear();
        allocatedValues.clear();
        allocatedBlockGroups.clear();
        allocatedBlocks.clear();
        allocatedStructs.clear();
        allocatedClasses.clear();
        allocatedEnums.clear();
        allocatedExtends.clear();
    }

    std::unordered_set<CustomType*> GetAllCustomTypes() const;
    std::unordered_set<GenericType*> GetAllGenericTypes() const;

    void EnableIRCheckerAfterPlugin();
    void DisableIRCheckerAfterPlugin();
    bool IsEnableIRCheckerAfterPlugin() const;

private:
    CHIRContext& context;

    // A flag indicate if the created CHIR value/expression should be marked as compile time value for const evaluation
    bool markAsCompileTimeValue = false;
    bool enableIRCheckerAfterPlugin = true;
    size_t threadIdx;
    std::vector<Expression*> allocatedExprs;
    std::vector<Value*> allocatedValues;
    std::vector<BlockGroup*> allocatedBlockGroups;
    std::vector<Block*> allocatedBlocks;
    std::vector<StructDef*> allocatedStructs;
    std::vector<ClassDef*> allocatedClasses;
    std::vector<EnumDef*> allocatedEnums;
    std::vector<ExtendDef*> allocatedExtends;
};
} // namespace Cangjie::CHIR
#endif // CANGJIE_CHIR_CHIRBUILDER_H
