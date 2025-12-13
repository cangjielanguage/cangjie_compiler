// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/IR/AttributeInfo.h"
#include "cangjie/CHIR/Utils/Utils.h"

#include <mutex>
static std::mutex g_memberVarMutex;
namespace Cangjie::CHIR {
using namespace AST;

void Translator::BindingFuncParam(
    const AST::FuncParamList& paramList, const BlockGroup& funcBody, const BindingConfig& cfg)
{
    const auto& params = GetFuncParams(funcBody);
    size_t offset = paramList.params.size() < params.size() ? 1 : 0;
    CJC_ASSERT(paramList.params.size() + offset == params.size());
    auto block = funcBody.GetEntryBlock();
    for (size_t i = 0; i < paramList.params.size(); ++i) {
        auto arg = params[i + offset];
        auto& param = paramList.params[i];
        if (cfg.setSymbol) {
            SetSymbolTable(*param, *arg);
        }
        if (!cfg.createDebug) {
            continue;
        }
        auto loc = TranslateLocation(*param);
        auto paramLoc = GetDeclLoc(builder.GetChirContext(), *param);
        // Don't need to report unused parameter on defaut value parameter function.
        auto debug = CreateAndAppendExpression<Debug>(
            paramLoc, loc, builder.GetUnitTy(), arg, param->identifier.GetRawText(), block);
        if (cfg.hasInitial) {
            debug->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
        }
    }
}

static Ptr<AST::Decl> GetGenericDecl(const AST::Decl* instantiation)
{
    if (instantiation == nullptr) {
        return nullptr;
    }
    if (instantiation->outerDecl && instantiation->outerDecl->genericDecl) {
        return instantiation->outerDecl->genericDecl;
    }
    if (instantiation->genericDecl) {
        return instantiation->genericDecl;
    }
    return nullptr;
}

static bool IsCopiedSrcFuncFromInterface(const AST::Decl* decl)
{
    if (decl == nullptr) {
        return false;
    }
    // Imported copied function will have rawMangled name, and do not need to record its parent.
    return decl->TestAttr(AST::Attribute::DEFAULT, AST::Attribute::COMPILER_ADD) && decl->outerDecl != nullptr &&
        !decl->TestAttr(AST::Attribute::IMPORTED);
}

static bool IsMemberFuncOfBox(const AST::Decl* decl)
{
    if (decl == nullptr) {
        return false;
    }
    if (decl->astKind != AST::ASTKind::FUNC_DECL || decl->outerDecl == nullptr) {
        return false;
    }
    return decl->outerDecl->identifier.Val().find("$BOX_") != std::string::npos;
}

static bool IsMemberFuncOfExtend(const AST::Decl* decl)
{
    if (decl == nullptr) {
        return false;
    }
    if (decl->astKind != AST::ASTKind::FUNC_DECL || decl->outerDecl == nullptr) {
        return false;
    }
    return decl->outerDecl->astKind == AST::ASTKind::EXTEND_DECL;
}

void Translator::SetRawMangledNameForIncrementalCompile(const AST::FuncDecl& astFunc, Func& chirFunc) const
{
    AST::Decl* decl = nullptr;
    if (astFunc.TestAttr(AST::Attribute::HAS_INITIAL)) {
        // this func is default param of another func, so it doesn't have rawMangledName
        // if this func need to be recompiled, we need to recompile its owner func
        CJC_NULLPTR_CHECK(astFunc.ownerFunc);
        decl = astFunc.ownerFunc;
    }

    // this func is a member method of class, and copied from interface, so it doesn't have rawMangledName
    // if this func need to be recompiled, we need to recompile its class
    if (IsCopiedSrcFuncFromInterface(decl)) {
        CJC_NULLPTR_CHECK(decl);
        decl = decl->outerDecl;
    } else if (IsCopiedSrcFuncFromInterface(&astFunc)) {
        decl = astFunc.outerDecl;
    }

    // this func is a member method of BOX, so it doesn't have rawMangledName
    // if this func need to be recompiled, we need to recompile its class
    if (IsMemberFuncOfBox(decl)) {
        CJC_NULLPTR_CHECK(decl);
        decl = decl->outerDecl;
    } else if (IsMemberFuncOfBox(&astFunc)) {
        decl = astFunc.outerDecl;
    }

    AST::Decl* parent = nullptr;
    if (IsMemberFuncOfExtend(decl)) {
        CJC_NULLPTR_CHECK(decl);
        parent = decl->outerDecl;
    } else if (IsMemberFuncOfExtend(&astFunc)) {
        parent = astFunc.outerDecl;
    }

    // this func is a instantiated func, so it doesn't have rawMangledName
    // if this func need to be recompiled, we need to recompile its generic decl
    if (auto gd1 = GetGenericDecl(decl); gd1) {
        decl = gd1;
    } else if (auto gd2 = GetGenericDecl(&astFunc); gd2) {
        decl = gd2;
    } else if (auto gd3 = GetGenericDecl(parent); gd3) {
        parent = gd3;
    }

    if (decl != nullptr) {
        if (decl->identifier.Val().find("$BOX_") != std::string::npos) {
            chirFunc.SetRawMangledName("$BOX");
        } else {
            chirFunc.SetRawMangledName(decl->rawMangleName);
        }
    } else {
        chirFunc.SetRawMangledName(astFunc.rawMangleName);
    }
}

bool NeedCreateDebugForFirstParam(const Func& func)
{
    if (func.TestAttr(Attribute::STATIC)) {
        return false;
    }
    auto parentDef = func.GetParentCustomTypeDef();
    // global function doesn't have `this`
    if (parentDef == nullptr) {
        return false;
    }
    auto extendDef = DynamicCast<ExtendDef*>(parentDef);
    // function declared in class, struct, enum def need create `Debug(%0, this)`
    if (extendDef == nullptr) {
        return true;
    }
    // function declared in `extend Int32` doesn't need to create `Debug(%0, this)`, it's better in cjdb
    return extendDef->GetExtendedType()->IsNominal();
}

Ptr<Value> Translator::Visit(const AST::FuncDecl& func)
{
    CJC_NULLPTR_CHECK(func.funcBody);
    // Abstract function do not need func body. 'intrinsic' and 'foreign' function was ignored from toplevel.
    if (func.TestAnyAttr(AST::Attribute::INTRINSIC, AST::Attribute::ABSTRACT, AST::Attribute::FOREIGN)) {
        return nullptr;
    }
    if (IsCommonWithoutDefault(func)) {
        return nullptr;
    }
    bool isLifted = localConstFuncs.HasElement(&func);
    if (isLifted && !IsTopLevel(func)) {
        return nullptr;
    }
    if ((func.ownerFunc ? !IsGlobalOrMember(*func.ownerFunc) : !IsGlobalOrMember(func)) && !isLifted) {
        // local const funcs are lifted to global funcs, do not translate as nested func
        return TranslateNestedFunc(func);
    }
    // In incremental compilation, no need to generate body for non-recompile func, but we need to
    // exclude the nested func here
    if (increKind == IncreKind::INCR && !func.toBeCompiled && !IsSrcCodeImportedGlobalDecl(func, opts)) {
        return nullptr;
    }
    CJC_ASSERT(!isLifted || IsTopLevel(func));
    // translateTopLevel
    Func* curFunc = VirtualCast<Func*>(globalSymbolTable.Get(func));
    if (IsCopiedSrcFuncFromInterface(&func)) {
        curFunc->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
    }
    SetRawMangledNameForIncrementalCompile(func, *curFunc);
    if (curFunc->GetFuncKind() == FuncKind::GETTER || curFunc->GetFuncKind() == FuncKind::SETTER) {
        CJC_NULLPTR_CHECK(func.propDecl);
        auto propLoc = TranslateLocation(*func.propDecl);
        curFunc->SetPropLocation(propLoc);
    }

    auto body = curFunc->GetBody();
    blockGroupStack.emplace_back(body);
    auto entry = builder.CreateBlock(body);
    body->SetEntryBlock(entry);
    if (NeedCreateDebugForFirstParam(*curFunc)) {
        auto thisVar = curFunc->GetParam(0);
        CreateAndAppendExpression<Debug>(builder.GetUnitTy(), thisVar, "this", curFunc->GetEntryBlock());
    }
    BindingFuncParam(*func.funcBody->paramLists[0], *curFunc->GetBody(),
        {.hasInitial = func.TestAttr(AST::Attribute::HAS_INITIAL), .createDebug = true});
    // Set return value if the return type is not Void
    auto retType = curFunc->GetReturnType();
    if (!retType->IsVoid()) {
        auto retVal =
            CreateAndAppendExpression<Allocate>(DebugLocation(), builder.GetType<RefType>(retType), retType, entry)
                ->GetResult();
        curFunc->SetReturnValue(*retVal);
    }
    // Translate body.
    auto block = Visit(*func.funcBody.get());
    CreateAndAppendTerminator<GoTo>(StaticCast<Block*>(block.get()), entry);
    blockGroupStack.pop_back();
    return curFunc;
}

Ptr<Value> Translator::Visit(const AST::FuncBody& funcBody)
{
    Ptr<Value> ret{nullptr};
    if (funcBody.funcDecl == nullptr) {
        // Translate lambda body (lambda body is treated as normal func body).
        ret = TranslateFuncBody(funcBody);
    } else {
        auto func = funcBody.funcDecl;
        if (IsInstanceConstructor(*func)) {
            CJC_NULLPTR_CHECK(func->outerDecl);
            if (func->outerDecl->TestAnyAttr(AST::Attribute::COMMON, AST::Attribute::PLATFORM)) {
                ret = TranslateConstructorFunc(*func->outerDecl, funcBody);
            } else {
                ret = TranslateConstructorFuncInline(*func->outerDecl, funcBody);
            }
        } else if (IsInstanceMember(*func)) {
            ret = TranslateInstanceMemberFunc(*func->outerDecl, funcBody);
        } else {
            // Translate normal function
            ret = TranslateFuncBody(funcBody);
        }
    }

    CJC_NULLPTR_CHECK(ret);
    return ret;
}

Ptr<Block> Translator::TranslateFuncBody(const AST::FuncBody& funcBody)
{
    if (funcBody.body == nullptr) {
        return CreateBlock();
    }
    CJC_ASSERT(funcBody.body);
    Visit(*funcBody.body);
    CJC_ASSERT(currentBlock);
    // After translation of body,
    // if the last block is an unreachable block, it may not have terminator and will be removed later,
    // otherwise it must have a terminator.
    CJC_ASSERT((!currentBlock->IsEntry() && currentBlock->GetPredecessors().empty()) ||
        currentBlock->GetTerminator() != nullptr);
    return GetBlockByAST(*funcBody.body);
}

Ptr<Value> Translator::TranslateInstanceMemberFunc(const AST::Decl& parent, const AST::FuncBody& funcBody)
{
    auto curFunc = GetCurrentFunc();
    CJC_NULLPTR_CHECK(curFunc);
    // Binding the implicit 'this' with the first param of the function.
    auto thisVar = curFunc->GetParam(0);
    CJC_NULLPTR_CHECK(thisVar);
    if (parent.astKind == AST::ASTKind::EXTEND_DECL) {
        if (auto typeDecl = AST::Ty::GetDeclOfTy(parent.ty)) {
            SetSymbolTable(*typeDecl, *thisVar);
        }
    } else {
        SetSymbolTable(parent, *thisVar);
    }
    return TranslateFuncBody(funcBody);
}

namespace {
// Get instance field size of super type. Always return 0 for non-class type.
uint64_t GetSuperInstanceVarSize(const Type& thisType)
{
    CJC_ASSERT(thisType.IsRef());
    auto realType = StaticCast<const RefType&>(thisType).GetBaseType();
    if (!realType->IsClass()) {
        return 0;
    }
    auto super = StaticCast<ClassType*>(realType)->GetClassDef()->GetSuperClassDef();
    return super ? super->GetAllInstanceVarNum() : 0;
}

// returns true when the function body contains a delegated this constructor call
bool HasDelegatedThisCall(const AST::FuncBody& func)
{
    CJC_ASSERT(func.body);
    auto& body = func.body->body;
    CJC_ASSERT(!body.empty());
    // first expression is callexpr to constructor
    if (auto call = DynamicCast<AST::CallExpr>(body[0].get())) {
        // the `resolvedFunction` of callExpr with call_function_ptr is null.
        if (call->callKind == AST::CallKind::CALL_FUNCTION_PTR) {
            return false;
        }
        /* example code:
         init() {
            this(1)  // we want to find `this` constructor, and recognized by it's baseFunc is 'this' and
        resolvedFunction is constructor.
        }
        */
        if (call->resolvedFunction && call->resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR)) {
            // callee is a refexpr that is this
            if (auto ref = DynamicCast<RefExpr>(call->baseFunc.get())) {
                return ref->isThis;
            }
        }
    }
    return false;
}
} // unnamed namespace

void Translator::TranslateVariablesInit(const AST::Decl& parent, CHIR::Parameter& thisVar)
{
    auto offset = GetSuperInstanceVarSize(*thisVar.GetType());
    CustomTypeDef* typeDef = GetNominalSymbolTable(parent).get();

    std::unordered_map<std::string, MemberVarInfo*> varInfoByName;
    auto memberVars = typeDef->GetDirectInstanceVars();
    for (auto& varDef : memberVars) {
        varInfoByName.emplace(varDef.name, &varDef);
    }
    auto initOrder = AST::GetVarsInitializationOrderWithPositions(parent);
    for (auto [member, fieldPosition] : initOrder) {
        // Fields declared in primary constructor cannot have initializer
        CJC_ASSERT(!member->isMemberParam || !member->initializer);

        MemberVarInfo* varInfo = varInfoByName.at(member->identifier);
        if (ShouldTranslateMember(parent, *member)) {
            Translator trans = Copy();
            auto initializerFunc = trans.TranslateVarInit(*member);
            varInfo->initializerFunc = initializerFunc;
        }

        auto varInitFunc = varInfo->initializerFunc;
        if (varInitFunc) {
            std::vector<Value*> args{&thisVar};
            auto loc = TranslateLocation(*member);
            auto apply = CreateAndAppendExpression<Apply>(
                loc, varInitFunc->GetReturnType(), varInitFunc, FuncCallContext{
                .args = args,
                .thisType = thisVar.GetType()}, currentBlock);

            auto value = apply->GetResult();
            std::vector<uint64_t> path = {offset + fieldPosition};
            auto pureCurObjType = StaticCast<RefType*>(thisVar.GetType())->GetBaseType();
            CJC_ASSERT(!pureCurObjType->IsRef() && pureCurObjType->IsNominal());
            auto pureCurObjCustomType = StaticCast<CustomType*>(pureCurObjType);
            auto memberType = pureCurObjCustomType->GetInstMemberTypeByPath(path, builder);
            if (value->GetType() == memberType) {
                CreateAndAppendExpression<StoreElementRef>(
                    loc, builder.GetUnitTy(), value, &thisVar, path, currentBlock);
            } else {
                auto castedValue = TypeCastOrBoxIfNeeded(*value, *memberType, loc);
                CreateAndAppendExpression<StoreElementRef>(
                    loc, builder.GetUnitTy(), castedValue, &thisVar, path, currentBlock);
            }
        }
    }
    typeDef->SetDirectInstanceVars(memberVars);
}

Ptr<Value> Translator::TranslateConstructorFunc(const AST::Decl& parent, const AST::FuncBody& funcBody)
{
    CJC_NULLPTR_CHECK(funcBody.funcDecl);
    auto curFunc = GetCurrentFunc();
    CJC_NULLPTR_CHECK(curFunc);
    // Binding the implicit `this` with the first param of the function.
    auto thisVar = curFunc->GetParam(0);
    CJC_NULLPTR_CHECK(thisVar);
    SetSymbolTable(parent, *thisVar);
    // Create init blocks and initializers for constructor. NOTE: do not create 'GoTo' from entry to init here.
    // Return init block for callee to uniformly create 'GoTo' from entry in 'Visit FuncDecl'.
    auto initBlock = CreateBlock();
    currentBlock = initBlock;
    // insert member decl initializer if this constructor does not have delegate this call
    if (!HasDelegatedThisCall(funcBody)) {
        CustomTypeDef* typeDef = GetNominalSymbolTable(parent).get();
        FuncBase* varInitFunc = typeDef->GetVarInitializationFunc();
        CJC_NULLPTR_CHECK(varInitFunc);
        std::vector<Value*> args{thisVar};
        CreateAndAppendExpression<Apply>(DebugLocation(), varInitFunc->GetReturnType(), varInitFunc, FuncCallContext{
            .args = args,
            .thisType = thisVar->GetType()}, currentBlock);
    }
    // After generated the initializers, the 'currentBlock' may change.
    auto lastBlock = currentBlock;
    auto bodyBlock = TranslateFuncBody(funcBody);
    CreateAndAppendTerminator<GoTo>(bodyBlock, lastBlock);
    return initBlock;
}

Ptr<Value> Translator::TranslateConstructorFuncInline(const AST::Decl& parent, const AST::FuncBody& funcBody)
{
    CJC_NULLPTR_CHECK(funcBody.funcDecl);
    auto curFunc = GetCurrentFunc();
    CJC_NULLPTR_CHECK(curFunc);
    // Binding the implicit `this` with the first param of the function.
    auto thisVar = curFunc->GetParam(0);
    CJC_NULLPTR_CHECK(thisVar);
    SetSymbolTable(parent, *thisVar);
    // Create init blocks and initializers for constructor. NOTE: do not create 'GoTo' from entry to init here.
    // Return init block for callee to uniformly create 'GoTo' from entry in 'Visit FuncDecl'.
    auto initBlock = CreateBlock();
    currentBlock = initBlock;
    // insert member decl initializer if this constructor does not have delegate this call
    if (!HasDelegatedThisCall(funcBody)) {
        auto fieldOffset = GetSuperInstanceVarSize(*thisVar->GetType());
        for (auto member : parent.GetMemberDeclPtrs()) {
            if (member->astKind != AST::ASTKind::VAR_DECL || member->TestAttr(AST::Attribute::STATIC)) {
                continue;
            }
            if (auto& init = StaticCast<AST::VarDecl>(member)->initializer) {
                Ptr<Value> value;
                {
                    std::unique_lock<std::mutex> lock(g_memberVarMutex);
                    value = TranslateExprArg(*init);
                }
                auto loc = TranslateLocation(*member);
                std::vector<uint64_t> path = {fieldOffset};
                auto pureCurObjType = StaticCast<RefType*>(thisVar->GetType())->GetBaseType();
                CJC_ASSERT(!pureCurObjType->IsRef() && pureCurObjType->IsNominal());
                auto pureCurObjCustomType = StaticCast<CustomType*>(pureCurObjType);
                auto memberType = pureCurObjCustomType->GetInstMemberTypeByPath(path, builder);
                if (value->GetType() == memberType) {
                    CreateAndAppendExpression<StoreElementRef>(
                        loc, builder.GetUnitTy(), value, thisVar, path, currentBlock);
                } else {
                    auto castedValue = TypeCastOrBoxIfNeeded(*value, *memberType, loc);
                    CreateAndAppendExpression<StoreElementRef>(
                        loc, builder.GetUnitTy(), castedValue, thisVar, path, currentBlock);
                }
            }
            ++fieldOffset;
        }
    }
    // After generated the initializers, the 'currentBlock' may change.
    auto lastBlock = currentBlock;
    auto bodyBlock = TranslateFuncBody(funcBody);
    CreateAndAppendTerminator<GoTo>(bodyBlock, lastBlock);
    return initBlock;
}

Ptr<Value> Translator::TranslateNestedFunc(const AST::FuncDecl& func)
{
    CJC_ASSERT(func.funcBody && func.funcBody->body);
    if (func.TestAttr(AST::Attribute::GENERIC)) {
        TranslateFunctionGenericUpperBounds(chirTy, func);
    }
    auto lambdaTrans = SetupContextForLambda(*func.funcBody->body);
    auto funcTy = RawStaticCast<FuncType*>(TranslateType(*func.ty));
    // Create nested functions' body and parameters.
    CJC_NULLPTR_CHECK(currentBlock->GetTopLevelFunc());
    BlockGroup* body = builder.CreateBlockGroup(*currentBlock->GetTopLevelFunc());
    const auto& loc = TranslateLocation(func);
    std::vector<GenericType*> genericTys;
    // Only collect for generic function which has not been instantiated.
    if (auto generic = func.funcBody->generic.get(); generic && func.TestAttr(AST::Attribute::GENERIC)) {
        for (auto& type : generic->typeParameters) {
            genericTys.emplace_back(RawStaticCast<GenericType*>(TranslateType(*type->ty)));
        }
    }
    std::string lambdaMangleName = func.mangledName;
    Lambda* lambda = CreateAndAppendExpression<Lambda>(
        loc, funcTy, funcTy, currentBlock, true, lambdaMangleName, func.identifier, genericTys);
    CJC_ASSERT(lambda);
    lambda->InitBody(*body);
    if (func.isConst) {
        lambda->SetCompileTimeValue();
    }

    std::vector<DebugLocation> paramLoc;
    for (auto& astParam : func.funcBody->paramLists[0]->params) {
        paramLoc.emplace_back(TranslateLocationWithoutScope(builder.GetChirContext(), astParam->begin, astParam->end));
    }
    auto paramTypes = funcTy->GetParamTypes();
    CJC_ASSERT(paramTypes.size() == paramLoc.size());
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        builder.CreateParameter(paramTypes[i], paramLoc[i], *lambda);
    }

    SetSymbolTable(func, *lambda->GetResult());
    for (auto& param : func.funcBody->paramLists[0]->params) {
        if (param->desugarDecl) {
            // recursively translate default parameter function of a lambda (possibly a local func)
            auto defaultArgFunc = TranslateNestedFunc(*param->desugarDecl);
            StaticCast<Lambda*>(StaticCast<LocalVar*>(defaultArgFunc)->GetExpr())->SetParamDftValHostFunc(*lambda);

            // set such functions as local symbols in the current scope but not in the lambda translation context
            // as a default parameter function can only be called outside the function
            lambdaTrans.SetSymbolTable(*param->desugarDecl, *defaultArgFunc);
        }
    }
    // Local function allows to call itself inside funcBody.
    lambdaTrans.SetSymbolTable(func, *lambda->GetResult());
    return lambdaTrans.TranslateLambdaBody(
        lambda, *func.funcBody, {.hasInitial = func.TestAttr(AST::Attribute::HAS_INITIAL)});
}
} // namespace Cangjie::CHIR
