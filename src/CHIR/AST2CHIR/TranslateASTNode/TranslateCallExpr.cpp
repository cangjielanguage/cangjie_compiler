// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/AST/AttributePack.h"
#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/Utils/ConstantUtils.h"
#include "cangjie/CHIR/IR/IntrinsicKind.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/Utils/ConstantsUtils.h"

namespace Cangjie::CHIR {
static const std::unordered_map<std::string, IntrinsicKind> coreIntrinsicMap = {
    {SIZE_OF_NAME, SIZE_OF},
    {ALIGN_OF_NAME, ALIGN_OF},
    {ARRAY_ACQUIRE_RAW_DATA_NAME, ARRAY_ACQUIRE_RAW_DATA},
    {ARRAY_ACQUIRE_RAW_DATA2_NAME, ARRAY_ACQUIRE_RAW_DATA},
    {ARRAY_RELEASE_RAW_DATA_NAME, ARRAY_RELEASE_RAW_DATA},
    {ARRAY_RELEASE_RAW_DATA2_NAME, ARRAY_RELEASE_RAW_DATA},
    {ARRAY_BUILT_IN_COPY_TO_NAME, ARRAY_BUILT_IN_COPY_TO},
    {ARRAY_BUILT_IN_COPY_TO2_NAME, ARRAY_BUILT_IN_COPY_TO},
    {ARRAY_BUILT_IN_COPY_TO3_NAME, ARRAY_BUILT_IN_COPY_TO},
    {ARRAY_GET_NAME, ARRAY_GET},
    {ARRAY_SET_NAME, ARRAY_SET},
    {ARRAY_SET2_NAME, ARRAY_SET},
    {ARRAY_SET3_NAME, ARRAY_SET},
    {ARRAY_GET_UNCHECKED_NAME, ARRAY_GET_UNCHECKED},
    {ARRAY_GET_UNCHECKED2_NAME, ARRAY_GET_UNCHECKED},
    {ARRAY_GET_UNCHECKED3_NAME, ARRAY_GET_UNCHECKED},

    {ARRAY_GET_REF_UNCHECKED_NAME, ARRAY_GET_REF_UNCHECKED},

    {ARRAY_SET_UNCHECKED_NAME, ARRAY_SET_UNCHECKED},
    {ARRAY_SET_UNCHECKED2_NAME, ARRAY_SET_UNCHECKED},
    {ARRAY_SET_UNCHECKED3_NAME, ARRAY_SET_UNCHECKED},
    {ARRAY_SIZE_NAME, ARRAY_SIZE},
    {ARRAY_CLONE_NAME, ARRAY_CLONE},
    {ARRAY_SLICE_INIT_NAME, ARRAY_SLICE_INIT},
    {ARRAY_SLICE_NAME, ARRAY_SLICE},
    {ARRAY_SLICE_RAWARRAY_NAME, ARRAY_SLICE_RAWARRAY},
    {ARRAY_SLICE_START_NAME, ARRAY_SLICE_START},
    {ARRAY_SLICE_SIZE_NAME, ARRAY_SLICE_SIZE},
    {ARRAY_SLICE_GET_ELEMENT_NAME, ARRAY_SLICE_GET_ELEMENT},
    {ARRAY_SLICE_SET_ELEMENT_NAME, ARRAY_SLICE_SET_ELEMENT},
    {ARRAY_SLICE_GET_ELEMENT_UNCHECKED_NAME, ARRAY_SLICE_GET_ELEMENT_UNCHECKED},
    {ARRAY_SLICE_SET_ELEMENT_UNCHECKED_NAME, ARRAY_SLICE_SET_ELEMENT_UNCHECKED},

    {VECTOR_COMPARE_32_NAME, VECTOR_COMPARE_32},
    {VECTOR_INDEX_BYTE_32_NAME, VECTOR_INDEX_BYTE_32},

    {FILL_IN_STACK_TRACE_NAME, FILL_IN_STACK_TRACE},
    {DECODE_STACK_TRACE_NAME, DECODE_STACK_TRACE},
    {DUMP_CURRENT_THREAD_INFO_NAME, DUMP_CURRENT_THREAD_INFO},
    {DUMP_ALL_THREADS_INFO_NAME, DUMP_ALL_THREADS_INFO},

    {CPOINTER_GET_POINTER_ADDRESS_NAME, CPOINTER_GET_POINTER_ADDRESS},
    {CPOINTER_READ_NAME, CPOINTER_READ},
    {CPOINTER_WRITE_NAME, CPOINTER_WRITE},
    {CPOINTER_ADD_NAME, CPOINTER_ADD},

    {CSTRING_CONVERT_CSTR_TO_PTR_NAME, CSTRING_CONVERT_CSTR_TO_PTR},
    {BIT_CAST_NAME, BIT_CAST},

    {FUTURE_INIT_NAME, FUTURE_INIT},

    {IS_THREAD_OBJECT_INITED_NAME, IS_THREAD_OBJECT_INITED},
    {GET_THREAD_OBJECT_NAME, GET_THREAD_OBJECT},
    {SET_THREAD_OBJECT_NAME, SET_THREAD_OBJECT},

    {FUTURE_IS_COMPLETE_NAME, FUTURE_IS_COMPLETE},
    {FUTURE_WAIT_NAME, FUTURE_WAIT},
    {FUTURE_NOTIFYALL_NAME, FUTURE_NOTIFYALL},

    {OBJECT_REFEQ_NAME, OBJECT_REFEQ},

    {RAW_ARRAY_REFEQ_NAME, RAW_ARRAY_REFEQ},

    {OBJECT_ZERO_VALUE_NAME, OBJECT_ZERO_VALUE},
    {OBJECT_ZERO_VALUE2_NAME, OBJECT_ZERO_VALUE},

    {SOURCE_FILE_NAME, SOURCE_FILE},
    {SOURCE_LINE_NAME, SOURCE_LINE},

    {IDENTITY_HASHCODE_NAME, IDENTITY_HASHCODE},
    {IDENTITY_HASHCODE_FOR_ARRAY_NAME, IDENTITY_HASHCODE_FOR_ARRAY},
    {STRLEN_NAME, STRLEN},
    {MEMCPY_S_NAME, MEMCPY_S},
    {MEMSET_S_NAME, MEMSET_S},
    {FREE_NAME, FREE},
    {MALLOC_NAME, MALLOC},
    {STRCMP_NAME, STRCMP},
    {MEMCMP_NAME, MEMCMP},
    {STRNCMP_NAME, STRNCMP},
    {STRCASECMP_NAME, STRCASECMP},

    // atomic for Thread Class

    {ATOMIC_LOAD_NAME, ATOMIC_LOAD},
    {ATOMIC_STORE_NAME, ATOMIC_STORE},
    {ATOMIC_FETCH_ADD_NAME, ATOMIC_FETCH_ADD},
    {ATOMIC_COMPARE_AND_SWAP_NAME, ATOMIC_COMPARE_AND_SWAP},

    {SLEEP_NAME, SLEEP},

    {GET_TYPE_FOR_TYPE_PARAMETER_NAME, GET_TYPE_FOR_TYPE_PARAMETER},
    {IS_SUBTYPE_TYPES_NAME, IS_SUBTYPE_TYPES},

    {std::string{EXCLUSIVE_SCOPE_NAME}, EXCLUSIVE_SCOPE},
};

static const std::unordered_map<std::string, IntrinsicKind> overflowIntrinsicMap = {
    {OVERFLOW_CHECKED_ADD_NAME, OVERFLOW_CHECKED_ADD},
    {OVERFLOW_CHECKED_SUB_NAME, OVERFLOW_CHECKED_SUB},
    {OVERFLOW_CHECKED_MUL_NAME, OVERFLOW_CHECKED_MUL},
    {OVERFLOW_CHECKED_DIV_NAME, OVERFLOW_CHECKED_DIV},
    {OVERFLOW_CHECKED_MOD_NAME, OVERFLOW_CHECKED_MOD},
    {OVERFLOW_CHECKED_POW_NAME, OVERFLOW_CHECKED_POW},
    {OVERFLOW_CHECKED_INC_NAME, OVERFLOW_CHECKED_INC},
    {OVERFLOW_CHECKED_DEC_NAME, OVERFLOW_CHECKED_DEC},
    {OVERFLOW_CHECKED_NEG_NAME, OVERFLOW_CHECKED_NEG},
    {OVERFLOW_THROWING_ADD_NAME, OVERFLOW_THROWING_ADD},
    {OVERFLOW_THROWING_SUB_NAME, OVERFLOW_THROWING_SUB},
    {OVERFLOW_THROWING_MUL_NAME, OVERFLOW_THROWING_MUL},
    {OVERFLOW_THROWING_DIV_NAME, OVERFLOW_THROWING_DIV},
    {OVERFLOW_THROWING_MOD_NAME, OVERFLOW_THROWING_MOD},
    {OVERFLOW_THROWING_POW_NAME, OVERFLOW_THROWING_POW},
    {OVERFLOW_THROWING_INC_NAME, OVERFLOW_THROWING_INC},
    {OVERFLOW_THROWING_DEC_NAME, OVERFLOW_THROWING_DEC},
    {OVERFLOW_THROWING_NEG_NAME, OVERFLOW_THROWING_NEG},
    {OVERFLOW_SATURATING_ADD_NAME, OVERFLOW_SATURATING_ADD},
    {OVERFLOW_SATURATING_SUB_NAME, OVERFLOW_SATURATING_SUB},
    {OVERFLOW_SATURATING_MUL_NAME, OVERFLOW_SATURATING_MUL},
    {OVERFLOW_SATURATING_DIV_NAME, OVERFLOW_SATURATING_DIV},
    {OVERFLOW_SATURATING_MOD_NAME, OVERFLOW_SATURATING_MOD},
    {OVERFLOW_SATURATING_POW_NAME, OVERFLOW_SATURATING_POW},
    {OVERFLOW_SATURATING_INC_NAME, OVERFLOW_SATURATING_INC},
    {OVERFLOW_SATURATING_DEC_NAME, OVERFLOW_SATURATING_DEC},
    {OVERFLOW_SATURATING_NEG_NAME, OVERFLOW_SATURATING_NEG},
    {OVERFLOW_WRAPPING_ADD_NAME, OVERFLOW_WRAPPING_ADD},
    {OVERFLOW_WRAPPING_SUB_NAME, OVERFLOW_WRAPPING_SUB},
    {OVERFLOW_WRAPPING_MUL_NAME, OVERFLOW_WRAPPING_MUL},
    {OVERFLOW_WRAPPING_DIV_NAME, OVERFLOW_WRAPPING_DIV},
    {OVERFLOW_WRAPPING_MOD_NAME, OVERFLOW_WRAPPING_MOD},
    {OVERFLOW_WRAPPING_POW_NAME, OVERFLOW_WRAPPING_POW},
    {OVERFLOW_WRAPPING_INC_NAME, OVERFLOW_WRAPPING_INC},
    {OVERFLOW_WRAPPING_DEC_NAME, OVERFLOW_WRAPPING_DEC},
    {OVERFLOW_WRAPPING_NEG_NAME, OVERFLOW_WRAPPING_NEG},
};
static const std::unordered_map<std::string, IntrinsicKind> reflectIntrinsicMap = {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#define REFLECTION_KIND_TO_RUNTIME_FUNCTION(REFLECTION_KIND, CJ_FUNCTION, RUNTIME_FUNCTION)                            \
    {REFLECTION_KIND##_NAME, REFLECTION_KIND},
#include "cangjie/CHIR/Utils/LLVMReflectionIntrinsics.def"
#undef REFLECTION_KIND_TO_RUNTIME_FUNCTION
#endif
};

static const std::unordered_map<std::string, IntrinsicKind> interOpIntrinsicMap = {
    {CROSS_ACCESS_BARRIER_NAME, CROSS_ACCESS_BARRIER},
    {CREATE_EXPORT_HANDLE_NAME, CREATE_EXPORT_HANDLE},
    {GET_EXPORTED_REF_NAME, GET_EXPORTED_REF},
    {REMOVE_EXPORTED_REF_NAME, REMOVE_EXPORTED_REF}
};

static const std::unordered_map<std::string, IntrinsicKind> ohosArkInteropIntrinsicMap = {
    {"getJSLambdaAddr", GET_JSLAMBDA_ADDR}
};

static const std::unordered_map<std::string, IntrinsicKind> cjnativeSyncIntrinsicMap = {
    {ATOMIC_LOAD_NAME, ATOMIC_LOAD},
    {ATOMIC_STORE_NAME, ATOMIC_STORE},
    {ATOMIC_SWAP_NAME, ATOMIC_SWAP},
    {ATOMIC_COMPARE_AND_SWAP_NAME, ATOMIC_COMPARE_AND_SWAP},
    {ATOMIC_FETCH_ADD_NAME, ATOMIC_FETCH_ADD},
    {ATOMIC_FETCH_SUB_NAME, ATOMIC_FETCH_SUB},
    {ATOMIC_FETCH_AND_NAME, ATOMIC_FETCH_AND},
    {ATOMIC_FETCH_OR_NAME, ATOMIC_FETCH_OR},
    {ATOMIC_FETCH_XOR_NAME, ATOMIC_FETCH_XOR},
    {MUTEX_INIT_NAME, MUTEX_INIT},
    {MUTEX_LOCK_NAME, CJ_MUTEX_LOCK},
    {MUTEX_TRY_LOCK_NAME, MUTEX_TRY_LOCK},
    {MUTEX_CHECK_STATUS_NAME, MUTEX_CHECK_STATUS},
    {MUTEX_UNLOCK_NAME, MUTEX_UNLOCK},
    {WAITQUEUE_INIT_NAME, WAITQUEUE_INIT},
    {MONITOR_INIT_NAME, MONITOR_INIT},
    {MOITIOR_WAIT_NAME, MOITIOR_WAIT},
    {MOITIOR_NOTIFY_NAME, MOITIOR_NOTIFY},
    {MOITIOR_NOTIFY_ALL_NAME, MOITIOR_NOTIFY_ALL},
    {MULTICONDITION_WAIT_NAME, MULTICONDITION_WAIT},
    {MULTICONDITION_NOTIFY_NAME, MULTICONDITION_NOTIFY},
    {MULTICONDITION_NOTIFY_ALL_NAME, MULTICONDITION_NOTIFY_ALL},
};

static const std::unordered_map<std::string, IntrinsicKind> runtimeIntrinsicMap = {
    {INVOKE_GC_NAME, INVOKE_GC},
    {SET_GC_THRESHOLD_NAME, SET_GC_THRESHOLD},
    {DUMP_CJ_HEAP_DATA_NAME, DUMP_CJ_HEAP_DATA},
    {GET_GC_COUNT_NAME, GET_GC_COUNT},
    {GET_GC_TIME_US_NAME, GET_GC_TIME_US},
    {GET_GC_FREED_SIZE_NAME, GET_GC_FREED_SIZE},
    {START_CJ_CPU_PROFILING_NAME, START_CJ_CPU_PROFILING},
    {STOP_CJ_CPU_PROFILING_NAME, STOP_CJ_CPU_PROFILING},
    {BLACK_BOX_NAME, BLACK_BOX},
    {GET_MAX_HEAP_SIZE_NAME, GET_MAX_HEAP_SIZE},
    {GET_ALLOCATE_HEAP_SIZE_NAME, GET_ALLOCATE_HEAP_SIZE},
    {GET_REAL_HEAP_SIZE_NAME, GET_REAL_HEAP_SIZE},
    {GET_THREAD_NUMBER_NAME, GET_THREAD_NUMBER},
    {GET_BLOCKING_THREAD_NUMBER_NAME, GET_BLOCKING_THREAD_NUMBER},
    {GET_NATIVE_THREAD_NUMBER_NAME, GET_NATIVE_THREAD_NUMBER},
    {CROSS_ACCESS_BARRIER_NAME, CROSS_ACCESS_BARRIER},
    {CREATE_EXPORT_HANDLE_NAME, CREATE_EXPORT_HANDLE},
    {GET_EXPORTED_REF_NAME, GET_EXPORTED_REF},
    {REMOVE_EXPORTED_REF_NAME, REMOVE_EXPORTED_REF},
    {FUNC_REFEQ_NAME, FUNC_REFEQ},
};
static const std::unordered_map<std::string, IntrinsicKind> mathIntrinsicMap = {
    {ABS_NAME, ABS},
    {FABS_NAME, FABS},
    {FLOOR_NAME, FLOOR},
    {CEIL_NAME, CEIL},
    {TRUC_NAME, TRUNC},
    {SIN_NAME, SIN},
    {COS_NAME, COS},
    {EXP_NAME, EXP},
    {EXP2_NAME, EXP2},
    {LOG_NAME, LOG},
    {LOG2_NAME, LOG2},
    {LOG10_NAME, LOG10},
    {SQRT_NAME, SQRT},
    {ROUND_NAME, ROUND},
    {POW_NAME, POW},
    {POWI_NAME, POWI},
};

const std::unordered_map<Cangjie::TokenKind, BinaryExprKind> tokenKindToBinaryExprKind = {
    {Cangjie::TokenKind::ADD, BinaryExprKind::ADD},
    {Cangjie::TokenKind::SUB, BinaryExprKind::SUB},
    {Cangjie::TokenKind::MUL, BinaryExprKind::MUL},
    {Cangjie::TokenKind::DIV, BinaryExprKind::DIV},
    {Cangjie::TokenKind::MOD, BinaryExprKind::MOD},
    {Cangjie::TokenKind::EXP, BinaryExprKind::EXP},
    {Cangjie::TokenKind::AND, BinaryExprKind::AND},
    {Cangjie::TokenKind::OR, BinaryExprKind::OR},
    {Cangjie::TokenKind::BITAND, BinaryExprKind::BITAND},
    {Cangjie::TokenKind::BITOR, BinaryExprKind::BITOR},
    {Cangjie::TokenKind::BITXOR, BinaryExprKind::BITXOR},
    {Cangjie::TokenKind::LSHIFT, BinaryExprKind::LSHIFT},
    {Cangjie::TokenKind::RSHIFT, BinaryExprKind::RSHIFT},
    {Cangjie::TokenKind::LT, BinaryExprKind::LT},
    {Cangjie::TokenKind::GT, BinaryExprKind::GT},
    {Cangjie::TokenKind::LE, BinaryExprKind::LE},
    {Cangjie::TokenKind::GE, BinaryExprKind::GE},
    {Cangjie::TokenKind::NOTEQ, BinaryExprKind::NOTEQUAL},
    {Cangjie::TokenKind::EQUAL, BinaryExprKind::EQUAL},
};

namespace {
const std::unordered_map<std::string, const std::unordered_map<std::string, IntrinsicKind>> PACKAGE_MAP = {
    {CORE_PACKAGE_NAME, coreIntrinsicMap},
    {SYNC_PACKAGE_NAME, cjnativeSyncIntrinsicMap},
    {OVERFLOW_PACKAGE_NAME, overflowIntrinsicMap},
    {RUNTIME_PACKAGE_NAME, runtimeIntrinsicMap},
    {REFLECT_PACKAGE_NAME, reflectIntrinsicMap},
    {MATH_PACKAGE_NAME, mathIntrinsicMap},
    {INTEROP_PACKAGE_NAME, interOpIntrinsicMap},
    {"ohos.ark_interop", ohosArkInteropIntrinsicMap}
};

// Below are instrinsics without a source-level declaration, their declaration should be dynamically generated.
const std::unordered_map<std::string, IntrinsicKind> HEADLESS_INTRINSICS = {
    {GET_TYPE_FOR_TYPE_PARAMETER_NAME, IntrinsicKind::GET_TYPE_FOR_TYPE_PARAMETER},
    {IS_SUBTYPE_TYPES_NAME, IntrinsicKind::IS_SUBTYPE_TYPES},
};

const static std::unordered_map<std::string, const std::unordered_map<std::string, IntrinsicKind>> packageMap = {
    {CORE_PACKAGE_NAME, coreIntrinsicMap},
    {SYNC_PACKAGE_NAME, cjnativeSyncIntrinsicMap},
    {OVERFLOW_PACKAGE_NAME, overflowIntrinsicMap},
    {RUNTIME_PACKAGE_NAME, runtimeIntrinsicMap},
    {REFLECT_PACKAGE_NAME, reflectIntrinsicMap},
    {MATH_PACKAGE_NAME, mathIntrinsicMap},
    {INTEROP_PACKAGE_NAME, interOpIntrinsicMap},
    {"ohos.ark_interop", ohosArkInteropIntrinsicMap}
};
} // namespace

// Conditions to check if this is a call to member func (constructor is not counted here)
static bool IsNonConstructorMemberFunc(const AST::FuncDecl* func)
{
    return func && !func->TestAttr(AST::Attribute::CONSTRUCTOR) &&
        !func->TestAttr(AST::Attribute::ENUM_CONSTRUCTOR) && func->IsMemberDecl();
}

std::vector<Type*> Translator::TranslateASTTypes(const std::vector<AST::ModalTy>& genericInfos)
{
    std::vector<Type*> ts;
    for (auto& genericInfo : genericInfos) {
        ts.emplace_back(TranslateType(genericInfo));
    }
    return ts;
}

std::vector<Type*> Translator::TranslateASTTypes(const std::vector<AST::DataTy>& genericInfos)
{
    std::vector<Type*> ts;
    for (const auto& p : genericInfos) {
        ts.emplace_back(TranslateType(AST::ModalTy{p}));
    }
    return ts;
}

Ptr<AST::Expr> Translator::GetMapExpr(AST::Node& node) const
{
    if (auto expr = DynamicCast<AST::Expr*>(&node); expr) {
        while (expr != nullptr && expr->desugarExpr != nullptr) {
            expr = expr->desugarExpr.get();
        }
        if (expr != nullptr && expr->mapExpr != nullptr) {
            auto base = expr->mapExpr;
            return base;
        }
    }
    return nullptr;
}

// Init not called by 'this' or 'super'
static bool IsCallRegularInit(const AST::CallExpr& expr)
{
    if (expr.resolvedFunction && IsInstanceConstructor(*expr.resolvedFunction)) {
        bool callOtherInit = expr.baseFunc->astKind == AST::ASTKind::REF_EXPR &&
            (StaticCast<AST::RefExpr*>(expr.baseFunc.get())->isThis ||
                StaticCast<AST::RefExpr*>(expr.baseFunc.get())->isSuper);
        return !callOtherInit;
    }

    return false;
}

std::vector<Type*> Translator::GetFuncInstArgs(const AST::CallExpr& expr)
{
    CJC_ASSERT(expr.resolvedFunction != nullptr);
    std::vector<Type*> funcInstTypeArgs;
    if (auto nre = DynamicCast<AST::NameReferenceExpr*>(expr.baseFunc.get())) {
        // Skip the constructor since the instantiation type args there is for the parent custom type not for the
        // function call，e.g. let x = CA<Int64>()
        if (!expr.resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR)) {
            for (auto& instTy : nre->instTys) {
                funcInstTypeArgs.emplace_back(TranslateType(instTy));
            }
        }
    }
    return funcInstTypeArgs;
}

Ptr<Value> Translator::GetCurrentThisObject(const AST::FuncDecl& resolved)
{
    auto curFunc = GetCurrentFunc();
    CJC_NULLPTR_CHECK(curFunc);
    auto thisVar = curFunc->GetParam(0);
    bool isThisRef =
        curFunc->IsConstructor() || curFunc->TestAttr(Attribute::MUT) || curFunc->GetFuncKind() == FuncKind::SETTER;
    bool needThisRef =
        resolved.TestAttr(AST::Attribute::MUT) || resolved.TestAttr(AST::Attribute::CONSTRUCTOR) || resolved.isSetter;
    if (IsStructOrExtendMethod(*curFunc) && isThisRef && !needThisRef) {
        auto objType = thisVar->GetType();
        CJC_ASSERT(objType->IsRef() && !StaticCast<RefType*>(objType)->GetBaseType()->IsRef());
        auto objBaseType = StaticCast<RefType*>(objType)->GetBaseType();
        CJC_ASSERT(objBaseType->IsStruct());
        return CreateAndAppendExpression<Load>(objBaseType, thisVar, currentBlock)->GetResult();
    } else {
        return thisVar;
    }
}

Value* Translator::GetCurrentThisObjectByMemberAccess(const AST::MemberAccess& memAccess, const AST::FuncDecl& resolved,
    const DebugLocation& loc)
{
    // this or super call:this.f(), super.f()
    if (AST::IsThisOrSuper(*memAccess.baseExpr)) {
        // MemberAccess must not be constructor call.
        return GetCurrentThisObject(resolved);
    }
    // member access except this or super call
    auto curObj = TranslateExprArg(*memAccess.baseExpr);
    CJC_NULLPTR_CHECK(curObj);
    if (memAccess.baseExpr->GetTy()->IsClassLike()) {
        // class A {func foo(){return 0}
        // var a = A()
        // a.f()           // a is A&& need add `Load`
        // let b = A()
        // b.f()           // b is A& don't need add `Load`
        auto objType = curObj->GetType();
        if (objType->IsRef()) {
            // objType is A&& or A&
            auto objBaseType = StaticCast<RefType*>(objType)->GetBaseType();
            if (objBaseType->IsRef()) {
                // for example: objBaseType is A&
                auto derefedObj = CreateAndAppendExpression<Load>(
                    TranslateLocation(*memAccess.baseExpr), objBaseType, curObj, currentBlock)
                                      ->GetResult();
                derefedObj->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
                return derefedObj;
            }
        }
        return curObj;
    } else if (!memAccess.baseExpr->GetTy()->IsStruct() || !resolved.TestAttr(AST::Attribute::MUT)) {
        // Non-struct and non-classlike type must perform deref.
        // If `this` obj's type is struct, and resolved function is not `mut`, need add `Load`.
        // struct A {func foo(){return 0}
        // var a = A()
        // a.foo()           // a is A& need add `Load`
        // let b = A()
        // b.foo()           // b is A don't need add `Load`
        auto objType = curObj->GetType();
        if (objType->IsRef()) {
            // objType is A&
            auto objBaseType = StaticCast<RefType*>(objType)->GetBaseType();
            CJC_ASSERT(!objBaseType->IsRef());
            // for example: objBaseType is A
            curObj = CreateAndAppendExpression<Load>(loc, objBaseType, curObj, currentBlock)->GetResult();
        }
    }
    return curObj;
}

// this API should be moved to some other place
Translator::LeftValueInfo Translator::TranslateExprAsLeftValue(const AST::Expr& expr)
{
    auto base = &expr;
    auto backBlock = currentBlock;
    auto desugared = GetDesugaredExpr(expr);
    if (auto dexpr = DynamicCast<AST::Expr*>(desugared); dexpr && dexpr->mapExpr != nullptr) {
        base = dexpr->mapExpr;
    }
    if (auto res = exprValueTable.TryGet(*base)) {
        return LeftValueInfo(res, {});
    }

    if (desugared != &expr) {
        LeftValueInfo res = LeftValueInfo(nullptr, {});
        if (auto dexpr = DynamicCast<AST::Expr*>(desugared)) {
            res = TranslateExprAsLeftValue(*StaticCast<AST::Expr*>(dexpr));
        } else {
            res = LeftValueInfo(TranslateASTNode(*desugared, *this), {});
        }

        /* There are two cases need add `Goto` when translate AST::Block
            case1: if the Block node is a desugar node, then add `Goto`.
            example code：
                                                                                                 |     |   desugar block
            `print("${a.a.b}\n")` => desugar to  `print({var tmp1 = Stringbuilder(); tmp1.append({a.a.b})})`
                                                        |                                                | desugar block
            case2: unsafe block.
            example code:
            var a = unsafe{}
        */
        if (auto subBlock = DynamicCast<AST::Block*>(desugared)) {
            if (desugared != &expr || desugared->TestAttr(AST::Attribute::UNSAFE)) {
                CreateAndAppendTerminator<GoTo>(GetBlockByAST(*subBlock), backBlock);
            }
        }

        return res;
    }

    if (expr.astKind == AST::ASTKind::REF_EXPR) {
        return TranslateRefExprAsLeftValue(*StaticCast<AST::RefExpr*>(&expr));
    } else if (expr.astKind == AST::ASTKind::MEMBER_ACCESS) {
        return TranslateMemberAccessAsLeftValue(*StaticCast<AST::MemberAccess*>(&expr));
    } else if (expr.astKind == AST::ASTKind::PAREN_EXPR) {
        auto parenExpr = StaticCast<AST::ParenExpr*>(&expr);
        return TranslateExprAsLeftValue(*parenExpr->expr);
    } else if (expr.astKind == AST::ASTKind::CALL_EXPR) {
        return TranslateCallExprAsLeftValue(*StaticCast<AST::CallExpr*>(&expr));
    } else {
        return LeftValueInfo(TranslateASTNode(expr, *this), {});
    }
}

Value* Translator::GenerateLeftValue(const Translator::LeftValueInfo& leftValInfo, const DebugLocation& loc)
{
    Value* result = leftValInfo.base;
    if (!leftValInfo.path.empty()) {
        auto baseCustomType = StaticCast<CustomType*>(result->GetType()->StripAllRefs());
        auto memberType = GetInstMemberTypeByName(*baseCustomType, leftValInfo.path, builder);
        if (result->GetType()->IsRef()) {
            auto memberRefType = builder.GetType<RefType>(memberType);
            auto getMemberRef =
                CreateAndAppendExpression<GetElementByName>(loc, memberRefType, result, leftValInfo.path, currentBlock);
            result = getMemberRef->GetResult();
        } else {
            auto getMember =
                CreateAndAppendExpression<FieldByName>(loc, memberType, result, leftValInfo.path, currentBlock);
            result = getMember->GetResult();
        }
    }
    return result;
}

Value* Translator::TranslateThisObjectForNonStaticMemberFuncCall(const AST::CallExpr& expr, bool needsMutableThis)
{
    Ptr<AST::FuncDecl> resolved = expr.resolvedFunction;
    CJC_ASSERT(resolved && IsInstanceMember(*resolved));
    CJC_NULLPTR_CHECK(resolved->outerDecl);
    // polish here
    // When current is calling a constructor and is not called with 'this' or 'super',
    // it should not using 'this' existed in context.
    CJC_ASSERT(!IsCallRegularInit(expr));

    Value* thisObj = nullptr;
    auto loc = TranslateLocation(expr);
    if (auto memAccess = DynamicCast<AST::MemberAccess*>(expr.baseFunc.get())) {
        if (AST::IsThisOrSuper(*memAccess->baseExpr)) {
            // Case A: the member access is in form like "this.f()" or "super.f()", then we just get the "this" param
            // from current func
            thisObj = GetCurrentThisObject(*resolved);
        } else {
            // Case B: otherwise, we will generate the base part of the member access and get the "this"
            auto thisObjValueInfo = TranslateExprAsLeftValue(*memAccess->baseExpr);
            thisObj = thisObjValueInfo.base;
            // polish this
            if (!thisObjValueInfo.path.empty()) {
                auto lhsCustomType = StaticCast<CustomType*>(thisObj->GetType()->StripAllRefs());
                if (thisObj->GetType()->IsRef()) {
                    thisObj =
                        CreateGetElementRefWithPath(loc, thisObj, thisObjValueInfo.path, currentBlock, *lhsCustomType);
                } else {
                    auto memberType = GetInstMemberTypeByName(*lhsCustomType, thisObjValueInfo.path, builder);
                    auto getMember = CreateAndAppendExpression<FieldByName>(
                        loc, memberType, thisObj, thisObjValueInfo.path, currentBlock);
                    thisObj = getMember->GetResult();
                }
            }
        }
        // this case only happends when extending Unit or Nothing type
        if (thisObj == nullptr) {
            if (memAccess->baseExpr->GetTy()->IsUnit()) {
                thisObj = CreateAndAppendConstantExpression<UnitLiteral>(builder.GetUnitTy(), *currentBlock)
                    ->GetResult();
            } else if (memAccess->baseExpr->GetTy()->IsNothing()) {
                thisObj = CreateAndAppendConstantExpression<NullLiteral>(builder.GetNothingType(), *currentBlock)
                    ->GetResult();
            } else {
                CJC_ABORT();
            }
        }
    } else {
        thisObj = GetCurrentThisObject(*resolved);
    }
    auto thisObjTy = thisObj->GetType();
    CJC_ASSERT(thisObjTy->IsReferenceTypeWithRefDims(CLASS_REF_DIM) || thisObjTy->IsReferenceTypeWithRefDims(1) ||
        thisObjTy->IsValueOrGenericTypeWithRefDims(1) || thisObjTy->IsValueOrGenericTypeWithRefDims(0));
    if (!needsMutableThis) {
        if (thisObjTy->IsReferenceTypeWithRefDims(CLASS_REF_DIM) || thisObjTy->IsValueOrGenericTypeWithRefDims(1)) {
            auto pureThisObjTy = thisObjTy->StripAllRefs();
            auto targetTy = pureThisObjTy;
            if (pureThisObjTy->IsReferenceType()) {
                targetTy = builder.GetType<RefType>(pureThisObjTy);
            }
            thisObj = CreateAndAppendExpression<Load>(loc, targetTy, thisObj, currentBlock)->GetResult();
        }
    } else {
        CJC_ASSERT(!thisObjTy->IsValueOrGenericTypeWithRefDims(0));
        if (thisObjTy->IsReferenceTypeWithRefDims(CLASS_REF_DIM)) {
            auto pureThisObjTy = thisObjTy->StripAllRefs();
            auto targetTy = builder.GetType<RefType>(pureThisObjTy);
            thisObj = CreateAndAppendExpression<Load>(loc, targetTy, thisObj, currentBlock)->GetResult();
        }
    }

    CJC_NULLPTR_CHECK(thisObj);
    return thisObj;
}

void Translator::TranslateFuncArgsWithoutThisObj(
    const AST::CallExpr& expr, std::vector<Value*>& args, const std::vector<Type*>* expectedParamTys)
{
    auto& argExprs = expr.desugarArgs.value();
    if (expectedParamTys != nullptr) {
        // maybe callee has variable length argument, so we need to check the index here.
        CJC_ASSERT(expectedParamTys->size() <= argExprs.size());
    }
    const auto& loc = TranslateLocation(expr);
    for (size_t i = 0; i < argExprs.size(); i++) {
        if (argExprs[i]->TestAttr(AST::Attribute::HAS_INITIAL)) {
            Ptr<AST::FuncDecl> resolved = expr.resolvedFunction;
            CJC_ASSERT(resolved->funcBody && !resolved->funcBody->paramLists.empty());
            CJC_ASSERT(resolved->funcBody->paramLists[0]->params.size() == expr.desugarArgs.value().size() ||
                resolved->hasVariableLenArg);
            auto& params = resolved->funcBody->paramLists[0]->params;
            // In this case, the corresponding func param has default value which has been desugared into
            // a default-value-func, thus the func arg expr here will becomes a call to the default-value-func
            // which use all the previous args as input. For example:
            //      Original Code:
            //          func foo(x: Int64, y!: Int64 = x + 1) {...}
            //          let res = foo(1)
            //
            //      Desugared Result:
            //          func foo(x: Int64, y: Int64) {...}
            //          func foo_y_defalut_value(x: Int64) { x + 1 }
            //          let res = foo(1, foo_y_defalut_value(1))

            // 1) get the default-value-func
            CJC_NULLPTR_CHECK(params[i]->desugarDecl.get());
            auto defaultValueFunc = GetSymbolTable(*params[i]->desugarDecl);

            // 2) collect the previous args as the input of the call to default-value-func
            std::vector<Value*> defaultValueFuncArgs;
            for (size_t j = 0; j < args.size(); ++j) {
                defaultValueFuncArgs.emplace_back(args[j]);
            }

            // 3) calculte the instantiated type of the default-value-func
            auto instDefaultValueFuncRetTy = TranslateType(argExprs[i]->GetTy());
            auto thisInstType = GetMemberFuncCallerInstType(expr);
            /**
             *  class A {
             *      func foo<T>(x!: Int64 = 1) {}
             *  }
             * if `foo` is instantiated by `Bool`, then new instantiated func `foo_Bool` is global func,
             * not a member func, so instantiated func `x.0` is also a global func.
             */
            if (auto funcBase = DynamicCast<Function*>(defaultValueFunc); funcBase &&
                funcBase->GetParentCustomTypeDef() == nullptr) {
                thisInstType = nullptr;
            }

            std::vector<Type*> instArgs;
            // e.g. class A<T> { init(a!: Int64 = 1) }; var x = A<Int32>()
            // `A<Int32>()` is callExpr for class constructor, it use `instTys` to store Int32, but desugar func is
            // `a.0(): Int64 {...}` without generic param. So this apply should be `A<Int32>(a.0())`.
            if (expr.resolvedFunction == nullptr || !IsClassOrEnumConstructor(*expr.resolvedFunction)) {
                auto instParamsInOwnerFunc = StaticCast<AST::NameReferenceExpr*>(expr.baseFunc.get())->instTys;
                for (auto ty : instParamsInOwnerFunc) {
                    instArgs.emplace_back(TranslateType(ty));
                }
            }

            // check the this type and instParentCustomType value here
            auto funcCallContext = FuncCallContext {
                .args = defaultValueFuncArgs,
                .instTypeArgs = instArgs,
                .thisType = thisInstType
            };
            auto defaultValueCall = CreateAndAppendApplyCallFromCallExpr(
                *defaultValueFunc, funcCallContext, *instDefaultValueFuncRetTy, expr);
            Value* defaultValueCallResult = defaultValueCall->GetResult();
            if (expectedParamTys != nullptr) {
                // variable length argument can be only in c func, and c func's param can't have default value.
                CJC_ASSERT(expectedParamTys->size() > i);
                defaultValueCallResult = TypeCastOrBoxIfNeeded(*defaultValueCallResult, *(*expectedParamTys)[i], loc);
            }
            args.emplace_back(defaultValueCallResult);
        } else {
            Type* expectedTy = nullptr;
            // maybe callee has variable length argument, so we need to check the index here.
            if (expectedParamTys != nullptr && expectedParamTys->size() > i) {
                expectedTy = (*expectedParamTys)[i];
            }
            args.emplace_back(TranslateTrivialArgWithNoSugar(*argExprs[i], loc, expectedTy));
        }
    }
}

Value* Translator::TranslateTrivialArgWithNoSugar(const AST::FuncArg& arg, const DebugLocation& loc, Type* expectedTy)
{
    Value* argVal = nullptr;
    if (arg.withInout) {
        auto argLeftValInfo = TranslateExprAsLeftValue(*arg.expr);
        argVal = GenerateLeftValue(argLeftValInfo, loc);
        auto ty = TranslateType(arg.GetTy());
        auto callContext = IntrisicCallContext {
            .kind = IntrinsicKind::INOUT_PARAM,
            .args = std::vector<Value*>{argVal}
        };
        argVal = CreateAndAppendExpression<Intrinsic>(loc, ty, callContext, currentBlock)->GetResult();
    } else {
        argVal = TranslateExprArg(*arg.expr);
    }
    if (expectedTy != nullptr) {
        argVal = TypeCastOrBoxIfNeeded(*argVal, *expectedTy, loc);
    }
    CJC_NULLPTR_CHECK(argVal);
    return argVal;
}

std::vector<Value*> Translator::TranslateFuncArgs(
    const AST::CallExpr& expr, Type* expectedThisObjTy, const std::vector<Type*>* expectedParamTys)
{
    std::vector<Value*> args;
    auto callee = expr.resolvedFunction;

    // If the call is to a non-static member function, we need to generate the `this` argument first.
    // because if func param has default value, it will be desugared into a default-value-func,
    // and the default-value-func will use the previous args as input, `this` is one of them.
    if (IsNonConstructorMemberFunc(callee) && !callee->TestAttr(AST::Attribute::STATIC)) {
        auto thisObj =
            TranslateThisObjectForNonStaticMemberFuncCall(expr, callee->TestAttr(AST::Attribute::MUT));
        if (expectedThisObjTy != nullptr) {
            thisObj = TypeCastOrBoxIfNeeded(*thisObj, *expectedThisObjTy);
        }
        args.emplace_back(thisObj);
    }

    const auto& loc = TranslateLocation(expr);
    if (expr.desugarArgs.has_value()) {
        TranslateFuncArgsWithoutThisObj(expr, args, expectedParamTys);
    } else {
        if (expectedParamTys != nullptr) {
            // maybe callee has variable length argument, so we need to check the index here.
            CJC_ASSERT(expectedParamTys->size() <= expr.args.size());
        }
        for (size_t i = 0; i < expr.args.size(); i++) {
            auto& arg = expr.args[i];
            Type* expectedTy = nullptr;
            // maybe callee has variable length argument, so we need to check the index here.
            if (expectedParamTys != nullptr && expectedParamTys->size() > i) {
                expectedTy = (*expectedParamTys)[i];
            }
            args.emplace_back(TranslateTrivialArgWithNoSugar(*arg, loc, expectedTy));
        }
    }

    // class or struct constructor, but not static constructor
    auto isNonEnumNonStaticConstructor =
        callee && callee->IsMemberDecl() && !callee->TestAttr(AST::Attribute::STATIC) &&
        callee->TestAttr(AST::Attribute::CONSTRUCTOR) && !callee->TestAttr(AST::Attribute::ENUM_CONSTRUCTOR);
    // call constructor of class or struct, we need to generate the `this` argument at last.
    // because it can't be used in default-value-func.
    if (isNonEnumNonStaticConstructor) {
        Value* thisObj = nullptr;
        if (IsSuperOrThisCall(expr)) {
            // For super constructor call site, the `this` arg of current constructor should be passed into super
            // constructor
            thisObj = GetCurrentFunc()->GetParam(0);
        } else {
            // Allocate with constructor `this` type when provided (matches init's this-modal).
            // Call-site result modal (e.g. `F@local!`) is applied after Load by the caller.
            Type* allocElemTy = expectedThisObjTy != nullptr
                ? expectedThisObjTy->StripAllRefs()
                : chirTy.TranslateType(expr.GetTy())->StripAllRefs();
            auto allocateThis =
                TryCreate<Allocate>(currentBlock, loc, builder.GetType<RefType>(allocElemTy), allocElemTy);
            allocateThis->Set<DebugLocationInfoForWarning>(loc);
            thisObj = allocateThis->GetResult();
        }
        if (expectedThisObjTy != nullptr) {
            thisObj = TypeCastOrBoxIfNeeded(*thisObj, *expectedThisObjTy);
        }
        args.insert(args.begin(), thisObj);
    }
    return args;
}

void Translator::BlackBoxModifyArgTypeToRef(std::vector<Value*>& args)
{
    // This function change blackBox args to reference,
    // because this intrinsic need control reference of variables.
    for (auto& arg : args) {
        auto type = arg->GetType();
        if (type->IsRef()) {
            continue;
        }
        Ptr<Value> newArg = arg;
        if (arg->IsLocalVar()) {
            auto localVar = StaticCast<LocalVar*>(arg);
            auto expr = localVar->GetExpr();
            if (auto load = DynamicCast<Load*>(expr)) {
                newArg = load->GetLocation();
                if (GetNonDebugUsers(*expr->GetResult()).empty()) {
                    if (expr->GetResult()->GetDebugExpr()) {
                        // let eee = SA() // this sentence will generate Debug(%1, eee), and %1's type is
                        //   'Struct-SA', this Debug expr is used for warning location info. This Debug will be removed
                        //   after a better way introducing to store waring location, temporarily remove it here.
                        // blackBox(eee)
                        expr->GetResult()->GetDebugExpr()->RemoveSelfFromBlock();
                    }
                    expr->RemoveSelfFromBlock();
                }
            } else {
                auto loc = arg->GetDebugLocation();
                auto argRefType = builder.GetType<RefType>(type);
                newArg = TryCreate<Allocate>(currentBlock, loc, argRefType, type)->GetResult();
                CreateAndAppendWrappedStore(*arg, *newArg, loc);
            }
        }
        arg = newArg;
    }
}

Ptr<Value> Translator::TranslateRawArrayAllocate(const AST::CallExpr& expr)
{
    CJC_ASSERT(expr.args.size() == 1);
    const auto& loc = TranslateLocation(expr);
    auto arrayTy = chirTy.TranslateType(expr.GetTy());
    CJC_ASSERT(arrayTy->IsRef());
    auto eleTy = StaticCast<RawArrayType>(StaticCast<RefType>(arrayTy)->GetBaseType())->GetElementType();
    auto sizeVal = TranslateExprArg(*expr.args[0]);
    return TryCreate<RawArrayAllocate>(currentBlock, loc, arrayTy, eleTy, sizeVal)->GetResult();
}

Ptr<Value> Translator::TranslateRawArrayInitByValue(const AST::CallExpr& expr)
{
    constexpr size_t INIT_BY_VALUE_ARGS = 3;
    CJC_ASSERT(expr.args.size() == INIT_BY_VALUE_ARGS);
    const auto& loc = TranslateLocation(expr);
    auto funcType = StaticCast<FuncType*>(chirTy.TranslateType(expr.baseFunc->GetTy()));
    auto argTypes = funcType->GetParamTypes();
    CJC_ASSERT(argTypes.size() == INIT_BY_VALUE_ARGS);
    std::vector<Value*> args;
    for (size_t i = 0; i < INIT_BY_VALUE_ARGS; ++i) {
        args.emplace_back(TypeCastOrBoxIfNeeded(*TranslateExprArg(*expr.args[i]), *argTypes[i], loc));
    }
    CreateAndAppendExpression<RawArrayInitByValue>(
        loc, builder.GetUnitTy(), args[0], args[1], args[INIT_BY_VALUE_ARGS - 1], currentBlock);
    return CreateAndAppendConstantExpression<UnitLiteral>(builder.GetUnitTy(), *currentBlock)->GetResult();
}

Ptr<Value> Translator::TranslateIntrinsicCall(const AST::CallExpr& expr)
{
    // Conditions to check if this is a call to intrinsic
    if (expr.callKind != AST::CallKind::CALL_INTRINSIC_FUNCTION) {
        return nullptr;
    }

    auto target = expr.baseFunc->GetTarget();
    CJC_NULLPTR_CHECK(target);
    const std::string& identifier = target->identifier.Val();

    // Translate code position info
    const auto& loc = TranslateLocation(expr);

    auto ty = chirTy.TranslateType(expr.GetTy());

    // Get the intrinsic kind
    std::string packageName{};
    if (target->genericDecl) {
        packageName = target->genericDecl->fullPackageName;
    } else if (target->outerDecl && target->outerDecl->genericDecl) {
        packageName = target->outerDecl->genericDecl->fullPackageName;
    } else {
        packageName = target->fullPackageName;
    }
    if (packageName == CORE_PACKAGE_NAME) {
        if (identifier == "rawArrayAllocate" || identifier == "rawArrayAllocate2" ||
            identifier == "rawArrayAllocate3") {
            return TranslateRawArrayAllocate(expr);
        }
        if (identifier == "rawArrayInitByValue" || identifier == "rawArrayInitByValue2" ||
            identifier == "rawArrayInitByValue3") {
            return TranslateRawArrayInitByValue(expr);
        }
    }

    CHIR::IntrinsicKind intrinsicKind{NOT_INTRINSIC};
    // Should handle headlessIntrinsics first, because it can appear in any package
    if (auto it1 = HEADLESS_INTRINSICS.find(identifier); it1 != HEADLESS_INTRINSICS.end()) {
        intrinsicKind = it1->second;
    } else if (auto it = PACKAGE_MAP.find(packageName); it != PACKAGE_MAP.end()) {
        CJC_ASSERT(it->second.find(identifier) != it->second.end());
        intrinsicKind = it->second.at(identifier);
    }

    // Translate arguments
    auto args = TranslateFuncArgs(expr, nullptr, nullptr);
    auto retTy = ty;
    if (intrinsicKind == BLACK_BOX) {
        // intrinsic blackBox's signature is blackBox<T>(v: T): T,
        // and args need to be converted into reference types to control variable lifetimes.
        BlackBoxModifyArgTypeToRef(args);
        retTy = args[0]->GetType();
    }
    auto ne = StaticCast<AST::NameReferenceExpr*>(expr.baseFunc.get());
    auto callContext = IntrisicCallContext {
        .kind = intrinsicKind,
        .args = args,
        .instTypeArgs = TranslateASTTypes(ne->instTys)
    };
    auto intriVar = TryCreate<Intrinsic>(currentBlock, loc, retTy, callContext)->GetResult();

    // what is this for
    if (expr.GetTy()->IsUnit()) {
        // Codegen will not generate valid 'unit' value for intrinsic call.
        return CreateAndAppendConstantExpression<UnitLiteral>(builder.GetUnitTy(), *currentBlock)->GetResult();
    }

    if (retTy != ty && intrinsicKind == BLACK_BOX) {
        return CreateAndAppendExpression<Load>(ty, intriVar, currentBlock)->GetResult();
    }
    return intriVar;
}

Ptr<Value> Translator::TranslateForeignFuncCall(const AST::CallExpr& expr)
{
    // Conditions to check if this is a call to foreign func
    if (expr.resolvedFunction == nullptr) {
        return nullptr;
    }
    if (!expr.resolvedFunction->TestAttr(AST::Attribute::FOREIGN)) {
        return nullptr;
    }
    auto [paramInstTys, retInstTy] = GetMemberFuncParamAndRetInstTypes(expr);
    auto args = TranslateFuncArgs(expr, nullptr, &paramInstTys);
    auto resolvedFunction = expr.resolvedFunction;
    auto callee = GetSymbolTable(*resolvedFunction);
    CJC_ASSERT(callee != nullptr && "TranslateApply: not supported callee now!");
    auto funcCallContext = FuncCallContext {
        .args = args
    };
    auto funcCall = CreateAndAppendApplyCallFromCallExpr(*callee, funcCallContext, *retInstTy, expr);
    return funcCall->GetResult();
}

Value* Translator::TranslateCStringCtorCall(const AST::CallExpr& expr)
{
    auto isCStringCtor = [&expr]() -> bool {
        if (expr.resolvedFunction && expr.resolvedFunction->outerDecl &&
            expr.resolvedFunction->outerDecl->IsBuiltIn()) {
            auto bid = StaticCast<AST::BuiltInDecl*>(expr.resolvedFunction->outerDecl);
            return bid->type == AST::BuiltInType::CSTRING;
        }
        if (auto target = DynamicCast<AST::BuiltInDecl>(expr.baseFunc->GetTarget());
            target && target->type == AST::BuiltInType::CSTRING) {
            return true;
        }
        return false;
    };
    if (isCStringCtor()) {
        auto ty = TranslateType(expr.GetTy());
        const auto& loc = TranslateLocation(expr);
        CJC_ASSERT(expr.args.size() == 1);
        auto argVal = TranslateExprArg(*expr.args[0]);
        auto callContext = IntrisicCallContext {
            .kind = IntrinsicKind::CSTRING_INIT,
            .args = std::vector<Value*>{argVal}
        };
        return CreateAndAppendExpression<Intrinsic>(loc, ty, callContext, currentBlock)->GetResult();
    }
    return nullptr;
}

Ptr<Value> Translator::TranslateEnumCtorCall(const AST::CallExpr& expr)
{
    // Conditions to check if this is a call to construct an enum value
    if (expr.resolvedFunction == nullptr) {
        return nullptr;
    }
    if (!expr.resolvedFunction->TestAttr(AST::Attribute::ENUM_CONSTRUCTOR)) {
        return nullptr;
    }

    auto resolvedFunction = expr.resolvedFunction;

    // Translate code position info
    const auto& loc = TranslateLocation(expr);

    // Get the enum case ID
    auto enumDecl = resolvedFunction->funcBody->parentEnum;
    CJC_NULLPTR_CHECK(enumDecl);
    auto& constrs = enumDecl->constructors;
    auto fieldIt = std::find_if(constrs.begin(), constrs.end(),
        [&resolvedFunction](auto const& decl) -> bool { return resolvedFunction == decl.get(); });
    CJC_ASSERT(fieldIt != constrs.end());
    auto enumId = static_cast<uint64_t>(std::distance(constrs.begin(), fieldIt));

    // Calculate instantiated callee func type
    // polish this API
    auto [paramInstTys, retInstTy] = GetMemberFuncParamAndRetInstTypes(expr);

    // Translate arguments
    auto args = TranslateFuncArgs(expr, nullptr, &paramInstTys);
    auto ty = chirTy.TranslateType(expr.GetTy());
    auto selectorTy = GetSelectorType(StaticCast<AST::EnumTy>(*expr.GetTy()));
    CJC_ASSERT(ty->StripAllRefs()->IsEnum());
    auto constExpr = (selectorTy->IsBoolean()
            ? CreateAndAppendConstantExpression<BoolLiteral>(
                  loc, selectorTy, *currentBlock, static_cast<bool>(enumId))
            : CreateAndAppendConstantExpression<IntLiteral>(loc, selectorTy, *currentBlock, enumId));
    args.insert(args.begin(), constExpr->GetResult());

    return CreateAndAppendExpression<Tuple>(TranslateLocation(expr), ty, args, currentBlock)->GetResult();
}

// merge this function with the right value version
Translator::LeftValueInfo Translator::TranslateStructOrClassCtorCallAsLeftValue(const AST::CallExpr& expr)
{
    // Conditions to check if this is a call to member func (constructor is not counted here)
    if (expr.resolvedFunction == nullptr) {
        return LeftValueInfo(nullptr, {});
    }
    if (!expr.resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR)) {
        return LeftValueInfo(nullptr, {});
    }
    if (expr.resolvedFunction->outerDecl == nullptr) {
        return LeftValueInfo(nullptr, {});
    }
    if (!(expr.resolvedFunction->outerDecl->astKind == AST::ASTKind::CLASS_DECL ||
            expr.resolvedFunction->outerDecl->astKind == AST::ASTKind::STRUCT_DECL)) {
        return LeftValueInfo(nullptr, {});
    }
    // Specially, static init is not handled here
    if (expr.resolvedFunction->TestAttr(AST::Attribute::STATIC)) {
        return LeftValueInfo(nullptr, {});
    }

    // `this` must match constructor this-modal; call result modal is separate.
    auto resultTy = chirTy.TranslateType(expr.GetTy())->StripAllRefs();
    auto thisTyRef = GetThisTypeWithModal(*resultTy, *expr.resolvedFunction);
    auto [paramInstTys, retInstTy] = GetMemberFuncParamAndRetInstTypes(expr);

    // Translate arguments
    auto args = TranslateFuncArgs(expr, thisTyRef, &paramInstTys);
    auto callee = GetSymbolTable(*expr.resolvedFunction);
    CJC_ASSERT(callee != nullptr && "TranslateApply: not supported callee now!");
    auto funcCallContext = FuncCallContext {
        .args = args,
        .thisType = thisTyRef
    };
    CreateAndAppendApplyCallFromCallExpr(*callee, funcCallContext, *retInstTy, expr);

    return LeftValueInfo(args[0], {});
}

Expression* Translator::CreateAndAppendApplyCallFromCallExpr(
    Value& callee, FuncCallContext& context, Type& instRetType, const AST::CallExpr& expr)
{
    auto funcCall = TryCreate<Apply>(currentBlock, &instRetType, &callee, context);
    const auto& loc = TranslateLocation(expr);
    funcCall->SetDebugLocation(loc);
    if (expr.callKind == AST::CallKind::CALL_SUPER_FUNCTION) {
        if (auto apply = DynamicCast<Apply>(funcCall)) {
            apply->SetSuperCall();
        }
    }
    if (HasNothingTypeArg(context.args)) {
        if (expr.baseFunc != nullptr) {
            const auto& warningLoc = TranslateLocation(*expr.baseFunc);
            funcCall->Set<DebugLocationInfoForWarning>(warningLoc);
        } else {
            funcCall->Set<DebugLocationInfoForWarning>(loc);
        }
    }
    return funcCall;
}

// Conditions to check if this is a call to member func (constructor is not counted here)
static bool IsCtorCall(const AST::CallExpr& expr)
{
    return expr.resolvedFunction && expr.resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR) &&
        expr.resolvedFunction->outerDecl && (expr.resolvedFunction->outerDecl->astKind == AST::ASTKind::CLASS_DECL ||
        expr.resolvedFunction->outerDecl->astKind == AST::ASTKind::STRUCT_DECL) &&
        // Specially, static init is not handled here
        !expr.resolvedFunction->TestAttr(AST::Attribute::STATIC);
}

Value* Translator::TranslateStructOrClassCtorCall(const AST::CallExpr& expr)
{
    // Translate code position info
    const auto& loc = TranslateLocation(expr);

    // `this` for init must follow constructor this-modal (e.g. plain `init()` vs `init(this @local!)`),
    // not the call-site result modal (`let a: F@local! = F()`).
    auto resultTy = chirTy.TranslateType(expr.GetTy())->StripAllRefs();
    auto thisTyRef = GetThisTypeWithModal(*resultTy, *expr.resolvedFunction);
    auto thisTy = thisTyRef->StripAllRefs();
    auto [paramInstTys, retInstTy] = GetMemberFuncParamAndRetInstTypes(expr);

    // Translate arguments
    auto args = TranslateFuncArgs(expr, thisTyRef, &paramInstTys);
    auto callee = GetSymbolTable(*expr.resolvedFunction);
    CJC_ASSERT(callee != nullptr && "TranslateApply: not supported callee now!");
    auto funcCallContext = FuncCallContext {
        .args = args,
        .thisType = thisTyRef
    };
    CreateAndAppendApplyCallFromCallExpr(*callee, funcCallContext, *retInstTy, expr);

    if (expr.resolvedFunction->outerDecl->astKind == AST::ASTKind::STRUCT_DECL) {
        if (IsSuperOrThisCall(expr)) {
            return nullptr;
            // should be: return nullptr;
        }
        auto load = CreateAndAppendExpression<Load>(loc, thisTy, args[0], currentBlock);
        // this load should be removed if it is a super/this call, but it will trigger IRChecker error in:
        if (IsSuperOrThisCall(expr)) {
            load->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
            // should be: return nullptr;
        }
        // Cast to call result type when modal differs (Allocate/init used constructor this-modal).
        return TypeCastOrBoxIfNeeded(*load->GetResult(), *resultTy, loc);
    }
    return TypeCastOrBoxIfNeeded(*args[0], *builder.GetType<RefType>(resultTy), loc);
}

Ptr<Value> Translator::TranslateCFuncCtorCall(const AST::CallExpr& expr)
{
    if (!IsValidCFuncConstructorCall(expr)) {
        return nullptr;
    }
    auto& arg = expr.args[0]->expr;
    auto argValue = TranslateExprArg(*arg, *TranslateType(expr.GetTy()), true);
    return argValue;
}

Ptr<Value> Translator::TranslateFuncTypeValueCall(const AST::CallExpr& expr)
{
    // Conditions to check if this is a call to func type value
    if (expr.resolvedFunction != nullptr) {
        return nullptr;
    }

    // Translate code position info
    const auto& loc = TranslateLocation(expr);

    // translate callee before args translate
    //   eg: foo()(a), translate foo() first, then translate args a
    auto callee = TranslateExprArg(*expr.baseFunc);
    CJC_ASSERT(callee != nullptr && "TranslateApply: not supported callee now!");
    callee = GenerateLoadIfNeccessary(*callee, false, false, false, loc);
    // Translate arguments
    auto paramInstTys = StaticCast<FuncType*>(callee->GetType())->GetParamTypes();
    auto args = TranslateFuncArgs(expr, nullptr, &paramInstTys);
    auto funcCallContext = FuncCallContext {
        .args = args
    };
    auto instRetType = StaticCast<FuncType*>(callee->GetType())->GetReturnType();
    auto funcCall = CreateAndAppendApplyCallFromCallExpr(
        *callee, funcCallContext, *instRetType, expr);
    return funcCall->GetResult();
}

namespace Cangjie::CHIR {
/*
public common interface I { common func foo6(): Unit { println("I::foo6 common") } }

public common interface I2 <: I {}

public struct A <: I2 {}

public func runInCommon() {
    let a = A()
    // NOTE: foo6 call MUST NOT be devirtualized
    // `Invoke` should be generated instead of `Apply`
    // Because of implementation can be moved to more close parent(I2)
    a.foo6()
}

// NOTE: More precise check: there is common class parent that is child of those providing current implementation.
*/
inline bool CanActualFuncBeMovedInSpecific(const Type& thisType, CHIRBuilder& builder)
{
    if (!thisType.IsStruct() && !thisType.IsEnum()) {
        return false;
    }

    auto& customType = StaticCast<const CustomType&>(thisType);
    auto inheritanceList = customType.GetCustomTypeDef()->GetSuperTypesRecusively(builder);
    for (auto parentType: inheritanceList) {
        auto parent = parentType->GetCustomTypeDef();
        if (parent->TestAttr(Attribute::COMMON) || parent->TestAttr(Attribute::SPECIFIC)) {
            return true;
        }
    }

    return false;
}
}

bool Translator::IsOverflowOpCall(const AST::FuncDecl& func)
{
    if (!Is<AST::InterfaceDecl>(func.outerDecl)) {
        return false;
    }
    return IsOverflowOperator(func.identifier, *StaticCast<FuncType>(TranslateType(func.DataTy())));
}

Value* Translator::CreateGetRTTIWrapper(Value* value, Block* bl, const DebugLocation& loc)
{
    auto type = value->GetType();
    Expression* expr;
    // GetRTTI can only be used on Class& or This&. use GetRTTIStatic otherwise
    if (Is<RefType>(type) && (type->StripAllRefs()->IsClass() || type->StripAllRefs()->IsThis())) {
        expr = builder.CreateExpression<GetRTTI>(loc, builder.GetUnitTy(), value, bl);
    } else {
        expr = builder.CreateExpression<GetRTTIStatic>(loc, builder.GetUnitTy(), type, bl);
    }
    bl->AppendExpression(expr);
    return expr->GetResult();
}

Value* Translator::TranslateMemberFuncCall(const AST::CallExpr& expr)
{
    // Static member function, instance member function, global func.
    auto resolvedFunction = expr.resolvedFunction;
    CJC_NULLPTR_CHECK(resolvedFunction);
    CJC_ASSERT(!resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR));

    // Translate code position info
    const auto& loc = TranslateLocation(expr);
    InstCalleeInfo instCallInfo;
    if (auto ma = DynamicCast<AST::MemberAccess*>(expr.baseFunc.get())) {
        instCallInfo = GetInstCalleeInfoFromMemberAccess(*ma);
    } else {
        auto funcRef = StaticCast<AST::RefExpr*>(expr.baseFunc.get());
        instCallInfo = GetInstCalleeInfoFromRefExpr(*funcRef);
    }

    // Translate arguments
    Type* expectedThisObjTy = nullptr;
    auto expectedParamTys = instCallInfo.instParamTys;
    if (!resolvedFunction->TestAttr(AST::Attribute::STATIC)) {
        // for virtual func call, cast implicit `this` to instantiated parent type
        if (instCallInfo.isVirtualFuncCall) {
            expectedThisObjTy =
                GetThisTypeWithModal(*instCallInfo.instParentCustomTy, *instCallInfo.originalFuncDecl);
        } else {
            expectedThisObjTy = expectedParamTys[0];
        }
        expectedParamTys.erase(expectedParamTys.begin());
    }
    auto args = TranslateFuncArgs(expr, expectedThisObjTy, &expectedParamTys);
    LocalVar* ret = nullptr;
    if (instCallInfo.isVirtualFuncCall) {
        if (resolvedFunction->TestAttr(AST::Attribute::STATIC)) {
            // InvokeStatic
            Value* rtti = nullptr;
            auto topLevelFunc = currentBlock->GetTopLevelFunc();
            /**
            *  open class A {
            *      static func foo() { return 1 }
            *      func goo() { foo() } // we need to use `GetRTTI`, not `GetRTTIStatic`
            *  }
            *  class B <: A {
            *      static func foo() { return 2 }
            *  }
            *  var a: A = B()
            *  a.goo()  // the return value is 2, if `goo` is inlined here, the CHIR must be like:
            *           // InvokeStatic(GetRTTI(a))
            */
            if (expr.baseFunc->astKind == AST::ASTKind::REF_EXPR && !topLevelFunc->TestAttr(Attribute::STATIC)) {
                auto thisObj = topLevelFunc->GetParam(0);
                rtti = CreateAndAppendExpression<GetRTTI>(builder.GetUnitTy(), thisObj, currentBlock)->GetResult();
            } else {
                rtti = CreateAndAppendExpression<GetRTTIStatic>(
                    builder.GetUnitTy(), instCallInfo.thisType->StripAllRefs(), currentBlock)->GetResult();
            }
            auto invokeInfo = GenerateInvokeCallContext(instCallInfo, *rtti, args, expr.overflowStrategy);
            ret = TryCreate<InvokeStatic>(currentBlock, loc, instCallInfo.instRetTy, invokeInfo)->GetResult();
        } else {
            // Invoke
            CJC_ASSERT(!args.empty());
            auto obj = args[0];
            args.erase(args.begin());
            auto invokeInfo = GenerateInvokeCallContext(instCallInfo, *obj, args, expr.overflowStrategy);
            ret = TryCreate<Invoke>(currentBlock, loc, instCallInfo.instRetTy, invokeInfo)->GetResult();
        }
        if (HasNothingTypeArg(args)) {
            const auto& warningLoc = TranslateLocation(*expr.baseFunc);
            ret->GetExpr()->Set<DebugLocationInfoForWarning>(warningLoc);
        }
    } else {
        auto callee = GetSymbolTable(*resolvedFunction);
        auto funcCallContext = FuncCallContext {
            .args = args,
            .instTypeArgs = instCallInfo.instantiatedTypeArgs,
            .thisType = instCallInfo.thisType
        };
        ret = CreateAndAppendApplyCallFromCallExpr(
            *callee, funcCallContext, *instCallInfo.instRetTy, expr)->GetResult();
    }
    return ret;
}

Value* Translator::TranslateTrivialFuncCall(const AST::CallExpr& expr)
{
    // Conditions to check if this is a call to declared function which
    // can be a global func or local func
    if (expr.resolvedFunction == nullptr) {
        return nullptr;
    }

    auto resolvedFunction = expr.resolvedFunction;
    auto funcInstTypeArgs = GetFuncInstArgs(expr);
    // polish this API
    auto [paramInstTys, retInstTy] = GetMemberFuncParamAndRetInstTypes(expr);

    // Translate arguments
    auto args = TranslateFuncArgs(expr, nullptr, &paramInstTys);
    auto callee = GetSymbolTable(*resolvedFunction);
    CJC_ASSERT(callee != nullptr && "TranslateApply: not supported callee now!");
    auto funcCallContext = FuncCallContext {
        .args = args,
        .instTypeArgs = funcInstTypeArgs
    };
    auto funcCall = CreateAndAppendApplyCallFromCallExpr(*callee, funcCallContext, *retInstTy, expr);
    return funcCall->GetResult();
}

static bool IsCallingConstructor(const AST::CallExpr& expr)
{
    if (expr.resolvedFunction == nullptr) {
        return false;
    }
    if (expr.callKind == AST::CallKind::CALL_SUPER_FUNCTION) {
        return true;
    }
    // non-static init func, because expr.GetTy() is Unit in static init
    return expr.resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR) &&
        !expr.resolvedFunction->TestAttr(AST::Attribute::STATIC);
}

Ptr<Type> Translator::GetMemberFuncCallerInstType(const AST::CallExpr& expr, bool needExactTy)
{
    Type* callerType = nullptr;
    if (auto memAccess = DynamicCast<AST::MemberAccess*>(expr.baseFunc.get()); memAccess) {
        // xxx.memberFunc()
        if (!IsPackageMemberAccess(*memAccess)) {
            callerType = TranslateType(memAccess->baseExpr->DataTy());
        } else if (IsCallingConstructor(expr)) {
            callerType = TranslateType(expr.DataTy());
        }
    } else if (IsCallingConstructor(expr)) {
        callerType = TranslateType(expr.DataTy());
    } else if (expr.resolvedFunction != nullptr && expr.resolvedFunction->outerDecl != nullptr &&
        expr.resolvedFunction->outerDecl->IsNominalDecl()) {
        // call own member function in nominal decl, there are 3 cases:
        auto outerDef = currentBlock->GetTopLevelFunc()->GetParentCustomTypeDef();
        if (outerDef != nullptr) {
            // 1. struct A { func foo() {} }; extend A { func goo() { foo() } }
            //                                                        ^^^  call `foo` in extend A, then return `A`
            // 2. struct A { func foo() {}; func goo() { foo() } }
            //                                           ^^^  call `foo` in struct A, then return `A`
            callerType = outerDef->GetType();
            if (callerType->IsReferenceType()) {
                callerType = builder.GetType<RefType>(callerType);
            }
        } else if (IsStaticInit(*expr.resolvedFunction)) {
            // 3. in CHIR, we treat `static.init()` as global function, not member function,
            // because its outerDecl is something like `class A<T>`, if it's member function, ir is as follows:
            // Func gv$_init() {
            //     Apply(static.init)(A<T>, [], Unit) // `T` is not declared in this scope
            // }
            callerType = nullptr;
        } else {
            // 4. struct A { static let a = foo(); func foo() {} }
            //                              ^^^ call `foo` while initializing static member var, then return `A`
            callerType = TranslateType(expr.resolvedFunction->outerDecl->DataTy());
        }
    }

    if (needExactTy && callerType != nullptr && expr.resolvedFunction != nullptr) {
        auto [paramInstTys, retInstTy] = GetMemberFuncParamAndRetInstTypes(expr);
        if (expr.resolvedFunction != nullptr && !expr.resolvedFunction->TestAttr(AST::Attribute::STATIC)) {
            // Normalize this-modal to match method FuncType; otherwise GetExpectedFunc rejects
            // modal overloads such as `append(this @local!, ...)`.
            paramInstTys.insert(paramInstTys.begin(), GetThisTypeWithModal(*callerType, *expr.resolvedFunction));
        }
        auto instFuncType = builder.GetType<FuncType>(paramInstTys, retInstTy);
        std::vector<Type*> funcInstTypeArgs;
        if (auto nre = DynamicCast<AST::NameReferenceExpr*>(expr.baseFunc.get()); nre) {
            // a constructor mustn't have generic param, `init<T>()` is error, but `baseFunc` may have `instTys`
            // e.g. let x = CA<Int64>()
            // `CA<Int64>()` is CallExpr,
            if (expr.resolvedFunction == nullptr || !expr.resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR)) {
                auto tmp = TranslateASTTypes(nre->instTys);
                funcInstTypeArgs.insert(funcInstTypeArgs.end(), tmp.begin(), tmp.end());
            }
        }
        callerType = GetExactParentType(
            *callerType->StripAllRefs(), *expr.resolvedFunction, *instFuncType, funcInstTypeArgs, false);
        CJC_NULLPTR_CHECK(callerType);
        if (callerType->IsClass()) {
            callerType = builder.GetType<RefType>(callerType);
        }
    }

    // Note that, the constructor doesn't have caller type
    if (expr.resolvedFunction != nullptr && IsStructMutFunction(*expr.resolvedFunction) && callerType != nullptr) {
        callerType = builder.GetType<RefType>(callerType);
    }

    return callerType;
}

std::pair<std::vector<Type*>, Type*> Translator::GetMemberFuncParamAndRetInstTypes(const AST::CallExpr& expr)
{
    FuncType* funcType = nullptr;
    if (auto genericTy = DynamicCast<AST::GenericsTy*>(expr.baseFunc->DataTy())) {
        CJC_ASSERT(genericTy->upperBounds.size() == 1 && "not support multi-upperBounds for funcType in CHIR");
        funcType = StaticCast<FuncType*>(TranslateType(AST::ModalTy{*genericTy->upperBounds.begin()}));
    } else {
        funcType = StaticCast<FuncType*>(TranslateType(expr.baseFunc->DataTy()));
    }
    if (expr.resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR) || expr.resolvedFunction->IsFinalizer()) {
        return std::pair<std::vector<Type*>, Type*>{funcType->GetParamTypes(), builder.GetUnitTy()};
    }
    return std::pair<std::vector<Type*>, Type*>{funcType->GetParamTypes(), funcType->GetReturnType()};
}

Value* Translator::GenerateLoadIfNeccessary(Value& arg, bool isThis, bool isMut, bool isInOut, const DebugLocation& loc)
{
    auto argTy = arg.GetType();
    auto pureArgTy = argTy;
    while (pureArgTy->IsRef()) {
        pureArgTy = StaticCast<RefType*>(pureArgTy)->GetBaseType();
    }

    if (((isMut && isThis) || isInOut) && (pureArgTy->IsValueType() || pureArgTy->IsGeneric())) {
        // We are handling the `this` param for a mut function, thus we need a single-ref type even
        // it is value type
        if (argTy->IsCPointer()) {
            // CPointer is a special since it is value type but represent a pointer thus no need
            // to load
        } else {
            CJC_ASSERT(argTy->IsRef());
            CJC_ASSERT(!StaticCast<RefType*>(argTy)->GetBaseType()->IsRef());
            if (pureArgTy->IsGeneric()) {
                // But if this is a generic type, we still need to generate load cause generic itself can handle
                // mut semantics
                auto baseTy = StaticCast<RefType*>(argTy)->GetBaseType();
                CJC_ASSERT(!baseTy->IsRef());
                return CreateAndAppendExpression<Load>(loc, baseTy, &arg, currentBlock)->GetResult();
            }
        }
    } else {
        // Otherwise, value type will always pass by copy (i.e. with no ref in type) and reference type
        // will always pass by reference (i.e. with single-ref in type). Specially, the generic type and
        // func type are treated like value type
        if (pureArgTy->IsValueType() || pureArgTy->IsGeneric() || pureArgTy->IsFunc()) {
            if (argTy->IsRef()) {
                // Generate load if it is a single-ref value type (due to `var`)
                auto baseTy = StaticCast<RefType*>(argTy)->GetBaseType();
                CJC_ASSERT(!baseTy->IsRef());
                return CreateAndAppendExpression<Load>(loc, baseTy, &arg, currentBlock)->GetResult();
            }
        } else if (pureArgTy->IsReferenceType()) {
            CJC_ASSERT(argTy->IsRef());
            auto baseTy = StaticCast<RefType*>(argTy)->GetBaseType();
            if (baseTy->IsRef()) {
                // Generate load if it is a double-ref reference type (due to `var`)
                return CreateAndAppendExpression<Load>(loc, baseTy, &arg, currentBlock)->GetResult();
            }
        }
    }
    return &arg;
}

bool Translator::HasNothingTypeArg(std::vector<Value*>& args) const
{
    auto it = std::find_if(args.begin(), args.end(), [](auto arg) { return arg->GetType()->IsNothing(); });
    if (it != args.end()) {
        return true;
    }
    return false;
}

Ptr<Value> Translator::ProcessCallExpr(const AST::CallExpr& expr)
{
    if (auto res = TranslateIntrinsicCall(expr)) {
        return res;
    }
    if (auto res = TranslateForeignFuncCall(expr)) {
        return res;
    }
    if (auto res = TranslateCFuncCtorCall(expr)) {
        return res;
    }
    if (auto res = TranslateCStringCtorCall(expr)) {
        return res;
    }
    if (auto res = TranslateEnumCtorCall(expr)) {
        return res;
    }
    if (IsCtorCall(expr)) {
        return TranslateStructOrClassCtorCall(expr);
    }
    if (bool isNothingCall = !expr.resolvedFunction && expr.baseFunc->TyKind() == AST::TypeKind::TYPE_NOTHING;
        isNothingCall) {
        return TranslateExprArg(*expr.baseFunc);
    }
    if (auto res = TranslateFuncTypeValueCall(expr)) {
        return res;
    }
    if (IsNonConstructorMemberFunc(expr.resolvedFunction)) {
        return TranslateMemberFuncCall(expr);
    }
    if (auto res = TranslateTrivialFuncCall(expr)) {
        return res;
    }
    InternalError("translating unsupported CallExpr");
    return nullptr;
}

void Translator::ProcessMapExpr(AST::Node& originExpr, bool isSubScript)
{
    if (auto base = GetMapExpr(originExpr); base) {
        if (exprValueTable.Has(originExpr)) {
            return;
        }
        /* In the following cangjie code, we do not need to translate `base`.
        open class A {
            operator func [](y : Int64) { x }
            operator func [](y : Int64, value! : Int64) { x = value }}
        class B <: A {
            func f() {
                super[3] *= 4
            }
        }
         */
        if (!(isSubScript && originExpr.astKind == AST::ASTKind::REF_EXPR &&
                StaticCast<AST::RefExpr*>(&originExpr)->isSuper)) {
            auto chirNode = TranslateExprArg(originExpr);
            exprValueTable.Set(originExpr, *chirNode);
        }
    }
}

Translator::LeftValueInfo Translator::TranslateCallExprAsLeftValue(const AST::CallExpr& expr)
{
    if (auto res = TranslateStructOrClassCtorCallAsLeftValue(expr); res.base) {
        return res;
    }

    auto val = TranslateASTNode(expr, *this);
    return LeftValueInfo(val, {});
}

void Translator::TranslateCompoundAssignmentElementRef(const AST::MemberAccess& ma)
{
    auto loc = TranslateLocation(ma);
    auto baseResLeftValueInfo = TranslateExprAsLeftValue(*ma.baseExpr);
    auto baseResLeftValuePath = baseResLeftValueInfo.path;
    auto baseResLeftValue = baseResLeftValueInfo.base;
    if (baseResLeftValuePath.empty()) {
        exprValueTable.Set(*ma.baseExpr, *baseResLeftValue);
    } else {
        auto baseResLeftValueCustomType = StaticCast<CustomType*>(baseResLeftValue->GetType()->StripAllRefs());
        if (baseResLeftValue->GetType()->IsReferenceTypeWithRefDims(1) ||
            baseResLeftValue->GetType()->IsValueOrGenericTypeWithRefDims(1)) {
            auto getMemberRef = CreateGetElementRefWithPath(
                loc, baseResLeftValue, baseResLeftValuePath, currentBlock, *baseResLeftValueCustomType);
            exprValueTable.Set(*ma.baseExpr, *getMemberRef);
        } else {
            auto memberType = GetInstMemberTypeByName(*baseResLeftValueCustomType, baseResLeftValuePath, builder);
            CJC_ASSERT(baseResLeftValue->GetType()->IsValueOrGenericTypeWithRefDims(0));
            auto getMember = CreateAndAppendExpression<FieldByName>(
                loc, memberType, baseResLeftValue, baseResLeftValuePath, currentBlock);
            exprValueTable.Set(*ma.baseExpr, *getMember->GetResult());
        }
    }
}

Ptr<Value> Translator::Visit(const AST::CallExpr& callExpr)
{
    /****** Handle side-effect `mapExpr` here ******/
    if (!callExpr.TestAttr(AST::Attribute::SIDE_EFFECT)) {
        return ProcessCallExpr(callExpr);
    }
    CJC_ASSERT(callExpr.resolvedFunction);

    // Case 1: a call expr like:
    //      S.xxxSet(S.xxxGet() + k)
    if (callExpr.resolvedFunction->isSetter) {
        if (callExpr.baseFunc->astKind == AST::ASTKind::MEMBER_ACCESS) {
            auto ma = StaticCast<AST::MemberAccess*>(callExpr.baseFunc.get());
            if (ma->baseExpr->mapExpr != nullptr) {
                TranslateCompoundAssignmentElementRef(*ma);
            }
        } else if (callExpr.baseFunc->astKind == AST::ASTKind::REF_EXPR) {
            // Nothing need to do here
        }
    } else if (callExpr.resolvedFunction->TestAttr(AST::Attribute::OPERATOR)) {
        CJC_ASSERT(callExpr.baseFunc->astKind == AST::ASTKind::MEMBER_ACCESS);
        auto ma = StaticCast<AST::MemberAccess*>(callExpr.baseFunc.get());
        CJC_NULLPTR_CHECK(ma->baseExpr->mapExpr);
        TranslateCompoundAssignmentElementRef(*ma);
    } else {
        CJC_ABORT();
    }
    bool isSubScript = callExpr.resolvedFunction && callExpr.resolvedFunction->identifier == "[]";
    if (isSubScript) {
        // If the case is array access(`[]`), then callexpr's baseFunc and args may have side effect.
        std::vector<Ptr<AST::FuncArg>> args = callExpr.desugarArgs.value();
        CJC_ASSERT(!args.empty());
        for (size_t i = 0; i < args.size() - 1; i++) {
            ProcessMapExpr(*args[i]->expr, isSubScript);
        }
    }
    return ProcessCallExpr(callExpr);
}

void Translator::PrintDevirtualizationMessage(const AST::CallExpr& expr, const std::string& nodeType)
{
    if (!opts.chirDebugOptimizer) {
        return;
    }

    Ptr<AST::FuncDecl> resolvedFunction = expr.resolvedFunction;

    std::string message = "The function call to " + resolvedFunction->identifier + " in the line " +
        std::to_string(expr.begin.line) + " and the column " + std::to_string(expr.begin.column) + " was an " +
        nodeType + " call.\n";
    std::cout << message;
}
} // namespace Cangjie::CHIR
