// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_AST_NODES_LOCALMODALAST_H
#define CANGJIE_AST_NODES_LOCALMODALAST_H

#include "cangjie/AST/ModalInfo.h"
#include "cangjie/Basic/Position.h"
#include <string_view>

namespace Cangjie::AST {
/// locality used in AST node, parse representation. It has one more than Mode, that is None, which
/// defaults to Mode::NOT.
enum class ASTMode {
    NONE,
    NOT,
    HALF,
    FULL,
};
Mode ToLocalModal(ASTMode mod);
std::string_view ToString(ASTMode local);

struct ASTModalInfo {
    void SetAt(const Position& at)
    {
        atPos = at;
    }

    void SetLocal(ASTMode loc, const Position& localBegin)
    {
        local = loc;
        localPos = localBegin;
    }

    const Position& AtBegin() const
    {
        return atPos;
    }

    const Position& LocalBegin() const
    {
        return localPos;
    }

    Position LocalEnd() const;
    Position End() const;
    bool HasLocal() const;
    ASTMode Local() const
    {
        return local;
    }
    ModalInfo ToModalInfo() const;
    operator ModalInfo() const
    {
        return ToModalInfo();
    }
    /// Returns true if any modal is set. (currently only local modal)
    operator bool() const;
    /// Returns true if no modal is set.
    bool Empty() const;

private:
    Position atPos;
    Position localPos;
    ASTMode local{};
};
} // namespace Cangjie::AST
#endif // CANGJIE_AST_NODES_LOCALMODALAST_H
