// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include "gtest/gtest.h"
#include "cangjie/Parse/Parser.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Utils/ConstantsUtils.h"

using namespace Cangjie;
using namespace AST;
using namespace Meta;

class ParserTest : public testing::Test {
protected:
    void SetUp() override
    {
        diag.SetSourceManager(&sm);
        parser = std::make_unique<Parser>(code, diag, sm);
        file = parser->ParseTopLevel();
    }
    std::string code = R"(
    main(argc:int, argv:string="123") {
    let a:int=40
    let b = 2 ** -a
    print((a+3*b, (a+3) *b))
    }
)";
    std::unique_ptr<Parser> parser;
    OwnedPtr<File> file;
    SourceManager sm;
    DiagnosticEngine diag;
};

TEST(ParserTest1, PositionTest)
{
    std::string code = R"(
    main(argc:Int, argv:String") {
    let a:Int= [1..3];
    let b = 2 ** -a
    print((a+3*b, (a+3) *b))
    var scoreResult: string = match (score) {
        case 10  => "fail"
        case _ =>
        case (q,q) => "12"
    }
    }

    interface A {
        func f() {}
    }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::pair<int, int>> expectPosition;
    expectPosition.push_back(std::make_pair(2, 5));
    expectPosition.push_back(std::make_pair(2, 10));
    expectPosition.push_back(std::make_pair(2, 20));
    expectPosition.push_back(std::make_pair(6, 31));
    expectPosition.push_back(std::make_pair(7, 14));
    expectPosition.push_back(std::make_pair(8, 14));
    expectPosition.push_back(std::make_pair(9, 14));
    expectPosition.push_back(std::make_pair(15, 6));
    std::vector<std::pair<int, int>> position;
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const MainDecl& md) {
                position.push_back(std::make_pair(md.begin.line, md.begin.column));
                return VisitAction::WALK_CHILDREN;
            },
            [&](const FuncParam& fp) {
                position.push_back(std::make_pair(fp.begin.line, fp.begin.column));
                return VisitAction::WALK_CHILDREN;
            },
            [&](const MatchExpr& me) {
                position.push_back(std::make_pair(me.begin.line, me.begin.column));
                return VisitAction::WALK_CHILDREN;
            },
            [&](const ConstPattern& cp) {
                position.push_back(std::make_pair(cp.begin.line, cp.begin.column));
                return VisitAction::WALK_CHILDREN;
            },
            [&](const WildcardPattern& wp) {
                position.push_back(std::make_pair(wp.begin.line, wp.begin.column));
                return VisitAction::WALK_CHILDREN;
            },
            [&](const TuplePattern& tp) {
                position.push_back(std::make_pair(tp.begin.line, tp.begin.column));
                return VisitAction::WALK_CHILDREN;
            },
            [&](const InterfaceBody& ib) {
                position.push_back(std::make_pair(ib.end.line, ib.end.column));
                return VisitAction::SKIP_CHILDREN;
            },
            [&](const Node& node) { return VisitAction::WALK_CHILDREN; }, []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(position.size() == expectPosition.size());
    EXPECT_TRUE(std::equal(position.begin(), position.end(), expectPosition.begin()));
}

TEST_F(ParserTest, Decl)
{
    // shared test
    std::vector<std::string> expectIdentifiers{"main", "argc", "argv", "a", "b"};
    std::vector<std::string> identifiers;
    Walker walkerFirst(file.get(), [&identifiers](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&identifiers](const Decl& decl) {
                identifiers.push_back(decl.identifier);
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walkerFirst.Walk();
    EXPECT_TRUE(std::equal(identifiers.begin(), identifiers.end(), expectIdentifiers.begin()));
    // private test
    std::string classCode = R"(
    class A <: B & C & D {
        func E() {}
        let H = 40
        init() {}
    }
)";
    Parser parser = Parser(classCode, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectMembers{"A", "E", "H", "init"};
    std::vector<std::string> expectRefs{"B", "C", "D"};
    std::vector<std::string> classMembers;
    std::vector<std::string> classRefs;
    Walker walkerClass(file.get(), [&classMembers, &classRefs](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&classMembers](const Decl& decl) {
                classMembers.push_back(decl.identifier);
                return VisitAction::WALK_CHILDREN;
            },
            [&classRefs](const RefType& rt) {
                classRefs.push_back(rt.ref.identifier);
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walkerClass.Walk();
    EXPECT_TRUE(std::equal(classMembers.begin(), classMembers.end(), expectMembers.begin()));
    EXPECT_TRUE(std::equal(classRefs.begin(), classRefs.end(), expectRefs.begin()));
}

TEST_F(ParserTest, InterfaceDecl)
{
    std::string classCode = R"(
    interface I <: C & D{
        func E() {}
        let H = 40
        class A {
        }
        interface F <: E & F{
        }
    }
)";
    std::vector<std::string> expectMembers{"I", "E", "H", "A", "F"};
    std::vector<std::string> expectRefs{"C", "D", "E", "F"};
    std::vector<std::string> members;
    std::vector<std::string> refs;
    Parser parser = Parser(classCode, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();

    Walker walkerClass(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const Decl& decl) {
                members.push_back(decl.identifier);
                return VisitAction::WALK_CHILDREN;
            },
            [&](const RefType& refType) {
                refs.push_back(refType.ref.identifier);
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walkerClass.Walk();
    EXPECT_TRUE(std::equal(members.begin(), members.end(), expectMembers.begin()));
    EXPECT_TRUE(std::equal(refs.begin(), refs.end(), expectRefs.begin()));
}
TEST_F(ParserTest, EnumDecl)
{
    std::string classCode = R"(
    enum TimeUnit { Year
              | Month
              | Day
              | Hour
    }
    enum TimeUnit { Year(Int)
              | Month(Int, Float)
              | Day(Int, Float, Float)
              | Hour(Int, Float, Float, Float)
    }
    main() {
       enum TimeUnit { Year(Int)
                  | Month(Int, Float)
                  | Day(Int, Float, Float)
                  | Hour(Int, Float, Float, Float)
        }
    }
)";
    Parser parser = Parser(classCode, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectLits{
        "Year", "Month", "Day", "Hour", "Year", "Month", "Day", "Hour", "Year", "Month", "Day", "Hour"};
    std::vector<std::string> lits;
    bool varAttr = false;
    Walker walkerFirst(file.get(), [&lits, &varAttr](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&lits, &varAttr](const EnumDecl& decl) {
                for (auto& i : decl.constructors) {
                    lits.push_back(i->identifier);
                    if (Is<VarDecl>(i.get())) {
                        varAttr |= As<ASTKind::VAR_DECL>(i.get())->isVar;
                    }
                }
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walkerFirst.Walk();
    EXPECT_TRUE(std::equal(lits.begin(), lits.end(), expectLits.begin()));
    EXPECT_FALSE(varAttr);
}
TEST_F(ParserTest, Expr)
{
    std::vector<std::string> expectLits{"123", "40", "2", "3", "3"};
    std::vector<std::string> lits;
    Walker walkerFirst(file.get(), [&lits](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&lits](const LitConstExpr& litConstExpr) {
                lits.push_back(litConstExpr.stringValue);
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walkerFirst.Walk();
    EXPECT_TRUE(std::equal(lits.begin(), lits.end(), expectLits.begin()));

    // RefExpr test
    std::vector<std::string> expectRefExprs{"a", "print", "a", "b", "a", "b"};
    std::vector<std::string> refExprs;
    Walker walkerSecond(file.get(), [&refExprs](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&refExprs](const RefExpr& refExpr) {
                refExprs.push_back(refExpr.ref.identifier);
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walkerSecond.Walk();
    EXPECT_TRUE(std::equal(refExprs.begin(), refExprs.end(), expectRefExprs.begin()));

    // BinaryExpr
    std::vector<TokenKind> expectBinaryExprs{
        TokenKind::EXP, TokenKind::ADD, TokenKind::MUL, TokenKind::MUL, TokenKind::ADD};
    std::vector<TokenKind> binaryExprs;
    Walker walkerThird(file.get(), [&binaryExprs](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&binaryExprs](const BinaryExpr& binaryExpr) {
                binaryExprs.push_back(binaryExpr.op);
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walkerThird.Walk();
    EXPECT_TRUE(std::equal(binaryExprs.begin(), binaryExprs.end(), expectBinaryExprs.begin()));

    // UnaryExpr
    std::vector<TokenKind> expectUnaryExprs{TokenKind::SUB};
    std::vector<TokenKind> unaryExprs;
    Walker walkerForth(file.get(), [&unaryExprs](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&unaryExprs](const UnaryExpr& unaryExpr) {
                unaryExprs.push_back(unaryExpr.op);
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walkerForth.Walk();
    EXPECT_TRUE(std::equal(unaryExprs.begin(), unaryExprs.end(), expectUnaryExprs.begin()));
}

TEST_F(ParserTest, Type)
{
    // shared test
    std::vector<std::string> expectRefTypes{"int", "string", "int"};
    std::vector<std::string> refTypes;
    Walker walkerFirst(file.get(), [&refTypes](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&refTypes](const RefType& refType) {
                refTypes.push_back(refType.ref.identifier);
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walkerFirst.Walk();
    EXPECT_TRUE(std::equal(refTypes.begin(), refTypes.end(), expectRefTypes.begin()));
}

TEST_F(ParserTest, InterfaceBody)
{
}

TEST_F(ParserTest, ClassBody)
{
}

TEST(ParserTest1, Package)
{
    std::string code = R"(
        package a.b.b
                 import e.f.*
                 import {g.h as s, j.k}
                 import h.{i as k, f}
                 import m
.
a
                 import x.y.
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();

    std::string expectPackages = "a.b.b";
    std::vector<Position> expectedImportSpecEnd = {
        {0, 3, 30}, {0, 4, 34}, {0, 4, 39}, {0, 5, 34}, {0, 5, 37}, {0, 8, 2}, {0, 9, 29}};
    std::vector<Position> expectedImportedItemNamePos = {
        {0, 3, 29}, {0, 4, 28}, {0, 4, 38}, {0, 5, 28}, {0, 5, 36}, {0, 8, 1}, {0, 9, 29}};
    std::vector<std::string> expectImportPackageName{"e.f", "g", "j", "h", "h", "m", "x.y"};
    std::vector<std::string> expectImportedItemName{"*", "h", "k", "i", "f", "a", INVALID_IDENTIFIER};

    std::string packages;
    std::vector<Position> importSpecEnd;
    std::vector<Position> importedItemNamePos;
    std::vector<std::string> importPackageName;
    std::vector<std::string> importedItemName;
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const PackageSpec& package) {
                packages = package.GetPackageName();
                return VisitAction::WALK_CHILDREN;
            },
            [&](const ImportSpec& import) {
                if (import.IsImportMulti()) {
                    return VisitAction::WALK_CHILDREN;
                }
                auto names = import.content.prefixPaths;
                importPackageName.push_back(Utils::JoinStrings(names, "."));
                importedItemName.push_back(import.content.identifier);

                importSpecEnd.push_back(import.end);

                importedItemNamePos.push_back(import.content.identifier.Begin());

                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(expectPackages.begin(), expectPackages.end(), packages.begin()));
    EXPECT_TRUE(std::equal(expectImportPackageName.begin(), expectImportPackageName.end(), importPackageName.begin()));
    EXPECT_TRUE(std::equal(expectImportedItemName.begin(), expectImportedItemName.end(), importedItemName.begin()));
    EXPECT_TRUE(std::equal(expectedImportSpecEnd.begin(), expectedImportSpecEnd.end(), importSpecEnd.begin()));
    EXPECT_TRUE(std::equal(
        expectedImportedItemNamePos.begin(), expectedImportedItemNamePos.end(), importedItemNamePos.begin()));
}

TEST(ParserTest1, Modifier)
{
    std::string code = R"(
        public private protected static var a = 1
        abstract class a {}
        public interface d {}
        override func Main () {}
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();

    std::set<TokenKind> expectModifier{TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::PROTECTED, TokenKind::STATIC,
        TokenKind::ABSTRACT, TokenKind::PUBLIC, TokenKind::OVERRIDE};
    std::set<TokenKind> modifiers;
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const Modifier& modifier) {
                modifiers.insert(modifier.modifier);
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(expectModifier.begin(), expectModifier.end(), modifiers.begin()));
}
TEST(ParserTest1, VarDecl)
{
    std::string code = R"(
        var a1 : int = 40
        var a2 : Int32=40
        let b = 2 ** -a
        let mArray1 : Array<float> = [10.1, 20.2]
        let mArray2: Array<Array<Bool>>
        let mList1 : List<int> = [1, 2, 3]
        var mList2 : List<float>
        let tuplePIE = (3.14, 1, 'A',  "PIE")
        var (x,     y) = Point
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectDeclNames{
        "a1", "a2", "b", "mArray1", "mArray2", "mList1", "mList2", "tuplePIE", "", "x", "y"};

    std::vector<std::string> expectTypeNames{"int", "Int32", "Array", "float", "Array", "Array", "Bool", "List", "int",
        "List", "float", "int", "float", "int", "String", "int", "rune", "Tuple"};

    std::vector<std::string> declNames;
    std::vector<std::string> typeNames;
    Walker walker(file.get(), [&declNames, &typeNames](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&declNames](const Decl& decl) {
                declNames.push_back(decl.identifier);
                return VisitAction::WALK_CHILDREN;
            },
            [&typeNames](AST::Type& type) {
                return match(type)(
                    [&typeNames](const RefType& type) {
                        typeNames.push_back(type.ref.identifier);
                        return VisitAction::WALK_CHILDREN;
                    },
                    [&typeNames](const PrimitiveType& type) {
                        typeNames.push_back(type.str);
                        return VisitAction::WALK_CHILDREN;
                    },
                    []() { return VisitAction::WALK_CHILDREN; });
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(declNames.begin(), declNames.end(), expectDeclNames.begin()));
    EXPECT_TRUE(std::equal(typeNames.begin(), typeNames.end(), expectTypeNames.begin()));
}

TEST(ParserTest1, MacroDecl)
{
    std::string code = R"(
        external macro test(input1: Tokens): Tokens {
            return input1
        }
        external macro testattr(attr: Tokens, input2: Tokens): Tokens {
            return attr + input2
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectMacroNames{"test", "testattr"};
    std::vector<std::string> expectMacroParams{"input1", "attr", "input2"};
    std::vector<std::string> macroNames;
    std::vector<std::string> macroParams;
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const FuncDecl& macroDecl) {
                if (macroDecl.TestAttr(Attribute::MACRO_INVOKE_FUNC)) {
                    return VisitAction::WALK_CHILDREN;
                }
                macroNames.push_back(macroDecl.identifier);
                auto macroBody = macroDecl.funcBody.get();
                for (auto& param : macroBody->paramLists.front()->params) {
                    auto paramName = param.get()->identifier;
                    macroParams.push_back(paramName);
                }
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(macroNames.begin(), macroNames.end(), expectMacroNames.begin()));
    EXPECT_TRUE(std::equal(macroParams.begin(), macroParams.end(), expectMacroParams.begin()));
}

TEST(ParserTest1, ClassInterfaceDecl)
{
    std::string code = R"(
    class A <: B & C & D {
        func foo(){ }
        let tmp : Int32 = 40
        class AA{ }
        interface I1 {}
        interface I2 <: E1.E2 & F{}
    }
    class A <: B1.B2.B3 & C & D1.D2.D3 {}
    interface I <: J & K1.K2.K3{
        class A {}
        interface B{}
        func C() {}
        let D = 40
    }
)";
    std::vector<std::string> expectClassNames{"A", "B", "C", "D", "AA", "I1", "I2", "E1.E2", "F", "A", "B1.B2.B3", "C",
        "D1.D2.D3", "I", "J", "K1.K2.K3", "A", "B"};
    std::vector<std::string> expectMembers{"foo", "tmp", "AA", "I1", "I2", "A", "B", "C", "D"};
    std::vector<std::string> classNames, members;

    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();

    Walker walkerClass(file.get(), [&classNames, &members](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&members](const ClassBody& body) {
                for (auto& decl : body.decls) {
                    members.push_back(decl->identifier);
                }
                return VisitAction::WALK_CHILDREN;
            },
            [&members](const InterfaceBody& body) {
                for (auto& decl : body.decls) {
                    members.push_back(decl->identifier);
                }
                return VisitAction::WALK_CHILDREN;
            },
            [&classNames](const ClassDecl& decl) {
                classNames.push_back(decl.identifier);
                return VisitAction::WALK_CHILDREN;
            },
            [&classNames](const InterfaceDecl& decl) {
                classNames.push_back(decl.identifier);
                return VisitAction::WALK_CHILDREN;
            },
            [&classNames](const RefType& refType) {
                classNames.push_back(refType.ref.identifier);
                return VisitAction::SKIP_CHILDREN;
            },
            [&classNames](const QualifiedType& type) {
                std::function<std::string(Ptr<Node>)> getName = [&](Ptr<Node> type) {
                    return match(*type)(
                        [&](const QualifiedType& type) { return getName(type.baseType.get()) + '.' + type.field; },
                        [&](const RefType& type) { return type.ref.identifier.Val(); },
                        []() { return std::string(""); });
                };
                classNames.push_back(getName(type.baseType.get()) + '.' + type.field);
                return VisitAction::SKIP_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walkerClass.Walk();
    EXPECT_TRUE(std::equal(expectClassNames.begin(), expectClassNames.end(), classNames.begin()));
    EXPECT_TRUE(std::equal(expectMembers.begin(), expectMembers.end(), members.begin()));
}

TEST(ParserTest1, ClassDecl)
{
    std::string code = R"(
    class A {
        func B(a: Int32, b: Int32) {
            return a +
        }
        func C() {
        }
    }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();

    unsigned int counter = 0;
    Walker walker(file.get(), [&counter](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&counter](const ClassDecl& decl) {
                counter++;
                return VisitAction::WALK_CHILDREN;
            },
            [&counter](const FuncDecl& decl) {
                counter++;
                return VisitAction::SKIP_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    unsigned int expectedNodeCount = 3;
    EXPECT_EQ(counter, expectedNodeCount);
}

TEST(ParserTest1, StructDecl)
{
    std::string code = R"(
        struct Rectangle1 {
            var width : int
            var length : float
        }
        struct Rectangle2 {
            var width : int = 0
            var length : float = 0.0
        }
        struct Empty {
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectNames{"Rectangle1", "Rectangle2", "Empty"};
    std::vector<int> expectNums{2, 2, 0};

    std::vector<std::string> names;
    std::vector<size_t> nums;

    Walker walker(file.get(), [&names, &nums](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&names, &nums](const StructDecl& decl) {
                names.push_back(decl.identifier);
                nums.push_back(decl.body->decls.size());
                return VisitAction::SKIP_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(expectNames.begin(), expectNames.end(), names.begin()));
    EXPECT_TRUE(std::equal(expectNums.begin(), expectNums.end(), nums.begin()));
}

TEST(ParserTest1, RecordDecl2)
{
    std::string code = R"(
        struct Rect1 {
            var width : int
            var 2length : float
        }
        struct Rect2 {
            var 2width : int = 0
            var length : float = 0.0
        }
        struct Empty {
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();

    std::vector<std::string> expectNames{"Rect1", "Rect2", "Empty"};
    std::vector<std::string> names;

    Walker walker(file.get(), [&names](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&names](const StructDecl& decl) {
                names.push_back(decl.identifier);
                return VisitAction::SKIP_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(expectNames.begin(), expectNames.end(), names.begin()));
}

TEST(ParserTest1, OperatorFunc)
{
    std::string code = R"(
    extend Int32 {
        operator func -(b: Int32): Int32 { this + b }
        operator func [(b: Int32): Int32 { this + b }
    }
    class Test {
        operator func -(): Test {
            Point(-x, -y)
        }
        operator func [](): Test {
            Point(-x, -y)
        }
    }
    struct Test {
        operator func -(): Test {
            Point(-x, -y)
        }
    }
    interface Test {
        operator func -(): Test {
            Point(-x, -y)
        }
    }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectNames{
        "-",
        "<invalid identifier>",
        "Test",
        "-",
        "[]",
        "Test",
        "-",
        "Test",
        "-",
    };
    std::vector<std::string> names;

    Walker walker(file.get(), [&names](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&names](const ClassDecl& decl) {
                names.push_back(decl.identifier);
                return VisitAction::WALK_CHILDREN;
            },
            [&names](const StructDecl& decl) {
                names.push_back(decl.identifier);
                return VisitAction::WALK_CHILDREN;
            },
            [&names](const InterfaceDecl& decl) {
                names.push_back(decl.identifier);
                return VisitAction::WALK_CHILDREN;
            },
            [&names](const FuncDecl& decl) {
                names.push_back(decl.identifier);
                EXPECT_TRUE(decl.TestAttr(Attribute::OPERATOR));
                return VisitAction::SKIP_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(expectNames.begin(), expectNames.end(), names.begin()));
    const unsigned int expectedErrorCount = 1;
    EXPECT_EQ(diag.GetErrorCount(), expectedErrorCount);
}

TEST(ParserTest1, PrimitiveType)
{
    std::string code = R"(
        func test() {
            let a : Int8
            let a : Int16
            let a : Int32
            let a : Int64
            let a : UInt8
            let a : UInt16
            let a : UInt32
            let a : UInt64
            let a : Float16
            let a : Float32
            let a : Float64
            let a : Rune
            let a : Bool
            let a : Unit
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectTypes{"Int8", "Int16", "Int32", "Int64", "UInt8", "UInt16", "UInt32", "UInt64",
        "Float16", "Float32", "Float64", "Rune", "Bool", "Unit"};

    std::vector<std::string> types;

    Walker walker(file.get(), [&types](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&types](const PrimitiveType& e) {
                types.push_back(e.str);
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(expectTypes.begin(), expectTypes.end(), types.begin()));
}

TEST(ParserTest1, IfExpr)
{
    std::string code = R"(
        func test() {
            if (i<10) {
                b +=2
            }
            if (i<10) {
                b +=2
            }
            else if (i > 10) {
                b +=3
            }
            else if (i >2) {
            }
            else {
                b += 2
            }
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    int expectifnum = 4;
    int expectelseifnum = 2;
    int expectelsenum = 1;
    int ifnum = 0;
    int elsenum = 0;
    int elseifnum = 0;
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const IfExpr& expr) {
                ifnum++;
                if (expr.hasElse) {
                    match (*expr.elseBody.get())(
                        [&](const Block& block) { elsenum++; }, [&](const IfExpr& expr) { elseifnum++; });
                }
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_EQ(expectifnum, ifnum);
    EXPECT_EQ(expectelsenum, elsenum);
    EXPECT_EQ(expectelseifnum, elseifnum);
}

TEST(ParserTest1, QuoteExpr1)
{
    std::string code = R"(
        func test() {
            quote(1 + 2 + 3 \( \))
            quote($(ast) + $ast + ast + \$a)
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    int expectbinarynum = 0;
    int expectcallsnum = 0;
    int expectrefsnum = 2;
    int expectmemberaccessnum = 0;
    int binarynum = 0;
    int callnum = 0;
    int refnum = 0;
    int memberaccessnum = 0;
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const BinaryExpr& expr) {
                binarynum++;
                return VisitAction::WALK_CHILDREN;
            },
            [&](const CallExpr& expr) {
                callnum++;
                return VisitAction::WALK_CHILDREN;
            },
            [&](const RefExpr& expr) {
                refnum++;
                return VisitAction::WALK_CHILDREN;
            },
            [&](const MemberAccess& ma) {
                memberaccessnum++;
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_EQ(expectbinarynum, binarynum);
    EXPECT_EQ(expectcallsnum, callnum);
    EXPECT_EQ(expectrefsnum, refnum);
    EXPECT_EQ(expectmemberaccessnum, memberaccessnum);
}

TEST(ParserTest1, QuoteExpr2)
{
    std::string code = R"(
        macro package p
        macro test(input : Tokens): Tokens {
            let tokens : Tokens = quote(1 + 2 + 3)
            return quote(func test () : void {
                            a
                            \(
                            \)
                         }
            )
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    int expectquotenum = 2;
    size_t expecttokensnum = 5;
    size_t expectreturntoken = 16;
    int expectbinarynum = 0;
    int binarynum = 0;
    int quotenum = 0;
    size_t tokensnum = 0;
    size_t returntoken = 0;

    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const BinaryExpr& expr) {
                binarynum++;
                return VisitAction::WALK_CHILDREN;
            },
            [&](const VarDecl& decl) {
                if (decl.initializer) {
                    match (*decl.initializer)([&](const QuoteExpr& expr) {
                        quotenum++;
                        if (!expr.exprs.empty() && expr.exprs[0]->astKind == ASTKind::TOKEN_PART) {
                            tokensnum = RawStaticCast<TokenPart*>(expr.exprs[0].get())->tokens.size();
                        }
                    });
                }
                return VisitAction::WALK_CHILDREN;
            },
            [&](const ReturnExpr& retExpr) {
                if (retExpr.expr) {
                    match (*retExpr.expr)([&](const QuoteExpr& expr) {
                        quotenum++;
                        if (!expr.exprs.empty() && expr.exprs[0]->astKind == ASTKind::TOKEN_PART) {
                            returntoken = RawStaticCast<TokenPart*>(expr.exprs[0].get())->tokens.size();
                        }
                    });
                }
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_EQ(expectquotenum, quotenum);
    EXPECT_EQ(expecttokensnum, tokensnum);
    EXPECT_EQ(expectreturntoken, returntoken);
    EXPECT_EQ(expectbinarynum, binarynum);
}

TEST(ParserTest1, QuoteExpr3)
{
    std::string code = R"(
        public macro test(input : Tokens): Tokens {
            let b : Expr = CangjieLex("1 + 2 + 3")
            let c = 3
            return quote(func test () : void {
                            \(
                            \$
                            \$ b
                            \$b
                            $b
                            $(c + 1)
                         }
            )
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    int expectquotenum = 1;
    int expectexprsnum = 5;

    int quotenum = 0;
    int exprsnum = 0;

    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const ReturnExpr& retExpr) {
                if (retExpr.expr) {
                    match (*retExpr.expr)([&](const QuoteExpr& expr) {
                        quotenum++;
                        exprsnum = expr.exprs.size();
                    });
                }
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_EQ(expectquotenum, quotenum);
    EXPECT_EQ(expectexprsnum, exprsnum);
}
TEST(ParserTest1, ThrowExpr)
{
    std::string code = R"(
        func test() {
            throw IllegalArgumentException()
    }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    int expectthrownum = 1;
    int thrownum = 0;

    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const ThrowExpr& expr) {
                thrownum++;
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_EQ(expectthrownum, thrownum);
}

TEST(ParserTest1, StrInterpolationExpr)
{
    std::string code = R"(
        func test() {
            var a = "1"
            var b = "${a}"
            var c = "${a.}" // For lsp
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    int expectStrIntnum = 2;
    int strIntnum = 0;
    bool isClonedSourceCode = false;
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)([&](const StrInterpolationExpr& expr) { return VisitAction::WALK_CHILDREN; },
            [&](const InterpolationExpr& expr) {
                if (strIntnum == 0) {
                    EXPECT_EQ(expr.begin.column, 22);
                    EXPECT_EQ(expr.end.column, 26);
                } else if (strIntnum == 1) {
                    EXPECT_EQ(expr.end.column, 27);
                }
                strIntnum++;
                return VisitAction::WALK_CHILDREN;
            },
            [&]() {
                if (node->TestAttr(Attribute::IS_CLONED_SOURCE_CODE)) {
                    isClonedSourceCode = true;
                }
                return VisitAction::WALK_CHILDREN;
            });
    });
    walker.Walk();
    EXPECT_EQ(expectStrIntnum, strIntnum);
    EXPECT_TRUE(isClonedSourceCode);
}

TEST(ParserTest1, TryExpr)
{
    std::string code = R"(
        func test() {
            try {
                try {
                    throw IllegalArgumentException()
                } catch _:IllegalArgumentException | IndexOutOfBoundsException {
                    b+=2
                    throw IndexOutOfBoundsException()
                }
            } catch e:IllegalArgumentException | IndexOutOfBoundsException {
                b+=2
            } catch _ {
                b+=2
            } finally {
                b+=2
            }
}
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    int expecttrynum = 2;
    int expectcatchnum = 3;
    int expectfinallynum = 1;
    int trynum = 0;
    int catchnum = 0;
    int finallynum = 0;
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const TryExpr& expr) {
                trynum++;
                if (!expr.catchBlocks.empty()) {
                    for (size_t cnt = 0; cnt < expr.catchPatterns.size(); cnt++) {
                        catchnum++;
                    }
                }
                if (expr.finallyBlock) {
                    finallynum++;
                }
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_EQ(expecttrynum, trynum);
    EXPECT_EQ(expectcatchnum, catchnum);
    EXPECT_EQ(expectfinallynum, finallynum);
}

TEST(ParserTest1, TryExpr1)
{
    std::string code = R"(
    func test () {
        try {
            let lastElement = arrayTest[3]
        }
        catch (e: ArithmeticException | IndexOutOfBoundsException) {
            print("exception info: " + e)
        }
        catch (_) {
            print("neither ArithmeticException nor ArrayIndexOutOfBoundsException, exception info: " + e)
        }
        finally {
            print("the finally block is executed")
        }
    }
    func test () {
        try (input : File= FileInputStream("input.txt"), output = FileOutputStream("output.txt"))
        {
            let lineString = input.readLine()
        }
        catch (e : IOException) {
            print("IOException happened when executing the try-with-resources expression")
        }
        finally {
            print("end of the try-with-resources expression")
        }
    }
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();

    int tryBlock = 0;
    size_t catchBlock = 0;
    int finallyBlock = 0;
    int tryBlockExpect = 2;
    int catchBlockExpect = 3;
    int finallyBlockExpect = 2;
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const TryExpr& expr) {
                if (expr.tryBlock) {
                    tryBlock += 1;
                }
                catchBlock += expr.catchBlocks.size();
                if (expr.finallyBlock) {
                    finallyBlock += 1;
                }
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_EQ(tryBlock, tryBlockExpect);
    EXPECT_EQ(catchBlock, catchBlockExpect);
    EXPECT_EQ(finallyBlock, finallyBlockExpect);
}

TEST(ParserTest1, PatternMatch)
{
    std::string code = R"(
    var scoreResult: string = match (score) {
        case who => "zero"
        case 10  => "fail"
        case PASS => "pass"
        case _ =>
    }
    /*var scoreResult: String = match (scoreList) {
        case List{50,...} =>  "the lowest score is 50"
    }*/
    var rectangleArea2: int = match (colorPo) {
        case cp : Point => "pass"
    }
    var result: String = match {
        case score < 60 => "fail"
        case score < 70 => "pass"
        case score < 90 => "good"
        case _ => "excellent"
    }
    var howManyHours: float = match (oneYear) {
        case Year(y) => y*365*24      // enum + tuple + va + var
        case Year(y) | Month(y, z) => y*365*24
        case Year(y) | Month(y, z) | Day(y, z, x) => y*365*24
    }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();

    int varPattern = 0;
    int constPattern = 0;
    int wildcardPattern = 0;
    int tuplePattern = 0;
    int typePattern = 0;
    int enumPattern = 0;
    int varPatternExpect = 1;
    int constPatternExpect = 1;
    int wildcardPatternExpect = 2;
    int tuplePatternExpect = 0;
    int typePatternExpect = 1;
    int enumPatternEexpct = 6;
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const ConstPattern& pattern) {
                constPattern++;
                return VisitAction::WALK_CHILDREN;
            },
            [&](const VarPattern& pattern) {
                varPattern++;
                return VisitAction::WALK_CHILDREN;
            },
            [&](const WildcardPattern& pattern) {
                wildcardPattern++;
                return VisitAction::WALK_CHILDREN;
            },
            [&](const TuplePattern& pattern) {
                tuplePattern++;
                return VisitAction::WALK_CHILDREN;
            },
            [&](const TypePattern& pattern) {
                typePattern++;
                return VisitAction::WALK_CHILDREN;
            },
            [&](const EnumPattern& pattern) {
                enumPattern++;
                return VisitAction::WALK_CHILDREN;
            },
            [&](const WildcardExpr& wildcard) {
                wildcardPattern++;
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_EQ(varPattern, varPatternExpect);
    EXPECT_EQ(constPattern, constPatternExpect);
    EXPECT_EQ(wildcardPattern, wildcardPatternExpect);
    EXPECT_EQ(tuplePattern, tuplePatternExpect);
    EXPECT_EQ(typePattern, typePatternExpect);
    EXPECT_EQ(enumPattern, enumPatternEexpct);
}

TEST(ParserTest1, ForInExpr)
{
    std::string code = R"(
        main() {
            for (item in scoreList) {
                print(item)
            }
            for (item in scoreList where item > 50) {
                print(item)
            }
            for (_ in scoreList) {
                print("got a score")
            }
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectPatternLists{"VarPattern", "VarPattern", "WildcardPattern"};
    std::vector<std::string> expectPatternGuardLists{"BinaryExpr"};
    std::vector<std::string> patternLists, patternGuardLists;
    Walker walker(file.get(), [&patternLists, &patternGuardLists](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&patternLists, &patternGuardLists](const ForInExpr& expr) {
                match (*expr.pattern.get())(
                    [&patternLists](const VarPattern& e) { patternLists.push_back("VarPattern"); },
                    [&patternLists](const WildcardPattern& e) { patternLists.push_back("WildcardPattern"); });
                if (expr.patternGuard) {
                    match (*expr.patternGuard.get())(
                        [&patternGuardLists](const BinaryExpr& e) { patternGuardLists.push_back("BinaryExpr"); });
                }
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(patternLists.begin(), patternLists.end(), expectPatternLists.begin()));
    EXPECT_TRUE(std::equal(patternGuardLists.begin(), patternGuardLists.end(), expectPatternGuardLists.begin()));
}

TEST(ParserTest1, WhileExpr)
{
    std::string code = R"(
        func test() {
            while ((i<10))
            {
                a=b
            }
            while (()) {
                b +=2
            }
            while (i<10) {
                b = a.c
            }
            while {  // error
                return 0
            }
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectCondLists{"ParenExpr", "LitConstExpr", "BinaryExpr"};

    std::vector<std::string> condLists;

    Walker walker(file.get(), [&condLists](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&condLists](const WhileExpr& e) {
                match (*e.condExpr.get())(
                    [&condLists](const BinaryExpr& expr) { condLists.push_back(std::string("BinaryExpr")); },
                    [&condLists](const ParenExpr& expr) { condLists.push_back(std::string("ParenExpr")); },
                    [&condLists](const LitConstExpr& expr) { condLists.push_back(std::string("LitConstExpr")); });
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(condLists.begin(), condLists.end(), expectCondLists.begin()));
}

TEST(ParserTest1, DoWhileExpr)
{
    std::string code = R"(
        func test() {
            do {
                b +=2
            } while ((x < 100))

            do {
                b +=2
            } while x < 100
            do {
                b +=2
            } while (())
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectCondLists{"ParenExpr", "BinaryExpr"};

    std::vector<std::string> condLists;

    Walker walker(file.get(), [&condLists](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&condLists](const DoWhileExpr& expr) {
                match (*expr.condExpr.get())(
                    [&condLists](const ParenExpr& expr) { condLists.push_back(std::string("ParenExpr")); },
                    [&condLists](const BinaryExpr& expr) { condLists.push_back(std::string("BinaryExpr")); });
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(condLists.begin(), condLists.end(), expectCondLists.begin()));
}

TEST(ParserTest1, AssignExpr)
{
    std::string code = R"(
        func test() {
            A = expr
            A.B.C = expr
            A[0][1][2] = expr

            A.B[2].C.D[3] = expr
            A.B[0].C = expr
            A[0][1][1][2] = expr
            A.B[0][0].C[1] = expr

            this.A = expr
            this.A.B.C = expr
            this.A[0][1][2] = expr

            this.A.B[2].C.D[3] = expr
            this.A.B[0].C = expr
            this.A[0][1][1][2] = expr
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectLeftValues{"RefExpr", "MemberAccess", "SubscriptExpr", "SubscriptExpr",
        "MemberAccess", "SubscriptExpr", "SubscriptExpr", "MemberAccess", "MemberAccess", "SubscriptExpr",
        "SubscriptExpr", "MemberAccess", "SubscriptExpr"};

    std::vector<std::string> leftValues;

    Walker walker(file.get(), [&leftValues](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&leftValues](const AssignExpr& e) {
                return match(*e.leftValue)(
                    [&leftValues](const RefExpr&) {
                        leftValues.emplace_back("RefExpr");
                        return VisitAction::SKIP_CHILDREN;
                    },
                    [&leftValues](const MemberAccess&) {
                        leftValues.emplace_back("MemberAccess");
                        return VisitAction::SKIP_CHILDREN;
                    },
                    [&leftValues](const SubscriptExpr&) {
                        leftValues.emplace_back("SubscriptExpr");
                        return VisitAction::SKIP_CHILDREN;
                    },
                    []() { return VisitAction::STOP_NOW; });
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(expectLeftValues.begin(), expectLeftValues.end(), leftValues.begin()));
}

TEST(ParserTest1, IncOrDecExpr)
{
    std::string code = R"(
        func test() {
            a

            ++
            a = a

            --
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectOps{"++", "--"};

    std::vector<std::string> ops;

    Walker walker(file.get(), [&ops](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&ops](const IncOrDecExpr& e) {
                std::string str = (e.op == TokenKind::INCR) ? "++" : "--";
                ops.push_back(str);
                return VisitAction::SKIP_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(ops.begin(), ops.end(), expectOps.begin()));
}

TEST(ParserTest1, CallExpr)
{
    std::string code = R"(
        func test() {
            a = test1();
            b = test2(a, 1, a+b)
            c = test3(a:1, a:a+2)
            d = test4(if (()){}, while (1){})
            aa = test11()();
            bb = test22(aa)(bb:1)(cc:2);
            let a = Some(12)
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<int> expectNumParams{0, 3, 2, 2, 0, 1, 1};
    std::vector<int> expectNumExprs{1, 1, 1, 1, 2, 3, 1};
    std::vector<std::string> expectExprNames{"RefExpr", "LitConstExpr", "BinaryExpr", "LitConstExpr", "BinaryExpr",
        "IfExpr", "WhileExpr", "LitConstExpr", "LitConstExpr"};

    std::vector<size_t> numParams;
    std::vector<size_t> numExprs;
    std::vector<std::string> exprNames;

    Walker walker(file.get(), [&numParams, &numExprs, &exprNames](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&numParams, &numExprs, &exprNames](const CallExpr& e) {
                if (Is<RefExpr>(e.baseFunc.get())) {
                    std::string funcName = RawStaticCast<RefExpr*>(e.baseFunc.get())->ref.identifier;
                }

                numParams.push_back(e.args.size());
                for (auto& it : e.args) {
                    match (*it->expr.get())([&exprNames](const RefExpr& e) { exprNames.push_back("RefExpr"); },
                        [&exprNames](const LitConstExpr& e) { exprNames.push_back("LitConstExpr"); },
                        [&exprNames](const BinaryExpr& e) { exprNames.push_back("BinaryExpr"); },
                        [&exprNames](const AssignExpr& e) { exprNames.push_back("AssignExpr"); },
                        [&exprNames](const IfExpr& e) { exprNames.push_back("IfExpr"); },
                        [&exprNames](const WhileExpr& e) { exprNames.push_back("WhileExpr"); }, []() {});
                }
                std::function<int(int, Ptr<Node>)> getNum = [&](int n, Ptr<Node> expr) {
                    return match(*expr)(
                        [&](const CallExpr& e) { return getNum(n, e.baseFunc.get()) + 1; }, [&]() { return n; });
                };
                numExprs.push_back(getNum(1, e.baseFunc.get()));
                return VisitAction::SKIP_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(expectNumParams.begin(), expectNumParams.end(), numParams.begin()));
    EXPECT_TRUE(std::equal(expectExprNames.begin(), expectExprNames.end(), exprNames.begin()));
    EXPECT_TRUE(std::equal(expectNumExprs.begin(), expectNumExprs.end(), numExprs.begin()));
}

TEST(ParserTest1, MacroExpandExpr)
{
    std::string code = R"(
        main(): Int64 {
            @test1(Image1("company1.png"))
            @test2 Image2("company2.png")
            return 0
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectMacroExpandNames{"test1", "test2"};
    std::vector<std::string> expectMacroExpandArgs{
        "Image1", "(", "company1.png", ")", "Image2", "(", "company2.png", ")"};
    std::vector<std::string> macroExpandNames;
    std::vector<std::string> macroExpandArgs;
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const MacroExpandExpr& mee) {
                macroExpandNames.push_back(mee.identifier);
                for (auto& it : mee.invocation.args) {
                    macroExpandArgs.push_back(it.Value());
                }
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(macroExpandNames.begin(), macroExpandNames.end(), expectMacroExpandNames.begin()));
    EXPECT_TRUE(std::equal(macroExpandArgs.begin(), macroExpandArgs.end(), expectMacroExpandArgs.begin()));
}

TEST(ParserTest1, MacroExpandDecl)
{
    std::string code = R"(
        @test var a = 1
        @testattr[attribute token]
        main() { }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectMacroNames{"test", "testattr"};
    std::vector<std::string> expectMacroArgs{"var", "a", "=", "1", "main", "(", ")", "{", "}"};
    std::vector<std::string> expectMacroAttrs{"attribute", "token"};
    std::vector<std::string> macroNames;
    std::vector<std::string> macroArgs;
    std::vector<std::string> macroAttrs;
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const MacroExpandDecl& ma) {
                macroNames.push_back(ma.identifier.Val());
                for (auto& it : ma.invocation.args) {
                    macroArgs.push_back(it.Value());
                }
                for (auto& it : ma.invocation.attrs) {
                    macroAttrs.push_back(it.Value());
                }
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(macroNames.begin(), macroNames.end(), expectMacroNames.begin()));
    EXPECT_TRUE(std::equal(macroArgs.begin(), macroArgs.end(), expectMacroArgs.begin()));
    EXPECT_TRUE(std::equal(macroAttrs.begin(), macroAttrs.end(), expectMacroAttrs.begin()));
}

TEST(ParserTest1, MacroExpandInterStr)
{
    std::string code = R"(
        @M(println("${xx}"))
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();
    std::vector<std::string> originTokens;
    std::vector<std::string> expOriginTokens = {"(0, 2, 27)", "(0, 2, 23)", "(0, 2, 20)", "(0, 2, 19)", "(0, 2, 12)"};
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const MacroExpandDecl& macroExpand) {
                for (auto& it : macroExpand.invocation.originPosMap) {
                    originTokens.emplace_back(it.second.ToString());
                }
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(originTokens.begin(), originTokens.end(), expOriginTokens.begin()));
}

TEST(ParserTest1, PostfixUnaryExpr)
{
    std::string code = R"(
        func test() {
            b = b.c.d.e
            c = c[1][2][3][4]
            d = test(bb:1)(cc=2);
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<int> expectNums{3, 4, 2};

    std::vector<int> nums;

    Walker walker(file.get(), [&nums](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&nums](const MemberAccess& e) {
                std::function<int(int, Ptr<Node>)> getNum = [&](int n, Ptr<Node> expr) {
                    return match(*expr)(
                        [&](const MemberAccess& e) { return getNum(n, e.baseExpr.get()) + 1; }, [&]() { return n; });
                };
                nums.push_back(getNum(1, e.baseExpr.get()));
                return VisitAction::SKIP_CHILDREN;
            },
            [&nums](const SubscriptExpr& e) {
                std::function<int(int, Ptr<Node>)> getNum = [&](int n, Ptr<Node> expr) {
                    return match(*expr)(
                        [&](const SubscriptExpr& e) { return getNum(n, e.baseExpr.get()) + 1; }, [&]() { return n; });
                };
                nums.push_back(getNum(1, e.baseExpr.get()));
                return VisitAction::SKIP_CHILDREN;
            },
            [&nums](const CallExpr& e) {
                std::function<int(int, Ptr<Node>)> getNum = [&](int n, Ptr<Node> expr) {
                    return match(*expr)(
                        [&](const CallExpr& e) { return getNum(n, e.baseFunc.get()) + 1; }, [&]() { return n; });
                };
                nums.push_back(getNum(1, e.baseFunc.get()));
                return VisitAction::SKIP_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(expectNums.begin(), expectNums.end(), nums.begin()));
}

TEST(ParserTest1, RangeExpr)
{
    // included or nested.
    std::string code = R"(
        main() : Int
        {
            e1..e2
            e1..e2:e3
            e1..:e3
            1..10:3&1
            1..20:3&&2..10:2
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::vector<std::string>> expectExprLists{
        {"RangeExpr"},
        {"RangeExpr"},
        {"RangeExpr"},
        {"BinaryExpr", "RangeExpr"},
        {"BinaryExpr", "RangeExpr", "RangeExpr"},
    };
    std::vector<std::vector<std::string>> exprLists;

    for (auto& expr : RawStaticCast<MainDecl*>(file->decls[0].get())->funcBody->body->body) {

        std::vector<std::string> exprList;
        Walker walker(expr.get(), [&exprList](Ptr<Node> node) -> VisitAction {
            return match(*node)(
                [&exprList](const RangeExpr& e) {
                    exprList.push_back("RangeExpr");
                    return VisitAction::WALK_CHILDREN;
                },
                [&exprList](const BinaryExpr& e) {
                    exprList.push_back("BinaryExpr");
                    return VisitAction::WALK_CHILDREN;
                },
                []() { return VisitAction::WALK_CHILDREN; });
        });
        walker.Walk();
        exprLists.push_back(exprList);
    }

    EXPECT_TRUE(std::equal(exprLists.begin(), exprLists.end(), expectExprLists.begin()));
}

TEST(ParserTest1, RangeExprInBinaryExpr)
{
    std::string code = R"(
        main() : Int
        {
            let a = 1 || 2 ..= 10  : 1 + 1
            let b = !  !  -  9___E3__0
                >=  !  continue
                &&
                -  -  super  ..=
                -  0Xf.6_b_P7 : super ==
                !  -  super  -  -  -  super  is  Unit
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    bool missingOneStep = false;
    Walker walker(file.get(), [&missingOneStep](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&missingOneStep](const RangeExpr& re) {
                if (!re.stepExpr) {
                    missingOneStep = true;
                }
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    ASSERT_TRUE(!missingOneStep);
    ASSERT_TRUE(diag.GetErrorCount() == 0);
}

TEST(ParserTest1, MultiParenExpr)
{
    std::string code = R"(
    main()
    {
        var hOut = 0
        let strideH = 0
        let dilationH = 0
        let kernelSizeH = 0
        var padNeededH = (hOut - 1) * strideH + dilationH * (kernelSizeH - 1)
    }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();
    unsigned int expectErrorCount = 0;
    EXPECT_EQ(diag.GetErrorCount(), expectErrorCount);
}

TEST(ParserTest1, CurriedFunction)
{
    std::string code = R"(
        func test():unit{}
        func test(a:int, b:int=1): int {return a+b}
        func test()() {}
        func test(a:int)(b:int): int {return a+b}
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<int> expectNums{1, 1, 2, 2};
    std::vector<size_t> nums;

    Walker walker(file.get(), [&nums](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&nums](const FuncBody& e) {
                nums.push_back(e.paramLists.size());
                return VisitAction::SKIP_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(expectNums.begin(), expectNums.end(), nums.begin()));
}

TEST(ParserTest1, LambdaExpr)
{
    std::string code = R"(
        func test() {
            {=>}
            {=>}
            ()
            {a=>1}
            {a=>}
            {a:Int32, b => }
            {a, b, c, d:Int32, e => }
            {a, b, c, d, e => }
            {a, b, c, d, e => }
            (a, b, c,
            d, e)
            var sum = {a:int, b => a+b}
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectExprs{"LambdaExpr", "LambdaExpr", "LitConstExpr", "LambdaExpr", "LambdaExpr",
        "LambdaExpr", "LambdaExpr", "LambdaExpr", "LambdaExpr", "TupleLit", "LambdaExpr"};

    std::vector<std::string> exprs;
    Walker walker(file.get(), [&exprs](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&exprs](const LambdaExpr& e) {
                exprs.push_back("LambdaExpr");
                return VisitAction::SKIP_CHILDREN;
            },
            [&exprs](const LitConstExpr& e) {
                exprs.push_back("LitConstExpr");
                return VisitAction::SKIP_CHILDREN;
            },
            [&exprs](const TupleLit& e) {
                exprs.push_back("TupleLit");
                return VisitAction::SKIP_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(expectExprs.begin(), expectExprs.end(), exprs.begin()));
}

TEST(ParserTest1, TypeConvExpr)
{
    std::string code = R"(
        func test() {
            Int8(Int32(a))
            Float64(Int8(Float32(a)))  // Int8 is acutally a CallExpr
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectExprNames{"Int8", "Int32", "Float64", "Int8", "Float32"};
    std::vector<std::string> exprNames;

    Walker walker(file.get(), [&exprNames](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&exprNames](const TypeConvExpr& e) {
                exprNames.push_back(e.type->ToString());
                return VisitAction::WALK_CHILDREN;
            },
            [&exprNames](const CallExpr& e) {
                std::function<std::string(Ptr<Node>)> getName = [&](Ptr<Node> expr) {
                    return match(*expr)([&](const CallExpr& e) { return getName(e.baseFunc.get()); },
                        [&](const RefExpr& e) { return e.ref.identifier.Val(); }, []() { return std::string(""); });
                };
                exprNames.push_back(getName(e.baseFunc.get()));
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(expectExprNames.begin(), expectExprNames.end(), exprNames.begin()));
}

TEST(ParserTest1, Comparisons)
{
    std::string code = R"(
        func test() {
            a >= b ==c<d !=e > f // '!=e > f' is invalid and ignored
            a != b +c== d  // '==d' is invalid and ignored
            a > b ==c+d<=e
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    std::vector<std::string> expectNames{"a", "b", "c", "d", "e", "f", "a", "b", "c", "d", "a", "b", "c", "d", "e"};
    std::vector<std::string> names;
    Walker walker(file.get(), [&names](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&names](const RefExpr& e) {
                names.push_back(e.ref.identifier);
                return VisitAction::SKIP_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(std::equal(expectNames.begin(), expectNames.end(), names.begin()));
}

TEST(ParserTest1, IsAsExpr)
{
    std::string code = R"(
        let a = continue is Unit
          - ()
        let a = () as Unit - 1
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    bool wrongType = false;
    Walker walker(file.get(), [&wrongType](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&wrongType](const IsExpr& ie) {
                if (auto pt = AST::As<ASTKind::PRIMITIVE_TYPE>(ie.isType.get()); pt) {
                    if (pt->kind != TypeKind::TYPE_UNIT) {
                        wrongType = true;
                    }
                } else {
                    wrongType = true;
                }
                return VisitAction::WALK_CHILDREN;
            },
            [&wrongType](const AsExpr& ie) {
                if (auto pt = AST::As<ASTKind::PRIMITIVE_TYPE>(ie.asType.get()); pt) {
                    if (pt->kind != TypeKind::TYPE_UNIT) {
                        wrongType = true;
                    }
                } else {
                    wrongType = true;
                }
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    auto errorCount = diag.GetErrorCount();
    EXPECT_EQ(errorCount, 1);
    EXPECT_TRUE(!wrongType);
}

TEST(ParserTest1, OpExprs)
{
    std::string code = R"(
        func test() {
            a = -+-2 + -(2+a)
            b = -1 + a- -+- a
            c = a + (if (b) {1} else {3}) * (c * (d + 2))
            d = b + (if (c*a) {d*3} else {e}) + f**4
            e = -1 + a << 2 >>3 <=aaa
            f = if (a) {if (b) {c} else {d}} else {e}
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();
}

TEST(ParserTest1, Generics)
{
    std::string code = R"(
        func Add<T>(a: T, b: T) where T <: C {a + b}
        class Class<T> where T <: C { var c: T }
        interface Interface<T> where T <: C { var c: T }
        struct Struct<T> where T <: C { var c: T }
        enum Enum<T> where T <: C {a}
        type Type<T> = C
        main() {
            Add<Float64>(1.0, 2.0)
            var r: Struct<Int64>
            var e: Enum<UInt64> = Enum<Int64>.a
            var ee: Enum<UInt64> = Enum<Int64>.a(1, 2)
            var eee: Enum<UInt64> = Enum2<Float32, bool>.aaa<Int32, bool, Int64>(1, 2, 3, 4)
            var eeee: Enum<UInt64> = Enum3<Int64, bool>.fff<Float64, bool, Float64, Int64>
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    int checkNum = 0;
    Walker walker(file.get(), [&checkNum](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&checkNum](const FuncDecl& f) {
                if (f.identifier == "Add") {
                    EXPECT_EQ(f.funcBody->generic->typeParameters.size(), 1);
                    EXPECT_EQ(f.funcBody->generic->typeParameters[0]->identifier, "T");
                    auto& params = f.funcBody->paramLists[0]->params;
                    EXPECT_EQ(params[0]->identifier, "a");
                    EXPECT_EQ(As<ASTKind::REF_TYPE>(params[0]->type.get())->ref.identifier, "T");
                    EXPECT_EQ(params[1]->identifier, "b");
                    EXPECT_EQ(As<ASTKind::REF_TYPE>(params[1]->type.get())->ref.identifier, "T");
                    auto& genericConstraints = f.funcBody->generic->genericConstraints[0];
                    EXPECT_EQ(genericConstraints->type->ref.identifier, "T");
                    EXPECT_EQ(
                        As<ASTKind::REF_TYPE>(genericConstraints->upperBounds.begin()->get())->ref.identifier, "C");
                    ++checkNum;
                }
                return VisitAction::WALK_CHILDREN;
            },
            [&checkNum](const ClassDecl& c) {
                EXPECT_EQ(c.identifier, "Class");
                EXPECT_EQ(c.generic->typeParameters.size(), 1);
                EXPECT_EQ(c.generic->typeParameters[0]->identifier, "T");
                auto& genericConstraints = c.generic->genericConstraints;
                EXPECT_EQ(genericConstraints.size(), 1);
                EXPECT_EQ(c.generic->genericConstraints[0]->type->ref.identifier, "T");
                EXPECT_EQ(
                    As<ASTKind::REF_TYPE>(genericConstraints[0]->upperBounds.begin()->get())->ref.identifier, "C");
                EXPECT_EQ(c.body->decls.size(), 1);
                auto varDecl = As<ASTKind::VAR_DECL>(c.body->decls[0].get());
                EXPECT_EQ(As<ASTKind::REF_TYPE>(varDecl->type.get())->ref.identifier, "T");
                ++checkNum;
                return VisitAction::WALK_CHILDREN;
            },
            [&checkNum](const InterfaceDecl& i) {
                EXPECT_EQ(i.identifier, "Interface");
                EXPECT_EQ(i.generic->typeParameters.size(), 1);
                EXPECT_EQ(i.generic->typeParameters[0]->identifier, "T");
                auto& genericConstraints = i.generic->genericConstraints;
                EXPECT_EQ(genericConstraints.size(), 1);
                EXPECT_EQ(i.generic->genericConstraints[0]->type->ref.identifier, "T");
                EXPECT_EQ(
                    As<ASTKind::REF_TYPE>(genericConstraints[0]->upperBounds.begin()->get())->ref.identifier, "C");
                EXPECT_EQ(i.body->decls.size(), 1);
                auto varDecl = As<ASTKind::VAR_DECL>(i.body->decls[0].get());
                EXPECT_EQ(As<ASTKind::REF_TYPE>(varDecl->type.get())->ref.identifier, "T");
                ++checkNum;
                return VisitAction::WALK_CHILDREN;
            },
            [&checkNum](const StructDecl& r) {
                EXPECT_EQ(r.identifier, "Struct");
                EXPECT_EQ(r.generic->typeParameters.size(), 1);
                EXPECT_EQ(r.generic->typeParameters[0]->identifier, "T");
                auto& genericConstraints = r.generic->genericConstraints;
                EXPECT_EQ(genericConstraints.size(), 1);
                EXPECT_EQ(r.generic->genericConstraints[0]->type->ref.identifier, "T");
                EXPECT_EQ(
                    As<ASTKind::REF_TYPE>(genericConstraints[0]->upperBounds.begin()->get())->ref.identifier, "C");
                EXPECT_EQ(r.body->decls.size(), 1);
                auto varDecl = As<ASTKind::VAR_DECL>(r.body->decls[0].get());
                EXPECT_EQ(As<ASTKind::REF_TYPE>(varDecl->type.get())->ref.identifier, "T");
                ++checkNum;
                return VisitAction::WALK_CHILDREN;
            },
            [&checkNum](const EnumDecl& e) {
                EXPECT_EQ(e.identifier, "Enum");
                EXPECT_EQ(e.generic->typeParameters.size(), 1);
                EXPECT_EQ(e.generic->typeParameters[0]->identifier, "T");
                auto& genericConstraints = e.generic->genericConstraints;
                EXPECT_EQ(genericConstraints.size(), 1);
                EXPECT_EQ(e.generic->genericConstraints[0]->type->ref.identifier, "T");
                EXPECT_EQ(
                    As<ASTKind::REF_TYPE>(genericConstraints[0]->upperBounds.begin()->get())->ref.identifier, "C");
                ++checkNum;
                return VisitAction::WALK_CHILDREN;
            },
            [&checkNum](const TypeAliasDecl& t) {
                EXPECT_EQ(t.identifier, "Type");
                EXPECT_EQ(t.generic->typeParameters.size(), 1);
                EXPECT_EQ(t.generic->typeParameters[0]->identifier, "T");
                EXPECT_EQ(As<ASTKind::REF_TYPE>(t.type.get())->ref.identifier, "C");
                ++checkNum;
                return VisitAction::WALK_CHILDREN;
            },
            [&checkNum](const CallExpr& c) {
                if (auto re = AST::As<ASTKind::REF_EXPR>(c.baseFunc.get()); re) {
                    EXPECT_EQ(re->ref.identifier, "Add");
                    EXPECT_EQ(re->typeArguments.size(), 1);
                    EXPECT_EQ(As<ASTKind::PRIMITIVE_TYPE>(re->typeArguments[0].get())->kind, TypeKind::TYPE_FLOAT64);
                    ++checkNum;
                }
                if (auto ma = AST::As<ASTKind::MEMBER_ACCESS>(c.baseFunc.get()); ma) {
                    if (ma->typeArguments.empty()) {
                        if (auto re = AST::As<ASTKind::REF_EXPR>(ma->baseExpr.get()); re) {
                            EXPECT_EQ(re->ref.identifier, "Enum");
                            EXPECT_EQ(re->typeArguments.size(), 1);
                            EXPECT_EQ(
                                As<ASTKind::PRIMITIVE_TYPE>(re->typeArguments[0].get())->kind, TypeKind::TYPE_INT64);
                            EXPECT_EQ(c.args.size(), 2);
                            ++checkNum;
                        }
                    } else {
                        if (auto re = AST::As<ASTKind::REF_EXPR>(ma->baseExpr.get()); re) {
                            EXPECT_EQ(re->ref.identifier, "Enum2");
                            EXPECT_EQ(re->typeArguments.size(), 2);
                            EXPECT_EQ(
                                As<ASTKind::PRIMITIVE_TYPE>(re->typeArguments[0].get())->kind, TypeKind::TYPE_FLOAT32);
                            EXPECT_EQ(ma->field, "aaa");
                            EXPECT_EQ(ma->typeArguments.size(), 3);
                            EXPECT_EQ(
                                As<ASTKind::PRIMITIVE_TYPE>(ma->typeArguments[0].get())->kind, TypeKind::TYPE_INT32);
                            EXPECT_EQ(c.args.size(), 4);
                            ++checkNum;
                        }
                    }
                }
                return VisitAction::WALK_CHILDREN;
            },
            [&checkNum](const MemberAccess& ma) {
                if (auto re = AST::As<ASTKind::REF_EXPR>(ma.baseExpr.get()); re) {
                    if (re->ref.identifier == "Enum3") {
                        EXPECT_EQ(re->typeArguments.size(), 2);
                        EXPECT_EQ(As<ASTKind::PRIMITIVE_TYPE>(re->typeArguments[0].get())->kind, TypeKind::TYPE_INT64);
                        EXPECT_EQ(ma.field, "fff");
                        EXPECT_EQ(ma.typeArguments.size(), 4);
                        EXPECT_EQ(As<ASTKind::PRIMITIVE_TYPE>(ma.typeArguments[0].get())->kind, TypeKind::TYPE_FLOAT64);
                        ++checkNum;
                    }
                }
                return VisitAction::WALK_CHILDREN;
            },
            [&checkNum](const VarDecl& vd) {
                if (vd.identifier == "r") {
                    auto refType = As<ASTKind::REF_TYPE>(vd.type.get());
                    EXPECT_EQ(refType->ref.identifier, "Struct");
                    EXPECT_EQ(refType->typeArguments.size(), 1);
                    EXPECT_EQ(As<ASTKind::PRIMITIVE_TYPE>(refType->typeArguments[0].get())->kind, TypeKind::TYPE_INT64);
                    ++checkNum;
                }
                if (vd.identifier == "e") {
                    auto refType = As<ASTKind::REF_TYPE>(vd.type.get());
                    EXPECT_EQ(refType->ref.identifier, "Enum");
                    EXPECT_EQ(refType->typeArguments.size(), 1);
                    EXPECT_EQ(
                        As<ASTKind::PRIMITIVE_TYPE>(refType->typeArguments[0].get())->kind, TypeKind::TYPE_UINT64);
                    ++checkNum;
                }
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_EQ(checkNum, 12);
    EXPECT_EQ(1, diag.GetErrorCount());
}

TEST(ParserTest1, Return)
{
    std::string code = R"(
        func foo(): unit {
            return
            let a = 0
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    Walker walker(file.get(), [](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [](const ReturnExpr& retExpr) {
                EXPECT_TRUE(As<ASTKind::LIT_CONST_EXPR>(retExpr.expr.get()));
                return VisitAction::WALK_CHILDREN;
            },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
}

TEST(ParserPrefixTest, ParseMinusLitConst)
{
    std::string codes[] = {R"(-10)", R"(- 10)", R"(-0x01)", R"(- 0o1)", R"(-0b01)", R"(-0.01)", R"(- 0x0.1p2)"};
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    for (std::string& code : codes) {
        Parser parser(code, diag, sm);
        OwnedPtr<Expr> expr = parser.ParseExpr();
        EXPECT_TRUE(expr->astKind == ASTKind::LIT_CONST_EXPR);
    }
}

TEST(ParserTestDemo, Demo1)
{
    std::string code = R"(
        func Print(s : String) : unit {}
        func Sleep(t : int) : unit {}

        main() : int
        {
            while (true) {
                Print("Hello Rune Lang\n")
                Sleep(2000)
            }

            return 0
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();
}

TEST(ParserTestDemo, Demo2)
{
    std::string code = R"(
        let clockPort   = 12
        let dataPort    = 5
        let ledNum      = 64         // led number
        let lightColor  = 0xffff0000 // led light color -> b: 255, g: 0, r: 0

        // for LED show
        var pos : int   = 0          // LED position
        var leds : int[]

        // c libary api ======= fake FFI
        func print() : unit {}
        func print(str : String) : unit {}
        func sleep(inv : int) : unit {}
        func OpenGPIO(pin : int) : unit {}
        func WriteGPIO(pin : int, val : int) : unit {}
        func SetWord(clkPort : int, dataPort : int, val : int) : unit {}

        // Util function
        func CDW(val : int) : unit {
            SetWord(clockPort, dataPort, val)
        }

        // Set the global array
        func SetChaserPattern() : unit {
            leds[pos] = lightColor
            pos = (pos + 1) % ledNum;
            // for (var i = 0; i < ledNum; i++) {
            //     leds[i] *= 0.6 // if no float, what should we do here?
            // }
        }

        // Show LED: Right Shift Zero
        func ShowLED(leds : int, lightPWM : int) : unit {
            CDW(0)
            lightPWM = 0xFF000000
            // for (var i = 0; i < ledNum; i++) {
            //     let buf = lightPWM | leds[i]
            //     CDW(buf)
            // }
            CDW(0xffffffff)
        }

        func StartChaserMode() {
            while (true) {
                SetChaserPattern()
                ShowLED(leds, 0xFF000000)
                sleep(50) // fake sleep
            }
        }

        main() : int {
            print("hello world")

            // Initialize GPIO
            OpenGPIO(clockPort)
            WriteGPIO(clockPort, 1)

            OpenGPIO(dataPort)

            // Initialize LEDS
            // for (var i = 0; i < ledNum; i++) {
            //     leds[i] = 0x00000000;
            // }

            // Show LED
            print("Start Marquee...")
            StartChaserMode()
            return 0
        }
)";

    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();
}

TEST(ParserTestError, ErrorPrint)
{
    std::string code = R"(
        public static func Main() {
        }
        open public var a = 1
        default override class a {
        }
        default static interface a {}
        if (){}     // line = 8
        var a : int = 1
        class {
        }
        class func Main() {
        }
        func Main(int)
        }
        class a
            var aa : int
            var bb : int
        }
        var a : a = 1    // line = 20
        var a : int = (a+b)++
        enum TimeUnit {
        }
        struct a {
        var a : a = 1
        func
}
        var a = 1
        var scoreResult: String = match scoreList {
            case List 50, ..., } =>  "the lowest score is 50" // line = 30
        }
        var scoreResult: String = match scoreMap {
            case Map{"A" : score} => "the highest score is "+score.tostring()
        }
        var varString : String = "\a"
        var a = .a()
        public struct a {
            var a : a = 1
        }
        class a {  // line = 40
            func a ()
        }
        func a ()
        var a = a > b > c
        public private class a{}
        func a() {
            a+b c+d
        }
        func f1(a: Int32, b: Int32, c: Int32 = 3, d: Int32): Int32 {
            return a + b + c + d
         }
        let a = "中文\u4E2D\t"
        external internal struct A{
            static let a = 1;
            public func c() {
            }
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();
}

TEST(ParserTestNative, NativePrint)
{
    std::string code = R"(
    foreign func ok():unit
    foreign func ok1():unit
    foreign var a: Int32
    foreign var b: Int32 = 1
    foreign let c: Int32
    foreign{func ok2():unit}
    foreign {
        func ok2():unit
        var a1: Int32
        var b1: Int32 = 1
        let a2: Int32
    }
    foreign func ok():unit
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();

    EXPECT_EQ(0, diag.GetErrorCount());
}

TEST(ParserTestSpawn, Spawn)
{
    std::string code = R"(
        main(): Int64 {
            let i = 0
            let f =  spawn{ =>
                i++
            }
            return i
        }
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);

    OwnedPtr<File> file = parser.ParseTopLevel();

    EXPECT_EQ(0, diag.GetErrorCount());
}

TEST(ParserTestMatchCase, MatchCase)
{
    std::string code = R"(match(1){
        case 1 => a + aa
    })";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto expr = parser.ParseExpr();
    ASSERT_EQ(ASTKind::MATCH_EXPR, expr->astKind);

    auto matchExpr = StaticAs<ASTKind::MATCH_EXPR>(expr.get());
    EXPECT_EQ(Position(0, 2, 9), matchExpr->matchCases[0]->begin);
    EXPECT_EQ(Position(0, 2, 16), matchExpr->matchCases[0]->arrowPos);
    EXPECT_EQ(Position(0, 2, 25), matchExpr->matchCases[0]->end);
}

TEST(ParserTestWildcardExpr, WildcardExpr)
{
    std::string code = R"(
        _ = 1
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto expr = parser.ParseExpr();
    ASSERT_EQ(expr->astKind, ASTKind::ASSIGN_EXPR);

    auto assignExpr = StaticAs<ASTKind::ASSIGN_EXPR>(expr.get());
    EXPECT_EQ(assignExpr->leftValue->astKind, ASTKind::WILDCARD_EXPR);
}

TEST(ParserTestWildcardParam, LetOrVar)
{
    std::string code = R"(
        class A {
            A(let _:Int64){ }
        }
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto decl = parser.ParseDecl(ScopeKind::TOPLEVEL);
    EXPECT_EQ(diag.GetErrorCount(), 1);

    std::vector<Ptr<Node>> funcParams;
    Walker walker(decl.get(), [&funcParams](Ptr<Node> node) -> VisitAction {
        if (Is<FuncParam>(node)) {
            auto param = StaticAs<ASTKind::FUNC_PARAM>(node);
            EXPECT_EQ(param->identifier, "_");
            funcParams.push_back(node);
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    ASSERT_EQ(funcParams.size(), 1);
}

TEST(ParserTestWildcardParam, NamedParameter)
{
    std::string code = R"(
        class A {
            func a(_!:Int64){ }
        }
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto decl = parser.ParseDecl(ScopeKind::TOPLEVEL);
    EXPECT_EQ(diag.GetErrorCount(), 1);

    std::vector<Ptr<Node>> funcParams;
    Walker walker(decl.get(), [&funcParams](Ptr<Node> node) -> VisitAction {
        if (Is<FuncParam>(node)) {
            auto param = StaticAs<ASTKind::FUNC_PARAM>(node);
            EXPECT_EQ(param->identifier, "_");
            funcParams.push_back(node);
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    ASSERT_EQ(funcParams.size(), 1);
}

TEST(ParserTestWildcardParam, LambdaParameter)
{
    std::string code = R"(
        main() {
            let a = {_:Int64 => return false}
        }
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto decl = parser.ParseDecl(ScopeKind::TOPLEVEL);
    EXPECT_EQ(diag.GetErrorCount(), 0);

    std::vector<Ptr<Node>> funcParams;
    Walker walker(decl.get(), [&funcParams](Ptr<Node> node) -> VisitAction {
        if (Is<FuncParam>(node)) {
            auto param = StaticAs<ASTKind::FUNC_PARAM>(node);
            EXPECT_EQ(param->identifier, "_");
            funcParams.push_back(node);
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    ASSERT_EQ(funcParams.size(), 1);
}

TEST(ParserTestWildcardExpr, OnlyAppearOnTheLeftOfAssignment)
{
    std::string code = R"(
    func fn() {
        a = _
        b = _ + _
        _ += 1
    }
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto decl = parser.ParseDecl(ScopeKind::TOPLEVEL);
    EXPECT_EQ(diag.GetErrorCount(), 4);

    std::vector<Ptr<Node>> wildcardExprs;
    Walker walker(decl.get(), [&](Ptr<Node> node) -> VisitAction {
        if (node->astKind == ASTKind::WILDCARD_EXPR) {
            wildcardExprs.push_back(node);
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    ASSERT_EQ(wildcardExprs.size(), 4);
}

TEST(ParserTestWarningAboutNewline, ExprNlExpr)
{
    auto verify = [](std::string code) -> void {
        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser parser(code, diag, sm);
        parser.ParseExpr();
        EXPECT_EQ(diag.GetWarningCount(), 1);
    };
    std::unordered_set<std::string> operatorSet = {
        ">>=",
        ">=",
        ">>",
        "**",
        "*",
        "%",
        "/",
        "+",
        "&&",
        "||",
        "|>",
        "~>",
        "&",
        "|",
        "^",
        "<<",
        "..",
        "..=",
        "<",
        ">",
        "<=",
        "!=",
        "==",
        "+=",
        "-=",
        "*=",
        "**=",
        "/=",
        "%=",
        "&&=",
        "||=",
        "&=",
        "|=",
        "^=",
        "<<=",
    };
    std::unordered_set<std::string> operatorTypeSet = {"is", "as"};
    std::string firstLine = "1\n";

    for (auto&& code : operatorSet) {
        verify(firstLine + code + " 1");
    }
    for (auto&& code : operatorTypeSet) {
        verify(firstLine + code + " Int64");
    }
}

TEST(ParserTestWarningAboutNewline, ReturnNlExpr)
{
    std::string code = "return \n1";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    parser.ParseExpr();
    EXPECT_EQ(diag.GetWarningCount(), 1);
}

TEST(ParserTestTupleLeftValue, tupleLeftValue)
{
    std::string code = "(_, _) = (1, 2)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto expr = parser.ParseExpr();
    ASSERT_EQ(expr->astKind, ASTKind::ASSIGN_EXPR);

    auto assignExpr = StaticAs<ASTKind::ASSIGN_EXPR>(expr.get());
    ASSERT_EQ(assignExpr->leftValue->astKind, ASTKind::TUPLE_LIT);

    auto tupleLeftValue = StaticAs<ASTKind::TUPLE_LIT>(assignExpr->leftValue.get());
    EXPECT_EQ(tupleLeftValue->children[0]->astKind, ASTKind::WILDCARD_EXPR);
    EXPECT_EQ(tupleLeftValue->children[1]->astKind, ASTKind::WILDCARD_EXPR);

    EXPECT_EQ(diag.GetErrorCount(), 0);
}

TEST(ParserTestTupleLeftValue, wildcardExprOnTheRight)
{
    std::string code = "(a, b) = (1, _)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto expr = parser.ParseExpr();
    ASSERT_EQ(expr->astKind, ASTKind::ASSIGN_EXPR);

    auto assignExpr = StaticAs<ASTKind::ASSIGN_EXPR>(expr.get());
    ASSERT_EQ(assignExpr->rightExpr->astKind, ASTKind::TUPLE_LIT);

    auto tupleRightValue = StaticAs<ASTKind::TUPLE_LIT>(assignExpr->rightExpr.get());
    EXPECT_EQ(tupleRightValue->children[0]->astKind, ASTKind::LIT_CONST_EXPR);
    EXPECT_EQ(tupleRightValue->children[1]->astKind, ASTKind::WILDCARD_EXPR);

    EXPECT_EQ(diag.GetErrorCount(), 1);
}

TEST(ParserTestTupleLeftValue, TupleAddAssign)
{
    std::string code = "(a, b) += (1, 2)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto expr = parser.ParseExpr();
    ASSERT_EQ(expr->astKind, ASTKind::ASSIGN_EXPR);

    auto assignExpr = StaticAs<ASTKind::ASSIGN_EXPR>(expr.get());
    ASSERT_EQ(assignExpr->rightExpr->astKind, ASTKind::TUPLE_LIT);
    ASSERT_EQ(assignExpr->leftValue->astKind, ASTKind::TUPLE_LIT);

    EXPECT_EQ(diag.GetErrorCount(), 1);
}

TEST(ParserTest2, NamedInoutArg)
{
    std::string code = R"(let a = foo(a: inout a))";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto expr = parser.ParseDecl(ScopeKind::TOPLEVEL);
    auto diags = diag.GetCategoryDiagnostic(Cangjie::DiagCategory::PARSE);
    ASSERT_EQ(diags.size(), 1);
    EXPECT_EQ(diags[0].rKind, DiagKindRefactor::parse_expected_expression);
}

TEST(PositionTest, doWhileExpr)
{
    std::string code = R"(
    do { i++ }
    while (true)
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto expr = parser.ParseExpr();

    EXPECT_EQ(expr->astKind, ASTKind::DO_WHILE_EXPR);
    EXPECT_EQ(expr->begin.line, 2);
    EXPECT_EQ(expr->begin.column, 5);
    EXPECT_EQ(expr->end.line, 3);
    EXPECT_EQ(expr->end.column, 17);
}

TEST(PositionTest, macroPackageHeader)
{
    std::string code = R"(
    macro package mpg
    public macro m0 (input: Tokens) { input }
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();

    EXPECT_EQ(file->package->begin.line, 2);
    EXPECT_EQ(file->package->begin.column, 5);
}

TEST(PositionTest, packageHeader)
{
    std::string code = R"(
    package pg
    main () { 0 }
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();

    EXPECT_EQ(file->package->begin.line, 2);
    EXPECT_EQ(file->package->begin.column, 5);
}

TEST(PositionTest, varWithPattern1)
{
    std::string code = R"(
    let (a, b) =
        (1, 2)
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto expr = parser.ParseDecl(ScopeKind::TOPLEVEL);

    EXPECT_EQ(expr->astKind, ASTKind::VAR_WITH_PATTERN_DECL);

    auto e = StaticAs<ASTKind::VAR_WITH_PATTERN_DECL>(expr.get());

    EXPECT_EQ(e->begin.line, 2);
    EXPECT_EQ(e->begin.column, 5);

    EXPECT_EQ(e->keywordPos.line, 2);
    EXPECT_EQ(e->keywordPos.column, 5);

    EXPECT_EQ(e->end.line, 3);
    EXPECT_EQ(e->end.column, 15);
}

TEST(PositionTest, varWithPattern2)
{
    std::string code = R"(
    let (a, b) =
        (1, 2)
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto expr = parser.ParseDecl(ScopeKind::TOPLEVEL);

    EXPECT_EQ(expr->astKind, ASTKind::VAR_WITH_PATTERN_DECL);

    auto e = StaticAs<ASTKind::VAR_WITH_PATTERN_DECL>(expr.get());

    EXPECT_EQ(e->begin.line, 2);
    EXPECT_EQ(e->begin.column, 5);

    EXPECT_EQ(e->keywordPos.line, 2);
    EXPECT_EQ(e->keywordPos.column, 5);

    EXPECT_EQ(e->end.line, 3);
    EXPECT_EQ(e->end.column, 15);
}

TEST(PositionTest, unsafeBlock)
{
    std::string code = R"(
    unsafe {
        let a = 2
        let b = 4
    }
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto expr = parser.ParseExpr();

    EXPECT_EQ(expr->astKind, ASTKind::BLOCK);

    auto e = StaticAs<ASTKind::BLOCK>(expr.get());

    EXPECT_EQ(e->begin.line, 2);
    EXPECT_EQ(e->begin.column, 5);

    EXPECT_EQ(e->unsafePos.line, 2);
    EXPECT_EQ(e->unsafePos.column, 5);

    EXPECT_EQ(e->leftCurlPos.line, 2);
    EXPECT_EQ(e->leftCurlPos.column, 12);
}

TEST(PositionTest, multilineComment1)
{
    std::string code = R"(/* myComment
    more text
   someText
*/
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Lexer lexer(code, diag, sm);
    auto commentTok = lexer.GetTokens().front();

    EXPECT_EQ(commentTok.kind, TokenKind::COMMENT);

    EXPECT_EQ(commentTok.Begin().line, 1);
    EXPECT_EQ(commentTok.Begin().column, 1);

    EXPECT_EQ(commentTok.End().line, 4);
    EXPECT_EQ(commentTok.End().column, 3);
}

TEST(PositionTest, multilineComment2)
{
    std::string code = R"(    /* myComment */)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Lexer lexer(code, diag, sm);
    auto commentTok = lexer.GetTokens().front();

    EXPECT_EQ(commentTok.kind, TokenKind::COMMENT);

    EXPECT_EQ(commentTok.Begin().line, 1);
    EXPECT_EQ(commentTok.Begin().column, 5);

    EXPECT_EQ(commentTok.End().line, 1);
    EXPECT_EQ(commentTok.End().column, 20);
}

TEST(PositionTest, constVarWithModifier)
{
    std::string code = R"(
    public const a = 1;
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto decl = parser.ParseDecl(ScopeKind::TOPLEVEL);

    EXPECT_EQ(decl->astKind, ASTKind::VAR_DECL);

    auto e = StaticAs<ASTKind::VAR_DECL>(decl.get());

    EXPECT_EQ(e->begin.line, 2);
    EXPECT_EQ(e->begin.column, 5);
}

TEST(PositionTest, memberParamWithModifier)
{
    std::string code = R"(
    class A {
        A (public let a: Int64){ }
    }
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto decl = parser.ParseDecl(ScopeKind::TOPLEVEL);
    EXPECT_EQ(diag.GetErrorCount(), 0);

    std::vector<Ptr<Node>> funcParams;
    Walker walker(decl.get(), [&funcParams](Ptr<Node> node) -> VisitAction {
        if (Is<FuncParam>(node)) {
            auto param = StaticAs<ASTKind::FUNC_PARAM>(node);
            EXPECT_EQ(param->begin.line, 3);
            EXPECT_EQ(param->begin.column, 12);
            funcParams.push_back(node);
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    EXPECT_EQ(funcParams.size(), 1);
}

TEST_F(ParserTest, FmtWhen)
{
    std::string code{R"(@When[abcd == "apple"]
public open class Rectangle {
})"};
    Parser p(code, diag, sm);
    auto file = p.ParseTopLevel();
    auto& cl = StaticCast<ClassDecl>(*file->decls[0]);
    ASSERT_EQ(cl.annotations.size(), 1);
    ASSERT_EQ(cl.annotations[0]->kind, AnnotationKind::WHEN);
    ASSERT_EQ(cl.annotations[0]->end.column, 23);
}

TEST_F(ParserTest, OuterDeclUnderMacroExpandDecl)
{
    std::string code{
        R"(
@Derive[ToString]
@Derive[Equatable]
@!APILevel[
    19,
    atomicservice: true,
    stagemodelonly: true,
    syscap: "SystemCapability.Advertising.Ads"
]
public class Parameter {
    @M1
    @!APILevel[
        19,
        atomicservice: true,
        stagemodelonly: true,
        syscap: "SystemCapability.Advertising.Ads"
    ]
    public Parameter(
        let key: String
    ) {}
    @M1
    @Frozen
    public func foo() {}
}
@Derive[Equatable]
public struct S {
    @M1
    public func hoo() {}
    @M1
    @Frozen
    public prop b: Int64 {
        get() {
            0
        }
    }
}
@Derive[Equatable]
enum RGBColor {
    | Red | Green | Blue
    @M1
    public static func printType() {
        print("RGBColor")
    }
}
 
extend String {
    @M1
    public func printSize() {
        println("the size is ${this.size}")
    }
}
 
)"};
    Parser p(code, diag, sm);
    auto file = p.ParseTopLevel();
    bool hitKey = false;
    bool hitPrimaryCtor = false;
    bool hitFoo = false;
    bool hitHoo = false;
    bool hitB = false;
    bool hitPrintType = false;
    bool hitPrintSize = false;
    Walker walker(file.get(),
        [&hitPrimaryCtor, &hitKey, &hitFoo, &hitHoo, &hitB, &hitPrintType, &hitPrintSize](
            Ptr<Node> node) -> VisitAction {
            Meta::match (*node)(
                [&hitPrimaryCtor](const PrimaryCtorDecl& decl) {
                    if (decl.identifier == "Parameter" && decl.outerDecl) {
                        hitPrimaryCtor = true;
                    }
                },
                [&hitB](const PropDecl& decl) {
                    if (decl.outerDecl && decl.identifier == "b") {
                        hitB = true;
                    }
                },
                [&hitKey](const VarDecl& decl) {
                    if (decl.identifier == "key" && decl.outerDecl) {
                        hitKey = true;
                    }
                },
                [&hitFoo, &hitHoo, &hitPrintType, &hitPrintSize](const FuncDecl& decl) {
                    if (decl.outerDecl) {
                        if (decl.identifier == "foo") {
                            hitFoo = true;
                        } else if (decl.identifier == "hoo") {
                            hitHoo = true;
                        } else if (decl.identifier == "printType") {
                            hitPrintType = true;
                        } else if (decl.identifier == "printSize") {
                            hitPrintSize = true;
                        }
                    }
                });
            return VisitAction::WALK_CHILDREN;
        });
    walker.Walk();
    ASSERT_TRUE(hitPrimaryCtor);
    ASSERT_TRUE(hitKey);
    ASSERT_TRUE(hitFoo);
    ASSERT_TRUE(hitHoo);
    ASSERT_TRUE(hitB);
    ASSERT_TRUE(hitPrintType);
    ASSERT_TRUE(hitPrintSize);
}

TEST(PositionTest, FuncWithoutBodyCommentsPos)
{
    std::string code = R"(/**
* Provides class to generate logs.
*/
@!APILevel[
    21,
    stagemodelonly: true,
    syscap: "SystemCapability.HiviewDFX.HiLog"
]
public class HilogChannel {
    
    /**
    * Outputs debug-level logs.
    */
    @!APILevel[
        21,
        stagemodelonly: true,
        syscap: "SystemCapability.HiviewDFX.HiLog"
    ]
    public func debug<T>(message: T): Unit where T <: ToString
    
    /**
    * Outputs info-level logs.
    */
    @!APILevel[
        21,
        stagemodelonly: true,
        syscap: "SystemCapability.HiviewDFX.HiLog"
    ]
    public func info<T>(message: T): Unit where T <: ToString
})";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm, Position{0, 1, 1}, true, true);
    auto file = parser.ParseTopLevel();
    EXPECT_EQ(diag.GetErrorCount(), 0);
    auto& d = StaticCast<MacroExpandDecl>(*file->decls[0]);
    EXPECT_EQ(d.comments.innerComments.size(), 0);
    auto& hilog = StaticCast<ClassDecl>(*d.invocation.decl);
    EXPECT_EQ(hilog.comments.innerComments.size(), 0);
    auto members = hilog.GetMemberDeclPtrs();
    auto debugAPI = StaticCast<MacroExpandDecl>(members[0]);
    auto debug = StaticCast<FuncDecl>(debugAPI->invocation.decl.get());
    EXPECT_EQ(debug->begin, Position(19, 5));
    EXPECT_EQ(debug->end, Position(19, 63));
    EXPECT_EQ(debugAPI->comments.leadingComments.size(), 1);
    EXPECT_EQ(debugAPI->comments.trailingComments.size(), 0);
    auto infoAPI = StaticCast<MacroExpandDecl>(members[1]);
    auto info = StaticCast<FuncDecl>(infoAPI->invocation.decl.get());
    EXPECT_EQ(info->begin, Position(29, 5));
    EXPECT_EQ(info->end, Position(29, 62));
    EXPECT_EQ(infoAPI->comments.leadingComments.size(), 1);
    EXPECT_EQ(infoAPI->comments.trailingComments.size(), 0);
}

TEST(PositionTest, Finalizer)
{
    std::string code = R"(
class C {
    ~  init() {
        println(3)
    }
}
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto decl = parser.ParseDecl(ScopeKind::TOPLEVEL);
    auto& finalizer = StaticCast<FuncDecl>(*StaticCast<ClassDecl>(*decl).GetMemberDecls()[0]);
    EXPECT_EQ(finalizer.identifier.Begin().column, 5);
    EXPECT_EQ(finalizer.identifier.End().column, 12);
    EXPECT_EQ(finalizer.keywordPos.column, 8);
}

TEST_F(ParserTest, LSP)
{
    std::string lspCode = "package 1di";
    Parser parser(lspCode, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto p = diag.GetCategoryDiagnostic(DiagCategory::LEX);
    ASSERT_EQ(p.size(), 2);
    ASSERT_EQ(p[0].errorMessage, std::string{"illegal integer suffix 'i'"});
    ASSERT_EQ(p[1].errorMessage, std::string{"unexpected digit 'd' in decimal"});
}

TEST(PositionTest, Attribute)
{
    std::string code = R"(@Attribute[      Ham,    "string",]
class C {})";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm, Position{0, 1, 1}, true, true);
    auto file = parser.ParseTopLevel();
    EXPECT_EQ(diag.GetErrorCount(), 0);
    auto& c = *StaticCast<ClassDecl>(*file->decls[0]).annotations[0];
    ASSERT_EQ(c.kind, AnnotationKind::ATTRIBUTE);
    ASSERT_EQ(c.attrs.size(), 2);
    EXPECT_EQ(c.attrs[0].kind, TokenKind::IDENTIFIER);
    EXPECT_EQ(c.attrs[0].Begin(), Position(1, 18));
    EXPECT_EQ(c.attrs[1].kind, TokenKind::STRING_LITERAL);
    EXPECT_EQ(c.attrs[1].Begin(), Position(1, 26));
    EXPECT_EQ(c.attrs[1].End(), Position(1, 34));
    ASSERT_EQ(c.attrCommas.size(), 2);
    EXPECT_EQ(c.attrCommas[0], Position(1, 21));
    EXPECT_EQ(c.attrCommas[1], Position(1, 34));
}
 
TEST(PositionTest, QuoteEscape)
{
    std::string code = R"(let a = quote(\@ \( \) \$))";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm, Position{0, 1, 1}, true, true);
    auto file = parser.ParseTopLevel();
    EXPECT_EQ(diag.GetErrorCount(), 0);
    auto& d = StaticCast<VarDecl>(*file->decls[0]);
    auto& quote = StaticCast<QuoteExpr>(*d.initializer);
    auto& tokens = StaticCast<TokenPart>(*quote.exprs[0]).tokens;
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].Begin(), Position(1, 15));
    EXPECT_EQ(tokens[1].Begin(), Position(1, 18));
    EXPECT_EQ(tokens[2].Begin(), Position(1, 21));
    EXPECT_EQ(tokens[3].Begin(), Position(1, 24));
}
 
TEST(PositionTest, SubscriptExpr)
{
    std::string code = "main() {b[1, 2]}";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm, Position{0, 1, 1}, true, true);
    auto file = parser.ParseTopLevel();
    EXPECT_EQ(diag.GetErrorCount(), 0);
    auto& d = StaticCast<MainDecl>(*file->decls[0]);
    auto& e = StaticCast<SubscriptExpr>(*d.funcBody->body->body[0]);
    ASSERT_EQ(e.commaPos.size(), 1);
    EXPECT_EQ(e.commaPos[0], Position(1, 12));
}

TEST(PositionTest, GenericConstraintBegin)
{
    std::string code = R"(func foo<T>(a: T): Unit where T <: ToString {})";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm, Position{0, 1, 1}, true, true);
    auto file = parser.ParseTopLevel();
    EXPECT_EQ(diag.GetErrorCount(), 0);
    auto& foo = StaticCast<FuncDecl>(*file->decls[0]);
    ASSERT_NE(foo.funcBody->generic, nullptr);
    EXPECT_EQ(foo.funcBody->generic->genericConstraints[0]->begin, Position(1, 25));
}

TEST(ParserTest1, IntLiteral)
{
    std::string code = "let a = 0u32";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::LEX);
    EXPECT_EQ(diagnostics.size(), 0);
}
TEST(ParserTest1, IntLiteralu32)
{
    std::string code = "let a = 0__u32";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::LEX);
    EXPECT_EQ(diagnostics.size(), 0);
}
TEST(ParserTest1, RangeLiteral)
{
    std::string code = "let a = 0127..11";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::LEX);
    EXPECT_EQ(diagnostics.size(), 0);
}
TEST(ParserTest1, RangeLiteral0x)
{
    std::string code = "let a = 0x127..11";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::LEX);
    EXPECT_EQ(diagnostics.size(), 0);
}
TEST(ParserTest1, IntLiteralDot)
{
    std::string code = "let a = 000.a";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::LEX);
    ASSERT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(diagnostics[0].errorMessage, std::string{"cannot start a(n) integer literal with '00'"});
}
TEST_F(ParserTest, IntLiteralDot2)
{
    std::string code = "let a = 00___1.a";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::LEX);
    ASSERT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(diagnostics[0].errorMessage, std::string{"cannot start a(n) integer literal with '00___'"});
}
TEST(ParserTest1, FloatLiteralDot)
{
    std::string code = "let a = 0001.3";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::LEX);
    ASSERT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(diagnostics[0].errorMessage, std::string{"cannot start a(n) float literal with '000'"});
}
TEST(ParserTest1, ZeroUnderscore)
{
    std::string code = "let a = 0_a";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::LEX);
    ASSERT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(diagnostics[0].errorMessage, std::string{"unexpected digit 'a' in decimal"});
}
TEST(ParserTest1, ZeroUnderscore2)
{
    std::string code = "let a = 00_a";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::LEX);
    ASSERT_EQ(diagnostics.size(), 2);
    EXPECT_EQ(diagnostics[0].errorMessage, std::string{"cannot start a(n) integer literal with '0'"});
    EXPECT_EQ(diagnostics[1].errorMessage, std::string{"unexpected digit 'a' in decimal"});
}
