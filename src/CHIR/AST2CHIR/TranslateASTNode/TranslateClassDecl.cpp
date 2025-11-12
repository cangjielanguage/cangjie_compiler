// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/UserDefinedType.h"
#include "cangjie/Mangle/CHIRTypeManglingUtils.h"
#include "cangjie/Modules/ModulesUtils.h"

namespace Cangjie::CHIR {

Ptr<Value> Translator::Visit(const AST::ClassDecl& decl)
{
    ClassDef* classDef = StaticCast<ClassDef*>(GetNominalSymbolTable(decl).get());
    CJC_NULLPTR_CHECK(classDef);
    TranslateClassLikeDecl(*classDef, decl);
    return nullptr;
}

void Translator::SetClassSuperClass(ClassDef& classDef, const AST::ClassLikeDecl& decl)
{
    if (auto astTy = DynamicCast<AST::ClassTy*>(decl.ty); astTy && astTy->GetSuperClassTy() != nullptr) {
        auto type = TranslateType(*astTy->GetSuperClassTy());
        // The super class must be of the reference.
        CJC_ASSERT(type->IsRef());
        if (!classDef.HasSuperClass()) {
            auto realType = StaticCast<ClassType*>(StaticCast<RefType*>(type.get())->GetBaseType());
            classDef.SetSuperClassTy(*realType);
        }
    }
}

void Translator::SetClassImplementedInterface(ClassDef& classDef, const AST::ClassLikeDecl& decl)
{
    for (auto& superInterfaceTy : decl.GetStableSuperInterfaceTys()) {
        auto type = TranslateType(*superInterfaceTy);
        // The interface must be of the reference.
        CJC_ASSERT(type->IsRef());
        auto realType = StaticCast<ClassType*>(StaticCast<RefType*>(type)->GetBaseType());
        classDef.AddImplementedInterfaceTy(*realType);
    }
}

void Translator::TranslateClassLikeDecl(ClassDef& classDef, const AST::ClassLikeDecl& decl)
{
    // set annotation info
    CreateAnnotationInfo<ClassDef>(decl, classDef, &classDef);

    // set type
    auto classTy = TranslateType(*decl.ty);
    auto baseTy = StaticCast<ClassType*>(RawStaticCast<RefType*>(classTy)->GetBaseType());
    classDef.SetType(*baseTy);
    bool isImportedInstantiated =
        decl.TestAttr(AST::Attribute::IMPORTED) && decl.TestAttr(AST::Attribute::GENERIC_INSTANTIATED);
    classDef.Set<LinkTypeInfo>(isImportedInstantiated ? Linkage::INTERNAL : decl.linkage);

    // common and platform upper bounds are same, do not set again
    if (!(mergingPlatform && decl.TestAttr(AST::Attribute::PLATFORM) && !decl.TestAttr(AST::Attribute::IMPORTED))) {
        // set super class
        SetClassSuperClass(classDef, decl);
        // set implemented interface
        SetClassImplementedInterface(classDef, decl);
    }

    // translate member vars, funcs and props
    const auto& memberDecl = decl.GetMemberDeclPtrs();
    for (auto& member : memberDecl) {
        if (!ShouldTranslateMember(decl, *member)) {
            continue;
        }
        if (member->astKind == AST::ASTKind::VAR_DECL) {
            AddMemberVarDecl(classDef, *RawStaticCast<const AST::VarDecl*>(member));
        } else if (member->astKind == AST::ASTKind::FUNC_DECL) {
            auto funcDecl = RawStaticCast<const AST::FuncDecl*>(member);
            TranslateClassLikeMemberFuncDecl(classDef, *funcDecl);
        } else if (member->astKind == AST::ASTKind::PROP_DECL) {
            AddMemberPropDecl(classDef, *RawStaticCast<const AST::PropDecl*>(member));
        } else if (member->astKind == AST::ASTKind::PRIMARY_CTOR_DECL) {
            // do nothing, primary constructor decl has been desugared to func decl
        } else {
            CJC_ABORT();
        }
    }

    // collect annotation info of the type and members for annotation target check
    CollectTypeAnnotation(decl, classDef);

    // translate vars init for CJMP.
    Translator trans = Copy();
    classDef.SetVarInitializationFunc(trans.TranslateVarsInit(decl));
}

void Translator::AddMemberVarDecl(CustomTypeDef& def, const AST::VarDecl& decl)
{
    if (decl.TestAttr(AST::Attribute::PLATFORM)) {
        return;
    }
    if (decl.TestAttr(AST::Attribute::STATIC)) {
        auto staticVar = VirtualCast<GlobalVarBase*>(GetSymbolTable(decl));
        def.AddStaticMemberVar(staticVar);
        if (auto gv = DynamicCast<GlobalVar>(staticVar)) {
            CreateAnnotationInfo<GlobalVar>(decl, *gv, &def);
        }
    } else {
        Ptr<Type> ty = TranslateType(*decl.ty);
        auto loc = TranslateLocation(decl);
        MemberVarInfo varInfo{
            .name = decl.identifier,
            .rawMangledName = decl.rawMangleName,
            .type = ty,
            .attributeInfo = BuildVarDeclAttr(decl),
            .loc = loc,
            .annoInfo = CreateAnnoFactoryFuncSig(decl, &def),
            // Will be translated later in vars init
            .initializerFunc = nullptr,
            .outerDef = &def
        };
        // If get deserialized one, just need update attrs
        auto memberVars = def.GetDirectInstanceVars();
        for (size_t i = 0; i < memberVars.size(); i++) {
            if (memberVars[i].TestAttr(Attribute::DESERIALIZED) && memberVars[i].name == decl.identifier) {
                memberVars[i] = varInfo;
                def.SetDirectInstanceVars(memberVars);
                return;
            }
        }
        def.AddInstanceVar(varInfo);
    }
}

Func* Translator::ClearOrCreateVarInitFunc(const AST::Decl& decl)
{
    static const std::string POSTFIX = "$varInit";

    const AST::Decl& outerDecl = decl.outerDecl == nullptr ? decl : *decl.outerDecl;

    if (outerDecl.TestAttr(AST::Attribute::IMPORTED) ||
        !outerDecl.TestAnyAttr(AST::Attribute::COMMON, AST::Attribute::PLATFORM)) {
        return nullptr;
    }

    Func* func = nullptr;
    BlockGroup* body = nullptr;
    auto mangledName = decl.mangledName + POSTFIX;
    if (func = TryGetFromCache<Value, Func>(GLOBAL_VALUE_PREFIX + mangledName, deserializedVals); func) {
        // found deserialized one
        body = builder.CreateBlockGroup(*func);
        auto params = func->GetParams();
        CJC_ASSERT(params.size() == 1);
        func->ReplaceBody(*body);
        func->AddParam(*params[0]);
        func->SetDebugLocation(DebugLocation());
    } else {
        auto identifier = decl.identifier + POSTFIX;
        auto rawMangledName = decl.rawMangleName + POSTFIX;
        auto pkgName = outerDecl.fullPackageName;
        const std::vector<Type*> params = {};

        auto returnTy = decl.ty;
        if (auto varDecl = DynamicCast<AST::VarDecl>(&decl)) {
            if (varDecl->initializer) {
                returnTy = varDecl->initializer->ty;
            }
        }
        CJC_ASSERT(returnTy);

        auto returnType = (&decl == &outerDecl) ? builder.GetUnitTy() : TranslateType(*returnTy);
        auto funcType = builder.GetType<FuncType>(params, returnType);
        funcType = AdjustVarInitType(*funcType, outerDecl, builder, chirTy);
        auto loc = DebugLocation(TranslateLocationWithoutScope(builder.GetChirContext(), decl.begin, decl.end));

        auto customTypeDef = chirTy.GetGlobalNominalCache(outerDecl);
        func = builder.CreateFunc(loc, funcType, mangledName, identifier, rawMangledName, pkgName);
        customTypeDef->AddMethod(func);
        func->SetFuncKind(FuncKind::INSTANCEVAR_INIT);
        func->EnableAttr(Attribute::PRIVATE);
        func->EnableAttr(Attribute::COMPILER_ADD);
        func->EnableAttr(Attribute::MUT);

        builder.CreateParameter(funcType->GetParamType(0), loc, *func);
        body = builder.CreateBlockGroup(*func);
        func->InitBody(*body);
    }
    blockGroupStack.emplace_back(body);
    auto entry = builder.CreateBlock(body);
    body->SetEntryBlock(entry);
    auto thisVar = func->GetParam(0);
    CreateAndAppendExpression<Debug>(builder.GetUnitTy(), thisVar, "this", func->GetEntryBlock());

    return func;
}

Func* Translator::TranslateVarInit(const AST::VarDecl& varDecl)
{
    if (!varDecl.initializer) {
        return nullptr;
    }
    auto funcDef = ClearOrCreateVarInitFunc(varDecl);
    if (!funcDef) {
        return nullptr;
    }

    auto loc = DebugLocation(TranslateLocationWithoutScope(builder.GetChirContext(), varDecl.begin, varDecl.end));

    auto entry = funcDef->GetEntryBlock();
    auto thisVar = funcDef->GetParam(0);
    CJC_NULLPTR_CHECK(thisVar);
    SetSymbolTable(*varDecl.outerDecl, *thisVar);

    auto initBlock = CreateBlock();
    currentBlock = initBlock;

    Ptr<Value> value = TranslateExprArg(*varDecl.initializer);

    auto lastBlock = currentBlock;
    auto retType = funcDef->GetReturnType();
    auto retVal =
        CreateAndAppendExpression<Allocate>(loc, builder.GetType<RefType>(retType), retType, lastBlock)->GetResult();
    funcDef->SetReturnValue(*retVal);
    CreateAndAppendExpression<Store>(loc, builder.GetUnitTy(), value, retVal, lastBlock);
    CreateAndAppendTerminator<Exit>(lastBlock);
    CreateAndAppendTerminator<GoTo>(initBlock, entry);
    blockGroupStack.pop_back();

    return funcDef;
}

Func* Translator::TranslateVarsInit(const AST::Decl& decl)
{
    auto funcDef = ClearOrCreateVarInitFunc(decl);
    if (!funcDef) {
        return nullptr;
    }

    auto loc = DebugLocation(TranslateLocationWithoutScope(builder.GetChirContext(), decl.begin, decl.end));

    auto entry = funcDef->GetEntryBlock();
    auto thisVar = funcDef->GetParam(0);
    CJC_NULLPTR_CHECK(thisVar);
    SetSymbolTable(decl, *thisVar);

    auto initBlock = CreateBlock();
    currentBlock = initBlock;

    TranslateVariablesInit(decl, *thisVar);

    auto lastBlock = currentBlock;
    CreateAndAppendTerminator<Exit>(lastBlock);
    CreateAndAppendTerminator<GoTo>(initBlock, entry);
    blockGroupStack.pop_back();

    return funcDef;
}

bool Translator::ShouldTranslateMember(const AST::Decl& decl, const AST::Decl& member) const
{
    if (mergingPlatform && !decl.TestAttr(AST::Attribute::IMPORTED) && decl.TestAttr(AST::Attribute::PLATFORM) &&
        member.TestAttr(AST::Attribute::FROM_COMMON_PART)) {
        // Skip decls from common part when compiling platform
        return false;
    }
    return true;
}

void Translator::AddMemberMethodToCustomTypeDef(const AST::FuncDecl& decl, CustomTypeDef& def)
{
    if (IsStaticInit(decl)) {
        return;
    }
    auto func = VirtualCast<FuncBase>(GetSymbolTable(decl));
    def.AddMethod(func);
    for (auto& param : decl.funcBody->paramLists[0]->params) {
        if (param->desugarDecl != nullptr) {
            def.AddMethod(VirtualCast<FuncBase>(GetSymbolTable(*param->desugarDecl)));
        }
    }
    auto it = genericFuncMap.find(&decl);
    if (it != genericFuncMap.end()) {
        for (auto instFunc : it->second) {
            CJC_NULLPTR_CHECK(instFunc->outerDecl);
            CJC_ASSERT(instFunc->outerDecl == decl.outerDecl);
            def.AddMethod(VirtualCast<FuncBase*>(GetSymbolTable(*instFunc)));
            for (auto& param : instFunc->funcBody->paramLists[0]->params) {
                if (param->desugarDecl != nullptr) {
                    def.AddMethod(VirtualCast<FuncBase>(GetSymbolTable(*param->desugarDecl)));
                }
            }
        }
    }
    CreateAnnoFactoryFuncsForFuncDecl(decl, &def);
}

inline bool Translator::IsOpenPlatformReplaceAbstractCommon(ClassDef& classDef, const AST::FuncDecl& decl) const
{
    // Case 1: Open methods in abstract classes
    bool isAbstractClass = classDef.IsClass() && classDef.IsAbstract();
    bool isOpenInAbstractClass = decl.TestAttr(AST::Attribute::OPEN) && isAbstractClass;
    // Case 2: Static methods in interfaces
    bool isStaticAbstractInInterface = classDef.IsInterface() && decl.TestAttr(AST::Attribute::STATIC);
    // Case 3: Platform providing concrete implementation for abstract interface method
    /**
     * public common interface I {
     *      common func foo1(): Unit
     * }
     *
     * public platform interface I {
     *      platform func foo1(): Unit { println("foo1 of I in platform") }
     * }
     *
     */
    bool isNonAbstractMemberInInterface = classDef.IsInterface() && !decl.TestAttr(AST::Attribute::ABSTRACT);

    if (decl.TestAttr(AST::Attribute::PLATFORM) &&
        (isOpenInAbstractClass || isStaticAbstractInInterface || isStaticAbstractInInterface ||
            isNonAbstractMemberInInterface)) {
        return true;
    }

    return false;
}

inline void Translator::RemoveAbstractMethod(ClassDef& classDef, const AST::FuncDecl& decl) const
{
    const std::string expectedMangledName = decl.mangledName;
    const std::vector<AbstractMethodInfo>& abstractMethods = classDef.GetAbstractMethods();
    std::vector<AbstractMethodInfo> updatedAbstractMethods;
    std::remove_copy_if(std::begin(abstractMethods), std::end(abstractMethods),
                        std::back_inserter(updatedAbstractMethods), [expectedMangledName](auto& abstractMethod) {
        return abstractMethod.GetASTMangledName() == expectedMangledName;
    });
    classDef.SetAbstractMethods(updatedAbstractMethods);
}

void Translator::TranslateClassLikeMemberFuncDecl(ClassDef& classDef, const AST::FuncDecl& decl)
{
    // 1. if func is ABSTRACT, it should be put into `abstractMethods`, not `methods`
    // 2. virtual func need to put into vtable
    //    ps: an abstract func may be not a virtual func, it depends on the logic of `IsVirtualFunction`
    // 3. a func, not ABSTRACT, should be found in global symbol table
    // It is not already translated if cjmp package is imported into current,
    // however in other cases adding platform declaration cause duplication.
    if (mergingPlatform && classDef.TestAttr(CHIR::Attribute::DESERIALIZED)) {
        // Skip if method already exists in deserialized class
        for (auto method : classDef.GetMethods()) {
            if (method->GetIdentifierWithoutPrefix() == decl.mangledName) {
                return;
            }
        }
        if (IsOpenPlatformReplaceAbstractCommon(classDef, decl)) {
            RemoveAbstractMethod(classDef, decl);
        }
        for (auto abstractMethod : classDef.GetAbstractMethods()) {
            if (abstractMethod.GetASTMangledName() == decl.mangledName) {
                return;
            }
        }
    }
    if (decl.TestAttr(AST::Attribute::ABSTRACT)) {
        TranslateAbstractMethod(classDef, decl, false);
    } else {
        AddMemberMethodToCustomTypeDef(decl, classDef);
        if (classDef.IsInterface()) {
            // Member of interface should be recorded in abstract method.
            TranslateAbstractMethod(classDef, decl, true);
        }
    }
}

void Translator::AddMemberPropDecl(CustomTypeDef& def, const AST::PropDecl& decl)
{
    // prop defined within CLASS or INTERFACE can be abstract, so we should treat it as abstract func
    if (def.GetCustomKind() == CustomDefKind::TYPE_CLASS) {
        auto classDef = StaticCast<ClassDef*>(&def);
        for (auto& getter : decl.getters) {
            TranslateClassLikeMemberFuncDecl(*classDef, *getter);
        }
        for (auto& setter : decl.setters) {
            TranslateClassLikeMemberFuncDecl(*classDef, *setter);
        }
    } else {
        // prop defined within STRUCT or ENUM can't be abstract, so just put into method
        for (auto& getter : decl.getters) {
            auto func = VirtualCast<FuncBase>(GetSymbolTable(*getter));
            def.AddMethod(func);
            CreateAnnoFactoryFuncsForFuncDecl(StaticCast<AST::FuncDecl>(*getter), &def);
        }
        for (auto& setter : decl.setters) {
            auto func = VirtualCast<FuncBase>(GetSymbolTable(*setter));
            def.AddMethod(func);
            CreateAnnoFactoryFuncsForFuncDecl(StaticCast<AST::FuncDecl>(*setter), &def);
        }
    }
}

void Translator::TranslateAbstractMethod(ClassDef& classDef, const AST::FuncDecl& decl, bool hasBody)
{
    std::vector<AbstractMethodParam> params;
    auto& args = decl.funcBody->paramLists[0]->params;
    auto funcType = StaticCast<FuncType*>(TranslateType(*decl.ty));
    if (IsInstanceMember(decl)) {
        // Add info of this to the instance method, needed by reflection.
        auto paramTypes = funcType->GetParamTypes();
        auto thisTy = builder.GetType<RefType>(classDef.GetType());
        paramTypes.insert(paramTypes.begin(), thisTy);
        funcType = builder.GetType<FuncType>(paramTypes, funcType->GetReturnType());
        params.emplace_back(AbstractMethodParam{"this", thisTy});
    }
    for (auto& arg : args) {
        params.emplace_back(
            AbstractMethodParam{arg->identifier, TranslateType(*arg->ty), CreateAnnoFactoryFuncSig(*arg, &classDef)});
    }
    std::vector<GenericType*> funcGenericTypeParams;
    if (decl.funcBody->generic != nullptr) {
        for (auto& genericDecl : decl.funcBody->generic->typeParameters) {
            funcGenericTypeParams.emplace_back(StaticCast<GenericType*>(TranslateType(*genericDecl->ty)));
        }
    }

    const AST::Decl& annotationDecl = decl.propDecl ? *decl.propDecl : StaticCast<AST::Decl>(decl);
    auto attr = BuildAttr(decl.GetAttrs());
    attr.SetAttr(Attribute::ABSTRACT, true);
    auto abstractMethod = AbstractMethodInfo{decl.identifier, decl.mangledName, funcType, params, attr,
        CreateAnnoFactoryFuncSig(annotationDecl, &classDef), funcGenericTypeParams, hasBody, &classDef};
    classDef.AddAbstractMethod(abstractMethod);
}
} // namespace Cangjie::CHIR
