// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <functional>

#include "gtest/gtest.h"

#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/Sema/TypeManager.h"

using namespace Cangjie;
using namespace AST;

OwnedPtr<GenericParamDecl> MakeGenericParamDecl(std::string_view name)
{
    auto decl = MakeOwned<GenericParamDecl>();
    decl->identifier = name;
    return decl;
}

TEST(ModalTyTest, CompTyByNames)
{
    TypeManager tm;
    auto t = MakeGenericParamDecl("T");
    auto u = MakeGenericParamDecl("U");
    auto tty = tm.GetGenericsTy(*t);
    auto uty = tm.GetGenericsTy(*u);
    auto tple = tm.GetTupleTy({tty, uty});

    auto ed = MakeOwned<EnumDecl>();
    ed->identifier = "Option";
    ed->generic = MakeOwned<Generic>();
    ed->generic->typeParameters.push_back(MakeGenericParamDecl("TE"));
    auto ety = tm.GetEnumTy(*ed, {ed->generic->typeParameters[0]->DataTy()});
    ety->decl = ed.get();
    ed->SetTy(DataTy{ety});

    auto opt1 = tm.GetEnumTy(*ed, {tple});
    auto opt2 = tm.GetEnumTy(*ed, {tple});
    auto opt3 = EnumTy{ed->identifier, *ed, {tple}};
    EXPECT_EQ(opt1, opt2);
    ModalTy opm1{opt1};
    ModalTy opm2{&opt3};
    EXPECT_EQ(opm1.String(), opm2.String());
    std::hash<ModalTy> hash;
    EXPECT_EQ(hash(opm1), hash(opm2));
    // false because they are equal
    EXPECT_FALSE(CompTyByNames(&opt3, opt1));
    EXPECT_FALSE(CompTyByNamesModal(ModalTy{opt1}, ModalTy{&opt3}));

    auto id = MakeOwned<InterfaceDecl>();
    id->identifier = "Collection";
    id->generic = MakeOwned<Generic>();
    id->generic->typeParameters.push_back(MakeGenericParamDecl("TI"));
    auto ity = tm.GetInterfaceTy(*id, {ed->generic->typeParameters[0]->DataTy()});
    ity->decl = id.get();
    id->SetTy(DataTy{ity});
    auto collection1 = tm.GetInterfaceTy(*id, {tple});
    auto collection2 = tm.GetInterfaceTy(*id, {tple});
    EXPECT_EQ(collection1, collection2);
    EXPECT_EQ(CompTyByNames(collection1, collection2), 0);

    auto fun1 = tm.GetFunctionTy({{tple}}, {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
    auto fun2 = tm.GetFunctionTy({{tple}}, {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
    EXPECT_EQ(fun1, fun2);
    auto fun3 = FuncTy({{tple}}, {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
    auto funm1 = ModalTy{fun1};
    auto funm2 = ModalTy{&fun3};
    EXPECT_EQ(funm1.String(), funm2.String());
    EXPECT_TRUE(CompTyByNames(fun1, &fun3));
    EXPECT_TRUE(CompTyByNamesModal(funm1, funm2));
    EXPECT_EQ(hash(funm1), hash(funm2));
}