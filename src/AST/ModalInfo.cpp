// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/AST/ModalInfo.h"

#include "cangjie/Utils/Utils.h"

namespace Cangjie {

std::string_view ToString(Mode modal)
{
    switch (modal) {
        case Mode::IDEAL:
            return "@ideal";
        case Mode::NOT:
            return "";
        case Mode::HALF:
            return "@local?";
        case Mode::FULL:
            return "@local!";
        default:
            CJC_ABORT();
            return "";
    }
}

static_assert(sizeof(ModalInfo) <= 2 * sizeof(void*), "pass ModalInfo by reference instead of by value");

std::string ModalInfo::ToString() const
{
    return std::string{Cangjie::ToString(local)};
}

std::string ModalInfo::LocalString() const
{
    switch (local) {
        case Mode::IDEAL:
            return "@ideal";
        case Mode::NOT:
            return "@~local";
        case Mode::HALF:
            return "@local?";
        case Mode::FULL:
            return "@local!";
        default:
            CJC_ABORT();
            return "";
    }
}

std::string ModalInfo::AsTypeSuffixString() const
{
    auto b = Cangjie::ToString(local);
    if (b.empty()) {
        return {};
    }
    return std::string{" "} + std::string{b};
}

int ToIndex(ModalInfo modal)
{
    // IDEAL is a transient Sema-only modal (pending literal modal), mirroring IDEAL_INT/IDEAL_FLOAT.
    // Like an ideal type, it may temporarily participate in ordering/hashing of a ModalTy set
    // before generic type argument inference unifies it with the expected contextual modal. Map
    // it to the NOT slot so ordering/hashing stay total and do not underflow; a Sema-end
    // HasIdealModal guard catches any IDEAL that was not resolved before reaching the back end.
    if (modal.local == Mode::IDEAL) {
        return static_cast<int>(Mode::NOT) - 1; // == 0, the NOT slot
    }
    return static_cast<int>(modal.local) - 1;
}

ModalInfo ToModalInfo(int index)
{
    return {static_cast<Mode>(index + 1)};
}

Mode operator|(Mode lhs, Mode rhs)
{
    return static_cast<Mode>(static_cast<int>(lhs) | static_cast<int>(rhs));
}

Mode operator&(Mode lhs, Mode rhs)
{
    return static_cast<Mode>(static_cast<int>(lhs) & static_cast<int>(rhs));
}

ModalInfo ModalInfo::operator|(ModalInfo other) const
{
    return {local | other.local};
}

ModalInfo ModalInfo::operator&(ModalInfo other) const
{
    return {local & other.local};
}

bool ModalInfo::IsSubModal(ModalInfo other) const
{
    return (local | other.local) == other.local;
}

} // namespace Cangjie
