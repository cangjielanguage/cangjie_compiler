// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/MarkClassHasInited.h"

#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Type/ClassDef.h"

using namespace Cangjie::CHIR;

namespace {
const std::string HAS_INITED_VAR_NAME = "$hasInited";

void AddHasInitedFlagToImportedClass(const Package& package, CHIRBuilder& builder)
{
    for (auto classDef : package.GetImportedClasses()) {
        if (!classDef->GetFinalizer()) {
            continue;
        }
        auto attributeInfo = AttributeInfo();
        attributeInfo.SetAttr(Attribute::NO_REFLECT_INFO, true);
        attributeInfo.SetAttr(Attribute::COMPILER_ADD, true);
        attributeInfo.SetAttr(Attribute::PRIVATE, true);
        auto memberVar = MemberVarInfo {
            .name = HAS_INITED_VAR_NAME,
            .type = builder.GetBoolTy(),
            .attributeInfo = attributeInfo,
            .outerDef = classDef
        };
        classDef->AddInstanceVar(memberVar);
    }
}

void InitHasInitedFlagToFalse(Ptr<Func> initFunc, CHIRBuilder& builder, std::vector<uint64_t> path)
{
    auto boolTy = builder.GetBoolTy();
    auto entry = initFunc->GetEntryBlock();
    auto falseVal = builder.CreateConstantExpression<BoolLiteral>(boolTy, entry, false);
    auto thisArg = initFunc->GetParam(0);
    CJC_NULLPTR_CHECK(thisArg);
    auto storeRef =
        builder.CreateExpression<StoreElementRef>(builder.GetUnitTy(), falseVal->GetResult(), thisArg, path, entry);
    entry->InsertExprIntoHead(*storeRef);
    entry->InsertExprIntoHead(*falseVal);
}

void ReAssignHasInitedToTrue(Ptr<Func> initFunc, CHIRBuilder& builder, std::vector<uint64_t> path)
{
    auto boolTy = builder.GetBoolTy();
    auto thisArg = initFunc->GetParam(0);
    for (auto block : initFunc->GetBody()->GetBlocks()) {
        auto terminator = block->GetTerminator();
        if (!terminator || terminator->GetExprKind() != ExprKind::EXIT) {
            continue;
        }
        auto parent = terminator->GetParentBlock();
        auto terminatorAnnos = terminator->MoveAnnotation();
        terminator->RemoveSelfFromBlock();
        auto trueVal = builder.CreateConstantExpression<BoolLiteral>(boolTy, parent, true);
        auto storeRef =
            builder.CreateExpression<StoreElementRef>(builder.GetUnitTy(), trueVal->GetResult(), thisArg, path, parent);
        auto exit = builder.CreateTerminator<Exit>(parent);
        exit->SetAnnotation(std::move(terminatorAnnos));
        parent->AppendExpressions({trueVal, storeRef, exit});
    }
}

void AddGuardToFinalizer(Ptr<ClassDef> classDef, CHIRBuilder& builder, std::vector<uint64_t> path)
{
    auto finalizer = Cangjie::DynamicCast<Cangjie::CHIR::Func*>(classDef->GetFinalizer());
    if (!finalizer) {
        // While doing incremental compilation, the finalizer may be an ImportedFunc.
        return;
    }
    auto block = builder.CreateBlock(finalizer->GetBody());
    auto thisArg = finalizer->GetParam(0);
    CJC_NULLPTR_CHECK(thisArg);
    auto boolTy = builder.GetBoolTy();
    auto ref = builder.CreateExpression<GetElementRef>(builder.GetType<RefType>(boolTy), thisArg, path, block);
    auto load = builder.CreateExpression<Load>(boolTy, ref->GetResult(), block);

    auto entry = finalizer->GetEntryBlock();
    auto exit = builder.CreateBlock(finalizer->GetBody());
    exit->AppendExpression(builder.CreateTerminator<Exit>(exit));
    auto cond = builder.CreateTerminator<Branch>(load->GetResult(), entry, exit, block);
    block->AppendExpressions({ref, load, cond});
    finalizer->GetBody()->SetEntryBlock(block);
}
} // namespace

void MarkClassHasInited::RunOnPackage(const Package& package, CHIRBuilder& builder)
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

    // Add member variable `hasInited: bool` to all imported classes that have finalizer.
    // As any CHIR-added member won't be exported, we cannot see that the imported class has
    // this member variable. We need to add it by ourself.
    AddHasInitedFlagToImportedClass(package, builder);

    for (auto classDef : package.GetClasses()) {
        if (!classDef->GetFinalizer()) {
            continue;
        }
        auto attributeInfo = AttributeInfo();
        attributeInfo.SetAttr(Attribute::NO_REFLECT_INFO, true);
        attributeInfo.SetAttr(Attribute::COMPILER_ADD, true);
        attributeInfo.SetAttr(Attribute::PRIVATE, true);
        auto memberVar = MemberVarInfo {
            .name = HAS_INITED_VAR_NAME,
            .type = builder.GetBoolTy(),
            .attributeInfo = attributeInfo,
            .outerDef = classDef
        };
        classDef->AddInstanceVar(memberVar);
        auto index = std::vector<uint64_t>{classDef->GetAllInstanceVarNum() - 1};

        for (auto& funcBase : classDef->GetMethods()) {
            if (auto func = DynamicCast<Func*>(funcBase); func && func->IsConstructor()) {
                InitHasInitedFlagToFalse(func, builder, index);
                ReAssignHasInitedToTrue(func, builder, index);
            }
        }

        AddGuardToFinalizer(classDef, builder, index);
    }
}