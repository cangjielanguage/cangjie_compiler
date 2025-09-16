// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the translation of CHIR to BCHIR.
 */

#ifndef CANGJIE_CHIR_INTERRETER_BCHIR_H
#define CANGJIE_CHIR_INTERRETER_BCHIR_H

#include <functional>
#include <iostream>
#include <vector>

#include "cangjie/CHIR/Interpreter/OpCodes.h"
#include "cangjie/CHIR/Interpreter/InterpreterValue.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie::CHIR::Interpreter {

class Bchir {
    friend class BCHIRLinker;

public:
    Bchir()
    {
        defaultFuncPtrs.resize(static_cast<size_t>(DefaultFunctionKind::INVALID));
    }

    Bchir Clone() const;

    /** @brief the type of each cell in the bytecode. */
    using ByteCodeContent = uint32_t;
    using ByteCodeIndex = uint32_t;
    static const ByteCodeContent BYTECODE_CONTENT_MAX = UINT32_MAX;
    static const ByteCodeIndex BYTECODE_INDEX_MAX = UINT32_MAX;
    static const int byteCodeContentWidth = sizeof(ByteCodeContent) * CHAR_BIT;

    /** @brief the type of a variable identifier in BCHIR. Should be the same as ByteCodeContent for
     * simplification purposes. */
    using VarIdx = ByteCodeContent;

    /** @brief constant for Ops */
    static const ByteCodeContent FLAG_ONE = 1;
    static const ByteCodeContent FLAG_TWO = 2;
    static const ByteCodeContent FLAG_THREE = 3;
    static const ByteCodeContent FLAG_FOUR = 4;
    static const ByteCodeContent FLAG_FIVE = 5;
    static const ByteCodeContent FLAG_SIX = 6;
    static const ByteCodeContent DUMMY = 0;

    struct CodePosition {
        // position in fileNames vector
        size_t fileID;
        unsigned line;
        unsigned column;
    };

    struct Definition {
    public:
        /** @brief pushes opcode into the bytecode. */
        void Push(OpCode opcode);
        /** @brief pushes value into the bytecode. */
        void Push(Bchir::ByteCodeContent value);
        /** @brief push a 8 bytes value */
        void Push8bytes(uint64_t value);
        /** @brief sets value at index in the bytecode. */
        void Set(ByteCodeIndex index, Bchir::ByteCodeContent value);
        /** @brief sets opcode at index in the bytecode. */
        void SetOp(ByteCodeIndex index, OpCode opcode);
        /** @brief get value at index in bytecode */
        inline ByteCodeContent Get(ByteCodeIndex index) const
        {
            CJC_ASSERT(index < bytecode.size());
            return bytecode[static_cast<size_t>(index)];
        }
        /** @brief get 8-bytes value at index in bytecode */
        inline uint64_t Get8bytes(ByteCodeIndex index) const
        {
            // needs two cells to represent 8 bytes
            CJC_ASSERT(index + 1 < bytecode.size());
            return bytecode[static_cast<size_t>(index)] +
                (static_cast<uint64_t>(bytecode[static_cast<size_t>(index) + 1]) << byteCodeContentWidth);
        }
        /** @brief get size of bytecode */
        size_t Size() const;
        /** @brief next available index (same as Size, but returns ByteCodeIndex). */
        ByteCodeIndex NextIndex() const;
        /** @brief resizes the bytecode array */
        void Resize(size_t newSize);
        /** @brief get a reference to the bytecode array */
        const std::vector<ByteCodeContent>& GetByteCode() const;
        /** @brief set total number of local vars (including arguments) */
        void SetNumLVars(ByteCodeContent num);
        /** @brief get total number of local vars */
        ByteCodeContent GetNumLVars() const;
        /** @brief set number of arguments */
        void SetNumArgs(ByteCodeContent num);

        // Annotations

        /** @brief associate `nameIdx` mangled name to `idx` in mangledNames */
        void AddMangledNameAnnotation(ByteCodeIndex idx, const std::string& mangledName);
        /** @brief associate `pos` to `idx` in codePositions */
        void AddCodePositionAnnotation(ByteCodeIndex idx, const CodePosition& pos);
        /** @brief get mangled name of `idx` */
        const std::string& GetMangledNameAnnotation(ByteCodeIndex idx) const;
        /** @brief get code position of `idx` */
        const CodePosition& GetCodePositionAnnotation(ByteCodeIndex idx) const;
        /** @brief get all the mangled names */
        const std::unordered_map<ByteCodeIndex, std::string>& GetMangledNamesAnnotations() const;
        /** @brief get all the code positions */
        const std::unordered_map<ByteCodeIndex, CodePosition>& GetCodePositionsAnnotations() const;

    private:
        /** @brief number of parameters */
        ByteCodeContent numArgs{0};
        /** @brief number of local vars (including parameters) */
        ByteCodeContent numLVars{0};
        /** @brief bytecode of this definition */
        std::vector<ByteCodeContent> bytecode;
        /** @brief Annotation: bytecode index -> mangled name */
        std::unordered_map<ByteCodeIndex, std::string> mangledNamesAnnotations;
        /** @brief Annotation: bytecode index -> source code position */
        std::unordered_map<ByteCodeIndex, CodePosition> codePositionsAnnotations;
    };

    // Default functions from core
    static const std::string throwArithmeticException;
    static const std::string throwOverflowException;
    static const std::string throwIndexOutOfBoundsException;
    static const std::string throwNegativeArraySizeException;
    static const std::string callToString;
    static const std::string throwArithmeticExceptionMsg;
    static const std::string throwOutOfMemoryError;
    static const std::string checkIsError;
    static const std::string throwError;
    static const std::string callPrintStackTrace;
    static const std::string callPrintStackTraceError;

    /** @brief default functions declared by the user or standard library that the interpreter
     * needs to identify */
    enum class DefaultFunctionKind {
        THROW_ARITHMETIC_EXCEPTION,
        THROW_OVERFLOW_EXCEPTION,
        THROW_INDEX_OUT_OF_BOUNDS_EXCEPTION,
        THROW_NEGATIVA_ARRAY_SIZE_EXCEPTION,
        CALL_TO_STRING,
        THROW_ARITHMETIC_EXCEPTION_MSG,
        THROW_OUT_OF_MEMORY_ERROR,
        CHECK_IS_ERROR,
        THROW_ERROR,
        CALL_PRINT_STACK_TRACE,
        CALL_PRINT_STACK_TRACE_ERROR,
        MAIN,
        INVALID
    };

    static const std::vector<std::string> defaultFunctionsManledNames;

    /** @brief Get function pointer */
    ByteCodeIndex GetDefaultFunctionPointer(DefaultFunctionKind f) const;
    /** @brief Return the vector of pointers to the default functions */
    const std::vector<ByteCodeIndex>& GetDefaultFuncPtrs() const;
    /** @brief Generate `defaultFuncPtrs` from `defaultFuncMangledNames` and `mangle2ptr` */
    void LinkDefaultFunctions(const std::unordered_map<std::string, Bchir::ByteCodeIndex>& mangle2ptr);

    /** @brief get main function mangled name */
    const std::string& GetMainMangledName() const;
    /** @brief Set main function mangled name */
    void SetMainMangledName(const std::string& mangledName);
    /** @brief returns the number of arguments the main function expects, or 0 if package does not contain a main
     * function */
    size_t GetMainExpectedArgs() const;
    /** @brief sets the number of arguments the main function expects */
    void SetMainExpectedArgs(size_t v);

    /** @brief sets this package as bchir for core package */
    void SetAsCore();
    /** @brief true if this bchir for core package */
    bool IsCore() const;

    // For serialization
    // OPTIMIZE: this is quite expensive to serialize all mangled names again,
    //       we can do better by resolving those that can be resolved first
    // method name -> mangled name
    using SVTable = std::unordered_map<std::string, std::string>;
    struct SClassInfo {
        // only direct superclasses
        std::vector<std::string> superClasses;
        SVTable vtable;
        std::string finalizer;
    };
    // mangled name -> class
    using SClassTable = std::unordered_map<std::string, SClassInfo>;

    // For execution
    // method id -> func body index
    using VTable = std::unordered_map<ByteCodeContent, ByteCodeIndex>;
    struct ClassInfo {
        // Transitive closure of superclasses, required for instanceof
        std::set<ByteCodeContent> superClasses;
        VTable vtable;
        ByteCodeIndex finalizerIdx = 0; // func body index
        // Required to go from `ClassId` to CHIR `Class*` during constant evaluation.
        std::string mangledName;
    };
    // OPTIMIZE: we can do better to make this as a vector
    // class id -> class info
    using ClassTable = std::unordered_map<ByteCodeContent, ClassInfo>;

    /** @brief get linkedByteCode */
    const Definition& GetLinkedByteCode() const;

    /** @brief get value at index from linkedByteCode */
    inline ByteCodeContent Get(ByteCodeIndex index) const
    {
        return linkedByteCode.Get(index);
    }
    /** @brief get 8-bytes value at index from linkedByteCode */
    inline uint64_t Get8bytes(ByteCodeIndex index) const
    {
        return linkedByteCode.Get8bytes(index);
    }
    /** @brief sets value at index in the linkedByteCode. */
    void Set(ByteCodeIndex index, Bchir::ByteCodeContent value);
    /** @brief sets opcode at index in the linkedByteCode. */
    void SetOp(ByteCodeIndex index, OpCode opcode);
    /** @brief resize linked bytecode array */
    void Resize(size_t newSize);

    /** @brief add a string to the string section */
    size_t AddString(std::string str);
    /** @brief get string at index from the string section */
    const std::string& GetString(size_t index) const;
    /** @brief get all strings */
    const std::vector<std::string>& GetStrings() const;
    IVal* StoreStringArray(IArray&& array);

    /** @brief get types section */
    const std::vector<Cangjie::CHIR::Type*>& GetTypes() const;

    /** @brief Adds a new type to the types section and returns its index. */
    size_t AddType(Cangjie::CHIR::Type& ty);
    /** @brief Retrieves a type from the types section
     *  Index must be smaller than type vector
     */
    const Cangjie::CHIR::Type* GetTypeAt(size_t idx) const;

    /** @brief add a function definition */
    void AddFunction(std::string mangledName, Definition&& def);
    /** @brief get all functions */
    const std::map<std::string, Definition>& GetFunctions() const;

    /** @brief add global variable */
    void AddGlobalVar(std::string mangledName, Definition&& def);
    /** @brief get all global variables */
    const std::map<std::string, Definition>& GetGlobalVars() const;

    /** @brief Add a new file name to the file names section. */
    size_t AddFileName(const std::string& name);
    /** @brief Get the file name at index `idx` from the file names section. */
    const std::string& GetFileName(size_t idx) const;
    /** @brief Get the file names section. */
    const std::vector<std::string>& GetFileNames() const;
    /** @brief Set the file names section. */
    void SetFileNames(std::vector<std::string>&& names);

    size_t GetNumGlobalVars() const;
    void SetNumGlobalVars(size_t num);

    void SetGlobalInitFunc(const std::string& name);
    const std::string& GetGlobalInitFunc() const;

    void SetGlobalInitLiteralFunc(const std::string& name);
    const std::string& GetGlobalInitLiteralFunc() const;

    void AddSClass(const std::string& mangledName, SClassInfo&& classInfo);
    const SClassInfo* GetSClass(const std::string& mangledName) const;
    SClassInfo* GetSClass(const std::string& mangledName);
    const SClassTable& GetSClassTable() const;

    void AddClass(ByteCodeContent id, ClassInfo&& classInfo);
    const ClassInfo& GetClass(ByteCodeContent id) const;
    bool ClassExists(ByteCodeContent id) const;
    void SetVtableEntry(ByteCodeContent classId, ByteCodeContent mId, ByteCodeIndex idx);
    /** @brief set the class finalizer */
    void SetClassFinalizer(ByteCodeContent classId, ByteCodeIndex idx);
    ByteCodeIndex GetClassFinalizer(ByteCodeContent classId);
    const ClassTable& GetClassTable() const;

    /** @brief Instantiate important types and functions from core. To be used only when we don't want to link core. */
    void InstantiateDefaultCoreClassesAndFunctions();

    std::string packageName;

    void RemoveFunction(const std::string& name);
    void RemoveGlobalVar(const std::string& name);
    void RemoveClass(const std::string& name);
    /** @brief remove the function/variable/class with the provided mangled name */
    void RemoveDefinition(const std::string& name);
    std::vector<std::string> initFuncsForConsts;

private:
    // before linking (needs serializing)
    // ordered map so that serialization order is deterministic
    /** @brief mangled name -> global variables definition (initializer if any)
     *         the initializer can only be Constant or a Function
     */
    std::map<std::string, Definition> globalVars;
    /** @brief mangled name -> function definition */
    std::map<std::string, Definition> functions;
    // mangled name of the global init function of this package
    std::string globalInitFunc;
    // mangled name of the global init Literal function of this package
    std::string globalInitLiteralFunc;
    /** @brief class table for serialization */
    SClassTable sClassTable;
    /** @brief all the mangled names */
    std::vector<std::string> mangledNames;

    // both before and after (needs serializing)
    /** @brief pointers to CHIR context types */
    std::vector<Type*> types;
    /** @brief section for string literals */
    std::vector<std::string> strings;
    /** @brief IArrays for const strings. Only for linked BCHIR. */
    std::vector<std::unique_ptr<IVal>> stringArrays;
    /** @brief all the file names */
    std::vector<std::string> fileNames;
    /** @brief main function mangled name */
    std::string mainMangledName;

    // after linking (no need to serialize)
    /** @brief bytecode after linking */
    Definition linkedByteCode;
    /** @brief class table after linking */
    ClassTable classTable;
    /** @brief all special function pointers
     *
     * Assumption: ByteCodeIndex == 0 means that the function does not exist in BCHIR
     */
    std::vector<ByteCodeIndex> defaultFuncPtrs;
    /** @brief the number of arguments the function main expects if this package has a main function */
    size_t expectedNumberOfArgumentsByMain = 0;
    /** @brief total num of global vars */
    size_t numGlobalVars = 0;
    /** @brief true if this is core package */
    bool isCore{false};

    static const std::string DEFAULT_MANGLED_NAME;
    static const CodePosition DEFAULT_POSITION;
};

} // namespace Cangjie::CHIR::Interpreter

#endif // CANGJIE_CHIR_INTERRETER_BCHIR_H
