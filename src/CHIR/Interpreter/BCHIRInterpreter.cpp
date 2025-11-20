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

#include "cangjie/CHIR/Interpreter/BCHIRInterpreter.h"
#include <securec.h>

using namespace Cangjie::CHIR::Interpreter;

const IVal& BCHIRInterpreter::PeekValueOfGlobal(Bchir::VarIdx id) const
{
    return env.PeekGlobal(id);
}

void BCHIRInterpreter::SetGlobalVars(std::unordered_map<Bchir::ByteCodeIndex, IVal>&& gVarInitIVals)
{
    for (auto& [id, iVal] : gVarInitIVals) {
        env.SetGlobal(id, std::move(iVal));
    }
}

const Bchir& BCHIRInterpreter::GetBchir() const
{
    return bchir;
}

void BCHIRInterpreter::Interpret()
{
    CJC_ASSERT(pc == baseIndex);
    // no bound variables in the top-level thunk
    while (!interpreterError) {
        auto current = static_cast<OpCode>(bchir.Get(pc));
#ifndef NDEBUG
        PrintDebugInfo(pc);
#endif

        // when interpreting an X_EXC operation
        // 1. set pcExcOffset to 1
        // 2. interpret operation X
        //
        // after interpreting X operation
        // 1. increment pc taking into account pcExcOffset
        //
        // basically when updating pc after interpreting X,
        // pcExcOffset is going to 0 if entry point was X or 1 is entry point was X_EXC
        Bchir::ByteCodeIndex pcExcOffset{0};
        switch (current) {
            case OpCode::ALLOCATE_RAW_ARRAY: {
                InterpretAllocateRawArray<false, false>();
                continue;
            }
            case OpCode::ALLOCATE_EXC:
                pcExcOffset = 1;
                // intended missing break
                // for the time being allocate never raises exception
            case OpCode::ALLOCATE: {
                auto ptr = IPointer();
                ptr.content = AllocateValue(INullptr());
                interpStack.ArgsPush(ptr);
                pc += 1 + pcExcOffset;
                continue;
            }
            case OpCode::ALLOCATE_STRUCT_EXC:
                pcExcOffset = 1;
                // intended missing break
                // for the time being allocate never raises exception
            case OpCode::ALLOCATE_STRUCT: {
                auto numField = bchir.Get(pc + 1);
                std::vector<IVal> content;
                for (size_t i = 0; i < numField; i++) {
                    content.emplace_back(INullptr());
                }
                auto ptr = IPointer();
                ptr.content = AllocateValue(ITuple{std::move(content)});
                interpStack.ArgsPush(ptr);
                pc += Bchir::FLAG_TWO + pcExcOffset;
                continue;
            }
            case OpCode::ALLOCATE_CLASS_EXC:
                pcExcOffset = 1;
                // intended missing break
                // for the time being allocate never raises exception
            case OpCode::ALLOCATE_CLASS: {
                auto classId = bchir.Get(pc + 1);
                auto numField = bchir.Get(pc + Bchir::FLAG_TWO);
                std::vector<IVal> content;
                for (size_t i = 0; i < numField; i++) {
                    content.emplace_back(INullptr());
                }
                auto ptr = IPointer();
                ptr.content = AllocateValue(IObject{classId, std::move(content)});
                interpStack.ArgsPush(ptr);
                pc += Bchir::FLAG_THREE + pcExcOffset;
                continue;
            }
            case OpCode::FRAME: {
                auto num = bchir.Get(pc + 1);
                env.AllocateLocalVarsForFrame(static_cast<size_t>(num));
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::LVAR: {
                auto varIdx = pc + 1;
                auto var = bchir.Get(varIdx);
                // update pc for next operation
                pc += Bchir::FLAG_TWO;

                interpStack.ArgsPushIValRef(env.GetLocal(var));
                continue;
            }
            case OpCode::GVAR: {
                auto varId = bchir.Get(pc + 1);
                auto& val = env.GetGlobal(varId);
                interpStack.ArgsPush(IPointer{&val});
                // update pc for next operation
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::GVAR_SET: {
                auto varId = bchir.Get(pc + 1);
                env.SetGlobal(varId, interpStack.ArgsPopIVal());
                pc = pc + 1 + 1;
                continue;
            }
            case OpCode::LVAR_SET: {
                auto varId = bchir.Get(pc + 1);
                env.SetLocal(varId, interpStack.ArgsPopIVal());
                pc = pc + 1 + 1;
                continue;
            }
            case OpCode::UINT8: {
                auto valIdx = pc + 1;
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IUInt8>(static_cast<uint8_t>(bchir.Get(valIdx))));
                // update pc for next operation
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::UINT16: {
                auto valIdx = pc + 1;
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IUInt16>(static_cast<uint16_t>(bchir.Get(valIdx))));
                // update pc for next operation
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::UINT32: {
                auto valIdx = pc + 1;
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IUInt32>(static_cast<uint32_t>(bchir.Get(valIdx))));
                // update pc for next operation
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::UINT64: {
                auto valIdx = pc + 1;
                interpStack.ArgsPush(
                    IValUtils::PrimitiveValue<IUInt64>(static_cast<uint64_t>(bchir.Get8bytes(valIdx))));
                // update pc for next operation
                pc += Bchir::FLAG_THREE;
                continue;
            }
            case OpCode::UINTNAT: {
                auto valIdx = pc + 1;
#if (defined(__x86_64__) || defined(__aarch64__))
                interpStack.ArgsPush(
                    IValUtils::PrimitiveValue<IUIntNat>(static_cast<uint64_t>(bchir.Get8bytes(valIdx))));
#else
                interpStack.ArgsPush(
                    IValUtils::PrimitiveValue<IUIntNat>(static_cast<uint32_t>(bchir.Get8bytes(valIdx))));
#endif
                // update pc for next operation
                pc += Bchir::FLAG_THREE;
                continue;
            }
            case OpCode::INT8: {
                auto valIdx = pc + 1;
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IInt8>(static_cast<int8_t>(bchir.Get(valIdx))));
                // update pc for next operation
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::INT16: {
                auto valIdx = pc + 1;
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IInt16>(static_cast<int16_t>(bchir.Get(valIdx))));
                // update pc for next operation
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::INT32: {
                auto valIdx = pc + 1;
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IInt32>(static_cast<int32_t>(bchir.Get(valIdx))));
                // update pc for next operation
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::INT64: {
                auto valIdx = pc + 1;
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IInt64>(static_cast<int64_t>(bchir.Get8bytes(valIdx))));
                // update pc for next operation
                pc += Bchir::FLAG_THREE;
                continue;
            }
            case OpCode::INTNAT: {
                auto valIdx = pc + 1;
#if (defined(__x86_64__) || defined(__aarch64__))
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IIntNat>(static_cast<int64_t>(bchir.Get8bytes(valIdx))));
#else
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IIntNat>(static_cast<int32_t>(bchir.Get8bytes(valIdx))));
#endif
                // update pc for next operation
                pc += Bchir::FLAG_THREE;
                continue;
            }
            case OpCode::FLOAT16: {
                auto valIdx = pc + 1;
                auto tmp = bchir.Get(valIdx);
                // Value for a FLOAT16 instruction is a 32-bit float.
                float f;
                auto ret = memcpy_s(&f, sizeof(f), &tmp, sizeof(tmp));
                if (ret != EOK) {
                    return;
                }
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IFloat16>(static_cast<float>(f)));
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::FLOAT32: {
                auto valIdx = pc + 1;
                auto tmp = bchir.Get(valIdx);
                float f;
                auto ret = memcpy_s(&f, sizeof(f), &tmp, sizeof(tmp));
                if (ret != EOK) {
                    return;
                }
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IFloat32>(static_cast<float>(f)));
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::FLOAT64: {
                auto valIdx = pc + 1;
                auto tmp = bchir.Get8bytes(valIdx);
                double d;
                auto ret = memcpy_s(&d, sizeof(d), &tmp, sizeof(double));
                if (ret != EOK) {
                    return;
                }
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IFloat64>(d));
                pc += Bchir::FLAG_THREE;
                continue;
            }
            case OpCode::RUNE: {
                auto valIdx = pc + 1;
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IRune>(bchir.Get(static_cast<char32_t>(valIdx))));
                // update pc for next operation
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::BOOL: {
                auto valIdx = pc + 1;
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IBool>(bchir.Get(valIdx)));
                // update pc for next operation
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::UNIT: {
                interpStack.ArgsPush(IUnit());
                // update pc for next operation
                pc += 1;
                continue;
            }
            case OpCode::NULLPTR: {
                interpStack.ArgsPush(INullptr());
                // update pc for next operation
                pc += 1;
                continue;
            }
            case OpCode::STRING: {
                InterpretString();
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::TUPLE: {
                auto sizeIdx = pc + 1;
                auto size = bchir.Get(sizeIdx);
                pc = sizeIdx + 1;

                auto tuple = ITuple();
                interpStack.ArgsPop(size, tuple.content);
                interpStack.ArgsPush(std::move(tuple));
                continue;
            }
            case OpCode::VARRAY: {
                auto sizeIdx = pc + 1;
                auto size = bchir.Get(sizeIdx);
                auto array = IArray();
                interpStack.ArgsPop(size, array.content);
                interpStack.ArgsPush(std::move(array));
                pc = sizeIdx + 1;
                continue;
            }
            case OpCode::VARRAY_GET: {
                InterpretVArrayGet();
                continue;
            }
            case OpCode::RAW_ARRAY_LITERAL_INIT: {
                InterpretRawArrayLiteralInit();
                continue;
            }
            case OpCode::FUNC: {
                // FUNC :: THUNK_IDX :: NEXT_OP
                auto thunkIdx = pc + 1;
                auto func = IFunc{bchir.Get(thunkIdx)};
                interpStack.ArgsPush(func);
                pc = thunkIdx + 1;
                continue;
            }
            case OpCode::RETURN: {
                InterpretReturn();
                continue;
            }
            case OpCode::EXIT: {
                // we are done
                return;
            }
            case OpCode::DROP: {
                interpStack.ArgsPopBack();
                pc += 1;
                continue;
            }
            case OpCode::JUMP: {
                pc = bchir.Get(pc + 1);
                continue;
            }
            case OpCode::BRANCH: {
                auto cond = interpStack.ArgsPop<IBool>();
                if (cond.content) {
                    pc = bchir.Get(pc + 1);
                } else {
                    pc = bchir.Get(pc + Bchir::FLAG_TWO);
                }
                continue;
            }
            case OpCode::UN_NEG_EXC: {
                BinOp<OpCode::UN_NEG_EXC>();
                continue;
            }
            case OpCode::BIN_ADD_EXC: {
                BinOp<OpCode::BIN_ADD_EXC>();
                continue;
            }
            case OpCode::BIN_SUB_EXC: {
                BinOp<OpCode::BIN_SUB_EXC>();
                continue;
            }
            case OpCode::BIN_MUL_EXC: {
                BinOp<OpCode::BIN_MUL_EXC>();
                continue;
            }
            case OpCode::BIN_DIV_EXC: {
                BinOp<OpCode::BIN_DIV_EXC>();
                continue;
            }
            case OpCode::BIN_MOD_EXC: {
                BinOp<OpCode::BIN_MOD_EXC>();
                continue;
            }
            case OpCode::BIN_EXP_EXC: {
                BinOp<OpCode::BIN_EXP_EXC>();
                continue;
            }
            case OpCode::BIN_LSHIFT_EXC: {
                BinOp<OpCode::BIN_LSHIFT_EXC>();
                continue;
            }
            case OpCode::BIN_RSHIFT_EXC: {
                BinOp<OpCode::BIN_RSHIFT_EXC>();
                continue;
            }
            case OpCode::UN_NEG: {
                BinOp<OpCode::UN_NEG>();
                continue;
            }
            case OpCode::UN_DEC: {
                BinOp<OpCode::UN_DEC>();
                continue;
            }
            case OpCode::UN_INC: {
                BinOp<OpCode::UN_INC>();
                continue;
            }
            case OpCode::UN_NOT: {
                BinOpFixedBool<OpCode::UN_NOT>();
                continue;
            }
            case OpCode::UN_BITNOT: {
                BinOp<OpCode::UN_BITNOT>();
                continue;
            }
            case OpCode::BIN_ADD: {
                BinOp<OpCode::BIN_ADD>();
                continue;
            }
            case OpCode::BIN_SUB: {
                BinOp<OpCode::BIN_SUB>();
                continue;
            }
            case OpCode::BIN_MUL: {
                BinOp<OpCode::BIN_MUL>();
                continue;
            }
            case OpCode::BIN_DIV: {
                BinOp<OpCode::BIN_DIV>();
                continue;
            }
            case OpCode::BIN_MOD: {
                BinOp<OpCode::BIN_MOD>();
                continue;
            }
            case OpCode::BIN_EXP: {
                BinOp<OpCode::BIN_EXP>();
                continue;
            }
            case OpCode::BIN_LT: {
                BinOp<OpCode::BIN_LT>();
                continue;
            }
            case OpCode::BIN_GT: {
                BinOp<OpCode::BIN_GT>();
                continue;
            }
            case OpCode::BIN_LE: {
                BinOp<OpCode::BIN_LE>();
                continue;
            }
            case OpCode::BIN_GE: {
                BinOp<OpCode::BIN_GE>();
                continue;
            }
            case OpCode::BIN_NOTEQ: {
                BinOp<OpCode::BIN_NOTEQ>();
                continue;
            }
            case OpCode::BIN_EQUAL: {
                BinOp<OpCode::BIN_EQUAL>();
                continue;
            }
            case OpCode::BIN_BITAND: {
                BinOp<OpCode::BIN_BITAND>();
                continue;
            }
            case OpCode::BIN_BITOR: {
                BinOp<OpCode::BIN_BITOR>();
                continue;
            }
            case OpCode::BIN_BITXOR: {
                BinOp<OpCode::BIN_BITXOR>();
                continue;
            }
            case OpCode::BIN_LSHIFT: {
                BinOp<OpCode::BIN_LSHIFT>();
                continue;
            }
            case OpCode::BIN_RSHIFT: {
                BinOp<OpCode::BIN_RSHIFT>();
                continue;
            }
            case OpCode::FIELD_TPL: {
                InterpretFieldTpl();
                continue;
            }
            case OpCode::FIELD: {
                auto fieldIdx = pc + 1;
                auto field = bchir.Get(fieldIdx);
                // OPTIMIZE
                auto arg = interpStack.ArgsPopIVal();
                if (auto tuple = IValUtils::GetIf<ITuple>(&arg)) {
                    interpStack.ArgsPushIVal(std::move(tuple->content[field]));
                } else {
                    CJC_ASSERT(std::holds_alternative<IObject>(arg));
                    auto object = IValUtils::Get<IObject>(std::move(arg));
                    // -1 because we don't have the class node anymore
                    interpStack.ArgsPushIVal(std::move(object.content[field - 1]));
                }
                pc = fieldIdx + 1;
                continue;
            }
            case OpCode::INVOKE_EXC: {
                InterpretInvoke<OpCode::INVOKE_EXC>();
                continue;
            }
            case OpCode::INVOKE: {
                InterpretInvoke<OpCode::INVOKE>();
                continue;
            }
            case OpCode::TYPECAST: {
                InterpretTypeCast();
                if (raiseExnToTopLevel) {
                    return;
                }
                continue;
            }
            case OpCode::INSTANCEOF: {
                auto ptr = interpStack.ArgsPop<IPointer>();
                auto& obj = IValUtils::Get<IObject>(*ptr.content);
                auto lhs = obj.classId;
                auto rhs = bchir.Get(pc + 1);
                interpStack.ArgsPush(IBool{IsSubclass(lhs, rhs)});
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::BOX: {
                auto classId = bchir.Get(pc + 1);
                std::vector<IVal> content;
                interpStack.ArgsPop(1, content);
                auto ptr = IPointer();
                ptr.content = AllocateValue(IObject{classId, std::move(content)});
                interpStack.ArgsPush(std::move(ptr));
                pc += Bchir::FLAG_TWO;
                continue;
            }
            case OpCode::UNBOX: {
                auto ptr = interpStack.ArgsPop<IPointer>();
                auto& obj = IValUtils::Get<IObject>(*ptr.content);
                auto value = obj.content[0];
                interpStack.ArgsPushIVal(std::move(value));
                pc++;
                continue;
            }
            case OpCode::UNBOX_REF: {
                auto ptr = interpStack.ArgsPop<IPointer>();
                auto& obj = IValUtils::Get<IObject>(*ptr.content);
                ptr.content = &obj.content[0]; // reusing ptr
                interpStack.ArgsPush(std::move(ptr));
                pc++;
                continue;
            }
            case OpCode::APPLY: {
                InterpretApply<OpCode::APPLY>();
                continue;
            }
            case OpCode::APPLY_EXC: {
                InterpretApply<OpCode::APPLY_EXC>();
                continue;
            }
            case OpCode::ASG: {
                auto ptr = interpStack.ArgsPop<IPointer>();
                auto value = interpStack.ArgsPopIVal();
                *ptr.content = std::move(value);
                interpStack.ArgsPush(IUnit());
                pc += 1;
                continue;
            }
            case OpCode::STOREINREF: {
                InterpretStoreInRef();
                continue;
            }
            case OpCode::STORE: {
                auto ptr = interpStack.ArgsPop<IPointer>();
                auto value = interpStack.ArgsPopIVal();
                *ptr.content = std::move(value);
                pc += 1;
                continue;
            }
            case OpCode::DEREF: {
                InterpretDeref();
                continue;
            }
            case OpCode::INTRINSIC0: {
                InterpretIntrinsic<OpCode::INTRINSIC0>();
                if (raiseExnToTopLevel) {
                    return;
                }
                continue;
            }
            case OpCode::INTRINSIC1: {
                InterpretIntrinsic<OpCode::INTRINSIC1>();
                if (raiseExnToTopLevel) {
                    return;
                }
                continue;
            }
            case OpCode::SWITCH: {
                InterpretSwitch();
                continue;
            }
            case OpCode::GETREF: {
                InterpretGetRef();
                continue;
            }
            case OpCode::SYSCALL:
            case OpCode::CAPPLY:
            case OpCode::ABORT: {
                if (!isConstEval) {
                    FailWith(pc, "operation not currently supported in const eval", DiagKind::const_eval_unsupported);
                }
                interpreterError = true;
                return;
            }
            case OpCode::SPAWN:
            case OpCode::NOT_SUPPORTED:
            case OpCode::INVALID:
            default: {
                FailWith(pc, "operation not currently supported in interpreter", DiagKind::interp_unsupported,
                    "Interpret", GetOpCodeLabel(current));
                CJC_ABORT();
            }
        }
    }
}

void BCHIRInterpreter::InterpretString()
{
    // String values in the interpreter must match the definition of strings in the core library
    /* Possible optimization.
    Instead of having a section of std::strings literals and converting them to match
    core library RawArray<UInt8> during interpretation, we can have a section of
    IArray strings, and during interpretation we just create an IPtr pointing to the
    corresponding IArray string. This is correct because strings are immutable in CJ.
    Also, the Unicode size of the string should be calculated on translation */
    auto strIdxIdx = pc + 1;
    auto strIdx = bchir.Get(strIdxIdx);
    auto& str = bchir.GetString(strIdx);
    auto array = IValUtils::StringToArray(str);
    auto ptr = IPointer();
    ptr.content = AllocateValue(std::move(array));

    // The size is not the size of the array, but the
    // amount of Unicode characters that it represents
    int64_t realSize = 0;
    for (auto& c : str) {
        if ((c & 0xC0) != 0x80) {
            realSize++;
        }
    }

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    (void)realSize;
    auto tuple = ITuple{{ptr, IValUtils::PrimitiveValue<IUInt32>(uint32_t(0)),
        IValUtils::PrimitiveValue<IUInt32>(static_cast<uint32_t>(str.size()))}};
#endif
    interpStack.ArgsPush(std::move(tuple));
}

void BCHIRInterpreter::InterpretStoreInRef()
{
    // ASG_TPL | ASG_OBJ :: FIXED_ARGS :: SIZE_OF_PATH :: p1 :: ...
    pc++;
    auto target = interpStack.ArgsPop<IPointer>();
    auto targetVal = target.content;
    auto value = interpStack.ArgsPopIVal();
    auto pathSize = bchir.Get(pc++);
    for (uint32_t p = 0; p < pathSize; ++p) {
        // OPTIMIZE: we can prolly just use ITuple to represent IObject as well and so we dont need
        // this check
        if (auto tuple = IValUtils::GetIf<ITuple>(targetVal)) {
            targetVal = &tuple->content[bchir.Get(pc + p)];
        } else if (auto object = IValUtils::GetIf<IObject>(targetVal)) {
            // -1 because we don't have the class node anymore
            targetVal = &object->content[bchir.Get(pc + p)];
        } else if (auto arr = IValUtils::GetIf<IArray>(targetVal)) {
            // + 1 because content[0] is the array size.
            auto index = bchir.Get(pc + p) + static_cast<uint32_t>(1);
            CJC_ASSERT(index < arr->content.size());
            targetVal = &arr->content[index];
        } else {
            CJC_ABORT();
        }
    }
    *targetVal = value;
    interpStack.ArgsPush(IUnit());
    pc += pathSize;
}

void BCHIRInterpreter::InterpretFieldTpl()
{
    pc++;
    auto pathSize = bchir.Get(pc++);
    auto tuple = interpStack.ArgsPop<ITuple>();
    for (Bchir::ByteCodeContent i = 0; i < pathSize - 1; ++i) {
        auto innerTpl = std::move(tuple.content[static_cast<size_t>(bchir.Get(pc++))]);
        tuple = std::move(IValUtils::Get<ITuple>(innerTpl));
    }
    interpStack.ArgsPushIVal(std::move(tuple.content[static_cast<size_t>(bchir.Get(pc++))]));
}

void BCHIRInterpreter::InterpretReturn()
{
    auto ctrl = interpStack.CtrlPop();
    auto opCode = static_cast<OpCode>(bchir.Get(ctrl.byteCodePtr));
    CJC_ASSERT(opCode == OpCode::CAPPLY || opCode == OpCode::APPLY || opCode == OpCode::INVOKE ||
        opCode == OpCode::APPLY_EXC || opCode == OpCode::INVOKE_EXC);
    // it might happen that a function doesn't have any variables/atoms
    CJC_ASSERT(ctrl.envBP <= env.GetBP());
    env.RestoreStackFrameTo(ctrl.envBP);
    switch (static_cast<OpCode>(bchir.Get(ctrl.byteCodePtr))) {
        case OpCode::CAPPLY: {
            auto numberOfArgs = bchir.Get(ctrl.byteCodePtr + 1);
            // in the CAPPLY we need to skip the type of the result and the type of the arguments
            // ctrl.byteCodePtr + NUMBER_OF_ARGS + ARG_TY_1 + ... + ARG_TY_N + 1
            pc = ctrl.byteCodePtr + 1 + numberOfArgs + 1 + 1;
            break;
        }
        case OpCode::INVOKE_EXC: {
            // skip the index of the exception target bb
            ctrl.byteCodePtr += 1;
            // intended missing break
            [[fallthrough]];
        }
        case OpCode::INVOKE: {
            // ctrl.byteCodePtr + NUMBER_OF_ARGS + 1
            pc = ctrl.byteCodePtr + 1 + 1 + 1;
            break;
        }
        case OpCode::APPLY_EXC: {
            // skip the index of the exception target bb
            ctrl.byteCodePtr += 1;
            // intended missing break
            [[fallthrough]];
        }
        case OpCode::APPLY: {
            // ctrl.byteCodePtr + NUMBER_OF_ARGS + 1
            pc = ctrl.byteCodePtr + 1 + 1;
            break;
        }
        default: {
            CJC_ASSERT(false);
        }
    }
    return;
}

void BCHIRInterpreter::RaiseError(Bchir::ByteCodeIndex, const std::string&)
{
    interpreterError = true;
}

void BCHIRInterpreter::RaiseArithmeticExceptionMsg(Bchir::ByteCodeIndex sourcePc, const std::string& str)
{
    ReportConstEvalException(sourcePc, "ArithmeticException: " + str);
    interpreterError = true;
}

void BCHIRInterpreter::RaiseArithmeticException(Bchir::ByteCodeIndex sourcePc)
{
    ReportConstEvalException(sourcePc, "ArithmeticException");
}

void BCHIRInterpreter::RaiseOverflowException(Bchir::ByteCodeIndex sourcePc)
{
    ReportConstEvalException(sourcePc, "OverflowException");
}

void BCHIRInterpreter::RaiseIndexOutOfBoundsException(Bchir::ByteCodeIndex sourcePc)
{
    ReportConstEvalException(sourcePc, "IndexOutOfBoundsException");
}

void BCHIRInterpreter::RaiseNegativeArraySizeException(Bchir::ByteCodeIndex sourcePc)
{
    ReportConstEvalException(sourcePc, "NegativeArraySizeException");
}

void BCHIRInterpreter::RaiseOutOfMemoryError(Bchir::ByteCodeIndex sourcePc)
{
    ReportConstEvalException(sourcePc, "OutOfMemoryError");
}

// HACK: Work around exceptions not being const
void BCHIRInterpreter::ReportConstEvalException(Bchir::ByteCodeIndex opIdx, std::string exceptionName)
{
    interpreterError = true;
    Cangjie::Position errorPosition = DEFAULT_POSITION;
    auto applyPosition = bchir.GetLinkedByteCode().GetCodePositionAnnotation(opIdx);
    auto fileName = bchir.GetFileName(applyPosition.fileID);
    auto fileId = diag.GetSourceManager().GetFileID(fileName);
    if (fileId != -1 && !diag.GetSourceManager().GetSource(static_cast<unsigned int>(fileId)).buffer.empty()) {
        errorPosition = Cangjie::Position(static_cast<unsigned int>(fileId), static_cast<int>(applyPosition.line),
            static_cast<int>(applyPosition.column));
    }
    auto error = diag.Diagnose(errorPosition, DiagKind::const_eval_exception);
    error.AddNote(exceptionName);
}

void BCHIRInterpreter::InterpretGetRef()
{
    // GETREF :: SIZE_OF_PATH :: p1 :: ...
    pc += 1;
    auto target = interpStack.ArgsPop<IPointer>();
    auto targetVal = target.content;
    auto pathSize = bchir.Get(pc++);
    for (size_t p = 0; p < pathSize; ++p) {
        if (auto object = IValUtils::GetIf<IObject>(targetVal)) {
            targetVal = &object->content[bchir.Get(static_cast<unsigned>(pc + p))];
        } else if (auto tuple = IValUtils::GetIf<ITuple>(targetVal)) {
            targetVal = &tuple->content[bchir.Get(static_cast<unsigned>(pc + p))];
        } else {
            auto array = IValUtils::GetIf<IArray>(targetVal);
            CJC_ASSERT(array != nullptr);
            targetVal = &array->content[bchir.Get(pc + static_cast<Bchir::ByteCodeIndex>(p)) +
                1]; // index 0 of array is the size of the array
        }
    }

    auto ptr = IPointer();
    ptr.content = targetVal;
    interpStack.ArgsPush(ptr);
    pc += pathSize;
}

template <typename SourceTyRaw, typename TargetTy, typename TargetTyRaw>
bool BCHIRInterpreter::CastOrRaiseExceptionForInt(SourceTyRaw v, OverflowStrategy strategy, Bchir::ByteCodeIndex opIdx)
{
    TargetTyRaw res = 0;
    bool isOverflow = CHIR::OverflowChecker::IsTypecastOverflowForInt<SourceTyRaw, TargetTyRaw>(v, &res, strategy);
    if (isOverflow && strategy == OverflowStrategy::THROWING) {
        RaiseOverflowException(opIdx);
        return true;
    } else {
        interpStack.ArgsPush(IValUtils::PrimitiveValue<TargetTy, TargetTyRaw>(res));
        return false;
    }
}

template <typename T, typename K> static bool IsTypecastOverflowForFloat(T x, K* res)
{
    CJC_NULLPTR_CHECK(res);
    CJC_ASSERT((std::is_same<T, double>::value) || (std::is_same<T, float>::value));
    bool bMax = x > static_cast<double>(std::numeric_limits<K>::max());
    bool bMin = x < static_cast<double>(std::numeric_limits<K>::min());
    bool isOverflow = bMax || bMin;
    *res = static_cast<K>(x);
    return isOverflow;
}

template <typename SourceTyRaw, typename TargetTy, typename TargetTyRaw>
bool BCHIRInterpreter::CastOrRaiseExceptionForFloat(SourceTyRaw floatVal, Bchir::ByteCodeIndex opIdx)
{
    CJC_ASSERT((std::is_same<SourceTyRaw, double>::value) || (std::is_same<SourceTyRaw, float>::value));
    TargetTyRaw res = 0;
    bool isOverflow = IsTypecastOverflowForFloat<SourceTyRaw, TargetTyRaw>(floatVal, &res);
    // overflow semantics for floats is always THROWING
    if (isOverflow) {
        RaiseOverflowException(opIdx);
        return true;
    } else {
        interpStack.ArgsPush(IValUtils::PrimitiveValue<TargetTy, TargetTyRaw>(res));
        return false;
    }
}

template <typename T>
bool BCHIRInterpreter::InterpretTypeCastForInt(
    T val, CHIR::Type::TypeKind targetKind, OverflowStrategy strategy, Bchir::ByteCodeIndex opIdx)
{
    switch (targetKind) {
        case CHIR::Type::TypeKind::TYPE_RUNE:
            return CastOrRaiseExceptionForInt<T, IRune, uint32_t>(val, strategy, opIdx);
        case CHIR::Type::TypeKind::TYPE_INT8:
            return CastOrRaiseExceptionForInt<T, IInt8, int8_t>(val, strategy, opIdx);
        case CHIR::Type::TypeKind::TYPE_INT16:
            return CastOrRaiseExceptionForInt<T, IInt16, int16_t>(val, strategy, opIdx);
        case CHIR::Type::TypeKind::TYPE_INT32:
            return CastOrRaiseExceptionForInt<T, IInt32, int32_t>(val, strategy, opIdx);
        case CHIR::Type::TypeKind::TYPE_INT64:
            return CastOrRaiseExceptionForInt<T, IInt64, int64_t>(val, strategy, opIdx);
        case CHIR::Type::TypeKind::TYPE_INT_NATIVE:
#if (defined(__x86_64__) || defined(__aarch64__))
            return CastOrRaiseExceptionForInt<T, IIntNat, int64_t>(val, strategy, opIdx);
#else
            return CastOrRaiseExceptionForInt<T, IIntNat, int32_t>(val, strategy, opIdx);
#endif
        case CHIR::Type::TypeKind::TYPE_UINT8:
            return CastOrRaiseExceptionForInt<T, IUInt8, uint8_t>(val, strategy, opIdx);
        case CHIR::Type::TypeKind::TYPE_UINT16:
            return CastOrRaiseExceptionForInt<T, IUInt16, uint16_t>(val, strategy, opIdx);
        case CHIR::Type::TypeKind::TYPE_UINT32:
            return CastOrRaiseExceptionForInt<T, IUInt32, uint32_t>(val, strategy, opIdx);
        case CHIR::Type::TypeKind::TYPE_UINT64:
            return CastOrRaiseExceptionForInt<T, IUInt64, uint64_t>(val, strategy, opIdx);
        case CHIR::Type::TypeKind::TYPE_UINT_NATIVE:
            return CastOrRaiseExceptionForInt<T, IUIntNat, size_t>(val, strategy, opIdx);
        case CHIR::Type::TypeKind::TYPE_FLOAT16:
            interpStack.ArgsPush(IValUtils::PrimitiveValue<IFloat16>(static_cast<float>(val)));
            return false;
        case CHIR::Type::TypeKind::TYPE_FLOAT32:
            interpStack.ArgsPush(IValUtils::PrimitiveValue<IFloat32>(static_cast<float>(val)));
            return false;
        case CHIR::Type::TypeKind::TYPE_FLOAT64:
            interpStack.ArgsPush(IValUtils::PrimitiveValue<IFloat64>(static_cast<double>(val)));
            return false;
        default: {
            FailWith(opIdx, "interpreter cannot perform integer typecast", DiagKind::interp_cannot_interp_node,
                "InterpretTypeCastForInt");
            return true;
        }
    }
}

template <typename T>
bool BCHIRInterpreter::InterpretTypeCastForFloat(
    T floatVal, CHIR::Type::TypeKind targetKind, Bchir::ByteCodeIndex opIdx)
{
    CJC_ASSERT((std::is_same<T, double>::value) || (std::is_same<T, float>::value));
    switch (targetKind) {
        case CHIR::Type::TypeKind::TYPE_INT8:
            return CastOrRaiseExceptionForFloat<T, IInt8, int8_t>(floatVal, opIdx);
        case CHIR::Type::TypeKind::TYPE_INT16:
            return CastOrRaiseExceptionForFloat<T, IInt16, int16_t>(floatVal, opIdx);
        case CHIR::Type::TypeKind::TYPE_INT32:
            return CastOrRaiseExceptionForFloat<T, IInt32, int32_t>(floatVal, opIdx);
        case CHIR::Type::TypeKind::TYPE_INT64:
            return CastOrRaiseExceptionForFloat<T, IInt64, int64_t>(floatVal, opIdx);
        case CHIR::Type::TypeKind::TYPE_INT_NATIVE:
#if (defined(__x86_64__) || defined(__aarch64__))
            return CastOrRaiseExceptionForFloat<T, IIntNat, int64_t>(floatVal, opIdx);
#else
            return CastOrRaiseExceptionForFloat<T, IIntNat, int32_t>(floatVal, opIdx);
#endif
        case CHIR::Type::TypeKind::TYPE_UINT8:
            return CastOrRaiseExceptionForFloat<T, IUInt8, uint8_t>(floatVal, opIdx);
        case CHIR::Type::TypeKind::TYPE_UINT16:
            return CastOrRaiseExceptionForFloat<T, IUInt16, uint16_t>(floatVal, opIdx);
        case CHIR::Type::TypeKind::TYPE_UINT32:
            return CastOrRaiseExceptionForFloat<T, IUInt32, uint32_t>(floatVal, opIdx);
        case CHIR::Type::TypeKind::TYPE_UINT64:
            return CastOrRaiseExceptionForFloat<T, IUInt64, uint64_t>(floatVal, opIdx);
        case CHIR::Type::TypeKind::TYPE_UINT_NATIVE:
            return CastOrRaiseExceptionForFloat<T, IUIntNat, size_t>(floatVal, opIdx);
        case CHIR::Type::TypeKind::TYPE_FLOAT16:
            interpStack.ArgsPush(IValUtils::PrimitiveValue<IFloat16>(static_cast<float>(floatVal)));
            return false;
        case CHIR::Type::TypeKind::TYPE_FLOAT32:
            interpStack.ArgsPush(IValUtils::PrimitiveValue<IFloat32>(static_cast<float>(floatVal)));
            return false;
        case CHIR::Type::TypeKind::TYPE_FLOAT64:
            interpStack.ArgsPush(IValUtils::PrimitiveValue<IFloat64>(static_cast<double>(floatVal)));
            return false;
        default: {
            FailWith(opIdx, "interpreter cannot perform float typecast", DiagKind::interp_cannot_interp_node,
                "InterpretTypeCastForFloat");
            return true;
        }
    }
}

template <OpCode op> void BCHIRInterpreter::InterpretTypeCast()
{
    auto opIdx = pc;
    pc += 1;
    auto srcKind = static_cast<CHIR::Type::TypeKind>(bchir.Get(pc++));
    auto targetKind = static_cast<CHIR::Type::TypeKind>(bchir.Get(pc++));
    auto strat = static_cast<Cangjie::OverflowStrategy>(bchir.Get(pc++));

    bool raisedExc = false;
    switch (srcKind) {
        case CHIR::Type::TypeKind::TYPE_RUNE: {
            if (targetKind == CHIR::Type::TypeKind::TYPE_UINT32) {
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IUInt32>(interpStack.ArgsPop<IRune>().content));
            } else {
                CJC_ASSERT(targetKind == CHIR::Type::TypeKind::TYPE_UINT64);
                interpStack.ArgsPush(IValUtils::PrimitiveValue<IUInt64>(interpStack.ArgsPop<IRune>().content));
            }
            break;
        }
        case CHIR::Type::TypeKind::TYPE_INT8:
            raisedExc = InterpretTypeCastForInt(interpStack.ArgsPop<IInt8>().content, targetKind, strat, opIdx);
            break;
        case CHIR::Type::TypeKind::TYPE_INT16:
            raisedExc = InterpretTypeCastForInt(interpStack.ArgsPop<IInt16>().content, targetKind, strat, opIdx);
            break;
        case CHIR::Type::TypeKind::TYPE_INT32:
            raisedExc = InterpretTypeCastForInt(interpStack.ArgsPop<IInt32>().content, targetKind, strat, opIdx);
            break;
        case CHIR::Type::TypeKind::TYPE_INT64:
            raisedExc = InterpretTypeCastForInt(interpStack.ArgsPop<IInt64>().content, targetKind, strat, opIdx);
            break;
        case CHIR::Type::TypeKind::TYPE_INT_NATIVE:
            raisedExc = InterpretTypeCastForInt(interpStack.ArgsPop<IIntNat>().content, targetKind, strat, opIdx);
            break;
        case CHIR::Type::TypeKind::TYPE_UINT8:
            raisedExc = InterpretTypeCastForInt(interpStack.ArgsPop<IUInt8>().content, targetKind, strat, opIdx);
            break;
        case CHIR::Type::TypeKind::TYPE_UINT16:
            raisedExc = InterpretTypeCastForInt(interpStack.ArgsPop<IUInt16>().content, targetKind, strat, opIdx);
            break;
        case CHIR::Type::TypeKind::TYPE_UINT32:
            raisedExc = InterpretTypeCastForInt(interpStack.ArgsPop<IUInt32>().content, targetKind, strat, opIdx);
            break;
        case CHIR::Type::TypeKind::TYPE_UINT64:
            raisedExc = InterpretTypeCastForInt(interpStack.ArgsPop<IUInt64>().content, targetKind, strat, opIdx);
            break;
        case CHIR::Type::TypeKind::TYPE_UINT_NATIVE:
            raisedExc = InterpretTypeCastForInt(interpStack.ArgsPop<IUIntNat>().content, targetKind, strat, opIdx);
            break;
        case CHIR::Type::TypeKind::TYPE_FLOAT16:
            raisedExc = InterpretTypeCastForFloat(interpStack.ArgsPop<IFloat16>().content, targetKind, opIdx);
            break;
        case CHIR::Type::TypeKind::TYPE_FLOAT32:
            raisedExc = InterpretTypeCastForFloat(interpStack.ArgsPop<IFloat32>().content, targetKind, opIdx);
            break;
        case CHIR::Type::TypeKind::TYPE_FLOAT64:
            raisedExc = InterpretTypeCastForFloat(interpStack.ArgsPop<IFloat64>().content, targetKind, opIdx);
            break;
        case CHIR::Type::TypeKind::TYPE_BOOLEAN:
            raisedExc = InterpretTypeCastForInt(interpStack.ArgsPop<IBool>().content, targetKind, strat, opIdx);
            break;
        case CHIR::Type::TypeKind::TYPE_INVALID:
        default: {
            CJC_ABORT();
        }
    };
    (void)raisedExc;
}

template <OpCode op> void BCHIRInterpreter::InterpretApply()
{
    // APPLY :: NUMBER_OF_ARGS
    auto numberArgsIdx = pc + 1;
    size_t numberArgs = bchir.Get(numberArgsIdx);
    CJC_ASSERT(numberArgs > 0);
    CJC_ASSERT(interpStack.ArgsSize() >= numberArgs);
    // argStack = ... :: FUNC :: ARG_1 :: ... :: ARG_N
    auto func = IValUtils::Get<IFunc>(interpStack.ArgsGet(numberArgs, 0));
    auto funcThunkIdx = func.content;
    // add apply to opStack so that we know where to continue when we reach RETURN
    interpStack.CtrlPush({op, funcThunkIdx, pc, env.GetBP()});
    env.StartStackFrame();
    pc = static_cast<unsigned>(funcThunkIdx);
}

Bchir::ByteCodeIndex BCHIRInterpreter::FindMethod(Bchir::ByteCodeContent classId, Bchir::ByteCodeContent nameId)
{
    auto classInfoIt = bchir.GetClassTable().find(classId);
    CJC_ASSERT(classInfoIt != bchir.GetClassTable().end());
    auto& classInfo = classInfoIt->second;
    auto methodIt = classInfo.vtable.find(nameId);
    CJC_ASSERT(methodIt != classInfo.vtable.end());
    return methodIt->second;
}

bool BCHIRInterpreter::IsSubclass(Bchir::ByteCodeContent lhs, Bchir::ByteCodeContent rhs)
{
    if (lhs == rhs) {
        return true;
    }
    auto& classTable = bchir.GetClassTable();
    auto it = classTable.find(lhs);
    CJC_ASSERT(it != classTable.end());
    return it->second.superClasses.find(rhs) != it->second.superClasses.end();
}

template <OpCode op> void BCHIRInterpreter::InterpretInvoke()
{
    // INVOKE :: NUMBER_OF_ARGS
    auto numberArgsIdx = pc + 1;
    size_t numberArgs = bchir.Get(numberArgsIdx);
    size_t nameId = bchir.Get(numberArgsIdx + 1);
    CJC_ASSERT(numberArgs > 0);
    CJC_ASSERT(interpStack.ArgsSize() >= numberArgs);
    // argStack = ... :: DUMMY :: PTR :: ARG_1 :: ... :: ARG_N
    auto ptr = IValUtils::Get<IPointer>(interpStack.ArgsGet(numberArgs, 0));
    auto& object = IValUtils::Get<IObject>(*ptr.content);
    auto classId = object.classId;

    auto funcThunkIdx = FindMethod(classId, static_cast<unsigned>(nameId));

    // add apply to opStack so that we know where to continue when we reach RETURN
    interpStack.CtrlPush({op, funcThunkIdx, pc, env.GetBP()});
    env.StartStackFrame();
    pc = funcThunkIdx;
}

IPointer BCHIRInterpreter::ToArena(IVal&& value)
{
    auto ptr = IPointer();
    ptr.content = AllocateValue(std::move(value));
    return ptr;
}

void BCHIRInterpreter::InterpretDeref()
{
    auto ptr = interpStack.ArgsPop<IPointer>();
    pc += 1;

    interpStack.ArgsPushIValRef(*ptr.content);
}

static constexpr int INSTRUCTION_DIFF{3};
template <typename T, typename S>
bool BCHIRInterpreter::PushIfNotOverflow(bool overflow, S res, Cangjie::OverflowStrategy strat)
{
    if (overflow && strat == OverflowStrategy::THROWING) {
        // pc is already pointing to the next instruction,
        // thus sourcePc = pc - 1 (strategy) - 1 (kind idx) - 1
        RaiseOverflowException(pc - INSTRUCTION_DIFF);
        return true;
    } else if (strat == OverflowStrategy::CHECKED) {
        if (overflow) {
            // option type - None
            auto enumNone = ITuple{{IValUtils::PrimitiveValue<IBool>(true)}};
            interpStack.ArgsPush(std::move(enumNone));
        } else {
            // option type - Some(res)
            auto enumSome = ITuple{{IValUtils::PrimitiveValue<IBool>(false), IValUtils::PrimitiveValue<T, S>(res)}};
            interpStack.ArgsPush(std::move(enumSome));
        }
    } else {
        interpStack.ArgsPush(IValUtils::PrimitiveValue<T, S>(res));
    }
    return false;
}

template <OpCode op, typename T, typename S> bool BCHIRInterpreter::BinOpInt(Cangjie::OverflowStrategy strat)
{
    if constexpr (op == OpCode::BIN_EXP || op == OpCode::BIN_EXP_EXC) {
        // this is a special case: only Int64 ** UInt64 is allowed by the spec.
        CJC_ASSERT((std::is_same_v<T, IInt64>));
        CJC_ASSERT((std::is_same_v<S, int64_t>));
        return BinExpOpInt(strat);
    } else if constexpr (op == OpCode::UN_NEG || op == OpCode::UN_NEG_EXC) {
        // this is another special case because SUB is a unary operator
        auto a = interpStack.ArgsPop<T>();
        S res;
        bool overflow = CHIR::OverflowChecker::IsOverflowAfterSub<S>(0, a.content, strat, &res);
        return PushIfNotOverflow<T, S>(overflow, res, strat);
    } else if constexpr (op == OpCode::UN_INC) {
        // this is another special case because INC is a unary operator
        auto a = interpStack.ArgsPop<T>();
        S res;
        bool overflow = CHIR::OverflowChecker::IsOverflowAfterAdd<S>(a.content, 1, strat, &res);
        return PushIfNotOverflow<T, S>(overflow, res, strat);
    } else if constexpr (op == OpCode::UN_DEC) {
        // this is another special case because DEC is a unary operator
        auto a = interpStack.ArgsPop<T>();
        S res;
        bool overflow = CHIR::OverflowChecker::IsOverflowAfterSub<S>(a.content, 1, strat, &res);
        return PushIfNotOverflow<T, S>(overflow, res, strat);
    } else if constexpr (op == OpCode::UN_BITNOT) {
        auto a1 = interpStack.ArgsPop<T>();
        S res = static_cast<S>(~a1.content);
        interpStack.ArgsPush(IValUtils::PrimitiveValue<T, S>(res));
        return false;
    } else if constexpr (op == OpCode::BIN_LSHIFT || op == OpCode::BIN_RSHIFT || op == OpCode::BIN_LSHIFT_EXC ||
        op == OpCode::BIN_RSHIFT_EXC) {
        return BinShiftOpInt<op, T, S>();
    } else {
        return BinRegOpInt<op, T, S>(strat);
    }
}

template <typename T> static bool IsOverflowAfterExp(T x, T y, Cangjie::OverflowStrategy strategy, T* res)
{
    CJC_NULLPTR_CHECK(res);
    *res = 1;
    bool isOverflow = false;
    for (T j = 1; j <= y; j++) {
        if (__builtin_mul_overflow(x, *res, res) && !isOverflow) {
            isOverflow = true;
        }
    }
    if (isOverflow && strategy == Cangjie::OverflowStrategy::SATURATING) {
        T magicNumber = 2;
        if (x < 0 && (y % magicNumber == 0)) {
            *res = std::numeric_limits<T>::max();
        } else if (x < 0 && (y % magicNumber == 1)) {
            *res = std::numeric_limits<T>::min();
        } else {
            *res = std::numeric_limits<T>::max();
        }
    }
    return isOverflow;
}

bool BCHIRInterpreter::BinExpOpInt(Cangjie::OverflowStrategy strat)
{
    auto a2 = interpStack.ArgsPop<IUInt64>();
    auto a1 = interpStack.ArgsPop<IInt64>();
    int64_t res;
    bool overflow = CHIR::OverflowChecker::IsExpOverflow(a1.content, a2.content, strat, &res);
    return PushIfNotOverflow<IInt64, int64_t>(overflow, res, strat);
}

template <OpCode op, typename T, typename S> bool BCHIRInterpreter::BinRegOpInt(Cangjie::OverflowStrategy strat)
{
    auto a2 = interpStack.ArgsPop<T>();
    auto a1 = interpStack.ArgsPop<T>();
    S gRes;
    if constexpr (op == OpCode::BIN_ADD || op == OpCode::BIN_ADD_EXC) {
        auto isOverflow = CHIR::OverflowChecker::IsOverflowAfterAdd(a1.content, a2.content, strat, &gRes);
        return PushIfNotOverflow<T, S>(isOverflow, gRes, strat);
    } else if constexpr (op == OpCode::BIN_SUB || op == OpCode::BIN_SUB_EXC) {
        auto isOverflow = CHIR::OverflowChecker::IsOverflowAfterSub(a1.content, a2.content, strat, &gRes);
        return PushIfNotOverflow<T, S>(isOverflow, gRes, strat);
    } else if constexpr (op == OpCode::BIN_MUL || op == OpCode::BIN_MUL_EXC) {
        auto isOverflow = CHIR::OverflowChecker::IsOverflowAfterMul(a1.content, a2.content, strat, &gRes);
        return PushIfNotOverflow<T, S>(isOverflow, gRes, strat);
    } else if constexpr (op == OpCode::BIN_DIV || op == OpCode::BIN_DIV_EXC) {
        if (a2.content == 0) {
            // pc is already pointing to the next instruction,
            // thus sourcePc = pc - 1 (strategy) - 1 (kind idx) - 1
            RaiseArithmeticException(pc - INSTRUCTION_DIFF);
            return true;
        }
        auto isOverflow = CHIR::OverflowChecker::IsOverflowAfterDiv(a1.content, a2.content, strat, &gRes);
        return PushIfNotOverflow<T, S>(isOverflow, gRes, strat);
    } else if constexpr (op == OpCode::BIN_MOD || op == OpCode::BIN_MOD_EXC) {
        if (a2.content == 0) {
            // pc is already pointing to the next instruction,
            // thus sourcePc = pc - 1 (strategy) - 1 (kind idx) - 1
            RaiseArithmeticException(pc - INSTRUCTION_DIFF);
            return true;
        }
        auto isOverflow = CHIR::OverflowChecker::IsOverflowAfterMod(a1.content, a2.content, &gRes);
        return PushIfNotOverflow<T, S>(isOverflow, gRes, strat);
    } else if constexpr (op == OpCode::BIN_BITAND) {
        // BIN_BITOR, BIN_BITAND and BIN_BITXOR Bitwise operations can not raise exceptions
        // but these operations are handled like the rest to keep code homogeneity
        S res = a1.content & a2.content;
        interpStack.ArgsPush(IValUtils::PrimitiveValue<T, S>(res));
        return false;
    } else if constexpr (op == OpCode::BIN_BITOR) {
        S res = a1.content | a2.content;
        interpStack.ArgsPush(IValUtils::PrimitiveValue<T, S>(res));
        return false;
    } else if constexpr (op == OpCode::BIN_BITXOR) {
        S res = a1.content ^ a2.content;
        interpStack.ArgsPush(IValUtils::PrimitiveValue<T, S>(res));
        return false;
    } else if constexpr (op == OpCode::BIN_EQUAL || op == OpCode::BIN_NOTEQ || op == OpCode::BIN_LT ||
        op == OpCode::BIN_GT || op == OpCode::BIN_LE || op == OpCode::BIN_GE) {
        return BinOpCompare<op, decltype(a1.content)>(a1.content, a2.content);
    } else {
        CJC_ABORT();
    }
    return false;
}

template <typename Type, typename T, typename S, OpCode op> bool BCHIRInterpreter::BinShiftOpIntCase()
{
    auto a2 = interpStack.ArgsPop<Type>();
    auto a1 = interpStack.ArgsPop<T>();
    if constexpr (std::is_same_v<Type, IInt8> || std::is_same_v<Type, IInt16> || std::is_same_v<Type, IInt32> ||
        std::is_same_v<Type, IInt64> || std::is_same_v<Type, IIntNat>) {
        if (a2.content < 0) {
            const std::string msg = "Overshift: Value of right operand is less than 0!";
            // pc is already pointing to the next instruction,
            // thus sourcePc = pc - 1 (rhs kind idx) - 1 (strategy) - 1 (kind idx) - 1
            RaiseArithmeticExceptionMsg(pc - Bchir::FLAG_FOUR, msg);
            return true;
        }
    }
    auto shiftValue = static_cast<int64_t>(a2.content);
    if (shiftValue >= static_cast<int64_t>(IValUtils::SizeOf<T>())) {
        const std::string msg =
            "Overshift: Value of right operand is greater than or equal to the width of left operand!";
        // pc is already pointing to the next instruction,
        // thus sourcePc = pc - 1 (rhs kind idx) - 1 (strategy) - 1 (kind idx) - 1
        RaiseArithmeticExceptionMsg(pc - Bchir::FLAG_FOUR, msg);
        return true;
    }
    if constexpr (op == OpCode::BIN_LSHIFT || op == OpCode::BIN_LSHIFT_EXC) {
        interpStack.ArgsPush(IValUtils::PrimitiveValue<T, S>(static_cast<S>(a1.content << shiftValue)));
    } else {
        static_assert((op == OpCode::BIN_RSHIFT || op == OpCode::BIN_RSHIFT_EXC));
        interpStack.ArgsPush(IValUtils::PrimitiveValue<T, S>(static_cast<S>(a1.content >> shiftValue)));
    }
    return false;
}

template <OpCode op, typename T, typename S> bool BCHIRInterpreter::BinShiftOpInt()
{
    auto rhsTy = static_cast<CHIR::Type::TypeKind>(bchir.Get(pc++));
    switch (rhsTy) {
        case CHIR::Type::TypeKind::TYPE_INT8: {
            return BinShiftOpIntCase<IInt8, T, S, op>();
        }
        case CHIR::Type::TypeKind::TYPE_INT16: {
            return BinShiftOpIntCase<IInt16, T, S, op>();
        }
        case CHIR::Type::TypeKind::TYPE_INT32: {
            return BinShiftOpIntCase<IInt32, T, S, op>();
        }
        case CHIR::Type::TypeKind::TYPE_INT64: {
            return BinShiftOpIntCase<IInt64, T, S, op>();
        }
        case CHIR::Type::TypeKind::TYPE_INT_NATIVE: {
            return BinShiftOpIntCase<IIntNat, T, S, op>();
        }
        case CHIR::Type::TypeKind::TYPE_UINT8: {
            return BinShiftOpIntCase<IUInt8, T, S, op>();
        }
        case CHIR::Type::TypeKind::TYPE_UINT16: {
            return BinShiftOpIntCase<IUInt16, T, S, op>();
        }
        case CHIR::Type::TypeKind::TYPE_UINT32: {
            return BinShiftOpIntCase<IUInt32, T, S, op>();
        }
        case CHIR::Type::TypeKind::TYPE_UINT64: {
            return BinShiftOpIntCase<IUInt64, T, S, op>();
        }
        case CHIR::Type::TypeKind::TYPE_UINT_NATIVE: {
            return BinShiftOpIntCase<IUIntNat, T, S, op>();
        }
        default:
            CJC_ABORT();
    }
    return false;
}

template <OpCode op, typename T, typename S> bool BCHIRInterpreter::BinOpFloat()
{
    if constexpr (op == OpCode::BIN_EXP) {
        // this is a special case because the type of the values are different
        auto rhs = interpStack.ArgsPopIVal();
        auto a1 = interpStack.ArgsPop<T>();

        if constexpr (std::is_same_v<T, IFloat64>) {
            CJC_ASSERT(typeid(S) == typeid(double));
            if (auto a2 = IValUtils::GetIf<IInt64>(&rhs)) {
                interpStack.ArgsPush(
                    IValUtils::PrimitiveValue<T, S>(pow(a1.content, static_cast<double>(a2->content))));
                return false;
            } else {
                CJC_ASSERT(std::holds_alternative<IFloat64>(rhs));
                auto ea2 = IValUtils::Get<IFloat64>(std::move(rhs));
                interpStack.ArgsPush(IValUtils::PrimitiveValue<T, S>(pow(a1.content, ea2.content)));
                return false;
            }
        }
        auto a2 = IValUtils::Get<T>(std::move(rhs));
        interpStack.ArgsPush(IValUtils::PrimitiveValue<T, S>(static_cast<S>(pow(a1.content, a2.content))));
    }
    auto a2 = interpStack.ArgsPop<T>();
    if constexpr (op == OpCode::UN_NEG) {
        // this is a special case because SUB is a unary operator
        interpStack.ArgsPush(IValUtils::PrimitiveValue<T, S>(-a2.content));
        return false;
    }
    auto a1 = interpStack.ArgsPop<T>();
    if constexpr (op == OpCode::BIN_ADD) {
        interpStack.ArgsPush(IValUtils::PrimitiveValue<T, S>(a1.content + a2.content));
    } else if constexpr (op == OpCode::BIN_SUB) {
        interpStack.ArgsPush(IValUtils::PrimitiveValue<T, S>(a1.content - a2.content));
    } else if constexpr (op == OpCode::BIN_MUL) {
        interpStack.ArgsPush(IValUtils::PrimitiveValue<T, S>(a1.content * a2.content));
    } else if constexpr (op == OpCode::BIN_DIV) {
        interpStack.ArgsPush(IValUtils::PrimitiveValue<T, S>(a1.content / a2.content));
    } else if constexpr (op == OpCode::BIN_EQUAL || op == OpCode::BIN_NOTEQ || op == OpCode::BIN_LT ||
        op == OpCode::BIN_GT || op == OpCode::BIN_LE || op == OpCode::BIN_GE) {
        return BinOpCompare<op, decltype(a1.content)>(a1.content, a2.content);
    } else {
        CJC_ABORT();
    }
    return false;
}

template <OpCode op>
bool BCHIRInterpreter::BinOpTyKindAndOverflowStrat(CHIR::Type::TypeKind kind, Cangjie::OverflowStrategy strat)
{
    switch (kind) {
        case CHIR::Type::TypeKind::TYPE_UINT8: {
            return BinOpInt<op, IUInt8, uint8_t>(strat);
        }
        case CHIR::Type::TypeKind::TYPE_UINT16: {
            return BinOpInt<op, IUInt16, uint16_t>(strat);
        }
        case CHIR::Type::TypeKind::TYPE_UINT32: {
            return BinOpInt<op, IUInt32, uint32_t>(strat);
        }
        case CHIR::Type::TypeKind::TYPE_UINT64: {
            return BinOpInt<op, IUInt64, uint64_t>(strat);
        }
        case CHIR::Type::TypeKind::TYPE_UINT_NATIVE: {
            return BinOpInt<op, IUIntNat, size_t>(strat);
        }
        case CHIR::Type::TypeKind::TYPE_INT8: {
            return BinOpInt<op, IInt8, int8_t>(strat);
        }
        case CHIR::Type::TypeKind::TYPE_INT16: {
            return BinOpInt<op, IInt16, int16_t>(strat);
        }
        case CHIR::Type::TypeKind::TYPE_INT32: {
            return BinOpInt<op, IInt32, int32_t>(strat);
        }
        case CHIR::Type::TypeKind::TYPE_INT64: {
            return BinOpInt<op, IInt64, int64_t>(strat);
        }
        case CHIR::Type::TypeKind::TYPE_INT_NATIVE: {
#if (defined(__x86_64__) || defined(__aarch64__))
            return BinOpInt<op, IIntNat, int64_t>(strat);
#else
            return BinOpInt<op, IIntNat, int32_t>(strat);
#endif
        }
        case CHIR::Type::TypeKind::TYPE_FLOAT16: {
            return BinOpFloat<op, IFloat16, float>();
        }
        case CHIR::Type::TypeKind::TYPE_FLOAT32: {
            return BinOpFloat<op, IFloat32, float>();
        }
        case CHIR::Type::TypeKind::TYPE_FLOAT64: {
            return BinOpFloat<op, IFloat64, double>();
        }
        case CHIR::Type::TypeKind::TYPE_BOOLEAN: {
            return BinOpBool<op>();
        }
        case CHIR::Type::TypeKind::TYPE_RUNE: {
            return BinOpRune<op>();
        }
        case CHIR::Type::TypeKind::TYPE_UNIT: {
            return BinOpUnit<op>();
        }
        default:
            CJC_ABORT();
    }
    return false;
}

template <OpCode op> void BCHIRInterpreter::BinOp()
{
    auto initPc = pc;
    auto kindIdx = pc + 1;
    auto stratIdx = kindIdx + 1;
    auto kind = static_cast<CHIR::Type::TypeKind>(bchir.Get(kindIdx));
    auto strat = static_cast<Cangjie::OverflowStrategy>(bchir.Get(stratIdx));
    pc = stratIdx + 1;

    if constexpr (OpHasExceptionHandler(op)) {
        interpStack.CtrlPush({op, 0, initPc, env.GetBP()});
    }

    auto raisedException = BinOpTyKindAndOverflowStrat<op>(kind, strat);

    if constexpr (OpHasExceptionHandler(op)) {
        if (!raisedException) {
            CJC_ASSERT(interpStack.CtrlTop().opCode == op);
            CJC_ASSERT(interpStack.CtrlTop().byteCodePtr == initPc);
            interpStack.CtrlDrop();
            pc++;
        }
    } else {
        (void)initPc;
        (void)raisedException;
    }
}

template <OpCode op> void BCHIRInterpreter::BinOpFixedBool()
{
    pc = pc + Bchir::FLAG_THREE;

    BinOpBool<op>();
}

template <OpCode op> bool BCHIRInterpreter::BinOpBool()
{
    if constexpr (op == OpCode::UN_NOT) {
        auto v = interpStack.ArgsPop<IBool>();
        interpStack.ArgsPush(IValUtils::PrimitiveValue<IBool>(!v.content));
    } else if constexpr (op == OpCode::BIN_EQUAL) {
        auto v1 = interpStack.ArgsPop<IBool>();
        auto v2 = interpStack.ArgsPop<IBool>();

        interpStack.ArgsPush(IValUtils::PrimitiveValue<IBool>(v1.content == v2.content));
    } else if constexpr (op == OpCode::BIN_NOTEQ) {
        auto v1 = interpStack.ArgsPop<IBool>();
        auto v2 = interpStack.ArgsPop<IBool>();

        interpStack.ArgsPush(IValUtils::PrimitiveValue<IBool>(v1.content != v2.content));
    } else {
        // should not happen
        CJC_ABORT();
    }
    return false;
}

template <OpCode op> bool BCHIRInterpreter::BinOpRune()
{
    auto v2 = interpStack.ArgsPop<IRune>();
    auto v1 = interpStack.ArgsPop<IRune>();

    if constexpr (op == OpCode::BIN_EQUAL || op == OpCode::BIN_NOTEQ || op == OpCode::BIN_LT || op == OpCode::BIN_GT ||
        op == OpCode::BIN_LE || op == OpCode::BIN_GE) {
        return BinOpCompare<op, decltype(v1.content)>(v1.content, v2.content);
    } else {
        // should not happen
        CJC_ABORT();
    }
    return false;
}

template <OpCode op, typename T> bool BCHIRInterpreter::BinOpCompare(T x, T y)
{
    if constexpr (op == OpCode::BIN_EQUAL) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
        interpStack.ArgsPush(IValUtils::PrimitiveValue<IBool>(x == y));
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    } else if constexpr (op == OpCode::BIN_NOTEQ) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
        interpStack.ArgsPush(IValUtils::PrimitiveValue<IBool>(x != y));
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    } else if constexpr (op == OpCode::BIN_LT) {
        interpStack.ArgsPush(IValUtils::PrimitiveValue<IBool>(x < y));
    } else if constexpr (op == OpCode::BIN_GT) {
        interpStack.ArgsPush(IValUtils::PrimitiveValue<IBool>(x > y));
    } else if constexpr (op == OpCode::BIN_LE) {
        interpStack.ArgsPush(IValUtils::PrimitiveValue<IBool>(x <= y));
    } else if constexpr (op == OpCode::BIN_GE) {
        interpStack.ArgsPush(IValUtils::PrimitiveValue<IBool>(x >= y));
    }
    return false;
}

template <OpCode op> bool BCHIRInterpreter::BinOpUnit()
{
    CJC_ASSERT(std::holds_alternative<IUnit>(interpStack.ArgsTopIVal()));
    interpStack.ArgsPopBack();
    CJC_ASSERT(std::holds_alternative<IUnit>(interpStack.ArgsTopIVal()));
    interpStack.ArgsPopBack();

    if constexpr (op == OpCode::BIN_EQUAL) {
        interpStack.ArgsPush(IValUtils::PrimitiveValue<IBool>(true));
    } else if constexpr (op == OpCode::BIN_NOTEQ) {
        interpStack.ArgsPush(IValUtils::PrimitiveValue<IBool>(false));
    } else {
        // should not happen
        CJC_ABORT();
    }
    return false;
}

IResult BCHIRInterpreter::Run(size_t baseIdx, bool expectsReturn)
{
    baseIndex = static_cast<unsigned>(baseIdx);
    pc = baseIndex;
    playgroundIdx = playgroundIdxBase;
    raiseExnToTopLevel = false;
    Interpret();
    if (interpreterError || raiseExnToTopLevel) {
        return IException{IInvalid{}};
    } else if (!expectsReturn) {
        // this happens when we run the top-level
        CJC_ASSERT(interpStack.ArgsSize() == 0);
        CJC_ASSERT(interpStack.CtrlIsEmpty());
        result = INotRun{};
        return result;
    } else {
        CJC_ASSERT(interpStack.ArgsSize() == 1);
        CJC_ASSERT(interpStack.CtrlIsEmpty());
        result = ISuccess{interpStack.ArgsPopIVal()};
        return result;
    }
}

const IResult& BCHIRInterpreter::GetLastResult() const
{
    return result;
}

void BCHIRInterpreter::InterpretSwitch()
{
    pc += 1;
    switch (static_cast<CHIR::Type::TypeKind>(bchir.Get(pc))) {
        case CHIR::Type::TypeKind::TYPE_UINT8: {
            InterpretSwitchWithType<IUInt8>();
            break;
        }
        case CHIR::Type::TypeKind::TYPE_UINT16: {
            InterpretSwitchWithType<IUInt16>();
            break;
        }
        case CHIR::Type::TypeKind::TYPE_UINT32: {
            InterpretSwitchWithType<IUInt32>();
            break;
        }
        case CHIR::Type::TypeKind::TYPE_UINT64: {
            InterpretSwitchWithType<IUInt64>();
            break;
        }
        case CHIR::Type::TypeKind::TYPE_UINT_NATIVE: {
            InterpretSwitchWithType<IUIntNat>();
            break;
        }
        case CHIR::Type::TypeKind::TYPE_INT8: {
            InterpretSwitchWithType<IInt8>();
            break;
        }
        case CHIR::Type::TypeKind::TYPE_INT16: {
            InterpretSwitchWithType<IInt16>();
            break;
        }
        case CHIR::Type::TypeKind::TYPE_INT32: {
            InterpretSwitchWithType<IInt32>();
            break;
        }
        case CHIR::Type::TypeKind::TYPE_INT64: {
            InterpretSwitchWithType<IInt64>();
            break;
        }
        case CHIR::Type::TypeKind::TYPE_INT_NATIVE: {
            InterpretSwitchWithType<IIntNat>();
            break;
        }
        case CHIR::Type::TypeKind::TYPE_RUNE: {
            InterpretSwitchWithType<IRune>();
            break;
        }
        case CHIR::Type::TypeKind::TYPE_BOOLEAN: {
            InterpretSwitchWithType<IBool>();
            break;
        }
        default: {
            CJC_ABORT();
        }
    }
}

template <typename Ty> void BCHIRInterpreter::InterpretSwitchWithType()
{
    size_t realValue = static_cast<unsigned char>(interpStack.ArgsPop<Ty>().content);
    // sorted case
    auto casesIdx = pc + 1;
    auto cases = bchir.Get(casesIdx);

    // each case is a 8 bytes value
    auto from = reinterpret_cast<const size_t*>(&*(bchir.GetLinkedByteCode().GetByteCode().begin() + casesIdx + 1));
    auto until = from + cases;

    auto found = std::lower_bound(from, until, realValue);
    if (found == until || *found != realValue) {
        // default target
        pc = bchir.Get(casesIdx + 1 + cases * Bchir::FLAG_TWO);
    } else {
        pc = bchir.Get(static_cast<unsigned>(casesIdx + 1 + cases * Bchir::FLAG_TWO + 1 + (found - from)));
    }
}

IVal* BCHIRInterpreter::AllocateValue(IVal&& value)
{
    IVal* ptr = arena.Allocate(std::move(value));
    auto objectVal = IValUtils::GetIf<IObject>(ptr);
    if (objectVal == nullptr) {
        return ptr;
    }
    auto finalizerIdx = bchir.GetClassFinalizer(objectVal->classId);
    if (finalizerIdx != 0) {
        (void)arena.finalizingObjects.emplace_back(ptr);
    }
    return ptr;
}

#ifndef NDEBUG
std::string BCHIRInterpreter::DebugGetPosition(Bchir::ByteCodeIndex index)
{
    auto pos = bchir.GetLinkedByteCode().GetCodePositionAnnotation(index);
    auto fileName = bchir.GetFileName(pos.fileID);
    auto posStr = fileName + ":" + std::to_string(pos.line) + ":" + std::to_string(pos.column);
    return posStr;
}

std::string BCHIRInterpreter::DebugGetMangledName(Bchir::ByteCodeIndex index) const
{
    return bchir.GetLinkedByteCode().GetMangledNameAnnotation(index);
}

void BCHIRInterpreter::PrintDebugInfo(Bchir::ByteCodeIndex currentPc)
{
    if (printRuntimeDebugInfo) {
        CJC_ASSERT(debugFile.is_open());
        auto currentOp = static_cast<OpCode>(bchir.Get(currentPc));
        auto mangled = bchir.GetLinkedByteCode().GetMangledNameAnnotation(currentPc);
        auto position = bchir.GetLinkedByteCode().GetCodePositionAnnotation(currentPc);
        auto file = bchir.GetFileName(position.fileID);
        debugFile << std::to_string(currentPc) << " - " << GetOpCodeLabel(currentOp);
        if (mangled != "") {
            debugFile << " - " << mangled;
        }
        if (position.fileID != 0 || position.line != 0 || position.column != 0) {
            debugFile << " - " << file << ":" << position.line << ":" << position.column;
        }
        debugFile << std::endl;
    }
}

void BCHIRInterpreter::PrepareRuntimeDebug(const GlobalOptions& options)
{
    if (debugFile.is_open()) {
        debugFile.close();
    }
    printRuntimeDebugInfo = options.PrintBchir(GlobalOptions::PrintBCHIROption::INTERPRETER);
    if (printRuntimeDebugInfo) {
        auto stageName = isConstEval ? "ce-interpreted" : "interpreted";
        debugFile = BCHIRPrinter::GetBCHIROutputFile(options, bchir.packageName, stageName);
    }
}
#endif

template <bool isLiteral, bool isExc> void BCHIRInterpreter::InterpretAllocateRawArray()
{
    auto initPc = pc++;
    auto array = IArray();
    if constexpr (isLiteral) {
        size_t size = static_cast<size_t>(bchir.Get(pc++));
        interpStack.ArgsPop(size, array.content);
        CJC_ASSERT(array.content.size() == size);
        (void)initPc;
    } else {
        auto sizeIVal = interpStack.ArgsPop<IInt64>();
        auto size = sizeIVal.content;

        if (size < 0) {
            return RaiseNegativeArraySizeException(initPc);
        }

        // overflow or greater than max_size
        if (static_cast<size_t>(size + 1) > array.content.max_size()) {
            return RaiseOutOfMemoryError(initPc);
        }

        array.content.reserve(static_cast<size_t>(size) + 1);
        array.content.emplace_back(std::move(sizeIVal));
        for (size_t i = 0; i < static_cast<size_t>(size); ++i) {
            array.content.emplace_back(INullptr());
        }
        CJC_ASSERT(array.content.size() == static_cast<size_t>(size) + 1);
    }
    auto ptr = IPointer();
    ptr.content = AllocateValue(std::move(array));
    interpStack.ArgsPush(ptr);
    if constexpr (isExc) {
        pc++;
    }
}

template <OpCode op> void BCHIRInterpreter::InterpretIntrinsic()
{
    if constexpr (op == OpCode::INTRINSIC0) {
        InterpretIntrinsic0();
    } else if constexpr (op == OpCode::INTRINSIC1) {
        InterpretIntrinsic1();
    }
}

void BCHIRInterpreter::InterpretVArrayGet()
{
    auto initPc = pc++;
    auto pathSize = bchir.Get(pc++);
    std::vector<IVal> path;
    interpStack.ArgsPop(pathSize, path);
    auto array = interpStack.ArgsPop<IArray>();
    for (size_t i = 0; i < pathSize - 1; ++i) {
        auto arrayIndex = IValUtils::Get<IInt64>(path[i]).content;
        array = IValUtils::Get<IArray>(array.content[static_cast<size_t>(arrayIndex)]);
    }
    auto finalIndex = IValUtils::Get<IInt64>(path[static_cast<size_t>(pathSize - 1)]);
    auto getIdx = finalIndex.content;
    if (getIdx < 0 || getIdx > static_cast<int64_t>(array.content.size())) {
        RaiseIndexOutOfBoundsException(static_cast<unsigned int>(initPc));
    } else {
        interpStack.ArgsPushIVal(std::move(array.content[static_cast<size_t>(getIdx)]));
    }
}

void BCHIRInterpreter::InterpretRawArrayLiteralInit()
{
    pc++; // to array size
    auto size = bchir.Get(pc);
    pc++; // to next operation
    std::vector<IVal> elems;
    interpStack.ArgsPop(size, elems);
    auto arrayPtr = interpStack.ArgsPop<IPointer>();
    auto& array = IValUtils::Get<IArray>(*arrayPtr.content);
    CJC_ASSERT(static_cast<Bchir::ByteCodeContent>(IValUtils::Get<IInt64>(array.content[0]).content) == size);
    for (size_t i = 0; i < static_cast<size_t>(size); ++i) {
        array.content[i + 1] = std::move(elems[i]);
    }
    interpStack.ArgsPush(IUnit());
}

