// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Utils/ModalWrite.h"

#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/Expression/Expression.h"
#include "cangjie/CHIR/IR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"

using namespace Cangjie;
using namespace Cangjie::CHIR;
using namespace Cangjie::CodeGen;

namespace {
/// The field that a store actually writes, after walking the full field path. Mirrors the
/// per-level walk in CustomType::GetInstMemberTypeByPathCheckingReadOnly, but also tracks the
/// modal of the object that *directly* owns the final field -- which matters for nested paths
/// where an intermediate `demode` member resets the modal back to `~local` (heap).
struct FinalMember {
    /// Modal of the object that directly owns the written field. For `o.inner.data` this is the
    /// modal of `o.inner`, not of `o`.
    CHIR::ModalInfo ownerModal;
    /// Effective type of the written field (its modal already applied).
    const Type* memberType = nullptr;
    /// Whether the written field is declared `demode`.
    bool isDemode = false;
};

/// Resolves the object type a store writes into, together with the field path, for the two
/// shapes CodeGen can emit a member write barrier for:
///   - StoreElementRef(value, location, path)
///   - Store(value, %p) where %p = GetElementRef(location, path)
/// Returns false when \p expr is not a store to an instance member variable.
bool ResolveMemberStore(const Expression& expr, const Type*& objectType, const std::vector<uint64_t>*& path)
{
    const Value* location = nullptr;
    if (auto storeElemRef = DynamicCast<const StoreElementRef*>(&expr)) {
        location = storeElemRef->GetBase();
        path = &storeElemRef->GetPath();
    } else if (auto store = DynamicCast<const Store*>(&expr)) {
        auto localVar = DynamicCast<const LocalVar*>(store->GetLocation());
        if (localVar == nullptr) {
            return false;
        }
        auto getElemRef = DynamicCast<const GetElementRef*>(localVar->GetExpr());
        if (getElemRef == nullptr) {
            return false;
        }
        location = getElemRef->GetBase();
        path = &getElemRef->GetPath();
    } else {
        return false;
    }

    if (location == nullptr || path->empty()) {
        return false;
    }
    auto locationType = location->GetType();
    if (locationType == nullptr || !locationType->IsRef()) {
        return false;
    }
    objectType = locationType->StripAllRefs();
    return objectType != nullptr;
}

/// Walks \p path from \p rootType down to the field that is written. Returns false when the path
/// leaves the nominal-type world (e.g. through a not-yet-instantiated generic member).
bool ResolveFinalMember(
    const CustomType& rootType, const std::vector<uint64_t>& path, CHIRBuilder& builder, FinalMember& result)
{
    auto current = &rootType;
    for (size_t i = 0; i < path.size(); ++i) {
        auto def = current->GetCustomTypeDef();
        if (def == nullptr) {
            return false;
        }
        // NOTE: GetAllInstanceVars() returns by value, so nothing may point into it.
        auto instanceVars = def->GetAllInstanceVars();
        auto memberTys = const_cast<CustomType*>(current)->GetInstantiatedMemberTys(builder);
        auto index = path[i];
        if (index >= instanceVars.size() || index >= memberTys.size()) {
            return false;
        }

        if (i + 1 == path.size()) {
            result.ownerModal = current->GetModalInfo();
            result.memberType = memberTys[index];
            result.isDemode = instanceVars[index].TestAttr(Attribute::DEMODE);
            return result.memberType != nullptr;
        }

        auto nextType = memberTys[index]->StripAllRefs();
        if (nextType == nullptr || !nextType->IsNominal()) {
            return false;
        }
        current = static_cast<const CustomType*>(nextType);
    }
    return false;
}
} // namespace

ModalWriteKind CodeGen::ClassifyModalWrite(CHIRBuilder& builder, const Expression& store)
{
    const Type* objectType = nullptr;
    const std::vector<uint64_t>* path = nullptr;
    if (!ResolveMemberStore(store, objectType, path)) {
        return ModalWriteKind::NONE;
    }

    // Only nominal types have instance member variables addressable by path. Generic
    // (not-yet-instantiated) receivers are not handled here;
    if (!objectType->IsNominal()) {
        return ModalWriteKind::NONE;
    }

    FinalMember member;
    if (!ResolveFinalMember(*static_cast<const CustomType*>(objectType), *path, builder, member)) {
        return ModalWriteKind::NONE;
    }

    // A reference-typed member is modelled as RefType(T @m), i.e. the modal sits on the pointee
    // rather than on the RefType wrapper, so the refs have to be stripped before the modal is
    // readable. (For a non-reference member such as `Int64 @local?` the modal is on the type
    // itself, and stripping is a no-op.)
    auto memberDataType = member.memberType->StripAllRefs();
    if (memberDataType == nullptr) {
        return ModalWriteKind::NONE;
    }
    // A `Copyable` field contains no references, so it cannot create a cross-boundary edge.
    if (memberDataType->IsCopyable()) {
        return ModalWriteKind::NONE;
    }

    if (member.isDemode) {
        // A demoded field is always `~local`, so the value being stored is known to be on the
        // heap. Only the owner's modal decides whether this can be a `region -> heap` edge:
        // an owner whose modal is NONE is provably on the heap (a region object can only be
        // typed `local!`/`local?`, never `~local`), so `heap -> heap` needs no extra work.
        return member.ownerModal.Local() == CHIR::Mode::NONE ? ModalWriteKind::NONE
                                                             : ModalWriteKind::DEMODE;
    }

    // Keep all local-aware writes out of the ordinary MCC_WriteRefField hot path:
    //   - NONE (`~local`) : owner/value are ordinary heap references, use gcwrite.ref.
    //   - MUST (`local!`) : owner/value live in a local region; the local-aware runtime ABI
    //                       performs the direct local-to-local store.
    //   - MAYBE (`local?`): the runtime has to classify owner/value dynamically.
    // Reusing maybe.local.write.ref for MUST avoids adding IsLocalObject checks to every
    // normal-mode field write and does not require another LLVM intrinsic.
    return memberDataType->GetModalInfo().Local() == CHIR::Mode::NONE ? ModalWriteKind::NONE
                                                                      : ModalWriteKind::MAYBE_LOCAL;
}
