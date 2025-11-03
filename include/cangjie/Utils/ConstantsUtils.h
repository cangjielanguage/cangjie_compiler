// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares some utility constants.
 */

#ifndef CANGJIE_CONSTANTSUTILS_H
#define CANGJIE_CONSTANTSUTILS_H

#include <map>
#include <stdint.h>
#include <string>
#include <string_view>
#include <cstdint>

namespace Cangjie {
enum class OverflowStrategy : uint8_t { NA, CHECKED, WRAPPING, THROWING, SATURATING, OVERFLOW_STRATEGY_END };

inline const std::string MAIN_INVOKE = "$mainInvoke";
inline const std::string ENV_NAME = "$env";
inline const std::string TEST_ENTRY_NAME = "$test.entry";
inline const std::string BOX_DECL_PREFIX = "$BOX_";
inline const std::string STATIC_INIT_VAR = "$init";
inline const std::string STATIC_INIT_FUNC = "static.init";
constexpr std::string_view TO_ANY{"$toAny"};

inline const std::string INOUT_GHOST_SYSCALL = "_inout_";

inline const std::string ITER_COMPILER = "iter-compiler";
inline const std::string V_COMPILER = "v-compiler";
inline const std::string INVALID_IDENTIFIER = "<invalid identifier>";
inline const std::string RESOURCE_NAME = "v-freshExc";

inline const std::string CANGJIE_HOME = "CANGJIE_HOME";
inline const std::string SOURCEFILE = "sourceFile";
inline const std::string SOURCELINE = "sourceLine";
inline const std::string SANCOV_VARIABLE_FLAG = "$sancov$";

// File extension.
inline const std::string SERIALIZED_FILE_EXTENSION = ".cjo";
inline const std::string CJ_D_FILE_EXTENSION = ".cj.d";
inline const std::string FULL_BCHIR_SERIALIZED_FILE_EXTENSION = ".full.bchir";
inline const std::string BCHIR_SERIALIZED_FILE_EXTENSION = ".bchir";
inline const std::string CACHED_AST_EXTENSION = ".cachedast";
inline const std::string CACHED_IMPORTED_CJO_EXTENSION = ".iast";
inline const std::string CACHED_CHIR_OPT_EXTENSION = ".cachedchiropt";
inline const std::string CACHED_GLOBAL_DECL_DEP_EXTENSION = ".cachedgdecldep";
inline const std::string CACHED_MODIFIED_AST_EXTENSION = ".cachedModified";
inline const std::string CACHED_LOG_EXTENSION = ".log";
inline const std::string CHIR_SERIALIZATION_FILE_EXTENSION = ".chir";
inline const std::string CHIR_READABLE_FILE_EXTENSION = ".chirtxt";

// Built-in type name.
inline const std::string CLASS_EXCEPTION = "Exception";
inline const std::string CLASS_ERROR = "Error";
inline const std::string CLASS_COMMAND = "Command";
inline const std::string CLASS_HANDLER_FRAME = "HandlerFrame";
inline const std::string CLASS_IMMEDIATE_FRAME = "ImmediateFrame";
inline const std::string CLASS_DOUBLE_RESUME_EXCEPTION = "DoubleResumeException";
inline const std::string CLASS_FRAME_EXCEPTION_WRAPPER = "ImmediateFrameExceptionWrapper";
inline const std::string CLASS_FRAME_ERROR_WRAPPER = "ImmediateFrameErrorWrapper";
inline const std::string CLASS_EARLY_RETURN = "ImmediateEarlyReturn";
inline const std::string OPTION_NAME = "Option";
inline const std::string OPTION_VALUE_CTOR = "Some";
inline const std::string OPTION_NONE_CTOR = "None";
inline const std::string ANY_NAME = "Any";
inline const std::string OBJECT_NAME = "Object";
inline const std::string JOBJECT_NAME = "JObject";
inline const std::string OCOBJECT_NAME = "NSObject";
inline const std::string CTYPE_NAME = "CType";
inline const std::string RAW_ARRAY_NAME = "RawArray";
constexpr std::string_view CPOINTER_NAME = "CPointer";
constexpr std::string_view CSTRING_NAME = "CString";
constexpr std::string_view CFUNC_NAME = "CFunc";
inline const std::string VARRAY_NAME = "VArray";
inline const std::string TOSTRING_NAME = "ToString";
inline const std::string BOX_NAME = "Box";

const std::string JTYPE_NAME = "JType";
const std::string JARRAY_NAME = "JArray";

// Macro with context.
inline const int MACRO_DEF_NUM = 2;
inline const std::string MC_EXCEPTION = "MacroContextException";
inline const int MACRO_COMMON_ARGS = 1;
inline const int MACRO_ATTR_ARGS = 2;
inline const std::string MACRO_OBJECT_NAME = "MACRO_OBJECT";
constexpr std::string_view IF_AVAILABLE = "IfAvailable";

// Standard library package name
// Please do not change the const char[] type to std::string type because the initialization order of
// std::string and std::map types across translation units is undefined,especially on the Windows platform.
inline const std::string DEFAULT_PACKAGE_NAME = "default";
inline constexpr const char CORE_PACKAGE_NAME[] = "std.core";
inline constexpr const char SYNC_PACKAGE_NAME[] = "std.sync";
inline constexpr const char MATH_PACKAGE_NAME[] = "std.math";
inline constexpr const char OVERFLOW_PACKAGE_NAME[] = "std.overflow";
inline constexpr const char RUNTIME_PACKAGE_NAME[] = "std.runtime";
inline constexpr const char UNICODE_PACKAGE_NAME[] = "std.unicode";
inline constexpr const char UNITTEST_MOCK_PACKAGE_NAME[] = "std.unittest.mock";
inline constexpr const char UNITTEST_MOCK_INTERNAL_PACKAGE_NAME[] = "std.unittest.mock.internal";
inline constexpr const char AST_PACKAGE_NAME[] = "std.ast";
inline constexpr const char INTEROP_PACKAGE_NAME[] = "std.interop";
inline constexpr const char NET_PACKAGE_NAME[] = "std.net";
inline constexpr const char REFLECT_PACKAGE_NAME[] = "std.reflect";
inline constexpr const char REF_PACKAGE_NAME[] = "std.ref";
inline constexpr const char EFFECT_INTERNALS_PACKAGE_NAME[] = "stdx.effect";
inline constexpr const char EFFECT_PACKAGE_NAME[] = "stdx.effect";

// Standard library class name
inline const std::string STD_LIB_ARRAY = "Array";
inline const std::string STD_LIB_FUTURE = "Future";
inline const std::string STD_LIB_MONITOR = "Monitor";
inline const std::string STD_LIB_MUTEX = "Mutex";
inline const std::string STD_LIB_OPTION = "Option";
inline const std::string STD_LIB_STRING = "String";
inline const std::string STD_LIB_WAIT_QUEUE = "WaitQueue";
inline const std::string STD_LIB_WEAK_REF = "WeakRef";
inline const std::string INTEROP_LIB_EXPORTED_REF = "ExportedRef";
inline const std::string INTEROP_LIB_FOREIGN_PROXY = "ForeignProxy";

// Global init function name for CHIR, CodeGen.
inline const std::string ANNOTATION_VAR_POSTFIX{"@Annotation@"};

// Closure Conversion
inline const std::string CC_DEF_PREFIX = "$C";
inline const std::string GENERIC_VIRTUAL_FUNC = "$GenericVirtualFunc";
inline const std::string INST_VIRTUAL_FUNC = "$InstVirtualFunc";

// CFFI
inline const std::string CFFI_FUNC_SUFFIX = "$real";

// Identifier for generated flag to check if instance of a class is initialized.
inline const std::string HAS_INITED_IDENT = "$hasInited";

namespace Interop::Java {
inline const std::string INTEROP_JOBJECT_NAME = "JObject";
inline const std::string INTEROP_JSTRING_NAME = "JString";
inline const std::string INTEROP_JARRAY_NAME = "JArray";
inline const std::string INTEROP_JAVA_LANG_PACKAGE = "java.lang";
inline const std::string JAVA_REF_FIELD_NAME = "$javaref";
constexpr auto JAVA_REF_GETTER_FUNC_NAME = "$getJavaRef";
constexpr auto INTEROPLIB_CFFI_JAVA_ENTITY = "Java_CFFI_JavaEntity";
constexpr auto JAVA_OBJECT_CONTROLLER = "JavaObjectController";
constexpr auto JAVA_OBJECT_CONTROLLER_NAME = "controller";
constexpr auto JAVA_OVERRIDE_MASK_NAME = "overrideMask";
constexpr auto ENV = "env";
} // namespace Cangjie::Interop::Java

namespace Interop::ObjC {
inline const std::string OBJ_C_ID_IDENT = "ObjCId";
inline const std::string OBJ_C_POINTER_IDENT = "ObjCPointer";
inline const std::string OBJ_C_FUNC_IDENT = "ObjCFunc";
inline const std::string OBJ_C_BLOCK_IDENT = "ObjCBlock";
inline const std::string INTEROPLIB_OBJ_C_PACKAGE_IDENT = "interoplib.objc";
inline const std::string OBJ_C_LANG_PACKAGE_IDENT = "objc.lang";
inline const std::string NATIVE_HANDLE_IDENT = "$obj";
inline const std::string REGISTRY_ID_IDENT = "$registryId";
inline const std::string SYNTHETIC_CLASS_SUFFIX = "$impl";
inline const std::string NATIVE_HANDLE_GETTER_IDENT = "$getObj";
} // namespace Interop::ObjC

// Headless instrinsics
inline const std::string GET_TYPE_FOR_TYPE_PARAMETER_FUNC_NAME = "getTypeForTypeParameter";
inline const std::string IS_SUBTYPE_TYPES_FUNC_NAME = "isSubtypeTypes";

constexpr std::string_view ORG_NAME_SEPARATOR{"@"};
} // namespace Cangjie
#endif // CANGJIE_CONSTANTSUTILS_H
