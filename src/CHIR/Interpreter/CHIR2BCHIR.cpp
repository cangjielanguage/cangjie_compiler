// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements a translation from CHIR to BCHIR.
 */
#include "cangjie/CHIR/Interpreter/CHIR2BCHIR.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Interpreter/Utils.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/CHIR/Value.h"

using namespace Cangjie::CHIR;
using namespace Interpreter;

/** @brief set of non-const exceptions that can be thrown during const-eval evaluation. */
static const std::set<std::string> CONST_EXCEPTIONS = {"@_CNat19ArithmeticExceptionE", "@_CNat5ErrorE",
    "@_CNat9ExceptionE", "@_CNat24IllegalArgumentExceptionE", "@_CNat25IndexOutOfBoundsExceptionE",
    "@_CNat18NoneValueExceptionE", "@_CNat17OverflowExceptionE"};

/** @brief set of non-const functions that can be executed during const-eval evaluation. */
static const std::set<std::string> CONST_FUNCTIONS = {"@_CNat19ArithmeticException6<init>Hv",
    "@_CNat19ArithmeticException6<init>HRNat6StringE", "@_CNat9Exception6<init>Hv",
    "@_CNat9Exception6<init>HRNat6StringE", "@_CNat5Error6<init>Hv", "@_CNat5Error6<init>HRNat6StringE",
    "@_CNat24IllegalArgumentException6<init>Hv", "@_CNat24IllegalArgumentException6<init>HRNat6StringE",
    "@_CNat25IndexOutOfBoundsException6<init>Hv", "@_CNat25IndexOutOfBoundsException6<init>HRNat6StringE",
    "@_CNat17OverflowException6<init>Hv", "@_CNat17OverflowException6<init>HRNat6StringE", "@_CNat5ArrayIhE5cloneHv",
    "@_CNat6String7toArrayHv"};

template <bool ForConstEval> void CHIR2BCHIR::TranslatePackage(
    const Package& chirPkg, const std::vector<CHIR::FuncBase*>& initFuncsForConstVar)
{
    bchir.packageName = chirPkg.GetName();
    if (chirPkg.GetName() == CORE_PACKAGE_NAME) {
        bchir.SetAsCore();
        // the object class isn't defined anywhere, so we force it's generation when compiling core
        bchir.AddSClass("_CN8std$core6ObjectE", Bchir::SClassInfo());
    }
    TranslateClassesLike<ForConstEval>(chirPkg);
    TranslateGlobalVars<ForConstEval>(chirPkg);
    TranslateFunctions<ForConstEval>(chirPkg);
    bchir.SetGlobalInitFunc(chirPkg.GetPackageInitFunc()->GetIdentifierWithoutPrefix());
    if constexpr (ForConstEval) {
        for (const auto v : initFuncsForConstVar) {
            bchir.initFuncsForConsts.emplace_back(v->GetIdentifierWithoutPrefix());
        }
    }
}

static void CollectMethods(const CustomTypeDef& chirClass, Bchir::SClassInfo& classInfo)
{
    for (const auto& it : chirClass.GetVTable()) {
        for (const auto& funcInfo : it.second) {
            if (!funcInfo.typeInfo.sigType) {
                continue;
            }
            auto methodName = MangleMethodName(funcInfo.srcCodeIdentifier, *funcInfo.typeInfo.sigType);
            if (funcInfo.instance != nullptr) {
                classInfo.vtable.emplace(methodName, funcInfo.instance->GetIdentifierWithoutPrefix());
            } // else, this class/interface does not implement the method
        }
    }
}

static void AddClassInfo(
    std::string&& classMangledName, Bchir::SClassInfo&& classInfo, Bchir& bchir, bool isIncremental)
{
    if (isIncremental) {
        bchir.RemoveClass(classMangledName);
    }
    bchir.AddSClass(classMangledName, std::move(classInfo));
}

bool CHIR2BCHIR::IsConstClass(const CustomTypeDef& def) const
{
    if (CONST_EXCEPTIONS.count(def.GetIdentifier()) > 0) {
        return true;
    }
    if (def.IsClassLike()) {
        if (def.TestAttr(Attribute::COMPILER_ADD)) {
            // all typedefs added by compiler are considered const declarations
            return true;
        }
        auto methods = def.GetMethods();
        for (auto ex : def.GetExtends()) {
            auto m = ex->GetMethods();
            methods.insert(methods.end(), m.begin(), m.end());
        }
        for (auto method : methods) {
            if (method->TestAttr(Attribute::CONST)) {
                // if a method is const we assume the class can be const
                return true;
            }
        }
        // if the class does not contain any methods we assume it can be used in a const context
        return methods.empty();
    } else {
        return true;
    }
};

template <bool ForConstEval> void CHIR2BCHIR::TranslateClassesLike(const Package& chirPkg)
{
    TranslateClasses<ForConstEval>(chirPkg);
    TranslateStucts<ForConstEval>(chirPkg);
    TranslateEnums<ForConstEval>(chirPkg);
    TranslateExtends<ForConstEval>(chirPkg);
}

template <bool ForConstEval> void CHIR2BCHIR::TranslateClasses(const Package& chirPkg)
{
    for (const auto chirClass : chirPkg.GetAllClassDef()) {
        if constexpr (ForConstEval) {
            if (!chirClass->IsInterface() && !IsConstClass(*chirClass)) {
                continue;
            }
        }
        Bchir::SClassInfo classInfo;
        auto super = chirClass->GetSuperClassDef();
        if (super != nullptr) {
            classInfo.superClasses.emplace_back(super->GetIdentifierWithoutPrefix());
        }
        auto finalizer = chirClass->GetFinalizer();
        classInfo.finalizer = finalizer != nullptr ? finalizer->GetIdentifierWithoutPrefix() : "";
        CollectMethods(*chirClass, classInfo);
        AddClassInfo(chirClass->GetIdentifierWithoutPrefix(), std::move(classInfo), bchir, isIncremental);
    }
}

template <bool ForConstEval> void CHIR2BCHIR::TranslateStucts(const Package& chirPkg)
{
    for (const auto chirClass : chirPkg.GetAllStructDef()) {
        if constexpr (ForConstEval) {
            if (!IsConstClass(*chirClass)) {
                continue;
            }
        }
        Bchir::SClassInfo classInfo;
        CollectMethods(*chirClass, classInfo);
        AddClassInfo(chirClass->GetIdentifierWithoutPrefix(), std::move(classInfo), bchir, isIncremental);
    }
}

template <bool ForConstEval> void CHIR2BCHIR::TranslateEnums(const Package& chirPkg)
{
    for (const auto chirClass : chirPkg.GetAllEnumDef()) {
        Bchir::SClassInfo classInfo;
        CollectMethods(*chirClass, classInfo);
        AddClassInfo(chirClass->GetIdentifierWithoutPrefix(), std::move(classInfo), bchir, isIncremental);
    }
}

template <bool ForConstEval> void CHIR2BCHIR::TranslateExtends(const Package& chirPkg)
{
    for (const auto chirClass : chirPkg.GetAllExtendDef()) {
        auto extendedDef = chirClass->GetExtendedCustomTypeDef();
        if constexpr (ForConstEval) {
            if (extendedDef != nullptr && !IsConstClass(*extendedDef)) {
                continue;
            }
        }
        if (extendedDef == nullptr) {
            // assumption: this is a primitive type
            auto ty = chirClass->GetExtendedType();
            CJC_ASSERT(ty->IsPrimitive() || ty->IsCString() || ty->IsCPointer());
            auto primitiveClassName = ty->ToString();
            auto classInfo = bchir.GetSClass(primitiveClassName);
            if (classInfo == nullptr) {
                // first time extending primitive type
                Bchir::SClassInfo freshClassInfo;
                CollectMethods(*chirClass, freshClassInfo);
                AddClassInfo(std::move(primitiveClassName), std::move(freshClassInfo), bchir, isIncremental);
            } else {
                CollectMethods(*chirClass, *classInfo);
            }
        } else {
            auto classInfo = bchir.GetSClass(chirClass->GetExtendedCustomTypeDef()->GetIdentifierWithoutPrefix());
            if (classInfo == nullptr) {
                // This should only happen when we are translating CHIR for const-eval
                CJC_ASSERT(ForConstEval);
                continue;
            }
            CollectMethods(*chirClass, *classInfo);
        }
    }
}

template <bool ForConstEval> void CHIR2BCHIR::TranslateGlobalVars(const Package& chirPkg)
{
    for (const auto gv : chirPkg.GetGlobalVars()) {
        if constexpr (ForConstEval) {
            if (!gv->IsCompileTimeValue()) {
                // Global variable not required for const-evaluation.
                continue;
            }
        }
        auto ctx = TranslateGlobalVar<ForConstEval>(*gv);
        auto mangledName = gv->GetIdentifierWithoutPrefix();
        if (mangledName == CHIR::GV_PKG_INIT_ONCE_FLAG) {
            // This is an hack because $has_applied_pkg_init_func is not a real mangled name. It's
            // not unique amongst packages. T0D0: issue 2079
            auto fixedMangledName = CHIR::GV_PKG_INIT_ONCE_FLAG + "-" + bchir.packageName;
                bchir.RemoveGlobalVar(fixedMangledName);
            if (isIncremental) {
            }
            bchir.AddGlobalVar(fixedMangledName, std::move(ctx.def));
        } else {
            if (isIncremental) {
                bchir.RemoveGlobalVar(mangledName);
            }
            bchir.AddGlobalVar(mangledName, std::move(ctx.def));
        }
    }
}

template <bool ForConstEval> void CHIR2BCHIR::TranslateFunctions(const Package& chirPkg)
{
    for (const auto f : chirPkg.GetGlobalFuncs()) {
        auto fIdent = f->GetIdentifierWithoutPrefix();
        if (isIncremental && bchir.GetFunctions().find(fIdent) != bchir.GetFunctions().end()) {
            bchir.RemoveFunction(fIdent);
        }

        if constexpr (ForConstEval) {
            bool missingBody = f->TestAttr(Attribute::SKIP_ANALYSIS) && !f->GetBody();
            if (missingBody) {
                continue;
            }
            // If ForConstEval we only need to translate the IsCompileTimeValue expressions.
            if (
                // it is a const function
                f->IsCompileTimeValue() ||
                // function that can be executed during const-eval
                CONST_FUNCTIONS.count(f->GetIdentifier()) > 0 ||
                // interpreter relies on this set of functions
                (bchir.IsCore() &&
                    std::find(Bchir::defaultFunctionsManledNames.begin(), Bchir::defaultFunctionsManledNames.end(),
                        fIdent) != Bchir::defaultFunctionsManledNames.end()) ||
                // it is a finalizer -- at the moment there is no easy way to find out if a class is const.
                // The right way to do this would be to mark the finalizer as const, when there is a const init.
                f->IsFinalizer()) {
                // need to translate function for const eval
            } else {
                // no need to translate function for const eval
                continue;
            }
        }
        CJC_ASSERT(f->GetBody());

        Context ctx;
        TranslateFuncDef(ctx, *f);
        bchir.AddFunction(fIdent, std::move(ctx.def));
        if (f->GetFuncKind() == FuncKind::MAIN_ENTRY) {
            bchir.SetMainMangledName(fIdent);
            bchir.SetMainExpectedArgs(f->GetNumOfParams());
        }
    }
}

// force instantiation of TranslatePackage with ForConstEval = True and ForConstEval = false
template void CHIR2BCHIR::TranslatePackage<false>(
    const Package& chirPkg, const std::vector<CHIR::FuncBase*>& initFuncsForConstVar);
template void CHIR2BCHIR::TranslatePackage<true>(
    const Package& chirPkg, const std::vector<CHIR::FuncBase*>& initFuncsForConstVar);

Bchir::ByteCodeContent CHIR2BCHIR::GetTypeIdx(Cangjie::CHIR::Type& chirType)
{
    auto itTy = typesMemoization.find(&chirType);
    if (itTy != typesMemoization.end()) {
        return itTy->second;
    }
    auto tyIdx = bchir.AddType(chirType);
    CJC_ASSERT(tyIdx < static_cast<size_t>(Bchir::BYTECODE_CONTENT_MAX));
    typesMemoization.emplace_hint(itTy, &chirType, static_cast<Bchir::ByteCodeContent>(tyIdx));

    switch (chirType.GetTypeKind()) {
        case CHIR::Type::TYPE_INT8:
        case CHIR::Type::TYPE_INT16:
        case CHIR::Type::TYPE_INT32:
        case CHIR::Type::TYPE_INT64:
        case CHIR::Type::TYPE_INT_NATIVE:
        case CHIR::Type::TYPE_UINT8:
        case CHIR::Type::TYPE_UINT16:
        case CHIR::Type::TYPE_UINT32:
        case CHIR::Type::TYPE_UINT64:
        case CHIR::Type::TYPE_UINT_NATIVE:
        case CHIR::Type::TYPE_FLOAT16:
        case CHIR::Type::TYPE_FLOAT32:
        case CHIR::Type::TYPE_FLOAT64:
        case CHIR::Type::TYPE_BOOLEAN:
        case CHIR::Type::TYPE_RUNE:
        case CHIR::Type::TYPE_NOTHING:
        case CHIR::Type::TYPE_CSTRING:
        case CHIR::Type::TYPE_ENUM:
        case CHIR::Type::TYPE_STRUCT:
        case CHIR::Type::TYPE_UNIT:
            break;
        case CHIR::Type::TYPE_CPOINTER: {
            auto& cType = StaticCast<const CPointerType&>(chirType);
            auto elemType = cType.GetElementType();
            (void)GetTypeIdx(*elemType);
            break;
        }
        case CHIR::Type::TYPE_TUPLE: {
            auto& tType = StaticCast<const TupleType&>(chirType);
            for (auto& it : tType.GetElementTypes()) {
                (void)GetTypeIdx(*it);
            }
            break;
        }
        case CHIR::Type::TYPE_FUNC: {
            auto& fType = StaticCast<const FuncType&>(chirType);
            for (auto& it : fType.GetParamTypes()) {
                (void)GetTypeIdx(*it);
            }
            auto retType = fType.GetReturnType();
            (void)GetTypeIdx(*retType);
            break;
        }
        case CHIR::Type::TYPE_RAWARRAY: {
            auto& rType = StaticCast<const RawArrayType&>(chirType);
            auto elemType = rType.GetElementType();
            (void)GetTypeIdx(*elemType);
            break;
        }
        case CHIR::Type::TYPE_REFTYPE: {
            // we can have reference types for CTypes generated by the inout
            auto& refType = StaticCast<const RefType&>(chirType);
            auto elemType = refType.GetBaseType();
            (void)GetTypeIdx(*elemType);
            break;
        }
        case CHIR::Type::TYPE_CLASS: {
            // Temporarily disable CJC_ASSERT(false)
            break;
        }
        default:
            break;
    }
    return static_cast<Bchir::ByteCodeContent>(tyIdx);
}

template <bool ForConstEval> CHIR2BCHIR::Context CHIR2BCHIR::TranslateGlobalVar(const GlobalVar& gv)
{
    Context ctx;
    if (auto init = gv.GetInitializer()) {
        // init is always a literal value, otherwise var will be initialized by a function
        TranslateLiteralValue(ctx, *init);
    } else {
        CJC_ASSERT(gv.GetInitFunc() != nullptr);
        ctx.def.Push(OpCode::NULLPTR);
        if constexpr (ForConstEval) {
            // store the mangled name of the initializer function
            ctx.def.AddMangledNameAnnotation(ctx.def.NextIndex() - 1, gv.GetInitFunc()->GetIdentifierWithoutPrefix());
        }
    }
    // we will append OpCode::GVAR_SET during linking]
    return ctx;
}

void CHIR2BCHIR::TranslateFuncDef(Context& ctx, const Func& func)
{
    auto args = func.GetParams();
    // function parameters are simply local variables
    // reverse iterator is because the last argument will be at the top of the stack
    for (auto arg = args.crbegin(); arg != args.crend(); ++arg) {
        ctx.def.Push(OpCode::LVAR_SET);
        CJC_ASSERT(ctx.val2lvarId.find(*arg) == ctx.val2lvarId.end());
        ctx.def.Push(LVarId(ctx, **arg));
    }
    // dropping the func arg itself
    ctx.def.Push(OpCode::DROP);
    // start translating the entry block
    auto bbGroup = func.GetBody();
    CJC_ASSERT(bbGroup->GetEntryBlock());
    TranslateBlock(ctx, *bbGroup->GetEntryBlock());
    // translate all other blocks
    for (auto bb : bbGroup->GetBlocks()) {
        if (bb == bbGroup->GetEntryBlock()) {
            continue;
        }
        TranslateBlock(ctx, *bb);
    }
    // we use this number to pop from argStack
    ctx.def.SetNumArgs(static_cast<unsigned>(func.GetNumOfParams()));
    // we use this number to reserve space for a env(stack) frame
    ctx.def.SetNumLVars(ctx.localVarId);
}

void CHIR2BCHIR::TranslateBlock(Context& ctx, const Block& bb)
{
    ResolveBB2IndexPlaceHolder(ctx, bb, ctx.def.NextIndex());

    for (auto expr : bb.GetExpressions()) {
        if (expr->GetExprKind() == ExprKind::DEBUGEXPR) {
            continue;
        }
        TranslateExpression(ctx, *expr);
        // Statements without result, i.e. terminators are not required to have a local var
        // Only terminator expr has no result and they do not push values on stack
        // A statement "%1 = expr" essentially represents a local var
        if (!expr->IsTerminator()) {
            PushOpCodeWithAnnotations(ctx, OpCode::LVAR_SET, *expr, LVarId(ctx, *expr->GetResult()));
        }
    }
}

void CHIR2BCHIR::TranslateExpression(Context& ctx, const Expression& expr)
{
    if (expr.GetExprKind() == ExprKind::INVOKE || expr.GetExprKind() == ExprKind::INVOKE_WITH_EXCEPTION) {
        // Create a dummy value on the arg stack, to be dropped when entering the function implementing the method
        // Functions in BCHIR have the strict form LVAR_SET :: ID1 :: ... :: LVAR_SET :: IDn :: DROP :: body, where
        // - LVAR_SET :: ID1 :: ... :: LVAR_SET :: IDn initializes the function arguments by popping args from the stack
        // - DROP drops the FUNC value from the arg stack (or NULLPTR when the function was INVOKED)
        PushOpCodeWithAnnotations(ctx, OpCode::NULLPTR, expr);
    }

    // translate all operands
    for (auto value : expr.GetOperands()) {
        TranslateValue(ctx, *value);
    }

    switch (expr.GetExprMajorKind()) {
        case ExprMajorKind::TERMINATOR:
            TranslateTerminatorExpression(ctx, expr);
            break;
        case ExprMajorKind::UNARY_EXPR:
            CJC_ASSERT(expr.GetResult());
            TranslateUnaryExpression(ctx, expr);
            break;
        case ExprMajorKind::BINARY_EXPR:
            CJC_ASSERT(expr.GetResult());
            TranslateBinaryExpression(ctx, expr);
            break;
        case ExprMajorKind::MEMORY_EXPR:
            CJC_ASSERT(expr.GetResult());
            TranslateMemoryExpression(ctx, expr);
            break;
        case ExprMajorKind::STRUCTURED_CTRL_FLOW_EXPR:
            // unreachable
            CJC_ASSERT(false);
            PushOpCodeWithAnnotations(ctx, OpCode::ABORT, expr);
            break;
        case ExprMajorKind::OTHERS:
            CJC_ASSERT(expr.GetResult());
            TranslateOthersExpression(ctx, expr);
            break;
    }
}

Bchir::ByteCodeContent CHIR2BCHIR::BlockIndex(Context& ctx, const Block& bb, Bchir::ByteCodeIndex indexPlaceHolder)
{
    auto bb2IndexIt = ctx.bb2Index.find(&bb);
    if (bb2IndexIt != ctx.bb2Index.end()) {
        return bb2IndexIt->second;
    }

    auto it = ctx.bb2IndexPlaceHolder.find(&bb);
    if (it == ctx.bb2IndexPlaceHolder.end()) {
        std::vector<Bchir::ByteCodeIndex> vec{indexPlaceHolder};
        ctx.bb2IndexPlaceHolder.emplace(&bb, vec);
    } else {
        it->second.emplace_back(indexPlaceHolder);
    }
    return static_cast<Bchir::ByteCodeContent>(0);
}

void CHIR2BCHIR::ResolveBB2IndexPlaceHolder(Context& ctx, const Block& bb, Bchir::ByteCodeIndex idx)
{
    ctx.bb2Index.emplace(&bb, idx);
    auto it = ctx.bb2IndexPlaceHolder.find(&bb);
    if (it != ctx.bb2IndexPlaceHolder.end()) {
        for (auto ph : it->second) {
            ctx.def.Set(ph, idx);
        }
    }
    ctx.bb2IndexPlaceHolder.erase(&bb);
}

Bchir::ByteCodeContent CHIR2BCHIR::GetStringIdx(std::string str)
{
    auto literalId = constStringsMemoization.find(str);
    if (literalId == constStringsMemoization.end()) {
        auto constIdx = bchir.AddString(str);
        CJC_ASSERT(constIdx <= static_cast<size_t>(Bchir::BYTECODE_CONTENT_MAX));
        constStringsMemoization.emplace(str, static_cast<Bchir::ByteCodeContent>(constIdx));
        return static_cast<Bchir::ByteCodeContent>(constIdx);
    }
    return literalId->second;
}

Bchir::ByteCodeContent CHIR2BCHIR::LVarId(Context& ctx, const Value& value)
{
    auto it = ctx.val2lvarId.find(&value);
    if (it != ctx.val2lvarId.end()) {
        return it->second;
    }

    ctx.val2lvarId.emplace_hint(it, &value, ctx.localVarId);
    return ctx.localVarId++; // increment ctx.localVarId
}


void CHIR2BCHIR::TranslateAllocate(Context& ctx, const Expression& expr)
{
    CHIR::Type* ty;
    bool withException = false;
    if (expr.GetExprKind() == ExprKind::ALLOCATE) {
        auto allocate = StaticCast<const Allocate*>(&expr);
        ty = allocate->GetType();
    } else {
        CJC_ASSERT(expr.GetExprKind() == ExprKind::ALLOCATE_WITH_EXCEPTION);
        auto allocate = StaticCast<const AllocateWithException*>(&expr);
        ty = allocate->GetType();
        withException = true;
    }
    if (ty->IsClass()) {
        auto classTy = StaticCast<const ClassType*>(ty);
        auto numberOfFields = classTy->GetClassDef()->GetAllInstanceVarNum();
        auto idx = ctx.def.NextIndex();
        auto opCode = withException ? OpCode::ALLOCATE_CLASS_EXC : OpCode::ALLOCATE_CLASS;
        PushOpCodeWithAnnotations<false, true>(ctx, opCode, expr, 0, static_cast<unsigned>(numberOfFields));
        ctx.def.AddMangledNameAnnotation(idx, classTy->GetClassDef()->GetIdentifierWithoutPrefix());
    } else if (ty->IsStruct()) {
        auto structTy = StaticCast<const StructType*>(ty);
        auto opCode = withException ? OpCode::ALLOCATE_STRUCT_EXC : OpCode::ALLOCATE_STRUCT;
        PushOpCodeWithAnnotations(
            ctx, opCode, expr, static_cast<unsigned>(structTy->GetStructDef()->GetAllInstanceVarNum()));
    } else {
        auto opCode = withException ? OpCode::ALLOCATE_EXC : OpCode::ALLOCATE;
        PushOpCodeWithAnnotations(ctx, opCode, expr);
    }
}

const std::unordered_map<std::string, IntrinsicKind> CHIR2BCHIR::syscall2IntrinsicKind = {
    {CJ_AST_LEX, IntrinsicKind::FFI_CJ_AST_LEX},
    {CJ_ASTPARSEEXPR, IntrinsicKind::FFI_CJ_AST_PARSEEXPR},
    {CJ_ASTPARSEDECL, IntrinsicKind::FFI_CJ_AST_PARSEDECL},
    {CJ_ASTPARSE_PROPMEMBERDECL, IntrinsicKind::FFI_CJ_AST_PARSE_PROPMEMBERDECL},
    {CJ_ASTPARSE_PRICONSTRUCTOR, IntrinsicKind::FFI_CJ_AST_PARSE_PRICONSTRUCTOR},
    {CJ_ASTPARSEPATTERN, IntrinsicKind::FFI_CJ_AST_PARSE_PATTERN},
    {CJ_ASTPARSETYPE, IntrinsicKind::FFI_CJ_AST_PARSE_TYPE},
    {CJ_ASTPARSETOPLEVEL, IntrinsicKind::FFI_CJ_AST_PARSETOPLEVEL},
    {CJ_PARENT_CONTEXT, IntrinsicKind::FFI_CJ_PARENT_CONTEXT},
    {CJ_MACRO_ITEM_INFO, IntrinsicKind::FFI_CJ_MACRO_ITEM_INFO},
    {CJ_GET_CHILD_MESSAGES, IntrinsicKind::FFI_CJ_GET_CHILD_MESSAGES},
    {CJ_CHECK_ADD_SPACE, IntrinsicKind::FFI_CJ_CHECK_ADD_SPACE},
    {CJ_AST_DIAGREPORT, IntrinsicKind::FFI_CJ_AST_DIAGREPORT},
    {CJ_TLS_DYN_SET_SESSION_CALLBACK_NAME, IntrinsicKind::CJ_TLS_DYN_SET_SESSION_CALLBACK},
    {CJ_TLS_DYN_SSL_INIT_NAME, IntrinsicKind::CJ_TLS_DYN_SSL_INIT},
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    {CJ_CORE_CAN_USE_SIMD_NAME, IntrinsicKind::CJ_CORE_CAN_USE_SIMD},
    {"std.core:CJ_CORE_CanUseSIMD", IntrinsicKind::CJ_CORE_CAN_USE_SIMD},
#endif
};

Bchir::CodePosition CHIR2BCHIR::CHIRPos2BCHIRPos(const DebugLocation& loc)
{
    auto fileName = sourceManager.GetSource(static_cast<unsigned int>(loc.GetFileID())).path;
    auto fileId = fileNameToIndexMemoization.find(fileName);
    size_t fileIdx = 0;
    if (fileId == fileNameToIndexMemoization.end()) {
        fileIdx = bchir.AddFileName(fileName);
        fileNameToIndexMemoization.emplace_hint(fileId, fileName, fileIdx);
    } else {
        fileIdx = fileId->second;
    }
    return Bchir::CodePosition{fileIdx, loc.GetBeginPos().line, loc.GetBeginPos().column};
}

template <bool StoreMangledName, bool StoreCodePos>
void CHIR2BCHIR::AddAnnotations(Context& ctx, const Expression& expr, Bchir::ByteCodeIndex opCodeIndex)
{
    if constexpr (StoreMangledName) {
        auto result = expr.GetResult();
        if (result != nullptr) {
            auto mangled = result->GetIdentifierWithoutPrefix();
            ctx.def.AddMangledNameAnnotation(opCodeIndex, mangled);
        }
    }
    if constexpr (StoreCodePos) {
        auto& loc = expr.GetDebugLocation();
        auto bchirPos = CHIRPos2BCHIRPos(loc);
        ctx.def.AddCodePositionAnnotation(opCodeIndex, bchirPos);
    }
}

template <bool StoreMangledName, bool StoreCodePos>
void CHIR2BCHIR::AddAnnotations(Context& ctx, const Value& value, Bchir::ByteCodeContent opCodeIndex)
{
    if constexpr (StoreMangledName) {
        auto mangled = value.GetIdentifierWithoutPrefix();
        ctx.def.AddMangledNameAnnotation(opCodeIndex, mangled);
    }
    if constexpr (StoreCodePos) {
        auto& loc = value.GetDebugLocation();
        auto bchirPos = CHIRPos2BCHIRPos(loc);
        ctx.def.AddCodePositionAnnotation(opCodeIndex, bchirPos);
    }
}

template void CHIR2BCHIR::AddAnnotations<true, true>(
    Context& ctx, const Value& value, Bchir::ByteCodeContent opCodeIndex);
template void CHIR2BCHIR::AddAnnotations<true, false>(
    Context& ctx, const Value& value, Bchir::ByteCodeContent opCodeIndex);
template void CHIR2BCHIR::AddAnnotations<false, true>(
    Context& ctx, const Value& value, Bchir::ByteCodeContent opCodeIndex);
template void CHIR2BCHIR::AddAnnotations<false, false>(
    Context& ctx, const Value& value, Bchir::ByteCodeContent opCodeIndex);

template void CHIR2BCHIR::AddAnnotations<true, true>(
    Context& ctx, const Expression& expr, Bchir::ByteCodeIndex opCodeIndex);
template void CHIR2BCHIR::AddAnnotations<true, false>(
    Context& ctx, const Expression& expr, Bchir::ByteCodeIndex opCodeIndex);
template void CHIR2BCHIR::AddAnnotations<false, true>(
    Context& ctx, const Expression& expr, Bchir::ByteCodeIndex opCodeIndex);
template void CHIR2BCHIR::AddAnnotations<false, false>(
    Context& ctx, const Expression& expr, Bchir::ByteCodeIndex opCodeIndex);