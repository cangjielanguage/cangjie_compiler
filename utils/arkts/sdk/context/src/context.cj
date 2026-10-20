// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// Licensed under Apache-2.0 with Runtime Library Exception.
package cj_arkts_context_v1

import ohos.ark_interop.JSContext

let CURRENT_CONTEXT = ThreadLocal<JSContext>()

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

// Before -> after: Some(ctx) in the ThreadLocal slot remains Some(ctx); return
// that ctx after getNapiEnv() succeeds. A None slot throws ARKTS_NO_CALLING_CONTEXT
// instead of creating a context. Foreign wrappers call this accessor without a
// business-code context argument; validation errors propagate to the caller.
public func __cj_arkts_current(): JSContext {
    let ctx = CURRENT_CONTEXT.get() ?? throw Exception("ARKTS_NO_CALLING_CONTEXT")
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
