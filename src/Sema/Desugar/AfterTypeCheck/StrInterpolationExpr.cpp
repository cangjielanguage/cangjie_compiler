// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/ModalInfo.h"
#include "cangjie/Sema/TypeManager.h"

using namespace Cangjie;
using namespace AST;

namespace {
std::vector<OwnedPtr<FuncArg>> EstimatingLengthOfString(const StrInterpolationExpr& sie)
{
    size_t size = 0;
    for (auto& expr : sie.strPartExprs) {
        if (auto ie = DynamicCast<InterpolationExpr>(expr.get())) {
            if (ie->block->GetTy()->IsPrimitive()) {
                // 10 is maximum number of digits for Int64. This is an estimate.
                size += 10;
            } else {
                // 32 is default characters length of toString return. This is an estimate.
                size += 32;
            }
        } else if (auto lce = DynamicCast<LitConstExpr>(expr.get())) {
            size += lce->stringValue.size();
        }
    }
    auto int64Ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);
    auto ctorArg = CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(size), {int64Ty});
    CopyBasicInfo(&sie, ctorArg.get());
    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(ctorArg)));
    return args;
}

Ptr<Decl> MatchAppendFuncByParamTy(TypeManager& t, std::vector<Ptr<Decl>> candidates, ModalTy paramTy)
{
    std::vector<Ptr<Decl>> matched;
    for (auto& can : candidates) {
        auto fun = StaticCast<FuncDecl>(can);
        auto funcTy = DynamicCast<FuncTy>(fun->DataTy());
        if (t.ImplementsCopyInterface(paramTy.Ty())) {
            paramTy = paramTy.With({});
        }
        if (funcTy && funcTy->paramTys.size() == 1 && funcTy->paramTys[0].Ty() == paramTy.Ty()) {
            if (paramTy.IsDataType() ? paramTy == funcTy->paramTys[0] :
                paramTy.Mode().IsSubModal(funcTy->paramTys[0].Mode())) {
                return can;
            }
        }
    }
    return {};
}
} // namespace

std::vector<Ptr<Decl>> TypeChecker::TypeCheckerImpl::MatchToStringImpl(
    const ASTContext& ctx, const File& file, ModalTy ty)
{
    std::vector<Ptr<Decl>> found;
    if (ty->IsBuiltin()) {
        found = ExtendFieldLookup(ctx, file, ty.Ty(), "toString");
    } else {
        auto decl = Ty::GetDeclPtrOfTy(ty);
        found = FieldLookup(ctx, decl, "toString", {.file = &file});
    }
    for (auto it = found.begin(); it != found.end();) {
        auto stringDecl = importManager.GetCoreDecl<InheritableDecl>(STD_LIB_STRING);
        CJC_NULLPTR_CHECK(stringDecl);
        auto funcTy = StaticCast<FuncTy>((*it)->DataTy());
        if (!funcTy->paramTys.empty() || funcTy->retTy.Ty() != stringDecl->DataTy()) {
            // toString function have no parameter and return type is Struct-String.
            it = found.erase(it);
        } else {
            ++it;
        }
    }
    return found;
}

static void FilterToStringImpl(TypeManager& typeManager, std::vector<Ptr<Decl>>& candidates, ModalInfo objMode)
{
    for (auto it = candidates.begin(); it != candidates.end();) {
        auto funThisMode = typeManager.GetThisParamTy(StaticCast<FuncDecl>(**it)).Mode();
        auto modeMatch = objMode.local == Mode::NOT ? objMode == funThisMode : objMode.IsSubModal(funThisMode);
        // toString has the following signatures
        // 1) () -> String
        // 2) (this @local?) -> String @local!
        // if object mode is ~local, then the not local fun is better; otherwise use submode to let #2 matches local!
        if (!modeMatch) {
            it = candidates.erase(it);
        } else {
            ++it;
        }
    }
}

OwnedPtr<CallExpr> TypeChecker::TypeCheckerImpl::DesugarStrPartExpr(
    const ASTContext& ctx, Expr& expr, const std::vector<Ptr<Decl>> appendDecls, VarDecl& sbItem)
{
    // Create append's argument, must be a primitive type or a subclass of ToString.
    std::vector<OwnedPtr<FuncArg>> appendArgs;
    Ptr<Decl> appendDecl = nullptr;
    CJC_NULLPTR_CHECK(expr.curFile);
    if (auto ie = DynamicCast<InterpolationExpr>(&expr)) {
        // For interpolation part.
        CJC_ASSERT(ie->block && ie->block->GetTy().IsCorrect());
        // Find an 'append' function that matches ie->block->GetTy().
        auto matchedAppend = MatchAppendFuncByParamTy(typeManager, appendDecls, ie->block->GetTy());
        if (matchedAppend) {
            // CreateFuncArg wraps the block in a '$dummy' RefExpr and copies the block's position
            // onto it, so no manual position fix-up is needed here.
            appendArgs.emplace_back(CreateFuncArg(std::move(ie->block)));
            // Select append of the corresponding parameter type version: $tmp.append({...}).
            appendDecl = matchedAppend;
        } else {
            // Find toString function target, if can't find by interpolation expr type, use the version in the ToString
            // interface. Only one candidate should be eligible.
            auto found = MatchToStringImpl(ctx, *expr.curFile, ie->block->GetTy());
            if (found.empty()) {
                auto toStringInterface = importManager.GetCoreDecl<InheritableDecl>(TOSTRING_NAME);
                found = FieldLookup(ctx, toStringInterface, "toString");
            }
            // Two toString candidates are expected before filtering: the default version and one overload.
            constexpr size_t TOSTRING_CANDIDATES_BEFORE_FILTER = 2;
            CJC_ASSERT(found.size() == TOSTRING_CANDIDATES_BEFORE_FILTER);
            FilterToStringImpl(typeManager, found, ie->block->TyMode());
            CJC_ASSERT(found.size() == 1);
            auto toStringFunc = CreateMemberAccess(std::move(ie->block), *found[0]);
            CopyBasicInfo(toStringFunc->baseExpr.get(), toStringFunc.get());
            auto toStringCall = CreateCallExpr(std::move(toStringFunc), {});
            auto funcTy = StaticCast<FuncTy*>(found[0]->DataTy());
            toStringCall->SetTy(funcTy->retTy);
            toStringCall->resolvedFunction = DynamicCast<FuncDecl*>(found[0]);
            appendArgs.emplace_back(CreateFuncArg(std::move(toStringCall)));
            // Select append of with Struct-String type version, and add toString call: $tmp.append({...}.toString()).
            appendDecl = MatchAppendFuncByParamTy(typeManager, appendDecls, funcTy->retTy);
        }
    } else if (auto lce = DynamicCast<LitConstExpr>(&expr)) {
        // There is always an extra empty string at the end of the interpolation string, and there is no need to
        // generate an empty string.
        if (lce->rawString.empty()) {
            return nullptr;
        }
        // For String Literal.
        appendDecl = MatchAppendFuncByParamTy(typeManager, appendDecls, expr.GetTy());
        appendArgs.emplace_back(CreateFuncArg(ASTCloner::Clone(Ptr(&expr))));
    }
    CJC_ASSERT(appendDecl);
    auto appendFunc = CreateMemberAccess(CreateRefExpr(sbItem), *appendDecl);
    CopyBasicInfo(&expr, appendFunc->baseExpr.get());
    CopyBasicInfo(&expr, appendFunc.get());
    // sb.append() return type is unit.
    auto appendCall = CreateCallExpr(
        std::move(appendFunc), std::move(appendArgs), nullptr, {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
    appendCall->resolvedFunction = StaticCast<FuncDecl>(appendDecl);
    return appendCall;
}

/**
 * Desugar InterpolationExpr to Stringbuilder
 * *************** before desugar ****************
 * var count: Int64 = 10
 * var obj: ToStringInstance = ToStringInstance()
 * let interps = "There are ${count * count} ${obj}."
 * *************** after desugar ****************
 * let interps = {
 *     var $tmpN = StringBuilder()
 *     $tmpN.append("There are ")
 *     $tmpN.append(count * count)
 *     $tmpN.append(" ")
 *     $tmpN.append(obj.toString())
 *     $tmpN.append(".")
 *     $tmpN.toString()
 * }
 * */
void TypeChecker::TypeCheckerImpl::DesugarStrInterpolationExpr(ASTContext& ctx, LitConstExpr& litConstExpr)
{
    CJC_NULLPTR_CHECK(litConstExpr.curFile);
    auto siexpr = litConstExpr.siExpr.get();
    auto blk = MakeOwned<Block>();
    CopyBasicInfo(siexpr, blk.get());
    blk->EnableAttr(Attribute::COMPILER_ADD);
    blk->SetTy(siexpr->GetTy());
    // Create StringBuilder constructor call.
    auto sbDecl = importManager.GetCoreDecl<InheritableDecl>("StringBuilder");
    CJC_NULLPTR_CHECK(sbDecl);
    // Roughly estimate the length of the interpolated string, which is used to initialize the StringBuilder capacity.
    auto sbCtor = CreateRefExpr(*sbDecl);
    CopyBasicInfo(siexpr, sbCtor.get());
    auto sbCtorCall = CreateCallExpr(std::move(sbCtor), EstimatingLengthOfString(*siexpr));
    sbCtorCall->baseFunc->EnableAttr(Attribute::IMPLICIT_ADD);
    auto tmpVar = CreateTmpVarDecl(nullptr, sbCtorCall.get());
    CopyBasicInfo(siexpr, tmpVar.get());
    blk->body.emplace_back(std::move(tmpVar));
    // Get '$tmpN' var declaration, and synthesize it's ty by initializer expression.
    auto sbItem = StaticCast<VarDecl*>(blk->body.front().get());
    CopyNodeScopeInfo(siexpr, sbItem);
    // Local variable names do not require mangling.
    sbItem->EnableAttr(Attribute::NO_MANGLE);
    sbItem->SetTy(SynthesizeWithoutRecover({ctx, SynPos::EXPR_ARG}, sbItem->initializer.get()));
    // Get all StringBuilder append candidate functions.
    LookupInfo info{.file = litConstExpr.curFile, .lookupExtend = false};
    std::vector<Ptr<Decl>> appendDecls = FieldLookup(ctx, sbDecl, "append", info);
    // Generate append call for every string part expr.
    for (auto& expr : siexpr->strPartExprs) {
        // Create '$tmpN.append' call and insert into block.
        auto appendCall = DesugarStrPartExpr(ctx, *expr, appendDecls, *sbItem);
        if (appendCall == nullptr) {
            continue;
        }
        blk->body.emplace_back(std::move(appendCall));
    }
    auto field = MatchToStringImpl(ctx, *litConstExpr.curFile, sbDecl->GetTy());
    FilterToStringImpl(typeManager, field, {});
    CJC_ASSERT(field.size() == 1);
    auto toStringFunc = CreateMemberAccess(CreateRefExpr(*sbItem), *field[0]);
    CopyBasicInfo(sbCtorCall.get(), toStringFunc->baseExpr.get());
    CopyBasicInfo(sbCtorCall.get(), toStringFunc.get());
    auto toStringCall = CreateCallExpr(std::move(toStringFunc), {});
    toStringCall->SetTy(blk->GetTy()); // toStringCall ty is Struct-String.
    toStringCall->resolvedFunction = DynamicCast<FuncDecl*>(field[0]);
    CopyBasicInfo(sbCtorCall.get(), toStringCall.get());
    blk->body.emplace_back(std::move(toStringCall));
    AddCurFile(*blk, litConstExpr.curFile);
    litConstExpr.desugarExpr = std::move(blk);
}
