// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the AST utils interface.
 */

#include "cangjie/AST/Utils.h"

#include <mutex>
#include <deque>
#include <sstream>

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/Utils.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "cangjie/Utils/FloatFormat.h"
#include "cangjie/Utils/StdUtils.h"
#include "cangjie/Utils/Utils.h"

namespace Cangjie::AST {
void AddCurFile(Node& root, Ptr<File> file)
{
    Ptr<File> curFile = file;
    unsigned walkerID = Walker::GetNextWalkerID();
    /**
     * A set that stores all visited nodes to avoid multiple visits to the same node.
     */
    std::function<VisitAction(Ptr<Node>)> setCurFile = [&curFile, &setCurFile, walkerID](Ptr<Node> node) {
        switch (node->astKind) {
            case ASTKind::PACKAGE: {
                auto package = RawStaticCast<Package*>(node);
                for (auto& it : package->files) {
                    Walker(it.get(), walkerID, setCurFile).Walk();
                }
                return VisitAction::STOP_NOW;
            }
            case ASTKind::FILE: {
                curFile = RawStaticCast<File*>(node);
                curFile->curFile = curFile;
                for (auto& import : curFile->imports) {
                    import->curFile = curFile;
                }
                return VisitAction::WALK_CHILDREN;
            }
            default:
                if (node->curFile == nullptr) {
                    node->curFile = curFile;
                }
                // For diag after macro expansion.
                if (node->TestAttr(Attribute::MACRO_EXPANDED_NODE) && node->curMacroCall) {
                    AddMacroAttr(*node);
                    return VisitAction::SKIP_CHILDREN;
                }
                return VisitAction::WALK_CHILDREN;
        }
    };
    Walker(&root, walkerID, setCurFile).Walk();
}

/**
 * Add Attribute and curfile in macro expanded node.
 */
void AddMacroAttr(AST::Node& node)
{
    if (!node.TestAttr(Attribute::MACRO_EXPANDED_NODE) || !node.curMacroCall) {
        return;
    }
    auto macroCall = node.curMacroCall;
    auto pInvocation = macroCall->GetInvocation();
    if (!pInvocation) {
        return;
    }
    auto curFile = node.curFile;
    if (IsPureAnnotation(*pInvocation)) {
        Walker walker(&node, [curFile](auto curNode) -> VisitAction {
            curNode->curFile = curFile;
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
        return;
    }
    Walker walker(&node, [macroCall, curFile](Ptr<Node> curNode) -> VisitAction {
        curNode->EnableAttr(Attribute::MACRO_EXPANDED_NODE);
        curNode->curMacroCall = macroCall;
        curNode->curFile = curFile;
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
}

std::vector<Ptr<const AST::Modifier>> SortModifierByPos(const std::set<AST::Modifier>& modifiers)
{
    std::vector<Ptr<const AST::Modifier>> modifiersVec{};
    (void)std::for_each(modifiers.begin(), modifiers.end(), [&](auto& mod) { modifiersVec.push_back(&mod); });
    std::sort(
        modifiersVec.begin(), modifiersVec.end(), [](auto& mod1, auto& mod2) { return mod1->begin < mod2->begin; });
    return modifiersVec;
}

bool IsMemberParam(const Decl& decl)
{
    return decl.astKind == AST::ASTKind::FUNC_PARAM && StaticCast<AST::FuncParam>(decl).isMemberParam;
}

bool IsSingleRuneStringLiteral(const Expr& expr)
{
    if (auto lit = DynamicCast<LitConstExpr*>(&expr);
        lit && lit->kind == LitConstKind::STRING && lit->codepoint.size() == 1) {
        return true;
    }
    return false;
}

bool IsSingleByteStringLiteral(const Expr& expr)
{
    if (IsSingleRuneStringLiteral(expr) &&
        StaticCast<LitConstExpr&>(expr).codepoint.front() <= std::numeric_limits<uint8_t>::max()) {
        return true;
    }
    return false;
}

Ptr<AST::FuncDecl> GetSizeDecl(const AST::Ty& ty)
{
    if (!ty.IsClassLike()) {
        return nullptr;
    }
    auto decl = Ty::GetDeclOfTy(&ty);
    CJC_ASSERT(decl);
    for (auto& member : decl->GetMemberDecls()) {
        if (auto pd = DynamicCast<PropDecl*>(member.get());
            pd && !pd->getters.empty() && member->identifier == "size") {
            return pd->getters[0].get();
        }
    }
    return nullptr;
}

std::optional<AST::Attribute> HasJavaAttr(const AST::Node& node) noexcept
{
    if (node.TestAttr(Attribute::JAVA_APP)) {
        return {Attribute::JAVA_APP};
    }
    if (node.TestAttr(Attribute::JAVA_EXT)) {
        return {Attribute::JAVA_EXT};
    }
    return std::nullopt;
}

namespace {
Ptr<PrimitiveTy> GetPrimitiveUpperBoundTy(Ty& ty)
{
    if (ty.IsPrimitive()) {
        return RawStaticCast<PrimitiveTy*>(&ty);
    }
    if (!ty.IsGeneric()) {
        return nullptr;
    }
    auto& genericTy = static_cast<GenericsTy&>(ty);
    if (genericTy.lowerBound != nullptr && genericTy.lowerBound->IsPrimitive()) {
        return RawStaticCast<PrimitiveTy*>(genericTy.lowerBound);
    }
    return nullptr;
}

const std::map<std::pair<uint64_t, TypeKind>, TypeKind> NATIVE_TYPEKIND_MAP{
    {{32, TypeKind::TYPE_INT_NATIVE}, TypeKind::TYPE_INT32},
    {{64, TypeKind::TYPE_INT_NATIVE}, TypeKind::TYPE_INT64},
    {{32, TypeKind::TYPE_UINT_NATIVE}, TypeKind::TYPE_UINT32},
    {{64, TypeKind::TYPE_UINT_NATIVE}, TypeKind::TYPE_UINT64},
};
} // namespace

std::mutex g_floatTypeInfoMtx;
FloatTypeInfo GetFloatTypeInfoByKind(AST::TypeKind kind)
{
    const static std::unordered_map<AST::TypeKind, FloatTypeInfo> floatTypeInfoMap = {
        {AST::TypeKind::TYPE_FLOAT16, {0xF800, "5.960464477539063E-8", "6.5504E4"}},
        {AST::TypeKind::TYPE_FLOAT32, {0xFF000000, "1.40129846E-45", "3.40282347E38"}},
        {AST::TypeKind::TYPE_FLOAT64, {0xFFE0000000000000, "4.9406564584124654E-324", "1.7976931348623157E308"}},
        {AST::TypeKind::TYPE_IDEAL_FLOAT, {0xFFE0000000000000, "4.9406564584124654E-324", "1.7976931348623157E308"}}};
    std::lock_guard<std::mutex> guard(g_floatTypeInfoMtx);
    auto info = floatTypeInfoMap.find(kind);
    CJC_ASSERT(info != floatTypeInfoMap.end());
    return info->second;
}

void InitializeLitConstValue(LitConstExpr& lce)
{
    if (!Ty::IsTyCorrect(lce.ty)) {
        return;
    }
    // LitConstExpr is always a const expression.
    lce.isConst = true;
    // We don't need to handle the exception throwing from string-to-number api here,
    // because it's already been done in Parser phase.
    auto primitiveTy = GetPrimitiveUpperBoundTy(*lce.ty);
    if (primitiveTy == nullptr) {
        return;
    }
    if (primitiveTy->IsInteger()) {
        auto kind = primitiveTy->IsNative() ? NATIVE_TYPEKIND_MAP.at({primitiveTy->bitness, primitiveTy->kind})
                                            : primitiveTy->kind;
        auto stringVal = IsSingleByteStringLiteral(lce) ? std::to_string(lce.codepoint.front()) : lce.stringValue;
        lce.constNumValue.asInt.InitIntLiteral(stringVal, kind);
    } else if (primitiveTy->IsFloating()) {
        std::string stringValue = lce.stringValue;
        stringValue.erase(std::remove(stringValue.begin(), stringValue.end(), '_'), stringValue.end());
        if (auto val = Stold(stringValue)) {
            lce.constNumValue.asFloat.value = *val;
        } else {
            if (Cangjie::FloatFormat::IsUnderFlowFloat(stringValue)) {
                lce.constNumValue.asFloat.flowStatus = Expr::FlowStatus::UNDER;
                lce.constNumValue.asFloat.value = 0;
            } else {
                lce.constNumValue.asFloat.flowStatus = Expr::FlowStatus::OVER;
                uint64_t value = GetFloatTypeInfoByKind(AST::TypeKind::TYPE_FLOAT64).inf >> 1;
                lce.constNumValue.asFloat.value = reinterpret_cast<double&>(value);
            }
        }
    } else if (primitiveTy->IsBoolean()) {
        lce.constNumValue.asBoolean = lce.stringValue == "true";
    }
}

void SetOuterFunctionDecl(AST::Decl& decl)
{
    Ptr<AST::Node> root = nullptr;
    if (auto fd = DynamicCast<AST::FuncDecl*>(&decl)) {
        root = fd->funcBody.get();
    } else if (auto vd = DynamicCast<AST::VarDecl*>(&decl);
        vd && (vd->TestAttr(Attribute::GLOBAL) || (vd->outerDecl && vd->outerDecl->IsNominalDecl()))) {
        // As for decls in lambda expr, their outerDecl is lambda's left decl, may be a VarDecl(only global var or
        // member var). Because lambda is expr in AST, not a decl, so we can't set lambda as outerDecl.
        root = vd->initializer.get();
    }
    if (root == nullptr) {
        return;
    }
    auto visitor = [&decl](Ptr<Node> node) -> VisitAction {
        // `var d = { i => let temp = 1 }`, outerDecls of lambda param `i` and local var decl `temp` are both `d`
        if (auto fp = DynamicCast<FuncParam*>(node); fp) {
            fp->outerDecl = &decl;
            return VisitAction::SKIP_CHILDREN;
        } else if (auto funcDecl = DynamicCast<FuncDecl*>(node); funcDecl) {
            funcDecl->outerDecl = &decl;
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(root, visitor);
    walker.Walk();
}

bool IsInDeclWithAttribute(const Decl& decl, AST::Attribute attr)
{
    auto current = &decl;
    while (current != nullptr) {
        if (current->TestAttr(attr)) {
            return true;
        }
        if (auto fd = DynamicCast<const FuncDecl*>(current); fd && fd->ownerFunc && fd->ownerFunc->TestAttr(attr)) {
            return true; // Default param decl of a generic function is also treated as in generic.
        }
        current = current->outerDecl;
    }
    return false;
}

std::vector<Ptr<AST::Pattern>> FlattenVarWithPatternDecl(const AST::VarWithPatternDecl& vwpDecl)
{
    std::vector<Ptr<AST::Pattern>> result;
    std::deque<Ptr<AST::Pattern>> patterns;
    patterns.emplace_back(vwpDecl.irrefutablePattern.get());
    while (!patterns.empty()) {
        Ptr<AST::Pattern> pattern = patterns.front();
        patterns.pop_front();
        switch (pattern->astKind) {
            case AST::ASTKind::WILDCARD_PATTERN:
            case AST::ASTKind::VAR_PATTERN:
                result.emplace_back(pattern);
                break;
            case AST::ASTKind::TUPLE_PATTERN: {
                auto tuplePattern = StaticCast<AST::TuplePattern*>(pattern);
                for (size_t i = 0; i < tuplePattern->patterns.size(); i++) {
                    patterns.emplace_back(tuplePattern->patterns[i].get());
                }
                break;
            }
            case AST::ASTKind::ENUM_PATTERN: {
                auto enumPattern = StaticCast<AST::EnumPattern*>(pattern);
                for (size_t i = 0; i < enumPattern->patterns.size(); i++) {
                    patterns.emplace_back(enumPattern->patterns[i].get());
                }
                break;
            }
            default:
                break;
        }
    }
    return result;
}

std::string GetAnnotatedDeclKindString(const Decl& decl)
{
    switch (decl.astKind) {
        case ASTKind::CLASS_DECL:
        case ASTKind::INTERFACE_DECL:
        case ASTKind::STRUCT_DECL:
        case ASTKind::ENUM_DECL:
            return "type";
        case ASTKind::FUNC_DECL:
            if (decl.TestAttr(Attribute::CONSTRUCTOR)) {
                return "init";
            }
            CJC_ASSERT(decl.TestAnyAttr(Attribute::IN_CLASSLIKE, Attribute::IN_STRUCT, Attribute::IN_ENUM));
            return "member function";
        case ASTKind::PROP_DECL:
            return "member property";
        case ASTKind::VAR_DECL:
            CJC_ASSERT(decl.TestAnyAttr(Attribute::IN_CLASSLIKE, Attribute::IN_STRUCT, Attribute::IN_ENUM));
            return "member variable";
        case ASTKind::FUNC_PARAM:
            return "parameter";
        default:
            CJC_ABORT();
            return "";
    }
}

/**
 * Iterate all variables and functions in @p id.
 */
static void IterateAllMembersInTypeDecl(const InheritableDecl& id, const std::function<void(Decl&)> action)
{
    for (auto& decl : id.GetMemberDeclPtrs()) {
        if (decl->astKind == ASTKind::FUNC_DECL && !decl->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            action(*decl);
        } else if (decl->astKind == ASTKind::PROP_DECL) {
            auto propDecl = StaticAs<ASTKind::PROP_DECL>(decl.get());
            for (auto& funcDecl : propDecl->getters) {
                action(*funcDecl.get());
            }
            for (auto& funcDecl : propDecl->setters) {
                action(*funcDecl.get());
            }
        } else if (decl->astKind == ASTKind::VAR_DECL) {
            action(*decl);
        }
    }
}

void IterateAllExportableDecls(const Package& pkg, const std::function<void(Decl&)> action)
{
    for (auto& file : pkg.files) {
        for (auto& it : file->decls) {
            action(*it);
            if (auto id = DynamicCast<InheritableDecl>(it.get())) {
                IterateAllMembersInTypeDecl(*id, action);
            }
        }
    }
}

bool IsPackageMemberAccess(const AST::MemberAccess& ma)
{
    if (ma.baseExpr == nullptr) {
        return false;
    }
    Ptr<AST::Decl> target = ma.baseExpr->GetTarget();
    if (target == nullptr) {
        return false;
    }
    if (target->astKind == ASTKind::PACKAGE_DECL) {
        return true;
    }
    return false;
}
bool IsThisOrSuper(const AST::Expr& expr)
{
    if (auto re = DynamicCast<RefExpr>(&expr)) {
        return re->isThis || re->isSuper;
    }
    return false;
}

std::string GetImportedItemFullName(const ImportContent& content, const std::string& commonPrefix)
{
    std::stringstream ss;
    if (!commonPrefix.empty()) {
        ss << commonPrefix << ".";
    }
    ss << content.GetPrefixPath();
    if (content.kind == ImportKind::IMPORT_ALIAS || content.kind == ImportKind::IMPORT_SINGLE) {
        ss << "." << content.identifier.Val();
    }
    return ss.str();
}

bool IsCondition(const Expr& e)
{
    if (Is<LetPatternDestructor>(e)) {
        return true;
    }
    if (auto p = DynamicCast<ParenExpr>(&e)) {
        return IsCondition(*p->expr);
    }
    if (auto bin = DynamicCast<BinaryExpr>(&e); bin && (bin->op == TokenKind::AND || bin->op == TokenKind::OR)) {
        return IsCondition(*bin->leftExpr) || IsCondition(*bin->rightExpr);
    }
    return false;
}

bool DoesNotHaveEnumSubpattern(const LetPatternDestructor& let)
{
    for (auto& p : let.patterns) {
        if (auto enumPattern = DynamicCast<EnumPattern>(&*p); enumPattern && !enumPattern->patterns.empty()) {
            return false;
        }
    }
    return true;
}

#define ATTR_ACCESS_MAP \
    ATTR_WITH_LEVEL(Attribute::PRIVATE, AccessLevel::PRIVATE) \
    ATTR_WITH_LEVEL(Attribute::INTERNAL, AccessLevel::INTERNAL) \
    ATTR_WITH_LEVEL(Attribute::PROTECTED, AccessLevel::PROTECTED) \
    ATTR_WITH_LEVEL(Attribute::PUBLIC, AccessLevel::PUBLIC)

AccessLevel GetAccessLevel(const Node& node)
{
    static const std::array<std::pair<Attribute, AccessLevel>, 4> ATTRIBUTE_ACCESS_MAP = {
#define ATTR_WITH_LEVEL(ATTR, LEVEL) std::make_pair(ATTR, LEVEL),
        ATTR_ACCESS_MAP
#undef ATTR_WITH_LEVEL
    };
    auto level = node.astKind == ASTKind::IMPORT_SPEC ? AccessLevel::PRIVATE : AccessLevel::INTERNAL;
    for (const auto& e : ATTRIBUTE_ACCESS_MAP) {
        if (node.TestAttr(e.first)) {
            level = e.second;
            break;
        }
    }
    // When decl has parent type decl, real access level will not larger than parent decl's access level.
    if (auto decl = DynamicCast<Decl*>(&node); decl && decl->outerDecl && decl->outerDecl->IsNominalDecl()) {
        for (const auto& e : ATTRIBUTE_ACCESS_MAP) {
            if (decl->outerDecl->TestAttr(e.first) && e.second < level) {
                return e.second;
            }
        }
    }
    return level;
}

Attribute GetAttrByAccessLevel(AccessLevel level)
{
    static const std::unordered_map<AccessLevel, Attribute> ACCESS_TO_ATTR_MAP = {
#define ATTR_WITH_LEVEL(ATTR, LEVEL) {LEVEL, ATTR},
        ATTR_ACCESS_MAP
#undef ATTR_WITH_LEVEL
    };
    return ACCESS_TO_ATTR_MAP.at(level);
}

std::string GetAccessLevelStr(const AST::Node& node, const std::string& surround)
{
    static const std::array<std::pair<Attribute, std::string>, 4> ATTRIBUTE_ACCESS_MAP = {
        std::make_pair(Attribute::PRIVATE, "private"),
        std::make_pair(Attribute::INTERNAL, "internal"),
        std::make_pair(Attribute::PROTECTED, "protected"),
        std::make_pair(Attribute::PUBLIC, "public"),
    };
    for (const auto& e : ATTRIBUTE_ACCESS_MAP) {
        if (node.TestAttr(e.first)) {
            return surround.empty() ? e.second : surround + e.second + surround;
        }
    }
    return surround.empty() ? "private" : surround + "private" + surround;
}

std::string GetAccessLevelStr(const AST::Package& pkg)
{
    switch (pkg.accessible) {
        case AccessLevel::INTERNAL:
            return "internal";
        case AccessLevel::PROTECTED:
            return "protected";
        default:
            return "public";
    }
}

inline bool NeedPoint(const std::string& str)
{
    return str.size() > 0 && (isalpha(str.back()) || isdigit(str.back()));
}

void ExtractArgumentsOfDeprecatedAnno(
    const Ptr<AST::Annotation> annotation,
    std::string& message,
    std::string& since,
    bool& strict
)
{
    for (auto& arg : annotation->args) {
        if (auto lce = DynamicCast<AST::LitConstExpr*>(arg->expr.get()); lce) {
            if (arg->name == "" || arg->name == "message") {
                message = " " + lce->stringValue;
            } else if (arg->name == "since") {
                since = " since " + lce->stringValue;
            } else if (arg->name == "strict") {
                strict = lce->ToString() == "true";
            }
        }
    }

    if (NeedPoint(message)) {
        message += '.';
    }
    if (NeedPoint(since)) {
        since += '.';
    }
}

bool IsValidCFuncConstructorCall(const CallExpr& ce)
{
    // ce.ty is correct only when the whole CFunc constructor call is correct
    if (Ty::IsTyCorrect(ce.ty) && ce.baseFunc && Is<RefExpr>(ce.baseFunc)) {
        // if this is a builtin CFunc constructor call, do not check the arguments
        if (auto callee = DynamicCast<BuiltInDecl>(StaticCast<RefExpr>(ce.baseFunc.get())->ref.target);
            callee && callee->type == BuiltInType::CFUNC) {
            return true;
        }
    }
    return false;
}

bool IsVirtualMember(const Decl& decl)
{
    // rule 1: top-level function is not virtual function
    if (!decl.outerDecl) {
        return false;
    }

    /**
     * rule 2: function in non-open and non-abstract class, is not virtual function
     * class A {
     *     // The following functions are not semantic virtual functions since the class is not `open`,
     *     // they will never be overrode even if they have `open` flag.
     *     public open func foo() {}
     *     open func goo() {}
     * }
     */
    auto isInNonOpenClass = decl.outerDecl->astKind == AST::ASTKind::CLASS_DECL &&
        !decl.outerDecl->TestAnyAttr(AST::Attribute::ABSTRACT, AST::Attribute::OPEN);
    if (isInNonOpenClass) {
        return false;
    }

    // rule 3: non-default static function, constructor and distructor are not virtual function
    //         The default function needs to be dispatched to the subtype.
    if (auto fd = DynamicCast<FuncDecl>(&decl);
        (decl.TestAttr(AST::Attribute::STATIC) && !decl.TestAttr(AST::Attribute::DEFAULT)) ||
        decl.TestAttr(AST::Attribute::CONSTRUCTOR) || (fd && fd->IsFinalizer())) {
        return false;
    }

    // rule 4: generic instantiated function, is not virtual function
    if (decl.TestAttr(AST::Attribute::GENERIC_INSTANTIATED)) {
        return false;
    }

    // rule 5: function defined within an extend is not virtual function
    if (decl.outerDecl->astKind == AST::ASTKind::EXTEND_DECL) {
        return false;
    }
    // rule 6: only public or protected function has the opportunity to be overrode
    if (decl.outerDecl->astKind == AST::ASTKind::CLASS_DECL && !decl.TestAttr(AST::Attribute::PUBLIC) &&
        !decl.TestAttr(AST::Attribute::PROTECTED)) {
        return false;
    }

    // rule 7: if the base function has open modifier, or is abstract,
    // or is defined within interface, it must be virtual function
    return decl.TestAnyAttr(AST::Attribute::OPEN, AST::Attribute::ABSTRACT) ||
        decl.outerDecl->astKind == AST::ASTKind::INTERFACE_DECL;
}
std::vector<VarDeclWithPosition> GetVarsInitializationOrderWithPositions(const Decl& parentDecl)
{
    std::vector<VarDeclWithPosition> commonDecls;
    std::vector<VarDeclWithPosition> platformDecls;

    std::size_t idx = 0;
    for (auto& decl : parentDecl.GetMemberDecls()) {
        if (decl->astKind != ASTKind::VAR_DECL || decl->IsStaticOrGlobal()) {
            continue;
        }
        Ptr<VarDecl> varDecl = StaticCast<VarDecl>(decl.get());

        if (varDecl->TestAttr(AST::Attribute::FROM_COMMON_PART)) {
            commonDecls.push_back({varDecl, idx});
        } else {
            platformDecls.push_back({varDecl, idx});
        }
        idx++;
    }

    std::sort(platformDecls.begin(), platformDecls.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.decl->begin < rhs.decl->begin;
    });

    std::vector<VarDeclWithPosition> resultDecls;
    auto platformDeclsIt = platformDecls.begin();
    std::unordered_set<Ptr<const Decl>> wasPlatformVars;
    for (auto& [commonDecl, commonDeclOffset] : commonDecls) {
        std::unordered_set<Ptr<const Decl>> platformVarsDeps;
        for (auto& dep : commonDecl->dependencies) {
            if (!dep->TestAttr(Attribute::PLATFORM)) {
                continue;
            }

            auto [_, inserted] = wasPlatformVars.emplace(dep);
            if (inserted) {
                platformVarsDeps.emplace(dep);
            }
        }

        while (!platformVarsDeps.empty()) {
            CJC_ASSERT(platformDeclsIt != platformDecls.end());
            auto it = platformVarsDeps.find(platformDeclsIt->decl);
            if (it != platformVarsDeps.end()) {
                platformVarsDeps.erase(it);
            }
            resultDecls.emplace_back(*platformDeclsIt);
            platformDeclsIt++;
        }

        resultDecls.push_back({commonDecl, commonDeclOffset});
    }

    for (; platformDeclsIt != platformDecls.end(); platformDeclsIt++) {
        resultDecls.emplace_back(*platformDeclsIt);
    }

    return resultDecls;
}

void InsertPropGetterSignature(PropDecl& prop, Attribute attrToBeSet)
{
    auto getter = MakeOwned<FuncDecl>();
    getter->identifier = "get";
    getter->outerDecl = prop.outerDecl;
    getter->propDecl = &prop;
    getter->isGetter = true;
    getter->CloneAttrs(prop);
    getter->DisableAttr(Attribute::MUT);
    getter->EnableAttr(Attribute::COMPILER_ADD, attrToBeSet);

    if (prop.outerDecl->astKind == ASTKind::INTERFACE_DECL) {
        getter->EnableAttr(Attribute::PUBLIC);
    }

    OwnedPtr<FuncBody> getterBody = MakeOwned<FuncBody>();
    getterBody->EnableAttr(Attribute::COMPILER_ADD);
    getterBody->paramLists.push_back(MakeOwned<FuncParamList>());
    getterBody->paramLists.begin()->get()->EnableAttr(Attribute::COMPILER_ADD);
    getter->funcBody = std::move(getterBody);
    getter->funcBody->funcDecl = getter.get();
    prop.getters.emplace_back(std::move(getter));
}

void InsertPropSetterSignature(PropDecl& prop, Attribute attrToBeSet)
{
    auto setter = MakeOwned<FuncDecl>();
    setter->identifier = "set";
    setter->outerDecl = prop.outerDecl;
    setter->propDecl = &prop;
    setter->isSetter = true;
    setter->CloneAttrs(prop);
    setter->DisableAttr(Attribute::MUT);
    setter->EnableAttr(Attribute::COMPILER_ADD, attrToBeSet);

    if (prop.outerDecl->astKind == ASTKind::INTERFACE_DECL) {
        setter->EnableAttr(Attribute::PUBLIC);
    }

    OwnedPtr<FuncBody> setterBody = MakeOwned<FuncBody>();
    setterBody->EnableAttr(Attribute::COMPILER_ADD);
    setterBody->paramLists.push_back(MakeOwned<FuncParamList>());
    auto setterParam = MakeOwned<FuncParam>();
    setterParam->EnableAttr(Attribute::COMPILER_ADD);
    setterParam->identifier = "set";
    setterBody->paramLists.begin()->get()->params.push_back(std::move(setterParam));
    setterBody->paramLists.begin()->get()->EnableAttr(Attribute::COMPILER_ADD);
    setter->funcBody = std::move(setterBody);
    setter->funcBody->funcDecl = setter.get();
    prop.setters.emplace_back(std::move(setter));
}

void InsertPropConvertedByField(ClassDecl& decl, VarDecl& varDecl, Attribute attrToBeSet)
{
    auto propDecl = MakeOwned<PropDecl>();
    propDecl->begin = varDecl.begin;
    propDecl->end = varDecl.end;
    propDecl->keywordPos = varDecl.keywordPos;
    propDecl->identifier = varDecl.identifier;
    propDecl->colonPos = varDecl.colonPos;
    propDecl->type = std::move(varDecl.type);
    propDecl->ty = varDecl.ty;
    propDecl->CloneAttrs(varDecl);
    propDecl->EnableAttr(Attribute::DESUGARED_MIRROR_FIELD);
    propDecl->modifiers.insert(varDecl.modifiers.begin(), varDecl.modifiers.end());
    propDecl->isVar = varDecl.isVar;
    for (auto& anno : varDecl.annotations) {
        propDecl->annotations.emplace_back(ASTCloner::Clone(anno.get()));
    }
    if (varDecl.isVar) {
        propDecl->EnableAttr(Attribute::MUT);
        Modifier mut = Modifier(TokenKind::MUT, varDecl.begin);
        mut.curFile = varDecl.curFile;
        propDecl->modifiers.insert(std::move(mut));
    }
    propDecl->outerDecl = varDecl.outerDecl;
    InsertPropGetterSignature(*propDecl.get(), attrToBeSet);
    if (varDecl.isVar) {
        InsertPropSetterSignature(*propDecl.get(), attrToBeSet);
    }
    decl.body->decls.emplace_back(std::move(propDecl));
}

void InsertMirrorVarProp(ClassDecl& decl, Attribute attrToBeSet)
{
    auto& members = decl.GetMemberDecls();
    // Collect the original field
    std::vector<VarDecl*> oldVars;
    for (auto& member : members) {
        if (member->astKind == ASTKind::VAR_DECL) {
            oldVars.emplace_back(StaticAs<ASTKind::VAR_DECL>(member.get()));
        }
    }
    // Generate and insert the new prop
    for (auto var : oldVars) {
        InsertPropConvertedByField(decl, *var, attrToBeSet);
    }
    // Delete the original field
    members.erase(std::remove_if(members.begin(), members.end(), [](auto& node) {
        return node.get()->astKind == ASTKind::VAR_DECL;
        }), members.end());
}

} // namespace Cangjie::AST

namespace {
using namespace Cangjie::AST;

void SetPositionAndCurFileByProvidedNode(Node& consumer, Node& provider)
{
    consumer.curFile = provider.curFile;
    consumer.begin = provider.begin;
    consumer.end = provider.end;
}
}

namespace Cangjie::Interop::Java {
bool IsImpl(const Decl& decl)
{
    return !decl.TestAttr(Attribute::JAVA_MIRROR) && decl.TestAttr(Attribute::JAVA_MIRROR_SUBTYPE);
}

bool IsJObject(const Decl& decl)
{
    return IsJObject(decl, decl.fullPackageName);
}

bool IsJObject(const Decl& decl, const std::string& packageName)
{
    return IsMirror(decl) &&
        decl.identifier.Val() == INTEROP_JOBJECT_NAME &&
        packageName == INTEROP_JAVA_LANG_PACKAGE;
}

bool IsMirror(const Decl& decl)
{
    return decl.TestAttr(Attribute::JAVA_MIRROR);
}

bool IsCJMapping(const Decl& decl)
{
    return decl.TestAttr(Attribute::JAVA_CJ_MAPPING);
}

bool IsObject(const Decl& decl)
{
    return decl.ty->IsObject();
}

bool IsFwdClass(const Decl& decl)
{
    return decl.TestAttr(Attribute::CJ_MIRROR_JAVA_INTERFACE_FWD);
}

/**
 * public func $getJavaRef(): Java_CFFI_JavaEntity {
 *     return Java_CFFI_JavaEntity()
 * }
 */
void InsertJavaRefGetterStubWithBody(ClassDecl& decl)
{
    std::vector<OwnedPtr<FuncParam>> callParams;
    std::vector<OwnedPtr<FuncParamList>> paramLists;
    auto pl = CreateFuncParamList(std::move(callParams));
    pl->begin = decl.begin;
    pl->end = decl.end;
    paramLists.push_back(std::move(pl));

    auto constructor = CreateCallExpr(CreateRefExpr(INTEROPLIB_CFFI_JAVA_ENTITY), {});
    auto ret = CreateReturnExpr(std::move(constructor));
    std::vector<OwnedPtr<Node>> nodes;
    nodes.emplace_back(std::move(ret));

    auto funcBody = CreateFuncBody(
        std::move(paramLists),
        CreateRefType(INTEROPLIB_CFFI_JAVA_ENTITY),
        CreateBlock(std::move(nodes)));

    auto fd = CreateFuncDecl(JAVA_REF_GETTER_FUNC_NAME, std::move(funcBody), nullptr);
    fd->EnableAttr(Attribute::PUBLIC, Attribute::IN_CLASSLIKE);
    fd->fullPackageName = decl.fullPackageName;
    fd->funcBody->funcDecl = fd.get();
    fd->funcBody->parentClassLike = &decl;
    fd->outerDecl = &decl;

    decl.body->decls.emplace_back(std::move(fd));
}

bool IsDeclAppropriateForSyntheticClassGeneration(const Decl& decl)
{
    return decl.TestAttr(Attribute::JAVA_MIRROR) &&
        (decl.astKind == ASTKind::INTERFACE_DECL ||
            (decl.astKind == ASTKind::CLASS_DECL && decl.TestAttr(Attribute::ABSTRACT)));
}

std::string GetSyntheticNameFromClassLike(const ClassLikeDecl& cld)
{
    return cld.identifier.Val() + "$impl";
}

// abstract on parser stage, on sema stage abstractness will be removed
void InsertSyntheticClassDecl(ClassLikeDecl& decl, File& file)
{
    auto synthetic = MakeOwned<ClassDecl>();
    if (decl.TestAttr(Attribute::PUBLIC)) {
        synthetic->EnableAttr(Attribute::PUBLIC);
    } else if (decl.TestAttr(Attribute::INTERNAL)) {
        synthetic->EnableAttr(Attribute::INTERNAL);
    } else if (decl.TestAttr(Attribute::PROTECTED)) {
        synthetic->EnableAttr(Attribute::PROTECTED);
    } else if (decl.TestAttr(Attribute::PRIVATE)) {
        synthetic->EnableAttr(Attribute::PRIVATE);
    }
    synthetic->EnableAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE,
        Attribute::JAVA_MIRROR_SYNTHETIC_WRAPPER, Attribute::COMPILER_ADD, Attribute::ABSTRACT);
    synthetic->identifier = GetSyntheticNameFromClassLike(decl);
    synthetic->identifier.SetPos(decl.identifier.Begin(), decl.identifier.End());

    if (decl.astKind == ASTKind::INTERFACE_DECL) { // add JObject as supertype
        auto jobject = CreateRefType(INTEROP_JOBJECT_NAME);
        SetPositionAndCurFileByProvidedNode(*jobject, decl);
        synthetic->inheritedTypes.emplace_back(std::move(jobject));
    }
    synthetic->inheritedTypes.emplace_back(CreateRefType(decl));
    
    synthetic->fullPackageName = decl.fullPackageName;
    SetPositionAndCurFileByProvidedNode(*synthetic, decl);
    
    synthetic->body = MakeOwned<ClassBody>();
    SetPositionAndCurFileByProvidedNode(*synthetic->body, *synthetic);

    synthetic->moduleName = ::Cangjie::Utils::GetRootPackageName(decl.fullPackageName);

    file.decls.emplace_back(std::move(synthetic));
}
} // namespace Cangjie::Interop::Java
