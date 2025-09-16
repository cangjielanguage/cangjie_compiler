// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.
#include "cangjie/Mangle/ASTMangler.h"

#include <type_traits>

#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Parse/ASTHasher.h"
#include "cangjie/AST/ASTCasting.h"
#include "cangjie/Utils/Utils.h"
#include "cangjie/Mangle/MangleUtils.h"

using namespace Cangjie;
using namespace AST;
using namespace MangleUtils;
using namespace Cangjie::Utils;

static std::string MangleType(const AST::Type& typeAnnotation);

static std::string MangleRefTypeAnnotation(const AST::Type& type)
{
    CJC_ASSERT(type.astKind == ASTKind::REF_TYPE);
    auto& refTyAnno = StaticCast<const RefType&>(type);
    std::string mangledName = std::to_string(refTyAnno.ref.identifier.Length()) + refTyAnno.ref.identifier;
    if (!refTyAnno.typeArguments.empty()) {
        mangledName += MANGLE_LT_PREFIX;
        for (auto& arg : refTyAnno.typeArguments) {
            mangledName += MangleType(*arg);
        }
        mangledName += MANGLE_GT_PREFIX;
    }
    return mangledName;
}

static std::string MangleParenTypeAnnotation(const AST::Type& type)
{
    CJC_ASSERT(type.astKind == ASTKind::PAREN_TYPE);
    auto& parenTyAnno = StaticCast<const ParenType&>(type);
    std::string mangledName = MANGLE_PAREN_PREFIX + MANGLE_WILDCARD_PREFIX;
    mangledName += MangleType(*parenTyAnno.type);
    return mangledName;
}

static std::string MangleOptionTypeAnnotation(const AST::Type& type)
{
    CJC_ASSERT(type.astKind == ASTKind::OPTION_TYPE);
    auto& optionTyAnno = StaticCast<const OptionType&>(type);
    std::string mangledName = MANGLE_OPTION_PREFIX + std::to_string(optionTyAnno.questNum) + MANGLE_WILDCARD_PREFIX;
    mangledName += MangleType(*optionTyAnno.componentType);
    return mangledName;
}

static std::string MangleFuncTypeAnnotation(const AST::Type& type)
{
    CJC_ASSERT(type.astKind == ASTKind::FUNC_TYPE);
    auto& funcTyAnno = StaticCast<const FuncType&>(type);
    std::string mangledName = funcTyAnno.isC ? MANGLE_AST_CFUNC_PREFIX : MANGLE_FUNC_PREFIX;
    mangledName += MangleType(*funcTyAnno.retType);
    for (auto& it : funcTyAnno.paramTypes) {
        mangledName += MangleType(*it);
    }
    return mangledName;
}

static std::string MangleConstantTypeAnnotation(const AST::Type& type)
{
    CJC_ASSERT(type.astKind == ASTKind::CONSTANT_TYPE);
    auto& constType = StaticCast<const ConstantType&>(type);
    std::string mangledName = MANGLE_DOLLAR_PREFIX;
    auto ce = constType.constantExpr.get();
    CJC_ASSERT(ce->astKind == ASTKind::LIT_CONST_EXPR);
    mangledName += RawStaticCast<LitConstExpr*>(ce)->stringValue;
    return mangledName;
}

static std::string MangleVArrayTypeAnnotation(const AST::Type& type)
{
    CJC_ASSERT(type.astKind == ASTKind::VARRAY_TYPE);
    auto& varrType = StaticCast<const VArrayType&>(type);
    std::string mangledName = MANGLE_VARRAY_PREFIX + MANGLE_WILDCARD_PREFIX;
    mangledName += MangleType(*varrType.typeArgument);
    mangledName += MangleType(*varrType.constantType);
    return mangledName;
}

static std::string MangleTupleTypeAnnotation(const AST::Type& type)
{
    CJC_ASSERT(type.astKind == ASTKind::TUPLE_TYPE);
    auto& tupleTyAnno = StaticCast<const TupleType&>(type);
    std::string mangledName = MANGLE_TUPLE_PREFIX + std::to_string(tupleTyAnno.fieldTypes.size()) +
        MANGLE_WILDCARD_PREFIX;
    for (auto& it : tupleTyAnno.fieldTypes) {
        mangledName += MangleType(*it);
    }
    return mangledName;
}

static std::string MangleQualifiedTypeAnnotation(const AST::Type& type)
{
    CJC_ASSERT(type.astKind == ASTKind::QUALIFIED_TYPE);
    auto& qualifiedTyAnno = StaticCast<const QualifiedType&>(type);
    std::string mangledName = MANGLE_QUALIFIED_PREFIX;
    mangledName += MangleType(*qualifiedTyAnno.baseType);
    mangledName += MANGLE_DOT_PREFIX + std::to_string(qualifiedTyAnno.field.Length()) + qualifiedTyAnno.field;
    if (!qualifiedTyAnno.typeArguments.empty()) {
        mangledName += MANGLE_LT_PREFIX;
        for (auto& arg : qualifiedTyAnno.typeArguments) {
            mangledName += MangleType(*arg);
        }
        mangledName += MANGLE_GT_PREFIX;
    }
    return mangledName;
}

static std::optional<std::string> MangleBuiltinTypeImpl(const std::string& type)
{
    static const std::unordered_map<std::string_view, std::string> PTYPE_NAMEMAP{
        {"Nothing", "n"},
        {"Unit", "u"},
        {"Rune", "c"},
        {"Bool", "b"},
        {"Float16", "Dh"},
        {"Float32", "f"},
        {"Float64", "d"},
        {"Int8", "a"},
        {"Int16", "s"},
        {"Int32", "i"},
        {"Int64", "l"},
        {"IntNative", "q"},
        {"UInt8", "h"},
        {"UInt16", "t"},
        {"UInt32", "j"},
        {"UInt64", "m"},
        {"UIntNative", "r"},
        {CSTRING_NAME, "CString"},
        {CFUNC_NAME, "CFunc"},
        {CPOINTER_NAME, "CPointer"},
    };
    auto it = PTYPE_NAMEMAP.find(type);
    if (it != PTYPE_NAMEMAP.cend()) {
        return it->second;
    }
    return {};
}

std::string ASTMangler::ManglePrimitiveType(const AST::Type& type)
{
    CJC_ASSERT(type.astKind == ASTKind::PRIMITIVE_TYPE);
    auto& primitiveTy = StaticCast<const PrimitiveType&>(type);
    if (auto t = MangleBuiltinTypeImpl(primitiveTy.str)) {
        return *t;
    }
    // Initial type is valid only in the return type of function $test.entry when enabling --test
    CJC_ASSERT(primitiveTy.ty && primitiveTy.ty->kind == TypeKind::TYPE_INITIAL);
    return MANGLE_INITIAL_PREFIX;
}

std::string ASTMangler::MangleBuiltinType(const std::string& type)
{
    auto t = MangleBuiltinTypeImpl(type);
    if (t) {
        return *t;
    }
    return type;
}

std::optional<std::string> ASTMangler::TruncateExtendMangledName(const std::string& name)
{
    // Member of extend, ignored
    if (name.find(std::to_string(MANGLE_EXIG_PREFIX.length()) + MANGLE_EXIG_PREFIX) != std::string::npos) {
        return {};
    }
    auto it = name.find(MANGLE_LT_COLON_PREFIX);
    if (it == std::string::npos) {
        return {};
    }
    return name.substr(0, it);
}

static std::string MangleType(const AST::Type& typeAnnotation)
{
    std::function<std::string(const AST::Type &)> mangleThisType =
        [](const AST::Type &) { return MANGLE_THIS_PREFIX; };
    static const std::unordered_map<AST::ASTKind, std::function<std::string (const AST::Type&)>>
        HANDLE_TYPE_MAP = {
            {ASTKind::PRIMITIVE_TYPE, ASTMangler::ManglePrimitiveType},
            {ASTKind::REF_TYPE, MangleRefTypeAnnotation},
            {ASTKind::PAREN_TYPE, MangleParenTypeAnnotation},
            {ASTKind::OPTION_TYPE, MangleOptionTypeAnnotation},
            {ASTKind::CONSTANT_TYPE, MangleConstantTypeAnnotation},
            {ASTKind::VARRAY_TYPE, MangleVArrayTypeAnnotation},
            {ASTKind::FUNC_TYPE, MangleFuncTypeAnnotation},
            {ASTKind::TUPLE_TYPE, MangleTupleTypeAnnotation},
            {ASTKind::QUALIFIED_TYPE, MangleQualifiedTypeAnnotation},
            {ASTKind::THIS_TYPE, mangleThisType}
        };

    if (auto found = HANDLE_TYPE_MAP.find(typeAnnotation.astKind); found != HANDLE_TYPE_MAP.end()) {
        return found->second(typeAnnotation);
    }

    CJC_ASSERT(false && "Unexpected type annotation.");
    return "";
}

static std::string MangleAccessibility(const AST::Decl& decl)
{
    std::string res{};
    for (auto& mod : decl.modifiers) {
        if (mod.modifier == TokenKind::PUBLIC) {
            res += MANGLE_PUBLIC_PREFIX;
            continue;
        }
        if (mod.modifier == TokenKind::PROTECTED) {
            res += MANGLE_PROTECTED_PREFIX;
            continue;
        }
        if (mod.modifier == TokenKind::PRIVATE) {
            res += MANGLE_PRIVATE_PREFIX;
            continue;
        }
    }
    return res;
}

static std::string MangleDeclAstKind(ASTKind k)
{
    // ASTKind beyond PROP_DECL is not mangled
    constexpr uint8_t mangledDeclKindMax{static_cast<uint8_t>(ASTKind::PROP_DECL) + 1};
    // Character 'A' for N/A, suggesting this decl is not to be mangled
    static const char kind2StrTable[mangledDeclKindMax] {
        'A', 'A', 'A', 'A', 'F', 'M', 'A', 'C', 'I', 'X', 'U', 'S', 'T', 'Y', 'A', 'V', 'P'
    };
    uint8_t k1{static_cast<uint8_t>(k)};
    if (k1 < mangledDeclKindMax && kind2StrTable[k1] != 'A') {
        return {kind2StrTable[k1]};
    }
    return "";
}

struct Cangjie::ASTManglerImpl {
    explicit ASTManglerImpl(const std::string& package) : fullPackageName{package}
    {
    }

    // Declare as rvalue method to avoid careless reentrant of this function
    std::string Mangle(const Decl& decl)
    {
        if (auto vp = DynamicCast<VarWithPatternDecl*>(&decl)) {
            MangleVarWithPatternDecl(*vp);
            return std::move(mangled);
        }
        // Don't use base name mangling for extend
        if (auto extend = DynamicCast<ExtendDecl*>(&decl)) {
            MangleExtendDecl(*extend);
            return std::move(mangled);
        }
        MangleDeclCommon(decl);
        return std::move(mangled);
    }

private:
    std::string mangled;
    std::string fullPackageName;
    std::unordered_map<std::string, int> wildcardMap{};

    std::string MangleFullPackageName([[maybe_unused]] const AST::Decl& decl)
    {
        auto ret = MangleUtils::MangleName(fullPackageName);
        return decl.TestAttr(Attribute::GLOBAL, Attribute::PRIVATE) ? ret + MangleUtils::MangleFilePrivate(decl) : ret;
    }

    void MangleFuncParameters(const std::vector<OwnedPtr<FuncParam>>& params)
    {
        for (const auto& param : params) {
            if (param->type) {
                mangled += MangleType(*param->type);
            }
            if (param->isMemberParam || param->isNamedParam) {
                mangled += '!';
            }
            if (param->isMemberParam) {
                if (param->hasLetOrVar) {
                    mangled += MangleAccessibility(*param);
                    mangled += (param->isVar ? MANGLE_VARIABLE_PREFIX : MANGLE_LET_PREFIX);
                }
            }
            if (param->isMemberParam || param->isNamedParam) {
                mangled += MangleUtils::MangleName(param->identifier);
                if (param->assignment) {
                    mangled += '=';
                }
            }
        }
    }

    void MangleDeclCommon(const Decl& decl)
    {
        mangled += MangleFullPackageName(decl);
        MangleExtendLiteralIfInExtend(decl);
        MangleNestedDecl(decl);
        // Add additional angle brackets to avoid duplication with raw identifier names.
        bool useInternalIdent = decl.TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::MAIN_ENTRY) &&
            decl.astKind != ASTKind::PRIMARY_CTOR_DECL;
        if (useInternalIdent) {
            mangled += MangleUtils::MangleName(MANGLE_LT_PREFIX + decl.identifier + MANGLE_GT_PREFIX);
        } else {
            mangled += MangleUtils::MangleName(decl.identifier);
        }
        MangleGenericArguments(decl);
        mangled += MangleDeclAstKind(decl.astKind);
        MangleOthers(decl);
    }

    void MangleOthers(const Decl& decl)
    {
        if (decl.astKind == ASTKind::VAR_DECL || IsMemberParam(decl)) {
            mangled += MANGLE_DIDOLLAR_PREFIX;
            if (auto& varType = StaticCast<VarDecl&>(decl).type) {
                mangled += MangleType(*varType);
            }
        }

        if (auto mainDecl = DynamicCast<const MainDecl*>(&decl)) {
            MangleFuncParameters(mainDecl->funcBody->paramLists[0]->params);
            if (auto retType = mainDecl->funcBody->retType.get()) {
                mangled += MangleType(*retType);
            }
        }

        if (auto funcDecl = DynamicCast<const FuncDecl*>(&decl)) {
            CJC_ASSERT(!funcDecl->funcBody->paramLists.empty());
            MangleFuncParameters(funcDecl->funcBody->paramLists[0]->params);
            // Special mangle rule for static constructor
            // so that instance and static constructor have different mangle name
            if (funcDecl->TestAttr(Attribute::CONSTRUCTOR) && funcDecl->TestAttr(Attribute::STATIC)) {
                constexpr ssize_t initNameAndEnd = 8; // length of "6<init>F"
                mangled.replace(mangled.size() - initNameAndEnd, initNameAndEnd, MangleUtils::MangleName("<clinit>"));
            }
            mangled += MANGLE_DIDOLLAR_PREFIX;
            if (auto& retType = funcDecl->funcBody->retType) {
                mangled += MangleType(*retType);
            } else if (funcDecl->outerDecl && funcDecl->outerDecl->astKind == ASTKind::PROP_DECL) {
                auto propDecl = StaticCast<PropDecl*>(funcDecl->outerDecl);
                mangled += MangleType(*propDecl->type);
            }
        }

        if (auto ctor = DynamicCast<PrimaryCtorDecl*>(&decl)) {
            MangleFuncParameters(ctor->funcBody->paramLists[0]->params);
        }

        if (auto macro = DynamicCast<MacroDecl*>(&decl)) {
            MangleFuncParameters(macro->funcBody->paramLists[0]->params);
            // Return type mangling is not needed in MacroDecl, because parser checks that the return type of macro
            // is either absent or a 'Tokens' literal
        }
    }

    void MangleExtendLiteralIfInExtend(const AST::Decl& decl)
    {
        // If a decl in an extended type, Mangle `extend` info.
        // Mangle the package name if a generic type is extended.
        auto outerDecl = decl.outerDecl;
        if (outerDecl == nullptr || outerDecl->astKind != ASTKind::EXTEND_DECL) {
            return;
        }
        mangled += MangleUtils::MangleName(MANGLE_EXIG_PREFIX);
    }

    void MangleGenericArguments(const Decl& decl)
    {
        std::vector<const std::string*> args;
        if (decl.astKind == ASTKind::FUNC_DECL) {
            auto& fd = static_cast<const FuncDecl&>(decl);
            if (fd.funcBody->generic) {
                for (auto& arg : fd.funcBody->generic->typeParameters) {
                    args.push_back(&arg->identifier.Val());
                }
            }
        } else if (decl.generic) {
            if (decl.astKind == ASTKind::EXTEND_DECL) {
                for (auto& arg : decl.generic->typeParameters) {
                    args.push_back(&arg->identifier.Val());
                }
            } else {
                // Since generic types with same number of typeParameters may result in same mangle name,
                // only mangle number of typeParameter for non-extend type decl (the detail will be combined in
                // sigHash).
                mangled += MANGLE_LT_PREFIX;
                mangled += std::to_string(decl.generic->typeParameters.size());
                mangled += MANGLE_GT_PREFIX;
                return;
            }
        }
        if (args.empty()) {
            return;
        }
        mangled += MANGLE_LT_PREFIX;
        for (auto it : args) {
            mangled += MangleUtils::MangleName(*it);
        }
        mangled += MANGLE_GT_PREFIX;
    }

    void MangleNestedDecl(const Decl& decl)
    {
        auto outerDecl = decl.outerDecl;
        // Concat all outerDecls' name back to forward.
        while (outerDecl != nullptr) {
            if (outerDecl->TestAttr(Attribute::GLOBAL, Attribute::PRIVATE)) {
                mangled += MangleUtils::MangleFilePrivate(*outerDecl);
            }
            if (outerDecl->astKind == ASTKind::EXTEND_DECL) {
                auto ed = RawStaticCast<const ExtendDecl*>(outerDecl);
                MangleExtendDecl(*ed);
            } else {
                mangled += MangleUtils::MangleName(outerDecl->identifier);
                MangleGenericArguments(*outerDecl);
                mangled += MangleDeclAstKind(outerDecl->astKind);
            }
            outerDecl = outerDecl->outerDecl;
        }
    }

    void MangleExtendDecl(const ExtendDecl& decl)
    {
        mangled += MangleType(*decl.extendedType);
        mangled += MANGLE_LT_COLON_PREFIX; // Always keep a separator, used in extend2decl relation
        std::vector<std::string> inherits(decl.inheritedTypes.size());
        // Sort mangled inherited type names
        for (size_t i{0}; i < inherits.size(); ++i) {
            inherits[i] = MangleType(*decl.inheritedTypes[i]);
        }
        std::stable_sort(inherits.begin(), inherits.end());
        for (size_t i{0}; i < decl.inheritedTypes.size(); ++i) {
            if (i != 0) {
                mangled += "&";
            }
            mangled += inherits[i];
        }
        if (decl.generic) {
            MangleGenericConstraints(*decl.generic);
        }
        mangled += MangleDeclAstKind(ASTKind::EXTEND_DECL);
    }

    // Only generic constraints of ExtendDecl are mangled
    void MangleGenericConstraints(const Generic& generic)
    {
        // Sort generic constraint by constrained type
        std::vector<std::pair<Ptr<const GenericConstraint>, std::string>> gcs(generic.genericConstraints.size());
        for (size_t i{0}; i < gcs.size(); ++i) {
            gcs[i] = {generic.genericConstraints[i].get(), MangleType(*generic.genericConstraints[i]->type)};
        }
        std::stable_sort(gcs.begin(), gcs.end(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
        for (const auto& gc : gcs) {
            mangled += MANGLE_GEXTEND_PREFIX;
            mangled += gc.second;
            // Sort upperbounds
            std::vector<std::string> uppers(gc.first->upperBounds.size());
            for (size_t i{0}; i < uppers.size(); ++i) {
                uppers[i] = MangleType(*gc.first->upperBounds[i]);
            }
            std::stable_sort(uppers.begin(), uppers.end());
            for (const auto& upper : uppers) {
                mangled += ':';
                mangled += upper;
            }
        }
    }

    void MangleVarWithPatternDecl(const AST::VarWithPatternDecl& decl)
    {
        std::string ret{};
        std::vector<Ptr<AST::Pattern>> patterns = FlattenVarWithPatternDecl(decl);

        for (auto pattern : patterns) {
            if (pattern->astKind == AST::ASTKind::VAR_PATTERN) {
                ret += MangleUtils::MangleName(StaticCast<AST::VarPattern*>(pattern)->varDecl->identifier);
            }
        }
        // Means all sub-patterns are wildcard pattern
        if (ret.empty()) {
            ret += MANGLE_WILDCARD_PREFIX;
            ret += MangleUtils::MangleName(decl.curFile->fileName);
            ret += std::to_string(NextWildcard(decl.curFile->fileName));
        }
        mangled += ret;
        if (auto ty = decl.type.get()) {
            mangled += MangleType(*ty);
        }
    }

    int NextWildcard(const std::string& filename)
    {
        return wildcardMap[filename]++;
    }
};

ASTMangler::ASTMangler(const std::string& fullPackageName): pimpl{new ASTManglerImpl{fullPackageName}} {}
ASTMangler::~ASTMangler() { delete pimpl; }

std::string ASTMangler::Mangle(const AST::Decl& decl) const
{
    return pimpl->Mangle(decl);
}
