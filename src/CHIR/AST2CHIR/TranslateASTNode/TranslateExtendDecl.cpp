// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/Type/ExtendDef.h"
#include "cangjie/Modules/ModulesUtils.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

Ptr<Value> Translator::Visit(const AST::ExtendDecl& decl)
{
    CJC_NULLPTR_CHECK(decl.extendedType);
    auto extendDef = StaticCast<ExtendDef*>(GetNominalSymbolTable(decl));

    // step 1: set annotation info
    CreateAnnotationInfo<ExtendDef>(decl, *extendDef, extendDef);

    // step 2: set extended type
    auto extendedTy = chirTy.TranslateType(*decl.extendedType->ty);
    if (extendedTy->IsRef()) {
        extendedTy = StaticCast<RefType*>(extendedTy)->GetBaseType();
    }
    extendDef->SetExtendedType(*extendedTy);

    // step 3: set member func
    for (auto& member : decl.members) {
        if (member->IsCommonMatchedWithPlatform()) {
            /**
             * Source Definitions:
             *   // common.cj
             *   common extend A {
             *     common func foo:Unit {}
             *   }
             *
             *   // platform.cj
             *   platform extend A {
             *     platform func foo:Unit { println("hello") }
             *   }
             *
             * After Sema Merge:
             *   platform extend A {
             *     common func foo:Unit;     // Declaration from common extend
             *     platform func foo:Unit {  // Implementation from platform extend
             *       println("hello")
             *     }
             *   }
             * Note:
             * The common declaration of `foo` should be skiped because it is already covered by the
             * platform-specific implementation. This ensures that the platform implementation is used, avoiding
             * redundancy.
             */
            continue;
        }
        if (member->astKind == AST::ASTKind::FUNC_DECL) {
            auto func = VirtualCast<FuncBase*>(GetSymbolTable(*member));
            extendDef->AddMethod(func);
            auto funcDecl = StaticCast<AST::FuncDecl*>(member.get());
            for (auto& param : funcDecl->funcBody->paramLists[0]->params) {
                if (param->desugarDecl != nullptr) {
                    extendDef->AddMethod(VirtualCast<FuncBase>(GetSymbolTable(*param->desugarDecl)));
                    auto it = genericFuncMap.find(param->desugarDecl.get().get());
                    if (it != genericFuncMap.end()) {
                        for (auto instFunc : it->second) {
                            CJC_NULLPTR_CHECK(instFunc->outerDecl);
                            CJC_ASSERT(instFunc->outerDecl == &decl);
                            extendDef->AddMethod(VirtualCast<FuncBase*>(GetSymbolTable(*instFunc)));
                        }
                    }
                }
            }
            auto it = genericFuncMap.find(funcDecl);
            if (it != genericFuncMap.end()) {
                for (auto instFunc : it->second) {
                    CJC_NULLPTR_CHECK(instFunc->outerDecl);
                    CJC_ASSERT(instFunc->outerDecl == &decl);
                    extendDef->AddMethod(VirtualCast<FuncBase*>(GetSymbolTable(*instFunc)));
                }
            }
            CreateAnnoFactoryFuncsForFuncDecl(StaticCast<AST::FuncDecl>(*member), extendDef);
        } else if (member->astKind == AST::ASTKind::PROP_DECL) {
            AddMemberPropDecl(*extendDef, *RawStaticCast<const AST::PropDecl*>(member.get()));
        } else {
            CJC_ABORT();
        }
    }

    // step 4: set implemented interface
    for (auto& superType : decl.GetStableSuperInterfaceTys()) {
        auto someTy = TranslateType(*superType);
        auto realType = StaticCast<RefType*>(someTy)->GetBaseType();
        extendDef->AddImplementedInterfaceTy(*StaticCast<ClassType*>(realType));
    }

    // step 5: fill upper bounds
    if (decl.TestAttr(AST::Attribute::GENERIC)) {
        CJC_NULLPTR_CHECK(decl.generic);
        auto genericDecl = decl.generic.get();
        for (auto& genericTy : genericDecl->typeParameters) {
            chirTy.FillGenericArgType(*StaticCast<AST::GenericsTy*>(genericTy->ty));
        }
    }

    // step 6: collect annotation info of the type and members for annotation target check
    CollectTypeAnnotation(decl, *extendDef);
    return nullptr;
}
