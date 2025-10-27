// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares utility classes for implementing a chain of responsibility.
 */

#ifndef CANGJIE_SEMA_OBJ_C_UTILS_HANDLER_H
#define CANGJIE_SEMA_OBJ_C_UTILS_HANDLER_H

#include <cstddef>
#include <tuple>

namespace Cangjie::Interop::ObjC {

enum class InteropType {
    NA,
    ObjC_Mirror,
    CJ_Mapping,
};

/**
 * Base CRTP-class for handlers.
 * Every descendant should implement `void HandleImpl(ContextT& ctx)` method.
 */
template <class DerivedT, class ContextT> class Handler {
public:
    void Handle(ContextT& ctx)
    {
        static_cast<DerivedT&>(*this).HandleImpl(ctx);
    }

private:
    Handler()
    {
    }
    friend DerivedT;
};

/**
 * Implements simple static Chain of responsibility pattern for Handlers.
 */
template <class ContextT, class... HandlersT> class ChainedHandlers {
public:
    explicit ChainedHandlers(HandlersT&&... hs) : handlers(std::forward<HandlersT>(hs)...)
    {
    }

    /**
     * Creates `NextHandlerT` with provided `NextHandlerArgsT` and adds it to the end of the chain.
     */
    template <class NextHandlerT, class... NextHandlerArgsT> auto Use(NextHandlerArgsT&&... args)
    {
        return ChainedHandlers<ContextT, HandlersT..., NextHandlerT>(std::move(std::get<HandlersT>(handlers))...,
            std::move(NextHandlerT(std::forward<NextHandlerArgsT>(args)...)));
    }

    /**
     * Recursively calls `Handle(ctx)` method on chained `HandlersT`
     */
    template <size_t I = 0> void Handle(ContextT& ctx)
    {
        if constexpr (I < sizeof...(HandlersT)) {
            std::get<I>(handlers).Handle(ctx);

            Handle<I + 1>(ctx);
        }
    }

private:
    std::tuple<HandlersT...> handlers;
};

/**
 * Factory class to start `ChainedHandlers` for specific `ContextT`.
 */
template <class ContextT> class HandlerFactory {
public:
    /**
     * Creates `FirstHandlerT` with provided `FirstHandlerArgsT` and starts a new chain of handlers (`ChainedHandlers`)
     * with `FirstHandlerT` at the beginning.
     */
    template <class FirstHandlerT, class... FirstHandlerArgsT> static auto Start(FirstHandlerArgsT&&... args)
    {
        return ChainedHandlers<ContextT, FirstHandlerT>(FirstHandlerT(std::forward<FirstHandlerArgsT>(args)...));
    }
};

} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_OBJ_C_UTILS_HANDLER_H