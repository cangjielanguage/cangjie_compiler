// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements name mangling utils.
 */

#include "cangjie/Mangle/BaseMangler.h"

#include <sstream>

#include "Compression.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/StdUtils.h"

using namespace Cangjie;
using namespace AST;
using namespace Meta;

namespace {
Ptr<const Decl> GetParentDecl(const FuncDecl& funcDecl)
{
    auto fd = RawStaticCast<const FuncDecl*>(&funcDecl);
    auto outerDecl = funcDecl.outerDecl;
    if (fd->funcBody->parentClassLike != nullptr) {
        outerDecl = fd->funcBody->parentClassLike;
    } else if (fd->funcBody->parentStruct != nullptr) {
        outerDecl = fd->funcBody->parentStruct;
    } else if (fd->funcBody->parentEnum != nullptr) {
        outerDecl = fd->funcBody->parentEnum;
    }
    return outerDecl;
}

const std::unordered_map<std::string, std::unordered_map<std::string, std::string>> SPECIAL_FUNC_NAMES = {
    {CORE_PACKAGE_NAME,
        {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
            {"throwGlobalOutOfMemoryError", "rt$ThrowGlobalOutOfMemoryError"},
            {"throwStackOverflowError", "rt$ThrowStackOverflowError"},
#endif
            {"throwOutOfMemoryError", "rt$ThrowOutOfMemoryError"},
            {"throwImplicitException", "rt$ThrowImplicitException"},
            {"throwArithmeticException", "rt$ThrowArithmeticException"},
            {"throwArithmeticExceptionMsg", "rt$ThrowArithmeticException_msg"},
            {"createArithmeticExceptionMsg", "rt$CreateArithmeticException_msg"},
            {"createOverflowExceptionMsg", "rt$CreateOverflowException_msg"},
            {"throwOverflowException", "rt$ThrowOverflowException"},
            {"throwIndexOutOfBoundsException", "rt$ThrowIndexOutOfBoundsException"},
            {"throwNegativeArraySizeException", "rt$ThrowNegativeArraySizeException"},
            {"callToString", "rt$CallToString"},
            {"checkIsError", "rt$CheckIsError"},
            {"throwError", "rt$ThrowError"},
            {"callPrintStackTrace", "rt$CallPrintStackTrace"},
            {"callPrintStackTraceError", "rt$CallPrintStackTraceError"},
        }},
};

std::optional<std::string> GetSpecialFuncName(const FuncDecl& fd)
{
    auto found = SPECIAL_FUNC_NAMES.find(fd.fullPackageName);
    if (found == SPECIAL_FUNC_NAMES.end()) {
        return std::nullopt;
    }
    auto& nameMap = found->second;
    if (auto has = nameMap.find(fd.identifier); has != nameMap.end()) {
        return {has->second};
    }
    return std::nullopt;
}

inline std::string ToHex(uint64_t val)
{
    std::stringstream ss;
    constexpr int width = 2;
    ss << MANGLE_DOLLAR_PREFIX << std::hex << std::uppercase << std::setfill('0') << std::setw(width) << val;
    return ss.str();
}

bool IsMangledToExportForTestBefore(const Decl& decl, const std::vector<Ptr<Node>>& prefix)
{
    if (decl.TestAttr(Attribute::FOR_TEST)) {
        return true;
    }
    for (auto& node : prefix) {
        if (node->TestAttr(Attribute::FOR_TEST)) {
            return true;
        }
    }
    return false;
}

uint32_t Fnv1aHash(const std::string& str)
{
    constexpr uint32_t fnvOffsetBasis = 2166136261;
    constexpr uint32_t fnvPrime = 16777619;

    uint32_t hash = fnvOffsetBasis;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= fnvPrime;
    }
    return hash;
}

std::string ToBase62(uint64_t value)
{
    const std::string base62Chars = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

    std::string result;
    while (value > 0) {
        result.push_back(base62Chars[value % base62Chars.size()]);
        value /= base62Chars.size();
    }
    // The length of hash is 13.
    size_t length = 13;
    while (result.size() < length) {
        result.push_back(base62Chars[0]);
    }

    std::reverse(result.begin(), result.end());
    return result.size() > length ? result.substr(0, length) : result;
}
} // namespace

namespace Cangjie::MangleUtils {
std::string MangleFilePrivate(const AST::Decl& decl)
{
    constexpr size_t cjExtLen = 3;
    constexpr size_t maxLen = 16;
    CJC_NULLPTR_CHECK(decl.curFile);
    const size_t filenameLen = decl.curFile->fileName.size();
    if (filenameLen <= maxLen + cjExtLen) {
        return MangleUtils::MangleName(MANGLE_DOLLAR_PREFIX + decl.curFile->fileName.substr(0, filenameLen - cjExtLen));
    }
    return MangleUtils::MangleName(ToHex(decl.curFile->fileHash));
}

std::string DecimalToManglingNumber(const std::string& decimal)
{
    const std::string base62Chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const int base62num = static_cast<int>(base62Chars.size());
    for (size_t i = 0; i < decimal.length(); i++) {
        if (!std::isdigit(decimal[i])) {
            return "";
        }
    }

    auto numOpt = Stoi(decimal);
    if (!numOpt.has_value()) {
        return "";
    }
    int num = *numOpt - 1;
    if (num < 0) {
        return MANGLE_WILDCARD_PREFIX;
    }
    std::string base62 = num > 0 ? "" : "0";
    while (num > 0) {
        base62 += base62Chars[static_cast<size_t>(num % base62num)];
        num /= base62num;
    }
    return base62 + MANGLE_WILDCARD_PREFIX;
}

std::string MangleModePayload(ModalInfo modal)
{
    // Fixed axis order: local -> unique -> immutable. Only the local axis is implemented.
    // axis-degree: uppercase letter = FULL (!), lowercase = HALF (?); NOT emits nothing.
    std::string payload;
    switch (modal.local) {
        case Mode::NOT:
            break;
        case Mode::HALF:
            payload += "l";
            break;
        case Mode::FULL:
            payload += "L";
            break;
        default:
            CJC_ABORT();
    }
    return payload;
}

std::string MangleTypeMode(ModalInfo modal)
{
    std::string payload = MangleModePayload(modal);
    if (payload.empty()) {
        return "";
    }
    return MANGLE_TYPE_MODE_PREFIX + payload + MANGLE_SUFFIX;
}

std::string MangleTypeMode(CHIR::ModalInfo modal)
{
    // Fixed axis order: local -> unique -> immutable. Only the local axis is implemented.
    // axis-degree: uppercase letter = MUST (!), lowercase = MAYBE (?); NONE emits nothing.
    std::string payload;
    switch (modal.Local()) {
        case CHIR::Mode::NONE:
            break;
        case CHIR::Mode::MAYBE:
            payload += "l";
            break;
        case CHIR::Mode::MUST:
            payload += "L";
            break;
        default:
            CJC_ASSERT(false && "unexpected modal to be mangled");
            return "";
    }
    if (payload.empty()) {
        return "";
    }
    return MANGLE_TYPE_MODE_PREFIX + payload + MANGLE_SUFFIX;
}

std::string MangleThisMode(ModalInfo modal)
{
    std::string payload = MangleModePayload(modal);
    if (payload.empty()) {
        return "";
    }
    return MANGLE_THIS_MODE_PREFIX + payload + MANGLE_SUFFIX;
}
} // namespace MangleUtils

std::string BaseMangler::MangleFullPackageName(const std::string& packageName) const
{
    std::string orgName;
    std::string newPkgName = packageName;
    size_t pos = packageName.find(TOKENS[static_cast<int>(TokenKind::DOUBLE_COLON)]);
    if (pos != std::string::npos) {
        orgName = packageName.substr(0, pos + 1);
        auto sz = std::string_view{TOKENS[static_cast<int>(TokenKind::DOUBLE_COLON)]}.size();
        if (packageName.length() > pos + sz) {
            newPkgName = packageName.substr(pos + sz, packageName.length() - pos - sz);
        } else {
            newPkgName = "";
        }
    }
    if (MangleUtils::STD_PKG_MANGLE.find(newPkgName) != MangleUtils::STD_PKG_MANGLE.end()) {
        return MangleUtils::STD_PKG_MANGLE.at(newPkgName);
    }
    return MangleUtils::MangleName(orgName + newPkgName);
}

std::string BaseMangler::MangleFullPackageName(const AST::Decl& decl) const
{
    return MangleFullPackageName(decl.GetFullPackageName());
}

std::string BaseMangler::GetPrefixOfType(const Ty& ty) const
{
    switch (ty.kind) {
        case TypeKind::TYPE_ENUM: {
            return MANGLE_TYPE_ENUM_PREFIX;
        }
        case TypeKind::TYPE_INTERFACE:
        case TypeKind::TYPE_CLASS: {
            return MANGLE_TYPE_CLASS_PREFIX;
        }
        case TypeKind::TYPE_STRUCT: {
            return MANGLE_TYPE_STRUCT_PREFIX;
        }
        default: {
            CJC_ASSERT(false && "unexpected type to be mangled");
            return "";
        }
    }
}

std::string BaseMangler::MangleUserDefinedType(
    ModalTy ty, std::vector<std::string>& genericsTypeStack, bool declare, bool isCollectGTy) const
{
    static const std::set<TypeKind> SUPPORT_TYS = {
        TypeKind::TYPE_ENUM, TypeKind::TYPE_STRUCT, TypeKind::TYPE_CLASS, TypeKind::TYPE_INTERFACE};
    if (SUPPORT_TYS.find(ty.Kind()) == SUPPORT_TYS.end()) {
        CJC_ASSERT(false && "unexpected type to be mangled");
        return "";
    }
    auto decl = Ty::GetDeclOfTy(ty.Ty());
    CJC_NULLPTR_CHECK(decl);
    if (MangleUtils::IsAutoBoxedBaseDecl(*decl)) {
        return decl->mangledName;
    }
    // Re-mangle a ty is to make different versions of instantiation have the same type. Therefore, remove the package
    // name where the generic is instantiated, and ignore the package where extend behavior occurs rather than use its
    // decl's `mangledName`.
    auto mangledName = GetPrefixOfType(*ty) + MANGLE_NESTED_PREFIX;
    std::string genericPkgName = ManglePackageNameForGeneric(*decl);
    mangledName += genericPkgName.empty() ? MangleFullPackageName(*decl) : genericPkgName;
    if (decl->TestAttr(Attribute::PRIVATE) && decl->linkage == Linkage::INTERNAL) {
        std::string fileName = decl->curFile->fileName;
        mangledName += MANGLE_FILE_ID_PREFIX +
            (IsHashable(fileName) ? HashToBase62(fileName)
                                  : (FileNameWithoutExtension(fileName) + MANGLE_DOLLAR_PREFIX));
    }
    mangledName += MangleUtils::MangleName(decl->identifier);
    if (decl->GetGeneric()) {
        mangledName += MANGLE_GENERIC_PREFIX;
    }
    for (auto arg : ty->typeArgs) {
        mangledName += MangleType(arg, genericsTypeStack, declare, isCollectGTy);
    }
    mangledName += MANGLE_SUFFIX;
    return MangleUtils::WithModal(mangledName, ty.Mode());
}

std::string BaseMangler::MangleType(ModalTy ty) const
{
    std::vector<std::string> genericsTypeStack;
    return MangleType(ty, genericsTypeStack, true);
}

std::string BaseMangler::MangleType(const Ty& ty) const
{
    return MangleType(ModalTy{DataTy(const_cast<Ty*>(&ty))});
}

std::string BaseMangler::MangleType(
    ModalTy ty, std::vector<std::string>& genericsTypeStack, bool declare, bool isCollectGTy) const
{
    if (MangleUtils::PRIMITIVE_TYPE_MANGLE.count(ty.Kind()) != 0) {
        return MangleUtils::WithModal(MangleUtils::PRIMITIVE_TYPE_MANGLE.at(ty.Kind()), ty.Mode());
    }

    if (ty.Kind() == TypeKind::TYPE_GENERICS) {
        if (declare || isCollectGTy) {
            return MangleGenericType(ty, genericsTypeStack, declare);
        } else {
            return MangleGenericType(ty);
        }
    } else if (ty.Kind() == TypeKind::TYPE_POINTER) {
        return MangleCPointerType(ty, genericsTypeStack, declare, isCollectGTy);
    } else if (ty.Kind() == TypeKind::TYPE_ENUM) {
        return MangleEnumType(ty, genericsTypeStack, declare, isCollectGTy);
    } else if (ty.Kind() == TypeKind::TYPE_ARRAY) {
        return MangleRawArrayType(ty, genericsTypeStack, declare, isCollectGTy);
    } else if (ty.Kind() == TypeKind::TYPE_VARRAY) {
        return MangleVArrayType(ty, genericsTypeStack, declare, isCollectGTy);
    } else if (ty.Kind() == TypeKind::TYPE_TUPLE) {
        return MangleTupleType(ty, genericsTypeStack, declare, isCollectGTy);
    } else if (ty.Kind() == TypeKind::TYPE_FUNC) {
        return MangleFuncType(ty, genericsTypeStack, declare, isCollectGTy);
    } else if (ty.Kind() == TypeKind::TYPE_STRUCT || ty.Kind() == TypeKind::TYPE_INTERFACE ||
        ty.Kind() == TypeKind::TYPE_CLASS) {
        return MangleUserDefinedType(ty, genericsTypeStack, declare, isCollectGTy);
    } else if (ty.Kind() == TypeKind::TYPE_CSTRING) {
        return MangleCStringType(ty);
    }
    return "";
}

std::string BaseMangler::MangleGenericArgumentsHelper(const Decl& decl, std::vector<std::string>& genericsTypeStack,
    bool declare, bool isCollectGTy) const
{
    std::string mangled;
    std::vector<ModalTy> args;

    if (decl.GetTy() && !decl.GetTy()->IsFunc()) {
        args = decl.GetTy()->typeArgs;
    } else if (decl.astKind == ASTKind::FUNC_DECL) {
        auto& fd = StaticCast<const FuncDecl&>(decl);
        if (fd.funcBody->generic) {
            for (auto& arg : fd.funcBody->generic->typeParameters) {
                args.push_back(arg->GetTy());
            }
        }
    }
    if (args.empty()) {
        return "";
    }
    mangled += MANGLE_GENERIC_PREFIX;
    for (auto it : args) {
        mangled += MangleType(it, genericsTypeStack, declare, isCollectGTy);
    }
    return mangled;
}

std::string BaseMangler::MangleGenericArguments(const Decl& decl, std::vector<std::string>& genericsTypeStack,
    bool declare) const
{
    return MangleGenericArgumentsHelper(decl, genericsTypeStack, declare, false);
}

std::string BaseMangler::MangleVarDecl(const AST::Decl& decl, const std::vector<Ptr<AST::Node>>& prefix) const
{
    std::string mangleStr = MANGLE_CANGJIE_PREFIX + MANGLE_NESTED_PREFIX;
    std::vector<std::string> genericsTypeStack;
    mangleStr += ManglePrefix(decl, prefix, genericsTypeStack, false);
    mangleStr += MangleUtils::MangleName(decl.identifier.Val());

    if (IsLocalVariable(decl) && manglerCtxTable.find(decl.fullPackageName) != manglerCtxTable.end()) {
        mangleStr += MANGLE_COUNT_PREFIX;
        auto manglerCtx = manglerCtxTable.at(decl.fullPackageName).get();
        if (!manglerCtx) {
            return "";
        }
        auto findOuterNodeOfLocalVariable = [&prefix]() {
            for (auto item = prefix.rbegin(); item != prefix.rend(); ++item) {
                auto curPrefix = *item;
                if (curPrefix->astKind == AST::ASTKind::FUNC_DECL ||
                    curPrefix->astKind == AST::ASTKind::PRIMARY_CTOR_DECL ||
                    curPrefix->astKind == AST::ASTKind::LAMBDA_EXPR ||
                    curPrefix->astKind == AST::ASTKind::CLASS_DECL ||
                    curPrefix->astKind == AST::ASTKind::INTERFACE_DECL ||
                    curPrefix->astKind == AST::ASTKind::STRUCT_DECL ||
                    // For VarDecl and VarWithPatternDecl, we only handle it when it is global
                    (Is<VarDeclAbstract>(curPrefix) && curPrefix->TestAttr(Attribute::GLOBAL))) {
                    return curPrefix;
                }
            }
            return Ptr<AST::Node>();
        };
        Ptr<Node> outerNode = findOuterNodeOfLocalVariable();
        if (outerNode) {
            if (outerNode->astKind == AST::ASTKind::CLASS_DECL ||
                outerNode->astKind == AST::ASTKind::INTERFACE_DECL ||
                outerNode->astKind == AST::ASTKind::STRUCT_DECL) {
                // Member variable of class/interface/struct must be counted as 0.
                mangleStr += MangleUtils::DecimalToManglingNumber("0");
            } else {
                // Local variable need to be counted from the scope of outerNode.
                std::optional<size_t> index = manglerCtx->GetIndexOfVar(outerNode,
                    StaticCast<const AST::VarDecl*>(&decl));
                CJC_ASSERT(index.has_value() && "index of local variable is not found.");
                mangleStr += MangleUtils::DecimalToManglingNumber(std::to_string(index.value()));
            }
        }
    }
    mangleStr += MANGLE_SUFFIX;
    return mangleStr;
}

std::optional<size_t> BaseMangler::GetIndexOfWildcard(
    const AST::VarWithPatternDecl& vwpDecl, const std::vector<Ptr<AST::Node>>& prefix) const
{
    CJC_ASSERT(ManglerContext::CheckAllElementsWildcard(vwpDecl.irrefutablePattern.get()));
    std::string pkgName = ManglerContext::ReduceUnitTestPackageName(vwpDecl.fullPackageName);
    CJC_ASSERT(!pkgName.empty() && "pkgName of varWithPatternDecl is empty.");
    CJC_ASSERT(manglerCtxTable.find(pkgName) != manglerCtxTable.end() && "can not find pkgName in manglerCtxTable.");
    auto mangleCtx = manglerCtxTable.at(pkgName);
    CJC_NULLPTR_CHECK(mangleCtx.get());
    std::optional<size_t> index = {};
    if (vwpDecl.TestAttr(Attribute::GLOBAL)) {
        index = mangleCtx->GetIndexOfGlobalWildcardVar(vwpDecl.curFile, &vwpDecl);
    } else {
        for (auto item = prefix.rbegin(); item != prefix.rend(); ++item) {
            auto curPrefix = *item;
            if (curPrefix->astKind == AST::ASTKind::FUNC_DECL ||
                curPrefix->astKind == AST::ASTKind::PRIMARY_CTOR_DECL ||
                curPrefix->astKind == AST::ASTKind::LAMBDA_EXPR || curPrefix->astKind == AST::ASTKind::CLASS_DECL ||
                curPrefix->astKind == AST::ASTKind::INTERFACE_DECL || curPrefix->astKind == AST::ASTKind::STRUCT_DECL ||
                // For VarDecl and VarWithPatternDecl, we only handle it when it is global
                (Is<VarDeclAbstract>(curPrefix) && curPrefix->TestAttr(Attribute::GLOBAL))) {
                index = mangleCtx->GetIndexOfLocalWildcardVar(curPrefix, &vwpDecl);
                break;
            }
        }
    }
    CJC_ASSERT(index.has_value() && "index of varWithPatternDecl has no value.");
    return index;
}

std::string BaseMangler::MangleVarWithPatternDecl(
    const AST::VarWithPatternDecl& vwpDecl, const ::std::vector<Ptr<AST::Node>>& prefix) const
{
    std::string mangleStr = "";
    std::function<void(Pattern*)> patternWalkThrough = [this, &patternWalkThrough, &prefix, &mangleStr](
                                                           Pattern* pattern) {
        if (!mangleStr.empty()) {
            return;
        }
        if (auto varPattern = DynamicCast<VarPattern*>(pattern); varPattern) {
            mangleStr = MangleVarDecl(*varPattern->varDecl, prefix);
        }

        if (auto tuplePattern = DynamicCast<TuplePattern*>(pattern); tuplePattern) {
            for (auto& item : tuplePattern->patterns) {
                patternWalkThrough(item.get());
            }
        } else if (auto enumPattern = DynamicCast<EnumPattern*>(pattern); enumPattern) {
            for (auto& item : enumPattern->patterns) {
                patternWalkThrough(item.get());
            }
        }
    };

    // In the case `let (x, _) = (1, 2)`, we mangle the VarWithPatternDecl `(x, _)` as Var `x`.
    if (auto tp = DynamicCast<TuplePattern>(vwpDecl.irrefutablePattern.get())) {
        for (auto& pattern : tp->patterns) {
            patternWalkThrough(pattern.get());
            if (!mangleStr.empty()) {
                return mangleStr;
            }
        }
    } else if (auto ep = DynamicCast<EnumPattern>(vwpDecl.irrefutablePattern.get())) {
        for (auto& pattern : ep->patterns) {
            patternWalkThrough(pattern.get());
            if (!mangleStr.empty()) {
                return mangleStr;
            }
        }
    }

    std::optional<size_t> index = GetIndexOfWildcard(vwpDecl, prefix);
    CJC_ASSERT_WITH_MSG(index.has_value(), "Index of wildcard should not be empty");
    mangleStr += MANGLE_CANGJIE_PREFIX + MANGLE_NESTED_PREFIX;
    std::vector<std::string> genericsTypeStack;
    mangleStr += ManglePrefix(vwpDecl, prefix, genericsTypeStack, true);
    if (vwpDecl.TestAttr(Attribute::GLOBAL)) {
        auto fileName = vwpDecl.curFile->fileName;
        mangleStr += MANGLE_FILE_ID_PREFIX +
            (IsHashable(fileName) ? HashToBase62(fileName)
                                  : (FileNameWithoutExtension(fileName) + MANGLE_DOLLAR_PREFIX));
    }
    mangleStr += MANGLE_ANONYMOUS_VARIABLE_PREFIX;
    mangleStr += MangleUtils::DecimalToManglingNumber(std::to_string(index.value()));
    mangleStr += MANGLE_SUFFIX;
    return mangleStr;
}

std::optional<std::string> BaseMangler::MangleEntryFunction(const FuncDecl& funcDecl) const
{
    // Change user main function to user.main, so that the function entry can be changed to RuntimeMain.
    // Initialization of light-weight thread scheduling can be performed in runtime.main.
    if (funcDecl.identifier == MAIN_INVOKE && funcDecl.TestAttr(Attribute::GLOBAL)) {
        return USER_MAIN_MANGLED_NAME;
    }
    if (funcDecl.identifier == TEST_ENTRY_NAME) {
        return std::string(TEST_ENTRY_NAME);
    }
    return std::nullopt;
}

std::string BaseMangler::Mangle(const Decl& decl) const
{
    std::vector<Ptr<Node>> prefix = {};
    auto outerDecl = decl.outerDecl;
    while (outerDecl) {
        prefix.insert(prefix.begin(), outerDecl);
        outerDecl = outerDecl->outerDecl;
    }
    return Mangle(decl, prefix);
}

std::string BaseMangler::Mangle(const Decl& decl, const std::vector<Ptr<Node>>& prefix) const
{
    if (decl.TestAttr(Attribute::NO_MANGLE)) {
        return decl.identifier;
    }
    std::vector<std::string> genericsTypeStack;
    switch (decl.astKind) {
        case ASTKind::VAR_DECL:
            return Compression::CJMangledCompression(MangleVarDecl(decl, prefix));
        case ASTKind::FUNC_DECL:
            return Compression::CJMangledCompression(MangleFunctionDecl(StaticCast<const FuncDecl&>(decl), prefix,
                genericsTypeStack, true));
        case ASTKind::VAR_WITH_PATTERN_DECL:
            return Compression::CJMangledCompression(MangleVarWithPatternDecl(
                StaticCast<const AST::VarWithPatternDecl&>(decl), prefix));
        default:
            return Compression::CJMangledCompression(MangleDecl(decl, prefix, genericsTypeStack, true));
    }
}

std::string BaseMangler::MangleFunctionDecl(const FuncDecl& funcDecl, const std::vector<Ptr<Node>>& prefix,
    std::vector<std::string>& genericsTypeStack, bool /* declare */) const
{
    // If the decl has already been mangled.
    if (!funcDecl.mangledName.empty()) {
        return funcDecl.mangledName;
    }
    // Quick fix: for those const instantiated generic funcs, they are lifted to toplevel and the prefix is empty.
    // we use the chain of outerDecl as the prefix and generate the mangled name.(currently there is no number count.)
    if (funcDecl.TestAttr(Attribute::GENERIC_INSTANTIATED) && prefix.empty()) {
        auto outerDecl = funcDecl.outerDecl;
        std::vector<Ptr<Node>> newPrefix;
        while (outerDecl != nullptr) {
            newPrefix.insert(newPrefix.begin(), outerDecl);
            outerDecl = outerDecl->outerDecl;
        }
        std::string mangled = MangleDecl(funcDecl, newPrefix, genericsTypeStack, true, false);
        mangled += MANGLE_FUNC_PARAM_TYPE_PREFIX;
        mangled += MangleFuncParams(funcDecl, genericsTypeStack, false);
        return mangled;
    }
    // For built-in hard-coding functions.
    if (funcDecl.outerDecl == nullptr) {
        if (auto specialName = GetSpecialFuncName(funcDecl); specialName) {
            return specialName.value();
        }
    }
    // For built-in hard-coding entry functions.
    std::string mangled = MangleEntryFunction(funcDecl).value_or("");
    if (!mangled.empty()) {
        return mangled;
    }
    if (funcDecl.TestAttr(Attribute::HAS_INITIAL)) {
        std::string param = funcDecl.identifier;
        CJC_NULLPTR_CHECK(funcDecl.ownerFunc);
        // Use function mangle(contains params types) to identify params which have same name in overloading function.
        // eg: func foo3(x!:Int32=1):Int32 // f1
        //     func foo3(x!:Int64=1):Int32 // f2
        //     default param functions for f1,f2 need to have different mangle names.
        auto outerDecl = funcDecl.ownerFunc->outerDecl;
        std::vector<Ptr<Node>> newPrefix;
        newPrefix.insert(newPrefix.begin(), funcDecl.ownerFunc);
        while (outerDecl != nullptr) {
            newPrefix.insert(newPrefix.begin(), outerDecl);
            outerDecl = outerDecl->outerDecl;
        }
        std::string mangledPrefix = MANGLE_CANGJIE_PREFIX + MANGLE_NESTED_PREFIX;
        std::string mangledOwnerFunc = MangleDecl(funcDecl, newPrefix, genericsTypeStack, true, true);
        mangled += MANGLE_CANGJIE_PREFIX + MANGLE_FUNC_PARA_INIT_PREFIX +
            mangledOwnerFunc.substr(mangledPrefix.size(), mangledOwnerFunc.size() - mangledPrefix.size() - 1);
    } else {
        mangled = MangleDecl(funcDecl, prefix, genericsTypeStack, true, false);
    }
    mangled += MANGLE_FUNC_PARAM_TYPE_PREFIX;
    // Add local modal mangling for instance member functions with local thisParam.
    // Check if it's an instance member function (has thisParam and not static).
    // <this-mode> (W<payload>E) is emitted right after `H`, before the explicit params.
    if (!funcDecl.TestAttr(Attribute::STATIC) && funcDecl.funcBody) {
        auto& paramList = funcDecl.funcBody->paramLists[0];
        if (paramList->thisParam) {
            auto modalInfo = paramList->thisParam->modal.ToModalInfo();
            mangled += MangleUtils::MangleThisMode(modalInfo);
        } else if (funcDecl.propDecl != nullptr) {
            // Accessor (getter/setter) no longer carries an explicit thisParam; its this-modal
            // is the modal of the owning property's type.
            auto propTy = funcDecl.propDecl->type ? funcDecl.propDecl->type->GetTy()
                : funcDecl.propDecl->GetTy();
            mangled += MangleUtils::MangleThisMode(propTy.Mode());
        }
    }

    // Mangle function parameters.
    mangled += MangleFuncParams(funcDecl, genericsTypeStack, false);
    return mangled;
}

std::string BaseMangler::MangleDecl(const Decl& decl, const std::vector<Ptr<Node>>& prefix,
    std::vector<std::string>& genericsTypeStack, bool /* declare */, bool withSuffix) const
{
    if (!decl.mangledName.empty() && IsMangledToExportForTestBefore(decl, prefix)) {
        return decl.mangledName;
    }

    std::string newMangled = MANGLE_CANGJIE_PREFIX + MANGLE_NESTED_PREFIX;
    if (decl.astKind == ASTKind::VAR_WITH_PATTERN_DECL) {
        newMangled += ManglePackage(decl);
        if (DynamicCast<const AST::VarWithPatternDecl*>(&decl) != nullptr) {
            newMangled += MangleVarWithPatternDecl(*DynamicCast<const AST::VarWithPatternDecl*>(&decl), prefix);
        }
    } else if (decl.astKind == ASTKind::EXTEND_DECL) {
        newMangled += ManglePrefix(decl, prefix, genericsTypeStack, true);
        newMangled += MangleExtendEntity(StaticCast<const ExtendDecl&>(decl), genericsTypeStack, true);
        newMangled += MangleGenericArguments(decl, genericsTypeStack, true);
    } else {
        newMangled = newMangled + ManglePrefix(decl, prefix, genericsTypeStack, true);
        // Add additional angle brackets to avoid duplication with raw identifier names.
        bool useInternalIdent = decl.TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::MAIN_ENTRY) &&
            decl.astKind != ASTKind::PRIMARY_CTOR_DECL;
        if (useInternalIdent) {
            std::string staticPrefix = decl.TestAttr(Attribute::STATIC)
                && decl.identifier != STATIC_INIT_FUNC ? "static." : "";
            newMangled += MangleUtils::MangleName(MANGLE_LT_PREFIX + staticPrefix + decl.identifier +
                MANGLE_GT_PREFIX);
        } else if (decl.TestAnyAttr(AST::Attribute::OPERATOR) &&
            MangleUtils::OPERATOR_TYPE_MANGLE.count(decl.identifier) != 0) {
            newMangled += MangleUtils::OPERATOR_TYPE_MANGLE.at(decl.identifier);
        } else if (decl.identifier.Val().find(MANGLE_DOLLAR_PREFIX, 0) == 0 &&
            (decl.identifier.Val().rfind(SPECIAL_NAME_FOR_SET) ==
            (decl.identifier.Val().size() - MANGLE_PROP_LEN) ||
            decl.identifier.Val().rfind(SPECIAL_NAME_FOR_GET) ==
            (decl.identifier.Val().size() - MANGLE_PROP_LEN))) {
            // MANGLE_CHAR_LEN is the length of "$" and MANGLE_PROP_LEN is "set"/"get".
            newMangled += MangleUtils::MangleName(decl.identifier.Val().substr(MANGLE_CHAR_LEN,
                decl.identifier.Val().size() - MANGLE_CHAR_LEN - MANGLE_PROP_LEN));
            newMangled += decl.identifier.Val().rfind(SPECIAL_NAME_FOR_SET) ==
                (decl.identifier.Val().size() - MANGLE_PROP_LEN) ? "ps" : "pg";
        } else {
            std::string name = decl.identifier;
            if (decl.astKind == ASTKind::GENERIC_PARAM_DECL && decl.outerDecl &&
                decl.outerDecl->TestAnyAttr(Attribute::COMMON, Attribute::SPECIFIC) &&
                decl.outerDecl->TestAttr(Attribute::GENERIC)) {
                // safe here because a generic param decl itself cannot have modal type
                auto genericsTy = StaticCast<const GenericsTy*>(decl.DataTy());
                auto result = std::find_if(genericsTypeStack.rbegin(), genericsTypeStack.rend(),
                    [&genericsTy](const std::string& name) { return name == genericsTy->name; });
                auto index = static_cast<size_t>(std::distance(result, genericsTypeStack.rend())) - MANGLE_CHAR_LEN;
                CJC_ASSERT(result != genericsTypeStack.rend() && "Using undeclared generic type!");
                auto number = MangleUtils::DecimalToManglingNumber(std::to_string(index));
                name = MANGLE_GENERIC_TYPE_PREFIX + number;
            } else if (decl.TestAttr(Attribute::HAS_INITIAL)) {
                // eg: c.2 ==> pos = 1, name = c, idx = "2"
                size_t pos = decl.identifier.Val().find(MANGLE_DOT_PREFIX);
                name = decl.identifier.Val().substr(0, pos);
            }
            newMangled += MangleUtils::MangleName(name);
            if (decl.IsFunc()) {
                auto& funcDecl = StaticCast<const AST::FuncDecl&>(decl);
                newMangled +=
                    IsLocalFunc(funcDecl) ? GetMangledLocalFuncIndex(StaticCast<const FuncDecl&>(decl)) : "";
            }
        }
        newMangled += MangleGenericArguments(decl, genericsTypeStack, true);
    }
    if (withSuffix) {
        newMangled += MANGLE_SUFFIX;
    }
    return newMangled;
}

std::string BaseMangler::MangleExtendEntity(const AST::ExtendDecl& extendDecl,
    std::vector<std::string>& genericsTypeStack, bool declare) const
{
    CJC_NULLPTR_CHECK(extendDecl.extendedType);
    CJC_NULLPTR_CHECK(extendDecl.extendedType->GetTy());
    std::string mangled;
    mangled += MANGLE_EXTEND_PREFIX + MangleType(extendDecl.extendedType->GetTy(), genericsTypeStack, declare);
    auto ctx = manglerCtxTable.find(ManglerContext::ReduceUnitTestPackageName(extendDecl.fullPackageName));
    CJC_ASSERT(ctx != manglerCtxTable.end());
    std::string fileName = extendDecl.curFile->fileName;
    mangled += MANGLE_FILE_ID_PREFIX +
        (IsHashable(fileName) ? HashToBase62(fileName) : (FileNameWithoutExtension(fileName) + MANGLE_DOLLAR_PREFIX));
    auto index = ctx->second->GetIndexOfExtend(extendDecl.curFile, &extendDecl);
    CJC_ASSERT(index.has_value());
    mangled += MANGLE_COUNT_PREFIX+ MangleUtils::DecimalToManglingNumber(std::to_string(index.value()));
    return mangled;
}

std::string BaseMangler::ManglePrefix(const Node& node, const std::vector<Ptr<Node>>& prefix,
    std::vector<std::string>& genericsTypeStack, bool declare) const
{
    std::string mangled = "";
    Ptr<Node> outerMostNode = nullptr;
    for (auto item = prefix.begin(); item != prefix.end(); ++item) {
        auto curPrefix = *item;
        switch (curPrefix->astKind) {
            case ASTKind::CLASS_DECL:
            case ASTKind::INTERFACE_DECL:
            case ASTKind::ENUM_DECL:
            case ASTKind::STRUCT_DECL:
            case ASTKind::BUILTIN_DECL: {
                auto& decl = StaticCast<const Decl&>(*curPrefix);
                mangled += MangleUtils::MangleName(decl.identifier);
                if (auto genericArgs = MangleGenericArgumentsHelper(decl, genericsTypeStack, true);
                    !genericArgs.empty()) {
                    mangled += genericArgs + MANGLE_SUFFIX;
                }
                break;
            }
            case ASTKind::VAR_DECL:
                if (curPrefix->TestAttr(Attribute::GLOBAL)) {
                    mangled += MangleUtils::MangleName(StaticCast<const Decl&>(*curPrefix).identifier);
                }
                break;
            case ASTKind::FUNC_DECL: {
                auto& decl = StaticCast<const FuncDecl&>(*curPrefix);
                mangled += MangleUtils::MangleName(decl.identifier);
                if (IsLocalFunc(decl)) {
                    mangled += GetMangledLocalFuncIndex(decl);
                }
                mangled += MangleGenericArgumentsHelper(decl, genericsTypeStack, true) + MANGLE_FUNC_PARAM_TYPE_PREFIX +
                    MangleFuncParams(decl, genericsTypeStack, false) + MANGLE_SUFFIX;
                break;
            }
            case ASTKind::EXTEND_DECL: {
                auto& decl = StaticCast<const ExtendDecl&>(*curPrefix);
                bool isInitFunc = node.astKind == ASTKind::FUNC_DECL && node.TestAttr(Attribute::HAS_INITIAL);
                bool isExtend = (node.astKind == ASTKind::GENERIC_PARAM_DECL) ||
                    (node.TestAttr(Attribute::PRIVATE) && !isInitFunc) ||
                    (isInitFunc && StaticCast<const FuncDecl&>(node).ownerFunc->TestAttr(Attribute::PRIVATE));
                if (isExtend) {
                    mangled += MangleExtendEntity(decl, genericsTypeStack, declare);
                } else {
                    mangled += MANGLE_EXTEND_PREFIX + MangleType(decl.extendedType->GetTy(), genericsTypeStack, true);
                }
                break;
            }
            case ASTKind::LAMBDA_EXPR: {
                auto& lambda = StaticCast<const LambdaExpr&>(*curPrefix);
                // The outerNode is the outer container of lambda which can be another lambda.
                // If outerNode is a decl, we use outerNode->fullPackageName to get mangleCtx,
                // otherwise, we find a prefixDecl from prefix and use prefixDecl->fullPackageName to get mangleCtx.
                auto outerNode = FindOuterNodeOfLambda(item, prefix);
                std::string pkgName = "";
                if (auto decl = dynamic_cast<Decl*>(outerNode.get()); decl) {
                    pkgName = decl->fullPackageName;
                } else {
                    for (size_t i = 0; i < prefix.size(); i++) {
                        if (auto prefixDecl = dynamic_cast<Decl*>(prefix[i].get()); prefixDecl) {
                            pkgName = prefixDecl->fullPackageName;
                            break;
                        }
                    }
                }
                pkgName = ManglerContext::ReduceUnitTestPackageName(pkgName);
                CJC_ASSERT(!pkgName.empty() && "pkgName of lambda is empty.");
                CJC_ASSERT(manglerCtxTable.find(pkgName) != manglerCtxTable.end()
                    && "can not find pkgName in manglerCtxTable.");
                auto mangleCtx = manglerCtxTable.at(pkgName);
                CJC_NULLPTR_CHECK(mangleCtx.get());
                std::optional<size_t> index = mangleCtx->GetIndexOfLambda(outerNode, &lambda);
                mangled += MANGLE_LAMBDA_PREFIX + MangleUtils::DecimalToManglingNumber(std::to_string(index.value()));
                break;
            }
            case ASTKind::VAR_WITH_PATTERN_DECL: {
                if (!curPrefix->TestAttr(Attribute::GLOBAL)) {
                    break;
                }
                std::string mangleStr = "";
                bool patternContainsVar = false;
                std::unordered_set<VarDecl*> varDeclCollection;
                auto& vwpDecl = StaticCast<const VarWithPatternDecl&>(*curPrefix);
                std::function<void (Pattern*)> patternWalkThrough;
                std::function<void (Pattern*)> collectAllVarPattern;
                patternWalkThrough =
                    [&patternWalkThrough, &mangleStr, &patternContainsVar, &varDeclCollection](Pattern* pattern) {
                    if (!mangleStr.empty()) {
                        return;
                    }
                    if (auto varPattern = DynamicCast<VarPattern*>(pattern); varPattern) {
                        mangleStr = MangleUtils::MangleName(varPattern->varDecl->identifier);
                        if (varDeclCollection.find(varPattern->varDecl.get()) != varDeclCollection.end()  ) {
                            patternContainsVar = true;
                        }
                    }

                    if (auto tuplePattern =  DynamicCast<TuplePattern*>(pattern); tuplePattern) {
                        for (auto& item : tuplePattern->patterns) {
                            patternWalkThrough(item.get());
                        }
                    } else if (auto enumPattern =  DynamicCast<EnumPattern*>(pattern); enumPattern) {
                        for (auto& item : enumPattern->patterns) {
                            patternWalkThrough(item.get());
                        }
                    }
                };

                collectAllVarPattern = [&collectAllVarPattern, &varDeclCollection](Pattern* pattern) {
                    if (auto vp = DynamicCast<VarPattern*>(pattern); vp) {
                        varDeclCollection.emplace(vp->varDecl.get());
                        return;
                    }
                    if (auto tuplePattern =  DynamicCast<TuplePattern*>(pattern); tuplePattern) {
                        for (auto& item : tuplePattern->patterns) {
                            collectAllVarPattern(item.get());
                        }
                    } else if (auto enumPattern =  DynamicCast<EnumPattern*>(pattern); enumPattern) {
                        for (auto& item : enumPattern->patterns) {
                            collectAllVarPattern(item.get());
                        }
                    }
                };

                if (auto tp = DynamicCast<TuplePattern>(vwpDecl.irrefutablePattern.get())) {
                    for (auto& pattern : tp->patterns) {
                        collectAllVarPattern(pattern.get());
                    }
                } else if (auto ep = DynamicCast<EnumPattern>(vwpDecl.irrefutablePattern.get())) {
                    for (auto& pattern : ep->patterns) {
                        collectAllVarPattern(pattern.get());
                    }
                }

                if (auto tp = DynamicCast<TuplePattern>(vwpDecl.irrefutablePattern.get())) {
                    for (auto& pattern : tp->patterns) {
                        patternWalkThrough(pattern.get());
                        if (!mangleStr.empty()) {
                            break;
                        }
                    }
                } else if (auto ep = DynamicCast<EnumPattern>(vwpDecl.irrefutablePattern.get())) {
                    for (auto& pattern : ep->patterns) {
                        patternWalkThrough(pattern.get());
                        if (!mangleStr.empty()) {
                            break;
                        }
                    }
                }

                if (patternContainsVar) {
                    break;
                }
                if (!mangleStr.empty()) {
                    mangled += mangleStr;
                    break;
                }

                std::optional<size_t> index = GetIndexOfWildcard(vwpDecl, prefix);
                CJC_ASSERT_WITH_MSG(index.has_value(), "Index of wildcard should not be empty");
                mangleStr += MANGLE_ANONYMOUS_VARIABLE_PREFIX;
                mangleStr += MangleUtils::DecimalToManglingNumber(std::to_string(index.value()));
                mangled += mangleStr;
                break;
            }
            default:
                break;
        }
        if (outerMostNode == nullptr) {
            outerMostNode = curPrefix;
        }
    }
    // Mangle outer most node (Top level node)
    auto& outerMostNodeRef = outerMostNode ? static_cast<const Node&>(*outerMostNode) : node;
    if (outerMostNodeRef.TestAttr(Attribute::PRIVATE)) {
        std::string fileName = outerMostNodeRef.curFile.get()->fileName;
        mangled = MANGLE_FILE_ID_PREFIX +
            (IsHashable(fileName) ? HashToBase62(fileName) :
            (FileNameWithoutExtension(fileName) + MANGLE_DOLLAR_PREFIX)) + mangled;
    }
    // Mangle package
    if (node.astKind == ASTKind::LAMBDA_EXPR) {
        for (auto it = prefix.rbegin(); it != prefix.rend(); ++it) {
            if (auto decl = dynamic_cast<const Decl*>(it->get()); decl) {
                mangled = ManglePackage(*decl) + mangled;
                break;
            }
        }
    } else {
        mangled = ManglePackage(StaticCast<const Decl&>(node)) + mangled;
    }
    return mangled;
}

std::string BaseMangler::ManglePackage(const Decl& decl) const
{
    std::string genericPackageName = ManglePackageNameForGeneric(decl);
    return genericPackageName.empty() ? MangleFullPackageName(decl) : genericPackageName;
}

std::unique_ptr<ManglerContext> BaseMangler::PrepareContextForPackage(const Ptr<AST::Package> pkg)
{
    std::string pkgName = ManglerContext::ReduceUnitTestPackageName(pkg->fullPackageName);
    auto manglerCtx = std::make_unique<ManglerContext>();
    manglerCtxTable[pkgName] = manglerCtx.get();
    return manglerCtx;
}

Ptr<AST::Node> BaseMangler::FindOuterNodeOfLambda(
    const std::vector<Ptr<AST::Node>>::const_iterator& iter, const ::std::vector<Ptr<AST::Node>>& prefix) const
{
    auto prevItem = prev(iter);
    auto cnt = prevItem - prefix.begin() + 1;
    while (cnt--) {
        auto prevPrefix = *prevItem;
        prevItem = prev(prevItem);
        auto vda = dynamic_cast<VarDeclAbstract*>(prevPrefix.get());
        if (prevPrefix->astKind == AST::ASTKind::FUNC_DECL || prevPrefix->astKind == AST::ASTKind::PRIMARY_CTOR_DECL ||
            prevPrefix->astKind == AST::ASTKind::LAMBDA_EXPR || (vda && vda->TestAttr(Attribute::GLOBAL)) ||
            (vda && vda->outerDecl.get() &&
                (vda->outerDecl->astKind == ASTKind::CLASS_DECL || vda->outerDecl->astKind == ASTKind::INTERFACE_DECL ||
                    vda->outerDecl->astKind == ASTKind::STRUCT_DECL ||
                    vda->outerDecl->astKind == ASTKind::ENUM_DECL))) {
            return prevPrefix;
        }
    }
    CJC_ASSERT(false && "no outer container of lambda found.");
    return Ptr<AST::Node>();
}

std::string BaseMangler::GetMangledLocalFuncIndex(const AST::FuncDecl& decl) const
{
    auto outerNode = GetOuterDecl(decl);
    std::string pkgName = decl.fullPackageName;
    pkgName = ManglerContext::ReduceUnitTestPackageName(pkgName);
    auto mangleCtx = manglerCtxTable.at(pkgName);
    std::optional<size_t> index = mangleCtx->GetIndexOfFunc(outerNode, &decl);
    CJC_ASSERT(index.has_value());
    if (index.value() > 0) {
        return MANGLE_COUNT_PREFIX + MangleUtils::DecimalToManglingNumber(std::to_string(index.value() - 1));
    } else {
        return "";
    }
}

std::string BaseMangler::MangleLambda(const LambdaExpr& lambda, const::std::vector<Ptr<AST::Node>>& prefix) const
{
    // The outerNode is the outer container of lambda which can be another lambda.
    // If outerNode is a decl, we use outerNode->fullPackageName to get mangleCtx,
    // otherwise, we use outerDecl->fullPackageName to get mangleCtx.
    auto outerNode = FindOuterNodeOfLambda(prefix.end(), prefix);
    std::string pkgName = "";
    if (auto decl = dynamic_cast<Decl*>(outerNode.get()); decl) {
        pkgName = decl->fullPackageName;
    } else {
        for (size_t i = 0; i < prefix.size(); i++) {
            if (auto prefixDecl = dynamic_cast<Decl*>(prefix[i].get()); prefixDecl) {
                pkgName = prefixDecl->fullPackageName;
                break;
            }
        }
    }
    pkgName = ManglerContext::ReduceUnitTestPackageName(pkgName);
    CJC_ASSERT(!pkgName.empty() && "pkgName of lambda is empty.");
    CJC_ASSERT(manglerCtxTable.find(pkgName) != manglerCtxTable.end() && "can not find pkgName in manglerCtxTable.");
    auto mangleCtx = manglerCtxTable.at(pkgName);
    CJC_NULLPTR_CHECK(mangleCtx.get());
    std::optional<size_t> index = mangleCtx->GetIndexOfLambda(outerNode, &lambda);
    std::string mangleStr = MANGLE_CANGJIE_PREFIX + MANGLE_NESTED_PREFIX;
    std::vector<std::string> genericsTypeStack;
    mangleStr += ManglePrefix(lambda, prefix, genericsTypeStack, true);
    mangleStr +=
        MANGLE_LAMBDA_PREFIX + MangleUtils::DecimalToManglingNumber(std::to_string(index.value())) + MANGLE_SUFFIX;
    return Compression::CJMangledCompression(mangleStr);
}

void BaseMangler::CollectLocalDecls(ManglerContext& ctx, AST::Package& pkg) const
{
    Walker(&pkg, [&ctx](const Ptr<Node> node) {
        if (auto ed = DynamicCast<ExtendDecl>(node); ed) {
            ctx.SaveExtend2CurFile(ed->curFile, ed);
        }
        if (auto vpd = DynamicCast<VarWithPatternDecl>(node); vpd && vpd->TestAttr(Attribute::GLOBAL)) {
            ctx.SaveGlobalWildcardVar(vpd->curFile, vpd);
        }
        bool isFunc = node->astKind == ASTKind::FUNC_DECL;
        bool isLambda = node->astKind == ASTKind::LAMBDA_EXPR;
        bool isGlobal = Is<VarDeclAbstract>(node) && node->TestAttr(Attribute::GLOBAL);
        auto vda = DynamicCast<VarDeclAbstract>(node);
        bool isCompositeType = vda && vda->outerDecl &&
            (vda->outerDecl->astKind == ASTKind::CLASS_DECL || vda->outerDecl->astKind == ASTKind::INTERFACE_DECL ||
                vda->outerDecl->astKind == ASTKind::STRUCT_DECL || vda->outerDecl->astKind == ASTKind::ENUM_DECL);
        bool needCollect = isFunc || isLambda || isGlobal || isCompositeType;
        if (needCollect) {
            ctx.SaveVar2CurDecl(node);
            ctx.SaveLambda2CurDecl(node);
            ctx.SaveLocalWildcardVar2Decl(node);
            ctx.SaveFunc2CurDecl(node);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void BaseMangler::MangleExportId(Package& pkg)
{
    std::string pkgName = ManglerContext::ReduceUnitTestPackageName(pkg.fullPackageName);
    auto manglerCtx = std::make_unique<ManglerContext>();
    manglerCtxTable[pkgName] = manglerCtx.get();
    CollectLocalDecls(*manglerCtxTable.at(pkgName), pkg);

    exportIdMode = true;
    Walker(&pkg, [this](auto node) {
        if (auto decl = DynamicCast<Decl*>(node);
            decl && decl->GetTy().IsCorrect() && decl->TestAttr(Attribute::GLOBAL)) {
            // Only global decl and member decls that may be referenced from other package need exportId!
            // NOTE: For cjo's compatibility of different version, the exportId must be decl's signature.
            //       ExtendDecl itself does not need exportId, but it's member needs.
            decl->exportId = MangleExportId(*decl);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

std::string BaseMangler::MangleExportId(Decl& decl) const
{
    if (decl.TestAttr(Attribute::NO_MANGLE)) {
        return decl.identifier;
    }
    return match(decl)(
        [&, this](FuncDecl& fd) {
            fd.exportId = Mangle(fd);
            if (fd.TestAttr(Attribute::DEFAULT) && fd.outerDecl && fd.outerDecl->astKind == ASTKind::INTERFACE_DECL) {
                // Interface default implementation may be copied to other package's type decl and referenced.
                MangleExportIdForGenericParamDecl(fd);
            }
            return fd.exportId;
        },
        [this](PropDecl& propDecl) {
            // PropDecl itself does not need exportId, only getters/setters need.
            auto mangleExport = [this](auto& it) { it->exportId = Mangle(*it); };
            std::for_each(propDecl.getters.begin(), propDecl.getters.end(), mangleExport);
            std::for_each(propDecl.setters.begin(), propDecl.setters.end(), mangleExport);
            // For modal-overloaded properties (multiple same-name PropDecl with different modal
            // on the prop type), include the modal so the PropDecl-level exportId stays unique.
            // <prop-decl-export-id> ::= <path><prop-name>[<mode-set>] (type-mode, leader Q).
            auto propTy = propDecl.type ? propDecl.type->GetTy() : propDecl.GetTy();
            return Mangle(propDecl) + MangleUtils::MangleTypeMode(propTy.Mode());
        },
        [](const PrimaryCtorDecl& /* primaryCtorDecl */) { return std::string(); },
        [&decl, this]() {
            decl.exportId = Mangle(decl);
            for (auto& it : decl.GetMemberDeclPtrs()) {
                it->exportId = MangleExportId(*it);
            }
            if (decl.astKind == ASTKind::INTERFACE_DECL && decl.TestAttr(Attribute::GENERIC)) {
                // Interface's generic type may be referenced in other package for upperbound call.
                // NOTE: only original generic declaration needs mangle.
                MangleExportIdForGenericParamDecl(decl);
            }
            return decl.exportId;
        });
}

void BaseMangler::MangleExportIdForGenericParamDecl(const Decl& decl) const
{
    if (auto generic = decl.GetGeneric()) {
        for (auto& gpd : generic->typeParameters) {
            CJC_NULLPTR_CHECK(gpd->outerDecl);
            std::vector<std::string> genericsTypeStack;
            gpd->exportId = gpd->outerDecl->exportId + MANGLE_WILDCARD_PREFIX + gpd->identifier +
                MangleType(gpd->GetTy(), genericsTypeStack, false, false);
        }
    }
}

Ptr<const Decl> BaseMangler::GetOuterDecl(const Decl& decl) const
{
    auto outerDecl = decl.outerDecl;
    if (!Is<FuncDecl>(&decl)) {
        return outerDecl;
    }
    return GetParentDecl(StaticCast<const FuncDecl&>(decl));
}

std::string BaseMangler::MangleEnumType(
    ModalTy ty, std::vector<std::string>& genericsTypeStack, bool declare, bool isCollectGTy) const
{
    CJC_ASSERT(ty.Kind() == TypeKind::TYPE_ENUM);
    return MangleUserDefinedType(ty, genericsTypeStack, declare, isCollectGTy);
}

std::string BaseMangler::MangleRawArrayType(
    ModalTy ty, std::vector<std::string>& genericsTypeStack, bool declare, bool isCollectGTy) const
{
    CJC_ASSERT(ty.Kind() == TypeKind::TYPE_ARRAY);
    auto arrayTy = StaticCast<const ArrayTy&>(*ty.Ty());
    return MangleUtils::WithModal(
        MANGLE_TYPE_ARRAY_PREFIX + MangleUtils::DecimalToManglingNumber(std::to_string(arrayTy.dims)) +
            MangleType(arrayTy.typeArgs[0], genericsTypeStack, declare, isCollectGTy),
        ty.Mode());
}

std::string BaseMangler::MangleVArrayType(
    ModalTy ty, std::vector<std::string>& genericsTypeStack, bool declare, bool isCollectGTy) const
{
    CJC_ASSERT(ty.Kind() == TypeKind::TYPE_VARRAY);
    auto varrayTy = StaticCast<const VArrayTy&>(*ty.Ty());
    // V<N>_<type>
    std::string mangled = MANGLE_VARRAY_PREFIX + MangleUtils::DecimalToManglingNumber(std::to_string(varrayTy.size));
    for (const auto it : varrayTy.typeArgs) {
        mangled += MangleType(it, genericsTypeStack, declare, isCollectGTy);
    }
    return MangleUtils::WithModal(mangled, ty.Mode());
}

std::string BaseMangler::MangleTupleType(
    ModalTy ty, std::vector<std::string>& genericsTypeStack, bool declare, bool isCollectGTy) const
{
    CJC_ASSERT(ty.Kind() == TypeKind::TYPE_TUPLE);
    auto tupleTy = StaticCast<const TupleTy&>(*ty.Ty());
    std::string mangled = MANGLE_TUPLE_PREFIX +
        MangleUtils::DecimalToManglingNumber(std::to_string(tupleTy.typeArgs.size()));
    for (const auto it : tupleTy.typeArgs) {
        mangled += MangleType(it, genericsTypeStack, declare, isCollectGTy);
    }
    mangled += MANGLE_SUFFIX;
    return MangleUtils::WithModal(mangled, ty.Mode());
}

std::string BaseMangler::MangleFuncType(
    ModalTy ty, std::vector<std::string>& genericsTypeStack, bool declare, bool isCollectGTy) const
{
    CJC_ASSERT(ty.Kind() == TypeKind::TYPE_FUNC);
    auto funcTy = StaticCast<const FuncTy&>(*ty.Ty());
    std::string mangled = funcTy.IsCFunc() ? MANGLE_CFUNC_PREFIX : MANGLE_GENERAL_FUNC_PREFIX;
    std::string retTy = MangleType(funcTy.retTy, genericsTypeStack, declare, isCollectGTy);
    if (retTy.empty()) {
        retTy = MANGLE_VOID_TY_SUFFIX;
    }
    std::string params = "";
    for (auto it : funcTy.paramTys) {
        params += MangleType(it, genericsTypeStack, declare, isCollectGTy);
    }
    mangled += retTy + params + MANGLE_SUFFIX;
    return MangleUtils::WithModal(mangled, ty.Mode());
}

std::string BaseMangler::MangleGenericType(ModalTy ty) const
{
    CJC_ASSERT(ty.Kind() == TypeKind::TYPE_GENERICS);
    auto genericsTy = StaticCast<const GenericsTy&>(*ty.Ty());
    std::string mangled = MANGLE_GENERIC_TYPE_PREFIX + MangleUtils::MangleName(genericsTy.name);
    return MangleUtils::WithModal(mangled, ty.Mode());
}

std::string BaseMangler::MangleGenericType(
    ModalTy ty, std::vector<std::string>& genericsTypeStack, bool declare) const
{
    CJC_ASSERT(ty.Kind() == TypeKind::TYPE_GENERICS);
    auto genericsTy = StaticCast<const GenericsTy&>(*ty.Ty());
    size_t index = 0;
    if (declare) {
        index = genericsTypeStack.size();
        genericsTypeStack.emplace_back(genericsTy.name);
    } else {
        auto result = std::find_if(genericsTypeStack.rbegin(), genericsTypeStack.rend(),
            [&genericsTy](const std::string& name) { return name == genericsTy.name; });
        index = static_cast<size_t>(std::distance(result, genericsTypeStack.rend())) - MANGLE_CHAR_LEN;
        CJC_ASSERT(result != genericsTypeStack.rend() && "Using undeclared generic type!");
    }
    auto number = MangleUtils::DecimalToManglingNumber(std::to_string(index));
    return MangleUtils::WithModal(MANGLE_GENERIC_TYPE_PREFIX + number, ty.Mode());
}

std::string BaseMangler::MangleFuncParams(const AST::FuncDecl& funcDecl, std::vector<std::string>& genericsTypeStack,
    bool declare, bool isCollectGTy) const
{
    if (!funcDecl.funcBody || funcDecl.funcBody->paramLists.empty()) {
        return MANGLE_VOID_TY_SUFFIX;
    }
    std::string mangled = "";
    if (funcDecl.funcBody->paramLists[0]->params.empty()) {
        return mangled + MANGLE_VOID_TY_SUFFIX;
    }
    for (auto& param : funcDecl.funcBody->paramLists[0]->params) {
        CJC_NULLPTR_CHECK(param->GetTy());
        if (Ty::IsInitialTy(param->DataTy())) {
            continue;
        }
        mangled += MangleType(param->GetTy(), genericsTypeStack, declare, isCollectGTy);
    }
    return mangled;
}

std::string BaseMangler::MangleCPointerType(
    ModalTy ty, std::vector<std::string>& genericsTypeStack, bool declare, bool isCollectGTy) const
{
    CJC_ASSERT(ty.Kind() == TypeKind::TYPE_POINTER);
    auto pointerTy = StaticCast<const PointerTy&>(*ty.Ty());
    CJC_ASSERT(!pointerTy.typeArgs.empty() && pointerTy.typeArgs[0]);
    if (!pointerTy.typeArgs.empty() && pointerTy.typeArgs[0]) {
        return MangleUtils::WithModal(
            MANGLE_POINTER_PREFIX + MangleType(pointerTy.typeArgs[0], genericsTypeStack, declare, isCollectGTy),
            ty.Mode());
    }
    return "";
}

std::string BaseMangler::MangleCStringType(ModalTy ty) const
{
    return MangleUtils::WithModal("k", ty.Mode());
}

std::string BaseMangler::ManglePackageNameForGeneric(const AST::Decl& decl) const
{
    std::string genericPkg;
    // Scenario: Imported generic functions can be overloaded in two imported different packages.
    // such as public pkg1.foo<Int64>() and public pkg2.foo<Int64>() with different implementations
    // Iterate itself to get the package name where defines the generic.
    auto genericDecl = decl.genericDecl;
    while (genericDecl != nullptr) {
        genericPkg = MangleFullPackageName(*genericDecl);
        genericDecl = genericDecl->genericDecl;
    }
    if (!genericPkg.empty()) {
        return genericPkg;
    }
    // Scenario: Two generic functions can be overloaded in two imported different types.
    // such as public pkg1.A<T> and public pkg2.A<T> both have function foo()
    // Iterate outerDecl to get a package name where defines the generic.
    auto outer = &decl;
    while (outer != nullptr) {
        if (outer->genericDecl != nullptr) {
            return MangleFullPackageName(*outer->genericDecl);
        }
        outer = GetOuterDecl(*outer);
    }
    return genericPkg;
}

bool BaseMangler::IsLocalVariable(const AST::Decl& decl) const
{
    auto varDecl = dynamic_cast<const AST::VarDecl*>(&decl);
    if (varDecl == nullptr) {
        return false;
    }
    // Check if varDecl is a static member var of class/interface.
    auto isStaticMemberVar = [varDecl]() {
        return varDecl->outerDecl && (varDecl->outerDecl->IsClassLikeDecl() ||
            varDecl->outerDecl->IsStructOrClassDecl()) && varDecl->TestAttr(AST::Attribute::STATIC);
    };
    // Static member variable of Class/interface is not treated as local variable.
    if (!varDecl->TestAttr(AST::Attribute::GLOBAL) && !isStaticMemberVar()) {
        return true;
    }
    return false;
}

bool BaseMangler::IsLocalFunc(const AST::FuncDecl& funcDecl) const
{
    return !funcDecl.TestAttr(Attribute::GLOBAL) && funcDecl.outerDecl && !funcDecl.outerDecl->IsNominalDecl() &&
        !funcDecl.outerDecl->IsBuiltIn();
}

std::string BaseMangler::HashToBase62(const std::string& input)
{
    uint32_t hashValue = Fnv1aHash(input);
    return ToBase62(hashValue);
}

bool BaseMangler::IsHashable(const std::string& str)
{
    auto fileName = FileNameWithoutExtension(str);
    // Restrict the maximum length of fileName is 12.
    if (fileName.size() > 12) {
        return true;
    }
    for (char c : fileName) {
        // [a-zA-Z0-9._-]
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == '.') || (c == '_') ||
                (c == '-'))) {
            return true;
        }
    }
    return false;
}

std::string BaseMangler::FileNameWithoutExtension(const std::string& fileName)
{
    std::string fileNameWithoutExtension = fileName;
    // The length of ".cj" is 3. If it ends with ".cj", strip the extension part.
    size_t length = 3;
    if (fileName.size() > length && fileName.rfind(".cj") == fileName.size() - length) {
        fileNameWithoutExtension = fileName.substr(0, fileName.size() - length);
    }
    return fileNameWithoutExtension;
}

void ManglerContext::SaveGlobalWildcardVar(const Ptr<const AST::File> file, const Ptr<AST::VarWithPatternDecl> vpd)
{
    if (vpd->irrefutablePattern->astKind == AST::ASTKind::WILDCARD_PATTERN ||
        CheckAllElementsWildcard(vpd->irrefutablePattern.get())) {
        file2GlobalWildcardVar[file].emplace_back(vpd);
    }
}

bool ManglerContext::CheckAllElementsWildcard(const Ptr<AST::Pattern>& root)
{
    if (root->astKind == AST::ASTKind::WILDCARD_PATTERN) {
        return true;
    }
    bool areAllElementsWildcard = true;
    if (auto tp = DynamicCast<TuplePattern>(root)) {
        for (auto& pattern : tp->patterns) {
            if (!pattern) {
                continue;
            }
            areAllElementsWildcard = areAllElementsWildcard && CheckAllElementsWildcard(pattern.get());
        }
    } else if (auto ep = DynamicCast<EnumPattern>(root)) {
        if (ep->patterns.empty()) {
            areAllElementsWildcard = false;
        }
        for (auto& pattern : ep->patterns) {
            if (!pattern) {
                continue;
            }
            areAllElementsWildcard = areAllElementsWildcard && CheckAllElementsWildcard(pattern.get());
        }
    } else {
        areAllElementsWildcard = false;
    }
    return areAllElementsWildcard;
}

namespace {
    Ptr<Node> GetDeclKey(const Ptr<Node> node)
    {
        if (auto lambda = DynamicCast<LambdaExpr>(node)) {
            return lambda->funcBody.get();
        } else if (auto function = DynamicCast<FuncDecl>(node)) {
            return function->funcBody.get();
        } else if (auto pcd = DynamicCast<PrimaryCtorDecl>(node)) {
            return pcd->funcBody.get();
        } else if (auto vda = DynamicCast<VarDeclAbstract>(node)) {
            return vda;
        }
        return nullptr;
    }
}

void ManglerContext::SaveFunc2CurDecl(const Ptr<Node> node)
{
    Ptr<Node> key = nullptr;

    // for local generic instantiated function which is lifted to global, this local decl should be saved under its
    // outerDecl's key.
    if (auto function = DynamicCast<FuncDecl>(node)) {
        if (function->TestAttr(AST::Attribute::GENERIC_INSTANTIATED) && !function->TestAttr(AST::Attribute::GLOBAL)) {
            if (function->outerDecl && !function->outerDecl->IsNominalDecl()) {
                key = GetDeclKey(function->outerDecl);
            }
            if (key != nullptr) {
                auto& mapOfName2Func = node2LocalFunc[key][function->identifier.Val()];
                mapOfName2Func.emplace_back(function);
            }
        }
    }

    key = GetDeclKey(node);
    if (key == nullptr) {
        return;
    }

    Walker(key, [this, &key](const Ptr<Node>& node) {
        if (auto fd = DynamicCast<FuncDecl>(node); fd && node != key) {
            auto& mapOfName2Func = node2LocalFunc[key][fd->identifier.Val()];
            mapOfName2Func.emplace_back(fd);
        }  else if (Is<FuncBody>(node) && node != key) {
            return VisitAction::WALK_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void ManglerContext::SaveLocalWildcardVar2Decl(const Ptr<Node> node)
{
    Ptr<Node> key = nullptr;
    if (auto lambda = DynamicCast<LambdaExpr>(node)) {
        key = lambda->funcBody.get();
    } else if (auto function = DynamicCast<FuncDecl>(node)) {
        key = function->funcBody.get();
    } else if (auto pcd = DynamicCast<PrimaryCtorDecl>(node)) {
        key = pcd->funcBody.get();
    } else if (auto vda = DynamicCast<VarDeclAbstract>(node); vda && vda->TestAttr(Attribute::GLOBAL)) {
        key = vda;
    }
    if (!key) {
        return;
    }

    Walker(key, [this, &key](const Ptr<Node>& node) {
        if (auto vda = DynamicCast<VarWithPatternDecl>(node); vda && node != key) {
            node2LocalWildcardVar[key].emplace_back(vda);
        } else if (Is<FuncBody>(node) && node != key) {
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void ManglerContext::SaveVar2CurDecl(const Ptr<Node> node)
{
    Ptr<Node> key = nullptr;
    if (auto lambda = DynamicCast<LambdaExpr>(node)) {
        key = lambda->funcBody.get();
    } else if (auto function = DynamicCast<FuncDecl>(node)) {
        key = function->funcBody.get();
    } else if (auto pcd = DynamicCast<PrimaryCtorDecl>(node)) {
        key = pcd->funcBody.get();
    } else if (auto vda = DynamicCast<VarDeclAbstract>(node); vda && vda->TestAttr(Attribute::GLOBAL)) {
        key = vda;
    }
    if (!key) {
        return;
    }
    Walker(key, [this, &key](const Ptr<Node>& node) {
        if (auto vda = DynamicCast<VarDeclAbstract>(node); vda && node != key) {
            auto& mapOfName2Var = node2LocalVar[key][vda->identifier.Val()];
            mapOfName2Var.emplace_back(vda);
        } else if (Is<FuncBody>(node) && node != key) {
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void ManglerContext::SaveLambda2CurDecl(const Ptr<Node> node)
{
    std::vector<Ptr<Node>> keys;
    if (auto lambda = DynamicCast<LambdaExpr>(node)) {
        keys.emplace_back(lambda->funcBody.get());
    } else if (auto function = DynamicCast<FuncDecl>(node)) {
        keys.emplace_back(function->funcBody.get());
        keys.emplace_back(function->annotationsArray.get());
    } else if (auto pcd = DynamicCast<PrimaryCtorDecl>(node)) {
        keys.emplace_back(pcd->funcBody.get());
        keys.emplace_back(pcd->annotationsArray.get());
    } else if (auto vda = DynamicCast<VarDeclAbstract>(node); vda &&
               (vda->TestAttr(Attribute::GLOBAL) ||
                   (vda->outerDecl &&
                       (vda->outerDecl->astKind == ASTKind::CLASS_DECL ||
                           vda->outerDecl->astKind == ASTKind::INTERFACE_DECL ||
                           vda->outerDecl->astKind == ASTKind::STRUCT_DECL ||
                           vda->outerDecl->astKind == ASTKind::ENUM_DECL)))) {
        keys.emplace_back(vda);
        keys.emplace_back(vda->annotationsArray.get());
    }
    for (Ptr<Node> key : keys) {
        if (!key) {
            continue;
        }
        Walker(key, [this, &key](const Ptr<Node>& node) {
            if (auto le = DynamicCast<LambdaExpr>(node); le && node != key) {
                node2Lambda[key].emplace_back(le);
            } else if (Is<FuncBody>(node) && node != key) {
                return VisitAction::SKIP_CHILDREN;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
}

void ManglerContext::SaveExtend2CurFile(const Ptr<const AST::File> file, const Ptr<AST::ExtendDecl> node)
{
    file2ExtendDecl[file][node->extendedType->GetTy().String()].emplace_back(node);
}

std::optional<size_t> ManglerContext::GetIndexOfGlobalWildcardVar(
    const Ptr<const AST::File> file, const Ptr<const VarWithPatternDecl> target) const
{
    if (!file || file->fileHash == 0) {
        return {};
    }
    auto found = file2GlobalWildcardVar.find(file);
    if (found != file2GlobalWildcardVar.end()) {
        for (size_t i = 0; i < found->second.size(); ++i) {
            if (found->second[i] == target) {
                return i;
            }
        }
    }
    return {};
}

std::optional<size_t> ManglerContext::GetIndexOfExtend(
    const Ptr<const AST::File> file, const Ptr<const AST::ExtendDecl> target) const
{
    if (!file) {
        return {};
    }
    auto fileMap = file2ExtendDecl.find(file);
    CJC_ASSERT(fileMap != file2ExtendDecl.end());
    auto elementsVector = fileMap->second.find(target->extendedType->GetTy().String());
    CJC_ASSERT(elementsVector != fileMap->second.end());
    auto found = std::find(elementsVector->second.begin(), elementsVector->second.end(), target);
    if (found != elementsVector->second.end()) {
        return found - elementsVector->second.begin();
    }
    return {};
}

std::optional<size_t> ManglerContext::GetIndexOfLocalWildcardVar(
    const Ptr<const AST::Node> node, const Ptr<const AST::VarWithPatternDecl> target) const
{
    auto foundFunc = node2LocalWildcardVar.end();
    if (auto lambda = DynamicCast<LambdaExpr>(node)) {
        foundFunc = node2LocalWildcardVar.find(lambda->funcBody.get());
    } else if (auto function = DynamicCast<FuncDecl>(node)) {
        foundFunc = node2LocalWildcardVar.find(function->funcBody.get());
    } else if (auto pcd = DynamicCast<PrimaryCtorDecl>(node)) {
        foundFunc = node2LocalWildcardVar.find(pcd->funcBody.get());
    } else if (auto vda = DynamicCast<VarDeclAbstract>(node); vda && vda->TestAttr(Attribute::GLOBAL)) {
        foundFunc = node2LocalWildcardVar.find(vda);
    }

    if (foundFunc != node2LocalWildcardVar.end()) {
        auto targetIt = std::find(foundFunc->second.begin(), foundFunc->second.end(), target);
        if (targetIt != foundFunc->second.end()) {
            return targetIt - foundFunc->second.begin();
        }
        return {};
    }
    return {};
}

std::optional<size_t> ManglerContext::GetIndexOfVar(
    const Ptr<const Node> node, const Ptr<const VarDeclAbstract> target) const
{
    auto foundFunc = node2LocalVar.end();
    if (auto lambda = DynamicCast<LambdaExpr>(node)) {
        foundFunc = node2LocalVar.find(lambda->funcBody.get());
    } else if (auto function = DynamicCast<FuncDecl>(node)) {
        foundFunc = node2LocalVar.find(function->funcBody.get());
    } else if (auto pcd = DynamicCast<PrimaryCtorDecl>(node)) {
        foundFunc = node2LocalVar.find(pcd->funcBody.get());
    } else if (auto vda = DynamicCast<VarDeclAbstract>(node); vda && vda->TestAttr(Attribute::GLOBAL)) {
        foundFunc = node2LocalVar.find(vda);
    }

    if (foundFunc != node2LocalVar.end()) {
        auto foundId = foundFunc->second.find(target->identifier);
        if (foundId == foundFunc->second.end()) {
            return {};
        }
        auto targetIt = std::find(foundId->second.begin(), foundId->second.end(), target);
        if (targetIt != foundId->second.end()) {
            return targetIt - foundId->second.begin();
        }
        return {};
    }
    return {};
}

std::optional<size_t> ManglerContext::GetIndexOfFunc(
    const Ptr<const Node> node, const Ptr<const FuncDecl> target) const
{
    auto foundFunc = node2LocalFunc.end();
    if (auto lambda = DynamicCast<LambdaExpr>(node)) {
        foundFunc = node2LocalFunc.find(lambda->funcBody.get());
    } else if (auto function = DynamicCast<FuncDecl>(node)) {
        foundFunc = node2LocalFunc.find(function->funcBody.get());
    } else if (auto pcd = DynamicCast<PrimaryCtorDecl>(node)) {
        foundFunc = node2LocalFunc.find(pcd->funcBody.get());
    } else if (auto vda = DynamicCast<VarDeclAbstract>(node)) {
        foundFunc = node2LocalFunc.find(vda);
    }

    if (foundFunc != node2LocalFunc.end()) {
        auto foundId = foundFunc->second.find(target->identifier);
        if (foundId == foundFunc->second.end()) {
            return {};
        }
        auto targetIt = std::find(foundId->second.begin(), foundId->second.end(), target);
        if (targetIt != foundId->second.end()) {
            return targetIt - foundId->second.begin();
        }
        return {};
    }
    return {};
}

std::optional<size_t> ManglerContext::GetIndexOfLambda(
    const Ptr<const Node> node, const Ptr<const LambdaExpr> target) const
{
    auto foundFunc = node2Lambda.end();
    auto foundArrayLit = node2Lambda.end();
    if (auto lambda = DynamicCast<LambdaExpr>(node)) {
        foundFunc = node2Lambda.find(lambda->funcBody.get());
    } else if (auto function = DynamicCast<FuncDecl>(node)) {
        foundFunc = node2Lambda.find(function->funcBody.get());
        foundArrayLit = node2Lambda.find(function->annotationsArray.get());
    } else if (auto pcd = DynamicCast<PrimaryCtorDecl>(node)) {
        foundFunc = node2Lambda.find(pcd->funcBody.get());
        foundArrayLit = node2Lambda.find(pcd->annotationsArray.get());
    } else if (auto vda = DynamicCast<VarDeclAbstract>(node); vda &&
               (vda->TestAttr(Attribute::GLOBAL) ||
                   (vda->outerDecl &&
                       (vda->outerDecl->astKind == ASTKind::CLASS_DECL ||
                           vda->outerDecl->astKind == ASTKind::INTERFACE_DECL ||
                           vda->outerDecl->astKind == ASTKind::STRUCT_DECL ||
                           vda->outerDecl->astKind == ASTKind::ENUM_DECL)))) {
        foundFunc = node2Lambda.find(vda);
        foundArrayLit = node2Lambda.find(vda->annotationsArray.get());
    }
    auto getIndex = [this, &target](std::map<Ptr<const AST::Node>,
        std::vector<Ptr<AST::LambdaExpr>>>::const_iterator it) -> std::optional<size_t> {
        if (it != node2Lambda.end()) {
            auto targetIt = std::find(it->second.begin(), it->second.end(), target);
            if (targetIt != it->second.end()) {
                return targetIt - it->second.begin();
            }
        }
        return {};
    };
    std::optional<size_t> funcIdx = getIndex(foundFunc);
    if (funcIdx.has_value()) {
        return funcIdx;
    }
    std::optional<size_t> arrayIdx = getIndex(foundArrayLit);
    CJC_ASSERT(arrayIdx.has_value() && "index of lambda has no value.");
    size_t offset = foundFunc != node2Lambda.end() ? foundFunc->second.size() : 0;
    return arrayIdx.value() + offset;
}
