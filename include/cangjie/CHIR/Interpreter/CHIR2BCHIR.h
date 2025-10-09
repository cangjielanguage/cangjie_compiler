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

#ifndef CANGJIE_CHIR_INTERRETER_CHIR2BCHIR_H
#define CANGJIE_CHIR_INTERRETER_CHIR2BCHIR_H

#include <fstream>
#include <functional>

#include "cangjie/Basic/SourceManager.h"
#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Interpreter/BCHIR.h"
#include "cangjie/CHIR/Interpreter/BCHIRPrinter.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Value.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Option/Option.h"

namespace Cangjie::CHIR::Interpreter {

class CHIR2BCHIR {
public:
    /** @brief Compile CHIR `chirPkg` into BCHIR `destBchir`. Set ForConstEval to true if BCHIR is intended
     * to evaluate constants. */
    template <bool ForConstEval = false>
    static void CompileToBCHIR(const Package& chirPkg, Bchir& destBchir,
        const std::vector<CHIR::FuncBase*>& initFuncsForConstVar, SourceManager& sm,
        const GlobalOptions& options, bool printBchir = false, bool incremental = false)
    {
        CHIR2BCHIR chir2bchir(destBchir, sm, incremental);
        chir2bchir.TranslatePackage<ForConstEval>(chirPkg, initFuncsForConstVar);
        if (printBchir) {
            auto stageName = ForConstEval ? "ce-chir2bchir" : "chir2bchir";
            auto bchirFile =
                CHIR::Interpreter::BCHIRPrinter::GetBCHIROutputFile(options, chirPkg.GetName(), stageName);
            auto bchirPrinter = CHIR::Interpreter::BCHIRPrinter(bchirFile, destBchir);
            bchirPrinter.PrintAll("BCHIR after CHIR2BCHIR for " + chirPkg.GetName());
        }
    }

private:
    Bchir& bchir;

    /** @brief source manager so that we can translate SOURCE_FILE intrinsic function */
    SourceManager& sourceManager;
    /** @brief CHIR const local variable ID */
    Bchir::ByteCodeContent constLocalVarId = 0;
    /** @brief Constant local variables ids. */
    std::unordered_map<const LocalVar*, Bchir::ByteCodeContent> const2CLVarId;
    /** @brief Whether this is incremental, with the previous BCHIR having been restored */
    bool isIncremental;

    CHIR2BCHIR(Bchir& bchir, SourceManager& sm, bool isIncremental)
        : bchir(bchir), sourceManager(sm), isIncremental(isIncremental)
    {
    }

    /** @brief memoization table for the index in bchir.fileNames */
    std::unordered_map<std::string, size_t> fileNameToIndexMemoization;
    /** @brief memoization table for the index in bchir.constStrings */
    std::unordered_map<std::string, Bchir::ByteCodeContent> constStringsMemoization;
    /** @brief memoization table for the index in bchir.types */
    std::unordered_map<CHIR::Type*, Bchir::ByteCodeContent> typesMemoization;

    /** @brief translate a CHIR package into BCHIR */
    template <bool ForConstEval> void TranslatePackage(
        const Package& chirPkg, const std::vector<CHIR::FuncBase*>& initFuncsForConstVar);
    template <bool ForConstEval> void TranslateClassesLike(const Package& chirPkg);
    template <bool ForConstEval> void TranslateClasses(const Package& chirPkg);
    template <bool ForConstEval> void TranslateStucts(const Package& chirPkg);
    template <bool ForConstEval> void TranslateEnums(const Package& chirPkg);
    template <bool ForConstEval> void TranslateExtends(const Package& chirPkg);
    template <bool ForConstEval> void TranslateGlobalVars(const Package& chirPkg);
    template <bool ForConstEval> void TranslateFunctions(const Package& chirPkg);

    /** @brief Adds a new type to the types section of bchir and returns its index. Uses typesMemoization. */
    Bchir::ByteCodeContent GetTypeIdx(CHIR::Type& chirType);

    void SetDefaultFunction(const Package& chirPkg, const Func& f);

    /* These functions should possibly be marked as intrinsic in CHIR, but since they aren't at the moment we
    intercept them in CHIR2BCHIR and translate them to intrinsic functions in BCHIR. */
    static const std::unordered_map<std::string, IntrinsicKind> syscall2IntrinsicKind;

    /** @brief Add a new string to the string section of bchir using stringsMem for memoization. */
    Bchir::ByteCodeContent GetStringIdx(std::string str);

    /** @brief Translate a CHIR location into a BCHIR location */
    Bchir::CodePosition CHIRPos2BCHIRPos(const DebugLocation& loc);

    struct Context {
        Bchir::Definition def;
        /** @brief CHIR local variable ID */
        Bchir::ByteCodeContent localVarId = 0;
        /** @brief Local variables ids. */
        std::unordered_map<const Value*, Bchir::ByteCodeContent> val2lvarId;
        /** @brief Index of the blocks that have been encoded so far. */
        std::unordered_map<const Block*, Bchir::ByteCodeIndex> bb2Index;
        /** @brief Index of the place holders of the blocks that haven't been encoded. */
        std::unordered_map<const Block*, std::vector<Bchir::ByteCodeIndex>> bb2IndexPlaceHolder;
    };

    template <bool ForConstEval> Context TranslateGlobalVar(const GlobalVar& gv);

    void TranslateFuncDef(Context& ctx, const Func& func);

    void TranslateValue(Context& ctx, const Value& value);
    void TranslateLiteralValue(Context& ctx, const LiteralValue& value);
    void TranslateBlock(Context& ctx, const Block& bb);

    void TranslateExpression(Context& ctx, const Expression& expr);
    void TranslateIntValue(Context& ctx, const IntLiteral& value);
    void TranslateFloatValue(Context& ctx, const FloatLiteral& value);
    void TranslateTerminatorExpression(Context& ctx, const Expression& expr);
    void TranslateUnaryExpression(Context& ctx, const Expression& expr);
    void TranslateBinaryExpression(Context& ctx, const Expression& expr);
    void TranslateMemoryExpression(Context& ctx, const Expression& expr);
    void TranslateOthersExpression(Context& ctx, const Expression& expr);
    void TranslateField(Context& ctx, const Expression& expr);
    void TranslateInvoke(Context& ctx, const Expression& expr);
    void TranslateTypecast(Context& ctx, const Expression& expr);
    template <typename T> void TranslateIntrinsicExpression(Context& ctx, const T& intrinsic);
    void TranslateCApplyExpression(Context& ctx, const Apply& apply, const Cangjie::CHIR::FuncType& funcTy);
    void TranslateApplyExpression(Context& ctx, const Apply& apply);
    void TranslateApplyWithExceptionExpression(Context& ctx, const ApplyWithException& apply);
    void TranslateMultiBranch(Context& ctx, const MultiBranch& branch);
    void TranslateAllocate(Context& ctx, const Expression& expr);
    void TranslateIntOpWithException(Context& ctx, const IntOpWithException& expr);
    void TranslateBox(Context& ctx, const Box& expr);
    void TranslateInstanceOf(Context& ctx, const InstanceOf& expr);

    /** @brief Returns the index for block `bb`. If the block hasn't been encoded yet
     * it returns a dummy index and adds `indexPlaceHolder` to `bb2IndexPlaceHolder`. */
    Bchir::ByteCodeContent BlockIndex(Context& ctx, const Block& bb, Bchir::ByteCodeIndex indexPlaceHolder);
    /** @brief Resolves all the placeholders for block `bb` with `idx`. */
    void ResolveBB2IndexPlaceHolder(Context& ctx, const Block& bb, Bchir::ByteCodeIndex idx);

    /** @brief Returns the id of a local variable. */
    Bchir::ByteCodeContent LVarId(Context& ctx, const Value& value);

    /** @brief generate bytecode for setting the result variable and jump indexes for
     * x_WITH_EXCEPTION operations */
    void TranslateTryTerminatorJumps(Context& ctx, const Terminator& expr);

    inline void PushArgs(Context&) const
    {
    }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif
    template <typename... Args> inline void PushArgs(Context& ctx, Bchir::ByteCodeContent v, Args&&... args)
    {
        ctx.def.Push(v);
        PushArgs(ctx, args...);
    }

    /** @brief Copy default annotations from CHIRNode into the OpAnnotationMap */
    template <bool StoreMangledName, bool StoreCodePos>
    void AddAnnotations(Context& ctx, const Expression& expr, Bchir::ByteCodeIndex opCodeIndex);
    /** @brief Copy default annotations from CHIRNode into the OpAnnotationMap */
    template <bool StoreMangledName, bool StoreCodePos>
    void AddAnnotations(Context& ctx, const Value& value, Bchir::ByteCodeContent opCodeIndex);

#ifndef NDEBUG
    // in debug mode store as much information as possible to make the debugging process easier
    template <bool StoreMangledName = true, bool StoreCodePos = true, typename... Args>
#else
    template <bool StoreMangledName = false, bool StoreCodePos = false, typename... Args>
#endif
    void PushOpCodeWithAnnotations(Context& ctx, OpCode opCode, const Expression& expr, Args&&... args)
    {
        auto opIdx = ctx.def.NextIndex();
        ctx.def.Push(opCode);
        if constexpr (StoreMangledName || StoreCodePos) {
            AddAnnotations<StoreMangledName, StoreCodePos>(ctx, expr, opIdx);
        }
        PushArgs(ctx, args...);
    }

#ifndef NDEBUG
    // in debug mode store as much information as possible to make the debugging process easier
    template <bool StoreMangledName = true, bool StoreCodePos = true, typename... Args>
#else
    template <bool StoreMangledName = false, bool StoreCodePos = false, typename... Args>
#endif
    void PushOpCodeWithAnnotations(Context& ctx, OpCode opCode, const Value& value, const Args&&... args)
    {
        auto opIdx = ctx.def.NextIndex();
        ctx.def.Push(opCode);
        if constexpr (StoreMangledName || StoreCodePos) {
            AddAnnotations<StoreMangledName, StoreCodePos>(ctx, value, opIdx);
        }
        PushArgs(ctx, args...);
    }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    bool IsConstClass(const CustomTypeDef& def) const;
};
} // namespace Cangjie::CHIR::Interpreter

#endif // CANGJIE_CHIR_INTERRETER_CHIR2BCHIR_H
