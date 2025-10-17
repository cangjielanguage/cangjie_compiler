// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements LLVM CodeGen.
 * LLVM CodeGen CFFI extends BaseGen CFFI.
 */

#include "CJNative/CJNativeCGCFFI.h"

#include "CGModule.h"
#include "IRAttribute.h"
#include "Utils/CGCommonDef.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/Utils/Casting.h"

using namespace Cangjie;
using namespace CodeGen;

namespace {
const unsigned int BITS_PER_BYTE = 8;
const unsigned int BYTES_PER_WORD = 8;

inline bool IsStructPtrTy(llvm::Type* type)
{
    CJC_NULLPTR_CHECK(type);
    if (!type->isPointerTy()) {
        return false;
    }
    auto ptrType = llvm::cast<llvm::PointerType>(type);
    if (GetPointerElementType(ptrType)->isStructTy()) {
        return true;
    }
    return false;
}

inline llvm::Type* GetArgType(const llvm::Function* func, unsigned idx)
{
    CJC_ASSERT(idx < func->arg_size());
    return func->getArg(idx)->getType();
}

bool IsMetCType(const CHIR::Type& chirTy)
{
    if (chirTy.IsVArray() || IsVArrayRef(chirTy)) {
        auto varrTy = chirTy.IsRef() ? chirTy.GetTypeArgs()[0] : &chirTy;
        auto elemType = StaticCast<CHIR::VArrayType*>(varrTy)->GetElementType();
        CJC_ASSERT(elemType && !elemType->IsInvalid());
        return IsMetCType(*elemType);
    }
    bool isPrimitiveCType = Utils::In(chirTy.GetTypeKind(),
        {CHIR::Type::TypeKind::TYPE_UNIT, CHIR::Type::TypeKind::TYPE_BOOLEAN, CHIR::Type::TypeKind::TYPE_INT8,
            CHIR::Type::TypeKind::TYPE_UINT8, CHIR::Type::TypeKind::TYPE_INT16, CHIR::Type::TypeKind::TYPE_UINT16,
            CHIR::Type::TypeKind::TYPE_INT32, CHIR::Type::TypeKind::TYPE_UINT32, CHIR::Type::TypeKind::TYPE_INT64,
            CHIR::Type::TypeKind::TYPE_UINT64, CHIR::Type::TypeKind::TYPE_INT_NATIVE,
            CHIR::Type::TypeKind::TYPE_UINT_NATIVE, CHIR::Type::TypeKind::TYPE_FLOAT32,
            CHIR::Type::TypeKind::TYPE_FLOAT64});
    return isPrimitiveCType || chirTy.IsCPointer() || chirTy.IsCString() || IsCFunc(chirTy) || IsCStruct(chirTy);
}

CGType* GetCGType(CGModule& cgMod, const llvm::Type& llvmTy)
{
    if (llvmTy.isIntegerTy(1)) {
        return CGType::GetBoolCGType(cgMod);
    }
    constexpr unsigned bits8 = 8;
    if (llvmTy.isIntegerTy(bits8)) {
        return CGType::GetInt8CGType(cgMod);
    }
    constexpr unsigned bits16 = 16;
    if (llvmTy.isIntegerTy(bits16)) {
        return CGType::GetInt16CGType(cgMod);
    }
    constexpr unsigned bits32 = 32;
    if (llvmTy.isIntegerTy(bits32)) {
        return CGType::GetInt32CGType(cgMod);
    }
    constexpr unsigned bits64 = 64;
    if (llvmTy.isIntegerTy(bits64)) {
        return CGType::GetInt64CGType(cgMod);
    }
    if (llvmTy.isFloatTy()) {
        return CGType::GetFloat32CGType(cgMod);
    }
    if (llvmTy.isDoubleTy()) {
        return CGType::GetFloat64CGType(cgMod);
    }
    // Rollback
    return CGType::GetInt8CGType(cgMod);
}

#ifdef __APPLE__
inline void AddAttrForIntTypeArgOnMacMx(llvm::Function& llvmFunc, const llvm::Argument& arg)
{
    auto intType = llvm::dyn_cast<llvm::IntegerType>(arg.getType());
    if (!intType) {
        return;
    }
    AddParamAttr(&llvmFunc, arg.getArgNo(), llvm::Attribute::NoUndef);
    if (intType->isIntegerTy(1u)) { // 1u means bool type.
        AddParamAttr(&llvmFunc, arg.getArgNo(), llvm::Attribute::ZExt);
    } else if (intType->getBitWidth() < 32) { // 32 means int32_t type.
        AddParamAttr(&llvmFunc, arg.getArgNo(), llvm::Attribute::SExt);
    }
}
#endif
} // namespace

bool LinuxCJNativeCGCFFI::ProcessInvocation(
    const CHIR::FuncType& chirFuncTy, std::vector<CGValue*>& args, IRBuilder2& builder)
{
    auto found = cfuncMap.find(&chirFuncTy);
    if (found == cfuncMap.end()) {
        return false;
    }
    auto& kinds = found->second.second;
    for (size_t i = 0, argIdx = 0; i < kinds.size(); ++i, ++argIdx) {
        while (i < kinds.size() && kinds[i] == ProcessKind::SKIP) {
            ++i;
        }
        if (i >= kinds.size()) {
            break;
        }
        if (kinds[i] == ProcessKind::NO_PROCESS) {
            continue;
        }
        auto paramTys = chirFuncTy.GetParamTypes();
        CJC_ASSERT(i < paramTys.size() && paramTys[i]);
        auto& paramTy = StaticCast<CHIR::StructType&>(*paramTys[i]);
        ProcessInvocationArg(paramTy, kinds[i], argIdx, args, builder);
    }
    auto foundRet = typeMap.find(chirFuncTy.GetReturnType());
    if (foundRet == typeMap.end()) {
        return false;
    }
    return !foundRet->second.IsIndirect();
}

llvm::Type* LinuxCJNativeCGCFFI::GetReturnType(CHIR::Type& chirTy, std::vector<llvm::Type*>& params)
{
    auto& llvmCtx = ctx.GetLLVMContext();
    if (IsUnitOrNothing(chirTy)) {
        return llvm::Type::getVoidTy(llvmCtx);
    }
    if (IsUnsizedStructTy(chirTy)) {
        return nullptr;
    }
    if (chirTy.IsStruct()) {
        return GetStructReturnType(StaticCast<CHIR::StructType&>(chirTy), params);
    }
    return GetLLVMType(chirTy);
}

llvm::FunctionType* LinuxAmd64CJNativeCGCFFI::GetCFuncType(const CHIR::FuncType& chirFuncTy)
{
    auto found = cfuncMap.find(&chirFuncTy);
    if (found != cfuncMap.end()) {
        return found->second.first;
    }
    CJC_ASSERT(chirFuncTy.IsCFunc());
    int8_t intNum = 6; // 6 is integer register num.
    int8_t sseNum = 8; // 8 is vector register num.
    std::vector<llvm::Type*> paramTys;
    auto chirParamTys = chirFuncTy.GetParamTypes();
    paramTys.reserve(chirParamTys.size());
    std::vector<ProcessKind> kinds;
    kinds.reserve(chirParamTys.size());
    llvm::Type* retTy = GetReturnType(*chirFuncTy.GetReturnType(), paramTys);
    if (!retTy) {
        return nullptr;
    }
    for (auto it : chirParamTys) {
        CJC_ASSERT(IsMetCType(*it));
        if (IsUnsizedStructTy(*it)) {
            return nullptr;
        }
        kinds.emplace_back(GetParamType(*it, intNum, sseNum, paramTys));
    }
    auto resTy = llvm::FunctionType::get(retTy, paramTys, chirFuncTy.HasVarArg());
    cfuncMap.emplace(&chirFuncTy, std::make_pair(resTy, kinds));
    return resTy;
}

void LinuxAmd64CJNativeCGCFFI::AddFunctionAttr(const CHIR::FuncType& chirFuncTy, llvm::Function& llvmFunc)
{
    CJC_ASSERT(chirFuncTy.IsCFunc());
    auto found = cfuncMap.find(&chirFuncTy);
    if (found == cfuncMap.end()) {
        return;
    }
    auto found1 = typeMap.find(chirFuncTy.GetReturnType());
    unsigned argIdx = 0;
    if (found1 != typeMap.end() && found1->second.IsIndirect()) {
        AddSRetAttribute(llvmFunc.arg_begin());
        argIdx++;
    } else if (llvmFunc.getReturnType()->isIntegerTy(1)) {
        AddRetAttr(&llvmFunc, llvm::Attribute::ZExt);
    }
    auto& kinds = found->second.second;
    for (auto it = kinds.begin(); it != kinds.end(); ++it, ++argIdx) {
        it = std::find_if(it, kinds.end(), [](auto kind) { return kind != ProcessKind::SKIP; });
        if (it == kinds.end()) {
            break;
        }
        if (GetArgType(&llvmFunc, argIdx)->isIntegerTy(1)) {
            (llvmFunc.arg_begin() + argIdx)->addAttr(llvm::Attribute::ZExt);
            continue;
        }
        if (*it == ProcessKind::NO_PROCESS || *it == ProcessKind::DIRECT) {
            continue;
        }
        if (*it == ProcessKind::EXPAND) {
            ++argIdx;
            continue;
        }
        if (*it == ProcessKind::INDIRECT) {
            CJC_ASSERT(IsStructPtrTy(GetArgType(&llvmFunc, argIdx)));
            auto argument = llvmFunc.arg_begin() + argIdx;
            AddByValAttribute(argument, GetTypeAlignment(cgMod, *argument->getType()));
        }
    }
    AddFnAttr(&llvmFunc, llvm::Attribute::get(llvmFunc.getContext(), CodeGen::CFUNC_ATTR));
}

void LinuxAmd64CJNativeCGCFFI::ProcessParam(
    CHIR::Type& chirParamTy, LLVMFuncArgIt& arg, llvm::Value* place, IRBuilder2& builder)
{
    if (!chirParamTy.IsStruct()) {
        return;
    }
    auto found = typeMap.find(&chirParamTy);
    if (found == typeMap.end()) {
        return;
    }
    auto& info = found->second;
    if (info.IsDirect()) {
        place = builder.CreateBitCast(place, arg->getType()->getPointerTo());
        builder.CreateStore(arg, place);
    } else if (info.IsExpand()) {
        llvm::Value* arg1 = arg;
        llvm::Value* arg2 = ++arg;
        auto arg1Type = arg1->getType();
        auto arg2Type = arg2->getType();
        auto destTy = llvm::StructType::get(ctx.GetLLVMContext(), {arg1Type, arg2Type});

        auto alloca = builder.CreateEntryAlloca(destTy);
        auto part0 = builder.CreateStructGEP(destTy, alloca, 0);
        builder.CreateStore(arg1, part0);
        auto part1 = builder.CreateStructGEP(destTy, alloca, 1);
        builder.CreateStore(arg2, part1);

        size_t actualSize = GetTypeSize(cgMod, chirParamTy);
        builder.CreateMemCpy(place, GetAlign(cgMod, chirParamTy), alloca, GetAlign(cgMod, *destTy), actualSize);
    }
}

void LinuxAmd64CJNativeCGCFFI::ProcessInvocationArg(
    CHIR::StructType& chirParamTy, ProcessKind kind, size_t& argIdx, std::vector<CGValue*>& args, IRBuilder2& builder)
{
    if (kind == ProcessKind::INDIRECT) {
        return;
    }
    auto found = typeMap.find(&chirParamTy);
    CJC_ASSERT(found != typeMap.end());
    auto& info = found->second;
    if (kind == ProcessKind::DIRECT) {
        CJC_ASSERT(info.IsDirect());
        const unsigned int addrSpace = args[argIdx]->GetRawValue()->getType()->getPointerAddressSpace();
        auto cast = builder.CreateBitCast(args[argIdx]->GetRawValue(), info[0]->getPointerTo(addrSpace));
        args[argIdx] = cgMod.CreateGhostCFuncArgValue(
            *builder.LLVMIRBuilder2::CreateLoad(info[0], cast), *GetCGType(cgMod, *info[0]));
    } else {
        CJC_ASSERT(info.IsExpand());
        auto& llvmCtx = ctx.GetLLVMContext();
        auto tmpTy = llvm::StructType::get(llvmCtx, {info[0], info[1]});

        auto alloca = builder.CreateEntryAlloca(tmpTy, nullptr, "arg.copy");
        builder.CreateMemCpy(alloca, GetAlign(cgMod, *tmpTy), args[argIdx]->GetRawValue(), GetAlign(cgMod, chirParamTy),
            GetTypeSize(cgMod, chirParamTy));

        auto argPtr1 = builder.CreateStructGEP(tmpTy, alloca, 0);
        auto argPtr2 = builder.CreateStructGEP(tmpTy, alloca, 1);
        args.emplace(args.begin() + static_cast<std::ptrdiff_t>(argIdx),
            cgMod.CreateGhostCFuncArgValue(
                *builder.LLVMIRBuilder2::CreateLoad(info[0], argPtr1), *GetCGType(cgMod, *info[0])));
        args[++argIdx] = cgMod.CreateGhostCFuncArgValue(
            *builder.LLVMIRBuilder2::CreateLoad(info[1], argPtr2), *GetCGType(cgMod, *info[1]));
    }
}

llvm::Value* LinuxAmd64CJNativeCGCFFI::ProcessRetValue(const llvm::Type& retType, llvm::Value& val, IRBuilder2& builder)
{
    CJC_ASSERT(!retType.isVoidTy());
    if (auto structType = llvm::dyn_cast<llvm::StructType>(&retType); structType && structType->isLiteral()) {
        auto tmpVal = &val;
        if (val.getType() != structType->getPointerTo()) {
            tmpVal = builder.CreateBitCast(&val, structType->getPointerTo());
        }
        return builder.LLVMIRBuilder2::CreateLoad(const_cast<llvm::Type*>(&retType), tmpVal);
    }
    llvm::Value* castRes = &val;
    if (!retType.isStructTy() && IsStructPtrTy(val.getType())) {
        castRes = builder.CreateBitCast(&val, retType.getPointerTo());
    }
    return builder.LLVMIRBuilder2::CreateLoad(const_cast<llvm::Type*>(&retType), castRes);
}

llvm::Type* LinuxAmd64CJNativeCGCFFI::GetStructReturnType(CHIR::StructType& chirTy, std::vector<llvm::Type*>& params)
{
    auto& llvmCtx = ctx.GetLLVMContext();
    if (GetTypeSize(cgMod, chirTy) == 0) {
        return llvm::Type::getVoidTy(llvmCtx);
    }
    auto info = GetMappingArgInfo(chirTy, false);
    if (info.IsIndirect()) {
        auto structType = GetLLVMType(chirTy);
        CJC_ASSERT(structType->isStructTy());
        params.emplace_back(structType->getPointerTo());
        return llvm::Type::getVoidTy(llvmCtx);
    }
    if (info.IsDirect()) {
        return info[0];
    }
    return llvm::StructType::get(llvmCtx, {info[0], info[1]});
}

ProcessKind LinuxAmd64CJNativeCGCFFI::GetParamType(
    CHIR::Type& chirTy, int8_t& intNum, int8_t& sseNum, std::vector<llvm::Type*>& params)
{
    CJC_ASSERT(!IsUnitOrNothing(chirTy));
    if (chirTy.IsStruct()) {
        if (GetTypeSize(cgMod, chirTy) == 0) {
            return ProcessKind::SKIP;
        }
        auto info = GetMappingArgInfo(StaticCast<CHIR::StructType&>(chirTy), true);
        auto structType = GetLLVMType(chirTy);
        CJC_ASSERT(structType->isStructTy());
        if (info.IsIndirect()) {
            params.emplace_back(structType->getPointerTo());
            return ProcessKind::INDIRECT;
        } else {
            auto resTy = GetParamType(structType, info, intNum, sseNum);
            if (resTy == nullptr) {
                params.emplace_back(info[0]);
                params.emplace_back(info[1]);
                return ProcessKind::EXPAND;
            }
            if (resTy != structType) {
                params.emplace_back(resTy);
                return ProcessKind::DIRECT;
            } else {
                params.emplace_back(structType->getPointerTo());
                return ProcessKind::INDIRECT;
            }
        }
    }
    llvm::Type* paramType = GetLLVMType(chirTy);
    // VArray as CFunc parament pass by a i8*.
    if (chirTy.IsVArray()) {
        paramType = llvm::Type::getInt8PtrTy(ctx.GetLLVMContext());
    }
    if (paramType->isIntOrPtrTy()) {
        intNum -= intNum > 0 ? 1 : 0;
    } else if (paramType->isFloatingPointTy()) {
        sseNum -= sseNum > 0 ? 1 : 0;
    }
    params.emplace_back(paramType);
    return ProcessKind::NO_PROCESS;
}

// If return nullptr, the callee should use the ty(s) stored in @p info.
// Otherwise, use thr return value.
llvm::Type* LinuxAmd64CJNativeCGCFFI::GetParamType(
    llvm::Type* ty, const ABIArgInfo& info, int8_t& intNum, int8_t& sseNum) const
{
    CJC_ASSERT(!info.IsIndirect() && "ArgInfo.size() cannot be 0.");
    if (info.IsDirect()) {
        if (info[0]->isIntOrPtrTy() && intNum > 0) {
            intNum--;
        } else if (info[0]->isFloatingPointTy() && sseNum > 0) {
            sseNum--;
        }
        return info[0];
    }
    CJC_ASSERT(info.IsExpand() && "ArgInfo.size() must be 2 here.");
    const int8_t infoSize = 2;
    if (info[0]->isIntOrPtrTy() && info[1]->isIntOrPtrTy()) {
        if (intNum < infoSize) {
            return ty;
        }
        intNum -= infoSize;
        return nullptr;
    }
    if (info[0]->isFloatingPointTy() && info[1]->isFloatingPointTy()) {
        if (sseNum < infoSize) {
            return ty;
        }
        sseNum -= infoSize;
        return nullptr;
    }
    if (intNum < 1 || sseNum < 1) {
        return ty;
    }
    intNum--;
    sseNum--;
    return nullptr;
}

ABIArgInfo LinuxAmd64CJNativeCGCFFI::GetMappingArgInfo(CHIR::StructType& chirTy, bool isArg)
{
    auto found = typeMap.find(&chirTy);
    if (found != typeMap.end()) {
        return found->second;
    }
    auto llvmType = GetLLVMType(chirTy);
    auto classes = ClassifyType(*llvmType);
    // According to the ABI SPEC of Linux x86_64, if a struct doesn't be passed by stack pointer,
    // it can be represented as one or two primitive types in the function type,
    // we mark it as a pair (low, high), in this case, the low part type cannot be nullptr, and
    // the high part type can be nullptr.
    // If the low part type is nullptr, the struct will be passed by stack pointer.
    auto lowPartTy = GetLowPartType(llvmType, classes, isArg);
    if (lowPartTy == nullptr) {
        return typeMap.emplace(&chirTy, ABIArgInfo::GetIndirect()).first->second;
    }

    if (classes.size() <= 1 || (lowPartTy->isVoidTy() && classes[1] == WordClass::NO_CLASS)) {
        return typeMap.emplace(&chirTy, ABIArgInfo::GetDirect(lowPartTy)).first->second;
    }

    llvm::Type* highPartTy = nullptr;
    if (classes[1] == WordClass::INTEGER) {
        // CONSIDER: When does this scenario occur?
        if (classes[0] == WordClass::NO_CLASS) {
            return typeMap.emplace(&chirTy, ABIArgInfo::GetIndirect()).first->second;
        }
        highPartTy = GetIntegerTypeWithOffset(*llvmType, BYTES_PER_WORD, *llvmType, BYTES_PER_WORD);
    } else if (classes[1] == WordClass::SSE) {
        // CONSIDER: When does this scenario occur?
        if (classes[0] == WordClass::NO_CLASS) {
            return typeMap.emplace(&chirTy, ABIArgInfo::GetIndirect()).first->second;
        }
        highPartTy = GetSseTypeWithOffset(*llvmType, BYTES_PER_WORD, *llvmType, BYTES_PER_WORD);
    } else if (classes[1] == WordClass::SSEUP) {
        lowPartTy = llvm::Type::getFP128Ty(ctx.GetLLVMContext());
    } else if (classes[1] == WordClass::X87UP) {
        // CONSIDER: When does this scenario occur?
        CJC_ASSERT(classes[0] == WordClass::X87);
    } else {
        CJC_ASSERT(false && "MEMORY, NO_CLASS, X87, COMPLEX_X87 cannot be here.");
    }
    if (highPartTy == nullptr) {
        return typeMap.emplace(&chirTy, ABIArgInfo::GetDirect(lowPartTy)).first->second;
    }
    return typeMap.emplace(&chirTy, ABIArgInfo::GetExpand(lowPartTy, highPartTy)).first->second;
}

llvm::Type* LinuxAmd64CJNativeCGCFFI::GetLowPartType(
    llvm::Type* type, std::vector<WordClass>& classes, bool isArg) const
{
    auto& llvmCtx = ctx.GetLLVMContext();
    auto lowPartTy = llvm::Type::getVoidTy(llvmCtx);
    if (classes[0] == WordClass::NO_CLASS) {
        return lowPartTy;
    } else if (classes[0] == WordClass::MEMORY) {
        return nullptr;
    } else if (classes[0] == WordClass::INTEGER) {
        lowPartTy = GetIntegerTypeWithOffset(*type, 0, *type, 0);
        // CONSIDER: Is ZEXT & SEXT needed?
    } else if (classes[0] == WordClass::SSE) {
        lowPartTy = GetSseTypeWithOffset(*type, 0, *type, 0);
    } else if (classes[0] == WordClass::X87) {
        // CONSIDER: CType does NOT contain long double type now.
        if (isArg) {
            return nullptr;
        }
        lowPartTy = llvm::Type::getX86_FP80Ty(llvmCtx);
    } else if (classes[0] == WordClass::COMPLEX_X87) {
        // CONSIDER: CType does NOT contain complex type now.
        if (isArg) {
            return nullptr;
        }
        lowPartTy = llvm::Type::getX86_FP80Ty(llvmCtx);
    } else {
        CJC_ASSERT(false && "SSEUP, X87UP cannot be here.");
    }
    return lowPartTy;
}

static inline void FillWithMemory(std::vector<WordClass>& wordClasses)
{
    std::fill(wordClasses.begin(), wordClasses.end(), WordClass::MEMORY);
}

static void UnifyWordClass(std::vector<WordClass>::iterator wordi, WordClass newClass)
{
    if (*wordi == newClass) {
        return;
    } else if (*wordi == WordClass::NO_CLASS) {
        *wordi = newClass;
    } else if (newClass == WordClass::NO_CLASS) {
        return;
    } else if (*wordi == WordClass::MEMORY || newClass == WordClass::MEMORY) {
        *wordi = WordClass::MEMORY;
    } else if (*wordi == WordClass::INTEGER || newClass == WordClass::INTEGER) {
        *wordi = WordClass::INTEGER;
    } else if (*wordi == WordClass::X87 || *wordi == WordClass::X87UP || *wordi == WordClass::COMPLEX_X87 ||
        newClass == WordClass::X87 || newClass == WordClass::X87UP || newClass == WordClass::COMPLEX_X87) {
        *wordi = WordClass::MEMORY;
    } else {
        *wordi = newClass;
    }
}

static inline size_t AsBits(size_t bytes)
{
    return bytes * BITS_PER_BYTE;
}

std::vector<WordClass> LinuxAmd64CJNativeCGCFFI::ClassifyType(llvm::Type& type) const
{
    // Eight bytes are a word.
    auto wordSize = (GetTypeSize(cgMod, type) + BYTES_PER_WORD - 1) / BYTES_PER_WORD;
    std::vector<WordClass> wordClasses(wordSize, WordClass::NO_CLASS);

    if (wordSize > 4) { // 4 is the upper limit of word size of a small struct.
        FillWithMemory(wordClasses);
        return wordClasses;
    }
    ClassifyType(type, wordClasses.begin(), 0);
    FixupClassification(type, wordClasses);
    return wordClasses;
}

void LinuxAmd64CJNativeCGCFFI::ClassifyType(
    llvm::Type& type, const std::vector<WordClass>::iterator begin, size_t offset) const
{
    size_t misAlign = offset % GetTypeAlignment(cgMod, type);
    if (misAlign != 0) {
        size_t i = offset / BYTES_PER_WORD;
        for (; i < (offset + GetTypeSize(cgMod, type) + BYTES_PER_WORD - 1) / BYTES_PER_WORD; i++) {
            auto newBegin = begin + static_cast<long int>(i);
            UnifyWordClass(newBegin, WordClass::MEMORY);
        }
        return;
    }

    auto wordOffset = offset / BYTES_PER_WORD;
    if (type.isIntOrPtrTy()) {
        UnifyWordClass(begin + static_cast<std::ptrdiff_t>(wordOffset), WordClass::INTEGER);
    } else if (type.isFloatingPointTy()) {
        uint64_t bits = type.getPrimitiveSizeInBits();
        if (bits == 16 || bits == 32 || bits == 64) { // Float16, Float32, Float64 in Cangjie.
            UnifyWordClass(begin + static_cast<std::ptrdiff_t>(wordOffset), WordClass::SSE);
        } else if (bits == 80) { // 80 bits means long double in C, there is no mapping type in Cangjie.
            UnifyWordClass(begin + static_cast<std::ptrdiff_t>(wordOffset), WordClass::X87);
            UnifyWordClass(begin + static_cast<std::ptrdiff_t>(wordOffset + 1), WordClass::X87UP);
        } else {
            CJC_ASSERT(false && "Bits in FloatingPointTy are wrong.");
        }
    } else if (type.isStructTy()) {
        auto structType = llvm::cast<llvm::StructType>(&type);
        auto fieldTypes = structType->elements();
        ClassifyStructType(fieldTypes, begin, offset);
    } else if (type.isArrayTy()) {
        auto arrayType = llvm::cast<llvm::ArrayType>(&type);
        auto elemNum = arrayType->getNumElements();
        auto elemType = arrayType->getElementType();
        for (uint64_t idx = 0; idx < elemNum; ++idx) {
            ClassifyType(*elemType, begin, offset);
            offset += GetTypeSize(cgMod, *elemType);
        }
    } else {
        CJC_ASSERT(false && "Unsupported type.");
    }
}

void LinuxAmd64CJNativeCGCFFI::ClassifyStructType(
    llvm::ArrayRef<llvm::Type*> fields, const std::vector<WordClass>::iterator begin, size_t offset) const
{
    if (fields.empty()) {
        // CONSIDER: When this is a empty struct, there is a special process in clay's algorithm. So why?
        // Here is the code: ClassifyType(llvm::Type::getInt8Ty(llvmCtx), begin, offset);
        return;
    }
    size_t fieldOffset = offset;
    for (auto field : fields) {
        fieldOffset = AlignedUpTo(cgMod, fieldOffset, *field);
        ClassifyType(*field, begin, fieldOffset);
        fieldOffset += GetTypeSize(cgMod, *field);
    }
}

void LinuxAmd64CJNativeCGCFFI::FixupClassification(const llvm::Type& type, std::vector<WordClass>& wordClasses) const
{
    auto it = wordClasses.begin();
    auto end = wordClasses.end();
    if (wordClasses.size() > MAX_SPLIT_NUM && type.isStructTy()) {
        if (*it != WordClass::SSE) {
            FillWithMemory(wordClasses);
            return;
        }
        ++it;
        for (; it != end; ++it) {
            if (*it != WordClass::SSEUP) {
                FillWithMemory(wordClasses);
                return;
            }
        }
        return;
    }
    while (it != end) {
        if (*it == WordClass::MEMORY || *it == WordClass::X87UP) {
            FillWithMemory(wordClasses);
            return;
        }
        if (*it == WordClass::SSE) {
            do {
                ++it;
            } while ((it != end) && (*it == WordClass::SSEUP));
        } else if (*it == WordClass::X87) {
            do {
                ++it;
            } while ((it != end) && (*it == WordClass::X87UP));
        } else {
            ++it;
        }
    }
}

// All offset's unit is byte.
llvm::Type* LinuxAmd64CJNativeCGCFFI::GetIntegerTypeWithOffset(
    llvm::Type& curType, size_t offset, llvm::Type& srcType, size_t srcOffset) const
{
    CJC_ASSERT(srcOffset == 0 || srcOffset == BYTES_PER_WORD);
    if (offset == 0) {
        auto typeSize = GetTypeSize(cgMod, curType);
        if (curType.isIntOrPtrTy() && typeSize == BYTES_PER_WORD) {
            return &curType;
        }

        if (curType.isIntOrPtrTy() &&
            (typeSize == 1 || typeSize == 2 || typeSize == 4) && // 1, 2, 4 means (U)Int8, (U)Int16, (U)Int32.
            ContainsUselessBits(srcType, AsBits(srcOffset + typeSize), AsBits(srcOffset + BYTES_PER_WORD))) {
            return &curType;
        }
    }

    if (curType.isStructTy() && offset < GetTypeSize(cgMod, curType)) {
        auto structType = llvm::cast<llvm::StructType>(&curType);
        auto fieldOffsets = CalcFieldOffsets(*structType);
        auto fieldIdx = GetFieldContainsOffset(fieldOffsets, offset);
        auto relOffset = offset - fieldOffsets[fieldIdx];
        return GetIntegerTypeWithOffset(*structType->getElementType(fieldIdx), relOffset, srcType, srcOffset);
    }

    auto typeSize = GetTypeSize(cgMod, srcType);
    CJC_ASSERT(typeSize != srcOffset);
    auto intSize = std::min<size_t>(typeSize - srcOffset, BYTES_PER_WORD);
    return llvm::IntegerType::get(ctx.GetLLVMContext(), static_cast<unsigned int>(AsBits(intSize)));
}

llvm::Type* LinuxAmd64CJNativeCGCFFI::GetSseTypeWithOffset(
    llvm::Type& curType, size_t offset, llvm::Type& srcType, size_t srcOffset) const
{
    CJC_ASSERT(srcOffset == 0 || srcOffset == BYTES_PER_WORD);

    auto& llvmCtx = ctx.GetLLVMContext();
    const size_t bytesOfHalfWord = 4;
    if (ContainsUselessBits(srcType, AsBits(srcOffset + bytesOfHalfWord), AsBits(srcOffset + BYTES_PER_WORD))) {
        return llvm::Type::getFloatTy(llvmCtx);
    }
    if (ContainsFloatAtOffset(curType, offset) && ContainsFloatAtOffset(curType, offset + bytesOfHalfWord)) {
        return llvm::VectorType::get(llvm::Type::getFloatTy(llvmCtx), 2, false); // { float, float } -> <2 x float>
    }
    return llvm::Type::getDoubleTy(llvmCtx);
}

// Unit of start and end is bits.
bool LinuxAmd64CJNativeCGCFFI::ContainsUselessBits(llvm::Type& type, size_t start, size_t end) const
{
    CJC_ASSERT(start <= end);
    if (AsBits(GetTypeSize(cgMod, type)) <= start) {
        return true;
    }

    if (type.isStructTy()) {
        auto structType = llvm::cast<llvm::StructType>(&type);
        auto fieldOffsets = CalcFieldOffsets(*structType);
        auto fields = structType->elements();
        for (size_t i = 0; i < fields.size(); i++) {
            auto field = fields[i];
            auto fieldOffset = fieldOffsets[i];
            auto fieldOffsetBits = AsBits(fieldOffset);
            if (fieldOffsetBits >= end) {
                break;
            }
            auto fieldStart = fieldOffsetBits < start ? start - fieldOffsetBits : 0;
            if (!ContainsUselessBits(*field, fieldStart, end - fieldOffsetBits)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

LinuxAmd64CJNativeCGCFFI::FieldOffsets LinuxAmd64CJNativeCGCFFI::CalcFieldOffsets(const llvm::StructType& type) const
{
    FieldOffsets offsets;
    auto fields = type.elements();
    offsets.reserve(fields.size());

    size_t offset = 0;
    for (auto field : fields) {
        offset = AlignedUpTo(cgMod, offset, *field);
        offsets.emplace_back(offset);
        offset += GetTypeSize(cgMod, *field);
    }
    return offsets;
}

unsigned LinuxAmd64CJNativeCGCFFI::GetFieldContainsOffset(FieldOffsets fieldOffsets, size_t offset) const
{
    size_t idx = 0;
    for (; idx + 1 < fieldOffsets.size(); idx++) {
        if (fieldOffsets[idx] <= offset && fieldOffsets[idx + 1] > offset) {
            break;
        }
    }
    return static_cast<unsigned>(idx);
}

bool LinuxAmd64CJNativeCGCFFI::ContainsFloatAtOffset(llvm::Type& type, size_t offset) const
{
    if (offset == 0 && type.isFloatTy()) {
        return true;
    }
    if (type.isStructTy()) {
        auto structType = llvm::cast<llvm::StructType>(&type);
        auto fieldOffsets = CalcFieldOffsets(*structType);
        auto fieldIdx = GetFieldContainsOffset(fieldOffsets, offset);
        auto relOffset = offset - fieldOffsets[fieldIdx];
        return ContainsFloatAtOffset(*structType->getElementType(fieldIdx), relOffset);
    }
    return false;
}

llvm::FunctionType* LinuxAarch64CJNativeCGCFFI::GetCFuncType(const CHIR::FuncType& chirFuncTy)
{
    auto found = cfuncMap.find(&chirFuncTy);
    if (found != cfuncMap.end()) {
        return found->second.first;
    }
    CJC_ASSERT(chirFuncTy.IsCFunc());

    std::vector<llvm::Type*> paramTys;
    auto chirParamTys = chirFuncTy.GetParamTypes();
    paramTys.reserve(chirParamTys.size());
    std::vector<ProcessKind> kinds;
    kinds.reserve(chirParamTys.size());
    auto retType = GetReturnType(*chirFuncTy.GetReturnType(), paramTys);
    if (retType == nullptr) {
        return nullptr;
    }
    for (auto ty : chirParamTys) {
        CJC_ASSERT(IsMetCType(*ty));
        if (IsUnsizedStructTy(*ty)) {
            return nullptr;
        }
        auto typeSize = GetTypeSize(cgMod, *ty);
        if (IsUnitOrNothing(*ty) || typeSize == 0) {
            kinds.emplace_back(ProcessKind::SKIP);
            continue;
        }
        kinds.emplace_back(GetParamType(*ty, paramTys));
    }
    auto resTy = llvm::FunctionType::get(retType, paramTys, chirFuncTy.HasVarArg());
    cfuncMap.emplace(&chirFuncTy, std::make_pair(resTy, kinds));
    return resTy;
}

void LinuxAarch64CJNativeCGCFFI::AddFunctionAttr(const CHIR::FuncType& chirFuncTy, llvm::Function& llvmFunc)
{
    CJC_ASSERT(chirFuncTy.IsCFunc());
    auto found = cfuncMap.find(&chirFuncTy);
    if (found == cfuncMap.end()) {
        return;
    }
    auto retTy = chirFuncTy.GetReturnType();
    auto found1 = typeMap.find(retTy);
    if (found1 != typeMap.end() && found1->second.IsIndirect()) {
        AddSRetAttribute(llvmFunc.arg_begin());
    }
    AddFnAttr(&llvmFunc, llvm::Attribute::get(llvmFunc.getContext(), CodeGen::CFUNC_ATTR));
}

void LinuxAarch64CJNativeCGCFFI::ProcessParam(
    CHIR::Type& chirParamTy, LLVMFuncArgIt& arg, llvm::Value* place, IRBuilder2& builder)
{
    if (!chirParamTy.IsStruct()) {
        return;
    }
    auto found = typeMap.find(&chirParamTy);
    if (found == typeMap.end()) {
        return;
    }
    auto& argInfo = found->second;
    if (argInfo.IsDirect()) {
        auto argType = arg->getType();
        llvm::Value* argVal = arg;

        auto alloca = builder.CreateEntryAlloca(argType);
        builder.CreateStore(argVal, alloca);

        size_t actualSize = GetTypeSize(cgMod, chirParamTy);
        builder.CreateMemCpy(place, GetAlign(cgMod, chirParamTy), alloca, GetAlign(cgMod, *argType), actualSize);
    }
    CJC_ASSERT(!argInfo.IsExpand() && "ArgInfo in aarch64 cannot be Expand.");
}

void LinuxAarch64CJNativeCGCFFI::ProcessInvocationArg(
    CHIR::StructType& chirParamTy, ProcessKind kind, size_t& argIdx, std::vector<CGValue*>& args, IRBuilder2& builder)
{
    if (kind == ProcessKind::INDIRECT) {
        return;
    }
    if (kind != ProcessKind::DIRECT) {
        CJC_ASSERT(false && "ArgInfo in aarch64 cannot be Expand.");
        return;
    }
    auto found = typeMap.find(&chirParamTy);
    CJC_ASSERT(found != typeMap.end());
    auto& info = found->second;
    CJC_ASSERT(info.IsDirect());
    CJC_ASSERT(argIdx < args.size());
    auto arg = args[argIdx];
    size_t actualSize = GetTypeSize(cgMod, chirParamTy);
    if (info[0]->isIntegerTy()) {
        auto numBits = static_cast<unsigned>(AsBits(actualSize));
        auto actualIntTy = llvm::IntegerType::get(ctx.GetLLVMContext(), numBits);
        const unsigned int addrSpace = arg->GetRawValue()->getType()->getPointerAddressSpace();
        auto tmpVal = builder.CreateBitCast(arg->GetRawValue(), actualIntTy->getPointerTo(addrSpace));
        tmpVal = builder.LLVMIRBuilder2::CreateLoad(actualIntTy, tmpVal);
        if (GetTypeSize(cgMod, *info[0]) != actualSize) {
            tmpVal = builder.CreateIntCast(tmpVal, info[0], false);
        }
        args[argIdx] = cgMod.CreateGhostCFuncArgValue(*tmpVal, *GetCGType(cgMod, *info[0]));
    } else {
        CJC_ASSERT(info[0]->isArrayTy());
        auto tmpVal = builder.CreateEntryAlloca(info[0], nullptr, "arg.copy");
        builder.CreateMemCpy(
            tmpVal, GetAlign(cgMod, *info[0]), arg->GetRawValue(), GetAlign(cgMod, chirParamTy), actualSize);

        args[argIdx] = cgMod.CreateGhostCFuncArgValue(
            *builder.LLVMIRBuilder2::CreateLoad(info[0], tmpVal), *GetCGType(cgMod, *info[0]));
    }
}

llvm::Value* LinuxAarch64CJNativeCGCFFI::ProcessRetValue(
    const llvm::Type& retType, llvm::Value& val, IRBuilder2& builder)
{
    CJC_ASSERT(!retType.isVoidTy());
    llvm::Value* castRes = &val;
    if (!retType.isStructTy() && IsStructPtrTy(val.getType())) {
        castRes = builder.CreateBitCast(&val, retType.getPointerTo());
    }
    return builder.LLVMIRBuilder2::CreateLoad(const_cast<llvm::Type*>(&retType), castRes);
}

llvm::Type* LinuxAarch64CJNativeCGCFFI::GetStructReturnType(CHIR::StructType& chirTy, std::vector<llvm::Type*>& params)
{
    auto& llvmCtx = ctx.GetLLVMContext();
    if (GetTypeSize(cgMod, chirTy) == 0) {
        return llvm::Type::getVoidTy(llvmCtx);
    }
    auto argInfo = GetMappingArgInfo(chirTy, false);
    if (argInfo.IsIndirect()) {
        auto structType = GetLLVMType(chirTy);
        CJC_ASSERT(structType->isStructTy());
        params.emplace_back(structType->getPointerTo());
        return llvm::Type::getVoidTy(llvmCtx);
    }
    CJC_ASSERT(argInfo.IsDirect() && "ArgInfo in aarch64 cannot be Expand.");
    return argInfo[0];
}

ProcessKind LinuxAarch64CJNativeCGCFFI::GetParamType(CHIR::Type& chirTy, std::vector<llvm::Type*>& params)
{
    if (chirTy.IsStruct()) {
        auto info = GetMappingArgInfo(StaticCast<CHIR::StructType&>(chirTy), true);
        auto structType = GetLLVMType(chirTy);
        CJC_ASSERT(structType->isStructTy());
        if (info.IsIndirect()) {
            params.emplace_back(structType->getPointerTo());
            return ProcessKind::INDIRECT;
        } else {
            CJC_ASSERT(info.IsDirect() && "ArgInfo in aarch64 cannot be Expand.");
            params.emplace_back(info[0]);
            return ProcessKind::DIRECT;
        }
    }
    llvm::Type* paramType = GetLLVMType(chirTy);
    // VArray as CFunc parament pass by a i8*.
    if (chirTy.IsVArray()) {
        paramType = llvm::Type::getInt8PtrTy(ctx.GetLLVMContext());
    }
    params.emplace_back(paramType);
    return ProcessKind::NO_PROCESS;
}

ABIArgInfo LinuxAarch64CJNativeCGCFFI::GetMappingArgInfo(CHIR::StructType& chirTy, [[maybe_unused]] bool isArg)
{
    auto found = typeMap.find(&chirTy);
    if (found != typeMap.end()) {
        return found->second;
    }
    auto type = GetLLVMType(chirTy);
    size_t size = GetTypeSize(cgMod, *type);
    llvm::Type* base = nullptr;
    if (size_t members = 0; IsHomogeneousAggregate(*type, base, members)) {
        return typeMap.emplace(&chirTy, ABIArgInfo::GetDirect(llvm::ArrayType::get(base, members))).first->second;
    }

    const size_t limitSizeOfSmallStruct = 16;
    if (size > limitSizeOfSmallStruct) {
        return typeMap.emplace(&chirTy, ABIArgInfo::GetIndirect()).first->second;
    }

    size_t alignment = GetTypeAlignment(cgMod, *type);
    size = llvm::alignTo(size, BYTES_PER_WORD);
    auto& llvmCtx = ctx.GetLLVMContext();
    if (alignment < limitSizeOfSmallStruct && size == limitSizeOfSmallStruct) {
        llvm::Type* baseTy = llvm::Type::getInt64Ty(llvmCtx);
        llvm::Type* resTy = llvm::ArrayType::get(baseTy, static_cast<uint64_t>(size) / BYTES_PER_WORD);
        return typeMap.emplace(&chirTy, ABIArgInfo::GetDirect(resTy)).first->second;
    }
    llvm::Type* resTy = llvm::IntegerType::get(llvmCtx, static_cast<unsigned>(size) * BITS_PER_BYTE);
    return typeMap.emplace(&chirTy, ABIArgInfo::GetDirect(resTy)).first->second;
}

bool LinuxAarch64CJNativeCGCFFI::IsHomogeneousAggregate(llvm::Type& type, llvm::Type*& base, size_t& members)
{
    if (auto st = llvm::dyn_cast<llvm::StructType>(&type)) {
        members = 0;
        for (auto fdTy : st->elements()) {
            if (GetTypeSize(cgMod, *fdTy) == 0) {
                continue;
            }
            size_t fdMembers = 0;
            if (!IsHomogeneousAggregate(*fdTy, base, fdMembers)) {
                return false;
            }
            members += fdMembers;
        }
        if (base == nullptr) {
            return false;
        }
        if (GetTypeSize(cgMod, *base) * members != GetTypeSize(cgMod, type)) {
            return false;
        }
    } else {
        members = 1;
        if (!type.isFloatingPointTy()) {
            return false;
        }
        if (base == nullptr) {
            base = &type;
        }
    }
    return members > 0 && members <= 4; // 4 is maximum number of homogeneous aggregate's members.
}
#ifdef __APPLE__
void MacAArch64CJNativeCGCFFI::AddFunctionAttr(const CHIR::FuncType& chirFuncTy, llvm::Function& llvmFunc)
{
    LinuxAarch64CJNativeCGCFFI::AddFunctionAttr(chirFuncTy, llvmFunc);
    for (auto& arg : llvmFunc.args()) {
        AddAttrForIntTypeArgOnMacMx(llvmFunc, arg);
    }
    if (auto intRetType = llvm::dyn_cast<llvm::IntegerType>(llvmFunc.getReturnType());
        intRetType && chirFuncTy.GetReturnType()->IsPrimitive()) {
        if (intRetType->isIntegerTy(1u)) { // 1u means bool type.
            AddRetAttr(&llvmFunc, llvm::Attribute::ZExt);
        } else if (intRetType->getBitWidth() < 32) { // 32 means int32_t type.
            AddRetAttr(&llvmFunc, llvm::Attribute::SExt);
        }
    }
}

llvm::Type* MacAArch64CJNativeCGCFFI::GetStructReturnType(CHIR::StructType& chirTy, std::vector<llvm::Type*>& params)
{
    auto type = GetLLVMType(chirTy);
    size_t size = GetTypeSize(cgMod, *type);
    auto& llvmCtx = ctx.GetLLVMContext();
    if (size == 0) {
        return llvm::Type::getVoidTy(llvmCtx);
    }
    llvm::Type* base = nullptr;
    if (size_t members = 0; IsHomogeneousAggregate(*type, base, members)) {
        return type;
    }
    if (size < BYTES_PER_WORD) {
        return llvm::IntegerType::get(llvmCtx, static_cast<unsigned>(size) * BITS_PER_BYTE);
    }
    return LinuxAarch64CJNativeCGCFFI::GetStructReturnType(chirTy, params);
}
#endif
llvm::FunctionType* WindowsAmd64CJNativeCGCFFI::GetCFuncType(const CHIR::FuncType& chirFuncTy)
{
    if (auto found = cfuncMap.find(&chirFuncTy); found != cfuncMap.end()) {
        return found->second.first;
    }
    CJC_ASSERT(chirFuncTy.IsCFunc());

    std::vector<llvm::Type*> paramTypes;
    auto chirParamTypes = chirFuncTy.GetParamTypes();
    paramTypes.reserve(chirParamTypes.size());
    std::vector<ProcessKind> kinds;
    kinds.reserve(chirParamTypes.size());
    auto retTy = GetReturnType(*chirFuncTy.GetReturnType(), paramTypes);
    if (retTy == nullptr) {
        return nullptr;
    }
    for (auto paramTy : chirParamTypes) {
        CJC_ASSERT(IsMetCType(*paramTy));
        if (IsUnsizedStructTy(*paramTy)) {
            return nullptr;
        }
        CJC_ASSERT(!IsUnitOrNothing(*paramTy));
        kinds.emplace_back(GetParamType(*paramTy, paramTypes));
    }
    auto resTy = llvm::FunctionType::get(retTy, paramTypes, chirFuncTy.HasVarArg());
    cfuncMap.emplace(&chirFuncTy, std::make_pair(resTy, kinds));
    return resTy;
}

void WindowsAmd64CJNativeCGCFFI::AddFunctionAttr(const CHIR::FuncType& chirFuncTy, llvm::Function& llvmFunc)
{
    CJC_ASSERT(chirFuncTy.IsCFunc());
    if (cfuncMap.find(&chirFuncTy) == cfuncMap.end()) {
        return;
    }
    auto retTy = chirFuncTy.GetReturnType();
    if (retTy->IsBoolean()) {
        AddRetAttr(&llvmFunc, llvm::Attribute::ZExt);
    } else if (auto found = typeMap.find(retTy); found != typeMap.end() && found->second.IsIndirect()) {
        auto sretArg = llvmFunc.arg_begin();
        AddSRetAttribute(sretArg);
        sretArg->addAttr(llvm::Attribute::NoAlias);
        auto align = llvm::Align(GetTypeAlignment(cgMod, *GetPointerElementType(sretArg->getType())));
        auto alignAttr = llvm::Attribute::getWithAlignment(llvmFunc.getContext(), align);
        sretArg->addAttr(alignAttr);
    }
    for (const auto& param : llvmFunc.args()) {
        if (param.getType()->isIntegerTy(1)) {
            AddParamAttr(&llvmFunc, param.getArgNo(), llvm::Attribute::ZExt);
        }
    }
    AddFnAttr(&llvmFunc, llvm::Attribute::get(llvmFunc.getContext(), CodeGen::CFUNC_ATTR));
}

void WindowsAmd64CJNativeCGCFFI::ProcessParam(
    CHIR::Type& chirParamTy, LLVMFuncArgIt& arg, llvm::Value* place, IRBuilder2& builder)
{
    if (!chirParamTy.IsStruct()) {
        return;
    }
    auto found = typeMap.find(&chirParamTy);
    if (found == typeMap.end()) {
        return;
    }
    ABIArgInfo& argInfo = found->second;
    if (argInfo.IsDirect()) {
        auto argType = arg->getType();
        llvm::Value* argVal = arg;

        auto alloca = builder.CreateEntryAlloca(argType);
        builder.CreateStore(argVal, alloca);
        builder.CreateMemCpy(
            place, GetAlign(cgMod, chirParamTy), alloca, GetAlign(cgMod, *argType), GetTypeSize(cgMod, chirParamTy));
    }
    CJC_ASSERT(!argInfo.IsExpand() && "ArgInfo in Windows x64 cannot be Expand.");
}

llvm::Value* WindowsAmd64CJNativeCGCFFI::ProcessRetValue(
    const llvm::Type& retType, llvm::Value& val, IRBuilder2& builder)
{
    CJC_ASSERT(!retType.isVoidTy());
    llvm::Value* castRes = &val;
    if (!retType.isStructTy() && IsStructPtrTy(val.getType())) {
        castRes = builder.CreateBitCast(&val, retType.getPointerTo());
    }
    return builder.LLVMIRBuilder2::CreateLoad(const_cast<llvm::Type*>(&retType), castRes);
}

void WindowsAmd64CJNativeCGCFFI::ProcessInvocationArg(
    CHIR::StructType& chirParamTy, ProcessKind kind, size_t& argIdx, std::vector<CGValue*>& args, IRBuilder2& builder)
{
    auto found = typeMap.find(&chirParamTy);
    CJC_ASSERT(found != typeMap.end());
    auto& info = found->second;
    if (kind == ProcessKind::DIRECT) {
        CJC_ASSERT(info.IsDirect());
        CJC_ASSERT(argIdx < args.size());
        const unsigned int addrSpace = args[argIdx]->GetRawValue()->getType()->getPointerAddressSpace();
        llvm::Value* arg = builder.CreateBitCast(args[argIdx]->GetRawValue(), info[0]->getPointerTo(addrSpace));
        args[argIdx] = cgMod.CreateGhostCFuncArgValue(
            *builder.LLVMIRBuilder2::CreateLoad(info[0], arg), *GetCGType(cgMod, *info[0]));
    } else if (kind == ProcessKind::INDIRECT) {
        CJC_ASSERT(info.IsIndirect());
        CJC_ASSERT(argIdx < args.size());
        auto& arg = args[argIdx];
        auto copy = builder.CreateEntryAlloca(*arg->GetCGType()->GetPointerElementType(), "arg.copy");
        llvm::MaybeAlign align(GetTypeAlignment(cgMod, *const_cast<llvm::Type*>(arg->GetLLVMType())));
        builder.CreateMemCpy(copy, align, arg->GetRawValue(), align, GetTypeSize(cgMod, chirParamTy));
        arg = cgMod.CreateGhostCFuncArgValue(*copy, *arg->GetCGType());
    } else {
        CJC_ASSERT(false && "ArgInfo in Windows x64 cannot be Expand.");
    }
}

llvm::Type* WindowsAmd64CJNativeCGCFFI::GetStructReturnType(CHIR::StructType& chirTy, std::vector<llvm::Type*>& params)
{
    auto& llvmCtx = ctx.GetLLVMContext();
    auto argInfo = GetMappingArgInfo(chirTy, false);
    if (!argInfo.IsIndirect()) {
        CJC_ASSERT(argInfo.IsDirect() && "ArgInfo in Windows x64 cannot be Expand.");
        return argInfo[0];
    }
    auto structType = GetLLVMType(chirTy);
    CJC_ASSERT(structType->isStructTy());
    params.emplace_back(structType->getPointerTo());
    return llvm::Type::getVoidTy(llvmCtx);
}

ProcessKind WindowsAmd64CJNativeCGCFFI::GetParamType(CHIR::Type& chirTy, std::vector<llvm::Type*>& params)
{
    if (chirTy.IsStruct()) {
        ABIArgInfo info = GetMappingArgInfo(StaticCast<CHIR::StructType&>(chirTy), true);
        llvm::Type* structType = GetLLVMType(chirTy);
        CJC_ASSERT(structType->isStructTy());
        if (info.IsIndirect()) {
            params.emplace_back(structType->getPointerTo());
            return ProcessKind::INDIRECT;
        }
        CJC_ASSERT(info.IsDirect() && "ArgInfo in Windows x64 cannot be Expand.");
        params.emplace_back(info[0]);
        return ProcessKind::DIRECT;
    }
    llvm::Type* paramType = GetLLVMType(chirTy);
    // VArray as CFunc parament pass by a i8*.
    if (chirTy.IsVArray()) {
        paramType = llvm::Type::getInt8PtrTy(ctx.GetLLVMContext());
    }
    params.emplace_back(paramType);
    return ProcessKind::NO_PROCESS;
}

ABIArgInfo WindowsAmd64CJNativeCGCFFI::GetMappingArgInfo(CHIR::StructType& chirTy, [[maybe_unused]] bool isArg)
{
    if (auto found = typeMap.find(&chirTy); found != typeMap.end()) {
        return found->second;
    }
    auto type = GetLLVMType(chirTy);
    size_t size = GetTypeSize(cgMod, *type);
    const size_t limitSizeOfSmallStruct = 8;
    if (size > limitSizeOfSmallStruct || Utils::NotIn(size, {1, 2, 4, 8})) {
        return typeMap.emplace(&chirTy, ABIArgInfo::GetIndirect()).first->second;
    }

    llvm::Type* resTy = llvm::IntegerType::get(ctx.GetLLVMContext(), static_cast<unsigned>(AsBits(size)));
    return typeMap.emplace(&chirTy, ABIArgInfo::GetDirect(resTy)).first->second;
}

llvm::FunctionType* LinuxOhosArm32CJNativeCGCFFI::GetCFuncType(const CHIR::FuncType& chirFuncTy)
{
    auto found = cfuncMap.find(&chirFuncTy);
    if (found != cfuncMap.end()) {
        return found->second.first;
    }
    CJC_ASSERT(chirFuncTy.IsCFunc());

    std::vector<llvm::Type*> paramTys;
    auto chirParamTys = chirFuncTy.GetParamTypes();
    paramTys.reserve(chirParamTys.size());
    std::vector<ProcessKind> kinds;
    kinds.reserve(chirParamTys.size());
    auto retType = GetReturnType(*chirFuncTy.GetReturnType(), paramTys);
    if (retType == nullptr) {
        return nullptr;
    }
    for (auto ty : chirParamTys) {
        CJC_ASSERT(IsMetCType(*ty));
        if (IsUnsizedStructTy(*ty)) {
            return nullptr;
        }
        auto typeSize = GetTypeSize(cgMod, *ty);
        if (IsUnitOrNothing(*ty) || typeSize == 0) {
            kinds.emplace_back(ProcessKind::SKIP);
            continue;
        }
        kinds.emplace_back(GetParamType(*ty, paramTys));
    }
    auto resTy = llvm::FunctionType::get(retType, paramTys, chirFuncTy.HasVarArg());
    cfuncMap.emplace(&chirFuncTy, std::make_pair(resTy, kinds));
    return resTy;
}

void LinuxOhosArm32CJNativeCGCFFI::ProcessParam(
    CHIR::Type& chirParamTy, LLVMFuncArgIt& arg, llvm::Value* place, IRBuilder2& builder)
{
    if (!chirParamTy.IsStruct()) {
        return;
    }
    auto found = paramTypeMap.find(&chirParamTy);
    if (found == paramTypeMap.end()) {
        return;
    }
    auto& argInfo = found->second;
    if (argInfo.IsDirect()) {
        auto argType = arg->getType();
        llvm::Value* argVal = arg;

        auto alloca = builder.CreateEntryAlloca(argType);
        builder.CreateStore(argVal, alloca);

        size_t actualSize = GetTypeSize(cgMod, chirParamTy);
        builder.CreateMemCpy(place, GetAlign(cgMod, chirParamTy), alloca, GetAlign(cgMod, *argType), actualSize);
    }
    CJC_ASSERT(!argInfo.IsExpand() && "ArgInfo in arm32 cannot be Expand.");
}

void LinuxOhosArm32CJNativeCGCFFI::ProcessInvocationArg(
    CHIR::StructType& chirParamTy, ProcessKind kind, size_t& argIdx, std::vector<CGValue*>& args, IRBuilder2& builder)
{
    if (kind == ProcessKind::INDIRECT) {
        return;
    }
    if (kind != ProcessKind::DIRECT) {
        CJC_ASSERT(false && "ArgInfo in arm32 cannot be Expand.");
        return;
    }
    auto found = paramTypeMap.find(&chirParamTy);
    CJC_ASSERT(found != paramTypeMap.end());
    auto& info = found->second;
    CJC_ASSERT(info.IsDirect());
    CJC_ASSERT(argIdx < args.size());
    auto arg = args[argIdx];
    size_t actualSize = GetTypeSize(cgMod, chirParamTy);
    if (info[0]->isIntegerTy()) {
        auto numBits = static_cast<unsigned>(AsBits(actualSize));
        auto actualIntTy = llvm::IntegerType::get(ctx.GetLLVMContext(), numBits);
        const unsigned int addrSpace = arg->GetRawValue()->getType()->getPointerAddressSpace();
        auto tmpVal = builder.CreateBitCast(arg->GetRawValue(), actualIntTy->getPointerTo(addrSpace));
        tmpVal = builder.LLVMIRBuilder2::CreateLoad(actualIntTy, tmpVal);
        if (GetTypeSize(cgMod, *info[0]) != actualSize) {
            tmpVal = builder.CreateIntCast(tmpVal, info[0], false);
        }
        args[argIdx] = cgMod.CreateGhostCFuncArgValue(*tmpVal, *GetCGType(cgMod, *info[0]));
    } else {
        CJC_ASSERT(info[0]->isArrayTy());
        auto tmpVal = builder.CreateEntryAlloca(info[0], nullptr, "arg.copy");
        builder.CreateMemCpy(
            tmpVal, GetAlign(cgMod, *info[0]), arg->GetRawValue(), GetAlign(cgMod, chirParamTy), actualSize);

        args[argIdx] = cgMod.CreateGhostCFuncArgValue(
            *builder.LLVMIRBuilder2::CreateLoad(info[0], tmpVal), *GetCGType(cgMod, *info[0]));
    }
}

llvm::Type* LinuxOhosArm32CJNativeCGCFFI::GetStructReturnType(CHIR::StructType& chirTy, std::vector<llvm::Type*>& params)
{
    auto& llvmCtx = ctx.GetLLVMContext();
    if (GetTypeSize(cgMod, chirTy) == 0) {
        return llvm::Type::getVoidTy(llvmCtx);
    }
    auto argInfo = GetMappingArgInfo(chirTy, false);
    if (argInfo.IsIndirect()) {
        auto structType = GetLLVMType(chirTy);
        CJC_ASSERT(structType->isStructTy());
        params.emplace_back(structType->getPointerTo());
        return llvm::Type::getVoidTy(llvmCtx);
    }
    CJC_ASSERT(argInfo.IsDirect() && "ArgInfo in arm32 cannot be Expand.");
    return argInfo[0];
}

ProcessKind LinuxOhosArm32CJNativeCGCFFI::GetParamType(CHIR::Type& chirTy, std::vector<llvm::Type*>& params)
{
    if (chirTy.IsStruct()) {
        auto info = GetMappingArgInfo(StaticCast<CHIR::StructType&>(chirTy), true);
        auto structType = GetLLVMType(chirTy);
        CJC_ASSERT(structType->isStructTy());
        if (info.IsIndirect()) {
            params.emplace_back(structType->getPointerTo());
            return ProcessKind::INDIRECT;
        } else {
            CJC_ASSERT(info.IsDirect() && "ArgInfo in arm32 cannot be Expand.");
            params.emplace_back(info[0]);
            return ProcessKind::DIRECT;
        }
    }
    llvm::Type* paramType = GetLLVMType(chirTy);
    // VArray as CFunc parament pass by a i8*.
    if (chirTy.IsVArray()) {
        paramType = llvm::Type::getInt8PtrTy(ctx.GetLLVMContext());
    }
    params.emplace_back(paramType);
    return ProcessKind::NO_PROCESS;
}

ABIArgInfo LinuxOhosArm32CJNativeCGCFFI::GetMappingArgInfo(CHIR::StructType& chirTy, bool isArg)
{
    if (isArg) {
        auto found = paramTypeMap.find(&chirTy);
        if (found != paramTypeMap.end()) {
            return found->second;
        }
    } else {
        auto found = typeMap.find(&chirTy);
        if (found != typeMap.end()) {
            return found->second;
        }
    }
    auto type = GetLLVMType(chirTy);
    size_t size = GetTypeSize(cgMod, *type);
    auto& llvmCtx = ctx.GetLLVMContext();

    if (isArg) {
        size_t alignment = GetTypeAlignment(cgMod, *type);
        llvm::Type* baseTy = nullptr;
        if (alignment <= 4) {
            alignment = 4;
            baseTy = llvm::Type::getInt32Ty(llvmCtx);
        } else if (alignment >= 8) {
            alignment = 8;
            baseTy = llvm::Type::getInt64Ty(llvmCtx);
        } else {
            CJC_ASSERT(true && "Alignment should not be between 4 and 8 bytes.");
        }
        size = llvm::alignTo(size, alignment);
        llvm::Type* resTy = llvm::ArrayType::get(baseTy, static_cast<uint64_t>(size) / alignment);
        return paramTypeMap.emplace(&chirTy, ABIArgInfo::GetDirect(resTy)).first->second;
    }

    const size_t limitSizeOfSmallStruct = 4;
    if (size > limitSizeOfSmallStruct) {
        return typeMap.emplace(&chirTy, ABIArgInfo::GetIndirect()).first->second;
    } else {
        llvm::Type* resTy = llvm::Type::getInt32Ty(llvmCtx);
        return typeMap.emplace(&chirTy, ABIArgInfo::GetDirect(resTy)).first->second;
    }
}
