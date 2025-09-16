// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/Modules/ModulesUtils.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

Ptr<Value> Translator::Visit(const AST::EnumDecl& decl)
{
    auto def = GetNominalSymbolTable(decl);
    CJC_ASSERT(def->GetCustomKind() == CustomDefKind::TYPE_ENUM);
    auto enumDef = StaticCast<EnumDef*>(def.get());

    // step 1: set annotation info
    CreateAnnotationInfo<EnumDef>(decl, *enumDef, enumDef);

    // step 2: set type
    auto chirType = StaticCast<EnumType*>(TranslateType(*decl.ty));
    enumDef->SetType(*chirType);
    enumDef->Set<LinkTypeInfo>(decl.TestAttr(AST::Attribute::GENERIC_INSTANTIATED) ? Linkage::INTERNAL : decl.linkage);

    // step 3: set constructor
    // e.g. enum A { red | yellow | blue(Int32) }
    // `red`, `yellow` and `blue(Int32)` are called constructors
    // `red` and `yellow` are defined as `VarDecl`, `blue(Int32)` is defined as `FuncDecl`
    for (auto& ctor : decl.constructors) {
        if (ctor->astKind == AST::ASTKind::VAR_DECL) {
            // default enum member store as {} -> EnumType
            enumDef->AddCtor(
                {ctor->identifier, ctor->mangledName, builder.GetType<FuncType>(std::vector<Type*>{}, chirType)});
        } else if (ctor->astKind == AST::ASTKind::FUNC_DECL) {
            std::vector<Type*> paramTypes;
            CJC_ASSERT(!ctor->ty->typeArgs.empty());
            for (size_t i = 0; i < ctor->ty->typeArgs.size() - 1; i++) {
                if (ctor->ty->typeArgs[i] == decl.ty) {
                    paramTypes.emplace_back(chirType);
                } else {
                    paramTypes.emplace_back(TranslateType(*ctor->ty->typeArgs[i]));
                }
            }
            enumDef->AddCtor({ctor->identifier, ctor->mangledName, builder.GetType<FuncType>(paramTypes, chirType)});
        } else {
            CJC_ABORT();
        }
    }

    // step 4: set member func and prop
    for (auto& member : decl.members) {
        if (member->astKind == AST::ASTKind::FUNC_DECL) {
            auto func = VirtualCast<FuncBase*>(GetSymbolTable(*member));
            enumDef->AddMethod(func);
            auto funcDecl = StaticCast<AST::FuncDecl*>(member.get());
            for (auto& param : funcDecl->funcBody->paramLists[0]->params) {
                if (param->desugarDecl != nullptr) {
                    enumDef->AddMethod(VirtualCast<FuncBase>(GetSymbolTable(*param->desugarDecl)));
                }
            }
            auto it = genericFuncMap.find(funcDecl);
            if (it != genericFuncMap.end()) {
                for (auto instFunc : it->second) {
                    CJC_NULLPTR_CHECK(instFunc->outerDecl);
                    CJC_ASSERT(instFunc->outerDecl == &decl);
                    enumDef->AddMethod(VirtualCast<FuncBase*>(GetSymbolTable(*instFunc)));
                }
            }
            CreateAnnoFactoryFuncsForFuncDecl(StaticCast<AST::FuncDecl>(*member), enumDef);
        } else if (member->astKind == AST::ASTKind::PROP_DECL) {
            AddMemberPropDecl(*enumDef, *RawStaticCast<const AST::PropDecl*>(member.get()));
        } else {
            CJC_ABORT();
        }
    }

    // step 5: set implemented interface
    for (auto& superInterfaceTy : decl.GetStableSuperInterfaceTys()) {
        auto astType = TranslateType(*superInterfaceTy);
        // The implemented interface type must be of reference type.
        CJC_ASSERT(astType->IsRef());
        auto realType = StaticCast<ClassType*>(StaticCast<RefType*>(astType)->GetBaseType());
        enumDef->AddImplementedInterfaceTy(*realType);
    }

    // step 6: collect annotation info of the type and members for annotation target check
    CollectTypeAnnotation(decl, *def);
    return nullptr;
}
