// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements BCHIR representation.
 */

#include <functional>

#include "cangjie/CHIR/Interpreter/BCHIR.h"

using namespace Cangjie::CHIR::Interpreter;

void Bchir::Definition::Push(OpCode opcode)
{
    CJC_ASSERT(bytecode.size() < BYTECODE_CONTENT_MAX);
    (void)bytecode.emplace_back(static_cast<Bchir::ByteCodeContent>(opcode));
}

void Bchir::Definition::Push(Bchir::ByteCodeContent value)
{
    (void)bytecode.emplace_back(value);
}

void Bchir::Definition::Push8bytes(uint64_t value)
{
    CJC_ASSERT(bytecode.size() + 1 < BYTECODE_CONTENT_MAX);
    (void)bytecode.emplace_back(static_cast<ByteCodeContent>(value));
    (void)bytecode.emplace_back(static_cast<ByteCodeContent>(value >> byteCodeContentWidth));
}


void Bchir::Definition::Set(ByteCodeIndex index, Bchir::ByteCodeContent value)
{
    CJC_ASSERT(index < bytecode.size());
    bytecode[static_cast<size_t>(index)] = value;
}

void Bchir::Definition::SetOp(ByteCodeIndex index, OpCode opcode)
{
    CJC_ASSERT(index < bytecode.size());
    bytecode[static_cast<size_t>(index)] = static_cast<Bchir::ByteCodeContent>(opcode);
}

size_t Bchir::Definition::Size() const
{
    return bytecode.size();
}

void Bchir::Definition::Resize(size_t newSize)
{
    CJC_ASSERT(newSize > bytecode.size());
    bytecode.resize(newSize);
}

Bchir::ByteCodeIndex Bchir::Definition::NextIndex() const
{
    CJC_ASSERT(this->Size() <= static_cast<size_t>(Bchir::BYTECODE_CONTENT_MAX));
    return static_cast<ByteCodeIndex>(this->Size());
}

const std::vector<Bchir::ByteCodeContent>& Bchir::Definition::GetByteCode() const
{
    return bytecode;
}

void Bchir::Definition::SetNumLVars(ByteCodeContent num)
{
    this->numLVars = num;
}

Bchir::ByteCodeContent Bchir::Definition::GetNumLVars() const
{
    return this->numLVars;
}

void Bchir::Definition::SetNumArgs(ByteCodeContent num)
{
    this->numArgs = num;
}

const Bchir::Definition& Bchir::GetLinkedByteCode() const
{
    return linkedByteCode;
}

void Bchir::Definition::AddMangledNameAnnotation(ByteCodeIndex idx, const std::string& mangledName)
{
    mangledNamesAnnotations.emplace(idx, mangledName);
}

void Bchir::Definition::AddCodePositionAnnotation(ByteCodeIndex idx, const CodePosition& pos)
{
    codePositionsAnnotations.emplace(idx, pos);
}

const std::string Bchir::DEFAULT_MANGLED_NAME = "";
const Bchir::CodePosition Bchir::DEFAULT_POSITION = CodePosition{0, 0, 0};

const std::string& Bchir::Definition::GetMangledNameAnnotation(ByteCodeIndex idx) const
{
    auto mangled = mangledNamesAnnotations.find(idx);
    if (mangled != mangledNamesAnnotations.end()) {
        return mangled->second;
    } else {
        return DEFAULT_MANGLED_NAME;
    }
}

const Bchir::CodePosition& Bchir::Definition::GetCodePositionAnnotation(ByteCodeIndex idx) const
{
    auto pos = codePositionsAnnotations.find(idx);
    if (pos != codePositionsAnnotations.end()) {
        return pos->second;
    } else {
        return DEFAULT_POSITION;
    }
}

const std::unordered_map<Bchir::ByteCodeIndex, std::string>& Bchir::Definition::GetMangledNamesAnnotations() const
{
    return mangledNamesAnnotations;
}

const std::unordered_map<Bchir::ByteCodeIndex, Bchir::CodePosition>&
Bchir::Definition::GetCodePositionsAnnotations() const
{
    return codePositionsAnnotations;
}

const std::string& Bchir::GetString(size_t index) const
{
    return strings[index];
}

size_t Bchir::AddString(std::string str)
{
    (void)strings.emplace_back(std::move(str));
    return strings.size() - 1;
}

const std::vector<std::string>& Bchir::GetStrings() const
{
    return strings;
}

IVal* Bchir::StoreStringArray(IArray&& array)
{
    return stringArrays.emplace_back(std::make_unique<IVal>(std::move(array))).get();
}

size_t Bchir::AddType(Cangjie::CHIR::Type& ty)
{
    auto idx = types.size();
    types.emplace_back(&ty);
    return idx;
}

const std::vector<Cangjie::CHIR::Type*>& Bchir::GetTypes() const
{
    return types;
}

const Cangjie::CHIR::Type* Bchir::GetTypeAt(size_t idx) const
{
    CJC_ASSERT(idx < types.size());
    return types.at(idx);
}

void Bchir::AddFunction(std::string mangledName, Bchir::Definition&& def)
{
    functions.emplace(mangledName, def);
}

const std::map<std::string, Bchir::Definition>& Bchir::GetFunctions() const
{
    return functions;
}

void Bchir::AddGlobalVar(std::string mangledName, Definition&& def)
{
    globalVars.emplace(mangledName, def);
}

const std::map<std::string, Bchir::Definition>& Bchir::GetGlobalVars() const
{
    return globalVars;
}

Bchir::ByteCodeIndex Bchir::GetDefaultFunctionPointer(Bchir::DefaultFunctionKind f) const
{
    CJC_ASSERT(defaultFuncPtrs.size() == static_cast<size_t>(DefaultFunctionKind::INVALID));
    return defaultFuncPtrs[static_cast<size_t>(f)];
}

const std::vector<Bchir::ByteCodeIndex>& Bchir::GetDefaultFuncPtrs() const
{
    return defaultFuncPtrs;
}

const std::string& Bchir::GetMainMangledName() const
{
    return mainMangledName;
}

void Bchir::LinkDefaultFunctions(const std::unordered_map<std::string, Bchir::ByteCodeIndex>& mangle2ptr)
{
    for (size_t i = 0; i < defaultFunctionsManledNames.size(); ++i) {
        auto it = mangle2ptr.find(defaultFunctionsManledNames[i]);
        if (it != mangle2ptr.end()) {
            defaultFuncPtrs[i] = it->second;
        } // else the interpreter was run without linking core
    }
    auto mainIt = mangle2ptr.find(mainMangledName);
    if (mainIt != mangle2ptr.end()) {
        defaultFuncPtrs[static_cast<size_t>(DefaultFunctionKind::MAIN)] = mainIt->second;
    }
}

void Bchir::SetMainMangledName(const std::string& mangledName)
{
    mainMangledName = mangledName;
}

size_t Bchir::GetMainExpectedArgs() const
{
    return expectedNumberOfArgumentsByMain;
}

void Bchir::SetMainExpectedArgs(size_t v)
{
    expectedNumberOfArgumentsByMain = v;
}

void Bchir::SetAsCore()
{
    isCore = true;
}

bool Bchir::IsCore() const
{
    return isCore;
}

const std::vector<std::string>& Bchir::GetFileNames() const
{
    return fileNames;
}

size_t Bchir::AddFileName(const std::string& name)
{
    auto idx = fileNames.size();
    fileNames.emplace_back(name);
    return idx;
}

const std::string& Bchir::GetFileName(size_t idx) const
{
    return fileNames[idx];
}

size_t Bchir::GetNumGlobalVars() const
{
    return numGlobalVars;
}

void Bchir::SetNumGlobalVars(size_t num)
{
    numGlobalVars = num;
}

void Bchir::SetGlobalInitFunc(const std::string& name)
{
    globalInitFunc = name;
}

const std::string& Bchir::GetGlobalInitFunc() const
{
    return globalInitFunc;
}

void Bchir::AddSClass(const std::string& mangledName, SClassInfo&& classInfo)
{
    sClassTable.emplace(mangledName, classInfo);
}

Bchir::SClassInfo* Bchir::GetSClass(const std::string& mangledName)
{
    auto it = sClassTable.find(mangledName);
    if (it == sClassTable.end()) {
        return nullptr;
    } else {
        return &it->second;
    }
}

const Bchir::SClassInfo* Bchir::GetSClass(const std::string& mangledName) const
{
    auto it = sClassTable.find(mangledName);
    if (it == sClassTable.end()) {
        return nullptr;
    } else {
        return &it->second;
    }
}

const Bchir::SClassTable& Bchir::GetSClassTable() const
{
    return sClassTable;
}

void Bchir::AddClass(ByteCodeContent id, ClassInfo&& classInfo)
{
    classTable.emplace(id, classInfo);
}

const Bchir::ClassInfo& Bchir::GetClass(ByteCodeContent id) const
{
    auto it = classTable.find(id);
    CJC_ASSERT(it != classTable.end());
    return it->second;
}

bool Bchir::ClassExists(ByteCodeContent id) const
{
    return classTable.find(id) != classTable.end();
}

Bchir::ByteCodeIndex Bchir::GetClassFinalizer(ByteCodeContent classId)
{
    auto classIt = classTable.find(classId);
    CJC_ASSERT(classIt != classTable.end());
    return classIt->second.finalizerIdx;
}

const Bchir::ClassTable& Bchir::GetClassTable() const
{
    return classTable;
}

void Bchir::RemoveFunction(const std::string& name)
{
    functions.erase(name);
}

void Bchir::RemoveGlobalVar(const std::string& name)
{
    globalVars.erase(name);
}

void Bchir::RemoveClass(const std::string& name)
{
    sClassTable.erase(name);
}

void Bchir::Set(ByteCodeIndex index, Bchir::ByteCodeContent value)
{
    linkedByteCode.Set(index, value);
}

void Bchir::SetOp(ByteCodeIndex index, OpCode opcode)
{
    linkedByteCode.SetOp(index, opcode);
}

void Bchir::Resize(size_t newSize)
{
    linkedByteCode.Resize(newSize);
}

const std::string Bchir::throwArithmeticException = "rt$ThrowArithmeticException";
const std::string Bchir::throwOverflowException = "rt$ThrowOverflowException";
const std::string Bchir::throwIndexOutOfBoundsException = "rt$ThrowIndexOutOfBoundsException";
const std::string Bchir::throwNegativeArraySizeException = "rt$ThrowNegativeArraySizeException";
const std::string Bchir::callToString = "rt$CallToString";
const std::string Bchir::throwArithmeticExceptionMsg = "rt$ThrowArithmeticException_msg";
const std::string Bchir::throwOutOfMemoryError = "rt$ThrowOutOfMemoryError";
const std::string Bchir::checkIsError = "rt$CheckIsError";
const std::string Bchir::throwError = "rt$ThrowError";
const std::string Bchir::callPrintStackTrace = "rt$CallPrintStackTrace";
const std::string Bchir::callPrintStackTraceError = "rt$CallPrintStackTraceError";

const std::vector<std::string> Bchir::defaultFunctionsManledNames = {throwArithmeticException, throwOverflowException,
    throwIndexOutOfBoundsException, throwNegativeArraySizeException, callToString, throwArithmeticExceptionMsg,
    throwOutOfMemoryError, checkIsError, throwError, callPrintStackTrace, callPrintStackTraceError,
};