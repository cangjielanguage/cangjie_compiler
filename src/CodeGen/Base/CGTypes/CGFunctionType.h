// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CGFUNCTIONTYPE_H
#define CANGJIE_CGFUNCTIONTYPE_H

#include "Base/CGTypes/CGType.h"
#include "cangjie/CHIR/CHIRCasting.h"

namespace Cangjie {
namespace CodeGen {
class CGFunctionType : public CGType {
    friend class CGTypeMgr;
    friend class CGFunction;

public:
    bool HasSRet() const
    {
        return hasSRet;
    }

    const std::unordered_map<size_t, size_t>& GetStructParamNeedsBasePtrIndices() const
    {
        return structParamNeedsBasePtr;
    }

    llvm::FunctionType* GetLLVMFunctionType() const;

    inline const std::vector<size_t>& GetRealArgIndices() const
    {
        return realParamIndices;
    }

    inline const std::unordered_map<CHIR::GenericType*, size_t> GetGenericParamIndicesMap() const
    {
        return genericParamIndicesMap;
    }

    inline std::optional<size_t> GetOuterTypeInfoIndex() const
    {
        return outerTypeInfoIndex;
    }

    inline std::optional<size_t> GetThisTypeInfoIndex() const
    {
        return thisTypeInfoIndex;
    }

    CGType* GetParamType(size_t idx) const
    {
        auto tmp = GetContainedTypes();
        CJC_ASSERT(idx + 1 < tmp.size());
        return tmp[idx + 1];
    }

    bool IsCFunc() const
    {
        CJC_ASSERT(GetOriginal().IsFunc());
        return StaticCast<const CHIR::FuncType&>(GetOriginal()).IsCFunc();
    }

    void CalculateSizeAndAlign() override;

    bool HasBasePtr() const
    {
        CJC_ASSERT(hasBasePtr ? allowBasePtr : true);
        return hasBasePtr;
    }

    bool IsMethodType() const
    {
        return isMethod;
    }

    bool IsStaticMethodType() const
    {
        return isStaticMethod;
    }

protected:
    llvm::Type* GenLLVMType() override;
    void GenContainedCGTypes() override;

private:
    CGFunctionType() = delete;

    explicit CGFunctionType(
        CGModule& cgMod, CGContext& cgCtx, const CHIR::FuncType& chirType, const TypeExtraInfo& extraInfo = {});
    explicit CGFunctionType(
        CGModule& cgMod, CGContext& cgCtx, const CHIR::FuncBase& chirFunc, const TypeExtraInfo& extraInfo = {});

    llvm::Constant* GenSourceGenericOfTypeInfo() override;
    llvm::Constant* GenTypeArgsNumOfTypeInfo() override;
    llvm::Constant* GenTypeArgsOfTypeInfo() override;

    bool hasSRet{false};
    bool hasBasePtr{false};
    llvm::FunctionType* llvmFunctionType{nullptr};

    /**
     * Just like `structParamIndices`, but Key and Value are swapped. `real` means in llvm function parameter list.
     * Key (the index of the vector):     Index of the type in ParamTypes.
     * Value (the element of the vector): Index of parameter in LLVM raw function.
     */
    std::vector<size_t> realParamIndices;
    /**
     * Stores indices of struct type parameter in LLVM raw function and containedCGTypes.
     * Key:   Index of parameter in LLVM raw function that is of struct type of basePtr is required.
     * Value: Index of the corresponding CGType in `containedCGTypes` field.
     */
    std::unordered_map<size_t, size_t> structParamNeedsBasePtr;
    /**
     * `closureParamIndices` is used to let escape analysis know that
     * this formal parameter is a structure used to represent a closure.
     * Its escape attribute is bound between the whole structure and a
     * layer of elements in the structure, either escape together or do
     * not escape together.
     */
    std::vector<llvm::Metadata*> closureParamIndices;

    const CHIR::FuncBase* chirFunc{nullptr};
    bool allowBasePtr{true};
    bool isMethod{false};
    bool isStaticMethod{false};
    bool forWrapper{false};
    std::vector<CHIR::Type*> instantiatedParamTypes;
    std::unordered_map<CHIR::GenericType*, size_t> genericParamIndicesMap;
    std::optional<size_t> outerTypeInfoIndex{std::nullopt};
    std::optional<size_t> thisTypeInfoIndex{std::nullopt};
};
} // namespace CodeGen
} // namespace Cangjie
#endif // CANGJIE_CGFUNCTIONTYPE_H
