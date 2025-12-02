// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Visitor Class in CHIR.
 */

#ifndef CANGJIE_CHIR_VISITOR_H
#define CANGJIE_CHIR_VISITOR_H

#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/CHIR/Utils/Visitor/SimpleIterator.h"
#include <functional>

namespace Cangjie::CHIR {
/*
 * @brief VisitResult of the walk
 *
 */
enum class VisitResult : uint8_t {
    CONTINUE, /**< Continue to walk into child items. */
    SKIP,     /**< Continue walking, but don't enter child items. */
    STOP      /**< Stop walking immediately. */
};

using VisitExprActionFunc = std::function<VisitResult(Expression&)>;
using VisitBlockActionFunc = std::function<VisitResult(Block&)>;
using VisitBlockGroupActionFunc = std::function<VisitResult(BlockGroup&)>;

/*
 * @brief Walk all blocksGroups nested under the given expression.
 *
 * The order in which blocksGroups, blocks and expressions are visited is determined by 'Iterator',
 * default using 'SimpleIterator'.
 *
 */
template <typename Iterator = SimpleIterator>
VisitResult Walk(const Expression& expr, VisitBlockGroupActionFunc pre, VisitBlockGroupActionFunc post)
{
    for (auto& blockGroup : Iterator::Iterate(expr)) {
        auto status = pre(*blockGroup);
        if (status == VisitResult::SKIP) {
            continue;
        }
        if (status == VisitResult::STOP) {
            return VisitResult::STOP;
        }
        for (auto& block : Iterator::Iterate(*blockGroup)) {
            for (auto& innerExpr : Iterator::Iterate(*block)) {
                if (Walk(*innerExpr, pre, post) == VisitResult::STOP) {
                    return VisitResult::STOP;
                }
            }
        }
        if (post(*blockGroup) == VisitResult::STOP) {
            return VisitResult::STOP;
        }
    }
    return VisitResult::CONTINUE;
}

/*
 * @brief Walk all blocks nested under the given expression.
 *
 * The order in which blocksGroups, blocks and expressions are visited is determined by 'Iterator',
 * default using 'SimpleIterator'.
 *
 */
template <typename Iterator = SimpleIterator>
VisitResult Walk(const Expression& expr, VisitBlockActionFunc pre, VisitBlockActionFunc post)
{
    for (auto& blockGroup : Iterator::Iterate(expr)) {
        for (auto& block : Iterator::Iterate(*blockGroup)) {
            auto status = pre(*block);
            if (status == VisitResult::SKIP) {
                continue;
            }
            if (status == VisitResult::STOP) {
                return VisitResult::STOP;
            }
            for (auto& innerExpr : Iterator::Iterate(*block)) {
                if (Walk(*innerExpr, pre, post) == VisitResult::STOP) {
                    return VisitResult::STOP;
                }
            }
            if (post(*block) == VisitResult::STOP) {
                return VisitResult::STOP;
            }
        }
    }
    return VisitResult::CONTINUE;
}

/*
 * @brief Walk all expressions nested under (and including) the given expression.
 *
 * The order in which blocksGroups, blocks and expressions are visited is determined by 'Iterator',
 * default using 'SimpleIterator'.
 *
 */
template <typename Iterator = SimpleIterator>
VisitResult Walk(Expression& expr, VisitExprActionFunc pre, VisitExprActionFunc post)
{
    auto status = pre(expr);
    if (status == VisitResult::SKIP) {
        return VisitResult::SKIP;
    }
    if (status == VisitResult::STOP) {
        return VisitResult::STOP;
    }
    for (auto& blockGroup : Iterator::Iterate(expr)) {
        for (auto& block : Iterator::Iterate(*blockGroup)) {
            for (auto& innerExpr : Iterator::Iterate(*block)) {
                if (Walk(*innerExpr, pre, post) == VisitResult::STOP) {
                    return VisitResult::STOP;
                }
            }
        }
    }
    return post(expr);
}

/// checking whether type T is one of any of the given
/// types in the variadic list.
template <typename T, typename... Ts> using IsAnyOf = std::disjunction<std::is_same<T, Ts>...>;
/// Helper templates to deduce the argument and return type of an ActionFunc.
template <typename Ret, typename Arg> Arg ArgTy(Ret (*)(Arg));
template <typename Ret, typename Arg> Ret RetTy(Ret (*)(Arg));
template <typename Ret, typename F, typename Arg> Arg ArgTy(Ret (F::*)(Arg));
template <typename Ret, typename F, typename Arg> Ret RetTy(Ret (F::*)(Arg));
template <typename Ret, typename F, typename Arg> Arg ArgTy(Ret (F::*)(Arg) const);
template <typename Ret, typename F, typename Arg> Ret RetTy(Ret (F::*)(Arg) const);
template <typename F> decltype(ArgTy(&F::operator())) ArgTy(F);
template <typename F> decltype(RetTy(&F::operator())) RetTy(F);

/// Type definition of the argument and return of the given callable 'T'.
template <typename T> using ArgTOf = decltype(ArgTy(std::declval<T>()));
template <typename T> using RetTOf = decltype(RetTy(std::declval<T>()));

/*
 * @brief Visitor for CHIR Node
 *
 * Visit all of the blocksGroups, blocks or expressions nested under (and including)
 * the given block group, basicblock or expression.
 */
class Visitor {
public:
    /*
     * @brief Visit all of the blocksGroups, blocks or expressions nested under (and including)
     * the given expression.
     *
     * The order in which blocksGroups, blocks and expressions are visited is determined by 'Iterator',
     * default using 'SimpleIterator'.
     *
     * Example:
     *     Visitor::Visit(expr, [](Expression& e) { ... }, [](Expression& e) { ... });
     *     Visitor::Visit(expr, [](Block& block) { ... }, [](Block& block) { ... });
     *     Visitor::Visit(expr, [](BlockGroup& t) { ... }, [](BlockGroup& t) { ... });
     *
     */
    template <typename Iterator = SimpleIterator, typename PreActionFuncTy, typename PostActionFuncTy,
        typename = std::enable_if_t<IsAnyOf<ArgTOf<PreActionFuncTy>, Expression&, Block&, BlockGroup&>::value &&
            std::is_same<ArgTOf<PreActionFuncTy>, ArgTOf<PostActionFuncTy>>::value &&
            std::is_same<RetTOf<PreActionFuncTy>, VisitResult>::value &&
            std::is_same<RetTOf<PreActionFuncTy>, RetTOf<PostActionFuncTy>>::value>>
    static void Visit(Expression& root, PreActionFuncTy pre, PostActionFuncTy post)
    {
        (void)Walk<Iterator>(root, pre, post);
    }
    /*
     * @brief Visit all of the blocksGroups, blocks or expressions nested under (and including)
     * the given expression.
     *
     * The order in which blocksGroups, blocks and expressions are visited is determined by 'Iterator',
     * default using 'SimpleIterator'.
     *
     * Example:
     *     Visitor::Visit(expr, [](Expression& e) { ... });
     *     Visitor::Visit(expr, [](Block& block) { ... });
     *     Visitor::Visit(expr, [](BlockGroup& t) { ... });
     *
     */
    template <typename Iterator = SimpleIterator, typename PreActionFuncTy, typename ArgT = ArgTOf<PreActionFuncTy>,
        typename = std::enable_if_t<IsAnyOf<ArgT, Expression&, Block&, BlockGroup&>::value &&
            std::is_same<RetTOf<PreActionFuncTy>, VisitResult>::value>>
    static void Visit(Expression& root, PreActionFuncTy pre)
    {
        auto post = [](ArgT) { return VisitResult::CONTINUE; };
        (void)Walk<Iterator>(root, pre, post);
    }
    /*
     * @brief Visit all of the Derived Expression nested under (and including)
     * the given expression.
     *
     * The order in which blocksGroups, blocks and expressions are visited is determined by 'Iterator',
     * default using 'SimpleIterator'.
     *
     * Example:
     *     Visitor::Visit(expr, [](BinaryExpr& be) { ... });
     *     Visitor::Visit(expr, [](Branch& b) { ... });
     *
     */
    template <typename Iterator = SimpleIterator, typename PreActionFuncTy, typename ArgT = ArgTOf<PreActionFuncTy>,
        typename = std::enable_if_t<std::is_same<RetTOf<PreActionFuncTy>, VisitResult>::value>,
        typename =
            std::enable_if_t<!std::is_same<ArgT, Expression&>::value && std::is_convertible<ArgT, Expression&>::value>>
    static void Visit(Expression& root, PreActionFuncTy pre)
    {
        auto wrapperPre = [&pre](Expression& child) {
            if (auto argTy = dynamic_cast<std::remove_reference_t<ArgT>*>(&child); argTy) {
                return pre(*argTy);
            }
            return VisitResult::CONTINUE;
        };
        auto post = [](Expression&) { return VisitResult::CONTINUE; };
        (void)Walk<Iterator>(root, wrapperPre, post);
    }
    /*
     * @brief Visit all of the Derived Expression nested under (and including)
     * the given expression.
     *
     * The order in which blocksGroups, blocks and expressions are visited is determined by 'Iterator',
     * default using 'SimpleIterator'.
     *
     * Example:
     *     Visitor::Visit(expr, [](BinaryExpr& be) { ... }, [](BinaryExpr& be) { ... });
     *     Visitor::Visit(expr, [](Branch& b) { ... }, [](Branch& b) { ... });
     *
     */
    template <typename Iterator = SimpleIterator, typename PreActionFuncTy, typename PostActionFuncTy,
        typename ArgT = ArgTOf<PreActionFuncTy>,
        typename = std::enable_if_t<std::is_same<RetTOf<PreActionFuncTy>, VisitResult>::value &&
            std::is_same<RetTOf<PostActionFuncTy>, VisitResult>::value && !std::is_same<ArgT, Expression&>::value &&
            std::is_convertible<ArgT, Expression&>::value && std::is_same<ArgTOf<PostActionFuncTy>, ArgT>::value>>
    static void Visit(Expression& root, PreActionFuncTy pre, PostActionFuncTy post)
    {
        auto wrapperPre = [&pre](Expression& child) {
            if (auto argTy = dynamic_cast<std::remove_reference_t<ArgT>*>(&child); argTy) {
                return pre(*argTy);
            }
            return VisitResult::CONTINUE;
        };
        auto wrapperPost = [&post](Expression& child) {
            if (auto argTy = dynamic_cast<std::remove_reference_t<ArgT>*>(&child); argTy) {
                return post(*argTy);
            }
            return VisitResult::CONTINUE;
        };
        (void)Walk<Iterator>(root, wrapperPre, wrapperPost);
    }
    /*
     * @brief Visit all of the blocksGroups, blocks, expressions or derived expression nested under (and including)
     * the given block or block group.
     *
     * The order in which blocksGroups, blocks and expressions are visited is determined by 'Iterator',
     * default using 'SimpleIterator'.
     *
     * Example:
     *     Visitor::Visit(block, [](Expression& e) { ... });
     *     Visitor::Visit(blockGroup, [](Block& block) { ... });
     *     Visitor::Visit(blockGroup, [](BlockGroup& t) { ... });
     *     Visitor::Visit(blockGroup, [](Branch& b) { ... });
     *
     */
    template <typename Iterator = SimpleIterator, typename RootTy, typename PreActionFuncTy,
        typename ArgT = ArgTOf<PreActionFuncTy>,
        typename = std::enable_if_t<IsAnyOf<RootTy&, Block&, BlockGroup&>::value &&
            std::is_same<RetTOf<PreActionFuncTy>, VisitResult>::value>>
    static void Visit(RootTy& root, PreActionFuncTy pre)
    {
        if constexpr (std::is_same<RootTy&, ArgT>::value) {
            auto status = pre(static_cast<ArgT>(root));
            if (status == VisitResult::SKIP || status == VisitResult::STOP) {
                return;
            }
        }
        for (auto& child : Iterator::Iterate(root)) {
            Visit<Iterator>(*child, pre);
        }
    }
    /*
     * @brief Visit all of the blocksGroups, blocks, expressions or derived expression nested under (and including)
     * the given block or block group.
     *
     * The order in which blocksGroups, blocks and expressions are visited is determined by 'Iterator',
     * default using 'SimpleIterator'.
     *
     * Example:
     *     Visitor::Visit(block, [](Expression& e) { ... }, [](Expression& e) { ... });
     *     Visitor::Visit(blockGroup, [](Block& block) { ... }, [](Block& block) { ... });
     *     Visitor::Visit(blockGroup, [](BlockGroup& t) { ... }, [](BlockGroup& t) { ... });
     *     Visitor::Visit(blockGroup, [](Branch& b) { ... }, [](Branch& b) { ... });
     *
     */
    template <typename Iterator = SimpleIterator, typename RootTy, typename PreActionFuncTy, typename PostActionFuncTy,
        typename ArgT = ArgTOf<PreActionFuncTy>,
        typename = std::enable_if_t<IsAnyOf<RootTy&, Block&, BlockGroup&>::value &&
            std::is_same<RetTOf<PreActionFuncTy>, VisitResult>::value &&
            std::is_same<ArgT, ArgTOf<PostActionFuncTy>>::value &&
            std::is_same<RetTOf<PreActionFuncTy>, RetTOf<PostActionFuncTy>>::value>>
    static void Visit(RootTy& root, PreActionFuncTy pre, PostActionFuncTy post)
    {
        if constexpr (std::is_same<RootTy&, ArgT>::value) {
            auto status = pre(static_cast<ArgT>(root));
            if (status == VisitResult::SKIP || status == VisitResult::STOP) {
                return;
            }
        }
        for (auto& child : Iterator::Iterate(root)) {
            Visit<Iterator>(*child, pre, post);
        }
        if constexpr (std::is_same<RootTy&, ArgT>::value) {
            post(static_cast<ArgT>(root));
        }
    }

    /*
     * @brief Visit all of blocksGroups, blocks and expressions under (and including)
     * the given func.
     *
     * Example:
     *     Visitor::Visit(func, [](BinaryExpr& be) { ... });
     *     Visitor::Visit(func, [](Branch& b) { ... });
     *
     */
    template <typename Iterator = SimpleIterator, typename PreActionFuncTy, typename ArgT = ArgTOf<PreActionFuncTy>,
        typename = std::enable_if_t<std::is_same<RetTOf<PreActionFuncTy>, VisitResult>::value>,
        typename = std::enable_if_t<!std::is_same<ArgT, Func&>::value>>
    static void Visit(const Func& root, PreActionFuncTy pre)
    {
        Visit(*root.GetBody(), pre);
    }
    /*
     * @brief Visit all of blocksGroups, blocks and expressions under (and including)
     * the given func.
     *
     * Example:
     *     Visitor::Visit(func, [](BinaryExpr& be) { ... }, [](BinaryExpr& be) { ... });
     *     Visitor::Visit(func, [](Branch& b) { ... }, [](Branch& b) { ... });
     *
     */
    template <typename Iterator = SimpleIterator, typename PreActionFuncTy, typename PostActionFuncTy,
        typename ArgT = ArgTOf<PreActionFuncTy>,
        typename = std::enable_if_t<std::is_same<RetTOf<PreActionFuncTy>, VisitResult>::value &&
            std::is_same<RetTOf<PostActionFuncTy>, VisitResult>::value && !std::is_same<ArgT, Func&>::value &&
            std::is_same<ArgTOf<PostActionFuncTy>, ArgT>::value>>
    static void Visit(const Func& root, PreActionFuncTy pre, PostActionFuncTy post)
    {
        Visit(*root.GetBody(), pre, post);
    }
};

} // namespace Cangjie::CHIR
#endif // CANGJIE_CHIR_VISITOR_H