// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements a BCHIR printer.
 */

#include <functional>
#include <securec.h>

#include "cangjie/CHIR/Interpreter/BCHIRPrinter.h"

#include "cangjie/Basic/Print.h"
#include "cangjie/CHIR/IntrinsicKind.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/Utils/FileUtil.h"

using namespace Cangjie::CHIR::Interpreter;

static std::string DefaultFunctionKind2String(Bchir::DefaultFunctionKind kind)
{
    switch (kind) {
        case Bchir::DefaultFunctionKind::THROW_ARITHMETIC_EXCEPTION:
            return "THROW_ARITHMETIC_EXCEPTION";
        case Bchir::DefaultFunctionKind::THROW_OVERFLOW_EXCEPTION:
            return "THROW_OVERFLOW_EXCEPTION";
        case Bchir::DefaultFunctionKind::THROW_INDEX_OUT_OF_BOUNDS_EXCEPTION:
            return "THROW_INDEX_OUT_OF_BOUNDS_EXCEPTION";
        case Bchir::DefaultFunctionKind::THROW_NEGATIVA_ARRAY_SIZE_EXCEPTION:
            return "THROW_NEGATIVA_ARRAY_SIZE_EXCEPTION";
        case Bchir::DefaultFunctionKind::CALL_TO_STRING:
            return "CALL_TO_STRING";
        case Bchir::DefaultFunctionKind::THROW_ARITHMETIC_EXCEPTION_MSG:
            return "THROW_ARITHMETIC_EXCEPTION_MSG";
        case Bchir::DefaultFunctionKind::THROW_OUT_OF_MEMORY_ERROR:
            return "THROW_OUT_OF_MEMORY_ERROR";
        case Bchir::DefaultFunctionKind::CHECK_IS_ERROR:
            return "CHECK_IS_ERROR";
        case Bchir::DefaultFunctionKind::THROW_ERROR:
            return "THROW_ERROR";
        case Bchir::DefaultFunctionKind::CALL_PRINT_STACK_TRACE:
            return "CALL_PRINT_STACK_TRACE";
        case Bchir::DefaultFunctionKind::CALL_PRINT_STACK_TRACE_ERROR:
            return "CALL_PRINT_STACK_TRACE_ERROR";
        case Bchir::DefaultFunctionKind::MAIN:
            return "MAIN";
        case Bchir::DefaultFunctionKind::INVALID:
        default:
            CJC_ASSERT(false);
    }
    return "";
}

void BCHIRPrinter::Print(std::string header)
{
    os << "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << std::endl;
    os << "+++ " << header << std::endl;
    auto linkedBchir = bchir.GetLinkedByteCode();
    if (linkedBchir.Size() > 0) {
        os << "====== Linked bytecode ======" << std::endl;
        auto mainDefPrinter = DefinitionPrinter(bchir, linkedBchir, os);
        mainDefPrinter.Print();
    } else {
        os << "====== Global vars before linkage ======" << std::endl;
        for (auto& [mangled, def] : bchir.GetGlobalVars()) {
            os << "---- " << mangled << std::endl;
            auto defPrinter = DefinitionPrinter(bchir, def, os);
            defPrinter.Print();
        }

        os << "====== Functions before linkage ======" << std::endl;
        for (auto& [mangled, def] : bchir.GetFunctions()) {
            os << "---- " << mangled << std::endl;
            auto defPrinter = DefinitionPrinter(bchir, def, os);
            defPrinter.Print();
        }
    }
    os << "====== Default functions mangled names ======" << std::endl;
    os << "main mangled name: " << bchir.GetMainMangledName() << std::endl;
    os << "main expected arguments: " << bchir.GetMainExpectedArgs() << std::endl;
    os << "Global init function: " << bchir.GetGlobalInitFunc() << std::endl;
    if (linkedBchir.Size() > 0) {
        // these functions are only linked by BCHIRLinker
        auto& defaultFuncsPtrs = bchir.GetDefaultFuncPtrs();
        for (size_t i = 0; i < defaultFuncsPtrs.size(); ++i) {
            os << DefaultFunctionKind2String(static_cast<Bchir::DefaultFunctionKind>(i)) << ": " << defaultFuncsPtrs[i]
               << std::endl;
        }
    }
}

void BCHIRPrinter::DefinitionPrinter::Print()
{
    index = 0;
    os << "byte code" << std::endl;
    while (index < bytecode.size()) {
        PrintOP();
    }
    // Flush the buffer
    os << std::endl;
}

void BCHIRPrinter::DefinitionPrinter::PrintOpCode()
{
    auto opIdx = index++;
    os << LEFT << opIdx << ARGSEP << OpCodeLabel[bytecode[opIdx]];
    auto& mangledNames = def.GetMangledNamesAnnotations();
    auto& codePositions = def.GetCodePositionsAnnotations();
    auto mangledNameIt = mangledNames.find(static_cast<unsigned>(opIdx));
    auto codePosIt = codePositions.find(static_cast<unsigned>(opIdx));
    if (mangledNameIt != mangledNames.end() || codePosIt != codePositions.end()) {
        os << " { ";
    }
    if (mangledNameIt != mangledNames.end()) {
        os << "mangledName: " << mangledNameIt->second;
        if (codePosIt != codePositions.end()) {
            os << ", ";
        }
    }
    if (codePosIt != codePositions.end()) {
        auto codePos = codePosIt->second;
        os << bchir.GetFileName(codePos.fileID) << ":" << codePos.line << ":" << codePos.column;
    }
    if (mangledNameIt != mangledNames.end() || codePosIt != codePositions.end()) {
        os << "}";
    }
    os << RIGHT << OPSEP;
    CJC_ASSERT(opIdx + 1 == index);
}

void BCHIRPrinter::DefinitionPrinter::PrintTy()
{
    size_t typeIdx = bytecode[index];
    os << LEFT << index << ARGSEP << "Ty (idx: " << typeIdx << "):";
    auto ty = bchir.GetTypeAt(typeIdx);
    os << ty->ToString() << RIGHT << OPSEP;
    index++;
}

void BCHIRPrinter::DefinitionPrinter::PrintAtIndex()
{
    os << LEFT << index << ARGSEP << bytecode[index] << RIGHT << OPSEP;
    index++;
}

void BCHIRPrinter::DefinitionPrinter::PrintAtIndex8bytes()
{
    auto val = *reinterpret_cast<const uint64_t*>(&bytecode[index]);
    os << LEFT << index << ARGSEP << std::to_string(val) << RIGHT << OPSEP;
    index++;
    index++;
}

void BCHIRPrinter::DefinitionPrinter::Print(size_t argIndex, const std::string& str)
{
    os << LEFT << argIndex << ARGSEP << str << RIGHT << OPSEP;
}

void BCHIRPrinter::DefinitionPrinter::PrintOPFloat()
{
    float d;
    if (memcpy_s(&d, sizeof(float), &bytecode[index], sizeof(float)) != EOK) {
        CJC_ABORT();
    } else {
        Print(index, std::to_string(d));
        index++;
    }
    return;
}

void BCHIRPrinter::DefinitionPrinter::PrintOPFloat8bytes()
{
    double d;
    if (memcpy_s(&d, sizeof(double), &bytecode[index], sizeof(double)) != EOK) {
        CJC_ABORT();
    } else {
        Print(index, std::to_string(d));
        index++;
        index++;
    }
    return;
}

void BCHIRPrinter::DefinitionPrinter::PrintOPRune()
{
    auto val = static_cast<char32_t>(bytecode[index]);
    std::stringstream stream;
    stream << "r'";
    if (val >= ' ' && val <= '~') {
        if (val == '\'' || val == '\\') {
            stream << '\\' << static_cast<char>(val);
        } else {
            stream << static_cast<char>(val);
        }
    } else {
        stream << "\\u{" << std::hex << val << "}";
    }
    stream << '\'';
    Print(index, stream.str());
    index++;
}

void BCHIRPrinter::DefinitionPrinter::PrintOPSwitch()
{
    // switch overloading type
    CHIR::Type::TypeKind tyKind = static_cast<CHIR::Type::TypeKind>(bytecode[index]);
    auto it = CHIR::TYPEKIND_TO_STRING.find(tyKind);
    CJC_ASSERT(it != CHIR::TYPEKIND_TO_STRING.end());
    Print(index, it->second);
    index++;
    size_t cases = bytecode[index];
    PrintAtIndex();

    // Print cases
    for (size_t i = 0; i < cases; i++) {
        PrintAtIndex8bytes();
    }

    // Print target index
    for (size_t i = 0; i <= cases; i++) {
        PrintAtIndex();
    }
}

const std::unordered_map<Cangjie::OverflowStrategy, std::string> BCHIRPrinter::OVERFLOW_STRAT2STRING = {
    {Cangjie::OverflowStrategy::NA, "NA"},
    {Cangjie::OverflowStrategy::CHECKED, "CHECKED"},
    {Cangjie::OverflowStrategy::WRAPPING, "WRAPPING"},
    {Cangjie::OverflowStrategy::THROWING, "THROWING"},
    {Cangjie::OverflowStrategy::SATURATING, "SATURATING"},
};

void BCHIRPrinter::DefinitionPrinter::PrintOPBinRshift(OpCode opCode)
{
    // binary operation overloading type
    CHIR::Type::TypeKind tyKind = static_cast<CHIR::Type::TypeKind>(bytecode[index]);
    auto it = CHIR::TYPEKIND_TO_STRING.find(tyKind);
    CJC_ASSERT(it != CHIR::TYPEKIND_TO_STRING.end());
    Print(index, it->second);
    index++;

    OverflowStrategy overflowStrat = static_cast<OverflowStrategy>(bytecode[index]);
    auto sit = OVERFLOW_STRAT2STRING.find(overflowStrat);
    CJC_ASSERT(sit != OVERFLOW_STRAT2STRING.end());
    Print(index, sit->second);
    index++;

    if (opCode == OpCode::BIN_LSHIFT || opCode == OpCode::BIN_RSHIFT || opCode == OpCode::BIN_RSHIFT_EXC ||
        opCode == OpCode::BIN_LSHIFT_EXC) {
        // type for rhs
        CHIR::Type::TypeKind rhsTyKind = static_cast<CHIR::Type::TypeKind>(bytecode[index]);
        auto it2 = CHIR::TYPEKIND_TO_STRING.find(rhsTyKind);
        CJC_ASSERT(it2 != CHIR::TYPEKIND_TO_STRING.end());
        auto typeKind = it2->second;
        Print(index, std::move(typeKind));
        index++;
    }
}

void BCHIRPrinter::DefinitionPrinter::PrintOPTypeCast()
{
    CHIR::Type::TypeKind tyKind = static_cast<CHIR::Type::TypeKind>(bytecode[index]);
    auto it = CHIR::TYPEKIND_TO_STRING.find(tyKind);
    CJC_ASSERT(it != CHIR::TYPEKIND_TO_STRING.end());
    Print(index, it->second);
    index++;

    tyKind = static_cast<CHIR::Type::TypeKind>(bytecode[index]);
    it = CHIR::TYPEKIND_TO_STRING.find(tyKind);
    CJC_ASSERT(it != CHIR::TYPEKIND_TO_STRING.end());
    Print(index, it->second);
    index++;

    OverflowStrategy overflowStrat = static_cast<OverflowStrategy>(bytecode[index]);
    auto sit = OVERFLOW_STRAT2STRING.find(overflowStrat);
    CJC_ASSERT(sit != OVERFLOW_STRAT2STRING.end());
    Print(index, sit->second);
    index++;
}

void BCHIRPrinter::DefinitionPrinter::PrintPath()
{
    auto pathSize = bytecode[index];
    PrintAtIndex();
    for (size_t i = 0; i < pathSize; ++i) {
        PrintAtIndex();
    }
}

void BCHIRPrinter::DefinitionPrinter::PrintOPIntrinsic(OpCode opCode)
{
    auto kind = static_cast<CHIR::IntrinsicKind>(bytecode[index]);
    Print(index, "INTRINSIC_KIND: " + std::to_string(bytecode[index]));
    index++;
    if (opCode == OpCode::INTRINSIC0 || opCode == OpCode::INTRINSIC0_EXC) {
        return;
    }
    Bchir::ByteCodeContent tyIdx = bytecode[index];
    if (tyIdx != Bchir::BYTECODE_CONTENT_MAX) {
        PrintTy(); // it increments index
    } else {
        Print(index++, "(no type)");
    }
    if (opCode == OpCode::INTRINSIC1 || opCode == OpCode::INTRINSIC1_EXC) {
        return;
    }
    // in this case we have an additional argument
    CJC_ASSERT(kind == CHIR::IntrinsicKind::ARRAY_SLICE || kind == CHIR::IntrinsicKind::ARRAY_SLICE_GET_ELEMENT ||
        kind == CHIR::IntrinsicKind::ARRAY_SLICE_SET_ELEMENT);
    auto stratIndex = index++;
    OverflowStrategy overflowStrat = static_cast<OverflowStrategy>(bytecode[stratIndex]);
    auto sit = OVERFLOW_STRAT2STRING.find(overflowStrat);
    CJC_ASSERT(sit != OVERFLOW_STRAT2STRING.end());
    auto overflow = sit->second;
    Print(index, std::move(overflow));
}


void BCHIRPrinter::DefinitionPrinter::PrintOP()
{
    auto opCode = static_cast<OpCode>(bytecode[index]);
    PrintOpCode();
    switch (opCode) {
        case OpCode::GVAR_SET:
        case OpCode::LVAR_SET:
        case OpCode::GVAR:
        case OpCode::LVAR: {
            PrintAtIndex();
            return;
        }
        case OpCode::ALLOCATE_RAW_ARRAY_LITERAL: {
            PrintAtIndex(); // size
            return;
        }
        case OpCode::ALLOCATE_RAW_ARRAY_LITERAL_EXC: {
            PrintAtIndex(); // size
            PrintAtIndex(); // jump target for when exception is raised
            return;
        }
        case OpCode::RAW_ARRAY_LITERAL_INIT: {
            PrintAtIndex(); // size
            return;
        }
        case OpCode::RAW_ARRAY_INIT_BY_VALUE:
        case OpCode::ALLOCATE_RAW_ARRAY: {
            return;
        }
        case OpCode::ALLOCATE_RAW_ARRAY_EXC: {
            PrintAtIndex();
            return;
        }
        case OpCode::ALLOCATE_CLASS: {
            // class id
            PrintAtIndex();
            // number of fields
            PrintAtIndex();
            return;
        }
        case OpCode::ALLOCATE_CLASS_EXC: {
            // class id
            PrintAtIndex();
            // number of fields
            PrintAtIndex();
            // jump target for when exception is raised
            PrintAtIndex();
            return;
        }
        case OpCode::ALLOCATE_STRUCT: {
            PrintAtIndex(); // number of fields
            return;
        }
        case OpCode::ALLOCATE_STRUCT_EXC: {
            PrintAtIndex(); // number of fields
            PrintAtIndex(); // jump target for when exception is raised
            return;
        }
        case OpCode::ALLOCATE: {
            return;
        }
        case OpCode::ALLOCATE_EXC: {
            PrintAtIndex(); // jump target for when exception is raised
            return;
        }
        case OpCode::FRAME: {
            PrintAtIndex();
            return;
        }
        case OpCode::UINT8:
        case OpCode::UINT16:
        case OpCode::UINT32: {
            PrintAtIndex();
            return;
        }
        case OpCode::UINT64:
        case OpCode::UINTNAT: {
            PrintAtIndex8bytes();
            return;
        }
        case OpCode::INT8:
        case OpCode::INT16:
        case OpCode::INT32: {
            auto val = static_cast<int32_t>(bytecode[index]);
            Print(index, std::to_string(val));
            index++;
            return;
        }
        case OpCode::INT64:
        case OpCode::INTNAT: {
            auto val = *reinterpret_cast<const int64_t*>(&bytecode[index]);
            Print(index, std::to_string(val));
            index++;
            index++;
            return;
        }
        case OpCode::FLOAT16:
        case OpCode::FLOAT32: {
            PrintOPFloat();
            return;
        }
        case OpCode::FLOAT64: {
            PrintOPFloat8bytes();
            return;
        }
        case OpCode::RUNE: {
            PrintOPRune();
            return;
        }
        case OpCode::BOOL: {
            auto val = static_cast<int32_t>(bytecode[index]);
            Print(index, std::to_string(val));
            index++;
            return;
        }
        case OpCode::UNIT:
        case OpCode::NULLPTR: {
            return;
        }
        case OpCode::STRING: {
            auto strIdx = bytecode[index];
            auto str = bchir.GetString(strIdx);
            Print(index, "(str: " + std::to_string(strIdx) + ")" + " -> " + str);
            index++;
            return;
        }
        case OpCode::TUPLE: {
            // tuple size
            PrintAtIndex();
            return;
        }
        case OpCode::ARRAY: {
            PrintAtIndex();
            return;
        }
        case OpCode::VARRAY_BY_VALUE: {
            return;
        }
        case OpCode::VARRAY: {
            PrintAtIndex();
            return;
        }
        case OpCode::VARRAY_GET: {
            PrintAtIndex();
            return;
        }
        case OpCode::FUNC: {
            // thunk index
            PrintAtIndex();
            return;
        }
        case OpCode::STORE:
        case OpCode::RETURN:
        case OpCode::EXIT:
        case OpCode::DROP: {
            return;
        }
        case OpCode::JUMP: {
            // target index
            PrintAtIndex();
            return;
        }
        case OpCode::BRANCH: {
            PrintAtIndex();
            PrintAtIndex();
            return;
        }
        case OpCode::SWITCH: {
            PrintOPSwitch();
            return;
        }
        case OpCode::UN_NEG:
        case OpCode::UN_NOT:
        case OpCode::UN_DEC:
        case OpCode::UN_INC:
        case OpCode::UN_BITNOT:
        case OpCode::BIN_ADD:
        case OpCode::BIN_SUB:
        case OpCode::BIN_MUL:
        case OpCode::BIN_DIV:
        case OpCode::BIN_MOD:
        case OpCode::BIN_EXP:
        case OpCode::BIN_LT:
        case OpCode::BIN_GT:
        case OpCode::BIN_LE:
        case OpCode::BIN_GE:
        case OpCode::BIN_NOTEQ:
        case OpCode::BIN_EQUAL:
        case OpCode::BIN_BITAND:
        case OpCode::BIN_BITOR:
        case OpCode::BIN_BITXOR:
        case OpCode::BIN_LSHIFT:
        case OpCode::BIN_RSHIFT: {
            PrintOPBinRshift(opCode);
            return;
        }
        case OpCode::UN_NEG_EXC:
        case OpCode::BIN_ADD_EXC:
        case OpCode::BIN_SUB_EXC:
        case OpCode::BIN_MUL_EXC:
        case OpCode::BIN_DIV_EXC:
        case OpCode::BIN_MOD_EXC:
        case OpCode::BIN_EXP_EXC:
        case OpCode::BIN_LSHIFT_EXC:
        case OpCode::BIN_RSHIFT_EXC: {
            PrintOPBinRshift(opCode); // type kind, overflow strategy
            PrintAtIndex();           // jump target for when exception is raised
            return;
        }
        case OpCode::FIELD: {
            // field index
            PrintAtIndex();
            return;
        }
        case OpCode::FIELD_TPL: {
            auto pathSize = bytecode[index];
            PrintAtIndex();
            for (Bchir::ByteCodeContent i = 0; i < pathSize; ++i) {
                PrintAtIndex();
            }
            break;
        }
        case OpCode::INVOKE: {
            // number of args
            PrintAtIndex();
            // name id
            PrintAtIndex();
            return;
        }
        case OpCode::INVOKE_EXC: {
            // number of args
            PrintAtIndex();
            // name id
            PrintAtIndex();
            // jump target for when exception is raised
            PrintAtIndex();
            return;
        }
        case OpCode::TYPECAST: {
            PrintOPTypeCast();
            return;
        }
        case OpCode::TYPECAST_EXC: {
            PrintOPTypeCast(); // source type kind, target type kind, overflow strategy
            PrintAtIndex();    // jump target for when exception is raised
            return;
        }
        case OpCode::INSTANCEOF: {
            // class id
            PrintAtIndex();
            return;
        }
        case OpCode::APPLY: {
            // number of arguments
            PrintAtIndex();
            return;
        }
        case OpCode::APPLY_EXC: {
            // number of arguments
            PrintAtIndex();
            // jump target for when exception is raised
            PrintAtIndex();
            return;
        }
        case OpCode::CAPPLY: {
            return;
        }
        case OpCode::ASG: {
            return;
        }
        case OpCode::GETREF:
        case OpCode::STOREINREF: {
            PrintPath();
            return;
        }
        case OpCode::DEREF: {
            return;
        }
        case OpCode::SYSCALL: {
            return;
        }
        case OpCode::INTRINSIC0:
        case OpCode::INTRINSIC1:
        case OpCode::INTRINSIC2: {
            PrintOPIntrinsic(opCode);
            return;
        }
        case OpCode::INTRINSIC0_EXC:
        case OpCode::INTRINSIC1_EXC:
        case OpCode::INTRINSIC2_EXC: {
            PrintOPIntrinsic(opCode);
            PrintAtIndex();
            return;
        }
        case OpCode::SPAWN_EXC: {
            PrintAtIndex();
            return;
        }
        case OpCode::RAISE_EXC: {
            PrintAtIndex(); // target block
            return;
        }
        case OpCode::ABORT: {
            return;
        }
        case OpCode::RAISE:
        case OpCode::GET_EXCEPTION:
        case OpCode::SPAWN:
        case OpCode::NOT_SUPPORTED: {
            return;
        }
        case OpCode::BOX: {
            // class id
            PrintAtIndex();
            return;
        }
        case OpCode::UNBOX:
        case OpCode::UNBOX_REF: {
            return;
        }
        default: {
            CJC_ASSERT(false);
            Errorln("Printer not implemented.");
        }
    }
}

void BCHIRPrinter::PrintAll(std::string header)
{
    Print(header);
    PrintSClassTable();
    PrintStrings();
    PrintTypes();
    PrintSourceFiles();
}

void BCHIRPrinter::PrintSClassTable()
{
    os << "====== Class table ======" << std::endl;
    for (auto& bclass : bchir.GetSClassTable()) {
        os << bclass.first << ":" << std::endl;
        auto& classInfo = bclass.second;
        for (auto& sc : classInfo.superClasses) {
            os << "\t" << sc << std::endl;
        }
        os << std::endl;
        for (auto& entry : classInfo.vtable) {
            os << "\t" << entry.first << ": " << entry.second << std::endl;
        }
        os << "\tfinalizer: " << classInfo.finalizer << std::endl;
    }
}

void BCHIRPrinter::PrintStrings()
{
    os << "====== Strings ======\n";
    auto& strings = bchir.GetStrings();
    for (size_t i = 0; i < strings.size(); ++i) {
        os << i << " - " << strings[i] << std::endl;
    }
}

void BCHIRPrinter::PrintTypes()
{
    os << "====== Types ======\n";
    auto& types = bchir.GetTypes();
    for (size_t i = 0; i < types.size(); ++i) {
        os << i << " - " << types[i]->ToString() << std::endl;
    }
}

void BCHIRPrinter::PrintSourceFiles()
{
    os << "====== Source files ======" << std::endl;
    auto& fileNames = bchir.GetFileNames();
    for (size_t i = 0; i < fileNames.size(); ++i) {
        os << i << " - " << fileNames[i] << std::endl;
    }
}

std::fstream BCHIRPrinter::GetBCHIROutputFile(
    const Cangjie::GlobalOptions& options, const std::string& fullPackageName, const std::string& stageName)
{
    std::fstream f;
    std::string bchirDir;
    auto& outputPath = options.output;

    if (FileUtil::IsDir(outputPath)) {
        bchirDir = FileUtil::JoinPath(outputPath, "BCHIR_Debug");
    } else {
        bchirDir = FileUtil::GetFileBase(outputPath) + "_BCHIR_Debug";
    }
    auto path = FileUtil::JoinPath(bchirDir, fullPackageName + "_" + stageName + ".bchir");
    FileUtil::CreateDirs(path);
    f.open(path, std::ios::out);
    return f;
}
