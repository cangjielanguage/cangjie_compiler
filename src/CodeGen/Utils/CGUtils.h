// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CGUTILS_H
#define CANGJIE_CGUTILS_H

#include <string>
#include <vector>

#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

#include "Base/CGTypes/CGFunctionType.h"
#include "Utils/CGCommonDef.h"
#include "cangjie/CHIR/CHIRContext.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/Mangle/CHIRTypeManglingUtils.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "cangjie/Utils/Utils.h"

namespace Cangjie {
namespace CHIR {
class Type;
class IntType;
} // namespace CHIR

namespace CodeGen {
class IRBuilder2;
class CGValue;

std::vector<llvm::Metadata*> UnwindGenericRelateType(llvm::LLVMContext& llvmCtx, const CHIR::Type& ty);

inline std::string GenNameForBB(const std::string& name, [[maybe_unused]] uint64_t extraInfo = 0)
{
    return name;
}

template <typename T> inline bool In(const T element, const std::unordered_set<T>& set)
{
    return set.find(element) != set.cend();
}

template <typename T> inline bool NotIn(T element, const std::vector<T>& container)
{
    auto it = std::find(container.begin(), container.end(), element);
    return it == container.end();
}

template <typename T, std::size_t... Indices>
auto Vec2TupleHelper(const std::vector<T>& v, std::index_sequence<Indices...>)
{
    return std::make_tuple(v[Indices]...);
}

template <std::size_t N, typename T> auto Vec2Tuple(const std::vector<T>& v)
{
    CJC_ASSERT(v.size() >= N);
    return Vec2TupleHelper(v, std::make_index_sequence<N>());
}

inline void SetStructTypeBody(llvm::StructType* structType, const std::vector<llvm::Type*>& bodyVec)
{
    CJC_NULLPTR_CHECK(structType);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    structType->setBodyForCJ(bodyVec);
#endif
}

inline std::string GetClassObjLayoutName(const std::string& className)
{
    return "ObjLayout." + className;
}

inline void SetGCCangjie(llvm::Function* func)
{
    func->setGC("cangjie");
}

inline std::string MangleName(const std::string& str)
{
    return std::to_string(str.length()) + str;
}

inline std::string GetMangledNameOfCompilerAddedClass(const std::string& className)
{
    // As for the class added by FE, it’s not in any package, so
    // in it’s mangled name, the <package name> would be empty
    // (i.e., length is zero).
    return MANGLE_NESTED_PREFIX + MangleName(className) + "E";
}

const std::unordered_map<CHIR::ExprKind, std::string> OPERATOR_KIND_TO_OP_MAP = {
    {CHIR::ExprKind::ADD, "add"},
    {CHIR::ExprKind::SUB, "sub"},
    {CHIR::ExprKind::MUL, "mul"},
    {CHIR::ExprKind::DIV, "div"},
    {CHIR::ExprKind::MOD, "mod"},
    {CHIR::ExprKind::EXP, "pow"},
    {CHIR::ExprKind::NEG, "neg"},
};
inline std::string GetCodeGenTypeName(llvm::Type* type)
{
    if (type->isStructTy()) {
        return llvm::cast<llvm::StructType>(type)->isLiteral() ? "" : type->getStructName().str();
    }
    return ""; // Other types do not have llvm type name.
}

std::string GetTypeName(const CGType* type);

bool SaveToBitcodeFile(const llvm::Module& module, const std::string& bcFilePath);

inline llvm::Type* GetPointerToWithSpecificAddrSpace(const CGType* srcType, unsigned dstAddrSpace)
{
    CJC_ASSERT(srcType->IsPointerType() && "a pointer type is expected");
    return srcType->GetPointerElementType()->GetLLVMType()->getPointerTo(dstAddrSpace);
}

inline llvm::Type* GetPointerElementType(const llvm::Type* type)
{
    return type->getNonOpaquePointerElementType();
}

/**
 * Check if @p type is a pointer type which points to a struct.
 */
inline bool IsStructPtrType(llvm::Type* type)
{
    return type->isPointerTy() && GetPointerElementType(type)->isStructTy();
}

inline bool HasNoUse(const llvm::Value& value)
{
    return !value.hasNUsesOrMore(1);
}

inline const CHIR::Type* DeRef(const CHIR::Type& chirTy)
{
    return chirTy.StripAllRefs();
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
bool IsReferenceType(const CHIR::Type& ty, CGModule& cgMod);
#endif

inline bool IsGenericRef(const CHIR::Type& chirTy)
{
    return chirTy.IsRef() && !chirTy.GetTypeArgs().empty() && chirTy.GetTypeArgs()[0]->IsGeneric();
}

inline bool IsTrivialEnum(const CHIR::Type& chirTy)
{
    return chirTy.IsEnum() && StaticCast<const CHIR::EnumType&>(chirTy).GetEnumDef()->IsAllCtorsTrivial();
}

inline bool IsStructRef(const CHIR::Type& chirTy)
{
    return chirTy.IsRef() && !chirTy.GetTypeArgs().empty() && chirTy.GetTypeArgs()[0]->IsStruct();
}

inline bool IsStaticStruct(const CHIR::Type& chirTy)
{
    return chirTy.IsStruct() && CGType::GetCGGenericKind(chirTy) == CGType::CGGenericKind::STATIC_GI;
}

inline bool IsDynamicStruct(const CHIR::Type& chirTy)
{
    return chirTy.IsStruct() && CGType::GetCGGenericKind(chirTy) == CGType::CGGenericKind::DYNAMIC_GI;
}

inline bool IsDynamicStructRef(const CHIR::Type& chirTy)
{
    if (!chirTy.IsRef() || chirTy.GetTypeArgs().empty()) {
        return false;
    }
    auto baseType = chirTy.GetTypeArgs()[0];
    return baseType->IsStruct() && CGType::GetCGGenericKind(*baseType) == CGType::CGGenericKind::DYNAMIC_GI;
}

inline bool IsClassRef(const CHIR::Type& chirTy)
{
    return chirTy.IsRef() && !chirTy.GetTypeArgs().empty() && chirTy.GetTypeArgs()[0]->IsClass();
}

inline bool IsStaticClass(const CHIR::Type& chirTy)
{
    return chirTy.IsClass() && CGType::GetCGGenericKind(chirTy) == CGType::CGGenericKind::STATIC_GI;
}

inline bool IsDynamicClass(const CHIR::Type& chirTy)
{
    return chirTy.IsClass() && CGType::GetCGGenericKind(chirTy) == CGType::CGGenericKind::DYNAMIC_GI;
}

inline bool IsDynamicClassRef(const CHIR::Type& chirTy)
{
    if (!chirTy.IsRef() || chirTy.GetTypeArgs().empty()) {
        return false;
    }
    auto baseType = chirTy.GetTypeArgs()[0];
    return baseType->IsClass() && CGType::GetCGGenericKind(*baseType) == CGType::CGGenericKind::DYNAMIC_GI;
}

inline bool IsVArrayRef(const CHIR::Type& chirTy)
{
    return chirTy.IsRef() && !chirTy.GetTypeArgs().empty() && chirTy.GetTypeArgs()[0]->IsVArray();
}

inline bool IsUnitOrNothing(const CHIR::Type& chirTy)
{
    return chirTy.IsUnit() || chirTy.IsNothing();
}

bool IsThisArgOfStructMethod(const CHIR::Value& chirValue);

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
inline bool IsAtomicIntrinsic(const CHIR::IntrinsicKind& intrinsicKind)
{
    return intrinsicKind >= CHIR::IntrinsicKind::ATOMIC_LOAD &&
        intrinsicKind <= CHIR::IntrinsicKind::ATOMIC_FETCH_XOR;
}

int64_t GetIntMaxOrMin(IRBuilder2& irBuilder, const CHIR::IntType& ty, bool isMax);
uint64_t GetUIntMax(IRBuilder2& irBuilder, const CHIR::IntType& ty);

inline bool IsPassedByReference(llvm::Type* type)
{
    return (type->isStructTy() || type->isArrayTy());
}
#endif

inline void NameFunctionParam(llvm::Function* function, std::vector<std::string> paramName)
{
    CJC_ASSERT(function->arg_size() == paramName.size());
    for (size_t idx = 0; idx < paramName.size(); idx++) {
        auto arg = function->args().begin() + idx;
        arg->setName(paramName[idx]);
    }
}

/**
 * Check if @p type is a pointer type which points to a Function.
 */
inline bool IsFuncPtrType(llvm::Type* type)
{
    return type->isPointerTy() && GetPointerElementType(type)->isFunctionTy();
}

/**
 * A literal struct pointer type looks like: {}*.
 * For now, this type means a cFunc pointer type with a incomplete parameter or return type.
 */
inline bool IsLitStructPtrType(llvm::Type* type)
{
    if (!type || !type->isPointerTy()) {
        return false;
    }
    auto structType = llvm::dyn_cast<llvm::StructType>(GetPointerElementType(type));
    return structType && structType->isLiteral();
}

class ArrayCopyToInfo {
public:
    ArrayCopyToInfo(llvm::Value* srcBP, llvm::Value* dstBP, llvm::Value* srcArrPtr, llvm::Value* dstArrPtr,
        llvm::Value* dataSize, llvm::Value* srcIndex, llvm::Value* dstIndex, const CGType* elemType,
        llvm::Value* elemCnt)
        : srcBP(srcBP),
          dstBP(dstBP),
          srcArrPtr(srcArrPtr),
          dstArrPtr(dstArrPtr),
          dataSize(dataSize),
          srcIndex(srcIndex),
          dstIndex(dstIndex),
          elemType(elemType),
          elemCnt(elemCnt)
    {
    }
    llvm::Value* srcBP{nullptr};     // address of the source array (i8Ptr)
    llvm::Value* dstBP{nullptr};     // address of the destination array (i8Ptr)
    llvm::Value* srcArrPtr{nullptr}; // address of the source array (arrayTypePtr)
    llvm::Value* dstArrPtr{nullptr}; // address of the destination array (arrayTypePtr)
    llvm::Value* dataSize{nullptr};  // size : ** deprecated **
    llvm::Value* srcIndex{nullptr};  // start index of source array
    llvm::Value* dstIndex{nullptr};  // start index of destination array
    const CGType* elemType{nullptr}; // array element type
    llvm::Value* elemCnt{nullptr};   // the number of elements to be copied
};
/*
 * Return a pair consisting of a bool value set to true if the input parameter is a constant, and
 * a string representing the result of the serialization.
 */
std::string ConstantValueToString(const CHIR::Constant& expr);
std::tuple<bool, std::string> IsConstantArray(const CHIR::RawArrayLiteralInit& arrayLiteralInit);
std::tuple<bool, std::string> IsConstantVArray(const CHIR::VArray& varray);
std::tuple<bool, std::string> IsConstantTuple(const CHIR::Tuple& tuple);
bool IsAllConstantValue(const std::vector<CGValue*>& args);

inline bool IsFPType(llvm::Type* type)
{
    return type->isHalfTy() || type->isFloatTy() || type->isDoubleTy() || type->isFP128Ty() || type->isPPC_FP128Ty() ||
        type->isX86_FP80Ty();
}

inline bool IsObject(const CHIR::ClassDef& classDef)
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    return IsCoreObject(classDef);
#endif
}

inline bool IsNonPublicCFunc(const CHIR::FuncType& funcTy, const CHIR::Value& func)
{
    // When incremental compile is enabled, foreign function is regarded as
    // imported variable, it should not be wrapped.
    if (func.TestAttr(CHIR::Attribute::FOREIGN)) {
        return false;
    }
    return funcTy.IsCFunc() && func.TestAttr(CHIR::Attribute::PRIVATE);
}

inline bool IsCommonStruct(const CHIR::Type& ty)
{
    return Utils::AllOf(ty.IsStruct(), !ty.IsCPointer(), !ty.IsCString());
}

inline bool IsCFunc(const CHIR::Type& chirTy)
{
    if (!chirTy.IsFunc()) {
        return false;
    }
    return StaticCast<const CHIR::FuncType&>(chirTy).IsCFunc();
}

inline bool IsCStruct(const CHIR::Type& chirTy)
{
    if (!chirTy.IsStruct()) {
        return false;
    }
    auto def = StaticCast<const CHIR::StructType&>(chirTy).GetStructDef();
    CJC_NULLPTR_CHECK(def);
    return def->IsCStruct();
}

class CGModule;
uint64_t GetTypeSize(const CGModule& cgMod, llvm::Type& ty);
uint64_t GetTypeSize(const CGModule& cgMod, const CHIR::Type& ty);
uint64_t GetTypeAlignment(const CGModule& cgMod, const CHIR::Type& ty);
uint64_t GetTypeAlignment(const CGModule& cgMod, llvm::Type& ty);
bool IsZeroSizedTypeInC(const CGModule& cgMod, const CHIR::Type& chirTy);

inline size_t AlignedUpTo(size_t offset, size_t align)
{
    return ((offset + align - 1) / align) * align;
}

inline size_t AlignedUpTo(const CGModule& cgMod, size_t offset, llvm::Type& type)
{
    return AlignedUpTo(offset, GetTypeAlignment(cgMod, type));
}

bool IsTypeContainsRef(llvm::Type* type);

/**
 * Collect link name used in metadata by bfs.
 */
void CollectLinkNameUsedInMeta(const llvm::NamedMDNode* n, std::unordered_set<std::string>& ctxSet);

bool IsGetElementRefOfClass(const CHIR::Expression& expr, CHIR::CHIRBuilder& builder);

CHIR::CustomTypeDef* GetTypeDefFromImplicitUsedFuncParam(
    const CGModule& cgModule, const std::string& funcName, const size_t paramIndex);

inline std::string GetCjStringDataLiteralName(const std::string& cjStr)
{
    return CJSTRING_DATA_PREFIX + Utils::HashString64(cjStr);
}

inline std::string GetCjStringLiteralName(const std::string& cjStr)
{
    return CJSTRING_LITERAL_PREFIX + Utils::HashString64(cjStr);
}

inline std::string GetConstantTupleName(const std::string& string)
{
    return CONST_TUPLE_PREFIX + Utils::HashString64(string);
}

inline std::string GetConstantArrayName(const std::string& string)
{
    return CONST_ARRAY_PREFIX + Utils::HashString64(string);
}

CGType* FixedCGTypeOfFuncArg(CGModule& cgMod, const CHIR::Value& chirFuncArg, llvm::Value& llvmValue);
void DumpIR(const llvm::Module& llvmModule, const std::string& filePath, bool debugMode);

llvm::StructType* GetLLVMStructType(
    CGModule& cgMod, const std::vector<CHIR::Type*>& elementTypes, const std::string& name = "");

std::vector<GenericTypeAndPath> GetGenericArgsFromCHIRType(const CHIR::Type& type);
CHIR::Type* GetTypeInnerType(const CHIR::Type& type, const GenericTypeAndPath& gtAndPath);

inline bool IsCoreFutureClass(const CHIR::ClassDef& classDef)
{
    return CheckCustomTypeDefIsExpected(classDef, CORE_PACKAGE_NAME, STD_LIB_FUTURE);
}

inline bool IsSyncRelatedClass(const CHIR::ClassDef& classDef)
{
    return CheckCustomTypeDefIsExpected(classDef, SYNC_PACKAGE_NAME, STD_LIB_MONITOR) ||
        CheckCustomTypeDefIsExpected(classDef, SYNC_PACKAGE_NAME, STD_LIB_MUTEX) ||
        CheckCustomTypeDefIsExpected(classDef, SYNC_PACKAGE_NAME, STD_LIB_WAIT_QUEUE);
}

inline bool IsWeakRefClass(const CHIR::ClassDef& classDef)
{
    return CheckCustomTypeDefIsExpected(classDef, REF_PACKAGE_NAME, STD_LIB_WEAK_REF);
}

inline bool IsExternalDefinedType(const CHIR::Type& type)
{
    if (!type.IsNominal()) {
        return true;
    }
    return dynamic_cast<const CHIR::CustomType&>(type).GetCustomTypeDef()->TestAttr(CHIR::Attribute::IMPORTED);
}

bool IsModifiableClass(const CHIR::Type& chirTy);
bool IsSizeTrustedInCompileUnit(CGModule& cgMod, const CHIR::Type& chirTy);
} // namespace CodeGen
} // namespace Cangjie

#endif // CANGJIE_CGUTILS_H
