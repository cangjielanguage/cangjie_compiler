// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the ASTHasher related classes.
 */
#include "cangjie/Parse/ASTHasher.h"

#ifndef NDEBUG
#include <string_view>
#include <csignal>
#endif

#include "cangjie/AST/Node.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/IncrementalCompilation/Utils.h"
#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/SipHash.h"

static constexpr int ALL = 0;
static constexpr int ONLY_POSITION = 1;
static constexpr int NON_POSITION = 2;
static bool g_needLineInfo{false};

// when hashing src info, trim the package path if one present. This is to ensure
// 1. code position does not change when the total number of source files (including source files of imported packages)
//    changes
// 2. code position does not change when all the source files are moved from one directory to another as long as the
//    relative path of each source file to the package does not change
static std::optional<std::string> g_packagePath{};
static std::vector<std::string> g_trimPaths{};

#ifndef NDEBUG
namespace Cangjie::AST {
namespace {
// This variable is purely used for debug. ASTHasher has been proved to be hard to debug by the means of breakpoints,
// as the hash value changes after almost every line, even by multiple times. As a fallback debug method, track the
// falty hash value of a decl by printing the hash value after every change is a good idea. Based on this method,
// turn this variable to true when the falty decl is detected as input to any entry function of ASTHasher module, print
// after each mutating method, and then turn it off when the hash of the decl ends to compute.
bool g_print{false};

[[maybe_unused]] void PrintDebug(Cangjie::AST::ASTHasher::hash_type v, int line = __LINE__)
{
    if (!g_print) {
        return;
    }
    std::cout << line << ":" << v << "\n";
}

struct [[maybe_unused]] DebugWhen {
    DebugWhen(const Cangjie::AST::Decl& decl, std::string_view m) noexcept
    {
        if (decl.rawMangleName == m) {
            g_print = true;
        }
    }
    explicit DebugWhen(bool v) noexcept
    {
        if (v) {
            g_print = true;
        }
    }
    ~DebugWhen() noexcept
    {
        g_print = false;
    }
};
} // namespace
}
#define PRINTV PrintDebug(value, __LINE__)
#define PRINTAV PrintDebug(a.value, __LINE__)
#endif

namespace Cangjie::IncrementalCompilation {
std::string TrimPackagePath(const std::string& path)
{
    // remove path prefix when --trimpath is provided
    if (!g_trimPaths.empty()) {
        return FileUtil::RemovePathPrefix(path, g_trimPaths);
    }
    // otherwise, trim the package path when -p is provided
    if (g_packagePath) {
        if (path.find(*g_packagePath) == 0) {
            if (path.size() > g_packagePath->size() && FileUtil::IsSlash(path[g_packagePath->size()]) &&
                !FileUtil::IsSlash(g_packagePath->at(g_packagePath->size() - 1))) {
                // trim the slash character if package path does not end with slash
                return path.substr(g_packagePath->size() + 1);
            }
            return path.substr(g_packagePath->size());
        }
    }
    // otherwise, return a copy of the original path
    return path;
}

std::string GetTrimmedPath(AST::File* file)
{
    if (!file) {
        return "";
    }
    // remove begining package name and the following '/'
    auto path = file->filePath;
    if (file->curPackage) {
        auto pkgName = file->curPackage->fullPackageName;
#ifdef _WIN32
        pkgName += "\\";
#else
        pkgName += "/";
#endif
        if (path.find(pkgName) == 0) {
            path = path.substr(pkgName.size());
        }
    }
    return TrimPackagePath(path);
}
}

namespace Cangjie::AST {
using namespace Cangjie::IncrementalCompilation;
struct ASTHasherImpl {
    using hash_type = ASTHasher::hash_type;
    ASTHasherImpl() : value{0}
    {
    }

    hash_type value;

    hash_type HashSpecs(const Package& pk)
    {
        HashPackage<NON_POSITION>(pk);
        return value;
    }

    /** CombineHash is a function used to create hash with fewer collisions. */
    template <int whatTypeToHash = 0, typename T> void CombineHash(const T& v)
    {
        (void)CombineTwoHashes(Utils::SipHash::GetHashValue(v));
    }

    template <int whatTypeToHash = 0, typename T> void CombineHash(const OwnedPtr<T>& v)
    {
        CombineHash<whatTypeToHash>(v.get());
    }

    template <int whatTypeToHash, typename T> void CombineHash(Ptr<T> v)
    {
        static_assert(std::is_base_of_v<Node, T> || std::is_same_v<MacroInvocation, T>,
            "only unique pointer of Node and its derived class and MacroInvocation is hashable");
        static_assert(
            !std::is_base_of_v<Package, T> && !std::is_base_of_v<File, T>, "`Package` and `File` is not allowed");
        (void)Hash<whatTypeToHash>(v);
    }

    template <int whatTypeToHash = 0, typename T> void CombineHash(const std::vector<T>& v)
    {
        for (auto& item : v) {
            SUPERHash<whatTypeToHash>(item);
        }
    }

    template <int whatTypeToHash = 0> void CombineHash(const Token& tok)
    {
        SUPERHash<whatTypeToHash>(static_cast<unsigned char>(tok.kind), tok.Begin(), tok.Value());
    }

    template <int whatTypeToHash = 0> void CombineHash(const StringPart& sp)
    {
        SUPERHash<whatTypeToHash>(sp.begin, sp.end, static_cast<int64_t>(sp.strKind), sp.value);
    }

    template <int whatTypeToHash = 0> void CombineHash(const Position& pos)
    {
        if constexpr (whatTypeToHash == NON_POSITION) {
            return;
        }
        if (pos.GetStatus() == PositionStatus::IGNORE) {
            return;
        }
        CombineHash<whatTypeToHash>(pos.line);
        CombineHash<whatTypeToHash>(pos.column);
        CombineHash<whatTypeToHash>(pos.isCurFile);
    }

    template <int whatTypeToHash = 0> void CombineHash(const Identifier& id)
    {
        CombineHash<whatTypeToHash>(id.Val());
        CombineHash<whatTypeToHash>(id.Begin());
    }
    template <int whatTypeToHash = 0> void CombineHash(const SrcIdentifier& id)
    {
        CombineHash<whatTypeToHash>(static_cast<const Identifier&>(id));
        CombineHash<whatTypeToHash>(id.IsRaw());
    }

    template <int whatTypeToHash = 0> void CombineHash(const AST::AttributePack& attrs)
    {
        for (auto attr : attrs.GetRawAttrs()) {
            CombineHash<whatTypeToHash>(attr);
        }
    }

    template <int whatTypeToHash = 0> void CombineHash(const std::set<AST::Modifier>& modifiers)
    {
        // specially handle decl.modifiers, we only care about AST::Modifier's modifier
        std::vector<TokenKind> modifierVec;
        modifierVec.reserve(modifiers.size());
        for (auto& modifier : modifiers) {
            (void)modifierVec.emplace_back(modifier.modifier);
        }
        std::stable_sort(modifierVec.begin(), modifierVec.end());
        for (auto& modifier : modifierVec) {
            CombineHash<whatTypeToHash>(static_cast<int64_t>(modifier));
        }
    }

    template <int whatTypeToHash, typename Arg> inline void SuperHash(const Arg& input)
    {
        if constexpr ((whatTypeToHash == ONLY_POSITION && std::is_same<Arg, Position>::value) ||
            (whatTypeToHash == NON_POSITION && !std::is_same<Arg, Position>::value) || whatTypeToHash == ALL) {
            // if there is `-g` in cjc's parameter, will ignore position's hash
            if (g_needLineInfo || !std::is_same<Arg, Position>::value) {
                CombineHash<whatTypeToHash>(input);
            }
        }
    }

    // skip debug info of this function
    template <int whatTypeToHash = 0, typename First, typename... Last>
    inline void SUPERHash(const First& firstArg, const Last&... inputs)
    {
        SuperHash<whatTypeToHash, First>(firstArg);
        if constexpr (sizeof...(inputs) != 0) {
            SUPERHash<whatTypeToHash, Last...>(inputs...);
        }
    }

    size_t CombineTwoHashes(const size_t ha)
    {
        value = ASTHasher::CombineHash(value, ha);
        return value;
    }

    using NodeKindT = std::underlying_type_t<ASTKind>;
    static constexpr int nodeKind{static_cast<NodeKindT>(ASTKind::NODE) + 1};
    static void (*hashFuncMap[nodeKind])(ASTHasherImpl&, Ptr<const Node>);
    static void (*hashFuncMapOnlyPosition[nodeKind])(ASTHasherImpl&, Ptr<const Node>);
    static void (*hashFuncMapNonPosition[nodeKind])(ASTHasherImpl&, Ptr<const Node>);

    static constexpr unsigned declKindNum{static_cast<unsigned>(ASTKind::INVALID_DECL) + 1};

    static inline bool IsNonZero(const Position& pos)
    {
        return pos.line != 0;
    }

    template <int whatTypeToHash> void HashVarDeclAbstract(const VarDeclAbstract& vda)
    {
        HashDecl<whatTypeToHash>(vda);
        SUPERHash<whatTypeToHash>(vda.type, vda.initializer, vda.isVar);
    }

    template <int whatTypeToHash> void HashNode(const Node& node)
    {
        SUPERHash<whatTypeToHash>(static_cast<int64_t>(node.astKind), node.scopeName, node.exportId, node.scopeLevel);

        // attrs below are set before sema
        auto nonSemaAttr = {Attribute::ABSTRACT, Attribute::CONSTRUCTOR, Attribute::DEFAULT,
            Attribute::ENUM_CONSTRUCTOR, Attribute::INTERNAL, Attribute::FOREIGN, Attribute::COMMON,
            Attribute::PLATFORM, Attribute::GENERIC, Attribute::GLOBAL,
            Attribute::INTRINSIC, Attribute::JAVA_APP, Attribute::JAVA_MIRROR, Attribute::OBJ_C_MIRROR,
            Attribute::OBJ_C_MIRROR_SUBTYPE, Attribute::MACRO_EXPANDED_NODE, Attribute::MACRO_FUNC,
            Attribute::MUT, Attribute::NUMERIC_OVERFLOW, Attribute::OPERATOR, Attribute::PRIVATE, Attribute::PUBLIC,
            Attribute::PROTECTED, Attribute::STATIC, Attribute::TOOL_ADD, Attribute::UNSAFE};

        for (auto attr : nonSemaAttr) {
            SUPERHash(node.TestAttr(attr));
        }
    }

    template <int whatTypeToHash> void HashExpr(const Expr& expr)
    {
        SUPERHash<whatTypeToHash>(expr.begin, expr.end);
        if constexpr (whatTypeToHash != NON_POSITION) {
            if (expr.begin.GetStatus() != PositionStatus::IGNORE) {
                CJC_ASSERT(expr.curFile);
                SUPERHash<whatTypeToHash>(TrimPackagePath(expr.curFile->filePath));
            }
        }
        HashNode<whatTypeToHash>(expr);
        // expr.overflowStrategy is from annos
        // expr.semiPos and expr.hasSemi are somehow overlapped
        SUPERHash<whatTypeToHash>(expr.semiPos, expr.hasSemi);
    }

    template <int whatTypeToHash> void HashIfExpr(const IfExpr& ifExpr)
    {
        HashExpr<whatTypeToHash>(ifExpr);
        SUPERHash<whatTypeToHash>(ifExpr.ifPos, ifExpr.leftParenPos, ifExpr.rightParenPos, ifExpr.condExpr,
            ifExpr.thenBody, ifExpr.elsePos, ifExpr.elseBody, ifExpr.isElseIf, ifExpr.hasElse);
    }

    template <int whatTypeToHash> void HashAnnotation(const Cangjie::AST::Annotation& anno)
    {
        HashNode<whatTypeToHash>(anno);
        // ignore annotation argument and expression positions
        SUPERHash<NON_POSITION>(static_cast<int64_t>(anno.kind), anno.identifier.Val(), anno.args,
                        static_cast<int64_t>(anno.overflowStrategy), anno.definedPackage, anno.condExpr,
            anno.isCompileTimeVisible, anno.attrCommas);
        for (auto& att : anno.attrs) {
            CombineHash(att);
        }
    }

    template <int whatTypeToHash> void HashDecl(const Decl& decl)
    {
        HashNode<whatTypeToHash>(decl);
        SUPERHash<whatTypeToHash>(decl.modifiers, decl.annotations, decl.identifier.Val(), decl.generic);
        if constexpr (whatTypeToHash != NON_POSITION) {
            if (decl.begin.GetStatus() != PositionStatus::IGNORE) {
                CJC_ASSERT(decl.curFile);
                SUPERHash<whatTypeToHash>(TrimPackagePath(decl.curFile->filePath));
            }
        }
    }
    template <int whatTypeToHash> void HashFuncArg(const FuncArg& funcArg)
    {
        HashNode<whatTypeToHash>(funcArg);
        SUPERHash<whatTypeToHash>(funcArg.name, funcArg.expr, funcArg.withInout);
    }
    template <int whatTypeToHash> void HashFuncBody(const FuncBody& funcBody)
    {
        HashNode<whatTypeToHash>(funcBody);
        SUPERHash<whatTypeToHash>(funcBody.paramLists, funcBody.retType, funcBody.body, funcBody.generic);
    }

    template <int whatTypeToHash> void HashFuncParamList(const FuncParamList& fpl)
    {
        HashNode<whatTypeToHash>(fpl);
        SUPERHash<whatTypeToHash>(fpl.params, fpl.variadicArgIndex, fpl.hasVariableLenArg);
    }

    template <int whatTypeToHash> void HashGeneric(const Generic& ge)
    {
        HashNode<whatTypeToHash>(ge);
        SUPERHash<whatTypeToHash>(ge.typeParameters, ge.genericConstraints);
    }
    template <int whatTypeToHash> void HashGenericConstraint(const GenericConstraint& gc)
    {
        HashNode<whatTypeToHash>(gc);
        SUPERHash<whatTypeToHash>(gc.type, gc.upperBounds);
    }

    template <int whatTypeToHash> void HashImportSpec(const ImportSpec& is)
    {
        HashNode<whatTypeToHash>(is);
        auto& im = is.content;
        if (im.kind != ImportKind::IMPORT_MULTI) {
            SUPERHash<whatTypeToHash>(
                static_cast<uint8_t>(im.kind), im.prefixPaths, im.identifier.Val(), im.aliasName);
        } else {
            SUPERHash<whatTypeToHash>(static_cast<uint8_t>(im.kind), im.prefixPaths, im.leftCurlPos, im.rightCurlPos);
            for (const auto& item : im.items) {
                SUPERHash<whatTypeToHash>(
                    static_cast<uint8_t>(item.kind), item.prefixPaths, item.identifier.Val(), item.aliasName.Val());
            }
        }
        SUPERHash<whatTypeToHash>(im.hasDoubleColon);
    }
    template <int whatTypeToHash> void HashPackageSpec(const PackageSpec& ps)
    {
        HashNode<whatTypeToHash>(ps);
        SUPERHash<whatTypeToHash>(ps.prefixPaths, ps.packageName, ps.hasMacro, ps.hasDoubleColon);
    }
    template <int whatTypeToHash> void HashMatchCase(const MatchCase& mc)
    {
        HashNode<whatTypeToHash>(mc);
        SUPERHash<whatTypeToHash>(mc.patterns, mc.patternGuard, mc.wherePos, mc.arrowPos, mc.exprOrDecls);
    }

    template <int whatTypeToHash> void HashMatchCaseOther(const MatchCaseOther& mco)
    {
        HashNode<whatTypeToHash>(mco);
        SUPERHash<whatTypeToHash>(mco.matchExpr, mco.arrowPos, mco.exprOrDecls);
    }
    template <int whatTypeToHash> void HashPackage(const Package& p)
    {
        for (auto& file : p.files) {
            for (auto& import : file->imports) {
                if (!import->TestAttr(Attribute::IMPLICIT_ADD)) {
                    HashImportSpec<whatTypeToHash>(*import);
                }
            }
            if (file->package) {
                HashPackageSpec<NON_POSITION>(*file->package);
            }
        }
    }

    template <int whatTypeToHash> void HashFuncDecl(const FuncDecl& fd)
    {
        HashDecl<whatTypeToHash>(fd);
        SUPERHash<whatTypeToHash>(fd.funcBody, static_cast<int64_t>(fd.op));
    }

    template <int whatTypeToHash> void HashPrimaryCtorDecl(const PrimaryCtorDecl& pc)
    {
        HashDecl<whatTypeToHash>(pc);
        SUPERHash<whatTypeToHash>(pc.funcBody);
    }
    template <int whatTypeToHash> void HashVarWithPatternDecl(const VarWithPatternDecl& vwp)
    {
        HashVarDeclAbstract<whatTypeToHash>(vwp);
        SUPERHash<whatTypeToHash>(vwp.irrefutablePattern);
    }

    template <int whatTypeToHash> void HashFuncParam(const FuncParam& fp)
    {
        HashVarDecl<whatTypeToHash>(fp);
        SUPERHash<whatTypeToHash>(fp.assignment, fp.isNamedParam, fp.hasLetOrVar);
    }

    template <int whatTypeToHash> void HashArrayExpr(const ArrayExpr& ae)
    {
        HashExpr<whatTypeToHash>(ae);
        SUPERHash<whatTypeToHash>(ae.type, ae.args);
    }
    template <int whatTypeToHash> void HashArrayLit(const ArrayLit& al)
    {
        HashExpr<whatTypeToHash>(al);
        SUPERHash<whatTypeToHash>(al.children);
    }
    template <int whatTypeToHash> void HashAsExpr(const AsExpr& ae)
    {
        HashExpr<whatTypeToHash>(ae);
        SUPERHash<whatTypeToHash>(ae.leftExpr, ae.asType);
    }
    template <int whatTypeToHash> void HashBlock(const Block& ae)
    {
        HashExpr<whatTypeToHash>(ae);
        // only position of unsafe block is used
        if (IsNonZero(ae.unsafePos)) {
            SUPERHash<whatTypeToHash>(ae.leftCurlPos, ae.body, ae.rightCurlPos);
        } else {
            SUPERHash<whatTypeToHash>(ae.body);
        }
        SUPERHash<whatTypeToHash>(ae.unsafePos);
    }
    template <int whatTypeToHash> void HashCallExpr(const CallExpr& ce)
    {
        HashExpr<whatTypeToHash>(ce);
        SUPERHash<whatTypeToHash>(ce.baseFunc, ce.args);
    }
    template <int whatTypeToHash> void HashDoWhileExpr(const DoWhileExpr& dwe)
    {
        HashExpr<whatTypeToHash>(dwe);
        SUPERHash<whatTypeToHash>(dwe.body, dwe.condExpr);
    }
    template <int whatTypeToHash> void HashForInExpr(const ForInExpr& fie)
    {
        HashExpr<whatTypeToHash>(fie);
        SUPERHash<whatTypeToHash>(fie.pattern, fie.inPos, fie.inExpression, fie.patternGuard, fie.body);
    }
    template <int whatTypeToHash> void HashIsExpr(const IsExpr& ie)
    {
        HashExpr<whatTypeToHash>(ie);
        SUPERHash<whatTypeToHash>(ie.leftExpr, ie.isType);
    }

    template <int whatTypeToHash> void HashLambdaExpr(const LambdaExpr& le)
    {
        HashExpr<whatTypeToHash>(le);
        SUPERHash<whatTypeToHash>(le.funcBody);
    }
    template <int whatTypeToHash> void HashLetPatternDestructor(const LetPatternDestructor& lpd)
    {
        SUPERHash<whatTypeToHash>(lpd.patterns, lpd.orPos, lpd.initializer, lpd.backarrowPos);
    }
    template <int whatTypeToHash> void HashLitConstExpr(const LitConstExpr& lce)
    {
        HashExpr<whatTypeToHash>(lce);
        SUPERHash<whatTypeToHash>(lce.rawString, lce.siExpr, lce.stringValue, lce.codepoint,
            static_cast<int64_t>(lce.kind), static_cast<int64_t>(lce.stringKind), lce.delimiterNum);
    }
    template <int whatTypeToHash> void HashMatchExpr(const MatchExpr& me)
    {
        HashExpr<whatTypeToHash>(me);
        SUPERHash<whatTypeToHash>(
            me.matchMode, me.selector, me.leftCurlPos, me.matchCases, me.matchCaseOthers, me.rightCurlPos);
    }
    template <int whatTypeToHash> void HashMemberAccess(const MemberAccess& ma)
    {
        SUPERHash<whatTypeToHash>(ma.baseExpr, ma.field, ma.typeArguments, ma.isPattern);
        if (auto maTarget = ma.target) {
            SUPERHash<whatTypeToHash>(maTarget->rawMangleName);
            CJC_ASSERT(maTarget->ty);
            SUPERHash<whatTypeToHash>(maTarget->ty->String());
        }
    }
    template <int whatTypeToHash> void HashTypeConvExpr(const TypeConvExpr& ntc)
    {
        HashExpr<whatTypeToHash>(ntc);
        SUPERHash<whatTypeToHash>(ntc.type, ntc.leftParenPos, ntc.expr, ntc.rightParenPos);
    }

    template <int whatTypeToHash> void HashParenExpr(const ParenExpr& pe)
    {
        HashExpr<whatTypeToHash>(pe);
        SUPERHash<whatTypeToHash>(pe.expr);
    }
    template <int whatTypeToHash> void HashPointerExpr(const PointerExpr& pe)
    {
        HashExpr<whatTypeToHash>(pe);
        SUPERHash<whatTypeToHash>(pe.type, pe.arg);
    }
    template <int whatTypeToHash> void HashQuoteExpr(const QuoteExpr& qe)
    {
        HashExpr<whatTypeToHash>(qe);
        SUPERHash<whatTypeToHash>(qe.quotePos);
    }
    template <int whatTypeToHash> void HashRangeExpr(const RangeExpr& re)
    {
        HashExpr<whatTypeToHash>(re);
        SUPERHash<whatTypeToHash>(re.startExpr, re.rangePos, re.stopExpr, re.colonPos, re.stepExpr, re.isClosed);
    }
    template <int whatTypeToHash> void HashRefExpr(const RefExpr& re)
    {
        HashExpr<whatTypeToHash>(re);
        SUPERHash<whatTypeToHash>(
            re.ref.identifier.Val(), re.typeArguments, re.isThis, re.isSuper, re.isQuoteDollar);
        if (auto refTarget = re.ref.target) {
            SUPERHash<whatTypeToHash>(refTarget->rawMangleName);
            CJC_ASSERT(refTarget->ty);
            SUPERHash<whatTypeToHash>(refTarget->ty->String());
        }
    }
    template <int whatTypeToHash> void HashReturnExpr(const ReturnExpr& re)
    {
        HashExpr<whatTypeToHash>(re);
        SUPERHash<whatTypeToHash>(re.expr);
    }
    template <int whatTypeToHash> void HashSpawnExpr(const SpawnExpr& se)
    {
        HashExpr<whatTypeToHash>(se);
        SUPERHash<whatTypeToHash>(se.spawnPos, se.futureObj, se.task, se.arg);
    }
    template <int whatTypeToHash> void HashSynchronizedExpr(const SynchronizedExpr& se)
    {
        HashExpr<whatTypeToHash>(se);
        SUPERHash<whatTypeToHash>(se.syncPos, se.leftParenPos, se.mutex, se.rightParenPos, se.body);
    }
    template <int whatTypeToHash> void HashThrowExpr(const ThrowExpr& te)
    {
        HashExpr<whatTypeToHash>(te);
        SUPERHash<whatTypeToHash>(te.throwPos, te.expr);
    }
    template <int whatTypeToHash> void HashPerformExpr(const PerformExpr& pe)
    {
        HashExpr<whatTypeToHash>(pe);
        SUPERHash<whatTypeToHash>(pe.performPos, pe.expr);
    }
    template <int whatTypeToHash> void HashResumeExpr(const ResumeExpr& re)
    {
        HashExpr<whatTypeToHash>(re);
        SUPERHash<whatTypeToHash>(re.resumePos);
    }
    template <int whatTypeToHash> void HashTrailingClosureExpr(const TrailingClosureExpr& tce)
    {
        HashExpr<whatTypeToHash>(tce);
        SUPERHash<whatTypeToHash>(tce.leftLambda, tce.expr, tce.lambda, tce.rightLambda);
    }
    template <int whatTypeToHash> void HashTryExpr(const TryExpr& te)
    {
        HashExpr<whatTypeToHash>(te);
        SUPERHash<whatTypeToHash>(te.lParen, te.resourceSpec, te.tryBlock, te.catchPosVector, te.catchBlocks,
            te.catchPatterns, te.finallyPos, te.finallyBlock, te.isDesugaredFromTryWithResources);
    }
    template <int whatTypeToHash> void HashTupleLit(const TupleLit& te)
    {
        SUPERHash<whatTypeToHash>(te.children);
    }
    template <int whatTypeToHash> void HashWhileExpr(const WhileExpr& we)
    {
        HashExpr<whatTypeToHash>(we);
        SUPERHash<whatTypeToHash>(we.whilePos, we.condExpr, we.body);
    }
    template <int whatTypeToHash> void HashOverloadableExpr(const OverloadableExpr& oe)
    {
        HashExpr<whatTypeToHash>(oe);
        SUPERHash<whatTypeToHash>(static_cast<unsigned char>(oe.op));
    }
    template <int whatTypeToHash> void HashAssignExpr(const AssignExpr& ae)
    {
        HashOverloadableExpr<whatTypeToHash>(ae);
        SUPERHash<whatTypeToHash>(ae.leftValue, ae.assignPos, ae.rightExpr, ae.isCompound);
    }
    template <int whatTypeToHash> void HashBinaryExpr(const BinaryExpr& be)
    {
        HashOverloadableExpr<whatTypeToHash>(be);
        SUPERHash<whatTypeToHash>(be.leftExpr, be.rightExpr, be.operatorPos);
    }
    template <int whatTypeToHash> void HashIncOrDecExpr(const IncOrDecExpr& iod)
    {
        HashOverloadableExpr<whatTypeToHash>(iod);
        SUPERHash<whatTypeToHash>(iod.operatorPos, iod.expr);
    }
    template <int whatTypeToHash> void HashSubscriptExpr(const SubscriptExpr& se)
    {
        HashExpr<whatTypeToHash>(se); // no need to hash op; also commaPos does not have debug info on it
        SUPERHash<whatTypeToHash>(se.baseExpr, se.leftParenPos, se.indexExprs);
    }
    template <int whatTypeToHash> void HashUnaryExpr(const UnaryExpr& ue)
    {
        HashOverloadableExpr<whatTypeToHash>(ue);
        SUPERHash<whatTypeToHash>(ue.expr, ue.operatorPos);
    }
    template <int whatTypeToHash> void HashConstPattern(const ConstPattern& cp)
    {
        HashPattern<whatTypeToHash>(cp);
        SUPERHash<whatTypeToHash>(cp.literal);
    }
    template <int whatTypeToHash> void HashEnumPattern(const EnumPattern& ep)
    {
        HashPattern<whatTypeToHash>(ep);
        SUPERHash<whatTypeToHash>(ep.constructor, ep.patterns);
    }
    template <int whatTypeToHash> void HashExceptTypePattern(const ExceptTypePattern& etp)
    {
        HashPattern<whatTypeToHash>(etp);
        SUPERHash<whatTypeToHash>(etp.pattern, etp.patternPos, etp.colonPos, etp.types, etp.bitOrPosVector);
    }
    template <int whatTypeToHash> void HashTuplePattern(const TuplePattern& tp)
    {
        HashPattern<whatTypeToHash>(tp);
        SUPERHash<whatTypeToHash>(tp.leftBracePos, tp.patterns, tp.rightBracePos);
    }
    template <int whatTypeToHash> void HashTypePattern(const TypePattern& tp)
    {
        HashPattern<whatTypeToHash>(tp);
        SUPERHash<whatTypeToHash>(tp.pattern, tp.type);
    }
    template <int whatTypeToHash> void HashVarOrEnumPattern(const VarOrEnumPattern& voe)
    {
        HashPattern<whatTypeToHash>(voe);
        SUPERHash<whatTypeToHash>(voe.identifier.Val());
    }
    template <int whatTypeToHash> void HashVarPattern(const VarPattern& vp)
    {
        HashPattern<whatTypeToHash>(vp);
        SUPERHash<whatTypeToHash>(vp.varDecl);
    }
    template <int whatTypeToHash> void HashFuncType(const FuncType& ft)
    {
        HashType<whatTypeToHash>(ft);
        SUPERHash<whatTypeToHash>(ft.paramTypes, ft.retType, ft.isC);
    }
    template <int whatTypeToHash> void HashOptionType(const OptionType& ot)
    {
        HashType<whatTypeToHash>(ot);
        SUPERHash<whatTypeToHash>(ot.componentType, ot.questNum);
    }
    template <int whatTypeToHash> void HashConstantType(const ConstantType& ct)
    {
        HashType<whatTypeToHash>(ct);
        SUPERHash<whatTypeToHash>(ct.constantExpr);
    }
    template <int whatTypeToHash> void HashVArrayType(const VArrayType& vt)
    {
        HashType<whatTypeToHash>(vt);
        SUPERHash<whatTypeToHash>(vt.typeArgument, vt.constantType);
    }
    template <int whatTypeToHash> void HashParenType(const ParenType& pt)
    {
        HashType<whatTypeToHash>(pt);
        SUPERHash<whatTypeToHash>(pt.type);
    }

    template <int whatTypeToHash> void HashPrimitiveType(const PrimitiveType& pt)
    {
        HashType<whatTypeToHash>(pt);
        SUPERHash<whatTypeToHash>(static_cast<int64_t>(pt.kind));
    }
    template <int whatTypeToHash> void HashQualifiedType(const QualifiedType& qt)
    {
        HashType<whatTypeToHash>(qt);
        SUPERHash<whatTypeToHash>(qt.baseType, qt.field, qt.typeArguments);
        if (auto refTarget = qt.target) {
            SUPERHash<whatTypeToHash>(refTarget->rawMangleName);
            CJC_ASSERT(refTarget->ty);
            SUPERHash<whatTypeToHash>(refTarget->ty->String());
        }
    }

    template <int whatTypeToHash> void HashRefType(const RefType& rt)
    {
        HashType<whatTypeToHash>(rt);
        SUPERHash<whatTypeToHash>(rt.ref.identifier.Val(), rt.typeArguments);
        if (auto refTarget = rt.ref.target) {
            SUPERHash<whatTypeToHash>(refTarget->rawMangleName);
            CJC_ASSERT(refTarget->ty);
            SUPERHash<whatTypeToHash>(refTarget->ty->String());
        }
    }

    template <int whatTypeToHash> void HashTupleType(const TupleType& tt)
    {
        HashType<whatTypeToHash>(tt);
        SUPERHash<whatTypeToHash>(tt.fieldTypes);
    }

    template <int whatTypeToHash> void HashVarDecl(const VarDecl& vd)
    {
        HashVarDeclAbstract<whatTypeToHash>(vd);
        SUPERHash<whatTypeToHash>(vd.isIdentifierCompilerAdd, vd.isResourceVar, vd.isMemberParam);
    }
    template <int whatTypeToHash> void HashGenericParamDecl(const GenericParamDecl& gpd)
    {
        HashDecl<whatTypeToHash>(gpd);
    }
    template <int whatTypeToHash> void HashPattern(const Pattern& pattern)
    {
        HashNode<whatTypeToHash>(pattern);
    }
    template <int whatTypeToHash> void HashWildcardPattern(const WildcardPattern& wp)
    {
        HashPattern<whatTypeToHash>(wp);
    }
    template <int whatTypeToHash> void HashType(const Type& type)
    {
        SUPERHash<whatTypeToHash>(type.typeParameterName);
    }

    template <int whatTypeToHash> void HashThisType(const ThisType& tt)
    {
        HashType<whatTypeToHash>(tt);
    }
    template <int whatTypeToHash> void HashOptionalExpr(const OptionalExpr& oe)
    {
        HashExpr<whatTypeToHash>(oe);
        SUPERHash<whatTypeToHash>(oe.baseExpr);
    }
    template <int whatTypeToHash> void HashOptionalChainExpr(const OptionalChainExpr& oce)
    {
        HashExpr<whatTypeToHash>(oce);
        SUPERHash<whatTypeToHash>(oce.expr);
    }
    template <int whatTypeToHash> void HashPrimitiveTypeExpr(const PrimitiveTypeExpr& pte)
    {
        HashExpr<whatTypeToHash>(pte);
        SUPERHash<whatTypeToHash>(static_cast<int64_t>(pte.typeKind));
    }
    template <int whatTypeToHash> void HashInterpolationExpr(const InterpolationExpr& ie)
    {
        HashExpr<whatTypeToHash>(ie);
        SUPERHash<whatTypeToHash>(ie.rawString, ie.dollarPos, ie.block);
    }
    template <int whatTypeToHash> void HashStrInterpolationExpr(const StrInterpolationExpr& sie)
    {
        HashExpr<whatTypeToHash>(sie);
        SUPERHash<whatTypeToHash>(sie.rawString, sie.strParts, sie.strPartExprs);
    }
    template <int whatTypeToHash> void HashJumpExpr(const JumpExpr& je)
    {
        HashExpr<whatTypeToHash>(je);
        SUPERHash<whatTypeToHash>(je.isBreak);
    }
    template <int whatTypeToHash> void HashWildcardExpr(const WildcardExpr& we)
    {
        HashExpr<whatTypeToHash>(we);
    }
    template <int whatTypeToHash> void HashInvalidExpr(const InvalidExpr&)
    {
    }
    template <int whatTypeToHash> void HashIfAvailableExpr(const IfAvailableExpr& expr)
    {
        HashExpr<whatTypeToHash>(expr);
        SUPERHash<whatTypeToHash>(expr.GetArg(), expr.GetLambda1(), expr.GetLambda2());
    }

    template <int whatTypeToHash> hash_type Hash(Ptr<const AST::Node> node)
    {
        if (node == nullptr) {
            return value;
        }
        auto kind{static_cast<NodeKindT>(node->astKind)};
        CJC_ASSERT(kind < nodeKind);
        if constexpr (whatTypeToHash == ALL) {
            CJC_NULLPTR_CHECK(hashFuncMap[kind]);
            hashFuncMap[kind](*this, node);
        } else if constexpr (whatTypeToHash == ONLY_POSITION) {
            CJC_NULLPTR_CHECK(hashFuncMapOnlyPosition[kind]);
            hashFuncMapOnlyPosition[kind](*this, node);
        } else {
            CJC_NULLPTR_CHECK(hashFuncMapNonPosition[kind]);
            hashFuncMapNonPosition[kind](*this, node);
        }
        return value;
    }

    static bool IsConsideredInSignatureHash(const Annotation& anno)
    {
        switch (anno.kind) {
            case AnnotationKind::C:
            case AnnotationKind::CALLING_CONV:
            case AnnotationKind::INTRINSIC:
            case AnnotationKind::JAVA:
            case AnnotationKind::JAVA_MIRROR:
            case AnnotationKind::JAVA_IMPL:
            case AnnotationKind::OBJ_C_MIRROR:
            case AnnotationKind::OBJ_C_IMPL:
            case AnnotationKind::FOREIGN_NAME:
            case AnnotationKind::WHEN:
            case AnnotationKind::DEPRECATED:
                return true;
            case AnnotationKind::ATTRIBUTE:
            case AnnotationKind::NUMERIC_OVERFLOW:
            case AnnotationKind::FASTNATIVE:
            case AnnotationKind::ANNOTATION:
            case AnnotationKind::CUSTOM:
            case AnnotationKind::FROZEN:
                return false;
            default:
                CJC_ASSERT(false);
                return false;
        }
    }
    static bool IsConsideredInBodyHash(const Annotation& anno)
    {
        switch (anno.kind) {
            case AnnotationKind::ATTRIBUTE:
                return false;
            case AnnotationKind::NUMERIC_OVERFLOW:
            case AnnotationKind::FASTNATIVE:
            case AnnotationKind::ANNOTATION:
            case AnnotationKind::CUSTOM:
            case AnnotationKind::DEPRECATED:
            case AnnotationKind::FROZEN:
                return true;
            case AnnotationKind::C:
            case AnnotationKind::CALLING_CONV:
            case AnnotationKind::INTRINSIC:
            case AnnotationKind::JAVA:
            case AnnotationKind::JAVA_MIRROR:
            case AnnotationKind::JAVA_IMPL:
            case AnnotationKind::OBJ_C_MIRROR:
            case AnnotationKind::OBJ_C_IMPL:
            case AnnotationKind::FOREIGN_NAME:
                // if anno.kind == WHEN, anno can only be in MacroDecl
            case AnnotationKind::WHEN:
                // annotations combined in body hash are basically the complementary of that combined in mangle name,
                // except for annotations with arguments, whose arguments are never mangled and therefore must be
                // computed in body hash
                return !anno.args.empty();
            default:
                CJC_ASSERT(false);
                return false;
        }
    }

    void HashAnnotationsDeclBody(const std::vector<OwnedPtr<Annotation>>& annos)
    {
        std::vector<Ptr<const Annotation>> sigAnnos;
        sigAnnos.reserve(annos.size());
        // in case multiple overflow annotation coexist on one function declaration, only the last one effects.
        // record the index of overflow annotation
        auto overflowAnnotationIndex = std::numeric_limits<size_t>::max();
        for (size_t i{0}; i < annos.size(); ++i) {
            const auto& anno{annos[i]};
            // Annotation not considered by body hash are either @C or @CallingConv, they should not have arugments
            // In the case they do have, the arguments are not considered in the mangle name, only their kinds. Add
            // them to the body hash so their change are noticed and further diangostics are possible
            if (!IsConsideredInBodyHash(*anno)) {
                continue;
            }
            if (anno->kind == AnnotationKind::NUMERIC_OVERFLOW) {
                if (overflowAnnotationIndex == std::numeric_limits<size_t>::max()) {
                    overflowAnnotationIndex = i;
                } else {
                    sigAnnos[overflowAnnotationIndex] = anno.get();
                    continue;
                }
            }
            sigAnnos.push_back(anno.get());
        }
        // should there be annotations that cannot be identified by their kind, the declaration order of such is
        // preserved for performance issue instead of applying a total ordering on these annotations
        std::stable_sort(sigAnnos.begin(), sigAnnos.end(),
            [](auto a1, auto a2) { return a1->identifier.Val() < a2->identifier.Val(); });
        for (auto anno : sigAnnos) {
            HashAnnotation<NON_POSITION>(*anno);
        }
    }

    void HashDeclBody(const Decl& decl)
    {
        // generic constraint
        if (decl.generic && decl.astKind != ASTKind::EXTEND_DECL) {
            HashConstraints(*decl.generic);
            std::vector<std::pair<std::string, Ptr<const GenericConstraint>>> constraints(
                decl.generic->genericConstraints.size());
            for (size_t i{0}; i < constraints.size(); ++i) {
                constraints[i] = {
                    decl.generic->genericConstraints[i]->type->ToString(), decl.generic->genericConstraints[i].get()};
            }
            std::stable_sort(
                constraints.begin(), constraints.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& constraint : constraints) {
                HashGenericConstraints(*constraint.second);
            }
        }
        HashAnnotationsDeclBody(decl.annotations);
    }

    // give types an arbitrary order so that the order in the source code does not matter
    static std::vector<Ptr<const Type>> SortTypes(const std::vector<OwnedPtr<Type>>& upperBounds)
    {
        std::vector<Ptr<const Type>> uppers(upperBounds.size());
        std::transform(upperBounds.begin(), upperBounds.end(), uppers.begin(), std::mem_fn(&OwnedPtr<Type>::get));
        std::stable_sort(uppers.begin(), uppers.end(), [](auto l, auto r) { return l->ToString() < r->ToString(); });
        return uppers;
    }

    void HashGenericConstraints(const GenericConstraint& gc)
    {
        SUPERHash<NON_POSITION>(gc.type, SortTypes(gc.upperBounds));
    }

    void HashTypeAlias(const TypeAliasDecl& decl)
    {
        HashDeclBody(decl);
        SUPERHash<NON_POSITION>(decl.type);
    }

    // incr 2.0 begins here
    void HashConstraints(const Generic& g)
    {
        std::vector<std::pair<std::string, Ptr<const GenericConstraint>>> constraints(g.genericConstraints.size());
        for (size_t i{0}; i < constraints.size(); ++i) {
            constraints[i] = {g.genericConstraints[i]->type->ToString(), g.genericConstraints[i].get()};
        }
        std::stable_sort(
            constraints.begin(), constraints.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& constraint : constraints) {
            HashGenericConstraints(*constraint.second);
        }
    }

    // hash specific modifier that has whose kind falls in \p kinds
    // order of annotations is ignored, but number is kept (i.e. two modifiers of the same kind will be hashed twice)
    void HashSpecificModifiers(const Decl& decl, std::set<TokenKind>&& kinds)
    {
        std::vector<std::underlying_type_t<TokenKind>> mods;
        mods.reserve(decl.modifiers.size());
        bool impliedOpen{false};
        bool hasOpen{false};
        bool hashVisibilityModifier{false};
        for (auto& mod : decl.modifiers) {
            if (kinds.count(mod.modifier) == 1) {
                mods.push_back(static_cast<std::underlying_type_t<TokenKind>>(mod.modifier));
                if (mod.modifier == TokenKind::ABSTRACT) {
                    impliedOpen = true;
                } else if (mod.modifier == TokenKind::OPEN) {
                    hasOpen = true;
                } else if (mod.modifier == TokenKind::SEALED) {
                    impliedOpen = true;
                }
            }
            if (Utils::In(
                mod.modifier, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::PRIVATE, TokenKind::INTERNAL})) {
                hashVisibilityModifier = true;
            }
        }
        // All members in interface needs to add the 'public'.
        if (kinds.count(TokenKind::PUBLIC) != 0 &&
            decl.outerDecl && decl.outerDecl->astKind == ASTKind::INTERFACE_DECL) {
            hashVisibilityModifier = true;
            mods.push_back(static_cast<std::underlying_type_t<TokenKind>>(TokenKind::PUBLIC));
        }
        // Omitted visibility of toplevel and member decl is 'internal'.
        if (kinds.count(TokenKind::INTERNAL) != 0 && !hashVisibilityModifier) {
            mods.push_back(static_cast<std::underlying_type_t<TokenKind>>(TokenKind::INTERNAL));
        }
        // abstract or sealed implies open, so adds open when abstract or sealed is present but open is not
        if (kinds.count(TokenKind::OPEN) != 0 && impliedOpen && !hasOpen) {
            mods.push_back(static_cast<std::underlying_type_t<TokenKind>>(TokenKind::OPEN));
        }

        std::sort(mods.begin(), mods.end());
        for (auto mod : std::as_const(mods)) {
            CombineHash(mod);
        }
    }

    using TokenType = std::underlying_type_t<TokenKind>;
    // hash open/abstract if decl is virtual
    void HashVirtual(const Decl& decl)
    {
        if (!decl.outerDecl) {
            return;
        }
        if (auto outer = dynamic_cast<InheritableDecl*>(decl.outerDecl.get());
            !outer || !outer->IsOpen()) {
            return;
        }
        HashSpecificModifiers(decl, {TokenKind::OPEN, TokenKind::ABSTRACT});
    }

    // hash funcbody of a funclet (func/main/primary ctor/macro/lambda decl)
    void HashBodyOfFuncLike(const FuncBody& funcBody, const std::pair<bool, bool>& srcInfo)
    {
        for (auto& param : funcBody.paramLists[0]->params) {
            // only hash identifiers of normal parameters
            // identifiers of named parameters and variable parameters are part of mangled name
            if (!param->isNamedParam && !param->hasLetOrVar) {
                CombineHash(param->identifier.Val());
            }
            if (srcInfo.first || srcInfo.second) {
                (void)CombineTwoHashes(Hash<ALL>(param->assignment.get()));
            } else {
                (void)CombineTwoHashes(Hash<NON_POSITION>(param->assignment.get()));
            }
            // only in debug mode we will need code position info of params
            if (srcInfo.first) {
                CombineHash(param->begin);
            }
        }
        if (funcBody.body) {
            if (srcInfo.first || srcInfo.second) {
                (void)CombineTwoHashes(Hash<ALL>(funcBody.body.get()));
            } else {
                (void)CombineTwoHashes(Hash<NON_POSITION>(funcBody.body.get()));
            }
        }
    }

    // hash annotations that contribute to body hash
    void BodyHashAnnotations(const std::vector<OwnedPtr<Annotation>>& annos)
    {
        std::vector<std::reference_wrapper<Annotation>> cons;
        cons.reserve(annos.size());
        Ptr<Annotation> overflowAnnotation{nullptr};
        for (auto& anno : annos) {
            // a numeric overflow annotation override other numeric overflow annotations before the annotated decl,
            // so only the last annotation of such kind is recorded
            if (anno->kind == AnnotationKind::NUMERIC_OVERFLOW) {
                overflowAnnotation = anno.get();
                continue;
            }
            if (anno->kind != AnnotationKind::ANNOTATION) {
                cons.push_back(std::ref(*anno));
            }
        }
        if (overflowAnnotation) {
            cons.push_back(std::ref(*overflowAnnotation));
        }
        HashAnnotationsNoOrder(cons);
    }

    void HashAnnotationsNoOrder(const std::vector<std::reference_wrapper<Annotation>> annos)
    {
        std::vector<hash_type> hashes(annos.size());
        for (size_t i{0}; i < annos.size(); ++i) {
            ASTHasherImpl p{};
            p.HashAnnotation<NON_POSITION>(annos[i]);
            hashes[i] = p.value;
        }
        std::sort(hashes.begin(), hashes.end());
        // sort the hash values of annotations instead of sorting the Annotation's, because there are many fields in
        // an Annotation and it is difficult to define a total ordering on that
        for (auto h : std::as_const(hashes)) {
            CombineTwoHashes(h);
        }
    }

    void SrcUseHashAnnotations(const std::vector<OwnedPtr<Annotation>>& annos)
    {
        std::vector<std::reference_wrapper<Annotation>> cons;
        cons.reserve(annos.size());
        for (auto& anno : annos) {
            if (anno->kind == AnnotationKind::ANNOTATION) {
                cons.push_back(std::ref(*anno));
            }
        }
        HashAnnotationsNoOrder(cons);
    }

    void HashMemberSignature(const Decl& decl)
    {
        SUPERHash<NON_POSITION>(decl.identifier.Val());
        SUPERHash<NON_POSITION>(decl.generic);
        if (auto func = DynamicCast<FuncDecl*>(&decl)) {
            SUPERHash<NON_POSITION>(func->funcBody->paramLists[0]->params);
            if (func->funcBody->retType) {
                SUPERHash<NON_POSITION>(func->funcBody->retType);
            } else {
                SUPERHash<NON_POSITION>(func->funcBody->body);
            }
        } else if (auto prop = DynamicCast<PropDecl*>(&decl)) {
            SUPERHash<NON_POSITION>(prop->type);
            HashSpecificModifiers(decl, {TokenKind::MUT});
        } else { // this function is only called on prop/func for now.
            // should this function be called on var, add the mutablility. otherwise, keep this branch unreachable.
            CJC_ASSERT(false);
        }
    }
};

// Not all declaration has Hash...DeclSignature or Hash...DeclBody (or else, etc.) function, this set of templates
// serves to create a lambda that wraps the member function with the specific type and a corresponding name with a
// lambda that safely cast the Ptr<Node> to the member's dynamic type.
// Note that if type Node strictly single inherits, it may be assumed that a pointer to the base class has the same
// value as the pointer to the derived class of the same object, if the platform uses a type-independent pointer
// representation (which most platforms satisfy). However, this behaviour is nowhere specified in the C++ standard
// and not adapted here.
#define ASTKIND(KIND, VALUE, NODE, SIZE)                                                                               \
    template <class, int v, class = void> struct HasHash##NODE : std::false_type {                                     \
    };                                                                                                                 \
    template <class T, int v>                                                                                          \
    struct HasHash##NODE<T, v,                                                                                         \
        std::void_t<decltype(static_cast<void (T::*)(const NODE&)>(&T::template Hash##NODE<0>))>> : std::true_type {   \
    };                                                                                                                 \
    template <class T, int v> constexpr bool hasHash##NODE##Value = HasHash##NODE<T, v>::value;                        \
    template <class T, int v> constexpr void (T::*GetHash##NODE##Fun())(const NODE&)                                   \
    {                                                                                                                  \
        if constexpr (hasHash##NODE##Value<T, v>) {                                                                    \
            return &T::template Hash##NODE<v>;                                                                         \
        }                                                                                                              \
        return nullptr;                                                                                                \
    }                                                                                                                  \
    template <class T, int v> constexpr void (*GetHash##NODE##OrNull())(T&, Ptr<const Node>)                           \
    {                                                                                                                  \
        if constexpr (hasHash##NODE##Value<T, v>) {                                                                    \
            return [](T& h, Ptr<const Node> node) {                                                                    \
                auto fn = GetHash##NODE##Fun<T, v>();                                                                  \
                return (h.*fn)(*static_cast<const NODE*>(node.get()));                                                 \
            };                                                                                                         \
        }                                                                                                              \
        return nullptr;                                                                                                \
    }
#include "cangjie/AST/ASTKind.inc"
#undef ASTKIND

void (*ASTHasherImpl::hashFuncMap[nodeKind])(ASTHasherImpl&, Ptr<const Node>) {
#define ASTKIND(KIND, VALUE, NODE, SIZE) GetHash##NODE##OrNull<ASTHasherImpl, 0>(),
#include "cangjie/AST/ASTKind.inc"
#undef ASTKIND
};
void (*ASTHasherImpl::hashFuncMapOnlyPosition[nodeKind])(ASTHasherImpl&, Ptr<const Node>) {
#define ASTKIND(KIND, VALUE, NODE, SIZE) GetHash##NODE##OrNull<ASTHasherImpl, 1>(),
#include "cangjie/AST/ASTKind.inc"
#undef ASTKIND
};
void (*ASTHasherImpl::hashFuncMapNonPosition[nodeKind])(ASTHasherImpl&, Ptr<const Node>) {
#define ASTKIND(KIND, VALUE, NODE, SIZE) GetHash##NODE##OrNull<ASTHasherImpl, 2>(),
#include "cangjie/AST/ASTKind.inc"
#undef ASTKIND
};

ASTHasher::hash_type ASTHasher::HashWithPos(Ptr<const Node> node)
{
    ASTHasherImpl a{};
    return a.Hash<ALL>(node);
}
ASTHasher::hash_type ASTHasher::HashNoPos(Ptr<const Node> node)
{
    ASTHasherImpl a{};
    return a.Hash<NON_POSITION>(node);
}

ASTHasher::hash_type ASTHasher::HashSpecs(const Package& pk)
{
    ASTHasherImpl a{};
    return a.HashSpecs(pk);
}

void ASTHasher::Init(const GlobalOptions& op)
{
    g_needLineInfo = op.enableCompileDebug || op.displayLineInfo;
    if (op.packagePaths.size() > 0) {
        g_packagePath = op.packagePaths[0];
    }
    g_trimPaths = op.removedPathPrefix;
}

static std::vector<Ptr<Type>> SortParentTypes(const InheritableDecl& decl)
{
    std::vector<Ptr<Type>> parents{};
    size_t beginIndex = decl.astKind == ASTKind::CLASS_DECL ? 1 : 0;
    for (size_t i{beginIndex}; i < decl.inheritedTypes.size(); ++i) {
        parents.push_back(decl.inheritedTypes[i].get());
    }
    std::sort(parents.begin(), parents.end(), [](auto a, auto b) { return a->ToString() < b->ToString(); });
    if (decl.astKind == ASTKind::CLASS_DECL && !decl.inheritedTypes.empty()) {
        parents.push_back(decl.inheritedTypes[0].get());
    }
    return parents;
}

ASTHasher::hash_type ASTHasher::SigHash(const Decl& decl)
{
    ASTHasherImpl a{};
    a.HashSpecificModifiers(decl, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::PRIVATE, TokenKind::INTERNAL});

    // GetGeneric() applied to constructors returns the generic of the enclosing type, rule it out;
    // constraints of extend are combined in mangled name
    if (!IsClassOrEnumConstructor(decl) && decl.astKind != ASTKind::EXTEND_DECL) {
        if (auto generic = decl.GetGeneric()) {
            a.CombineHash<NON_POSITION>(generic->typeParameters);
            a.HashConstraints(*generic);
        }
    }
    if (auto typeAlias = DynamicCast<TypeAliasDecl*>(&decl)) {
        a.HashTypeAlias(*typeAlias);
    }
    if (auto type = DynamicCast<InheritableDecl*>(&decl); type && type->astKind != ASTKind::EXTEND_DECL) {
        for (auto& parent : SortParentTypes(*type)) {
            (void)a.Hash<NON_POSITION>(parent);
        }
    }
    return a.value;
}

ASTHasher::hash_type ASTHasher::SrcUseHash(const AST::Decl& decl)
{
    ASTHasherImpl a{};
    a.HashSpecificModifiers(decl,
        {TokenKind::OPEN, TokenKind::ABSTRACT, TokenKind::SEALED, TokenKind::MUT, TokenKind::STATIC, TokenKind::CONST,
            TokenKind::FOREIGN});
    a.SrcUseHashAnnotations(decl.annotations);
    // FuncParam is for member param in primary ctor.
    if (decl.astKind == ASTKind::VAR_DECL || decl.astKind == ASTKind::FUNC_PARAM) {
        auto& var = static_cast<const VarDecl&>(decl);
        a.CombineHash(var.isVar);
        a.CombineHash(var.isConst);
    }
    if (auto prop = DynamicCast<PropDecl*>(&decl)) {
        a.SUPERHash<NON_POSITION>(prop->type);
    }
    return a.value;
}

ASTHasher::hash_type ASTHasher::HashMemberAPIs(std::vector<Ptr<const Decl>>&& memberAPIs)
{
    ASTHasherImpl hasher{};
    std::stable_sort(memberAPIs.begin(), memberAPIs.end(),
        [](const auto& a, const auto& b) { return a->rawMangleName < b->rawMangleName; });
    for (auto memberAPI : memberAPIs) {
        hasher.HashMemberSignature(*memberAPI);
        hasher.HashSpecificModifiers(*memberAPI, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::PRIVATE,
            TokenKind::INTERNAL, TokenKind::MUT, TokenKind::STATIC});
    }
    return hasher.value;
}

static bool IsVisibleAPI(const Decl& member)
{
    // protected members in extend are not collected as they can never rewrite decls of the extended type nor
    // particpate in boxing. Otherwise, protected members in class can override supertype open decls without 'open' or
    // 'override' keyword and therefore have an impact both on its own table and its subclasses, so they are collected.
    return member.TestAttr(Attribute::PUBLIC) ||
        (member.outerDecl->astKind != ASTKind::EXTEND_DECL && member.TestAttr(Attribute::PROTECTED));
}

static std::vector<Ptr<const Decl>> GetVisibleAPIs(const std::vector<Ptr<Decl>>& allMembers)
{
    std::vector<Ptr<const Decl>> ret;
    for (auto member : allMembers) {
        if (auto memberFunc = DynamicCast<const FuncDecl*>(member)) {
            if (memberFunc->TestAnyAttr(Attribute::OPEN, Attribute::ABSTRACT, Attribute::CONSTRUCTOR,
                Attribute::ENUM_CONSTRUCTOR, Attribute::PRIMARY_CONSTRUCTOR)) {
                continue;
            }
            if (memberFunc->IsFinalizer()) {
                continue;
            }
            if (IsVisibleAPI(*memberFunc)) {
                ret.emplace_back(memberFunc);
            }
        }
        if (auto memberProp = DynamicCast<const PropDecl*>(member)) {
            if (memberProp->TestAnyAttr(Attribute::OPEN, Attribute::ABSTRACT)) {
                continue;
            }
            if (IsVisibleAPI(*memberProp)) {
                ret.emplace_back(memberProp);
            }
        }
    }
    return ret;
}

// returns the funcbody if \p decl is a funclet and the body is present
static Ptr<FuncBody> GetFuncBody(const Decl& decl)
{
    switch (decl.astKind) {
        case ASTKind::FUNC_DECL:
            return DynamicCast<const FuncDecl*>(&decl)->funcBody.get();
        case ASTKind::PRIMARY_CTOR_DECL:
            return DynamicCast<const PrimaryCtorDecl*>(&decl)->funcBody.get();
        case ASTKind::MAIN_DECL:
            return DynamicCast<const MainDecl*>(&decl)->funcBody.get();
        case ASTKind::MACRO_DECL:
            return DynamicCast<const MacroDecl*>(&decl)->funcBody.get();
        default:
            return nullptr;
    }
}

ASTHasher::hash_type ASTHasher::BodyHash(const Decl& decl, const std::pair<bool, bool>& srcInfo, bool hashAnnos)
{
    ASTHasherImpl a{};
    a.HashSpecificModifiers(decl, {TokenKind::OVERRIDE, TokenKind::REDEF, TokenKind::UNSAFE});
    if (hashAnnos) {
        a.BodyHashAnnotations(decl.annotations);
    }

    // ===================== Body Hash For Var or Func Decl ===================== //

    // the constructors in enum don't have body or debug info
    if (decl.outerDecl && decl.outerDecl->astKind == ASTKind::ENUM_DECL) {
        if (decl.TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            return a.value;
        }
    }

    // only functions and variables may have debug info
    if (auto var = DynamicCast<const VarDeclAbstract*>(&decl)) {
        // property is just a sugar, we only care the `get` and `set` func inside
        if (var->astKind == ASTKind::PROP_DECL) {
            return a.value;
        }

        if (var->initializer.get()) {
            if (srcInfo.first || srcInfo.second) {
                a.Hash<ALL>(var->initializer.get());
            } else {
                a.Hash<NON_POSITION>(var->initializer.get());
            }
        }
        if (var->outerDecl &&
            (var->outerDecl->astKind == ASTKind::STRUCT_DECL || var->outerDecl->astKind == ASTKind::CLASS_DECL)) {
            return a.value;
        }

        if (srcInfo.first || srcInfo.second) {
            a.CombineHash(decl.begin);
        }
    }
    if (auto funcBody = GetFuncBody(decl)) {
        a.HashBodyOfFuncLike(*funcBody, srcInfo);
    }

    // ===================== Body Hash For Type Decl ===================== //

    if (auto classDecl = DynamicCast<const ClassDecl*>(&decl)) {
        // Also hash the existense of finalizer cause it is inserted into vtable in CodeGen
        for (auto member : classDecl->GetMemberDeclPtrs()) {
            if (auto memberFunc = DynamicCast<const FuncDecl*>(member)) {
                if (memberFunc->IsFinalizer()) {
                    a.HashMemberSignature(*memberFunc);
                }
            }
        }
    }

    if (auto extendDecl = DynamicCast<const ExtendDecl*>(&decl)) {
        // do not use this function on direct extends cause it needs to merge all same extends
        // first and calculate the body hash later
        if (extendDecl->inheritedTypes.empty()) {
            return a.value;
        }
    }

    if (auto typeDecl = DynamicCast<const InheritableDecl*>(&decl)) {
        auto visibleAPIs = GetVisibleAPIs(typeDecl->GetMemberDeclPtrs());
        std::stable_sort(visibleAPIs.begin(), visibleAPIs.end(),
            [](const auto& a, const auto& b) { return a->rawMangleName < b->rawMangleName; });
        for (auto visibleAPI : visibleAPIs) {
            a.HashMemberSignature(*visibleAPI);
            a.HashSpecificModifiers(*visibleAPI, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::INTERNAL,
                TokenKind::PRIVATE, TokenKind::MUT, TokenKind::STATIC});
        }
    }

    return a.value;
}

ASTHasher::hash_type ASTHasher::ImportedDeclBodyHash(const AST::Decl& decl)
{
    auto originalBodyHash = decl.hash.bodyHash;

    // For source imported decls which has body, it might be semantically changed even if the
    // source code is not changed. For example, a generic function `A` calls a non-generic function
    // `B` inside, if the signature of `B` changes, the `A` will be polluted. Yet the Sema doesn't
    // trace the dependency between `A` and `B` here (cause it is in up-stream pacakge), so we have
    // to take care of the dependency here.
    ASTHasherImpl a{};
    auto newBodyHash = ASTHasher::BodyHash(decl, {false, false});
    a.CombineTwoHashes(originalBodyHash);
    a.CombineTwoHashes(newBodyHash);
    return a.value;
}

ASTHasher::hash_type ASTHasher::VirtualHash(const Decl& decl)
{
    // The virtual hash of interface is handled specially
    CJC_ASSERT(decl.astKind != ASTKind::INTERFACE_DECL);

    std::vector<Ptr<Decl>> virtuals;
    for (auto member : decl.GetMemberDeclPtrs()) {
        if (IncrementalCompilation::IsVirtual(*member)) {
            virtuals.push_back(member);
        }
    }
    ASTHasherImpl a{};
    for (auto member : std::as_const(virtuals)) {
        a.HashMemberSignature(*member);
    }
    return a.value;
}
} // namespace Cangjie::AST
