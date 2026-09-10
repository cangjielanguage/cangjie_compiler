// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Utils.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/AttributePack.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ConstantsUtils.h"

namespace Cangjie::Native::FFI {

using namespace TypeCheckUtil;

OwnedPtr<RefExpr> CreateThisRef(Ptr<Decl> target, Ptr<Ty> ty, Ptr<File> curFile)
{
    auto thisRef = MakeOwned<RefExpr>();
    thisRef->isThis = true;
    thisRef->SetTy({ty});
    thisRef->ref.identifier = SrcIdentifier("this");
    thisRef->ref.target = target;
    thisRef->curFile = curFile;
    return thisRef;
}

OwnedPtr<CallExpr> CreateThisCall(
    Decl& target, FuncDecl& baseTarget, Ptr<Ty> funcTy, Ptr<File> curFile, std::vector<OwnedPtr<FuncArg>> args)
{
    auto call = CreateCallExpr(CreateThisRef(Ptr(&baseTarget), funcTy, curFile), std::move(args));
    call->callKind = CallKind::CALL_OBJECT_CREATION;
    call->SetTy(target.GetTy());
    call->resolvedFunction = Ptr(&baseTarget);

    return call;
}

OwnedPtr<PrimitiveType> CreateUnitType(Ptr<File> curFile)
{
    auto ret = MakeOwned<PrimitiveType>();
    ret->str = "Unit";
    ret->kind = TypeKind::TYPE_UNIT;
    ret->SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
    ret->curFile = curFile;

    return ret;
}

std::vector<ModalTy> GetParamTys(FuncParamList& params)
{
    std::vector<ModalTy> paramTys;

    for (auto& param : params.params) {
        paramTys.push_back(param->GetTy());
    }
    return paramTys;
}

OwnedPtr<RefExpr> CreateSuperRef(Ptr<Decl> target, Ptr<Ty> ty)
{
    auto superRef = MakeOwned<RefExpr>();
    superRef->isSuper = true;
    superRef->SetTy({ty});
    superRef->ref.identifier = SrcIdentifier("super");
    superRef->ref.target = target;
    return superRef;
}

OwnedPtr<CallExpr> CreateSuperCall(Decl& target, FuncDecl& baseTarget, Ptr<Ty> funcTy)
{
    auto call = CreateCallExpr(CreateSuperRef(Ptr(&baseTarget), funcTy), {});
    call->callKind = CallKind::CALL_SUPER_FUNCTION;
    call->SetTy(target.GetTy());
    call->resolvedFunction = Ptr(&baseTarget);

    return call;
}

OwnedPtr<Type> CreateType(ModalTy ty)
{
    auto res = MakeOwned<Type>();
    res->SetTy({ty});
    return res;
}

OwnedPtr<Type> CreateFuncType(Ptr<FuncTy> ty)
{
    auto res = MakeOwned<FuncType>();
    res->SetTy({ty});

    for (auto param : ty->paramTys) {
        res->paramTypes.push_back(CreateType(param.Ty()));
    }

    return res;
}

OwnedPtr<Expr> CreateBoolMatch(
    OwnedPtr<Expr> selector, OwnedPtr<Expr> trueBranch, OwnedPtr<Expr> falseBranch, ModalTy ty)
{
    static const auto BOOL_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);

    OwnedPtr<ConstPattern> truePattern = MakeOwned<ConstPattern>();
    truePattern->literal = CreateLitConstExpr(LitConstKind::BOOL, "true", {BOOL_TY});
    truePattern->SetTy({BOOL_TY});

    OwnedPtr<ConstPattern> falsePattern = MakeOwned<ConstPattern>();
    falsePattern->literal = CreateLitConstExpr(LitConstKind::BOOL, "false", {BOOL_TY});
    falsePattern->SetTy({BOOL_TY});

    auto caseTrue = CreateMatchCase(std::move(truePattern), std::move(trueBranch));
    auto caseFalse = CreateMatchCase(std::move(falsePattern), std::move(falseBranch));

    std::vector<OwnedPtr<MatchCase>> matchCases;
    matchCases.emplace_back(std::move(caseTrue));
    matchCases.emplace_back(std::move(caseFalse));
    auto curFile = selector->curFile;
    return WithinFile(CreateMatchExpr(std::move(selector), std::move(matchCases), {ty}), curFile);
}

StructDecl& GetStringDecl(const ImportManager& importManager)
{
    static auto decl = importManager.GetCoreDecl<StructDecl>(STD_LIB_STRING);
    CJC_NULLPTR_CHECK(decl);
    return *decl;
}

OwnedPtr<CallExpr> WrapReturningLambdaCall(TypeManager& typeManager, std::vector<OwnedPtr<Node>> nodes)
{
    CJC_ASSERT_WITH_MSG(!nodes.empty(), "cannot create lambda with empty body");
    auto retTy = nodes.back()->GetTy();
    auto lambda = WrapReturningLambdaExpr(typeManager, std::move(nodes));
    return CreateCallExpr(std::move(lambda), {}, nullptr, retTy);
}

OwnedPtr<LambdaExpr> WrapUnitLambdaExpr(
    TypeManager& typeManager, std::vector<OwnedPtr<Node>> nodes, std::vector<OwnedPtr<FuncParam>> lambdaParams)
{
    auto unitLiteral = CreateUnitExpr({TypeManager::GetPrimitiveTy(AST::TypeKind::TYPE_UNIT)});
    nodes.push_back(std::move(unitLiteral));

    return WrapReturningLambdaExpr(typeManager, std::move(nodes), std::move(lambdaParams));
}

OwnedPtr<LambdaExpr> WrapReturningLambdaExpr(
    TypeManager& typeManager, std::vector<OwnedPtr<Node>> nodes, std::vector<OwnedPtr<FuncParam>> lambdaParams)
{
    CJC_ASSERT(!nodes.empty());
    auto curFile = nodes[0]->curFile;
    std::vector<ModalTy> lambdaParamTys;
    std::transform(lambdaParams.begin(), lambdaParams.end(), std::back_inserter(lambdaParamTys),
        [](auto& p) { return p->GetTy(); });
    auto paramLists = Nodes<FuncParamList>(CreateFuncParamList(std::move(lambdaParams)));
    auto retTy = nodes.back()->GetTy();
    auto unsafeBlock = CreateBlock(Nodes(ASTCloner::Clone(Ptr(As<ASTKind::EXPR>(nodes.back().get())))), retTy);
    unsafeBlock->EnableAttr(Attribute::UNSAFE);
    auto retExpr = CreateReturnExpr(std::move(unsafeBlock));
    retExpr->SetTy({TypeManager::GetNothingTy()});
    nodes.pop_back();
    auto lambda =
        CreateLambdaExpr(CreateFuncBody(std::move(paramLists), nullptr, CreateBlock(std::move(nodes), retTy), retTy));
    retExpr->refFuncBody = lambda->funcBody.get();
    lambda->funcBody->body->body.push_back(std::move(retExpr));
    lambda->curFile = curFile;
    lambda->SetTy({typeManager.GetFunctionTy(std::move(lambdaParamTys), retTy)});
    return lambda;
}

std::string GetCangjieLibName(const std::string& outputLibPath, const std::string& fullPackageName, bool trimmed)
{
    if (FileUtil::IsDir(outputLibPath)) {
        return fullPackageName;
    }
    auto outputFileName = FileUtil::GetFileName(outputLibPath);

    constexpr std::string_view libPrefix = "lib";
    // check if [outputLibPath] starts with [LIB_PREFIX]
    if (outputFileName.rfind(libPrefix, 0) == 0) {
        if (!trimmed) {
            return outputFileName;
        }

        size_t extIdx = outputFileName.find_last_of(".");
        if (extIdx == std::string::npos) {
            return fullPackageName;
        }
        return outputFileName.substr(libPrefix.size(), extIdx - libPrefix.size());
    }
    return fullPackageName;
}

std::string GetMangledMethodName(const BaseMangler& mangler, const std::vector<OwnedPtr<FuncParam>>& params,
    const std::string& methodName)
{
    std::string name(methodName);

    for (auto& param : params) {
        auto paramTy = param->GetTy();
        std::string mangledParam = mangler.MangleType(*paramTy);
        std::replace(mangledParam.begin(), mangledParam.end(), '.', '_');
        name += mangledParam;
    }

    return name;
}

Ptr<Annotation> GetForeignNameAnnotation(const Decl& decl)
{
    return FindFirstAnnotation(decl, AnnotationKind::FOREIGN_NAME);
}

bool IsSuperConstructorCall(const CallExpr& call)
{
    auto baseFunc = As<ASTKind::REF_EXPR>(call.baseFunc.get());
    if (!baseFunc || !baseFunc->isSuper) {
        return false;
    }
    return call.callKind == CallKind::CALL_SUPER_FUNCTION;
}

Ptr<Annotation> GetAnnotation(const Decl& decl, AnnotationKind annotationKind)
{
    auto it = std::find_if(decl.annotations.begin(), decl.annotations.end(),
        [annotationKind](const auto& anno) { return anno->kind == annotationKind; });
    return it != decl.annotations.end() ? it->get() : nullptr;
}

Ptr<std::string> GetSingleArgumentAnnotationValue(const Decl& target, AnnotationKind annotationKind)
{
    for (auto& anno : target.annotations) {
        if (anno->kind != annotationKind) {
            continue;
        }

        if (anno->args.size() != 1) {
            break;
        }

        CJC_ASSERT(anno->args.size() == 1);
        CJC_ASSERT(anno->args[0]->expr->astKind == ASTKind::LIT_CONST_EXPR);
        auto lce = As<ASTKind::LIT_CONST_EXPR>(anno->args[0]->expr.get());
        CJC_ASSERT(lce);

        return &lce->stringValue;
    }

    return nullptr;
}

std::string GetObjCMirrorForeignName(const ClassLikeDecl& target)
{
    if (auto customName = GetSingleArgumentAnnotationValue(target, AnnotationKind::OBJ_C_MIRROR)) {
        return *customName;
    }
    return target.identifier.Val();
}

bool IsObjCGeneratedNSStringCtor(const Decl& target)
{
    auto funcDecl = DynamicCast<const FuncDecl>(&target);
    if (!funcDecl || funcDecl->identifier.Val() != INIT_IDENT ||
        !funcDecl->TestAttr(Attribute::CONSTRUCTOR, Attribute::COMPILER_ADD)) {
        return false;
    }

    CJC_ASSERT(funcDecl->funcBody);
    auto& paramLists = funcDecl->funcBody->paramLists;
    CJC_ASSERT(paramLists.size() > 0);
    auto& params = paramLists[0]->params;
    if (params.size() != 1 || params[0]->type->symbol->name != STD_LIB_STRING) {
        return false;
    }

    return true;
}

bool IsObjCGeneratedNSObjectToString(const Decl& target)
{
    auto funcDecl = DynamicCast<const FuncDecl>(&target);
    if (!funcDecl || funcDecl->identifier.Val() != TOSTRING_METHOD_IDENT ||
        !funcDecl->TestAttr(Attribute::COMPILER_ADD) || !funcDecl->funcBody) {
        return false;
    }

    CJC_ASSERT(funcDecl->funcBody);
    auto& paramLists = funcDecl->funcBody->paramLists;
    CJC_ASSERT(paramLists.size() > 0);
    auto& params = paramLists[0]->params;
    if (params.size() != 0) {
        return false;
    }

    return true;
}

bool IsObjCGeneratedMember(const Decl& target)
{
    auto classLikeDecl = DynamicCast<const ClassLikeDecl>(target.outerDecl);
    if (!classLikeDecl) {
        return false;
    }
    auto foreignName = GetObjCMirrorForeignName(*classLikeDecl);
    return (foreignName == NSSTRING_CLASS_IDENT && IsObjCGeneratedNSStringCtor(target)) ||
        (foreignName == NSOBJECT_CLASS_IDENT && IsObjCGeneratedNSObjectToString(target));
}

OwnedPtr<PrimitiveType> GetPrimitiveType(std::string typeName, AST::TypeKind typekind)
{
    OwnedPtr<PrimitiveType> type = MakeOwned<PrimitiveType>();
    type->str = typeName;
    type->kind = typekind;
    type->SetTy({TypeManager::GetPrimitiveTy(typekind)});
    return type;
}

void SplitAndTrim(std::string str, std::vector<std::string>& types)
{
    size_t pos = str.find(',');
    if (pos == std::string::npos) {
        types.push_back(str);
        return;
    }
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        types.push_back(token);
    }
}

std::string JoinVector(const std::vector<std::string>& vec, const std::string& delimiter)
{
    std::string result;
    for (size_t i = 0; i < vec.size(); ++i) {
        result += vec[i];
        if (i != vec.size() - 1) {
            result += delimiter;
        }
    }
    return result;
}

// Current generic just support primitive type
TypeKind GetActualTypeKind(std::string configType)
{
    static const std::unordered_map<std::string, TypeKind> typeMap = {{"Int", TypeKind::TYPE_INT64},
        {"Int8", TypeKind::TYPE_INT8}, {"Int16", TypeKind::TYPE_INT16}, {"Int32", TypeKind::TYPE_INT32},
        {"Int64", TypeKind::TYPE_INT64}, {"IntNative", TypeKind::TYPE_INT_NATIVE}, {"UInt8", TypeKind::TYPE_UINT8},
        {"UInt16", TypeKind::TYPE_UINT16}, {"UInt32", TypeKind::TYPE_UINT32}, {"UInt64", TypeKind::TYPE_UINT64},
        {"UIntNative", TypeKind::TYPE_UINT_NATIVE}, {"Float16", TypeKind::TYPE_FLOAT16},
        {"Float32", TypeKind::TYPE_FLOAT32}, {"Float64", TypeKind::TYPE_FLOAT64}, {"Bool", TypeKind::TYPE_BOOLEAN},
        {"Boolean", TypeKind::TYPE_BOOLEAN}, {"Unit", TypeKind::TYPE_UNIT}};
    auto it = typeMap.find(configType);
    CJC_ASSERT(it != typeMap.end());
    return it->second;
}

Ptr<Ty> GetTyByName(std::string typeStr)
{
    auto typeKind = GetActualTypeKind(typeStr);
    // Current only support primitive type.
    auto ty = TypeManager::GetPrimitiveTy(typeKind);
    return ty;
}

OwnedPtr<Type> GetTypeByName(std::string typeStr)
{
    auto typeKind = GetActualTypeKind(typeStr);
    // Current only support primitive type.
    auto type = GetPrimitiveType(typeStr, typeKind);
    return type;
}

bool IsThisConstructorCall(const CallExpr& call)
{
    auto baseFunc = As<ASTKind::REF_EXPR>(call.baseFunc.get());
    if (!baseFunc || !baseFunc->isThis) {
        return false;
    }
    // this(...) call is a kind of CALL_DECLARED_FUNCTION
    return call.callKind == CallKind::CALL_DECLARED_FUNCTION;
}

Ptr<Ty> GetInstantyForGenericTy(
    Decl& decl, const std::unordered_map<std::string, Ptr<Ty>>& actualTyArgMap, TypeManager& typeManager)
{
    std::vector<Ptr<Ty>> actualTypeArgs;
    for (const auto& typeArgDty : decl.DataTy()->TyArgs()) {
        std::string typeArgName = typeArgDty->name;

        auto it = actualTyArgMap.find(typeArgName);
        if (it != actualTyArgMap.end()) {
            actualTypeArgs.emplace_back(it->second);
        }
    }

    Ptr<Ty> instantTy;
    auto classDecl = As<ASTKind::CLASS_DECL>(&decl);
    if (classDecl) {
        instantTy = typeManager.GetClassTy(*classDecl, actualTypeArgs);
    }
    auto structDecl = As<ASTKind::STRUCT_DECL>(&decl);
    if (structDecl) {
        instantTy = typeManager.GetStructTy(*structDecl, actualTypeArgs);
    }
    auto enumDecl = As<ASTKind::ENUM_DECL>(&decl);
    if (enumDecl) {
        instantTy = typeManager.GetEnumTy(*enumDecl, actualTypeArgs);
    }
    auto interfaceDecl = As<ASTKind::INTERFACE_DECL>(&decl);
    if (interfaceDecl) {
        instantTy = typeManager.GetInterfaceTy(*interfaceDecl, actualTypeArgs);
    }
    return instantTy;
}

ClassDecl& GetExceptionDecl(const ImportManager& importManager)
{
    const static auto exception = [&] {
        const auto exceptionDecl = importManager.GetCoreDecl("Exception");
        CJC_NULLPTR_CHECK(exceptionDecl);

        ClassDecl* res = nullptr;
        if (auto ex = As<ASTKind::CLASS_DECL>(exceptionDecl)) {
            res = ex;
        } else {
            CJC_ABORT_WITH_MSG("'Exception' declaration expected to be 'ClassDecl'");
        }

        return res;
    }();

    return *exception;
}

OwnedPtr<ThrowExpr> CreateThrowExceptionCall(const ImportManager& importManager,
    TypeManager& typeManager, const std::string& msg, Ptr<File> curFile)
{
    auto exceptionArgs = [&] {
        auto exceptionMsg =
            WithinFile(CreateLitConstExpr(LitConstKind::STRING, msg, GetStringDecl(importManager).GetTy()), curFile);
        std::vector<OwnedPtr<Expr>> res;
        res.emplace_back(std::move(exceptionMsg));
        return res;
    }();
    const auto& exception = GetExceptionDecl(importManager);

    return CreateThrowException(exception, std::move(exceptionArgs), *curFile, typeManager);
}

bool AreParamTypeKindsValid(const FuncDecl& fd, const std::vector<TypeKind>& typeKinds)
{
    if (!fd.funcBody || fd.funcBody->paramLists[0]->params.size() != typeKinds.size()) {
        return false;
    }
    for (size_t i = 0; i < typeKinds.size(); ++i) {
        CJC_NULLPTR_CHECK(fd.funcBody->paramLists[0]->params[i]->GetTy());
        if (fd.funcBody->paramLists[0]->params[i]->GetTy()->kind != typeKinds[i]) {
            return false;
        }
    }
    return true;
}

} // namespace Cangjie::Native::FFI
