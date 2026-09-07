// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_CHIRTYPE_H
#define CANGJIE_CHIR_CHIRTYPE_H

#include "cangjie/AST/Types.h"
#include "cangjie/CHIR/AST2CHIR/AST2CHIRNodeMap.h"
#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/Type/Type.h"

#include <mutex>
#include <unordered_map>

namespace Cangjie::CHIR {

class CHIRTypeCache {
public:
    // Key is ModalTy: bare `T` is ModalTy{T, Mode::NOT}; `@local!` / `@local?` use FULL / HALF.
    std::unordered_map<AST::ModalTy, Type*>& typeMap;
    // cache custom type(interface decl, class decl, struct decl, enum decl), except for extend decl
    AST2CHIRNodeMap<CustomTypeDef> globalNominalCache;
    explicit CHIRTypeCache(std::unordered_map<AST::ModalTy, Type*>& typeMap) : typeMap(typeMap)
    {
    }
    explicit CHIRTypeCache(
        std::unordered_map<AST::ModalTy, Type*>& typeMap, const AST2CHIRNodeMap<CustomTypeDef>& globalNominalCache)
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
     * @brief Translates an AST ModalTy to a CHIR type.
     *
     * Bare `Ty*` is passed as ModalTy{ty} (Mode::NOT). Local forms use FULL / HALF.
     *
     * @param ty The AST type (with modal) to be translated.
     * @return The translated CHIR type.
     */
    Type* TranslateType(AST::ModalTy ty);

    /**
     * @brief Fill upper bounds for every GenericsTy ModalTy already present in typeMap
     * (including @local! / @local? variants created while translating FuncTy).
     */
    void FillAllGenericTypeUpperBounds();

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
    std::unordered_map<AST::ModalTy, Type*>& GetTypeMap() const
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
    Type* TranslateDataType(AST::Ty& ty);
    void FillGenericTypeUpperBounds(AST::ModalTy ty);

    CHIRBuilder& builder;
    CHIRTypeCache& chirTypeCache;
    // mutex for translateType. TranslateType is recursive, so we use the recursive mutex.
    static std::recursive_mutex chirTypeMtx;
};
} // namespace Cangjie::CHIR

#endif
