// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * TThis file declares the constants used in CodeGen.
 */

#ifndef CANGJIE_CODEGENCONSTANTS_H
#define CANGJIE_CODEGENCONSTANTS_H

#include <set>
#include <string>

namespace Cangjie {
namespace CodeGen {
const size_t UI64_WIDTH = 64;
const size_t I64_WIDTH = 64;
const unsigned long INHERITED_CLASS_NUM_FE_FLAG = 1UL << 15;
const std::string UNIT_TYPE_STR = "Unit.Type";
const std::string UNIT_VAL_STR = "Unit.Val";
const std::string ARRAY_LAYOUT_PREFIX = "ArrayLayout.";
const std::string INCREMENTAL_CFUNC_ATTR = "incremental";
const std::string INTERNAL_CFUNC_ATTR = "internal";
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
const std::string FAST_NATIVE_ATTR = "gc-leaf-function";
#endif
const std::string VTABLE_LOOKUP = "VTABLE_LOOKUP"; // Indicates that it is the instruction of VTable lookup.
const std::string GC_TYPE_META_NAME = "RelatedType";
const std::string GC_GLOBAL_VAR_TYPE = "GlobalVarType";
const std::string GC_KLASS_ATTR = "CFileKlass";
const std::string GC_TI_EXT_ATTR = "CFileTIExt";
const std::string NOT_MODIFIABLE_CLASS_ATTR = "NotModifiableClass";
const std::string HAS_EXT_PART = "HasExtPart";
const std::string GC_MTABLE_ATTR = "CFileMTable";
const std::string GC_FINALIZER_ATTR = "HasFinalizer";
const std::string STRUCT_MUT_FUNC_ATTR = "record_mut";
const std::string THIS_PARAM_HAS_BP = "thisParamHasBP";
const std::string STRUCT_TYPE_PREFIX = "record.";
const std::string CJ2C_ATTR = "cj2c";
const std::string C2CJ_ATTR = "c2cj";
const std::string CJSTUB_ATTR = "cjstub";
const std::string CFUNC_ATTR = "cfunc";
const std::string PREFIX_OF_BUILT_IN_SYMS = "CJ_";
const std::string PREFIX_OF_RUNTIME_SYMS = PREFIX_OF_BUILT_IN_SYMS + "MRT_";
const std::string PREFIX_OF_BACKEND_SYMS = PREFIX_OF_BUILT_IN_SYMS + "MCC_";
const std::string FOR_KEEPING_SOME_TYPES_FUNC_NAME = "0_for_keeping_some_types";
const std::string USER_MAIN_MANGLED_NAME = "user.main";
const std::string CJ_ENTRY_FUNC_NAME = "cj_entry$";
const std::string CONST_TUPLE_PREFIX = "$const_tuple.";
const std::string CONST_ARRAY_PREFIX = "$const_array.";
const std::string FUNC_USED_BY_CLOSURE = "UsedByClosure";
const std::string ENUM_TYPE_PREFIX = "enum.";
const std::string CJSTRING_LITERAL_PREFIX = "$const_cjstring.";
const std::string CJSTRING_DATA_PREFIX = "$const_cjstring_data.";
const std::string CJSTRING_LITERAL_ATTR = "cjstring_literal";
const std::string CJSTRING_DATA_ATTR = "cjstring_data";
const std::string CJGLOBAL_VALUE_ATTR = "CJGlobalValue";
const std::string CJTYPE_NAME_ATTR = "CJTypeName";
const std::string CJTI_OFFSETS_ATTR = "CJTIOffsets";
const std::string CJTI_TYPE_ARGS_ATTR = "CJTITypeArgs";
const std::string CJTI_FIELDS_ATTR = "CJTIFields";
const std::string CJTI_UPPER_BOUNDS_ATTR = "CJTIUpperBounds";
const std::string CJTT_FIELDS_FNS_ATTR = "CJTTFieldsFns";
const std::string CJED_FUNC_TABLE_ATTR = "CJFuncTable";
const std::string BASEPTR_SUFFIX = "$BP";
const std::string METADATA_TYPES = "types";
const std::string METADATA_PRIMITIVE_TYPES = "primitive_tis";
const std::string METADATA_PRIMITIVE_TYPETEMPLATES = "primitive_tts";
const std::string METADATA_TYPETEMPLATES = "type_templates";
const std::string METADATA_PKG = "pkg_info";
const std::string METADATA_FUNCTIONS = "functions";
const std::string ATTR_IMMUTABLE = "immutable"; // for let-fields
const std::string METADATA_ATTR_OPEN = "open";  // for mut-functions
const std::string METADATA_GLOBAL_VAR = "global_variables";
const std::string PREFIX_FOR_BB_NAME = "bb";
const std::string GENERIC_DECL_IN_IMPORTED_PKG_ATTR = "generic_decl_in_imported_pkg";
const std::string GENERIC_DECL_IN_CURRENT_PKG_ATTR = "generic_decl_in_current_pkg";
const std::string TYPE_TEMPLATE_ATTR = "cj_tt";
const std::string GENERIC_TYPEINFO_ATTR = "cj_generic_ti";
const std::string POSTFIX_WITHOUT_TI = "$withoutTI";
const std::string GENERIC_PREFIX = "$G_";
const std::string HAS_WITH_TI_WRAPPER_ATTR = "hasWithTIWrapper";
const std::string PKG_GV_INIT_PREFIX = "_CGP";
const std::string FILE_GV_INIT_PREFIX = "_CGF";
} // namespace CodeGen
} // namespace Cangjie
#endif // CANGJIE_CODEGENCONSTANTS_H
