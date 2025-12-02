// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the class for determine the memory layout of Class/Interface.
 */

#ifndef CANGJIE_CG_EXTENSION_DEF_H
#define CANGJIE_CG_EXTENSION_DEF_H

#include "Base/CGTypes/CGType.h"

#include <queue>
#include <string>

#include "CGModule.h"
#include "IRBuilder.h"
#include "Utils/CGCommonDef.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/IR/Type/Type.h"

namespace Cangjie::CodeGen {
class CGExtensionDef {
public:
    // Indicates the interpretation of 'value' based on the `isTypeInfo` flag.
    // 1. When `isTypeInfo` is true: `value` represents `typeInfo`
    // 2. When `isTypeInfo` is false: `value` represents `typeArgs`
    // (Note: These typeArgs are typically passed by the runtime, not read from `typeInfo`)
    struct InnerTiInfo {
        llvm::Value* value;
        bool isTypeInfo;
    };

    explicit CGExtensionDef(CGModule& cgMod, const CHIR::CustomTypeDef& chirDef);

    std::pair<uint32_t, std::vector<std::pair<const CHIR::Type*, const CHIR::Type*>>> Emit();
    uint32_t GetStartIdx() const
    {
        return startIdxOfNonExternalExtensionDef;
    }
    const std::vector<std::pair<const CHIR::Type*, const CHIR::Type*>>& GetExtendInterfaces() const
    {
        return extendInterfaces;
    }
    static std::vector<llvm::Constant*> GetEmptyExtensionDefContent(CGModule& cgMod, const CHIR::Type& targetType);
    static bool CreateExtensionDefForType(CGModule& cgMod, const std::string& extensionDefName,
        const std::vector<llvm::Constant*>& content, const CHIR::ClassType& inheritedType,
        bool isForExternalType = false);
    static bool FoundGenericTypeAndCollectPath(
        const CHIR::Type& srcType, CHIR::GenericType& gt, std::vector<size_t>& path);
    static llvm::Value* GetTypeInfoWithPath(IRBuilder2& irBuilder, const CHIR::Type& type, llvm::Value* entryTypeArgs,
        std::queue<size_t>&& remainPath, std::unordered_map<const CHIR::Type*, InnerTiInfo>& innerTypeMap);

private:
    bool CreateExtensionDefForType(const CHIR::ClassType& inheritedType);
    /**
     * Returns a pair:
     * The first element is FuncPtr if the second is `false` or TypeInfo if the second is `true`.
     */
    std::pair<llvm::Constant*, bool> GenerateInterfaceFn(const CHIR::ClassType& inheritedType);
    llvm::Constant* GenerateFuncTableForType(const CHIR::VTableInType& vtableInType);
    llvm::Constant* GenerateWhereConditionFn();
    llvm::Value* CreateTypeComparison(
        IRBuilder2& irBuilder, llvm::Value* typeInfo, const CHIR::Type& staticType, const std::string& prefix);
    llvm::Value* CreateCompareArgs(IRBuilder2& irBuilder, llvm::Value* typeInfos,
        const std::vector<CHIR::Type*>& typeArgs, const std::string& prefix = "");
    llvm::Value* GetTypeInfoOfGeneric(IRBuilder2& irBuilder, CHIR::GenericType& gt);
    void CollectGenericParamIndicesMap();
    llvm::Value* CheckGenericParams(IRBuilder2& irBuilder, llvm::Value* retVal);
    static llvm::Constant* GetTargetType(CGModule& cgModule, const CHIR::Type& type);

private:
    CGModule& cgMod;
    CGContext& cgCtx;
    const CHIR::CustomTypeDef& chirDef;
    std::string typeMangle;
    std::string extendDefName;
    CHIR::Type* targetType{nullptr};
    bool isForExternalType = false;
    llvm::Constant* whereCondFn{nullptr};
    std::unordered_map<const CHIR::Type*, std::function<llvm::Value*(IRBuilder2&)>> genericParamsMap;
    /**
     * This map is creating & using during 'GenerateWhereConditionFn', and will be cleared at quit time.
     * Record every inner type args local value.
     * eg: for 'Array<(Option<T>, Int64)>' map will contains:
     * typeinfo of '(Option<T>, Int64)', typeinfo of 'Option<T>' in accessing order.
     */
    std::unordered_map<const CHIR::Type*, InnerTiInfo> innerTypeMap;
    /**
     * Map of the path to get generic type from 'extendedType'.
     * eg: decl is 'extend<T> A<Int64, Option<(Int64, T)>> {}', path for 'T' is '1, 0, 1'.
     */
    std::unordered_map<const CHIR::GenericType*, std::vector<size_t>> gtAccessPathMap;
    /**
     * Map of CHIR generic type's id to their runtime typeInfo.
     * In partial instantiated extend, one generic type may mapping to multiple runtime typeInfo. eg:
     * class A<T, K> {}
     * interface I{}
     * extend<T> A<T, T> <: I {}
     * For this extend, 'T' have two runtime typeInfo, and they must be same.
     */
    std::map<std::string, std::vector<llvm::Value*>> generatedGenerics;

    std::map<const CHIR::Type*, const CHIR::Type*> extendInterfaces2;

    std::vector<std::pair<const CHIR::Type*, const CHIR::Type*>> extendInterfaces;
    uint32_t startIdxOfNonExternalExtensionDef = 0;
};
} // namespace Cangjie::CodeGen

#endif // CANGJIE_CG_EXTENSION_DEF_H
