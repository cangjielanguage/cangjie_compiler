// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/CollectLocalConstDecl/CollectLocalConstDecl.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/Utils/CastingTemplate.h"

using namespace Cangjie;
using namespace CHIR;

namespace {
bool ShouldSkipSelf(const AST::FuncDecl& func,
    const std::vector<const AST::FuncDecl*>& localConstFuncs, const int64_t& funcStack)
{
    /** don't visit param default func of global func, because:
     *  1. if visited, its `funcStack` is 1, it will be collected to `localConstFuncs` by mistake
     *  2. it must be in `globalAndMemberFuncs`, and will be visited later
     *
     *  func foo(a !: Bool = true) {}
     *  while visiting `a`, foo's param, its `desugerDecl` will be also visited, so `funcStack` count wrong,
     *  we should avoid this case
     */
    if (func.ownerFunc != nullptr && funcStack == 1) {
        return true;
    }
    /** if its generic decl isn't lifted, itself shouldn't be lifted, either
     *  func foo<T1>(a: T1) {
     *      func goo<T2>() {
     *          let x = a
     *      }
     *      goo<Bool>()
     *  }
     *  func `goo` is instantiated, but its instantiated decl isn't in func foo's body,
     *  so we can't skip visiting goo_Bool by skipping visiting foo's body, but goo's generic decl is in foo's body
     *  so it works by checking its generic decl whether be lifted
     *  in fact, it's expected that goo's instantiated decl can be in foo's body
     */
    if (func.TestAttr(AST::Attribute::GENERIC_INSTANTIATED)) {
        auto gDecl = StaticCast<AST::FuncDecl*>(func.genericDecl);
        if (std::find(localConstFuncs.begin(), localConstFuncs.end(), gDecl) == localConstFuncs.end()) {
            return true;
        }
    }
    return false;
}

bool ShouldSkipChildren(const AST::FuncDecl& func)
{
    /** don't visit member function, because `this` can be used in local const var or func
     *  class CA {
     *      const init() { const p = this }
     *  }
    */
    if (func.outerDecl != nullptr && Is<AST::InheritableDecl>(func.outerDecl)) {
        return true;
    }
    /** don't visit generic function, because generic type can be used in local func
     *  func foo<T>(a: T) {
     *      const func goo() {
     *          let x = a
     *      }
     *  }
     */
    if (func.TestAttr(AST::Attribute::GENERIC)) {
        return true;
    }
    /** the same reason with generic function
     *  class CA<T> {
     *      var a: T
     *      func foo() {
     *          const func goo() {
     *              let x = a
     *          }
     *      }
     *  }
     */
    if (func.outerDecl != nullptr && func.outerDecl->TestAttr(AST::Attribute::GENERIC)) {
        return true;
    }
    /** don't visit local generic instantiated function, because local var's mangled name may be duplicated
     *  func foo1() {
     *      const func goo<T>() {
     *          const a = 1
     *      }
     *      goo<Bool>()
     *  }
     *  func foo2() {
     *      const func goo<T>() {
     *          const a = 1
     *      }
     *      goo<Bool>()
     *  }
     *  in AST, foo1::goo and foo2::goo will be instantiated and stored in package.genericInstantiatedDecls,
     *  not in foo1 or foo2's func body, that means foo1::goo and foo2::goo are regarded as global function,
     *  so mangled names of local var `a` in them are same. This is a design bug in AST, and expected to be fixed
     */
    if (func.TestAttr(AST::Attribute::GENERIC_INSTANTIATED) && IsLocalFunc(func)) {
        return true;
    }
    return false;
}
}

void CollectLocalConstDecl::Collect(const std::vector<Ptr<const AST::Decl>>& decls, bool rootIsGlobalDecl)
{
    /** collect local const func and var decls, except for some special decls
     *  these special decls are listed in `ShouldSkipSelf` and `ShouldSkipChildren`
     */
    int64_t funcStack = 0;
    auto preVisit = [this, &funcStack](Ptr<const AST::Node> n) -> AST::VisitAction {
        if (auto func = DynamicCast<const AST::FuncDecl*>(n); func &&
            ShouldSkipSelf(*func, localConstFuncDecls, funcStack)) {
            funcStack++;
            return AST::VisitAction::SKIP_CHILDREN;
        }

        if (funcStack != 0) {
            if (auto var = DynamicCast<const AST::VarDecl*>(n); var && var->IsConst()) {
                localConstVarDecls.emplace_back(var);
            } else if (auto func = DynamicCast<const AST::FuncDecl*>(n); func && func->IsConst()) {
                localConstFuncDecls.emplace_back(func);
                CJC_ASSERT(func->funcBody);
                /** usually, param's desugar decl can be collected by visiting func body or `globalAndMemberFuncs`,
                 *  but for instantiated func decl, it doesn't work
                 *  func foo() {
                 *      func goo<T>(a !: Bool = true) {}
                 *      goo<Int32>()
                 *  }
                 *  for goo_Int32, a instantiated func decl, it's not in foo's body, we can only collect it by
                 *  visiting AST package's `genericInstantiatedDecls`, but `a_Int32.0`, a desugar instantiated decl
                 *  of goo_Int32's param, it's not in `genericInstantiatedDecls`, so we have to visit
                 *  `param->desugarDecl` to collect this kind of function
                 *  ps: decl in `genericInstantiatedDecls` is unique_ptr, and `param->desugarDecl` is also unique_ptr
                 *      so `param->desugarDecl` can't be moved to `genericInstantiatedDecls`
                 */
                for (auto& param : std::as_const(func->funcBody->paramLists[0]->params)) {
                    if (param->desugarDecl) {
                        localConstFuncDecls.emplace_back(param->desugarDecl.get());
                    }
                }
            }
        }
        if (Is<AST::LambdaExpr>(n) || Is<AST::MacroDecl>(n) || Is<AST::FuncDecl>(n)) {
            funcStack++;
        }
        if (auto func = DynamicCast<const AST::FuncDecl*>(n); func && ShouldSkipChildren(*func)) {
            return AST::VisitAction::SKIP_CHILDREN;
        }
        return AST::VisitAction::WALK_CHILDREN;
    };
    auto postVisit = [&funcStack](Ptr<const AST::Node> n) -> AST::VisitAction {
        if (Is<AST::LambdaExpr>(n) || Is<AST::MacroDecl>(n) || Is<AST::FuncDecl>(n)) {
            funcStack--;
            CJC_ASSERT(funcStack >= 0);
        }
        return AST::VisitAction::WALK_CHILDREN;
    };
    for (auto& it : decls) {
        funcStack = rootIsGlobalDecl ? 0 : 1;
        AST::ConstWalker walker{it.get(), preVisit, postVisit};
        walker.Walk();
    }
}

const std::vector<const AST::VarDecl*>& CollectLocalConstDecl::GetLocalConstVarDecls() const
{
    return localConstVarDecls;
}

const std::vector<const AST::FuncDecl*>& CollectLocalConstDecl::GetLocalConstFuncDecls() const
{
    return localConstFuncDecls;
}
