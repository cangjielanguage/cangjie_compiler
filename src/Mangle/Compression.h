// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Mangle Compression.
 */

#ifndef CANGJIE_MANGLE_COMPRESSION_H
#define CANGJIE_MANGLE_COMPRESSION_H

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Cangjie::Compression {
enum class EntityType {
    NAME,
    PACKAGE,
    ANONYMOUS,
    GENERAL_FUNCTION,
    GENERIC_FUNCTION,
    GENERIC_DATA,
    EXTEND,
    LAMBDA,
    ERROR_ENTITY_TYPE
};

enum class BaseType {
    PRIMITIVE_TYPE,
    ARRAY_TYPE,
    RAWARRAY_TYPE,
    VARRAY_TYPE,
    TUPLE_TYPE,
    CPOINTER_TYPE,
    CSTRING_TYPE,
    GENERIC_TYPE,
    ENUM_TYPE,
    STRUCT_TYPE,
    CLASS_TYPE,
    FUNCTION_TYPE,
    ERROR_BASE_TYPE
};

struct Entity {
    explicit Entity(const std::string& mangledName, EntityType entityTy) : mangledName(mangledName), entityTy(entityTy)
    {}
    std::string mangledName;
    EntityType entityTy;
    virtual ~Entity() = default;
};

struct CJType {
    explicit CJType(const std::string& mangledName, BaseType baseTy) : mangledName(mangledName),
        baseTy(baseTy)
    {
        this->mangledName = mangledName;
    }
    virtual ~CJType() = default;
    std::string mangledName = "";
    BaseType baseTy = BaseType::ERROR_BASE_TYPE;
};

struct FunctionEntity : public Entity {
    explicit FunctionEntity(const std::string& mangledName, EntityType entityTy,
        std::vector<std::unique_ptr<CJType>>& paramTys, std::vector<std::unique_ptr<CJType>>& genericTys)
        : Entity(mangledName, entityTy)
    {
            this->paramTys = std::move(paramTys);
            this->genericTys = std::move(genericTys);
    }
    std::vector<std::unique_ptr<CJType>> paramTys;
    std::vector<std::unique_ptr<CJType>> genericTys;
};

struct DataEntity : public Entity {
    explicit DataEntity(const std::string& mangledName, EntityType entityTy,
        std::vector<std::unique_ptr<CJType>>& genericTys)
        : Entity(mangledName, entityTy)
    {
            this->genericTys = std::move(genericTys);
    }
    std::vector<std::unique_ptr<CJType>> genericTys;
};

struct ExtendEntity : public Entity {
    explicit ExtendEntity(const std::string& mangledName, EntityType entityTy, std::unique_ptr<CJType> extendTy,
        const std::string& fileId, const std::string& localId) : Entity(mangledName, entityTy), fileId(fileId),
        localId(localId)
    {
        this->extendTy = std::move(extendTy);
    }
    std::unique_ptr<CJType> extendTy;
    std::string fileId;
    std::string localId;
};

struct CompositeType : public CJType {
    explicit CompositeType(const std::string& mangledName, BaseType baseTy,
        std::vector<std::unique_ptr<CJType>>& genericTys, const std::string& pkg, const std::string& name)
        : CJType(mangledName, baseTy), pkg(pkg), name(name)
    {
            this->genericTys = std::move(genericTys);
    }
    std::vector<std::unique_ptr<CJType>> genericTys;
    std::string pkg;
    std::string name;
};

struct FunctionType : public CJType {
    explicit FunctionType(const std::string& mangledName, BaseType baseTy, std::unique_ptr<CJType> retTy,
        std::vector<std::unique_ptr<CJType>>& paramTys) : CJType(mangledName, baseTy)
    {
            this->retTy = std::move(retTy);
            this->paramTys = std::move(paramTys);
    }
    std::unique_ptr<CJType> retTy;
    std::vector<std::unique_ptr<CJType>> paramTys;
};

struct TupleType : public CJType {
    explicit TupleType(const std::string& mangledName, BaseType baseTy,
        std::vector<std::unique_ptr<CJType>>& elementTys) : CJType(mangledName, baseTy)
    {
            this->elementTys = std::move(elementTys);
    }
    std::vector<std::unique_ptr<CJType>> elementTys;
};

/**
 * @brief Check whether the mangled name is variable decl.
 *
 * @param mangled The mangled name.
 * @return bool If yes, true is returned. Otherwise, false is returned.
 */
bool IsVarDeclEncode(std::string& mangled);

/**
 * @brief Check whether the mangled name is default param function.
 *
 * @param mangled The mangled name.
 * @return bool If yes, true is returned. Otherwise, false is returned.
 */
bool IsDefaultParamFuncEncode(std::string& mangled);

/**
 * @brief Get the index at the end of the type.
 *
 * @param mangled The mangled name.
 * @param tys the vector to save pointers of CJType.
 * @param isCompressed Whether the mangled name has been compressed.
 * @param idx The start index of the mangled name.
 * @return size_t The end index of the mangled name.
 */
size_t ForwardType(std::string& mangled, std::vector<std::unique_ptr<CJType>>& tys, bool& isCompressed,
    size_t idx = 0);

/**
 * @brief Get the index at the end of the types.
 *
 * @param mangled The mangled name.
 * @param tys the vector to save pointers of CJType.
 * @param isCompressed Whether the mangled name has been compressed.
 * @param startId The start index of the mangled name.
 * @return size_t The end index of the mangled name.
 */
size_t ForwardTypes(std::string& mangled, std::vector<std::unique_ptr<CJType>>& tys, bool& isCompressed,
    size_t startId = 0);

/**
 * @brief Get the index at the end of the name.
 *
 * @param mangled The mangled name.
 * @param isCompressed Whether the mangled name has been compressed.
 * @param idx The start index of the mangled name.
 * @return size_t The end index of the mangled name.
 */
size_t ForwardName(std::string& mangled, bool& isCompressed, size_t idx = 0);

/**
 * @brief Get the index at the end of the number.
 *
 * @param mangled The mangled name.
 * @param idx The start index of the mangled name.
 * @return size_t The end index of the mangled name.
 */
size_t ForwardNumber(std::string& mangled, size_t idx = 0);

/**
 * @brief Main entry of Mangler compression.
 *
 * @param mangled The mangled name.
 * @param isType Whether the mangled name is type.
 * @return std::string The mangled name after compression.
 */
std::string CJMangledCompression(const std::string mangled, bool isType = false);

/**
 * @brief Try parse path of the mangled name to generate entity vector.
 *
 * @param mangled The mangled name.
 * @param rest The mangled name after removing entities string.
 * @param isCompressed Whether the mangled name has been compressed.
 * @return std::vector<std::unique_ptr<Entity>> The entities.
 */
std::vector<std::unique_ptr<Entity>> TryParsePath(std::string& mangled, std::string& rest, bool& isCompressed);

/**
 * @brief Generate variable decl compressed mangled name.
 *
 * @param entities It belongs to prefix path of variable decl.
 * @param mangled The mangled name.
 * @param compressed The compressed mangled name to be modified.
 * @return bool If generate compressed mangled name success, true is returned. Otherwise, false is returned.
 */
bool SpanningVarDeclTree(std::vector<std::unique_ptr<Entity>>& entities, std::string& mangled,
    std::string& compressed);

/**
 * @brief Generate function decl compressed mangled name.
 *
 * @param entities It belongs to prefix path of function decl.
 * @param mangled The mangled name.
 * @param compressed The compressed mangled name to be modified.
 * @return bool If generate compressed mangled name success, true is returned. Otherwise, false is returned.
 */
bool SpanningFuncDeclTree(std::vector<std::unique_ptr<Entity>>& entities, std::string& mangled,
    std::string& compressed);

/**
 * @brief Generate default param function decl compressed mangled name.
 *
 * @param entities It belongs to prefix path of default param function decl.
 * @param mangled The mangled name.
 * @param compressed The compressed mangled name to be modified.
 * @return bool If generate compressed mangled name success, true is returned. Otherwise, false is returned.
 */
bool SpanningDefaultParamFuncDeclTree(std::vector<std::unique_ptr<Entity>>& entities, std::string& mangled,
    std::string& compressed);

/**
 * @brief Generate compressed mangled name via recursion entity.
 *
 * @param entity Recursed entity.
 * @param treeIdMap The map which key is substring of mangled name, value is compressed index.
 * @param mid The treeIdMap size.
 * @param compressed The compressed mangled name.
 */
void RecursionEntity(const std::unique_ptr<Entity>& entity, std::unordered_map<std::string, size_t>& treeIdMap,
    size_t& mid, std::string& compressed);

/**
 * @brief Generate compressed mangled name via recursion type.
 *
 * @param ty Recursed type.
 * @param treeIdMap The map which key is substring of mangled name, value is compressed index.
 * @param mid The treeIdMap size.
 * @param compressed The compressed mangled name.
 */
void RecursionType(const std::unique_ptr<CJType>& ty, std::unordered_map<std::string, size_t>& treeIdMap, size_t& mid,
    std::string& compressed, bool isReplaced);

/**
 * @brief Helper function for recursive unit, which is used to update treeIdMap and compressed.
 *
 * @param name The string which may be added to treeIdMap.
 * @param treeIdMap The map which may be updated.
 * @param mid The treeIdMap size.
 * @param compressed The compressed mangled name which may be updated.
 * @param isReplaced Whether the string has been traversed.
 * @param isLeaf Whether the string is leaf.
 * @return bool The end index of the mangled name.
 */
bool RecursionHelper(std::string& name, std::unordered_map<std::string, size_t>& treeIdMap, size_t& mid,
    std::string& compressed, bool isReplaced, bool isLeaf = true);
}  // namespace Cangjie::Compression
#endif // CANGJIE_MANGLE_COMPRESSION_H