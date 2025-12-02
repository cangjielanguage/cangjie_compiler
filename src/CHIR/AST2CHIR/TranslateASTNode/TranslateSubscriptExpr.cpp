// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/IR/IntrinsicKind.h"
#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

// Let's take an example to analyse.
// Given a two-dimensional tuple named `arr`,
// we first declare a tuple that contains the tuple, like:
//      var tuple = ( true, arr )
// If we would like to access an element in `arr`, like:
//      tuple[1][4][5]
//      --------------
//                   |
//                    --> It is a `SubscriptExpr`, of which the `baseExpr`
//                        is `tuple[1][4]`, `indexExpr` values `5`
// Then as for tuple[1][4],
//             -----------
//                       |
//                        ---> It is a `SubscriptExpr`, of which the `baseExpr`
//                             is `tuple[1]`, `indexExpr` values `4`
// With regard to tuple[1], although it is a `SubscriptExpr`, it is not a subscript
// access to a tuple, thus we have reached the outermost tuple node, aka tuple[1].
// During the process, we can also collect the all indexes to access the
// place of tuple element.
// varray has the same process except that the index has no literal limit.
Ptr<Value> Translator::Visit(const AST::SubscriptExpr& subscriptExpr)
{
    // Currently it can only be an access to a tuple or a varray,
    // all the other `ob[idx]` will be desugared to a CallExpr.
    CJC_ASSERT(subscriptExpr.indexExprs.size() == 1);

    if (subscriptExpr.isTupleAccess) {
        return TranslateTupleAccess(subscriptExpr);
    }

    if (subscriptExpr.IsVArrayAccess()) {
        return TranslateVArrayAccess(subscriptExpr);
    }
    CJC_ASSERT(false && "Certainly won't get here in translating subscriptExpr.");
    return nullptr;
}

Ptr<Value> Translator::TranslateTupleAccess(const AST::SubscriptExpr& subscriptExpr)
{
    const auto& loc = TranslateLocation(subscriptExpr);
    auto se = &subscriptExpr;
    std::list<uint64_t> indexs;
    AST::Expr* baseExpr = nullptr;
    for (; se != nullptr && se->isTupleAccess; se = DynamicCast<AST::SubscriptExpr*>(se->baseExpr.get())) {
        baseExpr = se->baseExpr.get();
        indexs.emplace_front(se->indexExprs[0]->constNumValue.asInt.Uint64());
    }
    CJC_NULLPTR_CHECK(baseExpr);
    auto base = TranslateExprArg(*baseExpr);
    if (base->GetType()->IsRef()) {
        base = CreateAndAppendExpression<Load>(
            loc, StaticCast<RefType*>(base->GetType())->GetBaseType(), base, currentBlock)->GetResult();
    }
    auto res = CreateAndAppendExpression<Field>(loc, chirTy.TranslateType(*subscriptExpr.ty), base,
        std::vector<uint64_t>(indexs.cbegin(), indexs.cend()), currentBlock);
    /*
        If the SubscriptExpr is added by compiler, DCE will skip it.
        for example code:
        let a:Int64
        let b:Int64
        (a, b, _) = (1, 2, 3)   -------->      var tmp = (1, 2, 3); a = tmp[0]; b =tmp[1]; _= tmp[2]

     */
    if (subscriptExpr.TestAttr(AST::Attribute::IMPLICIT_ADD)) {
        res->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
    }
    return res->GetResult();
}

Ptr<Value> Translator::TranslateVArrayAccess(const AST::SubscriptExpr& subscriptExpr)
{
    const auto& loc = TranslateLocation(subscriptExpr);
    auto se = &subscriptExpr;
    std::vector<Value*> indexs;
    std::vector<AST::Expr*> indexExprs;
    AST::Expr* baseExpr = nullptr;
    for (; se != nullptr && se->IsVArrayAccess(); se = DynamicCast<AST::SubscriptExpr*>(se->baseExpr.get())) {
        baseExpr = se->baseExpr.get();
        indexExprs.push_back(se->indexExprs[0].get());
    }
    CJC_NULLPTR_CHECK(baseExpr);
    // make sure that the more left `index` is, the earlier to be calculated.
    // i.e  a[foo1()][foo2()]
    // foo1 is called earlier than foo2.
    //
    // additionally, in this example, it's a SubscriptExpr which consists of a baseExpr `a[foo1()]`
    // and an index `foo2()`, and then the baseExpr is also a SubscriptExpr,
    // which consists of a baseExpr `a` and an index `foo1()`.
    for (auto it = indexExprs.crbegin(); it != indexExprs.crend(); ++it) {
        auto index = TranslateExprArg(**it);
        if (index->GetType()->IsRef()) {
            index = CreateAndAppendExpression<Load>(
                StaticCast<RefType*>(index->GetType())->GetBaseType(), index, currentBlock)->GetResult();
        }
        indexs.push_back(index);
    }
    auto base = TranslateExprArg(*baseExpr);
    indexs.insert(indexs.begin(), base);
    auto callContext = IntrisicCallContext {
        .kind = IntrinsicKind::VARRAY_GET,
        .args = indexs
    };
    return CreateAndAppendExpression<Intrinsic>(
        loc, chirTy.TranslateType(*subscriptExpr.ty), callContext, currentBlock)->GetResult();
}
