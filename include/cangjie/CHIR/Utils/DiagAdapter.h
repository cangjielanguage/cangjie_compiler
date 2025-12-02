// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_DIAG_WRAPPER_H
#define CANGJIE_CHIR_DIAG_WRAPPER_H

#include <map>

#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"

namespace Cangjie::CHIR {
class DiagAdapter {
public:
    DiagAdapter(DiagnosticEngine& diag) : diag(diag){};

    template <typename... Args>
    DiagnosticBuilder DiagnoseRefactor(DiagKindRefactor kind, const Range& range, Args&&... args)
    {
        auto key = static_cast<uint64_t>(range.begin.Hash64());
        auto it = posRange2MacroCallMap.lower_bound(key);
        if (it == posRange2MacroCallMap.end() || !it->second) {
            // means the range's begin is not from macro expanded node
            return diag.DiagnoseRefactor(kind, range, std::forward<Args>(args)...);
        }
        AST::Node node;
        node.EnableAttr(AST::Attribute::MACRO_EXPANDED_NODE);
        node.curMacroCall = it->second;
        return diag.DiagnoseRefactor(kind, node, range, std::forward<Args>(args)...);
    }

    template <typename... Args>
    DiagnosticBuilder DiagnoseRefactor(DiagKindRefactor kind, const Cangjie::Position& pos, Args&&... args)
    {
        auto key = static_cast<uint64_t>(pos.Hash64());
        auto it = posRange2MacroCallMap.lower_bound(key);
        CJC_ASSERT(it == posRange2MacroCallMap.end() || !it->second);
        // means the range's begin is not from macro expanded node
        return diag.DiagnoseRefactor(kind, pos, std::forward<Args>(args)...);
    }

    template <typename... Args> DiagnosticBuilder Diagnose(DiagKind kind, Args&&... args)
    {
        return diag.Diagnose(kind, std::forward<Args>(args)...);
    }

    uint64_t GetErrorCount()
    {
        return diag.GetErrorCount();
    }

    SourceManager& GetSourceManager() noexcept
    {
        return diag.GetSourceManager();
    }

    /**
     * the purpose of this map is to record PositionRange-to-MacroCall relation
     * for any position from `Macro Expanded Node`, we can get original `Macro Call`.
     *
     * let's give an example of a macro call
     *
     *
     * @Test
     * func foo() {
     *    ...
     *    let a = 1
     * }
     *
     * let b = 2
     *
     *
     * Because `a` is in MacroCall but `b` is not, position of `a` may change after `Macro Expand`,
     * the position relationship before and after `Macro Expand` of `a` is maintained in `Macro Call Node`,
     * it's important to get correct MacroCall Node from any random postion,
     * here we use this map to record PositionRange-to-MacroCall mapping.
     *
     * in detail of mapping, as for this example, will insert two key-value to this map,
     * the first is `@`'s position and the pointer that point to `Macro Call Node`
     * the second is `}`'s position and nullptr
     *
     * Let's say we need to get correct position of two VarDecls: a and b
     *    as for a:
     *    to find the correct `Macro Call Node`, we can use `std::map::lower_bound` to find `@`,
     *    whose position is `not greater` than a, that is `less or equal` than a.
     *
     *    the combination of `std::greater` and `std::map::lower_bound`
     *    is actually just a way to achieve `not greater`,
     *
     *    you may ask why not use `std::map::upper_bound` to find `@`, because then if we use `@` as an input to find
     *    the corresponding MacroCallNode, it will failed.
     *
     *    as for b:
     *    we found `}`'s position and nullptr by the same way, but `}`'s postion corresponds to `nullptr` as we
     *    set before, so the `b` is not in a `Macro Call Node`, the postion of b is already correct.
     *
     */
    std::map<uint64_t, Ptr<AST::Node>, std::greater<>> posRange2MacroCallMap;
    DiagnosticEngine& diag;
};
} // namespace Cangjie::CHIR

#endif
