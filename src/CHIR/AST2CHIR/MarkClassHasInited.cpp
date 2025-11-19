// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/MarkClassHasInited.h"

#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/NativeFFI/Utils.h"
#include "cangjie/CHIR/Type/ClassDef.h"

using namespace Cangjie::CHIR;

MarkClassHasInited::MarkClassHasInited(CHIRBuilder& builder)
    : builder(builder)
{
}

void MarkClassHasInited::AddHasInitedFlagToClassDef(ClassDef& classDef)
{
    // Java and Objective-C mirrors have this field generated from AST.
    if (Native::FFI::IsMirror(classDef)) {
        return;
    }

    auto attributeInfo = AttributeInfo();
    attributeInfo.SetAttr(Attribute::NO_REFLECT_INFO, true);
    attributeInfo.SetAttr(Attribute::COMPILER_ADD, true);
    attributeInfo.SetAttr(Attribute::PRIVATE, true);
    attributeInfo.SetAttr(Attribute::HAS_INITED_FIELD, true);
    classDef.AddInstanceVar(MemberVarInfo{
        .name = Cangjie::HAS_INITED_IDENT,
        .type = builder.GetBoolTy(),
        .attributeInfo = attributeInfo,
        .outerDef = &classDef
    });
}

void MarkClassHasInited::AddGuardToFinalizer(ClassDef& classDef)
{
    auto finalizer = Cangjie::DynamicCast<Cangjie::CHIR::Func*>(classDef.GetFinalizer());
    if (!finalizer) {
        // the finalizer may be an ImportedFunc when:
        // 1. incremental compilation
        // 2. class is imported
        return;
    }
    /*
        class CA {
            var hasInited: Bool
            ~init() {
                if (!hasInited) {
                    return;
                }
                ...
            }
        }
    */
    auto block = builder.CreateBlock(finalizer->GetBody());
    auto thisArg = finalizer->GetParam(0);
    CJC_NULLPTR_CHECK(thisArg);
    auto boolTy = builder.GetBoolTy();
    auto path = std::vector<std::string>{ Cangjie::HAS_INITED_IDENT };
    auto ref = builder.CreateExpression<GetElementByName>(builder.GetType<RefType>(boolTy), thisArg, path, block);
    auto load = builder.CreateExpression<Load>(boolTy, ref->GetResult(), block);

    auto entry = finalizer->GetEntryBlock();
    auto exit = builder.CreateBlock(finalizer->GetBody());
    exit->AppendExpression(builder.CreateTerminator<Exit>(exit));
    auto cond = builder.CreateTerminator<Branch>(load->GetResult(), entry, exit, block);
    block->AppendExpressions({ref, load, cond});
    finalizer->GetBody()->SetEntryBlock(block);
}

void MarkClassHasInited::AssignHasInitedFlagToFalseInConstructorHead(Func& constructor)
{
    /*
        class CA {
            var hasInited: Bool
            init() {
                hasInited = false
                ...
            }
        }
    */
    auto boolTy = builder.GetBoolTy();
    auto entry = constructor.GetEntryBlock();
    auto falseVal = builder.CreateConstantExpression<BoolLiteral>(boolTy, entry, false);
    auto thisArg = constructor.GetParam(0);
    CJC_NULLPTR_CHECK(thisArg);
    auto path = std::vector<std::string>{ Cangjie::HAS_INITED_IDENT };
    auto storeRef =
        builder.CreateExpression<StoreElementByName>(builder.GetUnitTy(), falseVal->GetResult(), thisArg, path, entry);
    entry->InsertExprIntoHead(*storeRef);
    entry->InsertExprIntoHead(*falseVal);
}

void MarkClassHasInited::AssignHasInitedFlagToTrueInConstructorExit(Func& constructor)
{
    /*
        class CA {
            var hasInited: Bool
            init() {
                ...
                if (xxx) {
                    ...
                    hasInited = true
                }
                hasInited = true
            }
        }
    */
    auto boolTy = builder.GetBoolTy();
    auto thisArg = constructor.GetParam(0);
    for (auto block : constructor.GetBody()->GetBlocks()) {
        auto terminator = block->GetTerminator();
        if (!terminator || terminator->GetExprKind() != ExprKind::EXIT) {
            continue;
        }
        auto trueVal = builder.CreateConstantExpression<BoolLiteral>(boolTy, block, true);
        trueVal->MoveBefore(terminator);
        auto path = std::vector<std::string>{ Cangjie::HAS_INITED_IDENT };
        auto storeRef =
            builder.CreateExpression<StoreElementByName>(builder.GetUnitTy(), trueVal->GetResult(), thisArg, path, block);
        storeRef->MoveBefore(terminator);
    }
}

void MarkClassHasInited::RunOnPackage(const Package& package)
{
    /**
     * To prevent any use-before-intialisation behaviour, we add a member variable
     * `hasInited` to indicate if this class has been initialised. The finalizer of
     * the class won't execute if the flag is false.
     *
     *  class CA {                              class CA {
     *      var x: Int64                            var x: Int64
     *      init() {                                var hasInited: Bool
     *          throw Exception()       ==>         init() {
     *      }                                           hasInited = false
     *      ~init() {                                   throw Exception()
     *          println(x)  // illegal                  hasInited = true
     *      }                                       }
     *  }                                           ~init() {
     *                                                  if (hasInited) {
     *                                                      println(x)      // won't be executed
     *                                                  }
     *                                              }
     */

    for (auto classDef : package.GetAllClassDef()) {
        if (classDef->GetFinalizer() == nullptr) {
            continue;
        }
        AddHasInitedFlagToClassDef(*classDef);
        for (auto funcBase : classDef->GetMethods()) {
            if (!funcBase->IsConstructor()) {
                continue;
            }
            if (auto func = DynamicCast<Func*>(funcBase)) {
                AssignHasInitedFlagToFalseInConstructorHead(*func);
                AssignHasInitedFlagToTrueInConstructorExit(*func);
            }
        }

        AddGuardToFinalizer(*classDef);
    }
}