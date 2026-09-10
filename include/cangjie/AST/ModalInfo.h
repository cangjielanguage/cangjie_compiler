// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Modal type info (Mode, ModalInfo) shared by AST and CHIR.
 */

#ifndef CANGJIE_UTILS_MODALINFO_H
#define CANGJIE_UTILS_MODALINFO_H

#include <cstdint>
#include <string>
#include <string_view>

namespace Cangjie {
enum class Mode : uint8_t {
    IDEAL = 0, // Pending modal of an unsolved literal in a func-arg lambda's implicit-return
               // position. Mirrors IDEAL_INT/IDEAL_FLOAT: kept pending so generic type argument
               // inference can unify it against the expected contextual modal. Must be resolved
               // (to a concrete modal, defaulting to NOT) before Sema ends; reaching the back end
               // or serialization is a bug.
    NOT = 1,   // @~local
    FULL = 2,  // @local!
    HALF = 3,  // @local?
};
std::string_view ToString(Mode modal);
/// Returns the Mode that both \ref lhs and \ref rhs can be converted to.
Mode operator|(Mode lhs, Mode rhs);

/// Modal type info, used by Ty. This type is small, so pass it by value.
struct ModalInfo {
    Mode local;
    /// Construct ModalInfo that does not care about local modal, which can be converted to any other one.
    ModalInfo() : local(Mode::NOT)
    {
    }
    ModalInfo(Mode modal) : local(modal)
    {
    }
    bool operator==(const ModalInfo& other) const
    {
        return local == other.local;
    }
    bool operator!=(const ModalInfo& other) const
    {
        return local != other.local;
    }
    std::string ToString() const;
    std::string LocalString() const;
    std::string AsTypeSuffixString() const;
    /// Returns the ModalInfo that both \ref lhs and \ref rhs can be converted to.
    ModalInfo operator|(ModalInfo other) const;
    ModalInfo operator&(ModalInfo other) const;

    bool IsSubModal(ModalInfo other) const;
    /// Whether this modal is the pending IDEAL modal of a literal awaiting unification with the
    /// expected contextual modal (mirrors IDEAL_INT/IDEAL_FLOAT). IDEAL must not escape Sema.
    bool IsIdeal() const { return local == Mode::IDEAL; }
};
constexpr int MODAL_INFO_COUNT = 3; // concrete modals: NOT, FULL, HALF. IDEAL is transient (Sema-only).
int ToIndex(ModalInfo modal);
ModalInfo ToModalInfo(int index);
} // namespace Cangjie

#endif // CANGJIE_UTILS_MODALINFO_H
