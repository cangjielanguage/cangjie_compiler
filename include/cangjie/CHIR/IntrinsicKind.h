// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the CHIR IntrinsicKind.
 */

#ifndef CANGJIE_CHIR_INTRINSICKIND_H
#define CANGJIE_CHIR_INTRINSICKIND_H

#include "cangjie/Utils/ConstantsUtils.h"
#include <string>
#include <unordered_map>

namespace Cangjie::CHIR {

/**
 * @brief Comparing the func name and package name is not a general way to distinguish
 * between different intrinsics. It requires there are no duplicate function name in the
 * same package.
 *
 * We should move to a more general method, such as adding  a @collect["some name"]
 * annotation in CJ so that parser will collect the corresponding Decl into a map.
 * This is also useful in CHIR where we need to find specific declarations by name.
 * (suggested by David)
 *
 */
// CORE
static const std::string SIZE_OF_NAME = "sizeOfType";
static const std::string ALIGN_OF_NAME = "alignOfType";
static const std::string ARRAY_ACQUIRE_RAW_DATA_NAME = "acquireRawData";
static const std::string ARRAY_RELEASE_RAW_DATA_NAME = "releaseRawData";
static const std::string ARRAY_BUILT_IN_COPY_TO_NAME = "intrinsicBuiltInCopyTo";
static const std::string ARRAY_GET_NAME = "arrayGet";
static const std::string ARRAY_SET_NAME = "arraySet";
static const std::string ARRAY_GET_UNCHECKED_NAME = "arrayGetUnchecked";

static const std::string ARRAY_GET_REF_UNCHECKED_NAME = "arrayGetRefUnchecked"; // cjnative only

static const std::string ARRAY_SET_UNCHECKED_NAME = "arraySetUnchecked";
static const std::string ARRAY_SIZE_NAME = "arraySize";
static const std::string ARRAY_CLONE_NAME = "arrayClone";
static const std::string ARRAY_SLICE_INIT_NAME = "arraySliceInit";
static const std::string ARRAY_SLICE_NAME = "arraySlice";
static const std::string ARRAY_SLICE_RAWARRAY_NAME = "rawArrayOfArraySlice";
static const std::string ARRAY_SLICE_START_NAME = "startOfArraySlice";
static const std::string ARRAY_SLICE_SIZE_NAME = "sizeOfArraySlice";
static const std::string ARRAY_SLICE_GET_ELEMENT_NAME = "arraySliceGet";
static const std::string ARRAY_SLICE_SET_ELEMENT_NAME = "arraySliceSet";
static const std::string ARRAY_SLICE_GET_ELEMENT_UNCHECKED_NAME = "arraySliceGetUnchecked";
static const std::string ARRAY_SLICE_SET_ELEMENT_UNCHECKED_NAME = "arraySliceSetUnchecked";

// vector intrinsic for array operation
static const std::string VECTOR_COMPARE_32_NAME = "vectorCompare32";      // cjnative only
static const std::string VECTOR_INDEX_BYTE_32_NAME = "vectorIndexByte32"; // cjnative only

// Foreign functions translated to intrinsic functions in the interpreter
// These functions are not not marked as intrinsic in Cangjie but they are translated to intrinsics in BCHIR

const std::string CJ_CORE_CAN_USE_SIMD_NAME = "CJ_CORE_CanUseSIMD"; // cjnative only

static const std::string CJ_TLS_DYN_SET_SESSION_CALLBACK_NAME = "CJ_TLS_DYN_SetSessionCallback";
static const std::string CJ_TLS_DYN_SSL_INIT_NAME = "CJ_TLS_DYN_SslInit";

static const std::string FILL_IN_STACK_TRACE_NAME = "fillInStackTrace";
static const std::string DECODE_STACK_TRACE_NAME = "decodeStackTrace";

static const std::string CPOINTER_GET_POINTER_ADDRESS_NAME = "getPointerAddress";
static const std::string CPOINTER_READ_NAME = "readPointer";
static const std::string CPOINTER_WRITE_NAME = "writePointer";
static const std::string CPOINTER_ADD_NAME = "addPointer";

static const std::string CSTRING_CONVERT_CSTR_TO_PTR_NAME = "convertCStr2Ptr";
static const std::string BIT_CAST_NAME = "bitCast";

/*
 * Used for the mock framework purpuses, to match concrete types of type parameters to the defined stubs
 * The intrinsic doesn't have a declaration and for its call,
 * it's intended to be generated dynamically in the compiler front-end
 */
static const std::string GET_TYPE_FOR_TYPE_PARAMETER_NAME = GET_TYPE_FOR_TYPE_PARAMETER_FUNC_NAME;
static const std::string IS_SUBTYPE_TYPES_NAME = IS_SUBTYPE_TYPES_FUNC_NAME;

// Package runtime
static const std::string INVOKE_GC_NAME = "invokeGC";
static const std::string SET_GC_THRESHOLD_NAME = "setCjGCThreshold";
static const std::string GET_MAX_HEAP_SIZE_NAME = "getMaxCjHeapSize";
static const std::string GET_ALLOCATE_HEAP_SIZE_NAME = "getAllocatedCjHeapSize";
static const std::string DUMP_CJ_HEAP_DATA_NAME = "dumpCjHeapData";
static const std::string GET_GC_COUNT_NAME = "getCjGCCount";
static const std::string GET_GC_TIME_US_NAME = "getCjGCTime";
static const std::string GET_GC_FREED_SIZE_NAME = "getCjGCFreedSize";
static const std::string START_CJ_CPU_PROFILING_NAME = "startCjCPUProfiling";
static const std::string STOP_CJ_CPU_PROFILING_NAME = "stopCjCPUProfiling";
static const std::string GET_REAL_HEAP_SIZE_NAME = "getRealHeapSize";

static const std::string GET_THREAD_NUMBER_NAME = "getThreadNumber";
static const std::string GET_BLOCKING_THREAD_NUMBER_NAME = "getBlockingThreadNumber";
static const std::string GET_NATIVE_THREAD_NUMBER_NAME = "getNativeThreadNumber";

static const std::string FUTURE_INIT_NAME = "futureInit";
static const std::string OBJECT_REFEQ_NAME = "intrinsicRefEq";

static const std::string RAW_ARRAY_REFEQ_NAME = "intrinsicRefEqRawArray"; // cjnative only

static const std::string OBJECT_ZERO_VALUE_NAME = "zeroValue";

static const std::string IS_THREAD_OBJECT_INITED_NAME = "isThreadObjectInited";
static const std::string GET_THREAD_OBJECT_NAME = "getThreadObject";
static const std::string SET_THREAD_OBJECT_NAME = "setThreadObject";

static const std::string FUTURE_IS_COMPLETE_NAME = "futureIsComplete"; // cjnative only
static const std::string FUTURE_WAIT_NAME = "futureWait";              // cjnative only
static const std::string FUTURE_NOTIFYALL_NAME = "futureNotifyAll";    // cjnative only

// ============================ cjnative only start ==============================
// REFLECTION
#define REFLECTION_KIND_TO_RUNTIME_FUNCTION(REFLECTION_KIND, CJ_FUNCTION, RUNTIME_FUNCTION)                            \
    static const std::string REFLECTION_KIND##_NAME = #CJ_FUNCTION;
#include "cangjie/CHIR/LLVMReflectionIntrinsics.def"
#undef REFLECTION_KIND_TO_RUNTIME_FUNCTION
// ============================ cjnative only end ==============================

// these intrinsics will be removed in later version
static const std::string OVERFLOW_CHECKED_ADD_NAME = "checkedAdd";
static const std::string OVERFLOW_CHECKED_SUB_NAME = "checkedSub";
static const std::string OVERFLOW_CHECKED_MUL_NAME = "checkedMul";
static const std::string OVERFLOW_CHECKED_DIV_NAME = "checkedDiv";
static const std::string OVERFLOW_CHECKED_MOD_NAME = "checkedMod";
static const std::string OVERFLOW_CHECKED_POW_NAME = "checkedPow";
static const std::string OVERFLOW_CHECKED_INC_NAME = "checkedInc";
static const std::string OVERFLOW_CHECKED_DEC_NAME = "checkedDec";
static const std::string OVERFLOW_CHECKED_NEG_NAME = "checkedNeg";
static const std::string OVERFLOW_THROWING_ADD_NAME = "throwingAdd";
static const std::string OVERFLOW_THROWING_SUB_NAME = "throwingSub";
static const std::string OVERFLOW_THROWING_MUL_NAME = "throwingMul";
static const std::string OVERFLOW_THROWING_DIV_NAME = "throwingDiv";
static const std::string OVERFLOW_THROWING_MOD_NAME = "throwingMod";
static const std::string OVERFLOW_THROWING_POW_NAME = "throwingPow";
static const std::string OVERFLOW_THROWING_INC_NAME = "throwingInc";
static const std::string OVERFLOW_THROWING_DEC_NAME = "throwingDec";
static const std::string OVERFLOW_THROWING_NEG_NAME = "throwingNeg";
static const std::string OVERFLOW_SATURATING_ADD_NAME = "saturatingAdd";
static const std::string OVERFLOW_SATURATING_SUB_NAME = "saturatingSub";
static const std::string OVERFLOW_SATURATING_MUL_NAME = "saturatingMul";
static const std::string OVERFLOW_SATURATING_DIV_NAME = "saturatingDiv";
static const std::string OVERFLOW_SATURATING_MOD_NAME = "saturatingMod";
static const std::string OVERFLOW_SATURATING_POW_NAME = "saturatingPow";
static const std::string OVERFLOW_SATURATING_INC_NAME = "saturatingInc";
static const std::string OVERFLOW_SATURATING_DEC_NAME = "saturatingDec";
static const std::string OVERFLOW_SATURATING_NEG_NAME = "saturatingNeg";

static const std::string OVERFLOW_WRAPPING_ADD_NAME = "wrappingAdd";
static const std::string OVERFLOW_WRAPPING_SUB_NAME = "wrappingSub";
static const std::string OVERFLOW_WRAPPING_MUL_NAME = "wrappingMul";
static const std::string OVERFLOW_WRAPPING_DIV_NAME = "wrappingDiv";
static const std::string OVERFLOW_WRAPPING_MOD_NAME = "wrappingMod";
static const std::string OVERFLOW_WRAPPING_POW_NAME = "wrappingPow";
static const std::string OVERFLOW_WRAPPING_INC_NAME = "wrappingInc";
static const std::string OVERFLOW_WRAPPING_DEC_NAME = "wrappingDec";
static const std::string OVERFLOW_WRAPPING_NEG_NAME = "wrappingNeg";

static const std::string SLEEP_NAME = "intrinsicSleep";

static const std::string SOURCE_FILE_NAME = "sourceFile";
static const std::string SOURCE_LINE_NAME = "sourceLine";
static const std::string IDENTITY_HASHCODE_NAME = "identityHashCodeForRefType";
static const std::string IDENTITY_HASHCODE_FOR_ARRAY_NAME = "identityHashCodeForArrayType";

static const std::string VARRAY_SET_NAME = "varraySet";
static const std::string VARRAY_GET_NAME = "varrayGet";

// ============================ cjnative only start ==============================
// SYNC
static const std::string ATOMIC_LOAD_NAME = "load";
static const std::string ATOMIC_STORE_NAME = "store";
static const std::string ATOMIC_SWAP_NAME = "swap";
static const std::string ATOMIC_COMPARE_AND_SWAP_NAME = "compareAndSwap";
static const std::string ATOMIC_FETCH_ADD_NAME = "fetchAdd";
static const std::string ATOMIC_FETCH_SUB_NAME = "fetchSub";
static const std::string ATOMIC_FETCH_AND_NAME = "fetchAnd";
static const std::string ATOMIC_FETCH_OR_NAME = "fetchOr";
static const std::string ATOMIC_FETCH_XOR_NAME = "fetchXor";
static const std::string MUTEX_INIT_NAME = "mutexInit";
static const std::string MUTEX_LOCK_NAME = "mutexLock";
static const std::string MUTEX_TRY_LOCK_NAME = "mutexTryLock";
static const std::string MUTEX_CHECK_STATUS_NAME = "mutexCheckStatus";
static const std::string MUTEX_UNLOCK_NAME = "mutexUnlock";
static const std::string WAITQUEUE_INIT_NAME = "waitQueueInit";
static const std::string MONITOR_INIT_NAME = "monitorInit";
static const std::string MOITIOR_WAIT_NAME = "monitorWait";
static const std::string MOITIOR_NOTIFY_NAME = "monitorNotify";
static const std::string MOITIOR_NOTIFY_ALL_NAME = "monitorNotifyAll";
static const std::string MULTICONDITION_WAIT_NAME = "multiConditionMonitorWait";
static const std::string MULTICONDITION_NOTIFY_NAME = "multiConditionMonitorNotify";
static const std::string MULTICONDITION_NOTIFY_ALL_NAME = "multiConditionMonitorNotifyAll";
// ============================ cjnative only end ==============================

// Math
static const std::string ABS_NAME = "intrinsicAbs";
static const std::string FABS_NAME = "intrinsicFabs";
static const std::string FLOOR_NAME = "intrinsicFloor";
static const std::string CEIL_NAME = "intrinsicCeil";
static const std::string TRUC_NAME = "intrinsicTrunc";
static const std::string SIN_NAME = "intrinsicSin";
static const std::string COS_NAME = "intrinsicCos";
static const std::string EXP_NAME = "intrinsicExp";
static const std::string EXP2_NAME = "intrinsicExp2";
static const std::string LOG_NAME = "intrinsicLog";
static const std::string LOG2_NAME = "intrinsicLog2";
static const std::string LOG10_NAME = "intrinsicLog10";
static const std::string SQRT_NAME = "intrinsicSqrt";
static const std::string ROUND_NAME = "intrinsicRound";
static const std::string POW_NAME = "intrinsicPow";
static const std::string POWI_NAME = "intrinsicPowi";

/* C FFI functions
These C functions are used in the standard library but never defined.
We consider them intrinsic, similar to FFI_CJ_AST_*, so that we can link them statically
in the interpreter.
*/
static const std::string STRLEN_NAME = "strlen";
static const std::string MEMCPY_S_NAME = "memcpy_s";
static const std::string MEMSET_S_NAME = "memset_s";
static const std::string FREE_NAME = "free";
static const std::string MALLOC_NAME = "malloc";
static const std::string STRCMP_NAME = "strcmp";
static const std::string MEMCMP_NAME = "memcmp";
static const std::string STRNCMP_NAME = "strncmp";
static const std::string STRCASECMP_NAME = "strcasecmp";

static const std::string UNSAFE_BEGIN = "_unsafe_begin";
static const std::string UNSAFE_END = "_unsafe_end";
static const std::string POINTER_INIT = "_Pointer_Init_";
static const std::string CSTR_INIT = "_CString_Init_";
static const std::string CJ_AST_LEX = "CJ_AST_Lex";
static const std::string CJ_ASTPARSEEXPR = "CJ_AST_ParseExpr";
static const std::string CJ_ASTPARSEDECL = "CJ_AST_ParseDecl";
static const std::string CJ_ASTPARSE_PROPMEMBERDECL = "CJ_AST_ParsePropMemberDecl";
static const std::string CJ_ASTPARSE_PRICONSTRUCTOR = "CJ_AST_ParsePrimaryConstructor";
static const std::string CJ_ASTPARSEPATTERN = "CJ_AST_ParsePattern";
static const std::string CJ_ASTPARSETYPE = "CJ_AST_ParseType";
static const std::string CJ_ASTPARSETOPLEVEL = "CJ_AST_ParseTopLevel";
static const std::string CJ_PARENT_CONTEXT = "CJ_CheckParentContext";
static const std::string CJ_MACRO_ITEM_INFO = "CJ_SetItemInfo";
static const std::string CJ_GET_CHILD_MESSAGES = "CJ_GetChildMessages";
static const std::string CJ_CHECK_ADD_SPACE = "CJ_CheckAddSpace";
static const std::string CJ_AST_DIAGREPORT = "CJ_AST_DiagReport";

/**
 * @brief In the future we should generate intrinsic/xxx node in CHIR
 *        to represent intrinsic functions. That way we don't need intrinsic
 *        to have function body as well. We can keep using this enum tho.
 *
 */
enum IntrinsicKind : uint16_t {
    NOT_INTRINSIC,
    NOT_IMPLEMENTED,

    // For hoisting, but we should later split arraybuilder
    // into allocation and initialisation
    ARRAY_INIT,

    // CORE
    SIZE_OF,
    ALIGN_OF,
    ARRAY_ACQUIRE_RAW_DATA,
    ARRAY_RELEASE_RAW_DATA,
    ARRAY_BUILT_IN_COPY_TO,
    ARRAY_GET,
    ARRAY_SET,
    ARRAY_GET_UNCHECKED,
    ARRAY_GET_REF_UNCHECKED,
    ARRAY_SET_UNCHECKED,
    ARRAY_SIZE,
    ARRAY_CLONE,
    ARRAY_SLICE_INIT,
    ARRAY_SLICE,
    ARRAY_SLICE_RAWARRAY,
    ARRAY_SLICE_START,
    ARRAY_SLICE_SIZE,
    ARRAY_SLICE_GET_ELEMENT,
    ARRAY_SLICE_GET_ELEMENT_UNCHECKED,
    ARRAY_SLICE_SET_ELEMENT,
    ARRAY_SLICE_SET_ELEMENT_UNCHECKED,
    FILL_IN_STACK_TRACE,
    DECODE_STACK_TRACE,

    CHR,
    ORD,

    CPOINTER_GET_POINTER_ADDRESS,
    CPOINTER_INIT0, // CPointer constructor with no arguments
    CPOINTER_INIT1, // CPointer constructor with one argument
    CPOINTER_READ,
    CPOINTER_WRITE,
    CPOINTER_ADD,

    CSTRING_INIT,
    CSTRING_CONVERT_CSTR_TO_PTR,

    INOUT_PARAM,

    REGISTER_WATCHED_OBJECT,

    OBJECT_REFEQ,

    RAW_ARRAY_REFEQ, // cjnative only

    OBJECT_ZERO_VALUE,

    INVOKE_GC,
    SET_GC_THRESHOLD,
    DUMP_CJ_HEAP_DATA,
    GET_GC_COUNT,
    GET_GC_TIME_US,
    GET_GC_FREED_SIZE,
    START_CJ_CPU_PROFILING,
    STOP_CJ_CPU_PROFILING,
    GET_MAX_HEAP_SIZE,
    GET_ALLOCATE_HEAP_SIZE,
    GET_REAL_HEAP_SIZE,
    GET_THREAD_NUMBER,
    GET_BLOCKING_THREAD_NUMBER,
    GET_NATIVE_THREAD_NUMBER,

    VARRAY_SET,
    VARRAY_GET,

    // About Future
    FUTURE_INIT,

    FUTURE_IS_COMPLETE, // cjnative only
    FUTURE_WAIT,        // cjnative only
    FUTURE_NOTIFYALL,   // cjnative only

    IS_THREAD_OBJECT_INITED,
    GET_THREAD_OBJECT,
    SET_THREAD_OBJECT,

    OVERFLOW_CHECKED_ADD,
    OVERFLOW_CHECKED_SUB,
    OVERFLOW_CHECKED_MUL,
    OVERFLOW_CHECKED_DIV,
    OVERFLOW_CHECKED_MOD,
    OVERFLOW_CHECKED_POW,
    OVERFLOW_CHECKED_INC,
    OVERFLOW_CHECKED_DEC,
    OVERFLOW_CHECKED_NEG,
    OVERFLOW_THROWING_ADD,
    OVERFLOW_THROWING_SUB,
    OVERFLOW_THROWING_MUL,
    OVERFLOW_THROWING_DIV,
    OVERFLOW_THROWING_MOD,
    OVERFLOW_THROWING_POW,
    OVERFLOW_THROWING_INC,
    OVERFLOW_THROWING_DEC,
    OVERFLOW_THROWING_NEG,
    OVERFLOW_SATURATING_ADD,
    OVERFLOW_SATURATING_SUB,
    OVERFLOW_SATURATING_MUL,
    OVERFLOW_SATURATING_DIV,
    OVERFLOW_SATURATING_MOD,
    OVERFLOW_SATURATING_POW,
    OVERFLOW_SATURATING_INC,
    OVERFLOW_SATURATING_DEC,
    OVERFLOW_SATURATING_NEG,
    OVERFLOW_WRAPPING_ADD,
    OVERFLOW_WRAPPING_SUB,
    OVERFLOW_WRAPPING_MUL,
    OVERFLOW_WRAPPING_DIV,
    OVERFLOW_WRAPPING_MOD,
    OVERFLOW_WRAPPING_POW,
    OVERFLOW_WRAPPING_INC,
    OVERFLOW_WRAPPING_DEC,
    OVERFLOW_WRAPPING_NEG,

    // llvm vector instructions for string optimization

    VECTOR_COMPARE_32,    // cjnative only
    VECTOR_INDEX_BYTE_32, // cjnative only

    // Foreign functions translated to intrinsic functions in the interpreter,
    // These functions are not not marked as intrinsic in Cangjie but they are translated to intrinsics in BCHIR

    CJ_CORE_CAN_USE_SIMD, // cjnative only

    CJ_TLS_DYN_SET_SESSION_CALLBACK,
    CJ_TLS_DYN_SSL_INIT,

    // ============================ cjnative only start =================
    REFLECTION_INTRINSIC_START_FLAG,
#define REFLECTION_KIND_TO_RUNTIME_FUNCTION(REFLECTION_KIND, CJ_FUNCTION, RUNTIME_FUNCTION) REFLECTION_KIND,
#include "cangjie/CHIR/LLVMReflectionIntrinsics.def"
#undef REFLECTION_KIND_TO_RUNTIME_FUNCTION
    REFLECTION_INTRINSIC_END_FLAG,
    // ============================ cjnative only end =================

    SLEEP,

    SOURCE_FILE,
    SOURCE_LINE,

    IDENTITY_HASHCODE,
    IDENTITY_HASHCODE_FOR_ARRAY,

    // ============================ cjnative only start =================
    // SYNC
    ATOMIC_LOAD,
    ATOMIC_STORE,
    ATOMIC_SWAP,
    ATOMIC_COMPARE_AND_SWAP,
    ATOMIC_FETCH_ADD,
    ATOMIC_FETCH_SUB,
    ATOMIC_FETCH_AND,
    ATOMIC_FETCH_OR,
    ATOMIC_FETCH_XOR,
    MUTEX_INIT,
    CJ_MUTEX_LOCK,
    MUTEX_TRY_LOCK,
    MUTEX_CHECK_STATUS,
    MUTEX_UNLOCK,
    WAITQUEUE_INIT,
    MONITOR_INIT,
    MOITIOR_WAIT,
    MOITIOR_NOTIFY,
    MOITIOR_NOTIFY_ALL,
    MULTICONDITION_WAIT,
    MULTICONDITION_NOTIFY,
    MULTICONDITION_NOTIFY_ALL,
    // ============================ cjnative only end =================

    // Syscall

    // AST lib FFI
    FFI_CJ_AST_LEX,
    FFI_CJ_AST_PARSEEXPR,
    FFI_CJ_AST_PARSEDECL,
    FFI_CJ_AST_PARSE_PROPMEMBERDECL,
    FFI_CJ_AST_PARSE_PRICONSTRUCTOR,
    FFI_CJ_AST_PARSE_PATTERN,
    FFI_CJ_AST_PARSE_TYPE,
    FFI_CJ_AST_PARSETOPLEVEL,
    FFI_CJ_AST_DIAGREPORT,

    // Macro With Context FFI
    FFI_CJ_PARENT_CONTEXT,
    FFI_CJ_MACRO_ITEM_INFO,
    FFI_CJ_GET_CHILD_MESSAGES,
    FFI_CJ_CHECK_ADD_SPACE,
    // CodeGen
    CG_UNSAFE_BEGIN,
    CG_UNSAFE_END,

    // C FFI funcs
    STRLEN,
    MEMCPY_S,
    MEMSET_S,
    FREE,
    MALLOC,
    STRCMP,
    MEMCMP,
    STRNCMP,
    STRCASECMP,

    // The interpreter is using these for cjnative backend as well
    ATOMIC_INT8_LOAD,
    ATOMIC_INT8_STORE,
    ATOMIC_INT8_SWAP,
    ATOMIC_INT8_CAS,
    ATOMIC_INT8_FETCH_ADD,
    ATOMIC_INT8_FETCH_SUB,
    ATOMIC_INT8_FETCH_AND,
    ATOMIC_INT8_FETCH_OR,
    ATOMIC_INT8_FETCH_XOR,

    ATOMIC_INT16_LOAD,
    ATOMIC_INT16_STORE,
    ATOMIC_INT16_SWAP,
    ATOMIC_INT16_CAS,
    ATOMIC_INT16_FETCH_ADD,
    ATOMIC_INT16_FETCH_SUB,
    ATOMIC_INT16_FETCH_AND,
    ATOMIC_INT16_FETCH_OR,
    ATOMIC_INT16_FETCH_XOR,

    ATOMIC_INT32_LOAD,
    ATOMIC_INT32_STORE,
    ATOMIC_INT32_SWAP,
    ATOMIC_INT32_CAS,
    ATOMIC_INT32_FETCH_ADD,
    ATOMIC_INT32_FETCH_SUB,
    ATOMIC_INT32_FETCH_AND,
    ATOMIC_INT32_FETCH_OR,
    ATOMIC_INT32_FETCH_XOR,

    ATOMIC_INT64_LOAD,
    ATOMIC_INT64_STORE,
    ATOMIC_INT64_SWAP,
    ATOMIC_INT64_CAS,
    ATOMIC_INT64_FETCH_ADD,
    ATOMIC_INT64_FETCH_SUB,
    ATOMIC_INT64_FETCH_AND,
    ATOMIC_INT64_FETCH_OR,
    ATOMIC_INT64_FETCH_XOR,

    ATOMIC_UINT8_LOAD,
    ATOMIC_UINT8_STORE,
    ATOMIC_UINT8_SWAP,
    ATOMIC_UINT8_CAS,
    ATOMIC_UINT8_FETCH_ADD,
    ATOMIC_UINT8_FETCH_SUB,
    ATOMIC_UINT8_FETCH_AND,
    ATOMIC_UINT8_FETCH_OR,
    ATOMIC_UINT8_FETCH_XOR,

    ATOMIC_UINT16_LOAD,
    ATOMIC_UINT16_STORE,
    ATOMIC_UINT16_SWAP,
    ATOMIC_UINT16_CAS,
    ATOMIC_UINT16_FETCH_ADD,
    ATOMIC_UINT16_FETCH_SUB,
    ATOMIC_UINT16_FETCH_AND,
    ATOMIC_UINT16_FETCH_OR,
    ATOMIC_UINT16_FETCH_XOR,

    ATOMIC_UINT32_LOAD,
    ATOMIC_UINT32_STORE,
    ATOMIC_UINT32_SWAP,
    ATOMIC_UINT32_CAS,
    ATOMIC_UINT32_FETCH_ADD,
    ATOMIC_UINT32_FETCH_SUB,
    ATOMIC_UINT32_FETCH_AND,
    ATOMIC_UINT32_FETCH_OR,
    ATOMIC_UINT32_FETCH_XOR,

    ATOMIC_UINT64_LOAD,
    ATOMIC_UINT64_STORE,
    ATOMIC_UINT64_SWAP,
    ATOMIC_UINT64_CAS,
    ATOMIC_UINT64_FETCH_ADD,
    ATOMIC_UINT64_FETCH_SUB,
    ATOMIC_UINT64_FETCH_AND,
    ATOMIC_UINT64_FETCH_OR,
    ATOMIC_UINT64_FETCH_XOR,

    ATOMIC_BOOL_LOAD,
    ATOMIC_BOOL_STORE,
    ATOMIC_BOOL_SWAP,
    ATOMIC_BOOL_CAS,

    ATOMIC_REFERENCEBASE_LOAD,
    ATOMIC_REFERENCEBASE_STORE,
    ATOMIC_REFERENCEBASE_SWAP,
    ATOMIC_REFERENCEBASE_CAS,

    ATOMIC_OPTIONREFERENCE_LOAD,
    ATOMIC_OPTIONREFERENCE_STORE,
    ATOMIC_OPTIONREFERENCE_SWAP,
    ATOMIC_OPTIONREFERENCE_CAS,

    // CHIR 2: Exception intrinsic
    BEGIN_CATCH,

    // CHIR 2: Math intrinsic
    ABS,
    FABS,
    FLOOR,
    CEIL,
    TRUNC,
    SIN,
    COS,
    EXP,
    EXP2,
    LOG,
    LOG2,
    LOG10,
    SQRT,
    ROUND,
    POW,
    POWI,
    BIT_CAST,

    // preinitialize intrinsic
    PREINITIALIZE,
    // Box cast intrinsic
    OBJECT_AS,
    IS_NULL,

    GET_TYPE_FOR_TYPE_PARAMETER,
    IS_SUBTYPE_TYPES,
};

static const std::unordered_map<std::string, IntrinsicKind> coreIntrinsicMap = {
    {SIZE_OF_NAME, SIZE_OF},
    {ALIGN_OF_NAME, ALIGN_OF},
    {ARRAY_ACQUIRE_RAW_DATA_NAME, ARRAY_ACQUIRE_RAW_DATA},
    {ARRAY_RELEASE_RAW_DATA_NAME, ARRAY_RELEASE_RAW_DATA},
    {ARRAY_BUILT_IN_COPY_TO_NAME, ARRAY_BUILT_IN_COPY_TO},
    {ARRAY_GET_NAME, ARRAY_GET},
    {ARRAY_SET_NAME, ARRAY_SET},
    {ARRAY_GET_UNCHECKED_NAME, ARRAY_GET_UNCHECKED},

    {ARRAY_GET_REF_UNCHECKED_NAME, ARRAY_GET_REF_UNCHECKED},

    {ARRAY_SET_UNCHECKED_NAME, ARRAY_SET_UNCHECKED},
    {ARRAY_SIZE_NAME, ARRAY_SIZE},
    {ARRAY_CLONE_NAME, ARRAY_CLONE},
    {ARRAY_SLICE_INIT_NAME, ARRAY_SLICE_INIT},
    {ARRAY_SLICE_NAME, ARRAY_SLICE},
    {ARRAY_SLICE_RAWARRAY_NAME, ARRAY_SLICE_RAWARRAY},
    {ARRAY_SLICE_START_NAME, ARRAY_SLICE_START},
    {ARRAY_SLICE_SIZE_NAME, ARRAY_SLICE_SIZE},
    {ARRAY_SLICE_GET_ELEMENT_NAME, ARRAY_SLICE_GET_ELEMENT},
    {ARRAY_SLICE_SET_ELEMENT_NAME, ARRAY_SLICE_SET_ELEMENT},
    {ARRAY_SLICE_GET_ELEMENT_UNCHECKED_NAME, ARRAY_SLICE_GET_ELEMENT_UNCHECKED},
    {ARRAY_SLICE_SET_ELEMENT_UNCHECKED_NAME, ARRAY_SLICE_SET_ELEMENT_UNCHECKED},

    {VECTOR_COMPARE_32_NAME, VECTOR_COMPARE_32},
    {VECTOR_INDEX_BYTE_32_NAME, VECTOR_INDEX_BYTE_32},

    {FILL_IN_STACK_TRACE_NAME, FILL_IN_STACK_TRACE},
    {DECODE_STACK_TRACE_NAME, DECODE_STACK_TRACE},

    {CPOINTER_GET_POINTER_ADDRESS_NAME, CPOINTER_GET_POINTER_ADDRESS},
    {CPOINTER_READ_NAME, CPOINTER_READ},
    {CPOINTER_WRITE_NAME, CPOINTER_WRITE},
    {CPOINTER_ADD_NAME, CPOINTER_ADD},

    {CSTRING_CONVERT_CSTR_TO_PTR_NAME, CSTRING_CONVERT_CSTR_TO_PTR},
    {BIT_CAST_NAME, BIT_CAST},

    {FUTURE_INIT_NAME, FUTURE_INIT},

    {IS_THREAD_OBJECT_INITED_NAME, IS_THREAD_OBJECT_INITED},
    {GET_THREAD_OBJECT_NAME, GET_THREAD_OBJECT},
    {SET_THREAD_OBJECT_NAME, SET_THREAD_OBJECT},

    {FUTURE_IS_COMPLETE_NAME, FUTURE_IS_COMPLETE},
    {FUTURE_WAIT_NAME, FUTURE_WAIT},
    {FUTURE_NOTIFYALL_NAME, FUTURE_NOTIFYALL},

    {OBJECT_REFEQ_NAME, OBJECT_REFEQ},

    {RAW_ARRAY_REFEQ_NAME, RAW_ARRAY_REFEQ},

    {OBJECT_ZERO_VALUE_NAME, OBJECT_ZERO_VALUE},

    {SOURCE_FILE_NAME, SOURCE_FILE},
    {SOURCE_LINE_NAME, SOURCE_LINE},

    {IDENTITY_HASHCODE_NAME, IDENTITY_HASHCODE},
    {IDENTITY_HASHCODE_FOR_ARRAY_NAME, IDENTITY_HASHCODE_FOR_ARRAY},
    {STRLEN_NAME, STRLEN},
    {MEMCPY_S_NAME, MEMCPY_S},
    {MEMSET_S_NAME, MEMSET_S},
    {FREE_NAME, FREE},
    {MALLOC_NAME, MALLOC},
    {STRCMP_NAME, STRCMP},
    {MEMCMP_NAME, MEMCMP},
    {STRNCMP_NAME, STRNCMP},
    {STRCASECMP_NAME, STRCASECMP},

    // atomic for Thread Class

    {ATOMIC_LOAD_NAME, ATOMIC_LOAD},
    {ATOMIC_STORE_NAME, ATOMIC_STORE},
    {ATOMIC_FETCH_ADD_NAME, ATOMIC_FETCH_ADD},
    {ATOMIC_COMPARE_AND_SWAP_NAME, ATOMIC_COMPARE_AND_SWAP},

    {SLEEP_NAME, SLEEP},

    {GET_TYPE_FOR_TYPE_PARAMETER_NAME, GET_TYPE_FOR_TYPE_PARAMETER},
    {IS_SUBTYPE_TYPES_NAME, IS_SUBTYPE_TYPES},
};

static const std::unordered_map<std::string, IntrinsicKind> overflowIntrinsicMap = {
    {OVERFLOW_CHECKED_ADD_NAME, OVERFLOW_CHECKED_ADD},
    {OVERFLOW_CHECKED_SUB_NAME, OVERFLOW_CHECKED_SUB},
    {OVERFLOW_CHECKED_MUL_NAME, OVERFLOW_CHECKED_MUL},
    {OVERFLOW_CHECKED_DIV_NAME, OVERFLOW_CHECKED_DIV},
    {OVERFLOW_CHECKED_MOD_NAME, OVERFLOW_CHECKED_MOD},
    {OVERFLOW_CHECKED_POW_NAME, OVERFLOW_CHECKED_POW},
    {OVERFLOW_CHECKED_INC_NAME, OVERFLOW_CHECKED_INC},
    {OVERFLOW_CHECKED_DEC_NAME, OVERFLOW_CHECKED_DEC},
    {OVERFLOW_CHECKED_NEG_NAME, OVERFLOW_CHECKED_NEG},
    {OVERFLOW_THROWING_ADD_NAME, OVERFLOW_THROWING_ADD},
    {OVERFLOW_THROWING_SUB_NAME, OVERFLOW_THROWING_SUB},
    {OVERFLOW_THROWING_MUL_NAME, OVERFLOW_THROWING_MUL},
    {OVERFLOW_THROWING_DIV_NAME, OVERFLOW_THROWING_DIV},
    {OVERFLOW_THROWING_MOD_NAME, OVERFLOW_THROWING_MOD},
    {OVERFLOW_THROWING_POW_NAME, OVERFLOW_THROWING_POW},
    {OVERFLOW_THROWING_INC_NAME, OVERFLOW_THROWING_INC},
    {OVERFLOW_THROWING_DEC_NAME, OVERFLOW_THROWING_DEC},
    {OVERFLOW_THROWING_NEG_NAME, OVERFLOW_THROWING_NEG},
    {OVERFLOW_SATURATING_ADD_NAME, OVERFLOW_SATURATING_ADD},
    {OVERFLOW_SATURATING_SUB_NAME, OVERFLOW_SATURATING_SUB},
    {OVERFLOW_SATURATING_MUL_NAME, OVERFLOW_SATURATING_MUL},
    {OVERFLOW_SATURATING_DIV_NAME, OVERFLOW_SATURATING_DIV},
    {OVERFLOW_SATURATING_MOD_NAME, OVERFLOW_SATURATING_MOD},
    {OVERFLOW_SATURATING_POW_NAME, OVERFLOW_SATURATING_POW},
    {OVERFLOW_SATURATING_INC_NAME, OVERFLOW_SATURATING_INC},
    {OVERFLOW_SATURATING_DEC_NAME, OVERFLOW_SATURATING_DEC},
    {OVERFLOW_SATURATING_NEG_NAME, OVERFLOW_SATURATING_NEG},
    {OVERFLOW_WRAPPING_ADD_NAME, OVERFLOW_WRAPPING_ADD},
    {OVERFLOW_WRAPPING_SUB_NAME, OVERFLOW_WRAPPING_SUB},
    {OVERFLOW_WRAPPING_MUL_NAME, OVERFLOW_WRAPPING_MUL},
    {OVERFLOW_WRAPPING_DIV_NAME, OVERFLOW_WRAPPING_DIV},
    {OVERFLOW_WRAPPING_MOD_NAME, OVERFLOW_WRAPPING_MOD},
    {OVERFLOW_WRAPPING_POW_NAME, OVERFLOW_WRAPPING_POW},
    {OVERFLOW_WRAPPING_INC_NAME, OVERFLOW_WRAPPING_INC},
    {OVERFLOW_WRAPPING_DEC_NAME, OVERFLOW_WRAPPING_DEC},
    {OVERFLOW_WRAPPING_NEG_NAME, OVERFLOW_WRAPPING_NEG},
};
static const std::unordered_map<std::string, IntrinsicKind> reflectIntrinsicMap = {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#define REFLECTION_KIND_TO_RUNTIME_FUNCTION(REFLECTION_KIND, CJ_FUNCTION, RUNTIME_FUNCTION)                            \
    {REFLECTION_KIND##_NAME, REFLECTION_KIND},
#include "cangjie/CHIR/LLVMReflectionIntrinsics.def"
#undef REFLECTION_KIND_TO_RUNTIME_FUNCTION
#endif
};

static const std::unordered_map<std::string, IntrinsicKind> cjnativeSyncIntrinsicMap = {
    {ATOMIC_LOAD_NAME, ATOMIC_LOAD},
    {ATOMIC_STORE_NAME, ATOMIC_STORE},
    {ATOMIC_SWAP_NAME, ATOMIC_SWAP},
    {ATOMIC_COMPARE_AND_SWAP_NAME, ATOMIC_COMPARE_AND_SWAP},
    {ATOMIC_FETCH_ADD_NAME, ATOMIC_FETCH_ADD},
    {ATOMIC_FETCH_SUB_NAME, ATOMIC_FETCH_SUB},
    {ATOMIC_FETCH_AND_NAME, ATOMIC_FETCH_AND},
    {ATOMIC_FETCH_OR_NAME, ATOMIC_FETCH_OR},
    {ATOMIC_FETCH_XOR_NAME, ATOMIC_FETCH_XOR},
    {MUTEX_INIT_NAME, MUTEX_INIT},
    {MUTEX_LOCK_NAME, CJ_MUTEX_LOCK},
    {MUTEX_TRY_LOCK_NAME, MUTEX_TRY_LOCK},
    {MUTEX_CHECK_STATUS_NAME, MUTEX_CHECK_STATUS},
    {MUTEX_UNLOCK_NAME, MUTEX_UNLOCK},
    {WAITQUEUE_INIT_NAME, WAITQUEUE_INIT},
    {MONITOR_INIT_NAME, MONITOR_INIT},
    {MOITIOR_WAIT_NAME, MOITIOR_WAIT},
    {MOITIOR_NOTIFY_NAME, MOITIOR_NOTIFY},
    {MOITIOR_NOTIFY_ALL_NAME, MOITIOR_NOTIFY_ALL},
    {MULTICONDITION_WAIT_NAME, MULTICONDITION_WAIT},
    {MULTICONDITION_NOTIFY_NAME, MULTICONDITION_NOTIFY},
    {MULTICONDITION_NOTIFY_ALL_NAME, MULTICONDITION_NOTIFY_ALL},
};

static const std::unordered_map<std::string, IntrinsicKind> runtimeIntrinsicMap = {
    {INVOKE_GC_NAME, INVOKE_GC},
    {SET_GC_THRESHOLD_NAME, SET_GC_THRESHOLD},
    {DUMP_CJ_HEAP_DATA_NAME, DUMP_CJ_HEAP_DATA},
    {GET_GC_COUNT_NAME, GET_GC_COUNT},
    {GET_GC_TIME_US_NAME, GET_GC_TIME_US},
    {GET_GC_FREED_SIZE_NAME, GET_GC_FREED_SIZE},
    {START_CJ_CPU_PROFILING_NAME, START_CJ_CPU_PROFILING},
    {STOP_CJ_CPU_PROFILING_NAME, STOP_CJ_CPU_PROFILING},
    {GET_MAX_HEAP_SIZE_NAME, GET_MAX_HEAP_SIZE},
    {GET_ALLOCATE_HEAP_SIZE_NAME, GET_ALLOCATE_HEAP_SIZE},
    {GET_REAL_HEAP_SIZE_NAME, GET_REAL_HEAP_SIZE},
    {GET_THREAD_NUMBER_NAME, GET_THREAD_NUMBER},
    {GET_BLOCKING_THREAD_NUMBER_NAME, GET_BLOCKING_THREAD_NUMBER},
    {GET_NATIVE_THREAD_NUMBER_NAME, GET_NATIVE_THREAD_NUMBER},
};
static const std::unordered_map<std::string, IntrinsicKind> mathIntrinsicMap = {
    {ABS_NAME, ABS},
    {FABS_NAME, FABS},
    {FLOOR_NAME, FLOOR},
    {CEIL_NAME, CEIL},
    {TRUC_NAME, TRUNC},
    {SIN_NAME, SIN},
    {COS_NAME, COS},
    {EXP_NAME, EXP},
    {EXP2_NAME, EXP2},
    {LOG_NAME, LOG},
    {LOG2_NAME, LOG2},
    {LOG10_NAME, LOG10},
    {SQRT_NAME, SQRT},
    {ROUND_NAME, ROUND},
    {POW_NAME, POW},
    {POWI_NAME, POWI},
};
} // namespace Cangjie::CHIR

#endif // Cangjie::CANGJIE_CHIR_INTRINSICKIND_H
