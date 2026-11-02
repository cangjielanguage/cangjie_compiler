// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the CHIRContext class in CHIR.
 */

#ifndef CANGJIE_CHIR_CHIRCONTEXT_H
#define CANGJIE_CHIR_CHIRCONTEXT_H

#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/IR/Value/Value.h"

#include <atomic>
#include <vector>
#include <mutex>
#include <unordered_set>

namespace Cangjie::CHIR {
class Type;
class Expression;
class Package;
class BlockGroup;
class Block;
class ExtendDef;

struct TypePtrHash {
    size_t operator()(const Type* ptr) const;
};

struct TypePtrEqual {
    bool operator()(const Type* ptr1, const Type* ptr2) const;
};

class CHIRContext {
    CHIRContext(const CHIRContext&) = delete;
    CHIRContext& operator=(const CHIRContext&) = delete;

public:
    std::unordered_set<Type*, TypePtrHash, TypePtrEqual> constAllocatedTys;
    std::unordered_set<Type*, TypePtrHash, TypePtrEqual> dynamicAllocatedTys;
    explicit CHIRContext(std::unordered_map<unsigned int, std::string>* fnMap = nullptr, size_t threadsNum = 1);
    ~CHIRContext();

    Package* GetCurPackage() const;
    void SetCurPackage(Package* pkg);
    // Register Source Name
    void RegisterSourceFileName(unsigned fileId, const std::string& fileName) const;
    const std::string& GetSourceFileName(unsigned fileId) const;
    const std::unordered_map<unsigned int, std::string>* GetFileNameMap() const;
    /** @brief Move the dynamicAllocatedTys in CHIRContext into constAllocatedTys of CHIRContext in this.*/
    void MergeTypes();
    void SetFileNameMap(std::unordered_map<unsigned int, std::string>* fnMap)
    {
        fileNameMap = fnMap;
    }
    void SetThreadNum(size_t num)
    {
        threadsNum = num;
    }

    Type* WithModal(Type* ty, ModalInfo modal);

    // ===--------------------------------------------------------------------===//
    // Type API
    // ===--------------------------------------------------------------------===//
    /** @brief Return a type.*/
    template <typename TType, typename... Args> TType* GetType(Args&&... args)
    {
        TType checkTy(std::forward<Args>(args)...);
        auto constIt = this->constAllocatedTys.find(&checkTy);
        if (constIt == this->constAllocatedTys.end()) {
            std::unique_lock<std::mutex> lock(CHIRContext::dynamicAllocatedTysMtx);
            auto dynamicIt = this->dynamicAllocatedTys.find(&checkTy);
            if (dynamicIt == this->dynamicAllocatedTys.end()) {
                auto ty = new TType(std::forward<Args>(args)...);
                this->dynamicAllocatedTys.insert(ty);
                return ty;
            } else {
                return static_cast<TType*>(*dynamicIt);
            }
        } else {
            return static_cast<TType*>(*constIt);
        }
    }

    StructType* GetStructType(
        const std::string& package, const std::string& name, const std::vector<std::string>& genericType = {}) const;

    StructType* GetStringTy() const;

    NothingType* GetNothingType() const
    {
        return nothingTy;
    }
    UnitType* GetUnitTy() const
    {
        return unitTy;
    }
    BooleanType* GetBoolTy() const
    {
        return boolTy;
    }
    RuneType* GetRuneTy() const
    {
        return runeTy;
    }
    IntType* GetInt8Ty() const
    {
        return int8Ty;
    }
    IntType* GetInt16Ty() const
    {
        return int16Ty;
    }
    IntType* GetInt32Ty() const
    {
        return int32Ty;
    }
    IntType* GetInt64Ty() const
    {
        return int64Ty;
    }
    IntType* GetIntNativeTy() const
    {
        return intNativeTy;
    }
    IntType* GetUInt8Ty() const
    {
        return uint8Ty;
    }
    IntType* GetUInt16Ty() const
    {
        return uint16Ty;
    }
    IntType* GetUInt32Ty() const
    {
        return uint32Ty;
    }
    IntType* GetUInt64Ty() const
    {
        return uint64Ty;
    }
    IntType* GetUIntNativeTy() const
    {
        return uintNativeTy;
    }
    FloatType* GetFloat16Ty() const
    {
        return float16Ty;
    }
    FloatType* GetFloat32Ty() const
    {
        return float32Ty;
    }
    FloatType* GetFloat64Ty() const
    {
        return float64Ty;
    }
    CStringType* GetCStringTy() const
    {
        return cstringTy;
    }
    VoidType* GetVoidTy() const
    {
        return voidTy;
    }
    // Need refactor: object may be a new type and not inherited from Class
    void SetObjectTy(ClassType* ty)
    {
        objectTy = ty;
    }
    ClassType* GetObjectTy() const
    {
        CJC_ASSERT(objectTy != nullptr);
        return objectTy;
    }

    void SetAnyTy(ClassType* ty)
    {
        anyTy = ty;
    }

    ClassType* GetAnyTy() const
    {
        CJC_ASSERT(anyTy != nullptr);
        return anyTy;
    }

    // get enum selector type based on type kind
    Type* ToSelectorType(Type::TypeKind kind) const;

    size_t GetAllNodesNum() const
    {
        size_t num = 0;
        num += allocatedValues.size();
        num += allocatedStructs.size();
        num += allocatedClasses.size();
        num += allocatedEnums.size();
        num += allocatedExtends.size();
        for (auto& [bg, ptrs] : allocatedPtrInFuncOrLambda) {
            num += ptrs.size() + 1;
        }
        return num;
    }
    size_t GetTypesNum() const
    {
        return dynamicAllocatedTys.size();
    }

    std::vector<Value*>& GetAllocatedValues()
    {
        return allocatedValues;
    }

    std::vector<StructDef*>& GetAllocatedStructs()
    {
        return allocatedStructs;
    }

    std::vector<ClassDef*>& GetAllocatedClasses()
    {
        return allocatedClasses;
    }

    std::vector<EnumDef*>& GetAllocatedEnums()
    {
        return allocatedEnums;
    }

    std::vector<ExtendDef*>& GetAllocatedExtends()
    {
        return allocatedExtends;
    }

    void DeleteAllocatedInstance(std::vector<size_t>& idxs);
    void DeleteAllocatedTys();
    void MergeAllocatedPtrInFuncOrLambda(std::unordered_map<BlockGroup*, std::vector<Base*>>& input);
    void FreeMemoryInFunc(BlockGroup& funcBody);
    void FreeWholePackage();
    void Init();
    ClassType* SearchObjectTyInPackage() const;
    ClassType* SearchAnyTyInPackage() const;

private:
    /*
     * @brief Cached Pointer of allocated instance in CHIR.
     */
    Package* curPackage;

    /* The file name string pool for debug location: fileID map to source path */
    std::unordered_map<unsigned int, std::string>* fileNameMap;
    /*
     * @brief Cached Pointer of allocated instance in CHIR.
     */
    std::vector<Value*> allocatedValues;
    std::vector<StructDef*> allocatedStructs;
    std::vector<ClassDef*> allocatedClasses;
    std::vector<EnumDef*> allocatedEnums;
    std::vector<ExtendDef*> allocatedExtends;
    std::unordered_set<CustomTypeDef*> allocatedCustomDefs;
    std::unordered_map<BlockGroup*, std::vector<Base*>> allocatedPtrInFuncOrLambda;

    static std::mutex allocatedListMtx;
    UnitType* unitTy{nullptr};
    BooleanType* boolTy{nullptr};
    RuneType* runeTy{nullptr};
    NothingType* nothingTy{nullptr};
    IntType *int8Ty{nullptr}, *int16Ty{nullptr}, *int32Ty{nullptr}, *int64Ty{nullptr}, *intNativeTy{nullptr};
    IntType *uint8Ty{nullptr}, *uint16Ty{nullptr}, *uint32Ty{nullptr}, *uint64Ty{nullptr}, *uintNativeTy{nullptr};
    FloatType *float16Ty{nullptr}, *float32Ty{nullptr}, *float64Ty{nullptr};
    CStringType* cstringTy{nullptr};
    ClassType* objectTy{nullptr};
    ClassType* anyTy{nullptr};
    VoidType* voidTy{nullptr};
    static std::mutex dynamicAllocatedTysMtx;

    size_t threadsNum;

    template <typename T> static void SafeDelete(T* p) noexcept
    {
        if (p == nullptr) {
            return;
        }
#ifndef CANGJIE_ENABLE_GCOV
        try {
#endif
            delete p;
#ifndef CANGJIE_ENABLE_GCOV
        } catch (...) {
            // Destructors should not throw; continue releasing other resources.
        }
#endif
    }
};
} // namespace Cangjie::CHIR
#endif // CANGJIE_CHIR_CHIRCONTEXT_H