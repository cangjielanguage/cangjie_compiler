// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Desugar/AfterTypeCheck.h"

#include "TypeCheckUtil.h"

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/AST/ASTCasting.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

namespace {
bool ShouldInsertReturnUnit(const FuncBody& fb)
{
    if (!fb.body) {
        return false; // Invalid node.
    }
    // An empty function body {} has type Unit. We change {} to { return () }.
    if (fb.body->body.empty()) {
        return true;
    }

    // When a function body's last expression is a declaration, insert a return () expression after it.
    if (fb.body->body.back()->IsDecl()) {
        return true;
    }

    // Adding return () for constructors for the sake of backend compatibility, currently.
    if (fb.funcDecl) {
        if (fb.funcDecl->TestAnyAttr(Attribute::PRIMARY_CONSTRUCTOR, Attribute::CONSTRUCTOR)) {
            return true;
        }
    }

    // If a function or lambda's return type is ANNOTATED as Unit, then insert return ().
    // A trick: after type checking, if no error is reported, then e : Unit is a fact. So the check here is not needed.
    if (fb.retType && fb.retType->ty && fb.retType->ty->IsUnit()) {
        Ptr<Node> last = fb.body->body.back().get();
        while (last && last->astKind == ASTKind::PAREN_EXPR) {
            last = StaticAs<ASTKind::PAREN_EXPR>(last)->expr.get();
        }
        // Should not insert unit if the last expression is return or is of type `Unit`,
        // instead we should `MakeLastNodeReturn`.
        if (last == nullptr || last->astKind == ASTKind::RETURN_EXPR ||
            (last->astKind == ASTKind::LIT_CONST_EXPR &&
                StaticAs<ASTKind::LIT_CONST_EXPR>(last)->kind == LitConstKind::UNIT)) {
            return false;
        }
        return true;
    }

    return false;
}

/**
 * Add return e when current function body's last expression isn't 'return e' or
 * current funcbody need to add' return ()'.
 */
void InsertUnitForFuncBody(FuncBody& fb, bool enableCoverage)
{
    if (fb.body == nullptr) {
        return;
    }

    auto ue = CreateUnitExpr(TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT));
    ue->curFile = fb.curFile;
    auto re = CreateReturnExpr(std::move(ue));
    re->curFile = fb.curFile;
    re->ty = TypeManager::GetNothingTy();
    re->refFuncBody = &fb;
    bool notHaveRightCurlPos = fb.funcDecl && fb.body->rightCurlPos.IsZero();
    // If compiled with `--coverage`, the inserted `return ()` should contain the line no of funcDecl,
    // thus, it will not result in redundant coverage reports.
    if (enableCoverage) {
        re->begin = fb.funcDecl ? fb.funcDecl->begin : fb.body->leftCurlPos;
    } else {
        /**
         * If the function has right curly brackets, like:
         *
         * func foo {
         * }  // setting the return expression line info
         *
         * or
         *
         * class A {
         *     init() {
         *     }  // setting the return expression line info
         * }
         *
         * If the function does not have right curly brackets, like the following init function:
         *
         * class A {  // setting the return expression line info
         *     var a = 10
         * }
         * */
        re->begin = notHaveRightCurlPos ? fb.funcDecl->end : fb.body->rightCurlPos;
    }
    fb.body->body.emplace_back(std::move(re));
}

void MakeLastNodeReturn(FuncBody& funcBody)
{
    if (!funcBody.body || funcBody.body->body.empty()) {
        return;
    }
    auto lastNode = funcBody.body->body.rbegin();
    if (auto e = DynamicCast<Expr*>(lastNode->get()); e && e->astKind != ASTKind::RETURN_EXPR) {
        auto lastExpr = OwnedPtr<Expr>(StaticAs<ASTKind::EXPR>(lastNode->release()));
        auto re = CreateReturnExpr(std::move(lastExpr));
        CopyBasicInfo(e, re.get());
        re->ty = TypeManager::GetNothingTy();
        funcBody.body->ty = TypeManager::GetNothingTy();
        re->refFuncBody = &funcBody;
        AddCurFile(*re, funcBody.curFile);
        *lastNode = std::move(re);
    }
}

inline void AddReturnExprForFuncBody(FuncBody& fb, bool enableCoverage)
{
    if (ShouldInsertReturnUnit(fb)) {
        InsertUnitForFuncBody(fb, enableCoverage);
    } else if (Ty::IsTyCorrect(fb.ty)) {
        MakeLastNodeReturn(fb);
    }
}

void InsertStaticInitCall(InheritableDecl& decl, FuncDecl& staticInit)
{
    // Create and insert static initializing as the static member "let $init = static_init()" into typedecl.
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    std::vector<OwnedPtr<FuncArg>> args;
    auto initializer = CreateCallExpr(CreateRefExpr(staticInit), std::move(args), &staticInit, unitTy);
    initializer->begin = decl.begin;
    auto initVar = CreateVarDecl(STATIC_INIT_VAR, std::move(initializer));
    initVar->fullPackageName = decl.fullPackageName;
    initVar->outerDecl = &decl;
    initVar->EnableAttr(Attribute::STATIC, Attribute::PRIVATE);
    initVar->toBeCompiled = staticInit.toBeCompiled;
    // Caller guarantees the 'decl' is class or struct.
    decl.IsClassLikeDecl() ? initVar->EnableAttr(Attribute::IN_CLASSLIKE) : initVar->EnableAttr(Attribute::IN_STRUCT);
    AddCurFile(*initVar, decl.curFile);
    (void)decl.GetMemberDecls().emplace_back(std::move(initVar));
}

/**
 * Adding calling point of user defined static init as static member, eg: "private static let $init = static_init()"
 *    eg: class A<T> {
 *            static var a : String
 *            static init() {  a = "str" }
 *        }
 *        to
 *        class A<T> {
 *            static var a : String
 *            static init() { a = "str" }
 *            private static let $init = static_init()
 *        }
 */
void AddStaticInitForTypeDecl(InheritableDecl& decl)
{
    Ptr<FuncBody> staticInitfb = nullptr;
    for (auto& member : decl.GetMemberDecls()) {
        CJC_ASSERT(member);
        if (!member->TestAttr(Attribute::STATIC)) {
            continue;
        }
        if (member->astKind == ASTKind::FUNC_DECL && member->TestAttr(Attribute::CONSTRUCTOR)) {
            // Valid static constructor must be funcDecl.
            CJC_ASSERT(member->astKind == ASTKind::FUNC_DECL);
            auto fd = StaticAs<ASTKind::FUNC_DECL>(member.get());
            staticInitfb = fd->funcBody.get();
        }
    }
    if (staticInitfb == nullptr) {
        return; // Current type decl does not need to modify static init.
    }
    CJC_ASSERT(staticInitfb->funcDecl);
    // Update name of 'static init', this function cannot be called by user, so user will not perceive changes.
    staticInitfb->funcDecl->identifier = STATIC_INIT_FUNC;
    InsertStaticInitCall(decl, *staticInitfb->funcDecl);
}

inline void CopyNecessaryAnnoForPropDecl(PropDecl& pd)
{
    const bool hasFrozen = pd.HasAnno(AnnotationKind::FROZEN);
    auto clonePropAnno = [&pd, hasFrozen](OwnedPtr<FuncDecl>& fd) {
        auto clonedAnnos = ASTCloner::CloneVector(pd.annotations);
        std::move(clonedAnnos.begin(), clonedAnnos.end(), std::back_inserter(fd->annotations));
        fd->isFrozen = hasFrozen;
    };
    std::for_each(pd.getters.begin(), pd.getters.end(), clonePropAnno);
    std::for_each(pd.setters.begin(), pd.setters.end(), clonePropAnno);
}
} // namespace

namespace Cangjie::Sema::Desugar::AfterTypeCheck {
void DesugarDeclsForPackage(Package& pkg, bool enableCoverage)
{
    Walker(&pkg, [enableCoverage](auto node) {
        CJC_ASSERT(node);
        if (node->astKind == ASTKind::CLASS_DECL || node->astKind == ASTKind::STRUCT_DECL) {
            AddStaticInitForTypeDecl(*StaticCast<InheritableDecl*>(node));
        } else if (node->astKind == ASTKind::FUNC_BODY) {
            AddReturnExprForFuncBody(*StaticAs<ASTKind::FUNC_BODY>(node), enableCoverage);
        } else if (node->astKind == ASTKind::PROP_DECL) {
            CopyNecessaryAnnoForPropDecl(*StaticAs<ASTKind::PROP_DECL>(node));
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}
} // namespace Cangjie::Sema::Desugar::AfterTypeCheck
