// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CGCOMMONDEF_H
#define CANGJIE_CGCOMMONDEF_H

/**
 * @file
 *
 * To declare the common definitions that are used in CodeGen
 */

#include <string>

#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/Intrinsics.h"

#include "Utils/Constants.h"
#include "cangjie/Basic/Linkage.h"
#include "cangjie/CHIR/IntrinsicKind.h"
#include "cangjie/CHIR/Type/Type.h"

namespace Cangjie {
namespace CodeGen {
enum TypeInfoFields : uint8_t {
    TYPEINFO_NAME = 0,            // mangledName : i8*.
    TYPEINFO_TYPE_KIND,           // typeKind : i8
    TYPEINFO_FLAG,                // flag filled by llvm : i8
    TYPEINFO_FIELDS_NUM,          // number of member variables : i16
    TYPEINFO_SIZE,                // size of the type (unit: byte) : i32
    TYPEINFO_GC_TIB,              // which field is reference : %BitMap*
    TYPEINFO_UUID,                // unique ID of a type in a process : i32
    TYPEINFO_ALIGN,               // alignment of the type : i8
    TYPEINFO_TYPE_ARGS_NUM,       // number of generic type args : i8
    TYPEINFO_INHERITED_CLASS_NUM, // number of inherited classes : i16
    TYPEINFO_OFFSETS,             // i32*
    TYPEINFO_SOURCE_GENERIC,      // i8*
    TYPEINFO_TYPE_ARGS,           // i8
    TYPEINFO_FIELDS,              // typeInfos list of member variables : i8*
    TYPEINFO_SUPER,               // typeinfo*
    TYPEINFO_EXTENSIONDEF_PTR,    // %ExtensionDef*
    TYPEINFO_MTABLE,              // i8*
    TYPEINFO_REFLECTION,          // i8*

    TYPE_INFO_FIELDS_NUM // the number of TypeInfo's fields
};

enum GenericTypeInfoFields : uint8_t {
    GENERIC_TYPEINFO_NAME = 0,        // srcCodeIdentifier : i8*
    GENERIC_TYPEINFO_TYPE_KIND,       // typeKind : i8
    GENERIC_TYPEINFO_UPPERBOUNDS_NUM, // the number of generic upper bounds : i32
    GENERIC_TYPEINFO_UPPERBOUNDS,     // the list of upperbounds : i8*
    GENERIC_TYPE_INFO_FIELDS_NUM      // the number of TypeInfo's fields
};

enum GenericCustomTypeInfoFields : uint8_t {
    GENERIC_CUSTOM_TYPEINFO_NAME = 0,       // i8*
    GENERIC_CUSTOM_TYPEINFO_TYPE_KIND,      // typeKind : i8
    GENERIC_CUSTOM_TYPEINFO_TYPE_ARGS_NUM,  // i8
    GENERIC_CUSTOM_TYPEINFO_TYPE_ARGS,      // i8*
    GENERIC_CUSTOM_TYPEINFO_SOURCE_GENERIC, // i8*
    GENERIC_CUSTOM_TYPE_INFO_FIELDS_NUM     // the number of TypeInfo's fields
};

enum TypeTemplateFields : uint8_t {
    TYPETEMPLATE_NAME = 0,            // mangledName : i8*.
    TYPETEMPLATE_TYPE_KIND,           // typeKind : i8
    TYPETEMPLATE_FLAG,                // i8
    TYPETEMPLATE_FIELDS_NUM,          // number of member variables : i16
    TYPETEMPLATE_TYPE_ARGS_NUM,       // i8
    TYPETEMPLATE_FIELDS_FNS,          // typeInfos list of member variables : i8*
    TYPETEMPLATE_SUPER_FN,            // i8*
    TYPETEMPLATE_FINALIZER,           // i8*
    TYPETEMPLATE_REFLECTION,          // i8*
    TYPETEMPLATE_EXTENSIONDEF_PTR,    // %ExtensionDef*
    TYPETEMPLATE_INHERITED_CLASS_NUM, // i16

    TYPE_TEMPLATE_FIELDS_NUM // the number of TypeTemplate's fields
};

enum ExtensionDefFields : uint8_t {
    TYPE_PARAM_COUNT = 0,         // i32
    IS_INTERFACE_TI,              // i8
    TARGET_TYPE,                  // i8*
    INTERFACE_FN_OR_INTERFACE_TI, // i8*
    WHERE_CONDITION_FN,           // i8*
    FUNC_TABLE,                   // i8*

    EXTENSION_DEF_FIELDS_NUM // the number of ExtensionDef's fields
};

enum ArrayBaseFields : uint8_t {
    ARRAY_BASE_ARRAY_LEN, // array length : i64

    ARRAY_BASE_FIELDS_NUM, // the number of ArrayBase's fields
};

enum BitMapFields : uint8_t {
    BITMAP_NUMS = 0, // nums of bit map : i32
    BITMAP_BITMAP,   // bitmap : [0 x i8]

    BITMAP_FIELDS_NUM // the number of BitMap's fields
};

struct IntrinsicFuncInfo {
    std::string funcName;
    std::vector<std::string> attributes;
};

struct GenericTypeAndPath {
public:
    GenericTypeAndPath(const CHIR::GenericType& gt, std::vector<size_t> path) : genericType(gt), path(path)
    {
    }
    const std::vector<size_t>& GetPath() const
    {
        return path;
    }
    const CHIR::GenericType* GetGenericType() const
    {
        return &genericType;
    }

private:
    const CHIR::GenericType& genericType;
    const std::vector<size_t> path;
};

const std::unordered_map<CHIR::BooleanType::TypeKind, std::pair<int64_t, int64_t>> G_SIGNED_INT_MAP = {
    {CHIR::BooleanType::TypeKind::TYPE_INT8, {std::numeric_limits<int8_t>::min(), std::numeric_limits<int8_t>::max()}},
    {CHIR::BooleanType::TypeKind::TYPE_INT16,
        {std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max()}},
    {CHIR::BooleanType::TypeKind::TYPE_INT32,
        {std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()}},
    {CHIR::BooleanType::TypeKind::TYPE_INT64,
        {std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()}},
    {CHIR::BooleanType::TypeKind::TYPE_INT_NATIVE,
        {std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()}}};

const std::unordered_map<CHIR::Type::TypeKind, uint64_t> G_UNSIGNED_INT_MAP = {
    {CHIR::Type::TypeKind::TYPE_UINT8, std::numeric_limits<uint8_t>::max()},
    {CHIR::Type::TypeKind::TYPE_UINT16, std::numeric_limits<uint16_t>::max()},
    {CHIR::Type::TypeKind::TYPE_UINT32, std::numeric_limits<uint32_t>::max()},
    {CHIR::Type::TypeKind::TYPE_UINT64, std::numeric_limits<uint64_t>::max()},
    {CHIR::Type::TypeKind::TYPE_UINT_NATIVE, std::numeric_limits<uint64_t>::max()}};

const std::set<std::string> REFLECT_INTRINSIC_FUNC = {"CJ_MCC_ApplyCJInstanceMethod", "CJ_MCC_ApplyCJStaticMethod",
    "CJ_MCC_ApplyCJGenericInstanceMethod", "CJ_MCC_ApplyCJGenericStaticMethod", "CJ_MCC_CheckMethodActualArgs",
    "CJ_MCC_GetOrCreateTypeInfoForReflect"};

const std::unordered_map<CHIR::IntrinsicKind, IntrinsicFuncInfo> INTRINSIC_KIND_TO_FUNCNAME_MAP = {
    // future
    {CHIR::IntrinsicKind::FUTURE_INIT, {"CJ_MCC_FutureInit", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::FUTURE_IS_COMPLETE, {"CJ_MCC_FutureIsComplete", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::FUTURE_WAIT, {"CJ_MCC_FutureWait", {}}},
    {CHIR::IntrinsicKind::FUTURE_NOTIFYALL, {"CJ_MCC_FutureNotifyAll", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::IS_THREAD_OBJECT_INITED, {"CJ_MCC_IsThreadObjectInited", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::GET_THREAD_OBJECT, {"CJ_MCC_GetCurrentCJThreadObject", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::SET_THREAD_OBJECT, {"CJ_MCC_SetCurrentCJThreadObject", {FAST_NATIVE_ATTR}}},
    // sync
    {CHIR::IntrinsicKind::MUTEX_INIT, {"CJ_MCC_MutexInit", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::CJ_MUTEX_LOCK, {"CJ_MCC_MutexLock", {}}},
    {CHIR::IntrinsicKind::MUTEX_TRY_LOCK, {"CJ_MCC_MutexTryLock", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::MUTEX_CHECK_STATUS, {"CJ_MCC_MutexCheckStatus", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::MUTEX_UNLOCK, {"CJ_MCC_MutexUnlock", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::WAITQUEUE_INIT, {"CJ_MCC_WaitQueueInit", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::MONITOR_INIT, {"CJ_MCC_WaitQueueForMonitorInit", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::MOITIOR_WAIT, {"CJ_MCC_MonitorWait", {}}},
    {CHIR::IntrinsicKind::MOITIOR_NOTIFY, {"CJ_MCC_MonitorNotify", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::MOITIOR_NOTIFY_ALL, {"CJ_MCC_MonitorNotifyAll", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::MULTICONDITION_WAIT, {"CJ_MCC_MultiConditionMonitorWait", {}}},
    {CHIR::IntrinsicKind::MULTICONDITION_NOTIFY, {"CJ_MCC_MultiConditionMonitorNotify", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::MULTICONDITION_NOTIFY_ALL, {"CJ_MCC_MultiConditionMonitorNotifyAll", {FAST_NATIVE_ATTR}}},
    {CHIR::IntrinsicKind::SLEEP, {"CJ_MRT_Sleep", {}}},
    // reflection
#define REFLECTION_KIND_TO_RUNTIME_FUNCTION(REFLECTION_KIND, CJ_FUNCTION, RUNTIME_FUNCTION)                            \
    {CHIR::IntrinsicKind::REFLECTION_KIND, {#RUNTIME_FUNCTION, {}}},
#include "cangjie/CHIR/LLVMReflectionIntrinsics.def"
#undef REFLECTION_KIND_TO_RUNTIME_FUNCTION
};

const std::unordered_map<CHIR::Type::TypeKind, CHIR::Type::TypeKind> INTEGER_CONVERT_MAP = {
    {CHIR::Type::TypeKind::TYPE_UINT8, CHIR::Type::TypeKind::TYPE_INT8},
    {CHIR::Type::TypeKind::TYPE_UINT16, CHIR::Type::TypeKind::TYPE_INT16},
    {CHIR::Type::TypeKind::TYPE_UINT32, CHIR::Type::TypeKind::TYPE_INT32},
    {CHIR::Type::TypeKind::TYPE_UINT64, CHIR::Type::TypeKind::TYPE_INT64},
    {CHIR::Type::TypeKind::TYPE_UINT_NATIVE, CHIR::Type::TypeKind::TYPE_INT_NATIVE}};

const std::unordered_map<CHIR::IntrinsicKind, llvm::Intrinsic::ID> INTRINSIC_KIND_TO_ID_MAP = {
    // runtime
    {CHIR::IntrinsicKind::GET_MAX_HEAP_SIZE, llvm::Intrinsic::cj_get_max_heap_size},
    {CHIR::IntrinsicKind::GET_ALLOCATE_HEAP_SIZE, llvm::Intrinsic::cj_get_allocated_heap_size},
    {CHIR::IntrinsicKind::GET_REAL_HEAP_SIZE, llvm::Intrinsic::cj_get_real_heap_size},
    {CHIR::IntrinsicKind::GET_THREAD_NUMBER, llvm::Intrinsic::cj_get_thread_number},
    {CHIR::IntrinsicKind::GET_BLOCKING_THREAD_NUMBER, llvm::Intrinsic::cj_get_blocking_thread_number},
    {CHIR::IntrinsicKind::GET_NATIVE_THREAD_NUMBER, llvm::Intrinsic::cj_get_native_thread_number},
    {CHIR::IntrinsicKind::DUMP_CJ_HEAP_DATA, llvm::Intrinsic::cj_dump_heap_data},
    {CHIR::IntrinsicKind::GET_GC_COUNT, llvm::Intrinsic::cj_get_gc_count},
    {CHIR::IntrinsicKind::GET_GC_TIME_US, llvm::Intrinsic::cj_get_gc_time_us},
    {CHIR::IntrinsicKind::GET_GC_FREED_SIZE, llvm::Intrinsic::cj_get_gc_freed_size},
    {CHIR::IntrinsicKind::START_CJ_CPU_PROFILING, llvm::Intrinsic::cj_start_cpu_profiling},
    {CHIR::IntrinsicKind::STOP_CJ_CPU_PROFILING, llvm::Intrinsic::cj_stop_cpu_profiling},
    {CHIR::IntrinsicKind::INVOKE_GC, llvm::Intrinsic::cj_invoke_gc},
    {CHIR::IntrinsicKind::SET_GC_THRESHOLD, llvm::Intrinsic::cj_set_gc_threshold},
    // math
    {CHIR::IntrinsicKind::ABS, llvm::Intrinsic::abs}, {CHIR::IntrinsicKind::POW, llvm::Intrinsic::pow},
    {CHIR::IntrinsicKind::POWI, llvm::Intrinsic::powi}, {CHIR::IntrinsicKind::FABS, llvm::Intrinsic::fabs},
    {CHIR::IntrinsicKind::FLOOR, llvm::Intrinsic::floor}, {CHIR::IntrinsicKind::CEIL, llvm::Intrinsic ::ceil},
    {CHIR::IntrinsicKind::TRUNC, llvm::Intrinsic::trunc}, {CHIR::IntrinsicKind::SIN, llvm::Intrinsic::sin},
    {CHIR::IntrinsicKind::COS, llvm::Intrinsic::cos}, {CHIR::IntrinsicKind::EXP, llvm::Intrinsic::exp},
    {CHIR::IntrinsicKind::EXP2, llvm::Intrinsic::exp2}, {CHIR::IntrinsicKind::LOG, llvm::Intrinsic::log},
    {CHIR::IntrinsicKind::LOG2, llvm::Intrinsic::log2}, {CHIR::IntrinsicKind::LOG10, llvm::Intrinsic::log10},
    {CHIR::IntrinsicKind::SQRT, llvm::Intrinsic::sqrt}, {CHIR::IntrinsicKind::ROUND, llvm::Intrinsic::round},
    // atomic
    {CHIR::IntrinsicKind::ATOMIC_STORE, llvm::Intrinsic::cj_atomic_store},
    {CHIR::IntrinsicKind::ATOMIC_COMPARE_AND_SWAP, llvm::Intrinsic::cj_atomic_compare_swap},
    {CHIR::IntrinsicKind::ATOMIC_LOAD, llvm::Intrinsic::cj_atomic_load},
    {CHIR::IntrinsicKind::ATOMIC_SWAP, llvm::Intrinsic::cj_atomic_swap},
    {CHIR::IntrinsicKind::BLACK_BOX, llvm::Intrinsic::cj_blackhole}};

bool IsCHIRWrapper(const std::string& chirFuncName);
llvm::GlobalValue::LinkageTypes CHIRLinkage2LLVMLinkage(Linkage chirLinkage);
void AddLinkageTypeMetadata(
    llvm::GlobalObject& globalObject, llvm::GlobalValue::LinkageTypes linkageType, bool markByMD);
llvm::GlobalValue::LinkageTypes GetLinkageTypeOfGlobalObject(const llvm::GlobalObject& globalObject);
} // namespace CodeGen
} // namespace Cangjie

#endif // CANGJIE_CGCOMMONDEF_H
