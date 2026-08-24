// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the ToString method for nodes.
 */

#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"

#include <algorithm>
#include <iomanip>
#include <ios>
#include <iterator>
#include <memory>
#include <numeric>
#include <queue>
#include <sstream>
#include <vector>

#include "cangjie/AST/Match.h"
#include "cangjie/AST/RecoverDesugar.h"
#include "cangjie/AST/Symbol.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/Position.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/StdUtils.h"

namespace Cangjie {
using namespace AST;

namespace {
const std::string EMPTY_PACKAGE_NAME = "";

struct Span {
    std::string str;
    int numNewLine{0};
    int width{0};
};

Span NextSpan(const std::string& str, const Position& begin, const Position& end, int offset = 0)
{
    Span span;
    span.str = str;
    span.numNewLine = end.line - begin.line;
    span.width = (span.numNewLine > 0 ? end.column - 1 : end.column - begin.column) + offset;
    return span;
}

std::ostream& operator<<(std::ostream& out, Span const& span)
{
    int numNewLine = span.numNewLine;
    while (numNewLine > 0) {
        out << '\n';
        numNewLine--;
    }
    out << std::right << std::setw(span.width) << span.str;
    return out;
}

template <typename T>
std::string JoinNodeStrings(const std::vector<OwnedPtr<T>>& nodes, const std::string& separator)
{
    std::string ret;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (i > 0) {
            ret += separator;
        }
        CJC_NULLPTR_CHECK(nodes[i]);
        ret += nodes[i]->ToString();
    }
    return ret;
}

/**
 * @brief Get the sourcePos of macrocall by the curfile's pos. For cjc, we
 *        get sourcePos only in *.macrocall file .
 * @return the sourcePos in curfile if the source can be founded in curfile,
 *  sourcePos in macrocall file otherwise.
 */
Position GetMacroSourcePos(const MacroInvocation& invocation, const Position& pos, bool isLowerBound = false)
{
    return invocation.macroCallDiagInfo.MapPos(pos, isLowerBound);
}
} // namespace

std::string Modifier::ToString() const
{
    if (isExplicit) {
        return TOKENS[static_cast<int>(modifier)];
    } else {
        return "";
    }
}

std::string VarDecl::ToString() const
{
    std::stringstream ss;
    Position curSpanBegin = begin;
    int i = 0;
    for (auto modifier : modifiers) {
        if (i > 0) {
            ss << NextSpan(modifier.ToString(), curSpanBegin, modifier.end);
        } else {
            ss << modifier.ToString();
        }
        curSpanBegin = modifier.end;
        i++;
    }
    // The length of "var" and "let" are both 3.
    ss << NextSpan(isVar ? "var" : "let", curSpanBegin, keywordPos, 3);
    curSpanBegin = keywordPos + Position{0, 0, 3};
    ss << NextSpan(identifier, curSpanBegin, identifier.Begin(), static_cast<int>(identifier.Length()));
    curSpanBegin = identifier.End();
    if (type) {
        ss << NextSpan(":", curSpanBegin, colonPos, 1);
        curSpanBegin = colonPos + Position{0, 0, 1};
        ss << NextSpan(type->ToString(), curSpanBegin, type->end);
        curSpanBegin = type->end;
    }
    if (initializer) {
        ss << NextSpan("=", curSpanBegin, assignPos, 1);
        curSpanBegin = assignPos + Position{0, 0, 1};
        ss << NextSpan(initializer->ToString(), curSpanBegin, initializer->end);
    }
    return ss.str();
}

std::string EnumPattern::GetIdentifier() const
{
    if (!constructor) {
        return "";
    }
    switch (constructor->astKind) {
        case ASTKind::REF_EXPR: {
            return static_cast<const RefExpr&>(*constructor).ref.identifier;
        }
        case ASTKind::MEMBER_ACCESS: {
            return static_cast<const MemberAccess&>(*constructor).field;
        }
        default: {
            CJC_ABORT();
            return "";
        }
    }
}

std::string CallExpr::ToString() const
{
    std::stringstream ss;
    Position curSpanBegin;
    if (baseFunc != nullptr) {
        curSpanBegin = baseFunc->begin;
        ss << NextSpan(baseFunc->ToString(), curSpanBegin, baseFunc->end);
        curSpanBegin = baseFunc->end;
        ss << NextSpan("(", curSpanBegin, leftParenPos, 1);
        curSpanBegin = leftParenPos + Position{0, 0, 1};
    } else {
        ss << "(";
        curSpanBegin = Position{0, 0, 1};
    }
    for (size_t i = 0; i < args.size(); ++i) {
        CJC_ASSERT(args[i] != nullptr);
        ss << NextSpan(args[i]->ToString(), curSpanBegin, args[i]->end);
        curSpanBegin = args[i]->end;
        if (i + 1 < args.size() && args[i]->commaPos != INVALID_POSITION) {
            ss << NextSpan(",", curSpanBegin, args[i]->commaPos, 1);
            curSpanBegin = args[i]->commaPos + Position{0, 0, 1};
        }
    }
    ss << NextSpan(")", curSpanBegin, rightParenPos, 1);
    curSpanBegin = rightParenPos + Position{0, 0, 1};
    if (modal) {
        ss << NextSpan(modal.ToModalInfo().ToString(), curSpanBegin, modal.End());
    }
    return ss.str();
}

void CallExpr::Clear() noexcept
{
    RecoverToCallExpr(*this);
    Expr::Clear();
    if (baseFunc == nullptr) {
        return;
    }
    baseFunc->Clear();
    callKind = CallKind::CALL_INVALID;
    resolvedFunction = nullptr;
}

std::string FuncArg::ToString() const
{
    // In null-safety scenarios we may build an incomplete FuncArg (name set, expr absent).
    // Returning a compact fallback avoids emitting artificial newlines/spaces from sparse positions.
    if (!name.Empty() && expr == nullptr) {
        return name.Val() + ":";
    }

    std::stringstream ss;
    Position curSpanBegin = begin;
    if (!name.Empty()) {
        curSpanBegin = name.Begin();
        ss << NextSpan(name, curSpanBegin, name.Begin(), static_cast<int>(name.Length()));
        curSpanBegin += Position{0, 0, static_cast<int>(name.Length())};
        ss << NextSpan(":", curSpanBegin, colonPos, 1);
        curSpanBegin += Position{0, 0, 1};
    }
    if (expr != nullptr) {
        if (withInout) {
            ss << "inout ";
        }
        ss << NextSpan(expr->ToString(), curSpanBegin, expr->end);
    } else if (!name.Empty()) {
        // If name is set but expr is null, return "name:"
        // curSpanBegin is already at the position after colon
    }
    return ss.str();
}

std::string MemberAccess::ToString() const
{
    std::stringstream ss;
    Position curSpanBegin;
    if (baseExpr != nullptr) {
        curSpanBegin = baseExpr->begin;
        ss << NextSpan(baseExpr->ToString(), curSpanBegin, baseExpr->end);
        curSpanBegin = baseExpr->end;
        if (Is<OptionalExpr>(baseExpr.get())) {
            curSpanBegin += Position{0, 0, 1};
        }
        ss << NextSpan(".", curSpanBegin, dotPos, 1);
        curSpanBegin = dotPos + Position{0, 0, 1};
    }
    ss << NextSpan(field, curSpanBegin, field.Begin(), static_cast<int>(field.Length()));
    curSpanBegin = field.GetRawEndPos();
    return ss.str();
}

std::string LitConstExpr::ToString() const
{
    if (kind == LitConstKind::STRING) {
        return "\"" + stringValue + "\"";
    } else {
        return stringValue;
    }
}

TypeKind LitConstExpr::GetNumLitTypeKind()
{
    int suffixWidth = 0;
    if (kind == LitConstKind::RUNE_BYTE) {
        return TypeKind::TYPE_UINT8;
    }
    if (kind == LitConstKind::INTEGER) {
        auto suffixStart =
            std::find_if(stringValue.begin(), stringValue.end(), [](char c) { return c == 'i' || c == 'u'; });
        std::string suffix = std::string(suffixStart, stringValue.end());
        if (suffix.empty()) {
            return TypeKind::TYPE_IDEAL_INT;
        } else {
            if (auto suffixWid = Stoi(std::string(suffix.begin() + 1, suffix.end()))) {
                suffixWidth = *suffixWid;
            } else {
                return TypeKind::TYPE_INVALID;
            }
            // The following will calculate logarithm 8, 16, 32, 64 base 2 and will get
            // 3,4,5,6, take 3(int) or 4(float) as base and plus powerBase2 - 3 + INT8 or UINT8 or FLOAT16
            int powerBase2 = __builtin_ctz(static_cast<unsigned>(suffixWidth));
            char signedness = suffix[0];
            // The following code violates P.08-CPP(V5.0), however, I think it is OK since the number of width must
            // be the several possibilities. If not the case, lexer would report an error in the early stage. Can
            // only be i, u, f three cases
            if (signedness == 'i' || signedness == 'u') {
                int leastPowerBase2 = 3; // int starts from 8=2^3 bits width
                TypeKind tk = signedness == 'i' ? TypeKind::TYPE_INT8 : TypeKind::TYPE_UINT8;
                return static_cast<TypeKind>(powerBase2 - leastPowerBase2 + static_cast<int>(tk));
            } else {
                return TypeKind::TYPE_INVALID;
            }
        }
    } else if (kind == LitConstKind::FLOAT) {
        // Check whether it is a hexadecimal floating pointing number. If it is then
        // 'f' is allowed as digits and there will not be any suffix.
        // We should skip the negative sign if it exists.
        bool isNegative = !stringValue.empty() && stringValue.front() == '-';
        std::string prefix = stringValue.substr(isNegative ? 1 : 0, std::min<size_t>(stringValue.size(), 2UL));
        if (prefix == "0x" || prefix == "0X") {
            return TypeKind::TYPE_IDEAL_FLOAT;
        }
        auto suffixStart = std::find_if(stringValue.begin(), stringValue.end(), [](char c) { return c == 'f'; });
        std::string suffix = std::string(suffixStart, stringValue.end());
        if (suffix.empty()) {
            return TypeKind::TYPE_IDEAL_FLOAT;
        } else {
            if (auto suffixWid = Stoi(std::string(suffix.begin() + 1, suffix.end()))) {
                suffixWidth = *suffixWid;
            } else {
                return TypeKind::TYPE_INVALID;
            }
            int powerBase2 = __builtin_ctz(static_cast<unsigned>(suffixWidth));
            int leastPowerBase2 = 4;
            return static_cast<TypeKind>(powerBase2 - leastPowerBase2 + static_cast<int>(TypeKind::TYPE_FLOAT16));
        }
    }
    return TypeKind::TYPE_INVALID;
}

std::string RefType::ToString() const
{
    std::stringstream ss;
    ss << ref.identifier.Val();
    Position curSpanBegin = ref.identifier.End();
    if (!typeArguments.empty()) {
        ss << NextSpan("<", curSpanBegin, leftAnglePos, 1);
        curSpanBegin = leftAnglePos + Position{0, 0, 1};
        for (size_t i = 0; i < typeArguments.size(); ++i) {
            CJC_ASSERT(typeArguments[i] != nullptr);
            ss << NextSpan(typeArguments[i]->ToString(), curSpanBegin, typeArguments[i]->end);
            curSpanBegin = typeArguments[i]->end;
        }
        ss << NextSpan(">", curSpanBegin, rightAnglePos, 1);
    }
    return ss.str();
}

std::string RefExpr::ToString() const
{
    return ref.identifier;
}

std::string ArrayLit::ToString() const
{
    std::stringstream ss;
    ss << "[";
    Position curSpanBegin = begin + Position{0, 0, 1};
    for (size_t i = 0; i < children.size(); ++i) {
        CJC_ASSERT(children[i] != nullptr);
        ss << NextSpan(children[i]->ToString(), curSpanBegin, children[i]->end);
        curSpanBegin = children[i]->end;
        if (i + 1 < children.size() && i < commaPosVector.size()) {
            ss << NextSpan(",", curSpanBegin, commaPosVector[i], 1);
            curSpanBegin = commaPosVector[i] + Position{0, 0, 1};
        }
    }
    ss << NextSpan("]", curSpanBegin, rightSquarePos, 1);
    return ss.str();
}

std::string ArrayExpr::ToString() const
{
    std::stringstream ss;
    Position curSpanBegin;
    if (type != nullptr) {
        curSpanBegin = begin;
        ss << NextSpan(type->ToString(), curSpanBegin, type->end);
        curSpanBegin = type->end;
        ss << NextSpan("(", curSpanBegin, leftParenPos, 1);
        curSpanBegin = leftParenPos + Position{0, 0, 1};
    } else {
        ss << "(";
        curSpanBegin = Position{0, 0, 1};
    }

    for (size_t i = 0; i < args.size(); ++i) {
        CJC_ASSERT(args[i] != nullptr);
        ss << NextSpan(args[i]->ToString(), curSpanBegin, args[i]->end);
        curSpanBegin = args[i]->end;
        if (i + 1 < args.size() && i < commaPosVector.size()) {
            ss << NextSpan(",", curSpanBegin, commaPosVector[i], 1);
            curSpanBegin = commaPosVector[i] + Position{0, 0, 1};
        }
    }
    ss << NextSpan(")", curSpanBegin, rightParenPos, 1);
    return ss.str();
}

std::string PointerExpr::ToString() const
{
    std::stringstream ss;
    std::string expr = "CPointer<";
    Position curSpanBegin = begin;
    Position curSpanEnd = curSpanBegin + Position{0, 0, static_cast<int>(expr.size())};
    ss << NextSpan(expr, curSpanBegin, curSpanEnd);

    curSpanBegin = curSpanEnd;
    auto pointeeTy = GetTy();
    if (pointeeTy && !pointeeTy->typeArgs.empty()) {
        expr = pointeeTy->typeArgs[0]->String();
        curSpanEnd = curSpanBegin + Position{0, 0, static_cast<int>(expr.size())};
        ss << NextSpan(expr, curSpanBegin, curSpanEnd);
    }

    // 2 is string length of ">(".
    curSpanEnd = curSpanBegin + Position{0, 0, 2};
    ss << NextSpan(">(", curSpanBegin, curSpanEnd);
    curSpanBegin = curSpanEnd;
    if (arg) {
        expr = arg->ToString();
        curSpanEnd = curSpanBegin + Position{0, 0, static_cast<int>(expr.size())};
        ss << NextSpan(expr, curSpanBegin, curSpanEnd);
    }
    ss << NextSpan(")", curSpanBegin, curSpanBegin + Position{0, 0, 1});
    return ss.str();
}

Ptr<Node> Block::GetLastExprOrDecl() const
{
    if (body.empty()) {
        return nullptr;
    }

    return body.back().get();
}

Node::~Node()
{
    Node::Clear();
    if (symbol && symbol->node == this) {
        symbol->invertedIndexBeenDeleted = true;
    }
}

bool Node::ShouldDiagnose(bool allowCompilerAdd) const
{
    if (!allowCompilerAdd) {
        if (TestAttr(Attribute::COMPILER_ADD) && !TestAttr(Attribute::IS_CLONED_SOURCE_CODE)) {
            return false;
        }
    } else if (TestAttr(Attribute::COMPILER_ADD) && begin.IsZero()) {
        // Should not diagnose when the position is empty even the 'allowCompilerAdd' flag is true.
        return false;
    }
    // Ignore macro added nodes.
    if (TestAttr(Attribute::MACRO_INVOKE_FUNC) || TestAttr(Attribute::MACRO_INVOKE_BODY)) {
        return false;
    }
    if (auto e = DynamicCast<const AST::Expr*>(this); e) {
        if (e->sourceExpr) {
            return false;
        }
    }
    return true;
}

bool Node::IsSamePackage(const Node& other) const
{
    Ptr<File> otherFile = other.curFile;
    if (!curFile || !otherFile) {
        return true;
    }
    Ptr<Package> curPackage = curFile->curPackage;
    Ptr<Package> otherPackage = otherFile->curPackage;
    if (!curPackage || !otherPackage) {
        return true;
    }
    return curPackage == otherPackage;
}

void SubscriptExpr::Clear() noexcept
{
    RecoverToSubscriptExpr(*this);
    if (baseExpr) {
        baseExpr->Clear();
    }
    for (auto& indexExpr : indexExprs) {
        indexExpr->Clear();
    }
    commaPos.clear();
    Expr::Clear();
}

void AssignExpr::Clear() noexcept
{
    RecoverToAssignExpr(*this);
    Expr::Clear();
}

void UnaryExpr::Clear() noexcept
{
    RecoverToUnaryExpr(*this);
    Expr::Clear();
    if (expr) {
        expr->Clear();
    }
}

void BinaryExpr::Clear() noexcept
{
    RecoverToBinaryExpr(*this);
    Expr::Clear();
    if (leftExpr) {
        leftExpr->Clear();
    }
    if (rightExpr) {
        rightExpr->Clear();
    }
}

void ParenExpr::Clear() noexcept
{
    Expr::Clear();
    if (expr) {
        expr->Clear();
    }
}

bool RefType::IsGenericThisType() const
{
    if (auto cd = DynamicCast<ClassDecl*>(ref.target);
        cd && cd->generic && !cd->generic->typeParameters.empty() && ref.identifier == "This") {
        return true;
    }
    return false;
}

std::set<Ptr<InterfaceTy>> InheritableDecl::GetSuperInterfaceTys() const
{
    std::set<Ptr<InterfaceTy>> ret;
    for (auto& types : inheritedTypes) {
        if (types && types->GetTy() && types->TyKind() == TypeKind::TYPE_INTERFACE) {
            ret.insert(RawStaticCast<InterfaceTy*>(types->DataTy()));
        }
    }
    return ret;
}

std::vector<Ptr<InterfaceTy>> InheritableDecl::GetStableSuperInterfaceTys() const
{
    auto cmp = [](Ptr<Ty> ty1, Ptr<Ty> ty2) { return CompTyByNames(ty1, ty2); };

    std::set<Ptr<InterfaceTy>, decltype(cmp)> ret(cmp);
    for (auto& types : inheritedTypes) {
        if (types && types->GetTy() && types->TyKind() == TypeKind::TYPE_INTERFACE) {
            ret.emplace(RawStaticCast<InterfaceTy*>(types->DataTy()));
        }
    }
    return std::vector<Ptr<InterfaceTy>>(ret.begin(), ret.end());
}

void ClassLikeDecl::MarkAsJavaMirror()
{
    EnableAttr(Attribute::JAVA_MIRROR);
}

void ClassLikeDecl::MarkAsJavaImpl()
{
    EnableAttr(Attribute::JAVA_IMPL);
}

std::vector<Ptr<ClassLikeDecl>> InheritableDecl::GetAllSuperDecls()
{
    std::set<Ptr<ClassLikeDecl>> visited; // to avoid multiple paths or cycle
    std::vector<Ptr<ClassLikeDecl>> ret; // to guarantee order
    std::queue<Ptr<InheritableDecl>> workList;
    workList.push(this);
    if (auto cd = DynamicCast<ClassLikeDecl*>(this)) {
        visited.emplace(cd);
        ret.emplace_back(cd);
    }
    while (!workList.empty()) {
        auto curDecl = workList.front();
        workList.pop();
        for (auto& it : curDecl->inheritedTypes) {
            if (auto clsTy = DynamicCast<ClassTy>(it->DataTy()); clsTy && visited.count(clsTy->declPtr) == 0) {
                workList.push(clsTy->declPtr);
                visited.emplace(clsTy->declPtr);
                ret.emplace_back(clsTy->declPtr);
            } else if (auto interfaceTy = DynamicCast<InterfaceTy>(it->DataTy());
                interfaceTy && visited.count(interfaceTy->declPtr) == 0) {
                workList.push(interfaceTy->declPtr);
                visited.emplace(interfaceTy->declPtr);
                ret.emplace_back(interfaceTy->declPtr);
            }
        }
    }
    return ret;
}

std::vector<Ptr<Decl>> Decl::GetMemberDeclPtrs() const
{
    std::vector<Ptr<Decl>> results;
    if (auto cd = DynamicCast<const ClassDecl*>(this); cd) {
        CJC_NULLPTR_CHECK(cd->body);
        for (auto& decl : cd->body->decls) {
            results.push_back(decl.get());
        }
    } else if (auto id = DynamicCast<const InterfaceDecl*>(this); id) {
        CJC_NULLPTR_CHECK(id->body);
        for (auto& decl : id->body->decls) {
            results.push_back(decl.get());
        }
    } else if (auto sd = DynamicCast<const StructDecl*>(this); sd) {
        CJC_NULLPTR_CHECK(sd->body);
        for (auto& decl : sd->body->decls) {
            results.push_back(decl.get());
        }
    } else if (auto ed = DynamicCast<const EnumDecl*>(this); ed) {
        for (auto& constructor : ed->constructors) {
            results.emplace_back(constructor.get());
        }
        for (auto& decl : ed->members) {
            results.push_back(decl.get());
        }
    } else if (auto exd = DynamicCast<const ExtendDecl*>(this); exd) {
        for (auto& decl : exd->members) {
            results.push_back(decl.get());
        }
    } else if (auto bid = DynamicCast<BuiltInDecl>(this)) {
        for (auto& decl : bid->members) {
            results.push_back(decl.get());
        }
    }
    return results;
}

void Node::SetTarget(Ptr<Decl> target)
{
    switch (astKind) {
        case ASTKind::REF_TYPE: {
            RawStaticCast<RefType*>(this)->ref.target = target;
            break;
        }
        case ASTKind::REF_EXPR: {
            RawStaticCast<RefExpr*>(this)->ref.target = target;
            break;
        }
        case ASTKind::QUALIFIED_TYPE: {
            RawStaticCast<QualifiedType*>(this)->target = target;
            break;
        }
        case ASTKind::MEMBER_ACCESS: {
            RawStaticCast<MemberAccess*>(this)->target = target;
            break;
        }
        case ASTKind::MACRO_EXPAND_DECL: {
            RawStaticCast<MacroExpandDecl*>(this)->invocation.target = target;
            break;
        }
        case ASTKind::MACRO_EXPAND_EXPR: {
            RawStaticCast<MacroExpandExpr*>(this)->invocation.target = target;
            break;
        }
        case ASTKind::MACRO_EXPAND_PARAM: {
            RawStaticCast<MacroExpandParam*>(this)->invocation.target = target;
            break;
        }
        default:
            return;
    }
}

Ptr<Decl> Node::GetTarget() const
{
    switch (astKind) {
        case ASTKind::REF_TYPE: {
            return RawStaticCast<const RefType*>(this)->ref.target;
        }
        case ASTKind::REF_EXPR: {
            return RawStaticCast<const RefExpr*>(this)->ref.target;
        }
        case ASTKind::QUALIFIED_TYPE: {
            return RawStaticCast<const QualifiedType*>(this)->target;
        }
        case ASTKind::MEMBER_ACCESS: {
            return RawStaticCast<const MemberAccess*>(this)->target;
        }
        case ASTKind::MACRO_EXPAND_DECL: {
            return RawStaticCast<const MacroExpandDecl*>(this)->invocation.target;
        }
        case ASTKind::MACRO_EXPAND_EXPR: {
            return RawStaticCast<const MacroExpandExpr*>(this)->invocation.target;
        }
        case ASTKind::MACRO_EXPAND_PARAM: {
            return RawStaticCast<const MacroExpandParam*>(this)->invocation.target;
        }
        default:
            return nullptr;
    }
}

std::vector<Ptr<Decl>> Node::GetTargets() const
{
    switch (astKind) {
        case ASTKind::REF_TYPE: {
            return RawStaticCast<const RefType*>(this)->ref.targets;
        }
        case ASTKind::REF_EXPR: {
            return RawStaticCast<const RefExpr*>(this)->ref.targets;
        }
        case ASTKind::MEMBER_ACCESS: {
            auto targetDecls = RawStaticCast<const MemberAccess*>(this)->targets;
            std::vector<Ptr<Decl>> decls(targetDecls.begin(), targetDecls.end());
            return decls;
        }
        default:
            return {};
    }
}

/**
 * Get a MacroInvocation ptr.
 * @return MacroInvocation ptr if a node is MacroExpandExpr or MacroExpandDecl,
 *  nullptr otherwise.
 */
Ptr<const MacroInvocation> Node::GetConstInvocation() const
{
    if (this->astKind == ASTKind::MACRO_EXPAND_EXPR) {
        auto mc = RawStaticCast<const MacroExpandExpr*>(this);
        return &(mc->invocation);
    }
    if (this->astKind == ASTKind::MACRO_EXPAND_DECL) {
        auto mc = RawStaticCast<const MacroExpandDecl*>(this);
        return &(mc->invocation);
    }
    if (this->astKind == ASTKind::MACRO_EXPAND_PARAM) {
        auto mc = RawStaticCast<const MacroExpandParam*>(this);
        return &(mc->invocation);
    }
    return nullptr;
}

bool MacroInvocation::IsIfAvailable() const
{
    return macroCallDiagInfo.fullName == IF_AVAILABLE;
}

Ptr<FuncArg> MacroExpandExpr::GetNamedArg() const
{
    if (invocation.IsIfAvailable() && !invocation.nodes.empty()) {
        return StaticCast<FuncArg>(invocation.nodes[0].get());
    }
    return {};
}
Ptr<LambdaExpr> MacroExpandExpr::GetLambda(size_t i) const
{
    if (invocation.IsIfAvailable() && invocation.nodes.size() > i) {
        return StaticCast<LambdaExpr>(invocation.nodes[i + 1].get());
    }
    return {};
}
std::tuple<OwnedPtr<FuncArg>, OwnedPtr<LambdaExpr>, OwnedPtr<LambdaExpr>> MacroExpandExpr::Decompose()
{
    return {OwnedPtr<FuncArg>(StaticCast<FuncArg>(std::move(invocation.nodes[0].release()))),
        OwnedPtr<LambdaExpr>(StaticCast<LambdaExpr>(std::move(invocation.nodes[1].release()))),
        OwnedPtr<LambdaExpr>(StaticCast<LambdaExpr>(std::move(invocation.nodes[2].release())))};
}

/**
 * Get a MacroInvocation ptr.
 * @return MacroInvocation ptr if a node is MacroExpandExpr or MacroExpandDecl,
 *  nullptr otherwise.
 */
Ptr<MacroInvocation> Node::GetInvocation()
{
    if (this->astKind == ASTKind::MACRO_EXPAND_EXPR) {
        auto mc = RawStaticCast<MacroExpandExpr*>(this);
        return &(mc->invocation);
    }
    if (this->astKind == ASTKind::MACRO_EXPAND_DECL) {
        auto mc = RawStaticCast<MacroExpandDecl*>(this);
        return &(mc->invocation);
    }
    if (this->astKind == ASTKind::MACRO_EXPAND_PARAM) {
        auto mc = RawStaticCast<MacroExpandParam*>(this);
        return &(mc->invocation);
    }
    return nullptr;
}

/**
 * Get the new Position of macrocall in curfile by originPos before the macro is expanded, for lsp.
 * @return new Position of macrocall in curfile if the Node is MacroExpandExpr/MacroExpandDecl or in macrocall,
 *  INVALID_POSITION otherwise.
 */
Position Node::GetMacroCallNewPos(const Position& originPos)
{
    Ptr<MacroInvocation> pInvocation = nullptr;
    if (this->isInMacroCall && this->curMacroCall) {
        Ptr<Node> tempNode = this->curMacroCall;
        // Get outermost macrocall.
        while (tempNode->curMacroCall) {
            tempNode = tempNode->curMacroCall;
        }
        pInvocation = tempNode->GetInvocation();
    }
    if (this->IsMacroCallNode()) {
        Ptr<Node> tempNode = this;
        // Get outermost macrocall.
        while (tempNode->curMacroCall) {
            tempNode = tempNode->curMacroCall;
        }
        pInvocation = tempNode->GetInvocation();
    }
    if (!pInvocation || pInvocation->macroCallDiagInfo.originPosMap.empty() || pInvocation->origin2newPosMap.empty()) {
        return INVALID_POSITION;
    }
    auto key = static_cast<unsigned int>(originPos.Hash32());
    if (pInvocation->macroCallDiagInfo.originPosMap.lower_bound(key) ==
        pInvocation->macroCallDiagInfo.originPosMap.end()) {
        return INVALID_POSITION;
    }
    auto newkey = pInvocation->macroCallDiagInfo.originPosMap.lower_bound(key)->second.Hash64();
    if (pInvocation->origin2newPosMap.find(newkey) != pInvocation->origin2newPosMap.cend()) {
        return pInvocation->origin2newPosMap.at(newkey);
    }
    return INVALID_POSITION;
}

/**
 * Get the sourcePos of macrocall by originPos in curfile.
 * @return the sourcePos in macrocall file if the Node is expanded from macrocall,
 *  originPos in curfile otherwise.
 */
Position Node::GetMacroCallPos(Position originPos, bool isLowerBound) const
{
    if (this->curMacroCall) {
        // If originPos is not from macro expansion, return it.
        if (originPos.line != this->curMacroCall->begin.line) {
            return originPos;
        }
        auto pInvocation = this->curMacroCall->GetConstInvocation();
        if (pInvocation && !IsPureAnnotation(*pInvocation)) {
            return GetMacroSourcePos(*pInvocation, originPos, isLowerBound);
        }
    }
    auto pInvocation = this->GetConstInvocation();
    if (pInvocation) {
        if (this->begin.fileID != originPos.fileID) {
            // The original position and macrocall are not in the same file.
            return originPos;
        }
        // The original position and macrocall should be in the same file.
        return GetMacroSourcePos(*pInvocation, originPos, isLowerBound);
    }
    return originPos;
}

/**
 * Get the begin Position of the Node.
 * @return begin Position in macrocall file if the Node is expanded from macrocall,
 *  begin position in curfile otherwise.
 */
Position Node::GetBegin() const
{
    return this->GetMacroCallPos(this->begin);
}

/**
 * Get the end Position of the Node.
 * @return end Position in macrocall file if the Node is expanded from macrocall,
 *  end position in curfile otherwise.
 */
Position Node::GetEnd() const
{
    auto beginPos = this->GetMacroCallPos(this->begin);
    auto endPos = this->GetMacroCallPos(this->end, true);
    // The fileID of position may be different, may come from the macro definition or from the macrocall.
    if (beginPos.fileID != endPos.fileID) {
        endPos = beginPos + 1;
    }
    return endPos;
}

size_t NameReferenceExpr::OuterArgSize() const
{
    if (auto ce = DynamicCast<CallExpr*>(callOrPattern)) {
        return ce->args.size();
    } else if (auto pat = DynamicCast<EnumPattern*>(callOrPattern)) {
        return pat->patterns.size();
    }
    return 0;
}

/**
 * Get the field Position of the MemberAccess.
 * @return field Position in macrocall file if the MemberAccess is expanded from macrocall,
 *  field position in curfile otherwise.
 */
Position MemberAccess::GetFieldPos() const
{
    return this->GetMacroCallPos(this->field.Begin());
}

/**
 * Get the identifier Position of the RefExpr.
 * @return identifier Position in macrocall file if the RefExpr is expanded from macrocall,
 *  identifier position in curfile otherwise.
 */
Position RefExpr::GetIdentifierPos() const
{
    return this->GetMacroCallPos(this->ref.identifier.Begin());
}

/**
 * Get the field Position of the QualifiedType.
 * @return field Position in macrocall file if the QualifiedType is expanded from macrocall,
 *  field position in curfile otherwise.
 */
Position QualifiedType::GetFieldPos() const
{
    return this->GetMacroCallPos(this->field.Begin());
}

bool Decl::IsBuiltIn() const
{
    return astKind == ASTKind::BUILTIN_DECL;
}

/**
 * Get the identifier Position of the Decl.
 * @return identifier Position in macrocall file if the Decl is expanded from macrocall,
 *  identifier position in curfile otherwise.
 */
Position Decl::GetIdentifierPos() const
{
    return this->GetMacroCallPos(this->identifier.Begin());
}

Ptr<Generic> Decl::GetGeneric() const
{
    if (auto fd = DynamicCast<const FuncDecl*>(this); fd && fd->funcBody) {
        if (fd->funcBody->generic) {
            return fd->funcBody->generic.get();
        } else if (fd->funcBody->parentEnum != nullptr && fd->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            return fd->funcBody->parentEnum->generic.get();
        }
        return nullptr;
    }
    if (auto vd = DynamicCast<const VarDecl*>(this);
        vd && vd->outerDecl && vd->outerDecl->astKind == ASTKind::ENUM_DECL) {
        return vd->outerDecl->generic.get();
    }
    return generic.get();
}

bool Decl::IsExportedDecl() const
{
    if (TestAnyAttr(Attribute::PUBLIC, Attribute::PROTECTED)) {
        return true;
    }
    if (TestAttr(Attribute::INTERNAL)) {
        if (curFile && curFile->curPackage) {
            return !curFile->curPackage->noSubPkg;
        } else {
            return true;
        }
    }
    if (TestAttr(Attribute::PRIVATE)) {
        return false;
    }
    // When the decl is `extend A<B>`, B may be decl without modifiers such as GenericParamDecl, BuiltinDecl.
    // In this case, they must be exported for extend's extendType checking.
    return true;
}

bool Decl::IsConst() const
{
    if (auto vd = DynamicCast<const VarDeclAbstract*>(this); vd) {
        return vd->isConst;
    } else if (auto fd = DynamicCast<const FuncDecl*>(this); fd) {
        return fd->isConst;
    } else if (auto pcd = DynamicCast<const PrimaryCtorDecl*>(this); pcd) {
        return pcd->isConst;
    }
    return false;
}

Ptr<FuncDecl> Decl::GetDesugarDecl() const
{
    if (auto macroDecl = DynamicCast<const MacroDecl*>(this); macroDecl) {
        return macroDecl->desugarDecl.get();
    } else if (auto mainDecl = DynamicCast<const MainDecl*>(this); mainDecl) {
        return mainDecl->desugarDecl.get();
    } else if (auto funcParam = DynamicCast<const FuncParam*>(this); funcParam) {
        return funcParam->desugarDecl.get();
    }
    return nullptr;
}

bool Decl::IsCommonOrSpecific() const
{
    return TestAttr(AST::Attribute::COMMON) || TestAttr(AST::Attribute::SPECIFIC);
}

bool Decl::IsCommonMatchedWithSpecific() const
{
    return TestAttr(AST::Attribute::COMMON) && specificImplementation;
}

/**
 * For a generic declaration finds generic parameters and returns them
 * @return the number of generic parameters or 0 if not applicable.
 */
size_t Decl::GetGenericsCount() const
{
    if (!TestAttr(Attribute::GENERIC)) { // fast path
        return 0;
    }
    auto genericNode = GetGeneric();
    if (!genericNode) {
        return 0;
    }
    return genericNode->typeParameters.size();
}

/**
 * For debug, get the original Position of the node if it is from MacroCall in curfile, curPos otherwise.
 */
Position Node::GetDebugPos(const Position& curPos) const
{
    auto pInvocation = this->GetConstInvocation();
    if (!pInvocation) {
        // If the node is not macrocall node, then check whether it is expanded from macrocall node or not.
        if (curPos == INVALID_POSITION || !this->curMacroCall) {
            return curPos;
        }
        // Current node is expanded from macrocall node.
        pInvocation = this->curMacroCall->GetConstInvocation();
        if (!pInvocation || pInvocation->macroDebugMap.empty()) {
            return curPos;
        }
    }
    auto key = static_cast<unsigned int>(curPos.column);
    if (pInvocation->macroDebugMap.find(key) == pInvocation->macroDebugMap.end()) {
        return curPos;
    }
    return pInvocation->macroDebugMap.find(key)->second;
}

const std::string& Node::GetFullPackageName() const
{
    if (auto decl = DynamicCast<Decl>(this); decl && !decl->fullPackageName.empty()) {
        return decl->fullPackageName;
    }
    if (curFile && curFile->curPackage) {
        return curFile->curPackage->fullPackageName;
    }
    return EMPTY_PACKAGE_NAME;
}

std::string PackageSpec::GetPackageName() const
{
    std::stringstream ss;
    for (size_t i{0}; i < prefixPaths.size(); ++i) {
        ss << prefixPaths[i];
        if (i == 0 && hasDoubleColon) {
            ss << TOKENS[static_cast<int>(TokenKind::DOUBLE_COLON)];
        } else {
            ss << TOKENS[static_cast<int>(TokenKind::DOT)];
        }
    }
    ss << packageName.Val();
    return ss.str();
}

std::string ImportContent::GetPrefixPath() const
{
    std::stringstream ss;
    for (size_t i{0}; i < prefixPaths.size(); ++i) {
        ss << prefixPaths[i];
        if (i == prefixPaths.size() - 1) {
            break;
        }
        if (i == 0 && hasDoubleColon) {
            // valid import do not end with ::
            ss << TOKENS[static_cast<int>(TokenKind::DOUBLE_COLON)];
        } else {
            ss << TOKENS[static_cast<int>(TokenKind::DOT)];
        }
    }
    return ss.str();
}

std::string ImportContent::GetImportedPackageName() const
{
    std::stringstream ss;
    for (size_t i{0}; i < prefixPaths.size(); ++i) {
        ss << prefixPaths[i];
        // do not add . if this is the last of import xxx.*, because * is not part of package name
        if (kind == ImportKind::IMPORT_ALL && i + 1 == prefixPaths.size()) {
            continue;
        }
        if (i == 0 && hasDoubleColon) {
            ss << TOKENS[static_cast<int>(TokenKind::DOUBLE_COLON)];
        } else {
            ss << TOKENS[static_cast<int>(TokenKind::DOT)];
        }
    }
    if (kind != ImportKind::IMPORT_ALL) {
        ss << identifier.Val();
    }
    return ss.str();
}

std::string ImportContent::GetImportedPackageNameWithIsDecl() const
{
    std::stringstream ss;
    for (size_t i{0}; i < prefixPaths.size(); ++i) {
        ss << prefixPaths[i];
        // do not add . if this is the last of import xxx.*, because * is not part of package name
        if (kind == ImportKind::IMPORT_ALL && i + 1 == prefixPaths.size()) {
            continue;
        }
        if (i == 0 && hasDoubleColon) {
            ss << TOKENS[static_cast<int>(TokenKind::DOUBLE_COLON)];
        } else if (i + 1 != prefixPaths.size()) {
            ss << TOKENS[static_cast<int>(TokenKind::DOT)];
        }
    }
    if (kind != ImportKind::IMPORT_ALL && !isDecl) {
        ss << TOKENS[static_cast<int>(TokenKind::DOT)] << identifier.Val();
    }
    return ss.str();
}

std::vector<std::string> ImportContent::GetPossiblePackageNames() const
{
    // Multi-imports are desugared after parser which should not be used for get package name.
    CJC_ASSERT(kind != ImportKind::IMPORT_MULTI);
    if (prefixPaths.empty()) {
        return {identifier};
    }
    std::stringstream ss;
    for (size_t i{0}; i < prefixPaths.size(); ++i) {
        ss << prefixPaths[i];
        // do not add . if this is the last of import xxx.*, because * is not part of package name
        if (i + 1 == prefixPaths.size()) {
            continue;
        }
        if (i == 0 && hasDoubleColon) {
            ss << TOKENS[static_cast<int>(TokenKind::DOUBLE_COLON)];
        } else {
            ss << TOKENS[static_cast<int>(TokenKind::DOT)];
        }
    }
    if (kind == ImportKind::IMPORT_ALL) {
        return {ss.str()};
    }
    if (hasDoubleColon && prefixPaths.size() == 1) {
        ss << TOKENS[static_cast<int>(TokenKind::DOUBLE_COLON)] << identifier.Val();
        return {ss.str()};
    }
    if (prefixPaths.empty()) {
        return {identifier.Val()};
    }
    // this order is important for resolving imported names
    return {ss.str() + std::string{TOKENS[static_cast<int>(TokenKind::DOT)]} + identifier.Val(), ss.str()};
}

std::string ImportContent::ToString() const
{
    std::function<void(std::stringstream&, const ImportContent&)> toString = [](auto& ss, auto& content) {
        for (size_t i{0}; i < content.prefixPaths.size(); ++i) {
            ss << content.prefixPaths[i];
            if (i == 0 && content.hasDoubleColon) {
                ss << TOKENS[static_cast<int>(TokenKind::DOUBLE_COLON)];
            } else {
                ss << TOKENS[static_cast<int>(TokenKind::DOT)];
            }
        }
        if (content.kind != ImportKind::IMPORT_MULTI) {
            ss << content.identifier.Val();
            if (content.kind == ImportKind::IMPORT_ALIAS) {
                ss << " as " << content.aliasName.Val();
            }
        }
        return ss.str();
    };
    std::stringstream ss;
    toString(ss, *this);
    if (kind != ImportKind::IMPORT_MULTI) {
        return ss.str();
    }
    ss << "{" << std::endl;
    for (const auto& item : items) {
        ss << "    ";
        toString(ss, item);
        ss << std::endl;
    }
    ss << "}";
    return ss.str();
}

std::string FeatureId::ToString() const
{
    std::stringstream ss;
    size_t idx = 0;
    for (const auto& ident : this->identifiers) {
        ss << ident.Val();
        if (idx < dotPoses.size()) {
            ss << ".";
            idx++;
        }
    }
    return ss.str();
}

bool ExtendDecl::IsExportedDecl() const
{
    // ExtendedType Check (Direct and Interface Extensions): If B in extend A<B> isn't exported, the extendDecl should
    // not be exported. For imported decl, extendedType may be nullptr and not ready.
    if (extendedType != nullptr) {
        for (auto& it : extendedType->GetTypeArgs()) {
            if (it && it->GetTarget() && !it->GetTarget()->IsExportedDecl()) {
                return false;
            }
        }
    }
    auto extendedDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(GetTy());
    bool isInSamePkg = extendedDecl && extendedDecl->fullPackageName == fullPackageName;
    auto isUpperBoundExport = [this]() {
        bool isUpperboundAllExported = true;
        for (auto& tp : generic->genericConstraints) {
            CJC_NULLPTR_CHECK(tp);
            for (auto& up : tp->upperBounds) {
                CJC_NULLPTR_CHECK(up);
                if (up->GetTarget() == nullptr) {
                    continue;
                }
                isUpperboundAllExported = up->GetTarget()->IsExportedDecl() && isUpperboundAllExported;
            }
        }
        return isUpperboundAllExported;
    };
    // Direct Extensions Check:
    if (inheritedTypes.empty()) {
        // Rule 1: In `package std.core`, direct extensions of types visible outside the package are exported.
        if (fullPackageName == "std.core") {
            return true;
        }
        if (isInSamePkg) {
            // Rule 2: When direct extensions are defined in the same `package` as the extended type, whether the
            // extension is exported is determined by the lowest access level of the type used in the extended type and
            // the generic constraints (if any).
            if (!generic) {
                return extendedDecl->IsExportedDecl();
            }
            return extendedDecl->IsExportedDecl() && isUpperBoundExport();
        } else {
            // Rule 3: When the direct extension is in a different `package` from the declaration of the type being
            // extended, the extension is never exported and can only be used in the current `package`.
            return false;
        }
    }
    // Interface Extensions Check:
    if (isInSamePkg) {
        // Rule 1: When the interface extension and the extended type are in the same `package`, the extension is
        // exported together with the extended type and is not affected by the access level of the interface type.
        return extendedDecl->IsExportedDecl();
    }
    // Rule 2: When an interface extension is in a different `package` from the type being extended, whether the
    // interface extension is exported is determined by the smallest access level of the type used in the
    // interface type and the generic constraints (if any).
    bool isInterfaceAllExported = false;
    for (auto& inhertType : inheritedTypes) {
        if (inhertType->GetTarget() == nullptr) {
            continue;
        }
        if (inhertType->GetTarget()->IsExportedDecl()) {
            isInterfaceAllExported = true;
            break;
        }
    }
    if (!generic) {
        return isInterfaceAllExported;
    }
    return isInterfaceAllExported && isUpperBoundExport();
}

bool PropDecl::IsExportedDecl() const
{
    if (!outerDecl || outerDecl->astKind != ASTKind::EXTEND_DECL) {
        return Decl::IsExportedDecl();
    }
    auto extend = StaticCast<ExtendDecl>(outerDecl);
    auto extendedDecl = Ty::GetDeclPtrOfTy(extend->GetTy());
    // If extend and extended decleration in same package, all member of extend will be exported.
    if (!extendedDecl || extendedDecl->fullPackageName == extend->fullPackageName) {
        return Decl::IsExportedDecl();
    }
    // If extend is direct extension, all member will be not exported.
    if (extend->inheritedTypes.empty()) {
        return false;
    }
    return Decl::IsExportedDecl() && TestAttr(Attribute::INTERFACE_IMPL);
}

bool FuncDecl::IsExportedDecl() const
{
    if (!outerDecl || outerDecl->astKind != ASTKind::EXTEND_DECL) {
        return Decl::IsExportedDecl();
    }
    auto extend = StaticCast<ExtendDecl>(outerDecl);
    auto extendedDecl = Ty::GetDeclPtrOfTy(extend->GetTy());
    // If extend and extended decleration in same package, all member of extend will be exported.
    if (!extendedDecl || extendedDecl->fullPackageName == extend->fullPackageName) {
        return Decl::IsExportedDecl();
    }
    // If extend is direct extension, all member will be not exported.
    if (extend->inheritedTypes.empty()) {
        return false;
    }
    return Decl::IsExportedDecl() && TestAttr(Attribute::INTERFACE_IMPL);
}

bool FuncDecl::IsOpen() const noexcept
{
    if (!outerDecl || !outerDecl->IsOpen() || TestAttr(Attribute::STATIC)) {
        return false;
    }
    if (TestAnyAttr(Attribute::OPEN, Attribute::ABSTRACT)) {
        return true;
    }
    return !TestAttr(AST::Attribute::IMPORTED) && !funcBody->body;
}

bool PropDecl::IsOpen() const noexcept
{
    if (!outerDecl || !outerDecl->IsOpen() || TestAttr(Attribute::STATIC)) {
        return false;
    }
    if (TestAnyAttr(Attribute::OPEN, Attribute::ABSTRACT)) {
        return true;
    }
    return !TestAttr(AST::Attribute::IMPORTED) && getters.empty() && setters.empty();
}

std::string PrimitiveType::ToString() const
{
    if (!str.empty()) {
        return str;
    }
    return Ty::KindName(kind);
}

std::string ParenType::ToString() const
{
    CJC_ASSERT(type != nullptr);
    return "(" + type->ToString() + ")";
}

std::string QualifiedType::ToString() const
{
    std::string ret;
    CJC_ASSERT(baseType != nullptr);
    ret = baseType->ToString();
    ret += ".";
    ret += field.GetRawText();

    if (!typeArguments.empty()) {
        ret += "<";
        for (size_t i = 0; i < typeArguments.size(); ++i) {
            if (i > 0) {
                ret += ", ";
            }
            auto arg = typeArguments[i].get();
            CJC_ASSERT(arg != nullptr);
            ret += arg->ToString();
        }
        ret += ">";
    }
    return ret;
}

std::string OptionType::ToString() const
{
    std::string ret;
    size_t count = (questNum > 0) ? questNum : questVector.size();
    for (size_t i = 0; i < count; ++i) {
        ret += "?";
    }
    CJC_ASSERT(componentType != nullptr);
    ret += componentType->ToString();
    return ret;
}

std::string ConstantType::ToString() const
{
    CJC_ASSERT(constantExpr != nullptr);
    return "$" + constantExpr->ToString();
}

std::string VArrayType::ToString() const
{
    std::string ret = "VArray<";
    CJC_ASSERT(typeArgument != nullptr);
    ret += typeArgument->ToString();
    if (constantType != nullptr) {
        ret += ", " + constantType->ToString();
    }
    ret += ">";
    return ret;
}

std::string FuncType::ToString() const
{
    std::string ret = "(";
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        if (i > 0) {
            ret += ", ";
        }
        CJC_ASSERT(paramTypes[i] != nullptr);
        ret += paramTypes[i]->ToString();
    }
    ret += ")";
    if (retType != nullptr) {
        ret += " -> " + retType->ToString();
    }
    return ret;
}

std::string TupleType::ToString() const
{
    std::string ret = "(";
    for (size_t i = 0; i < fieldTypes.size(); ++i) {
        if (i > 0) {
            ret += ", ";
        }
        CJC_ASSERT(fieldTypes[i] != nullptr);
        ret += fieldTypes[i]->ToString();
    }
    ret += ")";
    return ret;
}

std::string ThisType::ToString() const
{
    return TOKENS[static_cast<int>(TokenKind::THISTYPE)];
}

std::string ParenExpr::ToString() const
{
    CJC_ASSERT(expr != nullptr);
    return "(" + expr->ToString() + ")";
}

std::string AsExpr::ToString() const
{
    CJC_ASSERT(leftExpr != nullptr);
    CJC_ASSERT(asType != nullptr);
    return leftExpr->ToString() + " as " + asType->ToString();
}

std::string IsExpr::ToString() const
{
    CJC_ASSERT(leftExpr != nullptr);
    CJC_ASSERT(isType != nullptr);
    return leftExpr->ToString() + " is " + isType->ToString();
}

std::string TypeConvExpr::ToString() const
{
    CJC_ASSERT(type != nullptr);
    CJC_ASSERT(expr != nullptr);
    return type->ToString() + "(" + expr->ToString() + ")";
}

std::string OptionalExpr::ToString() const
{
    CJC_ASSERT(baseExpr != nullptr);
    return baseExpr->ToString() + "?";
}

std::string OptionalChainExpr::ToString() const
{
    CJC_ASSERT(expr != nullptr);
    return expr->ToString();
}

std::string PrimitiveTypeExpr::ToString() const
{
    return Ty::KindName(typeKind);
}

std::string WildcardPattern::ToString() const
{
    return "_";
}

std::string ConstPattern::ToString() const
{
    CJC_NULLPTR_CHECK(literal);
    return literal->ToString();
}

std::string VarPattern::ToString() const
{
    if (varDecl) {
        return varDecl->identifier.Val();
    }
    return "";
}

std::string TuplePattern::ToString() const
{
    return "(" + JoinNodeStrings(patterns, ", ") + ")";
}

std::string TypePattern::ToString() const
{
    CJC_NULLPTR_CHECK(pattern);
    CJC_NULLPTR_CHECK(type);
    std::string ret = pattern->ToString();
    if (!ret.empty()) {
        ret += ": ";
    }
    ret += type->ToString();
    return ret;
}

std::string EnumPattern::ToString() const
{
    CJC_NULLPTR_CHECK(constructor);
    std::string ret = constructor->ToString();
    if (!patterns.empty()) {
        ret += "(" + JoinNodeStrings(patterns, ", ") + ")";
    }
    return ret;
}

std::string VarOrEnumPattern::ToString() const
{
    if (pattern) {
        return pattern->ToString();
    }
    return identifier.Val();
}

std::string ExceptTypePattern::ToString() const
{
    CJC_NULLPTR_CHECK(pattern);
    std::string ret = pattern->ToString();
    if (!types.empty()) {
        if (!ret.empty()) {
            ret += ": ";
        }
        ret += JoinNodeStrings(types, " | ");
    }
    return ret;
}

std::string CommandTypePattern::ToString() const
{
    CJC_NULLPTR_CHECK(pattern);
    std::string ret = pattern->ToString();
    if (!types.empty()) {
        if (!ret.empty()) {
            ret += ": ";
        }
        ret += JoinNodeStrings(types, " | ");
    }
    return ret;
}

std::string LetPatternDestructor::ToString() const
{
    std::string ret = "let ";
    ret += JoinNodeStrings(patterns, " | ");
    ret += " <- ";
    CJC_NULLPTR_CHECK(initializer);
    ret += initializer->ToString();
    return ret;
}

std::string Block::ToString() const
{
    std::string ret = "{";
    for (size_t i = 0; i < body.size(); ++i) {
        CJC_NULLPTR_CHECK(body[i]);
        ret += "\n" + body[i]->ToString();
    }
    if (!body.empty()) {
        ret += "\n";
    }
    ret += "}";
    return ret;
}

std::string JumpExpr::ToString() const
{
    return isBreak ? "break" : "continue";
}

std::string ReturnExpr::ToString() const
{
    if (expr) {
        return "return " + expr->ToString();
    }
    return "return";
}

std::string ThrowExpr::ToString() const
{
    CJC_NULLPTR_CHECK(expr);
    return "throw " + expr->ToString();
}

std::string PerformExpr::ToString() const
{
    CJC_NULLPTR_CHECK(expr);
    return "perform " + expr->ToString();
}

std::string ResumeExpr::ToString() const
{
    std::string ret = "resume";
    if (withExpr) {
        ret += " with " + withExpr->ToString();
    }
    if (throwingExpr) {
        ret += " throwing " + throwingExpr->ToString();
    }
    return ret;
}

std::string IfExpr::ToString() const
{
    std::string ret = "if (";
    CJC_NULLPTR_CHECK(condExpr);
    ret += condExpr->ToString();
    ret += ") ";
    CJC_NULLPTR_CHECK(thenBody);
    ret += thenBody->ToString();
    if (elseBody) {
        ret += " else " + elseBody->ToString();
    }
    return ret;
}

std::string WhileExpr::ToString() const
{
    CJC_NULLPTR_CHECK(condExpr);
    CJC_NULLPTR_CHECK(body);
    return "while (" + condExpr->ToString() + ") " + body->ToString();
}

std::string DoWhileExpr::ToString() const
{
    CJC_NULLPTR_CHECK(body);
    CJC_NULLPTR_CHECK(condExpr);
    return "do " + body->ToString() + " while (" + condExpr->ToString() + ")";
}

std::string ForInExpr::ToString() const
{
    CJC_NULLPTR_CHECK(pattern);
    CJC_NULLPTR_CHECK(inExpression);
    std::string ret = "for (" + pattern->ToString() + " in " + inExpression->ToString();
    if (patternGuard) {
        ret += " where " + patternGuard->ToString();
    }
    ret += ") ";
    CJC_NULLPTR_CHECK(body);
    ret += body->ToString();
    return ret;
}

std::string MatchCase::ToString() const
{
    std::string ret = "case " + JoinNodeStrings(patterns, " | ");
    if (patternGuard) {
        ret += " where " + patternGuard->ToString();
    }
    CJC_NULLPTR_CHECK(exprOrDecls);
    ret += " => " + exprOrDecls->ToString();
    return ret;
}

std::string MatchCaseOther::ToString() const
{
    CJC_NULLPTR_CHECK(matchExpr);
    CJC_NULLPTR_CHECK(exprOrDecls);
    return "case " + matchExpr->ToString() + " => " + exprOrDecls->ToString();
}

std::string MatchExpr::ToString() const
{
    std::string ret = "match (";
    CJC_NULLPTR_CHECK(selector);
    ret += selector->ToString();
    ret += ") {";
    for (auto& mc : matchCases) {
        CJC_NULLPTR_CHECK(mc);
        ret += "\n" + mc->ToString();
    }
    for (auto& mco : matchCaseOthers) {
        CJC_NULLPTR_CHECK(mco);
        ret += "\n" + mco->ToString();
    }
    if (!matchCases.empty() || !matchCaseOthers.empty()) {
        ret += "\n";
    }
    ret += "}";
    return ret;
}

std::string TryExpr::ToString() const
{
    std::string ret = "try ";
    if (!resourceSpec.empty()) {
        ret += "(" + JoinNodeStrings(resourceSpec, ", ") + ") ";
    }
    CJC_NULLPTR_CHECK(tryBlock);
    ret += tryBlock->ToString();
    for (size_t i = 0; i < catchBlocks.size(); ++i) {
        ret += " catch(";
        if (i < catchPatterns.size() && catchPatterns[i]) {
            ret += catchPatterns[i]->ToString();
        }
        ret += ") ";
        CJC_NULLPTR_CHECK(catchBlocks[i]);
        ret += catchBlocks[i]->ToString();
    }
    for (auto& handler : handlers) {
        ret += " handle(";
        if (handler.commandPattern) {
            ret += handler.commandPattern->ToString();
        }
        ret += ") ";
        if (handler.block) {
            ret += handler.block->ToString();
        }
    }
    if (finallyBlock) {
        ret += " finally " + finallyBlock->ToString();
    }
    return ret;
}

std::string SpawnExpr::ToString() const
{
    std::string ret = "spawn";
    if (arg) {
        ret += "(" + arg->ToString() + ")";
    }
    CJC_NULLPTR_CHECK(task);
    ret += " " + task->ToString();
    return ret;
}

std::string SynchronizedExpr::ToString() const
{
    CJC_NULLPTR_CHECK(mutex);
    CJC_NULLPTR_CHECK(body);
    return "synchronized (" + mutex->ToString() + ") " + body->ToString();
}

std::string GenericParamDecl::ToString() const
{
    return identifier.Val();
}

std::string GenericConstraint::ToString() const
{
    CJC_NULLPTR_CHECK(type);
    std::string ret = type->ToString();
    if (!upperBounds.empty()) {
        ret += " <: " + JoinNodeStrings(upperBounds, " & ");
    }
    return ret;
}

std::string Generic::ToString() const
{
    if (typeParameters.empty()) {
        return "";
    }
    return "<" + JoinNodeStrings(typeParameters, ", ") + ">";
}

std::string FuncParam::ToString() const
{
    std::string ret = identifier.Val();
    if (isNamedParam) {
        ret += "!";
    }
    if (type) {
        ret += ": " + type->ToString();
    }
    return ret;
}

std::string FuncParamList::ToString() const
{
    return "(" + JoinNodeStrings(params, ", ") + ")";
}

std::string FuncBody::ToString() const
{
    std::string ret;
    if (generic) {
        ret += generic->ToString();
    }
    for (size_t i = 0; i < paramLists.size(); ++i) {
        if (i > 0) {
            ret += " ";
        }
        CJC_NULLPTR_CHECK(paramLists[i]);
        ret += paramLists[i]->ToString();
    }
    if (retType) {
        if (!colonPos.IsZero()) {
            ret += ": ";
        } else if (!doubleArrowPos.IsZero()) {
            ret += " => ";
        }
        ret += retType->ToString();
    }
    if (generic && !generic->genericConstraints.empty()) {
        ret += " where " + JoinNodeStrings(generic->genericConstraints, ", ");
    }
    if (body) {
        ret += " " + body->ToString();
    }
    return ret;
}

std::string FuncDecl::ToString() const
{
    std::string ret;
    for (auto& modifier : modifiers) {
        auto modStr = modifier.ToString();
        if (!modStr.empty()) {
            ret += modStr + " ";
        }
    }
    ret += "func " + identifier.Val();
    if (funcBody) {
        ret += funcBody->ToString();
    }
    return ret;
}

std::string LambdaExpr::ToString() const
{
    CJC_NULLPTR_CHECK(funcBody);
    return "{ " + funcBody->ToString() + " }";
}

std::string TrailingClosureExpr::ToString() const
{
    CJC_NULLPTR_CHECK(expr);
    std::string ret = expr->ToString();
    if (lambda) {
        ret += " " + lambda->ToString();
    }
    return ret;
}

std::string ClassBody::ToString() const
{
    std::string ret = "{";
    for (auto& decl : decls) {
        CJC_NULLPTR_CHECK(decl);
        ret += "\n" + decl->ToString();
    }
    if (!decls.empty()) {
        ret += "\n";
    }
    ret += "}";
    return ret;
}

std::string StructBody::ToString() const
{
    std::string ret = "{";
    for (auto& decl : decls) {
        CJC_NULLPTR_CHECK(decl);
        ret += "\n" + decl->ToString();
    }
    if (!decls.empty()) {
        ret += "\n";
    }
    ret += "}";
    return ret;
}

std::string InterfaceBody::ToString() const
{
    std::string ret = "{";
    for (auto& decl : decls) {
        CJC_NULLPTR_CHECK(decl);
        ret += "\n" + decl->ToString();
    }
    if (!decls.empty()) {
        ret += "\n";
    }
    ret += "}";
    return ret;
}

std::string TypeAliasDecl::ToString() const
{
    std::string ret;
    for (auto& modifier : modifiers) {
        auto modStr = modifier.ToString();
        if (!modStr.empty()) {
            ret += modStr + " ";
        }
    }
    ret += "type " + identifier.Val();
    if (generic) {
        ret += generic->ToString();
    }
    CJC_NULLPTR_CHECK(type);
    ret += " = " + type->ToString();
    return ret;
}

std::string InvalidType::ToString() const
{
    return "<invalid>";
}

std::string InvalidExpr::ToString() const
{
    return value.empty() ? std::string("<invalid>") : value;
}

std::string InvalidDecl::ToString() const
{
    return "<invalid>";
}

std::string InvalidPattern::ToString() const
{
    return "<invalid>";
}

std::string TokenPart::ToString() const
{
    std::string ret;
    for (auto& tok : tokens) {
        ret += tok.Value();
    }
    return ret;
}

std::string QuoteExpr::ToString() const
{
    std::string ret = "quote(";
    ret += JoinNodeStrings(exprs, ", ");
    ret += ")";
    return ret;
}

std::string InterpolationExpr::ToString() const
{
    return rawString;
}

std::string StrInterpolationExpr::ToString() const
{
    return rawString;
}

std::string MacroExpandExpr::ToString() const
{
    std::string ret = "@" + identifier.Val();
    if (!invocation.attrs.empty()) {
        ret += "[";
        for (size_t i = 0; i < invocation.attrs.size(); ++i) {
            if (i > 0) {
                ret += ", ";
            }
            ret += invocation.attrs[i].Value();
        }
        ret += "]";
    }
    if (invocation.hasParenthesis) {
        ret += "(";
        for (size_t i = 0; i < invocation.args.size(); ++i) {
            if (i > 0) {
                ret += ", ";
            }
            ret += invocation.args[i].Value();
        }
        ret += ")";
    }
    if (!invocation.nodes.empty()) {
        ret += " { ";
        for (auto& node : invocation.nodes) {
            if (node) {
                ret += node->ToString();
            }
        }
        ret += " }";
    }
    return ret;
}

std::string MacroExpandDecl::ToString() const
{
    return "@" + identifier.Val();
}

std::string MacroDecl::ToString() const
{
    std::string ret;
    for (auto& modifier : modifiers) {
        auto modStr = modifier.ToString();
        if (!modStr.empty()) {
            ret += modStr + " ";
        }
    }
    ret += "macro " + identifier.Val();
    if (funcBody) {
        ret += funcBody->ToString();
    }
    return ret;
}

std::string MainDecl::ToString() const
{
    std::string ret;
    for (auto& modifier : modifiers) {
        auto modStr = modifier.ToString();
        if (!modStr.empty()) {
            ret += modStr + " ";
        }
    }
    ret += "main";
    if (funcBody) {
        ret += funcBody->ToString();
    }
    return ret;
}

std::string ImportSpec::ToString() const
{
    std::string ret = "import ";
    if (modifier) {
        auto modStr = modifier->ToString();
        if (!modStr.empty()) {
            ret += modStr + " ";
        }
    }
    ret += content.ToString();
    return ret;
}

std::string PackageSpec::ToString() const
{
    return "package " + GetPackageName();
}

std::string PackageDecl::ToString() const
{
    return identifier.Val();
}

std::string File::ToString() const
{
    std::string ret;
    if (package) {
        ret += package->ToString() + "\n";
    }
    for (auto& imp : imports) {
        CJC_NULLPTR_CHECK(imp);
        ret += imp->ToString() + "\n";
    }
    for (auto& decl : decls) {
        CJC_NULLPTR_CHECK(decl);
        ret += decl->ToString() + "\n";
    }
    return ret;
}

std::string Package::ToString() const
{
    std::string ret;
    for (auto& file : files) {
        CJC_NULLPTR_CHECK(file);
        ret += file->ToString();
    }
    return ret;
}

} // namespace Cangjie
