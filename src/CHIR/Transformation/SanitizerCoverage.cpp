// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/SanitizerCoverage.h"

#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/Utils/ConstantUtils.h"
#include "cangjie/CHIR/Utils/DiagAdapter.h"
#include "cangjie/CHIR/IR/IntrinsicKind.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/Utils/Visitor/Visitor.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/SafePointer.h"

namespace Cangjie::CHIR {

namespace {
Ptr<Apply> CreateNonMemberApply(Ptr<Type> resultTy, Ptr<Value> callee, const std::vector<Value*>& args,
    Ptr<Block> parent, CHIRBuilder& builder, DebugLocation loc = INVALID_LOCATION)
{
    return builder.CreateExpression<Apply>(loc, resultTy, callee, FuncCallContext{.args = args}, parent);
}

Ptr<Apply> CreateMemberApply(Ptr<Type> resultTy, Ptr<Value> callee, Ptr<Type> thisType,
    const std::vector<Value*>& args, Ptr<Block> parent, CHIRBuilder& builder, DebugLocation loc = INVALID_LOCATION)
{
    return builder.CreateExpression<Apply>(loc, resultTy, callee, FuncCallContext{
        .args = args,
        .thisType = thisType}, parent);
}
} // namespace

static const std::string CHAR_ARRAY_TEST_PACK = "CharArrayTest";

/*
brief: create a bool array, which is used when sanitizer-coverage-inline-bool-flag is available.
param: uint64_t n, size of bool array.
return: bool*, return bool array.
*/
static const std::string SanCovBoolFlagInitName = "__cj_sancov_bool_flag_ctor";
/*
brief: create a char array, which is used when sanitizer-coverage-inline-8bit-counters is available.
param: uint64_t n, size of char array.
return: char*, return char array.
*/
static const std::string SanCovCharArrayInitName = "__cj_sancov_8bit_counters_ctor";
/*
brief: create a uint32 array, which is used when sanitizer-coverage-trace-pc-guard is available.
param: uint64_t n, size of uint32 array.
return: unsigned int* , return uint32 array.
*/
static const std::string SanCovUInt32ArrayInitName = "__cj_sancov_pc_guard_ctor";
static const std::string SAN_COV_PC_GUARD = "__sanitizer_cov_trace_pc_guard";
/*
brief: this func is used when sanitizer-coverage-trace-compares is available.
param: uint Arg1, uint Arg2
return: void.
*/
static const std::string SanCovTraceCmp1 = "__sanitizer_cov_trace_cmp1";
static const std::string SanCovTraceCmp2 = "__sanitizer_cov_trace_cmp2";
static const std::string SanCovTraceCmp4 = "__sanitizer_cov_trace_cmp4";
static const std::string SanCovTraceCmp8 = "__sanitizer_cov_trace_cmp8";
static const std::string SanCovTraceConstCmp1 = "__sanitizer_cov_trace_const_cmp1";
static const std::string SanCovTraceConstCmp2 = "__sanitizer_cov_trace_const_cmp2";
static const std::string SanCovTraceConstCmp4 = "__sanitizer_cov_trace_const_cmp4";
static const std::string SanCovTraceConstCmp8 = "__sanitizer_cov_trace_const_cmp8";

/*
brief: this func is used when sanitizer-coverage-trace-memcmp is available.
param: void *called_pc, const void *s1, const void *s2, size_t n, int result
return: void.
*/
static const std::string SAN_COV_TRACE_MEM_CMP = "__cj_sanitizer_weak_hook_memcmp";
static const std::string SAN_COV_TRACE_STRN_CMP = "__cj_sanitizer_weak_hook_strncmp";
static const std::string SAN_COV_TRACE_STR_CMP = "__cj_sanitizer_weak_hook_strcmp";
static const std::string SAN_COV_TRACE_STR_CASE_CMP = "__cj_sanitizer_weak_hook_strcasecmp";
/*
brief: this func is used when sanitizer-coverage-trace-compares is available.
param: uint64_t Val, uint64_t* Cases. n indicates size of uint64 array
       cases[0] count of cases array, cases[1] = 64, cases[2..n] value of case
return: void
*/
static const std::string SanCovTraceSwitchName = "__sanitizer_cov_trace_switch";
/*
brief: create a uint64 array and a CString array, which is used when sanitizer-coverage-pc-table is available.
param: uint64_t n, uint64_t* funcBBCountTable , int8_t** funcNameTable.
n indicates size of uint64 array and CString array, start address of uint64 array, start address of CString array.
return: void
*/
static const std::string SanCovCreatePCTable = "__cj_sancov_pcs_init";
static const std::string SanCovUpdateStackDepth = "__cj_sancov_update_stack_depth";

static const std::unordered_map<std::string, std::string> SAN_COV_CTOR2_GLOBAL_VAR_NAME = {
    {SanCovBoolFlagInitName, "sancov$_bool_flag"}, {SanCovCharArrayInitName, "sancov$_8bit_counters"},
    {SanCovUInt32ArrayInitName, "sancov$_pc_guard"}};

static const std::unordered_map<std::string, std::string> STRING_FUNC_NAME2_SAN_COV_FUNC = {
    {"==", SAN_COV_TRACE_MEM_CMP}, {"startsWith", SAN_COV_TRACE_MEM_CMP}, {"endsWith", SAN_COV_TRACE_MEM_CMP},
    {"indexOf", SAN_COV_TRACE_MEM_CMP}, {"replace", SAN_COV_TRACE_MEM_CMP}, {"contains", SAN_COV_TRACE_MEM_CMP}};

static const std::unordered_map<std::string, std::string> CSTRING_FUNC_NAME2_SAN_COV_FUNC = {
    {"equals", SAN_COV_TRACE_STR_CMP}, {"startsWith", SAN_COV_TRACE_MEM_CMP}, {"endsWith", SAN_COV_TRACE_STRN_CMP},
    {"compare", SAN_COV_TRACE_STR_CMP}, {"equalsLower", SAN_COV_TRACE_STR_CASE_CMP}};

static const std::unordered_set<std::string> HAS_N_PARAMETER_MEM_CMP_SET = {
    SAN_COV_TRACE_MEM_CMP, SAN_COV_TRACE_STRN_CMP};

enum class MemCmpType { STRING_TYPE, CSTRING_TYPE, ARRAY_TYPE, ARRAYLIST_TYPE };

std::optional<std::string> GetStringSanConvFunc(MemCmpType cmpType, const std::string& funcName)
{
    if (cmpType == MemCmpType::STRING_TYPE && STRING_FUNC_NAME2_SAN_COV_FUNC.count(funcName) != 0) {
        return STRING_FUNC_NAME2_SAN_COV_FUNC.at(funcName);
    }
    if (cmpType == MemCmpType::CSTRING_TYPE && CSTRING_FUNC_NAME2_SAN_COV_FUNC.count(funcName) != 0) {
        return CSTRING_FUNC_NAME2_SAN_COV_FUNC.at(funcName);
    }
    if (cmpType == MemCmpType::ARRAY_TYPE && funcName == "==") {
        return SAN_COV_TRACE_MEM_CMP;
    }
    if (cmpType == MemCmpType::ARRAYLIST_TYPE && funcName == "==") {
        return SAN_COV_TRACE_MEM_CMP;
    }
    return std::nullopt;
}

Type::TypeKind GetTraceCompareType(Type::TypeKind kind)
{
    switch (kind) {
        case Type::TypeKind::TYPE_INT64:
        case Type::TypeKind::TYPE_UINT64: {
            return Type::TypeKind::TYPE_UINT64;
        }
        case Type::TypeKind::TYPE_INT32:
        case Type::TypeKind::TYPE_UINT32: {
            return Type::TypeKind::TYPE_UINT32;
        }
        case Type::TypeKind::TYPE_INT16:
        case Type::TypeKind::TYPE_UINT16: {
            return Type::TypeKind::TYPE_UINT16;
        }
        case Type::TypeKind::TYPE_INT8:
        case Type::TypeKind::TYPE_UINT8: {
            return Type::TypeKind::TYPE_UINT8;
        }
        default:
            break;
    };
    return Type::TypeKind::TYPE_UNIT;
}

std::string GetTraceCompareSyscallName(const BinaryExpression& binaryOp)
{
    static const std::unordered_map<Type::TypeKind, std::string> COMPARE_MAP = {
        {Type::TypeKind::TYPE_UINT8, SanCovTraceCmp1},
        {Type::TypeKind::TYPE_UINT16, SanCovTraceCmp2},
        {Type::TypeKind::TYPE_UINT32, SanCovTraceCmp4},
        {Type::TypeKind::TYPE_UINT64, SanCovTraceCmp8},
    };
    static const std::unordered_map<Type::TypeKind, std::string> COMPARE_CONST_MAP = {
        {Type::TypeKind::TYPE_UINT8, SanCovTraceConstCmp1},
        {Type::TypeKind::TYPE_UINT16, SanCovTraceConstCmp2},
        {Type::TypeKind::TYPE_UINT32, SanCovTraceConstCmp4},
        {Type::TypeKind::TYPE_UINT64, SanCovTraceConstCmp8},
    };
    Type::TypeKind curSize = GetTraceCompareType(binaryOp.GetLHSOperand()->GetType()->GetTypeKind());

    std::string syscallName;
    bool hasArgsConst =
        binaryOp.GetLHSOperand()->GetType()->IsConstant() || binaryOp.GetRHSOperand()->GetType()->IsConstant();
    if (hasArgsConst) {
        syscallName = COMPARE_CONST_MAP.at(curSize);
    } else {
        syscallName = COMPARE_MAP.at(curSize);
    }
    return syscallName;
}

SanitizerCoverage::SanitizerCoverage(const GlobalOptions& option, CHIRBuilder& builder)
    : sanCovOption(option.sancovOption), builder(builder)
{
}

void SanitizerCoverage::InitFuncBag(const Package& package)
{
    // only need func, and not need generic func
    auto importedVarAndFuncs = package.GetImportedVarAndFuncs();
    for (auto v : importedVarAndFuncs) {
        funcBag.emplace(v->GetIdentifierWithoutPrefix(), v);
    }
}

bool SanitizerCoverage::RunOnPackage(const Ptr<const Package>& package, DiagAdapter& diag, bool isDebug)
{
    if (!CheckSancovOption(diag)) {
        return false;
    }
    InitFuncBag(*package);
    packageName = package->GetName();
    for (auto func : package->GetGlobalFuncs()) {
        // skip global init function and compiled add function
        if (func->IsGVInit() || func->TestAttr(Attribute::COMPILER_ADD) ||
            func->GetPackageName() != package->GetName()) {
            continue;
        }
        // skip literal function, need fix after CHIR is on
        std::string targetSuffix = SPECIAL_NAME_FOR_INIT_LITERAL_FUNCTION + MANGLE_FUNC_PARAM_TYPE_PREFIX +
            MANGLE_VOID_TY_SUFFIX;
        if (func->GetIdentifier().find(targetSuffix) == func->GetIdentifier().size() - targetSuffix.size()) {
            continue;
        }
        RunOnFunc(func, isDebug);
    }
    GenerateInitFunc(*package->GetPackageInitFunc(), isDebug);
    return true;
}

void SanitizerCoverage::RunOnFunc(const Ptr<Func>& func, bool isDebug)
{
    if (sanCovOption.traceCmp) {
        auto visitAction = [this, isDebug](Expression& expr) {
            if (expr.GetExprKind() == ExprKind::MULTIBRANCH) {
                // only support switch int and char
                auto mb = StaticCast<MultiBranch*>(&expr);
                InjectTraceForSwitch(*mb, isDebug);
            }
            if (expr.IsBinaryExpr()) {
                auto binary = StaticCast<BinaryExpression*>(&expr);
                InjectTraceForCmp(*binary, isDebug);
            }
            return VisitResult::CONTINUE;
        };
        Visitor::Visit(*func, visitAction);
    }
    if (sanCovOption.traceMemCmp) {
        auto visitAction = [this, isDebug](Expression& expr) {
            InjectTraceMemCmp(expr, isDebug);
            return VisitResult::CONTINUE;
        };
        Visitor::Visit(*func, visitAction);
    }
    bool isCoverageOpen = sanCovOption.inlineBoolFlag || sanCovOption.inline8bitCounters || sanCovOption.tracePCGuard ||
        sanCovOption.stackDepth;
    if (!isCoverageOpen) {
        return;
    }
    bool isBasicBlockLevel = sanCovOption.coverageType == GlobalOptions::SanitizerCoverageOptions::Type::SCK_BB ||
        sanCovOption.coverageType == GlobalOptions::SanitizerCoverageOptions::Type::SCK_UNKNOW;
    for (auto block : func->GetBody()->GetBlocks()) {
        if (isBasicBlockLevel || block->IsEntry()) {
            /*
                3 situations to insert fuzz fuctions
                1. insert to all blocks when basic block level is used by users
                2. insert to entry blocks when function level is used by users
                3. stack depth only insert to entry block
            */
            InsertCoverageAheadBlock(*block, isDebug);
        }
    }
}

bool SanitizerCoverage::CheckSancovOption(DiagAdapter& diag) const
{
    bool isSancov = sanCovOption.inline8bitCounters || sanCovOption.inlineBoolFlag || sanCovOption.tracePCGuard;
    bool isSancovLevelVaild =
        sanCovOption.coverageType == GlobalOptions::SanitizerCoverageOptions::Type::SCK_FUNCTION ||
        sanCovOption.coverageType == GlobalOptions::SanitizerCoverageOptions::Type::SCK_BB;
    // Rule: if pc-table option is available, one of inline8bitCounters, inlineBoolFlag, or tracePCGuard must also be
    // used.
    if (!isSancov && sanCovOption.pcTable) {
        diag.Diagnose(DiagKind::chir_sancov_illegal_usage_of_pc_table);
        return false;
    }
    if (isSancov && sanCovOption.coverageType == GlobalOptions::SanitizerCoverageOptions::Type::SCK_NONE) {
        diag.Diagnose(DiagKind::chir_sancov_illegal_usage_of_level);
        return false;
    }
    if (!isSancov && isSancovLevelVaild) {
        diag.Diagnose(DiagKind::chir_sancov_illegal_usage_of_level);
        return false;
    }
    return true;
}

void SanitizerCoverage::InjectTraceForCmp(BinaryExpression& binary, bool isDebug)
{
    // We need to inject the trace info for all the comparison operations.
    static std::unordered_set<ExprKind> cmpToken = {
        ExprKind::LT, ExprKind::GT, ExprKind::LE, ExprKind::GE, ExprKind::NOTEQUAL, ExprKind::EQUAL};
    if (cmpToken.find(binary.GetExprKind()) == cmpToken.end()) {
        return;
    }

    auto lhs = binary.GetLHSOperand();
    auto rhs = binary.GetRHSOperand();
    auto curSize = GetTraceCompareType(lhs->GetType()->GetTypeKind());
    if (curSize == Type::TypeKind::TYPE_UNIT) {
        return;
    }

    /*
        Add __sanitizer_cov_trace_const_cmp() function to cmp expression
        before:
        %0 = APPLY()
        %1 = APPLY()
        %2 = Equal(%0, %1)

        after
        %0 = APPLY()
        %1 = APPLY()
        %n = __sanitizer_cov_trace_const_cmpsize(arg1, arg2) // add function here
        %2 = Equal(%0, %1)
    */
    auto parent = binary.GetParentBlock();
    if (auto intTy = StaticCast<IntType*>(lhs->GetType()); intTy && intTy->IsSigned()) {
        // Note: If the operand type is int8, convert it to uint8. int16, int32, int64 is similar.
        auto unsignedType = builder.GetType<IntType>(GetTraceCompareType(lhs->GetType()->GetTypeKind()));
        auto lhsCast = builder.CreateExpression<TypeCast>(binary.GetDebugLocation(), unsignedType, lhs, parent);
        auto rhsCast = builder.CreateExpression<TypeCast>(binary.GetDebugLocation(), unsignedType, rhs, parent);
        lhs = lhsCast->GetResult();
        rhs = rhsCast->GetResult();
        lhsCast->MoveBefore(&binary);
        rhsCast->MoveBefore(&binary);
    }

    auto callName = GetTraceCompareSyscallName(binary);
    auto funcTy = builder.GetType<FuncType>(std::vector<Type*>{lhs->GetType(), rhs->GetType()}, builder.GetUnitTy(),
        /* hasVarLenParam = */ false, /* isCFunc = */ true);
    auto callee = GenerateForeignFunc(callName, binary.GetDebugLocation(), *funcTy, packageName);
    auto syscall = CreateNonMemberApply(
        builder.GetUnitTy(), callee, std::vector<Value*>{lhs, rhs}, parent, builder, binary.GetDebugLocation());

    syscall->MoveBefore(&binary);

    if (isDebug) {
        std::cout << "[SanitizerCoverage] Add trace compares" << ToPosInfo(binary.GetDebugLocation()) << ".\n";
    }
}

void SanitizerCoverage::InjectTraceForSwitch(MultiBranch& mb, bool isDebug)
{
    /*
        Inject trace for switch
        before:
        %1: UInt64 = Apply(...)
        MultiBranch(%1, #x, [1, #y], [2, #z])

        after:
        %1: UInt64 = Apply(...)
        %2: UInt64 = Constant(2)              // switch case size
        %3: UInt64 = Constant(64)             // switch value bit size
        %4: UInt64 = Constant(a)              // switch case 1
        %5: UInt64 = Constant(b)              // switch case 2
        %6: UInt64 = Constant(4)              // list size
        %7: RawArray<UInt64>& = RawArrayAllocate(UInt64, %6) // raw array for second param
        %8: Unit = RawArrayLiteralInit(%7, %2, %3, %4, %5)
        %9: CPointer<UInt64> = Intrinsic/acquireRawData(%7)                     // acquire c pointer
        %10: UInt64 = TypeCast(%9, UInt64)                                       // cast switch value to uint64
        %11: Unit = Apply(@__sanitizer_cov_trace_switch, %10, %7)  // apply sanitizer cov trace        MultiBranch(%1,
       #x, [1, #y], [2, #z])
    */
    if (!mb.GetCondition()->GetType()->IsInteger() && !mb.GetCondition()->GetType()->IsRune()) {
        return;
    }
    auto& callName = SanCovTraceSwitchName;
    const auto& u64Ty = builder.GetUInt64Ty();
    auto cPointTy = builder.GetType<CPointerType>(u64Ty);
    // 1. create trace function type for switch
    auto funcTy = builder.GetType<FuncType>(std::vector<Type*>{u64Ty, cPointTy}, builder.GetUnitTy(), false, true);
    auto callee = GenerateForeignFunc(callName, mb.GetDebugLocation(), *funcTy, packageName);
    auto parent = mb.GetParentBlock();
    // 2. generate lit value list from switch case list
    auto caseValList = CreateArrayForSwitchCaseList(mb);
    caseValList->MoveBefore(&mb);
    // 3. create acquireRawData function
    auto rawDataAcquire = CreateRawDataAcquire(*caseValList, *u64Ty);
    rawDataAcquire->MoveBefore(&mb);
    // 4. create cast if need
    auto switchVal = mb.GetCondition();
    if (switchVal->GetType()->GetTypeKind() != Type::TypeKind::TYPE_UINT64) {
        auto typeCast = builder.CreateExpression<TypeCast>(mb.GetDebugLocation(), u64Ty, switchVal, parent);
        switchVal = typeCast->GetResult();
        typeCast->MoveBefore(&mb);
    }
    // 5. create and insert trace function
    auto syscall = CreateNonMemberApply(builder.GetUnitTy(), callee,
        std::vector<Value*>{switchVal, rawDataAcquire->GetResult()}, parent, builder, mb.GetDebugLocation());

    syscall->MoveBefore(&mb);

    if (isDebug) {
        std::cout << "[SanitizerCoverage] Add trace switch" << ToPosInfo(mb.GetDebugLocation()) << ".\n";
    }
}

std::vector<Value*> SanitizerCoverage::GenerateCStringMemCmp(
    const std::string& fuzzName, Value& oper1, Value& oper2, Apply& apply)
{
    /*
        before:
        %c: Bool = Apply(@_CNatX7CString6equalsHk, %a, %b)

        after
        %1: Int64 = Apply(@_CNatX7CString4sizeHv, %a)
        %2: UInt32 = TypeCast(%1, UInt32)
        %3: CPointer<UInt8> = Intrinsic/convertCStr2Ptr(%a)
        %4: CPointer<UInt8> = Intrinsic/convertCStr2Ptr(%b)
        %5: Unit = Apply(@__cj_sanitizer_weak_hook_strcmp, %3, %4, %2)
        %c: Bool = Apply(@_CNatX7CString6equalsHk, %a, %b)
    */
    auto parent = apply.GetParentBlock();
    auto loc = apply.GetDebugLocation();
    std::vector<Value*> res;
    auto cPointerType = builder.GetType<CPointerType>(builder.GetUInt8Ty());
    auto callContext1 = IntrisicCallContext {
        .kind = IntrinsicKind::CSTRING_CONVERT_CSTR_TO_PTR,
        .args = std::vector<Value*>{&oper1}
    };
    auto cPointer = builder.CreateExpression<Intrinsic>(loc, cPointerType, callContext1, parent);
    cPointer->MoveBefore(&apply);
    auto callContext2 = IntrisicCallContext {
        .kind = IntrinsicKind::CSTRING_CONVERT_CSTR_TO_PTR,
        .args = std::vector<Value*>{&oper2}
    };
    auto cPointer2 = builder.CreateExpression<Intrinsic>(loc, cPointerType, callContext2, parent);
    cPointer2->MoveBefore(&apply);
    if (fuzzName == SAN_COV_TRACE_MEM_CMP) {
        // __cj_sanitizer_weak_hook_memcmp function need void* input
        auto typeTarget = builder.GetType<CPointerType>(builder.GetVoidTy());
        auto callContext3 = IntrisicCallContext {
            .kind = IntrinsicKind::CPOINTER_INIT1,
            .args = std::vector<Value*>{cPointer->GetResult()}
        };
        cPointer = builder.CreateExpression<Intrinsic>(loc, typeTarget, callContext3, parent);
        cPointer->MoveBefore(&apply);
        auto callContext4 = IntrisicCallContext {
            .kind = IntrinsicKind::CPOINTER_INIT1,
            .args = std::vector<Value*>{cPointer2->GetResult()}
        };
        cPointer2 = builder.CreateExpression<Intrinsic>(loc, typeTarget, callContext4, parent);
        cPointer2->MoveBefore(&apply);
    }
    res.push_back(cPointer->GetResult());
    res.push_back(cPointer2->GetResult());
    if (HAS_N_PARAMETER_MEM_CMP_SET.count(fuzzName) != 0) {
        auto getSize = GetImportedFunc(FUNC_MANGLE_NAME_CSTRING_SIZE);
        auto sizeN = CreateMemberApply(
            builder.GetInt64Ty(), getSize, oper1.GetType(), {&oper1}, parent, builder, loc);
        sizeN->MoveBefore(&apply);
        auto sizeNCasted = builder.CreateExpression<TypeCast>(loc, builder.GetUInt32Ty(), sizeN->GetResult(), parent);
        sizeNCasted->MoveBefore(&apply);
        res.push_back(sizeNCasted->GetResult());
    }
    return res;
}

Expression* SanitizerCoverage::CreateOneCPointFromList(Value& array, Apply& apply, Type& elementType, Type& startType)
{
    /*
        generate cpointer data from cj array:
        auto cPointerBase = acquire_raw_data(array.rawptr)
        auto cPointeroffset = array.start
        cPointer = cPointerBase + cPointeroffset
    */
    auto parent = apply.GetParentBlock();
    auto& loc = apply.GetDebugLocation();

    auto rawArrayType = builder.GetType<RefType>(builder.GetType<RawArrayType>(&elementType, 1U));
    auto rawArray = builder.CreateExpression<Field>(loc, rawArrayType, &array, std::vector<uint64_t>{0UL}, parent);
    rawArray->MoveBefore(&apply);
    auto start = builder.CreateExpression<Field>(loc, &startType, &array, std::vector<uint64_t>{1UL}, parent);
    start->MoveBefore(&apply);
    auto startRes = start->GetResult();
    if (startRes->GetType() != builder.GetInt64Ty()) {
        auto castToInt64 = builder.CreateExpression<TypeCast>(loc, builder.GetInt64Ty(), startRes, parent);
        castToInt64->MoveBefore(&apply);
        startRes = castToInt64->GetResult();
    }
    auto origPointerType = builder.GetType<CPointerType>(&elementType);
    auto callContext1 = IntrisicCallContext {
        .kind = IntrinsicKind::ARRAY_ACQUIRE_RAW_DATA,
        .args = std::vector<Value*>{rawArray->GetResult()},
        .instTypeArgs = std::vector<Type*>{&elementType}
    };
    auto cPointerAcquire = builder.CreateExpression<Intrinsic>(loc, origPointerType, callContext1, parent);
    cPointerAcquire->MoveBefore(&apply);

    auto callContext2 = IntrisicCallContext {
        .kind = IntrinsicKind::CPOINTER_ADD,
        .args = std::vector<Value*>{cPointerAcquire->GetResult(), startRes},
        .instTypeArgs = std::vector<Type*>{&elementType}
    };
    auto pointAdd = builder.CreateExpression<Intrinsic>(loc, origPointerType, callContext2, parent);
    pointAdd->MoveBefore(&apply);
    return pointAdd;
}

std::vector<Value*> SanitizerCoverage::GenerateStringMemCmp(
    const std::string& fuzzName, Value& oper1, Value& oper2, Apply& apply)
{
    /*
        before:
        %c: Bool = Apply(@_CNat6String2==ERNat6StringE, %a, %b)

        after:
        %1: Struct-_CNat5ArrayIhE<UInt8> = Field(%a, 0)
        %2: Struct-_CNat5ArrayIhE<UInt8> = Field(%b, 0)
        %3: RawArray<UInt8>& = Field(%1, 0)
        %4: RawArray<UInt8>& = Field(%2, 0)
        %5: Int64 = Field(%1, 1)  // offset of array
        %6: Int64 = Field(%2, 1)
        %7: CPointer<UInt8> = Intrinsic/acquireRawData(%3)
        %8: CPointer<UInt8> = Intrinsic/acquireRawData(%4)
        %9: CPointer<UInt8> = Intrinsic/addPointer(%7, %5)  // for cj array implement, add offset
        %10: CPointer<UInt8> = Intrinsic/addPointer(%8, %6)
        %11: Int64 = Field(%1, 2)  // get size of string array
        %12: UInt32 = TypeCast(%11, UInt32)
        %13: Unit = Apply(@__cj_sanitizer_weak_hook_memcmp, %9, %10, %12)
        %c: Bool = Apply(@_CNat6String2==ERNat6StringE, %a, %b)
    */
    std::vector<Value*> res;
    auto parent = apply.GetParentBlock();
    auto& loc = apply.GetDebugLocation();
    auto cPoint1 = CreateOneCPointFromList(oper1, apply, *builder.GetUInt8Ty(), *builder.GetUInt32Ty());
    auto cPoint2 = CreateOneCPointFromList(oper2, apply, *builder.GetUInt8Ty(), *builder.GetUInt32Ty());
    if (fuzzName == SAN_COV_TRACE_MEM_CMP) {
        // __cj_sanitizer_weak_hook_memcmp function need void* input
        auto typeTarget = builder.GetType<CPointerType>(builder.GetVoidTy());
        auto callContext1 = IntrisicCallContext {
            .kind = IntrinsicKind::CPOINTER_INIT1,
            .args = std::vector<Value*>{cPoint1->GetResult()}
        };
        cPoint1 = builder.CreateExpression<Intrinsic>(loc, typeTarget, callContext1, parent);
        cPoint1->MoveBefore(&apply);
        auto callContext2 = IntrisicCallContext {
            .kind = IntrinsicKind::CPOINTER_INIT1,
            .args = std::vector<Value*>{cPoint2->GetResult()}
        };
        cPoint2 = builder.CreateExpression<Intrinsic>(loc, typeTarget, callContext2, parent);
        cPoint2->MoveBefore(&apply);
    }
    res.push_back(cPoint1->GetResult());
    res.push_back(cPoint2->GetResult());
    if (HAS_N_PARAMETER_MEM_CMP_SET.count(fuzzName) != 0) {
        auto sizeN = builder.CreateExpression<Field>(
            loc, builder.GetUInt32Ty(), &oper1, std::vector<uint64_t>{2}, parent);
        sizeN->MoveBefore(&apply);
        res.push_back(sizeN->GetResult());
    }
    return res;
}

uint64_t GetMultipleFromType(const Type& type)
{
    CJC_ASSERT(type.IsInteger() || type.IsFloat());
    // change type to uint8, recalculate the array size = n * bits / 8U.
    return StaticCast<NumericType*>(&type)->GetBitness() / 8U;
}

std::vector<Value*> SanitizerCoverage::GenerateArrayCmp(
    const std::string& fuzzName, Value& oper1, Value& oper2, Apply& apply)
{
    /*
        before:
        %c: Bool = Apply(@_CNat6ExtendY_5ArrayIl2==ERNat5ArrayIlE, %a, %a)

        after:
        %1: RawArray<Int64>& = Field(%a, 0)  // get rawptr from array
        %2: RawArray<Int64>& = Field(%b, 0)
        %3: Int64 = Field(%a, 1)  // offset of array
        %4: Int64 = Field(%b, 1)
        %5: CPointer<Int64> = Intrinsic/acquireRawData(%1)
        %6: CPointer<Int64> = Intrinsic/acquireRawData(%2)
        %7: CPointer<Int64> = Intrinsic/addPointer(%5, %3)  // for cj array implement, add offset
        %8: CPointer<Int64> = Intrinsic/addPointer(%6, %4)
        %9: CPointer<Void> = Intrinsic/pointerInit1(%7) // type cast to uint8
        %10: CPointer<Void> = Intrinsic/pointerInit1(%8)
        %11: Int64 = Field(%a, 2)  // get size
        %12: UInt32 = TypeCast(%11, UInt32)
        %13: Unit = Apply(@__cj_sanitizer_weak_hook_memcmp, %9, %10, %12)
        %c: Bool = Apply(@_CNat6Extendat5ArrayIl2==ERNat5ArrayIlE, %a, %b)
    */
    std::vector<Value*> res;
    auto parent = apply.GetParentBlock();
    auto& loc = apply.GetDebugLocation();
    CJC_ASSERT(oper1.GetType()->GetTypeKind() == Type::TypeKind::TYPE_STRUCT);
    auto arrayType = StaticCast<StructType*>(oper1.GetType());
    CJC_ASSERT(arrayType->GetGenericArgs().size() == 1);
    auto elementType = arrayType->GetGenericArgs()[0];
    auto cPointer1 = CreateOneCPointFromList(oper1, apply, *elementType, *builder.GetInt64Ty());
    auto cPointer2 = CreateOneCPointFromList(oper2, apply, *elementType, *builder.GetInt64Ty());
    auto typeTarget = builder.GetType<CPointerType>(builder.GetUInt8Ty());
    if (fuzzName == SAN_COV_TRACE_MEM_CMP) {
        typeTarget = builder.GetType<CPointerType>(builder.GetVoidTy());
    }
    // __cj_sanitizer_weak_hook_memcmp function need void* input
    auto callContext1 = IntrisicCallContext {
        .kind = IntrinsicKind::CPOINTER_INIT1,
        .args = std::vector<Value*>{cPointer1->GetResult()}
    };
    auto typeCast1 = builder.CreateExpression<Intrinsic>(loc, typeTarget, callContext1, parent);
    typeCast1->MoveBefore(&apply);
    auto callContext2 = IntrisicCallContext {
        .kind = IntrinsicKind::CPOINTER_INIT1,
        .args = std::vector<Value*>{cPointer2->GetResult()}
    };
    auto typeCast2 = builder.CreateExpression<Intrinsic>(loc, typeTarget, callContext2, parent);
    typeCast2->MoveBefore(&apply);
    res.push_back(typeCast1->GetResult());
    res.push_back(typeCast2->GetResult());

    if (HAS_N_PARAMETER_MEM_CMP_SET.count(fuzzName) != 0) {
        auto sizeN =
            builder.CreateExpression<Field>(loc, builder.GetInt64Ty(), &oper1, std::vector<uint64_t>{2}, parent);
        sizeN->MoveBefore(&apply);
        auto multiple = builder.CreateConstantExpression<IntLiteral>(
            loc, builder.GetInt64Ty(), parent, GetMultipleFromType(*elementType));
        multiple->MoveBefore(&apply);
        auto calSize = builder.CreateExpression<BinaryExpression>(loc, builder.GetInt64Ty(),
            ExprKind::MUL, sizeN->GetResult(), multiple->GetResult(), OverflowStrategy::WRAPPING, parent);
        calSize->MoveBefore(&apply);
        auto sizeNCasted = builder.CreateExpression<TypeCast>(loc, builder.GetUInt32Ty(), calSize->GetResult(), parent);
        sizeNCasted->MoveBefore(&apply);
        res.push_back(sizeNCasted->GetResult());
    }
    return res;
}

std::pair<Value*, Value*> SanitizerCoverage::CastArrayListToArray(Value& oper1, Value& oper2, Apply& apply)
{
    /*
        Get array from arrayList
        %a: Class-_CNac9ArrayListIlE<Int64>&, %b: lass-_CNac9ArrayListIlE<Int64>&
        %1: Struct-_CNat5ArrayIlE<Int64>& = GetElementRef(%a, 0)
        %2: Struct-_CNat5ArrayIlE<Int64>& = GetElementRef(%b, 0)
        %3: Struct-_CNat5ArrayIlE<Int64> = Load(%1)
        %4: Struct-_CNat5ArrayIlE<Int64> = Load(%2)
    */
    auto& loc = apply.GetDebugLocation();
    auto refType = StaticCast<RefType*>(oper1.GetType());
    auto rawArrayType = StaticCast<ClassType*>(refType->GetBaseType());
    CJC_ASSERT(rawArrayType->GetGenericArgs().size() == 1);
    auto elementType = rawArrayType->GetGenericArgs()[0];
    // Get Array type from builder
    auto arrayGeneric = builder.GetStructType("std.core", "Array");
    CJC_NULLPTR_CHECK(arrayGeneric);
    CJC_ASSERT(arrayGeneric->GetGenericArgs().size() == 1);
    auto genericType = StaticCast<GenericType*>(arrayGeneric->GetGenericArgs()[0]);
    std::unordered_map<const GenericType*, Type*> table{{genericType, elementType}};
    auto arrayType = ReplaceRawGenericArgType(*arrayGeneric, table, builder);

    auto arrayRefType = builder.GetType<RefType>(arrayType);

    auto parent = apply.GetParentBlock();
    auto arrayRef1 =
        builder.CreateExpression<GetElementRef>(loc, arrayRefType, &oper1, std::vector<uint64_t>{0}, parent);
    arrayRef1->MoveBefore(&apply);
    auto arrayRef2 =
        builder.CreateExpression<GetElementRef>(loc, arrayRefType, &oper2, std::vector<uint64_t>{0}, parent);
    arrayRef2->MoveBefore(&apply);

    auto array1 = builder.CreateExpression<Load>(loc, arrayType, arrayRef1->GetResult(), parent);
    array1->MoveBefore(&apply);
    auto array2 = builder.CreateExpression<Load>(loc, arrayType, arrayRef2->GetResult(), parent);
    array2->MoveBefore(&apply);
    return std::make_pair(array1->GetResult(), array2->GetResult());
}

Expression* SanitizerCoverage::CreateMemCmpFunc(const std::string& intrinsicName, Type& paramsType,
    const std::vector<Value*>& params, const DebugLocation& loc, Block* parent)
{
    // create String or array memory fuzz function with its name and params
    std::vector<Type*> paramType{&paramsType, &paramsType};
    if (HAS_N_PARAMETER_MEM_CMP_SET.count(intrinsicName) != 0) {
        paramType.push_back(builder.GetUInt32Ty());
    }
    auto funcTy = builder.GetType<FuncType>(paramType, builder.GetUnitTy(), false, true);
    auto memCmpFunc = GenerateForeignFunc(intrinsicName, loc, *funcTy, packageName);
    return CreateNonMemberApply(builder.GetUnitTy(), memCmpFunc, params, parent, builder, loc);
}

std::pair<std::string, std::vector<Value*>> SanitizerCoverage::GetMemFuncSymbols(
    Value& oper1, Value& oper2, Apply& apply)
{
    std::optional<std::string> intrinsicName{""};
    std::vector<Value*> params;
    std::pair<std::string, std::vector<Value*>> defaultValue = {"", {}};
    auto applyCallName = apply.GetCallee()->GetSrcCodeIdentifier();
    if (oper1.GetType()->GetTypeKind() == Type::TypeKind::TYPE_CSTRING) {
        intrinsicName = GetStringSanConvFunc(MemCmpType::CSTRING_TYPE, applyCallName);
        if (intrinsicName == std::nullopt) {
            return defaultValue;
        }
        params = GenerateCStringMemCmp(intrinsicName.value(), oper1, oper2, apply);
    } else if (oper1.GetType()->GetTypeKind() == Type::TypeKind::TYPE_STRUCT) {
        auto structType = StaticCast<StructType*>(oper1.GetType());
        if (structType->GetStructDef()->GetPackageName() != "std.core") {
            return defaultValue;
        } else if (structType->GetStructDef()->GetSrcCodeIdentifier() == "String") {
            intrinsicName = GetStringSanConvFunc(MemCmpType::STRING_TYPE, applyCallName);
            if (intrinsicName == std::nullopt) {
                return defaultValue;
            }
            params = GenerateStringMemCmp(intrinsicName.value(), oper1, oper2, apply);
        } else if (structType->GetStructDef()->GetSrcCodeIdentifier() == "Array") {
            intrinsicName = GetStringSanConvFunc(MemCmpType::ARRAY_TYPE, applyCallName);
            if (intrinsicName == std::nullopt) {
                return defaultValue;
            }
            params = GenerateArrayCmp(intrinsicName.value(), oper1, oper2, apply);
        }
    } else if (oper1.GetType()->GetTypeKind() == Type::TypeKind::TYPE_REFTYPE) {
        auto refType = StaticCast<RefType*>(oper1.GetType());
        if (refType->GetBaseType()->GetTypeKind() != Type::TypeKind::TYPE_CLASS) {
            return defaultValue;
        }
        auto classType = StaticCast<ClassType*>(refType->GetBaseType());
        if (classType->GetClassDef()->GetPackageName() != "std.collection" ||
            classType->GetClassDef()->GetSrcCodeIdentifier() != "ArrayList") {
            return defaultValue;
        }
        intrinsicName = GetStringSanConvFunc(MemCmpType::ARRAYLIST_TYPE, applyCallName);
        if (intrinsicName == std::nullopt) {
            return defaultValue;
        }
        auto [cast0, cast1] = CastArrayListToArray(oper1, oper2, apply);
        params = GenerateArrayCmp(intrinsicName.value(), *cast0, *cast1, apply);
    }
    return std::make_pair(intrinsicName.value(), params);
}

void SanitizerCoverage::InjectTraceMemCmp(Expression& expr, bool isDebug)
{
    if (expr.GetExprKind() != ExprKind::APPLY) {
        return;
    }
    auto applyNode = StaticCast<Apply*>(&expr);
    if (applyNode->GetOperands().size() < 3U) {
        return;
    }
    auto oper1 = applyNode->GetOperand(1U);
    auto oper2 = applyNode->GetOperand(2U);
    auto [intrinsicName, params] = GetMemFuncSymbols(*oper1, *oper2, *applyNode);
    if (intrinsicName == "" || params.empty()) {
        return;
    }
    Expression* syscall;
    auto parent = expr.GetParentBlock();
    if (intrinsicName == SAN_COV_TRACE_MEM_CMP) {
        auto voidPointerType = builder.GetType<CPointerType>(builder.GetVoidTy());
        syscall = CreateMemCmpFunc(intrinsicName, *voidPointerType, params, expr.GetDebugLocation(), parent);
    } else {
        auto charPointerType = builder.GetType<CPointerType>(builder.GetUInt8Ty());
        syscall = CreateMemCmpFunc(intrinsicName, *charPointerType, params, expr.GetDebugLocation(), parent);
    }
    syscall->MoveBefore(&expr);

    if (isDebug) {
        std::cout << "[SanitizerCoverage] Add trace Memory Compare" << ToPosInfo(expr.GetDebugLocation()) << ".\n";
    }
}

void SanitizerCoverage::InsertCoverageAheadBlock(Block& block, bool isDebug)
{
    if (sanCovOption.pcTable) {
        CJC_ASSERT(block.GetTopLevelFunc());
        auto blockLocation = block.GetTopLevelFunc()->GetDebugLocation();
        for (auto it : block.GetExpressions()) {
            if (!it->GetDebugLocation().IsInvalidPos()) {
                blockLocation = it->GetDebugLocation();
                break;
            }
        }
        pcArray.emplace_back(std::make_pair(block.GetTopLevelFunc()->GetSrcCodeIdentifier(), blockLocation));
    }
    CJC_ASSERT(block.GetExpressions().size() > 0);
    auto callList = GenerateCoverageCallByOption(INVALID_LOCATION, isDebug, &block);
    if (sanCovOption.stackDepth && block.IsEntry()) {
        auto stackDepth = GenerateStackDepthExpr(INVALID_LOCATION, isDebug, &block);
        callList.insert(callList.end(), stackDepth.begin(), stackDepth.end());
    }
    auto firstExpr = block.GetExpressions()[0];
    for (auto call : callList) {
        call->MoveBefore(firstExpr);
    }
    bbCounter++;
}

RawArrayAllocate* SanitizerCoverage::CreateArrayForSwitchCaseList(MultiBranch& multiBranch)
{
    const auto& cases = multiBranch.GetCaseVals();
    // case[0 - n]
    // case[0] means switch case size
    // case[1] means bit size of switch condition type
    // case[2 - n] means switch value list

    std::vector<Value*> caseVals;
    // switch case size
    auto& loc = multiBranch.GetDebugLocation();
    auto caseSize = builder.CreateConstantExpression<IntLiteral>(
        loc, builder.GetUInt64Ty(), multiBranch.GetParentBlock(), cases.size());
    caseVals.emplace_back(caseSize->GetResult());
    caseSize->MoveBefore(&multiBranch);

    // bit size of switch condition type
    // only support uint64 as bit size of switch condition type
    auto elemBitSize = builder.CreateConstantExpression<IntLiteral>(
        loc, builder.GetUInt64Ty(), multiBranch.GetParentBlock(), sizeof(uint64_t) * CHAR_BIT);
    caseVals.emplace_back(elemBitSize->GetResult());
    elemBitSize->MoveBefore(&multiBranch);

    // switch value list
    std::for_each(cases.begin(), cases.end(), [this, &caseVals, &loc, &multiBranch](uint64_t caseVal) {
        auto valExpr = builder.CreateConstantExpression<IntLiteral>(
            loc, builder.GetUInt64Ty(), multiBranch.GetParentBlock(), caseVal);
        caseVals.emplace_back(valExpr->GetResult());
        valExpr->MoveBefore(&multiBranch);
    });

    // list size should be case size plus two
    auto listSize = builder.CreateConstantExpression<IntLiteral>(
        loc, builder.GetInt64Ty(), multiBranch.GetParentBlock(), cases.size() + 2U);
    listSize->MoveBefore(&multiBranch);
    // create raw literal array
    auto arrayTy = builder.GetType<RefType>(builder.GetType<RawArrayType>(builder.GetUInt64Ty(), 1U));
    auto rawArrayExpr = builder.CreateExpression<RawArrayAllocate>(
        arrayTy, builder.GetUInt64Ty(), listSize->GetResult(), multiBranch.GetParentBlock());
    return rawArrayExpr;
}

Intrinsic* SanitizerCoverage::CreateRawDataAcquire(const Expression& dataList, Type& elementType) const
{
    auto cPointerTy = builder.GetType<CPointerType>(&elementType);
    auto callContext = IntrisicCallContext {
        .kind = IntrinsicKind::ARRAY_ACQUIRE_RAW_DATA,
        .args = std::vector<Value*>{dataList.GetResult()},
        .instTypeArgs = std::vector<Type*>{&elementType}
    };
    return builder.CreateExpression<Intrinsic>(
        dataList.GetDebugLocation(), cPointerTy, callContext, dataList.GetParentBlock());
}

std::vector<Expression*> SanitizerCoverage::GenerateCoverageCallByOption(
    const DebugLocation& loc, bool isDebug, Block* parent)
{
    std::vector<Expression*> callList;
    if (sanCovOption.tracePCGuard) {
        auto guardList = GeneratePCGuardExpr(loc, isDebug, parent);
        callList.insert(callList.end(), guardList.begin(), guardList.end());
    }
    if (sanCovOption.inline8bitCounters) {
        auto inline8bitList = GenerateInline8bitExpr(loc, isDebug, parent);
        callList.insert(callList.end(), inline8bitList.begin(), inline8bitList.end());
    }
    if (sanCovOption.inlineBoolFlag) {
        auto inlineBoolFlag = GenerateInlineBoolExpr(loc, isDebug, parent);
        callList.insert(callList.end(), inlineBoolFlag.begin(), inlineBoolFlag.end());
    }
    return callList;
}

std::vector<Expression*> SanitizerCoverage::GeneratePCGuardExpr(const DebugLocation& loc, bool isDebug, Block* parent)
{
    auto& callName = SAN_COV_PC_GUARD;
    auto& initItem = SanCovUInt32ArrayInitName;
    // generate pc guard function
    auto cPointTy = builder.GetType<CPointerType>(builder.GetUInt32Ty());
    auto funcTy = builder.GetType<FuncType>(std::vector<Type*>{cPointTy}, builder.GetUnitTy(), false, true);
    auto pcGuardFunc = GenerateForeignFunc(callName, loc, *funcTy, packageName);

    // generate global var
    auto globalVarType = builder.GetType<CPointerType>(builder.GetUInt32Ty());
    auto mangleName = SAN_COV_CTOR2_GLOBAL_VAR_NAME.at(initItem);
    GlobalVar* charArrayTest = GenerateGlobalVar(mangleName, loc, *globalVarType);

    /*
        generate expression as below: globalVar as @CharArrayTest$sancov$_pc_guard
        %a: CPointer<UInt32> = Load(@CharArrayTest$sancov$_pc_guard)
        %b: Int64 = Constant(offset)
        %c: CPointer<UInt32> = Intrinsic/addPointer(%a, %b)
        %d: Unit = Apply(@__sanitizer_cov_trace_pc_guard, %c)
    */
    auto offset = builder.CreateConstantExpression<IntLiteral>(
        loc, builder.GetInt64Ty(), parent, static_cast<uint64_t>(bbCounter));
    Expression* arrayTestLoad = builder.CreateExpression<Load>(loc, globalVarType, charArrayTest, parent);
    auto callContext = IntrisicCallContext {
        .kind = IntrinsicKind::CPOINTER_ADD,
        .args = std::vector<Value*>{arrayTestLoad->GetResult(), offset->GetResult()},
        .instTypeArgs = std::vector<Type*>{builder.GetUInt32Ty()}
    };
    auto addPoint = builder.CreateExpression<Intrinsic>(loc, cPointTy, callContext, parent);
    auto syscall = CreateNonMemberApply(
        builder.GetUnitTy(), pcGuardFunc, std::vector<Value*>{addPoint->GetResult()}, parent, builder, loc);
    if (isDebug) {
        std::cout << "[SanitizerCoverage] Add trace pc guard" << ToPosInfo(loc) << ".\n";
    }
    return std::vector<Expression*>{arrayTestLoad, offset, addPoint, syscall};
}

std::vector<Expression*> SanitizerCoverage::GenerateInline8bitExpr(
    const DebugLocation& loc, bool isDebug, Block* parent)
{
    // generate global var
    auto& initItem = SanCovCharArrayInitName;
    auto globalVarType = builder.GetType<CPointerType>(builder.GetUInt8Ty());
    auto mangleName = SAN_COV_CTOR2_GLOBAL_VAR_NAME.at(initItem);
    GlobalVar* charArrayTest = GenerateGlobalVar(mangleName, loc, *globalVarType);

    /*
        generate expression as below: globalVar as sancov$_8bit_counters
        %a: CPointer<UInt8> = Load(@sancov$_8bit_counters)
        %b: Int64 = Constant(offset)
        %c: UInt8 = Intrinsic/readPointer(%a, %b)  // read (sancov$_8bit_counters + offset)
        %e: UInt8 = Constant(1)
        %f: UInt8 = Add(%c, %e)   // add 1 to the value of (sancov$_8bit_counters + offset)
        %h: Unit = Intrinsic/writePointer(%a, %b, %f)  // output result to (sancov$_8bit_counters + offset)
    */
    auto loadGlobal = builder.CreateExpression<Load>(loc, globalVarType, charArrayTest, parent);
    auto offset = builder.CreateConstantExpression<IntLiteral>(
        loc, builder.GetInt64Ty(), parent, static_cast<uint64_t>(bbCounter));
    auto callContext1 = IntrisicCallContext {
        .kind = IntrinsicKind::CPOINTER_READ,
        .args = std::vector<Value*>{loadGlobal->GetResult(), offset->GetResult()},
        .instTypeArgs = std::vector<Type*>{builder.GetUInt8Ty()}
    };
    auto readPoint = builder.CreateExpression<Intrinsic>(loc, builder.GetUInt8Ty(), callContext1, parent);
    auto one = builder.CreateConstantExpression<IntLiteral>(loc, builder.GetUInt8Ty(), parent, 1UL);
    auto addRes = builder.CreateExpression<BinaryExpression>(loc, builder.GetUInt8Ty(),
        ExprKind::ADD, readPoint->GetResult(), one->GetResult(), OverflowStrategy::WRAPPING, parent);
    auto callContext2 = IntrisicCallContext {
        .kind = IntrinsicKind::CPOINTER_WRITE,
        .args = std::vector<Value*>{loadGlobal->GetResult(), offset->GetResult(), addRes->GetResult()},
        .instTypeArgs = std::vector<Type*>{builder.GetUInt8Ty()}
    };
    auto writePoint = builder.CreateExpression<Intrinsic>(loc, builder.GetUnitTy(), callContext2, parent);
    if (isDebug) {
        std::cout << "[SanitizerCoverage] Add trace inline 8 bit" << ToPosInfo(loc) << ".\n";
    }
    return std::vector<Expression*>{loadGlobal, offset, readPoint, one, addRes, writePoint};
}

std::vector<Expression*> SanitizerCoverage::GenerateInlineBoolExpr(
    const DebugLocation& loc, bool isDebug, Block* parent)
{
    // generate global var
    auto& initItem = SanCovBoolFlagInitName;
    auto globalVarType = builder.GetType<CPointerType>(builder.GetBoolTy());
    auto mangleName = SAN_COV_CTOR2_GLOBAL_VAR_NAME.at(initItem);
    GlobalVar* charArrayTest = GenerateGlobalVar(mangleName, loc, *globalVarType);

    /*
        generate expression as below: globalVar as @CharArrayTest$sancov$_bool_flag
        %a: CPointer<Bool> = Load(@CharArrayTest$sancov$_bool_flag)
        %b: Int64 = Constant(offset)
        %c: Bool = Constant(true)   // set to true
        %d: Unit = Intrinsic/writePointer(%a, %b, %c)
    */
    auto loadGlobal = builder.CreateExpression<Load>(loc, globalVarType, charArrayTest, parent);
    auto offset = builder.CreateConstantExpression<IntLiteral>(
        loc, builder.GetInt64Ty(), parent, static_cast<uint64_t>(bbCounter));
    auto boolTrue = builder.CreateConstantExpression<BoolLiteral>(loc, builder.GetBoolTy(), parent, true);
    auto callContext = IntrisicCallContext {
        .kind = IntrinsicKind::CPOINTER_WRITE,
        .args = std::vector<Value*>{loadGlobal->GetResult(), offset->GetResult(), boolTrue->GetResult()},
        .instTypeArgs = std::vector<Type*>{builder.GetBoolTy()}
    };
    auto writePoint = builder.CreateExpression<Intrinsic>(loc, builder.GetUnitTy(), callContext, parent);
    if (isDebug) {
        std::cout << "[SanitizerCoverage] Add trace inline bool" << ToPosInfo(loc) << ".\n";
    }
    return std::vector<Expression*>{loadGlobal, offset, boolTrue, writePoint};
}

std::vector<Expression*> SanitizerCoverage::GenerateStackDepthExpr(
    const DebugLocation& loc, bool isDebug, Block* parent)
{
    auto& callName = SanCovUpdateStackDepth;
    auto funcTy = builder.GetType<FuncType>(std::vector<Type*>{}, builder.GetUnitTy(), false, true);
    auto callee = GenerateForeignFunc(callName, loc, *funcTy, packageName);
    auto syscall = CreateNonMemberApply(builder.GetUnitTy(), callee, std::vector<Value*>{}, parent, builder, loc);

    if (isDebug) {
        std::cout << "[SanitizerCoverage] Add trace stage depth" << ToPosInfo(loc) << ".\n";
    }
    return std::vector<Expression*>{syscall};
}

GlobalVar* SanitizerCoverage::GenerateGlobalVar(
    const std::string& globalVarName, const DebugLocation& loc, Type& globalType)
{
    if (auto it = globalVarBag.find(globalVarName); it != globalVarBag.end()) {
        return it->second;
    }
    auto globalVar = builder.CreateGlobalVar(
        loc, builder.GetType<RefType>(&globalType), globalVarName, globalVarName, "", packageName);
    globalVar->EnableAttr(Attribute::READONLY);
    globalVar->EnableAttr(Attribute::COMPILER_ADD);
    globalVarBag.insert(std::make_pair(globalVarName, globalVar));
    return globalVar;
}

GlobalVar* SanitizerCoverage::GetGlobalVar(const std::string& globalVarName)
{
    if (globalVarBag.count(globalVarName) == 0) {
        return nullptr;
    }
    return globalVarBag.at(globalVarName);
}

ImportedValue* SanitizerCoverage::GenerateForeignFunc(const std::string& globalFuncName,
    [[maybe_unused]] const DebugLocation& loc, Type& funcType, const std::string& packName)
{
    if (auto it = funcBag.find(globalFuncName); it != funcBag.end()) {
        return it->second;
    }
    auto func = builder.CreateImportedVarOrFunc<ImportedFunc>(&funcType, globalFuncName, globalFuncName, "", packName);
    func->EnableAttr(Attribute::FOREIGN);
    funcBag.insert(std::make_pair(globalFuncName, func));
    return func;
}

ImportedValue* SanitizerCoverage::GetImportedFunc(const std::string& mangledName)
{
    auto it = funcBag.find(mangledName);
    CJC_ASSERT(it != funcBag.end());
    return it->second;
}

Func* SanitizerCoverage::CreateInitFunc(
    const std::string& name, FuncType& funcType, [[maybe_unused]] const DebugLocation& loc)
{
    auto func = builder.CreateFunc(loc, &funcType, name, name, "", packageName);
    auto body = builder.CreateBlockGroup(*func);
    func->InitBody(*body);
    func->EnableAttr(Attribute::NO_INLINE);
    func->EnableAttr(Attribute::NO_REFLECT_INFO);
    func->EnableAttr(Attribute::COMPILER_ADD);
    func->SetFuncKind(FuncKind::GLOBALVAR_INIT);
    func->Set<LinkTypeInfo>(Linkage::INTERNAL);
    return func;
}

Func* SanitizerCoverage::CreateArrayInitFunc(const std::string& initItemName, Type& initType)
{
    auto funcTy = builder.GetType<FuncType>(std::vector<Type*>{}, builder.GetVoidTy());
    auto initName = MANGLE_CANGJIE_PREFIX + MANGLE_GLOBAL_VARIABLE_INIT_PREFIX + MangleUtils::MangleName("default") +
        MangleUtils::MangleName("0." + SAN_COV_CTOR2_GLOBAL_VAR_NAME.at(initItemName)) +
        MANGLE_FUNC_PARAM_TYPE_PREFIX + MANGLE_VOID_TY_SUFFIX;
    auto func = CreateInitFunc(initName, *funcTy, INVALID_LOCATION);
    auto body = func->GetBody();
    auto block0 = builder.CreateBlock(body);
    body->SetEntryBlock(block0);

    auto counterVal = builder.CreateConstantExpression<IntLiteral>(
        INVALID_LOCATION, builder.GetUInt64Ty(), block0, static_cast<uint64_t>(bbCounter));
    block0->AppendExpression(counterVal);
    auto cPointTy = builder.GetType<CPointerType>(&initType);
    auto guardCtorTy = builder.GetType<FuncType>(std::vector<Type*>{builder.GetUInt64Ty()}, cPointTy, false, true);
    auto guardCtorFunc = GenerateForeignFunc(initItemName, INVALID_LOCATION, *guardCtorTy, packageName);
    auto apply = CreateNonMemberApply(
        cPointTy, guardCtorFunc, std::vector<Value*>{counterVal->GetResult()}, block0, builder, INVALID_LOCATION);
    block0->AppendExpression(apply);
    auto globalVal = GetGlobalVar(SAN_COV_CTOR2_GLOBAL_VAR_NAME.at(initItemName));
    CJC_NULLPTR_CHECK(globalVal);
    block0->AppendExpression(
        builder.CreateExpression<Store>(INVALID_LOCATION, builder.GetUnitTy(), apply->GetResult(), globalVal, block0));
    globalVal->SetInitFunc(*func);

    block0->AppendExpression(builder.CreateTerminator<Exit>(INVALID_LOCATION, block0));
    return func;
}

Func* SanitizerCoverage::CreatePCTableInitFunc()
{
    auto funcTy = builder.GetType<FuncType>(std::vector<Type*>{}, builder.GetVoidTy());
    auto func = CreateInitFunc(MANGLE_CANGJIE_PREFIX + MANGLE_GLOBAL_VARIABLE_INIT_PREFIX +
        MangleUtils::MangleName("default") + MangleUtils::MangleName("0.pc_table") + MANGLE_FUNC_PARAM_TYPE_PREFIX +
        MANGLE_VOID_TY_SUFFIX, *funcTy, INVALID_LOCATION);
    auto body = func->GetBody();
    auto block0 = builder.CreateBlock(body);
    body->SetEntryBlock(block0);
    // import malloc func
    auto mallocStringFunc = GetImportedFunc(FUNC_MANGLE_NAME_MALLOC_CSTRING);
    // pc table size
    auto funcSize =
        builder.CreateConstantExpression<IntLiteral>(INVALID_LOCATION, builder.GetInt64Ty(), block0, pcArray.size());
    block0->AppendExpression(funcSize);
    // create package
    auto packExpr = builder.CreateConstantExpression<StringLiteral>(
        INVALID_LOCATION, builder.GetStringTy(), block0, packageName);
    block0->AppendExpression(packExpr);
    auto libc = builder.GetStructType("std.core", "LibC");
    auto libcRef = builder.GetType<RefType>(libc);
    auto packCString = CreateMemberApply(builder.GetCStringTy(), mallocStringFunc, libcRef,
        {packExpr->GetResult()}, block0, builder, INVALID_LOCATION);
    block0->AppendExpression(packCString);
    // create arrays of function name, file name and line number
    std::vector<Value*> funcNameArray;
    std::vector<Value*> fileNameArray;
    std::vector<Value*> lineNumberArray;
    for (auto pcIter : pcArray) {
        auto funcName = builder.CreateConstantExpression<StringLiteral>(
            INVALID_LOCATION, builder.GetStringTy(), block0, pcIter.first);
        block0->AppendExpression(funcName);
        auto cString = CreateMemberApply(builder.GetCStringTy(), mallocStringFunc, libcRef,
            {funcName->GetResult()}, block0, builder, INVALID_LOCATION);
        block0->AppendExpression(cString);
        funcNameArray.push_back(cString->GetResult());
        auto fileName = builder.CreateConstantExpression<StringLiteral>(INVALID_LOCATION, builder.GetStringTy(), block0,
            builder.GetChirContext().GetSourceFileName(pcIter.second.GetFileID()));
        block0->AppendExpression(fileName);
        auto fileNameCString = CreateMemberApply(builder.GetCStringTy(), mallocStringFunc, libcRef,
            {fileName->GetResult()}, block0, builder, INVALID_LOCATION);
        block0->AppendExpression(fileNameCString);
        fileNameArray.push_back(fileNameCString->GetResult());
        auto lineId = builder.CreateConstantExpression<IntLiteral>(
            INVALID_LOCATION, builder.GetUInt64Ty(), block0, pcIter.second.GetBeginPos().line);
        block0->AppendExpression(lineId);
        lineNumberArray.push_back(lineId->GetResult());
    }
    auto rawDataAcquireString =
        CreateRawDataAcquire(*builder.GetCStringTy(), funcNameArray, *funcSize->GetResult(), *block0);
    auto rawDataAcquireFileName =
        CreateRawDataAcquire(*builder.GetCStringTy(), fileNameArray, *funcSize->GetResult(), *block0);
    auto rawDataAcquireLine =
        CreateRawDataAcquire(*builder.GetUInt64Ty(), lineNumberArray, *funcSize->GetResult(), *block0);
    // create apply for pc table init
    auto paramTypeList =
        std::vector<Type*>{builder.GetCStringTy(), builder.GetUInt64Ty(), rawDataAcquireString->GetResult()->GetType(),
            rawDataAcquireFileName->GetResult()->GetType(), rawDataAcquireLine->GetResult()->GetType()};
    auto pcInitType = builder.GetType<FuncType>(paramTypeList, builder.GetUnitTy(), false, true);
    auto pcInitFunc = GenerateForeignFunc(SanCovCreatePCTable, INVALID_LOCATION, *pcInitType, packageName);
    auto typecast = builder.CreateExpression<TypeCast>(builder.GetUInt64Ty(), funcSize->GetResult(), block0);
    block0->AppendExpression(typecast);
    block0->AppendExpression(CreateNonMemberApply(builder.GetUnitTy(), pcInitFunc,
        std::vector<Value*>{packCString->GetResult(), typecast->GetResult(), rawDataAcquireString->GetResult(),
            rawDataAcquireFileName->GetResult(), rawDataAcquireLine->GetResult()},
        block0, builder, INVALID_LOCATION));
    block0->AppendExpression(builder.CreateTerminator<Exit>(INVALID_LOCATION, block0));
    return func;
}

Intrinsic* SanitizerCoverage::CreateRawDataAcquire(
    Type& type, const std::vector<Value*>& list, Value& size, Block& block)
{
    auto arrayTy = builder.GetType<RefType>(builder.GetType<RawArrayType>(&type, 1u));
    auto expr = builder.CreateExpression<RawArrayAllocate>(arrayTy, &type, &size, &block);
    block.AppendExpression(expr);
    auto arrayVar = expr->GetResult();
    block.AppendExpression(builder.CreateExpression<RawArrayLiteralInit>(builder.GetUnitTy(), arrayVar, list, &block));
    auto cPointerTy = builder.GetType<CPointerType>(&type);
    auto callContext = IntrisicCallContext {
        .kind = IntrinsicKind::ARRAY_ACQUIRE_RAW_DATA,
        .args = std::vector<Value*>{arrayVar},
        .instTypeArgs = std::vector<Type*>{&type}
    };
    auto rawDataAcquire = builder.CreateExpression<Intrinsic>(INVALID_LOCATION, cPointerTy, callContext, &block);
    block.AppendExpression(rawDataAcquire);
    return rawDataAcquire;
}

void SanitizerCoverage::CreateTopLevelInitFunc(const std::vector<Func*>& initFuncs, const Func& globalInitFunc)
{
    // create top level init func
    if (initFuncs.empty()) {
        return;
    }
    auto funcTy = builder.GetType<FuncType>(std::vector<Type*>{}, builder.GetVoidTy());
    auto initName = MANGLE_CANGJIE_PREFIX + MANGLE_GLOBAL_FILE_INIT_PREFIX + MangleUtils::MangleName("default") +
        MangleUtils::MangleName("0.sancov") + MANGLE_FUNC_PARAM_TYPE_PREFIX + MANGLE_VOID_TY_SUFFIX;
    auto initFunc = CreateInitFunc(initName, *funcTy, INVALID_LOCATION);
    auto body = initFunc->GetBody();
    auto block0 = builder.CreateBlock(body);
    body->SetEntryBlock(block0);
    for (auto init : initFuncs) {
        block0->AppendExpression(CreateNonMemberApply(
            init->GetReturnType(), init, std::vector<Value*>{}, block0, builder, INVALID_LOCATION));
    }
    block0->AppendExpression(builder.CreateTerminator<Exit>(INVALID_LOCATION, block0));
    // call top level fun in global init func
    auto initExpr = CreateNonMemberApply(builder.GetVoidTy(), initFunc, std::vector<Value*>{},
        globalInitFunc.GetEntryBlock(), builder, INVALID_LOCATION);
    AddExpressionsToGlobalInitFunc(globalInitFunc, std::vector<Expression*>{initExpr});
}

void SanitizerCoverage::GenerateInitFunc(const Func& globalInitFunc, bool isDebug)
{
    // Note: insert array init func at the first of package.
    // FuncSize and basic block size are evaluated  at compile stage, firstly set 0 as placeHolder and then updated to
    // the correct value
    bool needInsertArray = sanCovOption.inlineBoolFlag || sanCovOption.inline8bitCounters || sanCovOption.tracePCGuard;
    if (!needInsertArray) {
        return;
    }

    std::vector<Func*> initFuncs;

    if (sanCovOption.inlineBoolFlag &&
        GetGlobalVar(SAN_COV_CTOR2_GLOBAL_VAR_NAME.at(SanCovBoolFlagInitName)) != nullptr) {
        initFuncs.emplace_back(CreateArrayInitFunc(SanCovBoolFlagInitName, *builder.GetBoolTy()));
    }
    if (sanCovOption.inline8bitCounters &&
        GetGlobalVar(SAN_COV_CTOR2_GLOBAL_VAR_NAME.at(SanCovCharArrayInitName)) != nullptr) {
        initFuncs.emplace_back(CreateArrayInitFunc(SanCovCharArrayInitName, *builder.GetUInt8Ty()));
    }
    if (sanCovOption.tracePCGuard && funcBag.count(SAN_COV_PC_GUARD) != 0 &&
        GetGlobalVar(SAN_COV_CTOR2_GLOBAL_VAR_NAME.at(SanCovUInt32ArrayInitName)) != nullptr) {
        initFuncs.emplace_back(CreateArrayInitFunc(SanCovUInt32ArrayInitName, *builder.GetUInt32Ty()));
    }
    if (sanCovOption.pcTable && !pcArray.empty()) {
        initFuncs.emplace_back(CreatePCTableInitFunc());
        if (isDebug) {
            std::cout << "[SanitizerCoverage] Add trace pc table" << ToPosInfo(INVALID_LOCATION) << ".\n";
        }
    }
    CreateTopLevelInitFunc(initFuncs, globalInitFunc);
}

} // namespace Cangjie::CHIR
