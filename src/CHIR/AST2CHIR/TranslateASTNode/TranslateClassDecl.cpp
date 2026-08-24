// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/Utils/UserDefinedType.h"
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
    if (auto astTy = DynamicCast<AST::ClassTy*>(decl.DataTy()); astTy && astTy->GetSuperClassTy() != nullptr) {
        auto type = TranslateType(astTy->GetSuperClassTy());
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
        auto type = TranslateType(superInterfaceTy);
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
    auto classTy = TranslateType(decl.GetTy());
    auto baseTy = StaticCast<ClassType*>(RawStaticCast<RefType*>(classTy)->GetBaseType());
    classDef.SetType(*baseTy);
    bool isImportedInstantiated =
        decl.TestAttr(AST::Attribute::IMPORTED) && decl.TestAttr(AST::Attribute::GENERIC_INSTANTIATED);
    classDef.Set<LinkTypeInfo>(isImportedInstantiated ? Linkage::INTERNAL : decl.linkage);

    // common and specific upper bounds are same, do not set again
    // specific instantiations require inheritance setup because common side uses templates without instantiation
    bool needInitialSuperClassSetup = !decl.TestAttr(AST::Attribute::SPECIFIC) ||
        decl.TestAttr(AST::Attribute::IMPORTED) || decl.TestAttr(AST::Attribute::GENERIC_INSTANTIATED);
    if (needInitialSuperClassSetup) {
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
}

void Translator::AddMemberVarDecl(CustomTypeDef& def, const AST::VarDecl& decl)
{
    // Member variables of generic instantiated classes or structs need to be regenerated because the common side
    // doesn't have instantiated versions.
    if (decl.TestAttr(AST::Attribute::SPECIFIC) && !def.TestAttr(Attribute::GENERIC_INSTANTIATED)) {
        return;
    }
    if (decl.TestAttr(AST::Attribute::STATIC)) {
        auto staticVar = StaticCast<GlobalVar*>(GetSymbolTable(decl));
        def.AddStaticMemberVar(staticVar);
        if (!decl.TestAttr(AST::Attribute::IMPORTED) || IsSrcCodeImportedGlobalDecl(decl, opts)) {
            CreateAnnotationInfo<GlobalVar>(decl, *staticVar, &def);
        }
    } else {
        Ptr<Type> ty = TranslateType(decl.GetTy());
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

Function* Translator::ClearOrCreateVarInitFunc(const AST::Decl& decl)
{
    static const std::string POSTFIX = "$varInit";

    const AST::Decl& outerDecl = decl.outerDecl == nullptr ? decl : *decl.outerDecl;

    if (outerDecl.TestAttr(AST::Attribute::IMPORTED) ||
        !outerDecl.TestAnyAttr(AST::Attribute::COMMON, AST::Attribute::SPECIFIC)) {
        return nullptr;
    }

    Function* func = nullptr;
    BlockGroup* body = nullptr;
    auto mangledName = decl.mangledName + POSTFIX;
    if (func = TryGetFromCache<Value, Function>(GLOBAL_VALUE_PREFIX + mangledName, deserializedVals); func) {
        // found deserialized one
        body = builder.CreateBlockGroup(*func);
        func->ReplaceBody(*body);
        func->SetDebugLocation(DebugLocation());
    } else {
        auto identifier = decl.identifier + POSTFIX;
        auto rawMangledName = decl.rawMangleName + POSTFIX;
        auto pkgName = outerDecl.fullPackageName;
        const std::vector<Type*> params = {};

        auto returnTy = decl.GetTy();
        if (auto varDecl = DynamicCast<AST::VarDecl>(&decl)) {
            if (varDecl->initializer) {
                returnTy = varDecl->initializer->GetTy();
            }
        }
        CJC_ASSERT(returnTy);

        auto returnType = (&decl == &outerDecl) ? builder.GetUnitTy() : TranslateType(returnTy);
        auto funcType = builder.GetType<FuncType>(params, returnType);
        funcType = AdjustVarInitType(*funcType, outerDecl, builder, chirTy);
        auto loc = DebugLocation(TranslateLocationWithoutScope(builder.GetChirContext(), decl.begin, decl.end));

        auto customTypeDef = chirTy.GetGlobalNominalCache(outerDecl);
        func = builder.CreateFunction(funcType, mangledName, identifier, rawMangledName, pkgName);
        func->SetDebugLocation(loc);
        customTypeDef->AddMethod(func);
        func->SetFuncKind(FuncKind::INSTANCEVAR_INIT);
        func->EnableAttr(Attribute::PRIVATE);
        func->EnableAttr(Attribute::COMPILER_ADD);
        func->EnableAttr(Attribute::MUT);

        auto thisParam = builder.CreateParameter(funcType->GetParamType(0), loc, *func);
        thisParam->SetSrcCodeIdentifier("this");
        body = builder.CreateBlockGroup(*func);
        func->InitBody(*body);
    }
    blockGroupStack.emplace_back(body);
    auto entry = builder.CreateBlock(body);
    body->SetEntryBlock(entry);
    auto unitTyRef = builder.GetType<RefType>(builder.GetUnitTy());
    auto retVal = CreateAndAppendExpression<Allocate>(unitTyRef, builder.GetUnitTy(), entry);
    func->SetReturnValue(*retVal->GetResult());
    auto thisVar = func->GetParam(0);
    CreateAndAppendExpression<Debug>(builder.GetUnitTy(), thisVar, "this", func->GetEntryBlock());

    return func;
}

Function* Translator::TranslateVarInit(const AST::VarDecl& varDecl)
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

Function* Translator::TranslateVarsInit(const AST::Decl& decl)
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

inline bool DeclaredInDifferentFiles(const AST::Decl& d1, const AST::Decl& d2)
{
    return d1.curFile && d2.curFile && d1.curFile != d2.curFile;
}

bool Translator::ShouldTranslateMember(const AST::Decl& decl, const AST::Decl& member) const
{
    if (!mergingSpecific) {
        return true;
    }

    if (decl.TestAttr(AST::Attribute::IMPORTED)) {
        return true;
    }

    if (!decl.TestAttr(AST::Attribute::SPECIFIC)) {
        return true;
    }

    if (decl.TestAttr(AST::Attribute::GENERIC_INSTANTIATED)) {
        // Always translate since common side doesn't have instantiated versions
        return true;
    }

    bool justIntroducedSpecific = !decl.TestAttr(AST::Attribute::IMPORTED) && decl.TestAttr(AST::Attribute::SPECIFIC);
    if (mergingSpecific && justIntroducedSpecific && DeclaredInDifferentFiles(decl, member)) {
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
    auto func = StaticCast<Function>(GetSymbolTable(decl));
    def.AddMethod(func);
    for (auto& param : decl.funcBody->paramLists[0]->params) {
        if (param->desugarDecl != nullptr) {
            def.AddMethod(StaticCast<Function>(GetSymbolTable(*param->desugarDecl)));
        }
    }
    auto it = genericFuncMap.find(&decl);
    if (it != genericFuncMap.end()) {
        for (auto instFunc : it->second) {
            CJC_NULLPTR_CHECK(instFunc->outerDecl);
            CJC_ASSERT(instFunc->outerDecl == decl.outerDecl);
            def.AddMethod(StaticCast<Function*>(GetSymbolTable(*instFunc)));
            for (auto& param : instFunc->funcBody->paramLists[0]->params) {
                if (param->desugarDecl != nullptr) {
                    def.AddMethod(StaticCast<Function>(GetSymbolTable(*param->desugarDecl)));
                }
            }
        }
    }
    CreateAnnoFactoryFuncsForFuncDecl(decl, &def);
}

inline bool Translator::IsOpenSpecificReplaceAbstractCommon(ClassDef& classDef, const AST::FuncDecl& decl) const
{
    // Case 1: Open methods in abstract classes
    bool isAbstractClass = classDef.IsClass() && classDef.IsAbstract();
    bool isOpenInAbstractClass = decl.TestAttr(AST::Attribute::OPEN) && isAbstractClass;
    // Case 2: Static methods in interfaces
    bool isStaticAbstractInInterface = classDef.IsInterface() && decl.TestAttr(AST::Attribute::STATIC);
    // Case 3: Specific providing concrete implementation for abstract interface method
    /**
     * public common interface I {
     *      common func foo1(): Unit
     * }
     *
     * public specific interface I {
     *      specific func foo1(): Unit { println("foo1 of I in specific") }
     * }
     *
     */
    bool isNonAbstractMemberInInterface = classDef.IsInterface() && !decl.TestAttr(AST::Attribute::ABSTRACT);

    if (decl.TestAttr(AST::Attribute::SPECIFIC) &&
        (isOpenInAbstractClass || isStaticAbstractInInterface || isStaticAbstractInInterface ||
            isNonAbstractMemberInInterface)) {
        return true;
    }

    return false;
}

void Translator::TranslateClassLikeMemberFuncDecl(ClassDef& classDef, const AST::FuncDecl& decl)
{
    // Handle member function during specific merging with deserialized classes
    if (SkipMemberFuncInSpecificMerging(classDef, decl)) {
        return;
    }

    AddMemberMethodToCustomTypeDef(decl, classDef);
}

bool Translator::SkipMemberFuncInSpecificMerging(ClassDef& classDef, const AST::FuncDecl& decl)
{
    // Check if we're in specific merging mode with deserialized class
    if (!mergingSpecific || !classDef.TestAttr(CHIR::Attribute::DESERIALIZED)) {
        return false;
    }

    auto it = genericFuncMap.find(&decl);

    // Check if member function already exists in deserialized class
    for (auto method : classDef.GetMethods()) {
        if (method->GetIdentifierWithoutPrefix() == decl.mangledName) {
            if (it != genericFuncMap.end()) {
                AddMemberFunctionGenericInstantiations(classDef, it->second, decl);
            }
            return true; // Member function already exists, skip processing
        }
    }

    return false;
}

void Translator::AddMemberFunctionGenericInstantiations(
    ClassDef& classDef, const std::vector<AST::FuncDecl*>& instFuncs, const AST::FuncDecl& originalDecl)
{
    for (auto instFunc : instFuncs) {
        CJC_NULLPTR_CHECK(instFunc->outerDecl);
        CJC_ASSERT(instFunc->outerDecl == originalDecl.outerDecl);

        // Add the instantiated member function to class
        classDef.AddMethod(StaticCast<Function*>(GetSymbolTable(*instFunc)));

        // Add member function parameter desugar declarations to class
        for (auto& param : instFunc->funcBody->paramLists[0]->params) {
            if (param->desugarDecl != nullptr) {
                classDef.AddMethod(StaticCast<Function>(GetSymbolTable(*param->desugarDecl)));
            }
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
            auto func = StaticCast<Function>(GetSymbolTable(*getter));
            def.AddMethod(func);
            CreateAnnoFactoryFuncsForFuncDecl(StaticCast<AST::FuncDecl>(*getter), &def);
        }
        for (auto& setter : decl.setters) {
            auto func = StaticCast<Function>(GetSymbolTable(*setter));
            def.AddMethod(func);
            CreateAnnoFactoryFuncsForFuncDecl(StaticCast<AST::FuncDecl>(*setter), &def);
        }
    }
}
} // namespace Cangjie::CHIR
