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
#include "TypeCheckUtil.h"

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

TEST(ModalTypeManager, ReplaceThisInCompositeTypesPreservesModality)
{
    TypeManager manager;
    auto declaration = MakeOwned<ClassDecl>();
    declaration->identifier = "C";
    auto self = manager.GetClassThisTy(*declaration, {});
    auto concrete = manager.GetClassTy(*declaration, {});
    std::vector<DataTy> composites{manager.GetArrayTy(self, 2), manager.GetPointerTy(self)};
    auto structure = MakeOwned<StructDecl>();
    structure->identifier = "S";
    composites.push_back(manager.GetStructTy(*structure, {self}));
    auto interface = MakeOwned<InterfaceDecl>();
    interface->identifier = "I";
    composites.push_back(manager.GetInterfaceTy(*interface, {self}));
    auto enumeration = MakeOwned<EnumDecl>();
    enumeration->identifier = "E";
    composites.push_back(manager.GetEnumTy(*enumeration, {self}));
    composites.push_back(manager.GetClassTy(*declaration, {self}));
    for (auto composite : composites) {
        auto replaced = manager.ReplaceThisTy({composite, ModalInfo(Mode::HALF)});
        EXPECT_EQ(replaced.Mode().local, Mode::HALF);
        ASSERT_EQ(replaced->typeArgs.size(), 1u);
        EXPECT_EQ(replaced->typeArgs[0].Ty(), concrete);
        EXPECT_EQ(replaced.Kind(), composite->kind);
    }
    auto function = manager.GetFunctionTy({{self, ModalInfo(Mode::FULL)}}, {self, ModalInfo(Mode::HALF)});
    auto replaced = manager.ReplaceThisTy({function, ModalInfo(Mode::HALF)});
    auto functionType = static_cast<FuncTy*>(replaced.get());
    EXPECT_EQ(functionType->paramTys[0].Ty(), concrete);
    EXPECT_EQ(functionType->paramTys[0].Mode().local, Mode::FULL);
    EXPECT_EQ(functionType->retTy.Ty(), concrete);
    EXPECT_EQ(functionType->retTy.Mode().local, Mode::HALF);
    EXPECT_EQ(replaced.Mode().local, Mode::HALF);
}

TEST(ModalTypeManager, InvalidTypeNormalizationRetainsOuterModal)
{
    TypeManager manager;
    ModalTy invalid{TypeManager::GetInvalidTy(), ModalInfo(Mode::HALF)};
    auto result = manager.GetThisRealTy(invalid);
    EXPECT_EQ(result.Kind(), TypeKind::TYPE_INVALID);
    EXPECT_EQ(result.Mode().local, Mode::HALF);
}

TEST(ModalTypeManager, BlockResultUsesLoweredExpressionType)
{
    TypeManager manager;
    Block block;
    auto boolean = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    auto integer = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);
    block.SetTy({boolean});
    auto expression = MakeOwned<LitConstExpr>(LitConstKind::BOOL, "true");
    expression->SetTy({boolean});
    expression->desugarExpr = MakeOwned<LitConstExpr>(LitConstKind::INTEGER, "1");
    expression->desugarExpr->SetTy({integer});
    block.body.push_back(std::move(expression));
    EXPECT_EQ(manager.GetBlockRealTy(block), integer);
    block.desugarExpr = MakeOwned<LitConstExpr>(LitConstKind::BOOL, "false");
    block.desugarExpr->SetTy({boolean});
    EXPECT_EQ(manager.GetBlockRealTy(block), boolean);
    PropDecl property;
    property.SetTy({integer, ModalInfo(Mode::HALF)});
    EXPECT_EQ(TypeManager::GetThisParamMode(property).local, Mode::HALF);
    FuncDecl incomplete;
    EXPECT_FALSE(TypeManager::HasThisParam(incomplete));
}

TEST(ModalTypeManager, FunctionSignaturesDistinguishReceiverAndReturnPolicy)
{
    using namespace TypeCheckUtil;
    auto integer = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);
    auto boolean = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    FuncSig plain{"f", std::nullopt, {{integer}}, {boolean}, false};
    FuncSig local{"f", ModalInfo(Mode::FULL), {{integer}}, {boolean}, false};
    FuncSig renamed{"g", std::nullopt, {{integer}}, {boolean}, false};
    FuncSig withReturn{"f", std::nullopt, {{integer}}, {boolean}, true};
    FuncSigCmp compare;
    EXPECT_TRUE(compare(plain, local));
    EXPECT_FALSE(compare(local, plain));
    EXPECT_TRUE(compare(plain, renamed));
    EXPECT_FALSE(compare(renamed, plain));
    EXPECT_TRUE(compare(plain, withReturn));
    EXPECT_FALSE(compare(withReturn, plain));
    EXPECT_FALSE(compare(plain, plain));
    std::map<FuncSig, int, FuncSigCmp> overloads;
    overloads.emplace(plain, 1);
    overloads.emplace(local, 2);
    EXPECT_EQ(overloads.size(), 2u);
}

TEST(ModalTypeManager, GeneratedTypeNameIntrinsicHasConsistentSignature)
{
    TypeManager manager;
    Package package;
    package.fullPackageName = "test";
    package.files.push_back(MakeOwned<File>());
    auto intrinsic = TypeCheckUtil::GenerateGetTypeForTypeParamIntrinsic(package, manager);
    ASSERT_NE(intrinsic, nullptr);
    EXPECT_TRUE(intrinsic->TestAttr(Attribute::INTRINSIC));
    EXPECT_TRUE(intrinsic->TestAttr(Attribute::GENERIC));
    ASSERT_NE(intrinsic->funcBody, nullptr);
    EXPECT_EQ(intrinsic->funcBody->retType->DataTy(), manager.GetCStringTy());
    EXPECT_EQ(intrinsic->funcBody->GetTy(), intrinsic->GetTy());
    ASSERT_NE(intrinsic->funcBody->generic, nullptr);
    EXPECT_EQ(intrinsic->funcBody->generic->typeParameters.size(), 1u);
}

TEST(ModalTypeManager, TypeBoundsDiagnosticIncludesAllConstraintKinds)
{
    TyVarBounds bounds;
    ModalTy integer{TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64)};
    bounds.lbs.insert(integer);
    bounds.ubs.insert(integer);
    bounds.sum.insert(integer);
    bounds.eq.insert(integer);
    auto text = bounds.ToString();
    for (const auto& label : {"Lower bounds: {Int64, }", "Upper bounds: {Int64, }", "Sum: {Int64, }", "Equals: {Int64, }"}) {
        EXPECT_NE(text.find(label), std::string::npos);
    }
}

TEST(ModalTypeManager, FunctionBodyLookupHandlesMacroAndPrimaryConstructor)
{
    MacroDecl macro;
    macro.desugarDecl = MakeOwned<FuncDecl>();
    macro.desugarDecl->funcBody = MakeOwned<FuncBody>();
    EXPECT_EQ(TypeCheckUtil::GetFuncBody(macro), macro.desugarDecl->funcBody.get());
    PrimaryCtorDecl constructor;
    constructor.funcBody = MakeOwned<FuncBody>();
    EXPECT_EQ(TypeCheckUtil::GetFuncBody(constructor), constructor.funcBody.get());
    VarDecl variable;
    EXPECT_EQ(TypeCheckUtil::GetFuncBody(variable), nullptr);
}

TEST(ModalTypeManager, MultidimensionalArrayArgumentDropsOneDimension)
{
    TypeManager manager;
    auto integer = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);
    auto array = manager.GetArrayTy(integer, 3);
    auto arguments = manager.GetTypeArgs(*array);
    ASSERT_EQ(arguments.size(), 1u);
    EXPECT_EQ(arguments[0], manager.GetArrayTy(integer, 2));
}

TEST(ModalTypeManager, MissingPropertyAccessorHasNoCandidate)
{
    PropDecl property;
    EXPECT_EQ(TypeCheckUtil::GetUsedMemberDecl(property, true), nullptr);
    EXPECT_EQ(TypeCheckUtil::GetUsedMemberDecl(property, false), nullptr);
}
