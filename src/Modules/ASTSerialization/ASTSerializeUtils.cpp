// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "ASTSerializeUtils.h"

#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie {
namespace {
/**
 * Convert Mode to PackageFormat::LocalMode.
 * Mode values: NOT=1, FULL=2, HALF=3
 * PackageFormat::LocalMode values: NOT=0, FULL=1, HALF=2
 */
PackageFormat::LocalMode AstLocalToFormat(Mode astModal)
{
    // IDEAL is a transient Sema-only modal (a literal's pending modal). It must be resolved to a
    // concrete modal (NOT/FULL/HALF) before any type is serialized; reaching here is a bug.
    CJC_ASSERT(astModal != Mode::IDEAL);
    return static_cast<PackageFormat::LocalMode>(static_cast<int>(astModal) - 1);
}

/**
 * Convert PackageFormat::LocalMode to Mode.
 * PackageFormat::LocalMode values: NOT=0, FULL=1, HALF=2
 * Mode values: NOT=1, FULL=2, HALF=3
 */
Mode FormatToAstLocal(PackageFormat::LocalMode formatModal)
{
    return static_cast<Mode>(static_cast<int>(formatModal) + 1);
}
} // namespace

TModeOffset SaveModal(flatbuffers::FlatBufferBuilder& fbb, ModalInfo modal)
{
    if (modal == ModalInfo{}) {
        return {};
    }
    return PackageFormat::CreateMode(fbb, AstLocalToFormat(modal.local));
}

ModalInfo ReadModal(const PackageFormat::Mode* mode)
{
    if (mode == nullptr) {
        return {};
    }
    return {FormatToAstLocal(mode->local())};
}

TVectorOffset<TModeOffset> CreateTyModesVector(
    flatbuffers::FlatBufferBuilder& fbb, const std::vector<TModeOffset>& modes)
{
    if (modes.empty()) {
        return {};
    }
    for (const auto& mode : modes) {
        if (mode.o != 0) {
            return fbb.CreateVector(modes);
        }
    }
    return {};
}

TVectorOffset<TModeOffset> SaveModalVector(
    flatbuffers::FlatBufferBuilder& fbb, const std::vector<ModalInfo>& modals)
{
    for (const auto& modal : modals) {
        if (modal != ModalInfo{}) {
            std::vector<TModeOffset> offsets;
            offsets.reserve(modals.size());
            // Unconditional CreateMode: keep NOT slots so the vector index aligns with the type-arg index.
            for (const auto& m : modals) {
                offsets.push_back(PackageFormat::CreateMode(fbb, AstLocalToFormat(m.local)));
            }
            return CreateTyModesVector(fbb, offsets);
        }
    }
    return {};
}

AST::ModalTy ApplyLoadedModal(AST::DataTy ty, const PackageFormat::Mode* mode)
{
    if (mode == nullptr) {
        return ty;
    }
    return AST::ModalTy{ty, ReadModal(mode)};
}
} // namespace Cangjie
