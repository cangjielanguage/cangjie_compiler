// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Utils/CGUtils.h"

#include <queue>
#include <sstream>

#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"

#include "Base/CGTypes/CGEnumType.h"
#include "Utils/CGCommonDef.h"
#include "CGModule.h"
#include "cangjie/Basic/Linkage.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Value.h"
#include "cangjie/Option/Option.h"
#include "cangjie/Utils/CheckUtils.h"

using ChirTypeKind = Cangjie::CHIR::Type::TypeKind;
using namespace Cangjie::CHIR;

namespace {
// type mangling look-up table
const std::unordered_map<ChirTypeKind, std::string> TYPE_MANGLING_LUT = {
    {ChirTypeKind::TYPE_INT8, "a"},
    {ChirTypeKind::TYPE_INT16, "s"},
    {ChirTypeKind::TYPE_INT32, "i"},
    {ChirTypeKind::TYPE_INT64, "l"},
    {ChirTypeKind::TYPE_INT_NATIVE, "q"},
    {ChirTypeKind::TYPE_UINT8, "h"},
    {ChirTypeKind::TYPE_UINT16, "t"},
    {ChirTypeKind::TYPE_UINT32, "j"},
    {ChirTypeKind::TYPE_UINT64, "m"},
    {ChirTypeKind::TYPE_UINT_NATIVE, "r"},
    {ChirTypeKind::TYPE_FLOAT16, "Dh"},
    {ChirTypeKind::TYPE_FLOAT32, "f"},
    {ChirTypeKind::TYPE_FLOAT64, "d"},
    {ChirTypeKind::TYPE_RUNE, "c"},
    {ChirTypeKind::TYPE_BOOLEAN, "b"},
    {ChirTypeKind::TYPE_UNIT, "u"},
    {ChirTypeKind::TYPE_NOTHING, "n"},
    {ChirTypeKind::TYPE_CSTRING, "k"},
    {ChirTypeKind::TYPE_VOID, "u"},
};

void GetGenericArgsFromCHIRTypeHelper(const Cangjie::CHIR::Type& type, std::vector<size_t> path,
    std::vector<Cangjie::CodeGen::GenericTypeAndPath>& res)
{
    auto baseType = Cangjie::CodeGen::DeRef(type);
    if (baseType->IsGeneric()) {
        (void)res.emplace_back(
            Cangjie::CodeGen::GenericTypeAndPath(dynamic_cast<const Cangjie::CHIR::GenericType&>(*baseType), path));
        return;
    }

    size_t idx = 0;
    for (auto typeArg : baseType->GetTypeArgs()) {
        path.emplace_back(idx);
        GetGenericArgsFromCHIRTypeHelper(*typeArg, path, res);
        path.pop_back();
        ++idx;
    }
}
} // namespace

namespace Cangjie {
namespace CodeGen {
int64_t GetIntMaxOrMin(const CGModule& cgMod, const CHIR::IntType& ty, bool isMax)
{
    if (!ty.IsIntNative()) {
        auto minMax = G_SIGNED_INT_MAP.at(ty.GetTypeKind());
        return isMax ? minMax.second : minMax.first;
    }
    if (cgMod.GetCGContext().GetCompileOptions().target.arch == Triple::ArchType::ARM32) {
        auto minMax = G_SIGNED_INT_MAP.at(CHIR::Type::TypeKind::TYPE_INT32);
        return isMax ? minMax.second : minMax.first;
    } else {
        auto minMax = G_SIGNED_INT_MAP.at(CHIR::Type::TypeKind::TYPE_INT64);
        return isMax ? minMax.second : minMax.first;
    }
}

uint64_t GetUIntMax(const CGModule& cgMod, const CHIR::IntType& ty)
{
    if (!ty.IsUIntNative()) {
        return G_UNSIGNED_INT_MAP.at(ty.GetTypeKind());
    }
    if (cgMod.GetCGContext().GetCompileOptions().target.arch == Triple::ArchType::ARM32) {
        return G_UNSIGNED_INT_MAP.at(CHIR::Type::TypeKind::TYPE_UINT32);
    } else {
        return G_UNSIGNED_INT_MAP.at(CHIR::Type::TypeKind::TYPE_UINT64);
    }
}

std::string GetTypeName(const CGType* type)
{
    static const std::unordered_map<std::string, std::string> typeNameMap = {
        {UNIT_TYPE_STR, "Unit"},
    };
    auto llvmType = type->GetLLVMType();
    std::string name;
    // # isStructTy
    // such as `%Ref.Type = type{}`,
    // we can get the string "%Ref.Type" by calling `type->getStructName()`.
    // # Otherwise
    // such as `i8`
    // we can get the string "i8" by calling `type->print()`.
    if (llvmType->isStructTy()) {
        name = GetCodeGenTypeName(llvmType);
    } else {
        std::string typeStr;
        llvm::raw_string_ostream rso(typeStr);
        llvmType->print(rso);
        name = rso.str();
        if (llvmType->isPointerTy() && !type->IsCGFunction()) {
            auto baseType = type->GetPointerElementType()->GetLLVMType();
            if (baseType->isStructTy()) {
                name = GetCodeGenTypeName(baseType);
            }
        }
    }
    CJC_ASSERT(!name.empty());
    if (auto it = typeNameMap.find(name); it != typeNameMap.end()) {
        name = it->second;
    }
    return name;
}

namespace {
std::string ConstantValueToString(const CHIR::Constant& expr)
{
    std::string res;
    if (expr.IsBoolLit()) {
        res = std::to_string(expr.GetBoolLitVal());
    } else if (expr.IsFloatLit()) {
        res = std::to_string(expr.GetFloatLitVal());
    } else if (expr.IsSignedIntLit()) {
        res = std::to_string(expr.GetSignedIntLitVal());
    } else if (expr.IsUnSignedIntLit()) {
        res = std::to_string(expr.GetUnsignedIntLitVal());
    } else if (expr.IsRuneLit()) {
        res = std::to_string(expr.GetRuneLitVal());
    } else {
        CJC_ABORT();
    }
    return res;
}

std::tuple<bool, std::string> IsAllConstantNode(const std::vector<CHIR::Value*>& args)
{
    if (args.empty()) {
        return std::make_tuple(false, "");
    }

    bool isAllConstantNode = true;
    std::string serialized = "";
    for (auto node : args) {
        if (!node->IsLocalVar()) {
            isAllConstantNode = false;
            break;
        }
        auto localVar = StaticCast<const CHIR::LocalVar*>(node);
        auto expr = localVar->GetExpr();
        if (auto constant = DynamicCast<const Constant*>(expr); constant &&
            (constant->IsBoolLit() || constant->IsIntLit() || constant->IsFloatLit() || constant->IsRuneLit())) {
            serialized += "{" + ConstantValueToString(*constant) + localVar->GetType()->ToString() + "}";
            continue;
        }
        CJC_NULLPTR_CHECK(expr);
        bool isEleConstant = false;
        std::string serializedEle = "";
        if (expr->GetExprKind() == CHIR::ExprKind::TUPLE && localVar->GetType()->IsTuple()) {
            std::tie(isEleConstant, serializedEle) = IsConstantTuple(StaticCast<const CHIR::Tuple&>(*expr));
            serializedEle += "Tuple";
        } else if (expr->GetExprKind() == CHIR::ExprKind::VARRAY) {
            std::tie(isEleConstant, serializedEle) = IsConstantVArray(StaticCast<const CHIR::VArray&>(*expr));
            serializedEle += "VArray";
        } else {
            isAllConstantNode = false;
            break;
        }
        if (isEleConstant) {
            serialized += "{" + serializedEle + "}";
            continue;
        } else {
            isAllConstantNode = false;
            break;
        }
    }

    if (!isAllConstantNode) {
        serialized = "";
    }
    return std::make_tuple(isAllConstantNode, serialized);
}
} // namespace

std::tuple<bool, std::string> IsConstantArray(const CHIR::RawArrayLiteralInit& arrayLiteralInit)
{
    const auto& operands = arrayLiteralInit.GetOperands();
    const std::vector<CHIR::Value*> args(operands.begin() + 1, operands.end());
    return IsAllConstantNode(args);
}

std::tuple<bool, std::string> IsConstantVArray(const CHIR::VArray& varray)
{
    return IsAllConstantNode(varray.GetOperands());
}

std::tuple<bool, std::string> IsConstantTuple(const CHIR::Tuple& tuple)
{
    return IsAllConstantNode(tuple.GetOperands());
}

bool IsReferenceType(const CHIR::Type& ty, CGModule& cgMod)
{
    if (ty.IsRef()) {
        return IsReferenceType(*StaticCast<const CHIR::RefType&>(ty).GetBaseType(), cgMod);
    }
    if (ty.IsEnum()) {
        auto enumCGType = StaticCast<CGEnumType*>(CGType::GetOrCreate(cgMod, &ty));
        return enumCGType->IsCommonEnum() || enumCGType->IsOptionLikeRef() || enumCGType->IsOptionLikeT();
    }
    return ty.IsClass() || ty.IsBox() || ty.IsRawArray();
}

bool IsAllConstantValue(const std::vector<CGValue*>& args)
{
    return !args.empty() && std::all_of(args.begin(), args.end(), [](auto it) {
        return llvm::dyn_cast<llvm::Constant>(it->GetRawValue());
    });
}

bool SaveToBitcodeFile(const llvm::Module& module, const std::string& bcFilePath)
{
    std::error_code errorCode;
#ifdef _WIN32
    std::optional<std::string> tempPath = StringConvertor::NormalizeStringToUTF8(bcFilePath);
    CJC_ASSERT(tempPath.has_value() && "Incorrect file name encoding.");
    llvm::raw_fd_ostream os(tempPath.value(), errorCode);
#else
    llvm::raw_fd_ostream os(bcFilePath, errorCode);
#endif
    llvm::WriteBitcodeToFile(module, os);
    if (errorCode) {
        Errorln("Failed to write bitcode to file:", errorCode.message());
        return false;
    }
    os.close();
    return true;
}

uint64_t GetTypeSize(const CGModule& cgMod, llvm::Type& ty)
{
    return cgMod.GetLLVMModule()->getDataLayout().getTypeAllocSize(&ty);
}

uint64_t GetTypeSize(const CGModule& cgMod, const CHIR::Type& ty)
{
    if (ty.IsUnit() || ty.IsNothing()) {
        return 0U;
    }
    auto cgType = CGType::GetOrCreate(const_cast<CGModule&>(cgMod), &ty);
    return GetTypeSize(cgMod, *cgType->GetLLVMType());
}

uint64_t GetTypeAlignment(const CGModule& cgMod, const CHIR::Type& ty)
{
    if (ty.IsUnit() || ty.IsNothing()) {
        return 1U;
    }
    auto cgType = CGType::GetOrCreate(const_cast<CGModule&>(cgMod), &ty);
    return GetTypeAlignment(cgMod, *cgType->GetLLVMType());
}

uint64_t GetTypeAlignment(const CGModule& cgMod, llvm::Type& ty)
{
    return cgMod.GetLLVMModule()->getDataLayout().getABITypeAlignment(&ty);
}

bool IsZeroSizedTypeInC(const CGModule& cgMod, const CHIR::Type& chirTy)
{
    if (chirTy.IsRef()) {
        return IsZeroSizedTypeInC(cgMod, *StaticCast<const CHIR::RefType&>(chirTy).GetBaseType());
    }
    // For windows-gnu target, an (nested) empty struct is not treated as zero-sized type.
    return chirTy.IsUnit() || chirTy.IsNothing() ||
        (cgMod.GetCGContext().GetCompileOptions().target.os != Triple::OSType::WINDOWS &&
            GetTypeSize(cgMod, chirTy) == 0);
}

bool IsTypeContainsRef(llvm::Type* type)
{
    if (type->isPointerTy() && type->getPointerAddressSpace() == 1) {
        return true;
    }
    if (auto st = llvm::dyn_cast<llvm::StructType>(type)) {
        return std::any_of(st->element_begin(), st->element_end(), IsTypeContainsRef);
    }
    if (auto at = llvm::dyn_cast<llvm::ArrayType>(type)) {
        return IsTypeContainsRef(at->getElementType());
    }
    if (auto vt = llvm::dyn_cast<llvm::VectorType>(type)) {
        return IsTypeContainsRef(vt->getScalarType());
    }
    return false;
}

void CollectLinkNameUsedInMetaInsert(
    std::queue<const llvm::MDNode*>& queueMD, const llvm::MDOperand& op, std::unordered_set<std::string>& ctxSet)
{
    const llvm::MDNode* mdNodeOp = llvm::dyn_cast_or_null<llvm::MDNode>(op);
    if (mdNodeOp) {
        // Add the MDNode to the queue.
        queueMD.push(mdNodeOp);
    } else {
        // Add leaf node.
        const llvm::MDString* mds = llvm::dyn_cast<llvm::MDString>(op);
        if (mds) {
            ctxSet.emplace(mds->getString());
        }
    }
}

void CollectLinkNameUsedInMeta(const llvm::NamedMDNode* n, std::unordered_set<std::string>& ctxSet)
{
    if (!n) {
        return;
    }
    std::unordered_set<const llvm::Metadata*> mdVisitedSet;
    std::queue<const llvm::MDNode*> queueMD;
    for (unsigned i = 0; i < n->getNumOperands(); i++) {
        queueMD.push(n->getOperand(i));
    }
    while (!queueMD.empty()) {
        const llvm::MDNode* front = queueMD.front();
        queueMD.pop();
        for (unsigned i = 0; i < front->getNumOperands(); i++) {
            if (mdVisitedSet.find(front->getOperand(i).get()) == mdVisitedSet.end()) {
                CollectLinkNameUsedInMetaInsert(queueMD, front->getOperand(i), ctxSet);
                mdVisitedSet.insert(front->getOperand(i).get());
            }
        }
    }
}

CHIR::CustomTypeDef* GetTypeDefFromImplicitUsedFuncParam(
    const CGModule& cgModule, const std::string& funcName, const size_t paramIndex)
{
    auto func = cgModule.GetCGContext().GetImplicitUsedFunc(funcName);
    CJC_ASSERT(func);
    auto fType = dynamic_cast<CHIR::FuncType*>(func->GetType());
    auto classBaseType = dynamic_cast<CHIR::RefType*>(fType->GetParamType(paramIndex))->GetBaseType();
    CJC_ASSERT(classBaseType);
    auto classDef = dynamic_cast<CHIR::ClassType*>(classBaseType)->GetClassDef();
    CJC_ASSERT(classDef);
    return classDef;
}

bool IsGetElementRefOfClass(const CHIR::Expression& expr, CHIR::CHIRBuilder& builder)
{
    if (expr.GetExprKind() != CHIR::ExprKind::GET_ELEMENT_REF) {
        return false;
    }
    auto& getEleRef = dynamic_cast<const CHIR::GetElementRef&>(expr);
    auto baseType = getEleRef.GetLocation()->GetType()->GetTypeArgs()[0];
    auto& path = getEleRef.GetPath();
    CJC_ASSERT(!path.empty());
    for (size_t idx = 0; idx < path.size() - 1; ++idx) {
        baseType = GetFieldOfType(*baseType, path[idx], builder);
    }
    return baseType->IsClass();
}

bool IsCHIRWrapper(const std::string& chirFuncName)
{
    auto endsWith = [](const std::string& str, const std::string& suffix) -> bool {
        return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    return endsWith(chirFuncName, "_cc_imported_wrapper") || endsWith(chirFuncName, "_cc_wrapper") ||
        endsWith(chirFuncName, "_cc_abstractFunc_wrapper");
}

llvm::GlobalValue::LinkageTypes CHIRLinkage2LLVMLinkage(Linkage chirLinkage)
{
    switch (chirLinkage) {
        case Linkage::WEAK_ODR:
            return llvm::GlobalValue::WeakODRLinkage;
        case Linkage::EXTERNAL:
            return llvm::GlobalValue::ExternalLinkage;
        case Linkage::INTERNAL:
            return llvm::GlobalValue::InternalLinkage;
        case Linkage::LINKONCE_ODR:
            return llvm::GlobalValue::LinkOnceODRLinkage;
        default:
            return llvm::GlobalValue::ExternalLinkage;
    }
}

void AddLinkageTypeMetadata(
    llvm::GlobalObject& globalObject, llvm::GlobalValue::LinkageTypes linkageType, bool markByMD)
{
    if (!markByMD) {
        globalObject.setLinkage(linkageType);
        return;
    }

    static const std::unordered_map<llvm::GlobalValue::LinkageTypes, std::string> linkageType2Str = {
        {llvm::GlobalValue::ExternalLinkage, "ExternalLinkage"},
        {llvm::GlobalValue::AvailableExternallyLinkage, "AvailableExternallyLinkage"},
        {llvm::GlobalValue::LinkOnceAnyLinkage, "LinkOnceAnyLinkage"},
        {llvm::GlobalValue::LinkOnceODRLinkage, "LinkOnceODRLinkage"},
        {llvm::GlobalValue::WeakAnyLinkage, "WeakAnyLinkage"},
        {llvm::GlobalValue::WeakODRLinkage, "WeakODRLinkage"},
        {llvm::GlobalValue::AppendingLinkage, "AppendingLinkage"},
        {llvm::GlobalValue::InternalLinkage, "InternalLinkage"},
        {llvm::GlobalValue::PrivateLinkage, "PrivateLinkage"},
        {llvm::GlobalValue::ExternalWeakLinkage, "ExternalWeakLinkage"},
        {llvm::GlobalValue::CommonLinkage, "CommonLinkage"},
    };
    auto linkageTypeStr = linkageType2Str.find(linkageType);
    CJC_ASSERT(linkageTypeStr != linkageType2Str.end());
    auto md = llvm::MDTuple::get(
        globalObject.getContext(), {llvm::MDString::get(globalObject.getContext(), linkageTypeStr->second)});
    globalObject.addMetadata("LinkageType", *md);
}

llvm::GlobalValue::LinkageTypes GetLinkageTypeOfGlobalObject(const llvm::GlobalObject& globalObject)
{
    static const std::unordered_map<std::string, llvm::GlobalValue::LinkageTypes> str2LinkageType = {
        {"ExternalLinkage", llvm::GlobalValue::ExternalLinkage},
        {"AvailableExternallyLinkage", llvm::GlobalValue::AvailableExternallyLinkage},
        {"LinkOnceAnyLinkage", llvm::GlobalValue::LinkOnceAnyLinkage},
        {"LinkOnceODRLinkage", llvm::GlobalValue::LinkOnceODRLinkage},
        {"WeakAnyLinkage", llvm::GlobalValue::WeakAnyLinkage},
        {"WeakODRLinkage", llvm::GlobalValue::WeakODRLinkage},
        {"AppendingLinkage", llvm::GlobalValue::AppendingLinkage},
        {"InternalLinkage", llvm::GlobalValue::InternalLinkage},
        {"PrivateLinkage", llvm::GlobalValue::PrivateLinkage},
        {"ExternalWeakLinkage", llvm::GlobalValue::ExternalWeakLinkage},
        {"CommonLinkage", llvm::GlobalValue::CommonLinkage},
    };
    if (auto md = globalObject.getMetadata("LinkageType"); md) {
        auto linkageTypeStr = llvm::cast<llvm::MDString>(md->getOperand(0))->getString().str();
        auto linkageType = str2LinkageType.find(linkageTypeStr);
        CJC_ASSERT(linkageType != str2LinkageType.end());
        return linkageType->second;
    } else {
        return globalObject.getLinkage();
    }
}

CGType* FixedCGTypeOfFuncArg(CGModule& cgMod, const CHIR::Value& chirFuncArg, llvm::Value& llvmValue)
{
    auto chirFuncArgType = chirFuncArg.GetType();
    auto cgType = CGType::GetOrCreate(cgMod, chirFuncArgType);
    bool byRef = chirFuncArgType->IsStruct() || chirFuncArgType->IsTuple() || chirFuncArgType->IsVArray() ||
        chirFuncArgType->IsUnit() || chirFuncArgType->IsNothing() ||
        (chirFuncArgType->IsEnum() && (StaticCast<CGEnumType*>(cgType)->PassByReference()));
    if (byRef) {
        auto refType = CGType::GetRefTypeOf(cgMod.GetCGContext().GetCHIRBuilder(), *chirFuncArgType);
        cgType = CGType::GetOrCreate(cgMod, refType, {llvmValue.getType()->getPointerAddressSpace()});
    }
    cgMod.SetOrUpdateMappedCGValue(&chirFuncArg, std::make_unique<CGValue>(&llvmValue, cgType));
    return cgType;
}

void DumpIR(const llvm::Module& llvmModule, const std::string& filePath, bool debugMode)
{
#ifndef NDEBUG
    if (!debugMode) {
        return;
    }
    if (filePath.empty()) {
        llvmModule.print(llvm::outs(), nullptr);
        return;
    }

    std::string normalizedPath = filePath;
#ifdef _WIN32
    std::optional<std::string> tempPath = StringConvertor::NormalizeStringToUTF8(filePath);
    CJC_ASSERT(tempPath.has_value() && "Incorrect file name encoding.");
    normalizedPath = tempPath.value();
#endif
    if (FileUtil::CreateDirs(normalizedPath) < 0) {
        return;
    }
    std::error_code errorCode;
    llvm::raw_fd_ostream fileOS(normalizedPath, errorCode);
    llvmModule.print(fileOS, nullptr);
#else
    (void)llvmModule;
    (void)filePath;
    (void)debugMode;
#endif
}

llvm::StructType* GetLLVMStructType(
    CGModule& cgMod, const std::vector<CHIR::Type*>& elementTypes, const std::string& name)
{
    auto& cgCtx = cgMod.GetCGContext();
    auto& llvmCtx = cgMod.GetLLVMContext();
    auto layoutType = llvm::StructType::getTypeByName(llvmCtx, name);
    if (layoutType && cgCtx.IsGeneratedStructType(name)) {
        return layoutType;
    } else if (!layoutType) {
        layoutType = llvm::StructType::create(llvmCtx, name);
    }
    cgCtx.AddGeneratedStructType(layoutType->getName().str());

    std::vector<llvm::Type*> elemTypes;
    for (auto elemType : elementTypes) {
        auto paramCGType = CGType::GetOrCreate(cgMod, elemType);
        elemTypes.emplace_back(paramCGType->GetLLVMType());
    }
    SetStructTypeBody(layoutType, elemTypes);
    return layoutType;
}

std::vector<GenericTypeAndPath> GetGenericArgsFromCHIRType(const CHIR::Type& type)
{
    std::vector<GenericTypeAndPath> res;
    std::vector<size_t> path;
    GetGenericArgsFromCHIRTypeHelper(type, path, res);
    return res;
}

CHIR::Type* GetTypeInnerType(const CHIR::Type& type, const GenericTypeAndPath& gtAndPath)
{
    auto& path = gtAndPath.GetPath();
    size_t idx = 0;
    const CHIR::Type* res = DeRef(type);
    while (idx < path.size()) {
        const auto& pathPoint = path[idx];
        auto typeArgsOfRes = res->GetTypeArgs();
        CJC_ASSERT(pathPoint < typeArgsOfRes.size() && "Invalid generic type path.");
        res = DeRef(*typeArgsOfRes[pathPoint]);
        ++idx;
    }
    return const_cast<CHIR::Type*>(res);
}

bool IsThisArgOfStructMethod(const CHIR::Value& chirValue)
{
    if (!chirValue.IsParameter()) {
        return false;
    }

    auto& chirParam = StaticCast<const CHIR::Parameter&>(chirValue);
    auto chirFunc = chirParam.GetTopLevelFunc();
    if (!chirFunc || !chirFunc->GetNumOfParams() || chirFunc->GetParam(0) != &chirParam) {
        return false;
    }
    if (chirFunc->TestAttr(CHIR::Attribute::STATIC) || !IsStructOrExtendMethod(*chirFunc)) {
        return false;
    }
    return true;
}
} // namespace CodeGen
} // namespace Cangjie
