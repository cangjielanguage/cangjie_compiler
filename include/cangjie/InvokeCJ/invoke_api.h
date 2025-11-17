// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file This file declares the APIs for C Invoke CJ.
 * @author Zapanov Rinchin
 */

#ifndef CJ_INVOKE_API_H
#define CJ_INVOKE_API_H

#include <cstdint>

/**
 * Type that represents error codes. The programmer can quickly check the
 * return value of the API call to determine if a result in expected value range.
 */
typedef int32_t CJErrorCode;

/**
 * Runtime identifier.
 */
typedef int32_t CangjieRT;

/**
 * Type that represents a @c CJ function.
 */
typedef void* CJFunction;

/**
 * Type that represents a asynchronously invoked function.
 */
typedef int64_t CJThreadHandle;

const CJErrorCode CJ_OK                      =  0;
const CJErrorCode CJ_FAILED_WITH_EXCEPTION   =  1;
const CJErrorCode CJ_HANDLE_ALREADY_RELEASED =  2;
const CJErrorCode CJ_TASK_IS_RUNNING         =  3;
const CJErrorCode CJ_ILLEGAL_VALUE           = -1;
const CJErrorCode CJ_NO_MEM                  = -2;
const CJErrorCode CJ_INTERNAL_ERROR          = -3;

/**
 * This function should be called once before any other interaction with CJ.
 * If some customs signal handlers were set in C code (for example via
 * [sigaction](https://man7.org/linux/man-pages/man2/sigaction.2.html))
 * it will be saved during call of this function, and new, RT-specific handlers will be used.
 *
 * @param rt pointer to the location where the resulting `CangjieRT` will be
 *           placed, which is `INVALID_RT` on failure.
 * @param params an array of string arguments to initialize CJ Runtime. Should be zero-terminated.
 *
 * @return CJ_OK on success; otherwise, returns a suitable error code on failure.
 */
CJErrorCode initCangjieRuntime(CangjieRT* rt, char* params[]);

/**
 * Function that should be called when no more interaction with Cangjie code is needed.
 * All resources consumed by runtime are freed after that. If custom signal handlers
 * were saved during call of `initCangjieRuntime`, it will be restored.
 *
 * @param rt the Cangjie RT that will be destroyed.
 *
 * @return CJ_OK on success; otherwise, returns a suitable error code on failure.
 */
CJErrorCode destroyCangjieRuntime(CangjieRT rt);


/**
 * Finds public Cangjie `@C` function with the given name in the specified package.
 * Signature should not be specified here as `@C` functions can't be overloaded in Cangjie.
 *
 * @param functionLoc pointer to the location where the resulting `CJFunction` will be placed,
 *                    which is `INVALID_CJ_FUNCTION` on failure.
 * @param rt the Cangjie RT instance.
 * @param packageName a package name.
 * @param funcName a function name.
 *
 * @return CJ_OK on success; otherwise, returns a suitable error code on failure.
 */
CJErrorCode findCangjieFunction(CJFunction* functionLoc, CangjieRT rt, char* packageName, char* funcName);

/**
 * Synchronously invoke specified function.
 *
 * @param retLoc - pointer to the location where to put result.
 * @param exceptionDescriptionLoc pointer to the location where resulting exception description
 *                               will be stored.If pointer is `NULL` the description will not be prepared.
 * @param rt `CangjieRT` instance that was created with `initCangjieRuntime` function.
 * @param func Cangjie function found by `findCangjieFunction`.
 * @param varargs also, this function takes varargs, so, all needed by Cangjie function arguments could be passed there.
 *
 * @return On normal executions the value returned could be one of the following:
 *
 * ```c
 * CJ_OK,
 * CJ_FAILED_WITH_EXCEPTION,
 * ```
 *
 * If invocation of Cangjie function was successful, the status is set to `OK`, result is stored
 * in the location pointed by `retLoc`, and `exception_description` is `NULL`.
 *
 * However, if there was an **exception**: the status is set to `FAILED_WITH_EXCEPTION` and
 * nothing is written into `retLoc`. If `exceptionDescriptionLoc` is not `NULL` the C string
 * with description of exception is allocated: it contains name of exception, line number and
 * stack trace. The address of this string is written into `exceptionDescriptionLoc`.
 *
 * Otherwise, return a suitable error code on unexpected failure.
 *
 * @note this is an expensive operation and should be used with care.
 *
 * @note the C thread that calls Cangjie function doesn't become a fiber, it is still a native
 * thread. It means, that it will not be preempted during the execution of Cangjie code and this
 * thread will not start execution other  tasks from fibers queue. That's why such synchronous
 * calls of Cangjie code should be done with caution.
 */
CJErrorCode invokeCJFunction(void* retLoc, char** exceptionDescriptionLoc, CangjieRT rt, CJFunction func, ...);

/**
 * Asynchronously invoke specified function. Received handle could be used for result acquisition.
 * This call is not blocking, it immediately returns when task is scheduled.
 *
 * @param handleLoc pointer to the location where to put resulting handle.
 * @param rt `CangjieRT` instance that was created with `initCangjieRuntime` function.
 * @param func Cangjie function found by `findCangjieFunction`.
 * @param varargs Also, this function takes varargs, so, all needed by Cangjie function arguments could be passed there.
 *
 * @return `CJ_OK` on success; otherwise, returns a suitable error code on failure.
 */
CJErrorCode runCJFunctionAsync(CJThreadHandle* handleLoc, CangjieRT rt, CJFunction func, ...);

/**
 * Get the result of asynchrounsly invoked function.
 * Call of such function is blocking, if execution is still in progress, the caller will wait till it ends;
 * This function is very similar to `invokeCJFunction` function from synchronous part.
 *
 * @param retLoc address of the location, where to store the result, or `NULL` in case of `void` functions;
 * @param exceptionDescriptionLoc pointer to the location where resulting exception description will be stored.
 *                                If pointer is `NULL` the description will not be prepared.
 * @param rt `CangjieRT` instance that was created with `initCangjieRuntime` function.
 * @param handle the handle of the asynchronous invoked function.
 *
 * @return On normal executions the value returned could be one of the following:
 *
 * ```c
 * CJ_OK,
 * CJ_FAILED_WITH_EXCEPTION,
 * CJ_HANDLE_ALREADY_RELEASED,
 * ```
 *
 * If invocation of Cangjie function was successful, the status is set to `OK`, result is stored in
 * the location pointed by `retLoc`, and `exception_description` is `NULL`.
 *
 * However, if there was an **exception**: the status is set to `FAILED_WITH_EXCEPTION` and nothing
 * is written into `retLoc`. If `exceptionDescriptionLoc` is not `NULL` the C string with
 * description of exception is allocated: it contains name of exception, line number and stack
 * trace. The address of this string is written into `exceptionDescriptionLoc`.
 *
 * If accessed handle was already released to the moment when `getTaskResult` is called,
 * `CJ_HANDLE_ALREADY_RELEASED` is returned.
 *
 * Otherwise, return a suitable error code on unexpected failure.
 */
CJErrorCode getTaskResult(void* retLoc, char** exceptionDescriptionLoc, CangjieRT rt, CJThreadHandle handle);

/**
 * Releases `handle` to reclaim memory that used to store the task result. If task is still running
 * `releaseCJThreadHandle` will instantly return, without reclaiming any underlying memory.
 *
 * @param rt `CangjieRT` instance that was created with `initCangjieRuntime` function.
 * @param handle the instance of `CJThreadHandle` that should be released.
 *
 * @return `CJ_OK` on success; `CJ_TASK_IS_RUNNING` if the task is still running; otherwise,
 * returns a suitable error code on failure.
 */
CJErrorCode releaseCJThreadHandle(CangjieRT rt, CJThreadHandle handle);

#endif // CJ_INVOKE_API_H
