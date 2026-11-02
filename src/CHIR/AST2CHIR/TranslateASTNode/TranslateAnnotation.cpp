// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

#include "cangjie/CHIR/IR/Annotation.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/Mangle/CHIRManglingUtils.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

static const std::string& GetIdentifierToPrint(const AST::Decl& decl)
{
    if (auto prop = DynamicCast<AST::PropDecl>(&decl)) {
        if (!prop->getters.empty()) {
            return prop->getters[0]->identifier;
        }
    }
    return decl.identifier;
}

using namespace AST;

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
// Annotation mangling rules here.
// 1) Annotation Factory func: _CAF + package + orignalDecl + "Hv"
// 2) each annotation var: _CAO + package + originalDecl + 64bit var index + "E"
// 3) each annotation var init: _CAO + package + originalDecl + 64bit var index + "iiHv"
GlobalVar* Translator::TranslateCustomAnnoInstanceSig(const Expr& expr, const Function& func, size_t i)
{
    auto name = func.GetIdentifierWithoutPrefix();
    CJC_ASSERT(name.size() > 3UL);
    // change _CAF prefix to _CAO
    name[3UL] = 'O';
    name += MANGLE_COUNT_PREFIX;
    name += MangleUtils::DecimalToManglingNumber(std::to_string(i));
    auto varName = name + MANGLE_SUFFIX;
    auto gv = builder.CreateGlobalVar(
        builder.GetType<RefType>(TranslateType(expr.GetTy())), varName, varName, "", func.GetPackageName());
    gv->EnableAttr(Attribute::COMPILER_ADD);
    gv->EnableAttr(Attribute::CONST);
    gv->Set<LinkTypeInfo>(Linkage::INTERNAL);
    auto initName = std::move(name) + "iiHv";
    auto ty = builder.GetType<FuncType>(std::vector<Type*>{}, builder.GetUnitTy());
    auto init = builder.CreateFunction(ty, initName, initName, "", func.GetPackageName());
    init->SetFuncKind(FuncKind::GLOBALVAR_INIT);
    init->Set<LinkTypeInfo>(Linkage::INTERNAL);
    init->EnableAttr(Attribute::CONST);
    init->EnableAttr(Attribute::COMPILER_ADD);
    init->EnableAttr(Attribute::INITIALIZER);
    init->EnableAttr(Attribute::NO_INLINE);
    init->EnableAttr(Attribute::NO_REFLECT_INFO);
    initFuncsForAnnoFactory.push_back(init);
    auto bg = builder.CreateBlockGroup(*init);
    init->InitBody(*bg);
    auto bl = builder.CreateBlock(bg);
    bg->SetEntryBlock(bl);
    gv->SetInitFunc(*init);
    auto unitTyRef = builder.GetType<RefType>(builder.GetUnitTy());
    auto retVal = CreateAndAppendExpression<Allocate>(unitTyRef, builder.GetUnitTy(), bl);
    init->SetReturnValue(*retVal->GetResult());
    return gv;
}

std::vector<GlobalVar*> Translator::TranslateAnnotationsArraySig(const ArrayLit& annos, const Function& func)
{
    std::vector<GlobalVar*> res;
    for (size_t i{0}; i < annos.children.size(); ++i) {
        if (isComputingAnnos && annos.children[i]->TestAttr(AST::Attribute::NO_REFLECT_INFO)) {
            continue;
        }
        res.push_back(TranslateCustomAnnoInstanceSig(*annos.children[i], func, i));
    }
    return res;
}

void Translator::TranslateAnnotationsArrayBody(const Decl& decl, Function& func)
{
    auto& annoInsts = func.Get<AnnoFactoryInfo>();
    auto annoArrSize =
        CreateAndAppendConstantExpression<IntLiteral>(builder.GetInt64Ty(), *currentBlock, annoInsts.size())
            ->GetResult();
    auto objectTy = builder.GetType<RefType>(builder.GetObjectTy());
    auto rawArrayTy = builder.GetType<RawArrayType>(objectTy, 1u);
    auto rawArray = CreateAndAppendExpression<RawArrayAllocate>(
        builder.GetType<RefType>(rawArrayTy), objectTy, annoArrSize, currentBlock);
    auto arrayGeneric = builder.GetStructType("std.core", "Array");
    auto genericType = StaticCast<GenericType*>(arrayGeneric->GetGenericArgs()[0]);
    std::unordered_map<const GenericType*, Type*> table{{genericType, objectTy}};
    auto arrayType = ReplaceRawGenericArgType(*arrayGeneric, table, builder);
    auto array = CreateAndAppendExpression<Allocate>(builder.GetType<RefType>(arrayType), arrayType, currentBlock);
    auto arrayMethods = arrayGeneric->GetStructDef()->GetMethods();
    auto arrayInit = std::find_if(arrayMethods.begin(), arrayMethods.end(),
        [](auto method) { return method->IsConstructor() && method->GetNumOfParams() == 4UL; });
    CJC_ASSERT(arrayInit != arrayMethods.end());
    auto zero = CreateAndAppendConstantExpression<IntLiteral>(builder.GetInt64Ty(), *currentBlock, 0u);
    // call array init with RawArrayAllocate
    auto callContext = FuncCallContext {
        .args = std::vector<Value*>{array->GetResult(), rawArray->GetResult(), zero->GetResult(), annoArrSize},
        .thisType = builder.GetType<RefType>(arrayType)
    };
    auto retType = ReplaceRawGenericArgType(*(*arrayInit)->GetFuncType()->GetReturnType(), table, builder);
    CreateAndAppendExpression<Apply>(retType, *arrayInit, callContext, currentBlock);
    // load all Annotation Instances from lifted gv and store them into the return value of annoFactoryFunc
    for (size_t i{0}; i < annoInsts.size(); ++i) {
        auto gv = annoInsts[i];
        auto load =
            CreateAndAppendExpression<Load>(StaticCast<RefType>(gv->GetType())->GetBaseType(), gv, currentBlock);
        auto typecast = CreateAndAppendExpression<ClassStaticCast>(objectTy, load->GetResult(), currentBlock);
        CreateAndAppendExpression<StoreElementRef>(
            builder.GetUnitTy(), typecast->GetResult(), rawArray->GetResult(), std::vector<uint64_t>{i}, currentBlock);
    }
    func.SetReturnValue(*array->GetResult());
    CreateAndAppendTerminator<Exit>(currentBlock);

    // translate Annotation instance init funcs
    // no need to cache currentBlock here, because the current function has been completed
    size_t annoChildrenId{0};
    for (size_t i{0}; i < annoInsts.size(); ++i) {
        while (isComputingAnnos &&
            decl.annotationsArray->children[annoChildrenId]->TestAttr(AST::Attribute::NO_REFLECT_INFO)) {
            ++annoChildrenId;
        }
        auto gv = annoInsts[i];
        auto gvInit = gv->GetInitFunc();
        blockGroupStack.push_back(gvInit->GetBody());
        currentBlock = gvInit->GetEntryBlock();
        TranslateSubExprToLoc(*decl.annotationsArray->children[annoChildrenId++], gv);
        CreateAndAppendTerminator<Exit>(currentBlock);
        blockGroupStack.pop_back();
    }
#ifndef NDEBUG
    while (isComputingAnnos && annoChildrenId < decl.annotationsArray->children.size() &&
        decl.annotationsArray->children[annoChildrenId]->TestAttr(AST::Attribute::NO_REFLECT_INFO)) {
        ++annoChildrenId;
    }
    CJC_ASSERT(decl.annotationsArray->children.size() == annoChildrenId);
#endif
}
#endif

void Translator::TranslateAnnoFactoryFuncBody([[maybe_unused]] const AST::Decl& decl, Function& func)
{
    auto body = builder.CreateBlockGroup(func);
    blockGroupStack.emplace_back(body);
    func.InitBody(*body);
    func.EnableAttr(Attribute::COMPILER_ADD);
    // create body
    auto bodyBlock = CreateBlock();
    body->SetEntryBlock(bodyBlock);
    currentBlock = bodyBlock;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    TranslateAnnotationsArrayBody(decl, func);
#endif
    blockGroupStack.pop_back();
}

AnnoInfo Translator::CreateAnnoFactoryFuncSig(const AST::Decl& decl, CustomTypeDef* parent)
{
    // Since annotations must be consistent between common and specific sides,
    // the specific side can directly use the serialized annotations.
    auto annosArray = decl.annotationsArray.get();
    if (decl.TestAttr(AST::Attribute::IMPORTED) || !annosArray || annosArray->children.empty() ||
        decl.TestAttr(AST::Attribute::SPECIFIC) || opts.disableReflection) {
        return {};
    }

    // In OHOS environment, disable annotations for enum types
    if (opts.target.env == Triple::Environment::OHOS && parent &&
        parent->GetCustomKind() == CustomDefKind::TYPE_ENUM) {
        return {};
    }
    auto found = annotationFuncMap.find(&decl);
    if (found != annotationFuncMap.end()) {
        return found->second; // Property's getters and setters share the same annotation function.
    }
    Type* returnTy = TranslateType(annosArray->GetTy());
    auto funcType = builder.GetType<FuncType>(std::vector<Type*>{}, returnTy);
    const auto& loc = TranslateLocation(*decl.annotationsArray->children[0]);
    std::string mangledName = CHIRMangling::GenerateAnnotationFuncMangleName(decl.mangledName);
    if (opts.chirDebugOptimizer) {
        std::string ms = "The annotation factory function of " + GetIdentifierToPrint(decl) + " in the line " +
            std::to_string(decl.begin.line) + " is " + mangledName + '\n';
        std::cout << ms;
    }
    auto func = builder.CreateFunction(funcType, mangledName, mangledName, "", decl.fullPackageName);
    func->SetDebugLocation(loc);
    func->SetFuncKind(FuncKind::ANNOFACTORY_FUNC);
    func->EnableAttr(Attribute::CONST);
    annoFactoryFuncs.emplace_back(&decl, func);
    if (parent) {
        parent->AddMethod(func);
        func->EnableAttr(Attribute::STATIC);
    }

    auto gvs = TranslateAnnotationsArraySig(*annosArray, *func);
    func->Set<AnnoFactoryInfo>(std::move(gvs));
    // Collect annotations whose parameter values are literal constants.
    // Non-literal args leave annoInstances empty; still register the factory so
    // property getters/setters (same PropDecl) share one entry.
    std::vector<CustomAnnoInstance> annoInstances;
    bool allLiteralArgs = true;
    for (auto& elem : annosArray->children) {
        auto callExpr = StaticCast<AST::CallExpr*>(elem.get().get());
        auto& callee = callExpr->resolvedFunction->funcBody;
        auto annoClassDecl = callee->parentClassLike;
        CJC_ASSERT(annoClassDecl);
        auto annoClassDeclName = annoClassDecl->identifier.GetRawText();
        std::vector<std::string> paramValues;
        for (auto& arg : callExpr->args) {
            if (auto& argVal = arg->expr; argVal->astKind == AST::ASTKind::LIT_CONST_EXPR) {
                auto lit = StaticCast<AST::LitConstExpr*>(argVal.get().get());
                paramValues.emplace_back(lit->rawString);
            } else {
                allLiteralArgs = false;
                break;
            }
        }
        if (!allLiteralArgs) {
            annoInstances.clear();
            break;
        }
        auto annoLoc = TranslateLocation(*elem);
        annoInstances.emplace_back(annoClassDeclName, paramValues, annoLoc);
    }
    auto annoInfo = AnnoInfo(mangledName, std::move(annoInstances));
    annotationFuncMap.emplace(&decl, annoInfo);
    return annoInfo;
}

std::unordered_map<std::string, Ptr<Function>> Translator::jAnnoFuncMap;

void Translator::CreateParamAnnotationInfo(const AST::FuncParam& astParam, Parameter& chirParam, CustomTypeDef& parent)
{
    chirParam.SetAnnoInfo(CreateAnnoFactoryFuncSig(astParam, &parent));
}

void Translator::CreateAnnoFactoryFuncsForFuncDecl(const AST::FuncDecl& funcDecl, CustomTypeDef* parent)
{
    if (funcDecl.TestAttr(AST::Attribute::IMPORTED)) {
        return;
    }
    auto& params = funcDecl.funcBody->paramLists[0]->params;
    auto funcValue = GetSymbolTable(funcDecl);
    const AST::Decl& annotatedDecl = funcDecl.propDecl ? *funcDecl.propDecl : StaticCast<AST::Decl>(funcDecl);
    auto func = StaticCast<Function*>(funcValue);
    CreateAnnotationInfo<Function>(annotatedDecl, *func, parent);
    size_t offset = params.size() == func->GetNumOfParams() ? 0 : 1;
    for (size_t i = 0; i < params.size(); ++i) {
        CreateParamAnnotationInfo(*params[i], *func->GetParam(i + offset), *parent);
    }
}
