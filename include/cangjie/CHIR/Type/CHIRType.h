// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_CHIRTYPE_H
#define CANGJIE_CHIR_CHIRTYPE_H

#include "cangjie/AST/Types.h"
#include "cangjie/CHIR/AST2CHIR/AST2CHIRNodeMap.h"
#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/Type/Type.h"

#include <mutex>

namespace Cangjie::CHIR {

class CHIRTypeCache {
public:
    std::unordered_map<AST::Ty*, Type*>& typeMap; // AST::Type -> CHIR::Type
    // cache custom type(interface decl, class decl, struct decl, enum decl), except for extend decl
    AST2CHIRNodeMap<CustomTypeDef> globalNominalCache;
    explicit CHIRTypeCache(std::unordered_map<AST::Ty*, Type*>& typeMap) : typeMap(typeMap)
    {
    }
    explicit CHIRTypeCache(
        std::unordered_map<AST::Ty*, Type*>& typeMap, const AST2CHIRNodeMap<CustomTypeDef>& globalNominalCache)
        : typeMap(typeMap), globalNominalCache(globalNominalCache)
    {
    }
};
// typeTranslate
class CHIRType {
public:
    explicit CHIRType(CHIRBuilder& builder, CHIRTypeCache& typeCache) : builder(builder), chirTypeCache(typeCache)
    {
    }
    ~CHIRType() = default;

    /**
     * @brief Translates an AST type to a CHIR type.
     *
     * @param ty The AST type to be translated.
     * @return The translated CHIR type.
     */
    Type* TranslateType(AST::Ty& ty);
    
    /**
     * @brief Fills the generic argument types.
     *
     * @param ty The AST generics type to be processed.
     */
    void FillGenericArgType(AST::GenericsTy& ty);
    /* Notice that chirTypeCache.globalNominalCache is non-thread-safe, so SetGlobalNominalCache only be invoked
     * serially. Concurrent execution of SetGlobalNominalCache is not advisable.
     */
    void SetGlobalNominalCache(const AST::Decl& decl, CustomTypeDef& def)
    {
        chirTypeCache.globalNominalCache.Set(decl, def);
    }
    Ptr<CustomTypeDef> GetGlobalNominalCache(const AST::Decl& decl) const
    {
        return chirTypeCache.globalNominalCache.Get(decl);
    }
    Ptr<CustomTypeDef> TryGetGlobalNominalCache(const AST::Decl& decl) const
    {
        return chirTypeCache.globalNominalCache.TryGet(decl);
    }
    bool Has(const AST::Decl& decl) const
    {
        return chirTypeCache.globalNominalCache.Has(decl);
    }

    const std::unordered_map<const AST::Node*, CustomTypeDef*>& GetAllTypeDef() const
    {
        return chirTypeCache.globalNominalCache.GetALL();
    }
    std::unordered_map<AST::Ty*, Type*>& GetTypeMap() const
    {
        return chirTypeCache.typeMap;
    }
    const AST2CHIRNodeMap<CustomTypeDef>& GetGlobalNominalCache() const
    {
        return chirTypeCache.globalNominalCache;
    }

private:
    Type* TranslateTupleType(AST::TupleTy& tupleTy);
    Type* TranslateFuncType(const AST::FuncTy& fnTy);
    Type* TranslateStructType(AST::StructTy& structTy);
    Type* TranslateClassType(AST::ClassTy& classTy);
    Type* TranslateInterfaceType(AST::InterfaceTy& interfaceTy);
    Type* TranslateEnumType(AST::EnumTy& enumTy);
    Type* TranslateArrayType(AST::ArrayTy& arrayTy);
    Type* TranslateVArrayType(AST::VArrayTy& varrayTy);
    Type* TranslateCPointerType(AST::PointerTy& pointerTy);
    CHIRBuilder& builder;
    CHIRTypeCache& chirTypeCache;
    // mutex for translateType. TranslateType is recursive, so we use the recursive mutex.
    static std::recursive_mutex chirTypeMtx;
};
} // namespace Cangjie::CHIR

#endif