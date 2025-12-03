// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements intrinsics functions in the interpreter for the standard library.
 */

#include <securec.h>
#include <thread>

#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/Interpreter/BCHIRInterpreter.h"
#include "cangjie/CHIR/IR/IntrinsicKind.h"
#include "cangjie/Utils/ConstantsUtils.h"

using namespace Cangjie;
using namespace CHIR;
using namespace Interpreter;

bool BCHIRInterpreter::InterpretIntrinsic0()
{
    CJC_ASSERT(static_cast<OpCode>(bchir.Get(pc)) == OpCode::INTRINSIC0 ||
        static_cast<OpCode>(bchir.Get(pc)) == OpCode::INTRINSIC0_EXC);
    // INTRINSIC :: INTRINSIC_KIND
    auto opIdx = pc++;
    auto intrinsicKind = static_cast<IntrinsicKind>(bchir.Get(pc++));
    switch (intrinsicKind) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        case RAW_ARRAY_REFEQ:
            [[fallthrough]];
        case FUNC_REFEQ:
            [[fallthrough]];
#endif
        case OBJECT_REFEQ:
            InterpretRefEq();
            return false;
        default: {
            auto intrinsic = static_cast<size_t>(intrinsicKind);
            std::string concurrencyKeyword =
                intrinsicKind >= GET_THREAD_OBJECT && intrinsicKind <= SET_THREAD_OBJECT ? "concurrency " : "";
            std::string errorMSg = "interpreter does not support " + concurrencyKeyword + "intrinsic function " +
                std::to_string(intrinsic);
            FailWith(opIdx, errorMSg, DiagKind::interp_unsupported, "InterpretIntrinsic",
                GetOpCodeLabel(OpCode::INTRINSIC0));
            return true;
        }
    }
}

bool BCHIRInterpreter::InterpretIntrinsic1()
{
    CJC_ASSERT(static_cast<OpCode>(bchir.Get(pc)) == OpCode::INTRINSIC1 ||
        static_cast<OpCode>(bchir.Get(pc)) == OpCode::INTRINSIC1_EXC);
    // INTRINSIC1 :: INTRINSIC_KIND :: AUX_INFO1
    auto opIdx = pc;
    // skip pc
    pc++;
    // get and skip intrinsic kind
    auto intrinsicKind = static_cast<IntrinsicKind>(bchir.Get(pc++));
    // skip auxiliary type info
    pc++;

    switch (intrinsicKind) {
        // There's no need for these functions to be INTRINSIC1 instead of INTRINSIC0.
        // We just mark them as INTRINSIC1 in CHIR2BCHIR to know that the dummy function argument
        // needs to be popped from the argument stack. Revert commit once the functions from
        // syscallIntrinsicMap are marked as intrinsic in CHIR.
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        case CJ_CORE_CAN_USE_SIMD:
            InterpretCJCodeCanUseSIMD();
            return false;
#endif
        case ARRAY_GET_UNCHECKED:
            return InterpretArrayGetIntrinsic(opIdx, false);
        default: {
            auto intrinsic = static_cast<size_t>(intrinsicKind);
            std::string concurrencyKeyword =
                intrinsicKind >= GET_THREAD_OBJECT && intrinsicKind <= SET_THREAD_OBJECT ? "concurrency " : "";
            std::string errorMSg = "interpreter does not support " + concurrencyKeyword + "intrinsic function " +
                std::to_string(intrinsic);
            FailWith(opIdx, errorMSg, DiagKind::interp_unsupported, "InterpretIntrinsic",
                GetOpCodeLabel(OpCode::INTRINSIC1));
            return true;
        }
    }
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void BCHIRInterpreter::InterpretCJCodeCanUseSIMD()
{
    // this is an hack to remove the dummy func value
    interpStack.ArgsRemove(1);
    static int8_t simdSUPPORT = -1;
    if (simdSUPPORT < 0) {
#if defined(__linux__) || defined(__APPLE__)
#if defined(__x86_64__)
        __builtin_cpu_init();
        simdSUPPORT = __builtin_cpu_supports("avx") && __builtin_cpu_supports("avx2");
#elif defined(__aarch64__)
        simdSUPPORT = 1;
#else
        simdSUPPORT = 0;
#endif
#else
        simdSUPPORT = 0;
#endif
    }
    bool ret = (simdSUPPORT > 0);
    interpStack.ArgsPush(IValUtils::PrimitiveValue<IBool>(ret));
}
#endif

bool BCHIRInterpreter::InterpretArrayGetIntrinsic(Bchir::ByteCodeIndex idx, bool indexCheck)
{
    auto popIndex = interpStack.ArgsPop<IInt64>();
    auto arrayPtr = interpStack.ArgsPop<IPointer>();
    return InterpretArrayGet(idx, indexCheck, arrayPtr, popIndex.content);
}

bool BCHIRInterpreter::InterpretArrayGet(
    Bchir::ByteCodeIndex idx, bool indexCheck, IPointer& arrayPtr, int64_t argIndex)
{
    (void)idx;
    CJC_ASSERT(!indexCheck);
    if (auto array = IValUtils::GetIf<IArray>(arrayPtr.content)) { // this is a normal CHIR array
        CJC_ASSERT(argIndex >= 0);
        auto element = array->content[static_cast<size_t>(argIndex) + 1];
        interpStack.ArgsPushIVal(std::move(element));
    }
    return false;
}

void BCHIRInterpreter::InterpretRefEq()
{
    auto v1 = interpStack.ArgsPopIVal();
    auto v2 = interpStack.ArgsPopIVal();
    bool temp;
    if (IValUtils::GetIf<INullptr>(&v1) != nullptr) {
        temp = IValUtils::GetIf<INullptr>(&v2) != nullptr;
    } else if (IValUtils::GetIf<INullptr>(&v2) != nullptr) {
        temp = false;
    } else {
        temp = IValUtils::GetIf<IPointer>(&v1)->content == IValUtils::GetIf<IPointer>(&v2)->content;
    }
    interpStack.ArgsPush(IValUtils::PrimitiveValue<IBool>(temp));
}
