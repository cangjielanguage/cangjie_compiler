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

Ptr<Value> Translator::Visit(const AST::StructDecl& decl)
{
    auto def = GetNominalSymbolTable(decl);
    CJC_ASSERT(def->GetCustomKind() == CustomDefKind::TYPE_STRUCT);
    auto structDef = StaticCast<StructDef*>(def.get());

    // step 1: set annotation info
    CreateAnnotationInfo<StructDef>(decl, *structDef, structDef);

    // step 2: set type
    auto chirType = StaticCast<StructType*>(chirTy.TranslateType(*decl.ty));
    structDef->SetType(*chirType);
    structDef->Set<LinkTypeInfo>(decl.TestAttr(AST::Attribute::GENERIC_INSTANTIATED)
            ? Linkage::INTERNAL
            : decl.linkage);

    // step 3: set member var, func and prop
    const auto& memberDecl = decl.GetMemberDeclPtrs();
    for (auto& member : memberDecl) {
        if (member->astKind == AST::ASTKind::VAR_DECL) {
            AddMemberVarDecl(*structDef, *RawStaticCast<const AST::VarDecl*>(member));
        } else if (member->astKind == AST::ASTKind::FUNC_DECL) {
            auto funcDecl = StaticCast<AST::FuncDecl*>(member);
            if (!IsStaticInit(*funcDecl)) {
                auto func = VirtualCast<FuncBase*>(GetSymbolTable(*member));
                structDef->AddMethod(func);
                for (auto& param : funcDecl->funcBody->paramLists[0]->params) {
                    if (param->desugarDecl != nullptr) {
                        structDef->AddMethod(VirtualCast<FuncBase>(GetSymbolTable(*param->desugarDecl)));
                    }
                }
                auto it = genericFuncMap.find(funcDecl);
                if (it != genericFuncMap.end()) {
                    for (auto instFunc : it->second) {
                        CJC_NULLPTR_CHECK(instFunc->outerDecl);
                        CJC_ASSERT(instFunc->outerDecl == &decl);
                        structDef->AddMethod(VirtualCast<FuncBase*>(GetSymbolTable(*instFunc)));
                    }
                }
            }
            CreateAnnoFactoryFuncsForFuncDecl(*RawStaticCast<AST::FuncDecl*>(member), structDef);
        } else if (member->astKind == AST::ASTKind::PROP_DECL) {
            AddMemberPropDecl(*structDef, *RawStaticCast<const AST::PropDecl*>(member));
        } else if (member->astKind == AST::ASTKind::PRIMARY_CTOR_DECL) {
            // do nothing, primary constructor decl has been desugared to func decl
        } else {
            CJC_ABORT();
        }
    }
    // step 4: set implemented interface
    for (auto& superInterfaceTy : decl.GetStableSuperInterfaceTys()) {
        auto astType = TranslateType(*superInterfaceTy);
        // The implemented interface type must be of reference type.
        CJC_ASSERT(astType->IsRef());
        auto realType = StaticCast<ClassType*>(StaticCast<RefType*>(astType)->GetBaseType());
        structDef->AddImplementedInterfaceTy(*realType);
    }

    // step 5: collect annotation info of the type and members for annotation target check
    CollectTypeAnnotation(decl, *def);
    return nullptr;
}
