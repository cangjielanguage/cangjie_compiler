// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares a stack for the bytecode interpreter.
 */

#ifndef CANGJIE_CHIR_INTERRETER_INTERPRETERSTACK_H
#define CANGJIE_CHIR_INTERRETER_INTERPRETERSTACK_H

#include "cangjie/CHIR/Interpreter/BCHIR.h"
#include "cangjie/CHIR/Interpreter/InterpreterValueUtils.h"

namespace Cangjie::CHIR::Interpreter {

struct ControlState {
    OpCode opCode;
    size_t argStackPtr;
    Bchir::ByteCodeIndex byteCodePtr;
    size_t envBP;
};

/** @brief explicit stacks for the interpreter */
struct InterpreterStack {
    const static size_t ARG_STACK_SIZE = 16384;
    const static size_t OP_STACK_SIZE = 1024;
    InterpreterStack()
    {
        argStack.reserve(ARG_STACK_SIZE);
        controlStack.reserve(OP_STACK_SIZE);
    }
    ~InterpreterStack()
    {
        while (ArgsSize() > 0) {
            ArgsPopBack();
        }
    }
    InterpreterStack(InterpreterStack&) = delete;
    InterpreterStack& operator=(const InterpreterStack& other) = delete;

    /**
     * @brief Consume an IValStack and transform it into an IVal
     **/
    IVal ToIVal(IValStack&& n) const
    {
        return std::visit(
            [&](auto&& arg) -> IVal {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, ITuplePtr>) {
                    ITuple res;
                    std::swap(res.content, *arg.contentPtr);
                    delete arg.contentPtr;
                    return res;
                } else if constexpr (std::is_same_v<T, IArrayPtr>) {
                    IArray res;
                    std::swap(res.content, *arg.contentPtr);
                    delete arg.contentPtr;
                    return res;
                } else if constexpr (std::is_same_v<T, IObjectPtr>) {
                    IObject res;
                    std::swap(res.content, *arg.contentPtr);
                    res.classId = arg.classId;
                    delete arg.contentPtr;
                    return res;
                } else {
                    return arg;
                }
            },
            std::move(n));
    }

    /**
     * @brief Consume an IVal and transform it into an IValStack
     */
    IValStack FromIVal(IVal&& n) const
    {
        return std::visit(
            [&](auto&& arg) -> IValStack {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, ITuple>) {
                    std::vector<IVal>* ptr = new std::vector<IVal>();
                    std::swap(*ptr, arg.content);
                    ITuplePtr res{ptr};
                    return res;
                } else if constexpr (std::is_same_v<T, IArray>) {
                    std::vector<IVal>* ptr = new std::vector<IVal>();
                    std::swap(*ptr, arg.content);
                    IArrayPtr res{ptr};
                    return res;
                } else if constexpr (std::is_same_v<T, IObject>) {
                    std::vector<IVal>* ptr = new std::vector<IVal>();
                    std::swap(*ptr, arg.content);
                    IObjectPtr res{arg.classId, ptr};
                    return res;
                } else {
                    return arg;
                }
            },
            std::move(n));
    }

    /**
     * @brief Pop an element of type T from the stack
     *
     * Fails if the top of the stack is not of type T
     */
    template <typename T> T ArgsPop()
    {
        CJC_ASSERT(!argStack.empty());

        using S = std::decay_t<T>;
        auto arg = std::move(argStack.back());
        argStack.pop_back();

        if constexpr (std::is_same_v<S, ITuple>) {
            ITuple res;
            std::swap(res.content, *std::get<ITuplePtr>(arg).contentPtr);
            delete std::get<ITuplePtr>(arg).contentPtr;
            return res;
        } else if constexpr (std::is_same_v<S, IArray>) {
            IArray res;
            std::swap(res.content, *std::get<IArrayPtr>(arg).contentPtr);
            delete std::get<IArrayPtr>(arg).contentPtr;
            return res;
        } else if constexpr (std::is_same_v<S, IObject>) {
            IObject res;
            std::swap(res.content, *std::get<IObjectPtr>(arg).contentPtr);
            res.classId = std::get<IObjectPtr>(arg).classId;
            delete std::get<IObjectPtr>(arg).contentPtr;
            return res;
        } else {
            return std::get<S>(arg);
        }
    }

    /**
     * @brief Pop an IVal from the stack
     *
     * Avoid using this method unless you will use the result,
     * and you can't know the type of the top element
     */
    inline IVal ArgsPopIVal()
    {
        CJC_ASSERT(!argStack.empty());
        auto n = std::move(argStack.back());
        argStack.pop_back();

        return ToIVal(std::move(n));
    }

    /**
     * @brief Pop and destroy the top of the stack
     *
     * This is faster that using ArgsPopIVal
     */
    void ArgsPopBack()
    {
        CJC_ASSERT(!argStack.empty());

        std::visit(
            [&](auto& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, ITuplePtr>) {
                    delete arg.contentPtr;
                } else if constexpr (std::is_same_v<T, IArrayPtr>) {
                    delete arg.contentPtr;
                } else if constexpr (std::is_same_v<T, IObjectPtr>) {
                    delete arg.contentPtr;
                }
            },
            argStack.back());

        argStack.pop_back();
    }

    /**
     * @brief Pop size elements from the stack and place them on elems
     *
     * elems will be cleared before using it.
     */
    void ArgsPop(size_t size, std::vector<IVal>& elems)
    {
        // OPTIMIZE
        CJC_ASSERT(size <= argStack.size());
        elems.clear();
        elems.reserve(size);

        for (size_t i = 0; i < size; ++i) {
            (void)elems.emplace_back(ArgsPopIVal());
        }
        std::reverse(elems.begin(), elems.end());
    }

    /**
     * @brief Get a reference to the top element
     *
     * This method will be deprecated, avoid
     */
    template <typename T> const T& ArgsTop() const
    {
        CJC_ASSERT(!argStack.empty());
        using S = std::decay_t<T>;

        if constexpr (std::is_same_v<S, ITuple>) {
            // #warning You shouldn't use ArgsTop with ITuple
            abort();
        } else if constexpr (std::is_same_v<S, IArray>) {
            // #warning You shouldn't use ArgsTop with IArray
            abort();
        } else if constexpr (std::is_same_v<S, IObject>) {
            // #warning You shouldn't use ArgsTop with IObject
            abort();
        } else {
            return std::get<S>(argStack.back());
        }
    }

    /**
     * @brief Get the top element as an IVal
     *
     * This method is slow, try to avoid it
     */
    IVal ArgsTopIVal() const
    {
        CJC_ASSERT(!argStack.empty());
        return ArgsGet(1, 0);
    }

    /**
     * @brief remove n elements from the stack
     */
    void ArgsRemove(size_t n)
    {
        CJC_ASSERT(n <= argStack.size());
        while (n--) {
            ArgsPopBack();
        }
    }

    /**
     * @brief remove all but n element from the stack
     */
    void ArgsRemoveAfter(size_t n)
    {
        CJC_ASSERT(n <= argStack.size());
        auto toRemove = argStack.size() - n;
        ArgsRemove(toRemove);
    }

    /**
     * @brief Get the index element from offsetFromEnd element from the top
     *
     * Try to avoid this method, since it copies the IVal
     */
    IVal ArgsGet(size_t offsetFromEnd, size_t index) const
    {
        auto idx = (argStack.size() - offsetFromEnd) + index;
        CJC_ASSERT(idx < argStack.size());
        return ArgsGet(idx);
    }

    /**
     * @brief Get the idx element from the bottom of the stack
     *
     * Try to avoid this method, since it copies the IVal
     */
    IVal ArgsGet(size_t idx) const
    {
        IValStack val = argStack[idx];
        return std::visit(
            [&](auto&& arg) -> IVal {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, ITuplePtr>) {
                    ITuple res{*arg.contentPtr};
                    return res;
                } else if constexpr (std::is_same_v<T, IArrayPtr>) {
                    IArray res{*arg.contentPtr};
                    return res;
                } else if constexpr (std::is_same_v<T, IObjectPtr>) {
                    IObject res{arg.classId, *arg.contentPtr};
                    return res;
                } else {
                    return arg;
                }
            },
            std::move(val));
    }

    size_t ArgsSize() const
    {
        return argStack.size();
    }

    /**
     * @brief Push a value of type T into the stack
     */
    template <typename T> void ArgsPush(T&& node)
    {
        using S = std::decay_t<T>;
        static_assert(!std::is_same_v<S, IVal>, "ArgsPush can't be used with IVal, only the internal values of IVal");

        if constexpr (std::is_same_v<S, ITuple>) {
            ITuplePtr t{new std::vector<IVal>};
            std::swap(*t.contentPtr, node.content);
            (void)argStack.emplace_back(t);
        } else if constexpr (std::is_same_v<S, IArray>) {
            IArrayPtr t{new std::vector<IVal>};
            std::swap(*t.contentPtr, node.content);
            (void)argStack.emplace_back(t);
        } else if constexpr (std::is_same_v<S, IObject>) {
            IObjectPtr t{node.classId, new std::vector<IVal>};
            std::swap(*t.contentPtr, node.content);
            (void)argStack.emplace_back(t);
        } else {
            (void)argStack.emplace_back(std::forward<T>(node));
        }
    }

    /**
     * @brief Push an IVal into the stack
     *
     * Try to avoid this method since it's slow
     */
    void ArgsPushIVal(IVal&& node)
    {
        (void)argStack.emplace_back(FromIVal(std::move(node)));
    }

    /**
     * @brief Push an IVal, but only copy it when needed
     */
    void ArgsPushIValRef(const IVal& node)
    {
        std::visit(
            [this](const auto& arg) {
                using T = std::decay_t<decltype(arg)>;
                T v = arg;
                ArgsPush<T>(std::move(v));
            },
            node);
    }

    void ArgsSwapFromEnd(size_t i, size_t j, size_t offsetFromEnd)
    {
        std::swap(argStack[(argStack.size() - offsetFromEnd) + i], argStack[(argStack.size() - offsetFromEnd) + j]);
    }

    const ControlState& CtrlTop() const
    {
        return controlStack.back();
    }

    void CtrlPush(ControlState&& op)
    {
        controlStack.emplace_back(std::move(op));
    }

    ControlState CtrlPop()
    {
        auto ctrl = std::move(controlStack.back());
        controlStack.pop_back();
        return ctrl;
    }

    void CtrlDrop()
    {
        controlStack.pop_back();
    }

    bool CtrlIsEmpty() const
    {
        return controlStack.empty();
    }

    const std::vector<ControlState>& GetCtrlStack() const
    {
        return controlStack;
    }

    size_t CtrlSize() const
    {
        return controlStack.size();
    }

private:
    /** @brief stack for arguments */
    std::vector<IValStack> argStack;
    /** @brief stack for control flow */
    std::vector<ControlState> controlStack;
};

} // namespace Cangjie::CHIR::Interpreter

#endif // CANGJIE_CHIR_INTERRETER_INTERPRETERSTACK_H