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

void Translator::TranslateClassLikeDecl(ClassDef& classDef, const AST::ClassLikeDecl& decl)
{
    // step 1: set attribute
    classDef.SetAnnoInfo(CreateAnnoFactoryFuncSig(decl, &classDef));

    // step 2: set type
    auto classTy = TranslateType(*decl.ty);
    auto baseTy = StaticCast<ClassType*>(RawStaticCast<RefType*>(classTy)->GetBaseType());
    classDef.SetType(*baseTy);
    bool isImportedInstantiated =
        decl.TestAttr(AST::Attribute::IMPORTED) && decl.TestAttr(AST::Attribute::GENERIC_INSTANTIATED);
    classDef.Set<LinkTypeInfo>(isImportedInstantiated ? Linkage::INTERNAL : decl.linkage);

    // step 3: set super class
    if (auto astTy = DynamicCast<AST::ClassTy*>(decl.ty); astTy && astTy->GetSuperClassTy() != nullptr) {
        auto type = TranslateType(*astTy->GetSuperClassTy());
        // The super class must be of the reference.
        CJC_ASSERT(type->IsRef());
        auto realType = StaticCast<ClassType*>(StaticCast<RefType*>(type.get())->GetBaseType());
        classDef.SetSuperClassTy(*realType);
    }

    // step 4: set implemented interface
    for (auto& superInterfaceTy : decl.GetStableSuperInterfaceTys()) {
        auto type = TranslateType(*superInterfaceTy);
        // The interface must be of the reference.
        CJC_ASSERT(type->IsRef());
        auto realType = StaticCast<ClassType*>(StaticCast<RefType*>(type)->GetBaseType());
        classDef.AddImplementedInterfaceTy(*realType);
    }

    // step 5: set member var, func and prop
    const auto& memberDecl = decl.GetMemberDeclPtrs();
    for (auto& member : memberDecl) {
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

    // step 6: collect annotation info of the type and members for annotation target check
    CollectTypeAnnotation(decl, classDef);
}

void Translator::AddMemberVarDecl(CustomTypeDef& def, const AST::VarDecl& decl)
{
    if (decl.TestAttr(AST::Attribute::STATIC)) {
        auto staticVar = VirtualCast<GlobalVarBase*>(GetSymbolTable(decl));
        def.AddStaticMemberVar(staticVar);
        if (auto gv = DynamicCast<GlobalVar>(staticVar)) {
            CreateAnnotationInfo<GlobalVar>(decl, *gv,&def);
        }
    } else {
        Ptr<Type> ty = TranslateType(*decl.ty);
        auto loc = TranslateLocation(decl);
        def.AddInstanceVar({.name = decl.identifier,
            .rawMangledName = decl.rawMangleName,
            .type = ty,
            .attributeInfo = BuildVarDeclAttr(decl),
            .loc = loc,
            .annoInfo = CreateAnnoFactoryFuncSig(decl, &def)});
    }
}

void Translator::TranslateClassLikeMemberFuncDecl(ClassDef& classDef, const AST::FuncDecl& decl)
{
    // 1. if func is ABSTRACT, it should be put into `abstractMethods`, not `methods`
    // 2. virtual func need to put into vtable
    //    ps: an abstract func may be not a virtual func, it depends on the logic of `IsVirtualFunction`
    // 3. a func, not ABSTRACT, should be found in global symbol table
    Ptr<FuncBase> func = nullptr;
    if (decl.TestAttr(AST::Attribute::ABSTRACT)) {
        TranslateAbstractMethod(classDef, decl, false);
    } else if (!IsStaticInit(decl)) {
        func = VirtualCast<FuncBase>(GetSymbolTable(decl));
        classDef.AddMethod(func);
        for (auto& param : decl.funcBody->paramLists[0]->params) {
            if (param->desugarDecl != nullptr) {
                classDef.AddMethod(VirtualCast<FuncBase>(GetSymbolTable(*param->desugarDecl)));
            }
        }
        auto it = genericFuncMap.find(&decl);
        if (it != genericFuncMap.end()) {
            for (auto instFunc : it->second) {
                CJC_NULLPTR_CHECK(instFunc->outerDecl);
                CJC_ASSERT(instFunc->outerDecl == decl.outerDecl);
                classDef.AddMethod(VirtualCast<FuncBase*>(GetSymbolTable(*instFunc)));
            }
        }
        if (classDef.IsInterface()) {
            // Member of interface should be recorded in abstract method.
            TranslateAbstractMethod(classDef, decl, true);
        }
    }
    if (func != nullptr) {
        CreateAnnoFactoryFuncsForFuncDecl(StaticCast<AST::FuncDecl>(decl), &classDef);
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
