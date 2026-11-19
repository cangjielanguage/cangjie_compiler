// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// Licensed under Apache-2.0 with Runtime Library Exception.
package cj_arkts_context_v1

import ohos.ark_interop.JSContext

let CURRENT_CONTEXT = ThreadLocal<JSContext>()

// Raised when a wrapper is used without an active calling scope. The type is
// re-exported by `arkts.lang` as the interop runtime exception, so callers can
// catch a scope misuse by the same type as other runtime interop failures.
public class ForeignRuntimeException <: Exception {
    public init(message: String) {
        super(message)
    }
}

// Before: the current ThreadLocal slot contains previous (Some(parent) or None).
// After getNapiEnv() succeeds: slot = Some(ctx), result = previous. The generated
// callback saves that result and restores it in finally; nested entries can thus
// replace the active context temporarily without losing their caller's context.
// If context validation throws, execution has not reached the slot update.
public func __cj_arkts_enter(ctx: JSContext): ?JSContext {
    ctx.getNapiEnv()
    let previous = CURRENT_CONTEXT.get()
    CURRENT_CONTEXT.set(Some(ctx))
    return previous
}

// The context of the active calling scope. Without a scope the call fails with
// ForeignRuntimeException: interop calls only run inside an @ArkTS entry.
// Lifecycle and thread checks of the stored context propagate unchanged.
public func __cj_arkts_current(): JSContext {
    let ctx = CURRENT_CONTEXT.get() ?? throw ForeignRuntimeException(
        "ArkTS interop call requires an active calling scope: no JSContext is bound to the current thread")
    ctx.getNapiEnv()
    return ctx
}

// Before: the ThreadLocal slot holds the current entry's context. After:
// slot = previous, restoring Some(parent) for a nested entry or None for an
// outermost entry. The generated callback calls this in finally on both normal
// return and exception; no new context is created or validated by this function.
public func __cj_arkts_restore(previous: ?JSContext): Unit {
    CURRENT_CONTEXT.set(previous)
}
