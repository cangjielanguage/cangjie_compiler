// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements desugaring of Objective-C mirror subtypes.
 */

#include "NativeFFI/ObjC/Utils/Common.h"
#include "Handlers.h"
#include "cangjie/AST/Match.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void GenerateWrappers::HandleImpl(InteropContext& ctx)
{
    auto genWrapper = [this, &ctx](Decl& decl) {
        if (decl.TestAttr(Attribute::IS_BROKEN)) {
            return;
        }

        std::vector<Ptr<Decl>> results;
        if (auto ed = DynamicCast<const EnumDecl*>(&decl); ed) {
            for (auto& mem : ed->members) {
                results.push_back(mem.get());
            }
        } else {
            results = decl.GetMemberDeclPtrs();
        }
        for (auto& memberDecl : results) {
            if (memberDecl->TestAnyAttr(Attribute::IS_BROKEN, Attribute::CONSTRUCTOR)) {
                continue;
            }
            if (interopType == InteropType::ObjC_Mirror && !memberDecl->TestAnyAttr(Attribute::PUBLIC)) {
                continue;
            }

            if (ctx.factory.IsGeneratedMember(*memberDecl)) {
                continue;
            }

            if (interopType == InteropType::CJ_Mapping && !ctx.typeMapper.IsObjCCJMappingMember(*memberDecl)) {
                continue;
            }

            switch (memberDecl->astKind) {
                case ASTKind::FUNC_DECL:
                    this->GenerateWrapper(ctx, *StaticAs<ASTKind::FUNC_DECL>(memberDecl));
                    break;
                case ASTKind::PROP_DECL:
                    this->GenerateWrapper(ctx, *StaticAs<ASTKind::PROP_DECL>(memberDecl));
                    break;
                case ASTKind::VAR_DECL:
                    this->GenerateWrapper(ctx, *StaticAs<ASTKind::VAR_DECL>(memberDecl));
                    break;
                default:
                    break;
            }
        }
    };

    if (interopType == InteropType::ObjC_Mirror) {
        for (auto& impl : ctx.impls) {
            genWrapper(*impl);
        }
    } else if (interopType == InteropType::CJ_Mapping) {
        for (auto& cjmapping : ctx.cjMappings) {
            genWrapper(*cjmapping);
        }
    }
}

void GenerateWrappers::GenerateWrapper(InteropContext& ctx, FuncDecl& method)
{
    auto wrapper = ctx.factory.CreateMethodWrapper(method);
    CJC_NULLPTR_CHECK(wrapper);
    ctx.genDecls.emplace_back(std::move(wrapper));
}

void GenerateWrappers::GenerateWrapper(InteropContext& ctx, PropDecl& prop)
{
    auto wrapper = ctx.factory.CreateGetterWrapper(prop);
    CJC_NULLPTR_CHECK(wrapper);
    ctx.genDecls.emplace_back(std::move(wrapper));

    if (prop.isVar) {
        GenerateSetterWrapper(ctx, prop);
    }
}

void GenerateWrappers::GenerateSetterWrapper(InteropContext& ctx, PropDecl& prop)
{
    if (!SkipSetterForValueTypeDecl(*prop.outerDecl.get())) {
        auto wrapper = ctx.factory.CreateSetterWrapper(prop);
        CJC_NULLPTR_CHECK(wrapper);
        ctx.genDecls.emplace_back(std::move(wrapper));
    }
}

void GenerateWrappers::GenerateWrapper(InteropContext& ctx, VarDecl& field)
{
    if (ctx.factory.IsGeneratedNativeHandleField(field)) {
        return;
    }

    auto wrapper = ctx.factory.CreateGetterWrapper(field);
    CJC_NULLPTR_CHECK(wrapper);
    ctx.genDecls.emplace_back(std::move(wrapper));

    if (field.isVar) {
        GenerateSetterWrapper(ctx, field);
    }
}

void GenerateWrappers::GenerateSetterWrapper(InteropContext& ctx, VarDecl& field)
{
    if (!SkipSetterForValueTypeDecl(*field.outerDecl.get())) {
        auto wrapper = ctx.factory.CreateSetterWrapper(field);
        CJC_NULLPTR_CHECK(wrapper);
        ctx.genDecls.emplace_back(std::move(wrapper));
    }
}

bool GenerateWrappers::SkipSetterForValueTypeDecl(Decl& decl) const
{
    return interopType == InteropType::CJ_Mapping && DynamicCast<StructTy*>(decl.ty.get()) != nullptr;
}
