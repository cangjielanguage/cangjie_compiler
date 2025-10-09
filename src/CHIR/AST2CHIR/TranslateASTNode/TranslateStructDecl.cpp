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

    // set type
    auto chirType = StaticCast<StructType*>(chirTy.TranslateType(*decl.ty));
    structDef->SetType(*chirType);
    structDef->Set<LinkTypeInfo>(decl.TestAttr(AST::Attribute::GENERIC_INSTANTIATED)
            ? Linkage::INTERNAL
            : decl.linkage);

    // translate func and prop
    const auto& memberDecl = decl.GetMemberDeclPtrs();
    for (auto& member : memberDecl) {
        if (!ShouldTranslateMember(decl, *member)) {
            continue;
        }
        if (member->astKind == AST::ASTKind::VAR_DECL) {
            AddMemberVarDecl(*structDef, *RawStaticCast<const AST::VarDecl*>(member));
        } else if (member->astKind == AST::ASTKind::FUNC_DECL) {
            auto funcDecl = StaticCast<AST::FuncDecl*>(member);
            AddMemberMethodToCustomTypeDef(*funcDecl, *structDef);
        } else if (member->astKind == AST::ASTKind::PROP_DECL) {
            AddMemberPropDecl(*structDef, *RawStaticCast<const AST::PropDecl*>(member));
        } else if (member->astKind == AST::ASTKind::PRIMARY_CTOR_DECL) {
            // do nothing, primary constructor decl has been desugared to func decl
        } else {
            CJC_ABORT();
        }
    }

    // translate var init
    Translator trans = Copy();
    structDef->SetVarInitializationFunc(trans.TranslateVarsInit(decl));

    // set implemented interface
    for (auto& superInterfaceTy : decl.GetStableSuperInterfaceTys()) {
        auto astType = TranslateType(*superInterfaceTy);
        // The implemented interface type must be of reference type.
        CJC_ASSERT(astType->IsRef());
        auto realType = StaticCast<ClassType*>(StaticCast<RefType*>(astType)->GetBaseType());
        structDef->AddImplementedInterfaceTy(*realType);
    }

    // collect annotation info of the type and members for annotation target check
    CollectTypeAnnotation(decl, *def);
    return nullptr;
}
