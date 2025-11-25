// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TOSTRING_UTILS_H
#define CANGJIE_CHIR_TOSTRING_UTILS_H

#include "cangjie/CHIR/IntrinsicKind.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/StringWrapper.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Value.h"
#include "cangjie/Utils/SafePointer.h"

#include <functional>
#include <iostream>
#include <set>
#include <vector>

namespace Cangjie::CHIR {
/**
 * Map of IntrinsicKind to string value.
 */
const std::unordered_map<CHIR::IntrinsicKind, std::string> INTRINSIC_KIND_TO_STRING_MAP{
    {CHIR::NOT_INTRINSIC, "notIntrinsic"}, {CHIR::NOT_IMPLEMENTED, "notImplemented"},

    // For hoisting, but we should later split arraybuilder
    // into allocation and initialisation
    {CHIR::ARRAY_INIT, "arrayInit"},

    // CORE
    {CHIR::SIZE_OF, CHIR::SIZE_OF_NAME}, {CHIR::ALIGN_OF, CHIR::ALIGN_OF_NAME},
    {CHIR::GET_TYPE_FOR_TYPE_PARAMETER, CHIR::GET_TYPE_FOR_TYPE_PARAMETER_NAME},
    {CHIR::IS_SUBTYPE_TYPES, CHIR::IS_SUBTYPE_TYPES_NAME},
    {CHIR::ARRAY_ACQUIRE_RAW_DATA, CHIR::ARRAY_ACQUIRE_RAW_DATA_NAME},
    {CHIR::ARRAY_RELEASE_RAW_DATA, CHIR::ARRAY_RELEASE_RAW_DATA_NAME},
    {CHIR::ARRAY_BUILT_IN_COPY_TO, CHIR::ARRAY_BUILT_IN_COPY_TO_NAME}, {CHIR::ARRAY_GET, CHIR::ARRAY_GET_NAME},
    {CHIR::ARRAY_SET, CHIR::ARRAY_SET_NAME}, {CHIR::ARRAY_GET_UNCHECKED, CHIR::ARRAY_GET_UNCHECKED_NAME},
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    {CHIR::ARRAY_GET_REF_UNCHECKED, CHIR::ARRAY_GET_REF_UNCHECKED_NAME},
#endif
    {CHIR::ARRAY_SET_UNCHECKED, CHIR::ARRAY_SET_UNCHECKED_NAME}, {CHIR::ARRAY_SIZE, CHIR::ARRAY_SIZE_NAME},
    {CHIR::ARRAY_CLONE, CHIR::ARRAY_CLONE_NAME}, {CHIR::ARRAY_SLICE_INIT, CHIR::ARRAY_SLICE_INIT_NAME},
    {CHIR::ARRAY_SLICE, CHIR::ARRAY_SLICE_NAME}, {CHIR::ARRAY_SLICE_RAWARRAY, CHIR::ARRAY_SLICE_RAWARRAY_NAME},
    {CHIR::ARRAY_SLICE_START, CHIR::ARRAY_SLICE_START_NAME}, {CHIR::ARRAY_SLICE_SIZE, CHIR::ARRAY_SLICE_SIZE_NAME},
    {CHIR::ARRAY_SLICE_GET_ELEMENT, CHIR::ARRAY_SLICE_GET_ELEMENT_NAME},
    {CHIR::ARRAY_SLICE_SET_ELEMENT, CHIR::ARRAY_SLICE_SET_ELEMENT_NAME},
    {CHIR::ARRAY_SLICE_GET_ELEMENT_UNCHECKED, CHIR::ARRAY_SLICE_GET_ELEMENT_UNCHECKED_NAME},
    {CHIR::ARRAY_SLICE_SET_ELEMENT_UNCHECKED, CHIR::ARRAY_SLICE_SET_ELEMENT_UNCHECKED_NAME},
    {CHIR::FILL_IN_STACK_TRACE, CHIR::FILL_IN_STACK_TRACE_NAME},
    {CHIR::DECODE_STACK_TRACE, CHIR::DECODE_STACK_TRACE_NAME},

    {CHIR::CHR, "chr"}, {CHIR::ORD, "ord"},

    {CHIR::CPOINTER_GET_POINTER_ADDRESS, CHIR::CPOINTER_GET_POINTER_ADDRESS_NAME},
    {CHIR::CPOINTER_INIT0, "pointerInit0"}, // CPointer constructor with no parameters
    {CHIR::CPOINTER_INIT1, "pointerInit1"}, // CPointer constructor with one parameter
    {CHIR::CPOINTER_READ, CHIR::CPOINTER_READ_NAME}, {CHIR::CPOINTER_WRITE, CHIR::CPOINTER_WRITE_NAME},
    {CHIR::CPOINTER_ADD, CHIR::CPOINTER_ADD_NAME},

    {CHIR::CSTRING_INIT, "_CString_init_"},
    {CHIR::CSTRING_CONVERT_CSTR_TO_PTR, CHIR::CSTRING_CONVERT_CSTR_TO_PTR_NAME},
    {CHIR::BIT_CAST, CHIR::BIT_CAST_NAME},

    {CHIR::INOUT_PARAM, "_inout_"},

    {CHIR::REGISTER_WATCHED_OBJECT, "registerWatchedObject"},

    {CHIR::OBJECT_REFEQ, CHIR::OBJECT_REFEQ_NAME},
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    {CHIR::RAW_ARRAY_REFEQ, CHIR::RAW_ARRAY_REFEQ_NAME},
#endif
    {CHIR::OBJECT_ZERO_VALUE, CHIR::OBJECT_ZERO_VALUE_NAME},

    {CHIR::INVOKE_GC, CHIR::INVOKE_GC_NAME}, {CHIR::SET_GC_THRESHOLD, CHIR::SET_GC_THRESHOLD_NAME},
    {CHIR::GET_MAX_HEAP_SIZE, CHIR::GET_MAX_HEAP_SIZE_NAME},
    {CHIR::GET_ALLOCATE_HEAP_SIZE, CHIR::GET_ALLOCATE_HEAP_SIZE_NAME},
    {CHIR::DUMP_CJ_HEAP_DATA, CHIR::DUMP_CJ_HEAP_DATA_NAME},
    {CHIR::GET_GC_COUNT, CHIR::GET_GC_COUNT_NAME},
    {CHIR::GET_GC_TIME_US, CHIR::GET_GC_TIME_US_NAME},
    {CHIR::GET_GC_FREED_SIZE, CHIR::GET_GC_FREED_SIZE_NAME},
    {CHIR::START_CJ_CPU_PROFILING, CHIR::START_CJ_CPU_PROFILING_NAME},
    {CHIR::STOP_CJ_CPU_PROFILING, CHIR::STOP_CJ_CPU_PROFILING_NAME},
    {CHIR::GET_REAL_HEAP_SIZE, CHIR::GET_REAL_HEAP_SIZE_NAME},
    {CHIR::GET_THREAD_NUMBER, CHIR::GET_THREAD_NUMBER_NAME},
    {CHIR::GET_BLOCKING_THREAD_NUMBER, CHIR::GET_BLOCKING_THREAD_NUMBER_NAME},
    {CHIR::GET_NATIVE_THREAD_NUMBER, CHIR::GET_NATIVE_THREAD_NUMBER_NAME},

    {CHIR::VARRAY_SET, CHIR::VARRAY_SET_NAME}, {CHIR::VARRAY_GET, CHIR::VARRAY_GET_NAME},

    // About Future
    {CHIR::FUTURE_INIT, CHIR::FUTURE_INIT_NAME},
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    {CHIR::FUTURE_IS_COMPLETE, CHIR::FUTURE_IS_COMPLETE_NAME}, {CHIR::FUTURE_WAIT, CHIR::FUTURE_WAIT_NAME},
    {CHIR::FUTURE_NOTIFYALL, CHIR::FUTURE_NOTIFYALL_NAME},
#endif
    {CHIR::IS_THREAD_OBJECT_INITED, CHIR::IS_THREAD_OBJECT_INITED_NAME},
    {CHIR::GET_THREAD_OBJECT, CHIR::GET_THREAD_OBJECT_NAME},
    {CHIR::SET_THREAD_OBJECT, CHIR::SET_THREAD_OBJECT_NAME},

    {CHIR::OVERFLOW_CHECKED_ADD, CHIR::OVERFLOW_CHECKED_ADD_NAME},
    {CHIR::OVERFLOW_CHECKED_SUB, CHIR::OVERFLOW_CHECKED_SUB_NAME},
    {CHIR::OVERFLOW_CHECKED_MUL, CHIR::OVERFLOW_CHECKED_MUL_NAME},
    {CHIR::OVERFLOW_CHECKED_DIV, CHIR::OVERFLOW_CHECKED_DIV_NAME},
    {CHIR::OVERFLOW_CHECKED_MOD, CHIR::OVERFLOW_CHECKED_MOD_NAME},
    {CHIR::OVERFLOW_CHECKED_POW, CHIR::OVERFLOW_CHECKED_POW_NAME},
    {CHIR::OVERFLOW_CHECKED_INC, CHIR::OVERFLOW_CHECKED_INC_NAME},
    {CHIR::OVERFLOW_CHECKED_DEC, CHIR::OVERFLOW_CHECKED_DEC_NAME},
    {CHIR::OVERFLOW_CHECKED_NEG, CHIR::OVERFLOW_CHECKED_NEG_NAME},
    {CHIR::OVERFLOW_THROWING_ADD, CHIR::OVERFLOW_THROWING_ADD_NAME},
    {CHIR::OVERFLOW_THROWING_SUB, CHIR::OVERFLOW_THROWING_SUB_NAME},
    {CHIR::OVERFLOW_THROWING_MUL, CHIR::OVERFLOW_THROWING_MUL_NAME},
    {CHIR::OVERFLOW_THROWING_DIV, CHIR::OVERFLOW_THROWING_DIV_NAME},
    {CHIR::OVERFLOW_THROWING_MOD, CHIR::OVERFLOW_THROWING_MOD_NAME},
    {CHIR::OVERFLOW_THROWING_POW, CHIR::OVERFLOW_THROWING_POW_NAME},
    {CHIR::OVERFLOW_THROWING_INC, CHIR::OVERFLOW_THROWING_INC_NAME},
    {CHIR::OVERFLOW_THROWING_DEC, CHIR::OVERFLOW_THROWING_DEC_NAME},
    {CHIR::OVERFLOW_THROWING_NEG, CHIR::OVERFLOW_THROWING_NEG_NAME},
    {CHIR::OVERFLOW_SATURATING_ADD, CHIR::OVERFLOW_SATURATING_ADD_NAME},
    {CHIR::OVERFLOW_SATURATING_SUB, CHIR::OVERFLOW_SATURATING_SUB_NAME},
    {CHIR::OVERFLOW_SATURATING_MUL, CHIR::OVERFLOW_SATURATING_MUL_NAME},
    {CHIR::OVERFLOW_SATURATING_DIV, CHIR::OVERFLOW_SATURATING_DIV_NAME},
    {CHIR::OVERFLOW_SATURATING_MOD, CHIR::OVERFLOW_SATURATING_MOD_NAME},
    {CHIR::OVERFLOW_SATURATING_POW, CHIR::OVERFLOW_SATURATING_POW_NAME},
    {CHIR::OVERFLOW_SATURATING_INC, CHIR::OVERFLOW_SATURATING_INC_NAME},
    {CHIR::OVERFLOW_SATURATING_DEC, CHIR::OVERFLOW_SATURATING_DEC_NAME},
    {CHIR::OVERFLOW_SATURATING_NEG, CHIR::OVERFLOW_SATURATING_NEG_NAME},
    {CHIR::OVERFLOW_WRAPPING_ADD, CHIR::OVERFLOW_WRAPPING_ADD_NAME},
    {CHIR::OVERFLOW_WRAPPING_SUB, CHIR::OVERFLOW_WRAPPING_SUB_NAME},
    {CHIR::OVERFLOW_WRAPPING_MUL, CHIR::OVERFLOW_WRAPPING_MUL_NAME},
    {CHIR::OVERFLOW_WRAPPING_DIV, CHIR::OVERFLOW_WRAPPING_DIV_NAME},
    {CHIR::OVERFLOW_WRAPPING_MOD, CHIR::OVERFLOW_WRAPPING_MOD_NAME},
    {CHIR::OVERFLOW_WRAPPING_POW, CHIR::OVERFLOW_WRAPPING_POW_NAME},
    {CHIR::OVERFLOW_WRAPPING_INC, CHIR::OVERFLOW_WRAPPING_INC_NAME},
    {CHIR::OVERFLOW_WRAPPING_DEC, CHIR::OVERFLOW_WRAPPING_DEC_NAME},
    {CHIR::OVERFLOW_WRAPPING_NEG, CHIR::OVERFLOW_WRAPPING_NEG_NAME},
    {CHIR::REFLECTION_INTRINSIC_START_FLAG, "reflectionIntrinsicStart"},
    // REFLECTION
#define REFLECTION_KIND_TO_RUNTIME_FUNCTION(REFLECTION_KIND, CJ_FUNCTION, RUNTIME_FUNCTION)                            \
    {CHIR::REFLECTION_KIND, #CJ_FUNCTION},
#include "cangjie/CHIR/LLVMReflectionIntrinsics.def"
#undef REFLECTION_KIND_TO_RUNTIME_FUNCTION
    {CHIR::REFLECTION_INTRINSIC_END_FLAG, "reflectionIntrinsicEnd"},

    {CHIR::SLEEP, CHIR::SLEEP_NAME},

    {CHIR::SOURCE_FILE, CHIR::SOURCE_FILE_NAME}, {CHIR::SOURCE_LINE, CHIR::SOURCE_LINE_NAME},

    {CHIR::IDENTITY_HASHCODE, CHIR::IDENTITY_HASHCODE_NAME},
    {CHIR::IDENTITY_HASHCODE_FOR_ARRAY, CHIR::IDENTITY_HASHCODE_FOR_ARRAY_NAME},

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    // SYNC
    {CHIR::ATOMIC_LOAD, CHIR::ATOMIC_LOAD_NAME}, {CHIR::ATOMIC_STORE, CHIR::ATOMIC_STORE_NAME},
    {CHIR::ATOMIC_SWAP, CHIR::ATOMIC_SWAP_NAME},
    {CHIR::ATOMIC_COMPARE_AND_SWAP, CHIR::ATOMIC_COMPARE_AND_SWAP_NAME},
    {CHIR::ATOMIC_FETCH_ADD, CHIR::ATOMIC_FETCH_ADD_NAME}, {CHIR::ATOMIC_FETCH_SUB, CHIR::ATOMIC_FETCH_SUB_NAME},
    {CHIR::ATOMIC_FETCH_AND, CHIR::ATOMIC_FETCH_AND_NAME}, {CHIR::ATOMIC_FETCH_OR, CHIR::ATOMIC_FETCH_OR_NAME},
    {CHIR::ATOMIC_FETCH_XOR, CHIR::ATOMIC_FETCH_XOR_NAME}, {CHIR::MUTEX_INIT, CHIR::MUTEX_INIT_NAME},
    {CHIR::CJ_MUTEX_LOCK, CHIR::MUTEX_LOCK_NAME}, {CHIR::MUTEX_TRY_LOCK, CHIR::MUTEX_TRY_LOCK_NAME},
    {CHIR::MUTEX_CHECK_STATUS, CHIR::MUTEX_CHECK_STATUS_NAME}, {CHIR::MUTEX_UNLOCK, CHIR::MUTEX_UNLOCK_NAME},
    {CHIR::WAITQUEUE_INIT, CHIR::WAITQUEUE_INIT_NAME}, {CHIR::MONITOR_INIT, CHIR::MONITOR_INIT_NAME},
    {CHIR::MOITIOR_WAIT, CHIR::MOITIOR_WAIT_NAME}, {CHIR::MOITIOR_NOTIFY, CHIR::MOITIOR_NOTIFY_NAME},
    {CHIR::MOITIOR_NOTIFY_ALL, CHIR::MOITIOR_NOTIFY_ALL_NAME},
    {CHIR::MULTICONDITION_WAIT, CHIR::MULTICONDITION_WAIT_NAME},
    {CHIR::MULTICONDITION_NOTIFY, CHIR::MULTICONDITION_NOTIFY_NAME},
    {CHIR::MULTICONDITION_NOTIFY_ALL, CHIR::MULTICONDITION_NOTIFY_ALL_NAME},
    {CHIR::VECTOR_COMPARE_32, CHIR::VECTOR_COMPARE_32_NAME},
    {CHIR::VECTOR_INDEX_BYTE_32, CHIR::VECTOR_INDEX_BYTE_32_NAME},
    {CHIR::CJ_CORE_CAN_USE_SIMD, CHIR::CJ_CORE_CAN_USE_SIMD_NAME},
#endif
    {CHIR::CJ_TLS_DYN_SET_SESSION_CALLBACK, CHIR::CJ_TLS_DYN_SET_SESSION_CALLBACK_NAME},
    {CHIR::CJ_TLS_DYN_SSL_INIT, CHIR::CJ_TLS_DYN_SSL_INIT_NAME},
    // CodeGen
    {CHIR::CG_UNSAFE_BEGIN, "cgUnsafeBegin"}, {CHIR::CG_UNSAFE_END, "cgUnsafeEnd"},
    // CHIR 2: Exception intrinsic
    {CHIR::BEGIN_CATCH, "beginCatch"},

    // CHIR 2: Math intrinsic
    {CHIR::ABS, "abs"}, {CHIR::FABS, "fabs"}, {CHIR::FLOOR, "floor"}, {CHIR::CEIL, "ceil"}, {CHIR::TRUNC, "trunc"},
    {CHIR::SIN, "sin"}, {CHIR::COS, "cos"}, {CHIR::EXP, "exp"}, {CHIR::EXP2, "exp2"}, {CHIR::LOG, "log"},
    {CHIR::LOG2, "log2"}, {CHIR::LOG10, "log10"}, {CHIR::SQRT, "sqrt"}, {CHIR::ROUND, "round"}, {CHIR::POW, "pow"},
    {CHIR::POWI, "powi"},
    // preinitialize intrinsic
    {CHIR::PREINITIALIZE, "preinitialize"},
    {CHIR::IS_THREAD_OBJECT_INITED, IS_THREAD_OBJECT_INITED_NAME},

    // Box cast intrinsic
    {CHIR::OBJECT_AS, "object.as"}, {CHIR::IS_NULL, "isNull"},
    {CHIR::BLACK_BOX, "blackBox"}
};

const std::unordered_map<CHIR::Package::AccessLevel, std::string> PACKAGE_ACCESS_LEVEL_TO_STRING_MAP = {
    {CHIR::Package::AccessLevel::INTERNAL, "internal"},
    {CHIR::Package::AccessLevel::PROTECTED, "protected"},
    {CHIR::Package::AccessLevel::PUBLIC, "public"},
};

class BlockGroup;
class Block;
class Func;
class Lambda;
class ImportedValue;
class ClassType;

/**
 * @brief Prints indentation to the specified stream.
 *
 * @param stream The output stream to print to.
 * @param indent The number of indentation levels.
 */
void PrintIndent(std::ostream& stream, size_t indent = 1);

/**
 * @brief Generates a string representation of generic constraints.
 *
 * @param genericTypeParams The list of generic type parameters.
 * @return A string representing the generic constraints.
 */
std::string GetGenericConstaintsStr(const std::vector<GenericType*>& genericTypeParams);

/**
 * @brief Generates a string representation of a block group.
 *
 * @param blockGroup The block group to represent.
 * @param indent The number of indentation levels.
 * @return A string representing the block group.
 */
std::string GetBlockGroupStr(const BlockGroup& blockGroup, size_t indent = 0);

/**
 * @brief Generates a string representation of a block.
 *
 * @param block The block to represent.
 * @param indent The number of indentation levels.
 * @return A string representing the block.
 */
std::string GetBlockStr(const Block& block, size_t indent = 0);

/**
 * @brief Generates a string representation of a function.
 *
 * @param func The function to represent.
 * @param indent The number of indentation levels.
 * @return A string representing the function.
 */
std::string GetFuncStr(const Func& func, size_t indent = 0);

/**
 * @brief Generates a string representation of a lambda expression.
 *
 * @param lambda The lambda expression to represent.
 * @param indent The number of indentation levels.
 * @return A string representing the lambda expression.
 */
std::string GetLambdaStr(const Lambda& lambda, size_t indent = 0);

/**
 * @brief Generates a string representation of an imported value.
 *
 * @param value The imported value to represent.
 * @return A string representing the imported value.
 */
std::string GetImportedValueStr(const ImportedValue& value);

/**
 * @brief Generates a string representation of an imported function.
 *
 * @param value The imported function to represent.
 * @return A string representing the imported function.
 */
std::string GetImportedFuncStr(const ImportedFunc& value);

/**
 * @brief Generates a string representation of exceptions.
 *
 * @param exceptions The list of exception classes.
 * @return A string representing the exceptions.
 */
std::string GetExceptionsStr(const std::vector<ClassType*>& exceptions);

/**
 * @brief Generates a string representation of generic type parameters.
 *
 * @param genericTypeParams The list of generic type parameters.
 * @return A string representing the generic type parameters.
 */
std::string GetGenericTypeParamsStr(const std::vector<GenericType*>& genericTypeParams);

/**
 * @brief Generates a string representation of generic type parameter constraints.
 *
 * @param genericTypeParams The list of generic type parameters.
 * @return A string representing the generic type parameter constraints.
 */
std::string GetGenericTypeParamsConstraintsStr(const std::vector<GenericType*>& genericTypeParams);

/**
 * @brief Adds a comma or not to the string stream based on a condition.
 *
 * @param ss The string stream to modify.
 */
void AddCommaOrNot(std::stringstream& ss);

/**
 * @brief Converts a package access level to a string.
 *
 * @param level The package access level to convert.
 * @return A string representing the package access level.
 */
std::string PackageAccessLevelToString(const Package::AccessLevel& level);

/**
 * @brief Converts a custom type kind to a string.
 *
 * @param def The custom type definition to convert.
 * @return A string representing the custom type kind.
 */
std::string CustomTypeKindToString(const CustomTypeDef& def);

/**
 * @brief Converts a boolean value to a string.
 *
 * @param flag The boolean value to convert.
 * @return A string representing the boolean value.
 */
std::string BoolToString(bool flag);

StringWrapper ThisTypeToString(const Type* thisType);
std::string InstTypeArgsToString(const std::vector<Type*>& instTypeArgs);
std::string ExprOperandsToString(const std::vector<Value*>& args);
std::string ExprWithExceptionOperandsToString(const std::vector<Value*>& args, const std::vector<Block*>& successors);
std::string ParamTypesToString(const FuncType& funcType);
std::string OverflowToString(Cangjie::OverflowStrategy ofStrategy);
} // namespace Cangjie::CHIR

#endif // CANGJIE_CHIR_TOSTRING_UTILS_H
