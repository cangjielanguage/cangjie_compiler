// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/Utils.h"

#include "cangjie/AST/Utils.h"
#include "cangjie/CHIR/Utils/Utils.h"

namespace Cangjie {
namespace CHIR {

inline std::map<Cangjie::AST::Attribute, Attribute> g_attrMap = {
    {Cangjie::AST::Attribute::GENERIC_INSTANTIATED, Attribute::GENERIC_INSTANTIATED},
    {Cangjie::AST::Attribute::STATIC, Attribute::STATIC}, {Cangjie::AST::Attribute::FOREIGN, Attribute::FOREIGN},
    {Cangjie::AST::Attribute::MUT, Attribute::MUT}, {Cangjie::AST::Attribute::INTERNAL, Attribute::INTERNAL},
    {Cangjie::AST::Attribute::PUBLIC, Attribute::PUBLIC}, {Cangjie::AST::Attribute::PRIVATE, Attribute::PRIVATE},
    {Cangjie::AST::Attribute::PROTECTED, Attribute::PROTECTED},
    {Cangjie::AST::Attribute::ABSTRACT, Attribute::ABSTRACT}, {Cangjie::AST::Attribute::OPEN, Attribute::VIRTUAL},
    {Cangjie::AST::Attribute::OVERRIDE, Attribute::OVERRIDE}, {Cangjie::AST::Attribute::REDEF, Attribute::REDEF},
    {Cangjie::AST::Attribute::SEALED, Attribute::SEALED}, {Cangjie::AST::Attribute::OPERATOR, Attribute::OPERATOR},
    {Cangjie::AST::Attribute::COMPILER_ADD, Attribute::COMPILER_ADD},
    {Cangjie::AST::Attribute::IMPLICIT_ADD, Attribute::NO_DEBUG_INFO},
    {Cangjie::AST::Attribute::GENERIC, Attribute::GENERIC}, {Cangjie::AST::Attribute::IMPORTED, Attribute::IMPORTED},
    {Cangjie::AST::Attribute::NO_REFLECT_INFO, Attribute::NO_REFLECT_INFO},
    {Cangjie::AST::Attribute::COMMON, Attribute::COMMON}, {Cangjie::AST::Attribute::PLATFORM, Attribute::PLATFORM},
    {Cangjie::AST::Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR},
    {Cangjie::AST::Attribute::OBJ_C_MIRROR, Attribute::OBJ_C_MIRROR},
    {Cangjie::AST::Attribute::HAS_INITED_FIELD, Attribute::HAS_INITED_FIELD},
    {Cangjie::AST::Attribute::UNSAFE, Attribute::UNSAFE}};

void TranslateFunctionGenericUpperBounds(CHIRType& chirTy, const AST::FuncDecl& func)
{
    CJC_NULLPTR_CHECK(func.funcBody);
    if (func.funcBody->generic) {
        // We need to translate functions' generic type and fill their upperBounds during translation.
        for (auto& type : func.funcBody->generic->typeParameters) {
            chirTy.TranslateType(*type->ty);
        }
        // Must fill upper bounds after translation all generics.
        for (auto& type : func.funcBody->generic->typeParameters) {
            chirTy.FillGenericArgType(StaticCast<AST::GenericsTy>(*type->ty));
        }
    }
}

FuncType* AdjustVarInitType(
    const FuncType& funcType, const AST::Decl& outerDecl, CHIRBuilder& builder, CHIRType& chirType)
{
    auto params = funcType.GetParamTypes();
    std::vector<Type*> paramsTy;
    paramsTy.reserve(params.size() + 1); // additional 1 means the type of this.
    auto thisTy = chirType.TranslateType(*outerDecl.ty);
    // ClassLike decl has already been added ref type by `TranslateType`, so we just needs add ref type to
    // constructor and mut function of non-classLike type.
    if (outerDecl.astKind == AST::ASTKind::STRUCT_DECL) {
        paramsTy.emplace_back(builder.GetType<RefType>(thisTy));
    } else {
        paramsTy.emplace_back(thisTy);
    }
    paramsTy.insert(paramsTy.end(), params.begin(), params.end());

    return builder.GetType<FuncType>(paramsTy, funcType.GetReturnType());
}

FuncType* AdjustFuncType(FuncType& funcType, const AST::FuncDecl& funcDecl, CHIRBuilder& builder, CHIRType& chirType)
{
    auto params = funcType.GetParamTypes();
    // Instance member function needs to add this type.
    if (IsInstanceMember(funcDecl)) {
        std::vector<Type*> paramsTy;
        paramsTy.reserve(params.size() + 1); // additional 1 means the type of this.
        auto thisTy = chirType.TranslateType(*funcDecl.outerDecl->ty);
        paramsTy.emplace_back(AddRefIfFuncIsMutOrClass(*thisTy, funcDecl, builder));
        paramsTy.insert(paramsTy.end(), params.begin(), params.end());

        if (funcDecl.TestAttr(AST::Attribute::CONSTRUCTOR) || funcDecl.IsFinalizer()) {
            return builder.GetType<FuncType>(paramsTy, builder.GetVoidTy());
        } else {
            return builder.GetType<FuncType>(paramsTy, funcType.GetReturnType());
        }
    }
    return &funcType;
}

DebugLocation GetDeclLoc(const CHIRContext& cctx, const AST::Decl& decl)
{
    auto& begin = decl.identifier.Begin();
    return TranslateLocationWithoutScope(cctx, begin, decl.identifier.End());
}

DebugLocation TranslateLocationWithoutScope(
    const CHIRContext& context, const Cangjie::Position& beginPos, const Cangjie::Position& endPos)
{
    // Note: AST2CHIL adaptation of scopeInfo is needed.
    return {context.GetSourceFileName(beginPos.fileID), beginPos.fileID,
        {unsigned(beginPos.line), unsigned(beginPos.column)}, {unsigned(endPos.line), unsigned(endPos.column)}, {0}};
}

std::vector<GenericType*> GetGenericParamType(const AST::Decl& decl, CHIRType& chirType)
{
    std::vector<GenericType*> ts;
    AST::Generic* generic = nullptr;
    if (auto funcDecl = DynamicCast<const AST::FuncDecl*>(&decl); funcDecl) {
        generic = funcDecl->funcBody->generic.get();
    } else {
        generic = decl.generic.get();
    }
    if (!decl.TestAttr(AST::Attribute::GENERIC)) {
        return ts;
    }
    CJC_NULLPTR_CHECK(generic);
    for (auto& genericTy : generic->typeParameters) {
        ts.emplace_back(StaticCast<GenericType*>(chirType.TranslateType(*(genericTy->ty))));
    }
    return ts;
}

std::string GetNameOfDefinedPackage(const AST::FuncDecl& funcDecl)
{
    if (funcDecl.outerDecl != nullptr && funcDecl.outerDecl->genericDecl != nullptr) {
        return funcDecl.outerDecl->genericDecl->fullPackageName;
    }
    if (funcDecl.genericDecl != nullptr) {
        return funcDecl.genericDecl->fullPackageName;
    }
    return funcDecl.fullPackageName;
}

AttributeInfo BuildAttr(const AST::AttributePack& attr)
{
    AttributeInfo attrInfo;
    for (auto& it : std::as_const(g_attrMap)) {
        if (attr.TestAttr(it.first)) {
            attrInfo.SetAttr(it.second, true);
        }
    }
    return attrInfo;
}

AttributeInfo BuildVarDeclAttr(const AST::VarDecl& decl)
{
    auto attrInfo = BuildAttr(decl.GetAttrs());
    if (!decl.isVar) {
        attrInfo.SetAttr(Attribute::READONLY, true);
    }
    if (decl.isConst) {
        attrInfo.SetAttr(Attribute::CONST, true);
    }
    return attrInfo;
}

bool IsStructMutFunction(const AST::FuncDecl& function)
{
    if (function.outerDecl == nullptr) {
        return false;
    }
    if (function.outerDecl->astKind == AST::ASTKind::STRUCT_DECL) {
        return function.TestAnyAttr(AST::Attribute::CONSTRUCTOR, AST::Attribute::MUT);
    }
    if (function.outerDecl->astKind == AST::ASTKind::EXTEND_DECL &&
        RawStaticCast<AST::ExtendDecl*>(function.outerDecl)->extendedType->ty->IsStruct()) {
        return function.TestAnyAttr(AST::Attribute::CONSTRUCTOR, AST::Attribute::MUT);
    }
    return false;
}

bool CollectFrozenFuncDecl(const AST::FuncDecl& funcDecl, const GlobalOptions& opts)
{
    return opts.IsCHIROptimizationLevelOverO2() && funcDecl.isFrozen && !funcDecl.TestAttr(AST::Attribute::ABSTRACT);
}

bool IsLocalVarDecl(const AST::Decl& decl)
{
    return decl.astKind == AST::ASTKind::VAR_DECL &&
        !decl.TestAttr(AST::Attribute::STATIC) && !decl.TestAttr(AST::Attribute::GLOBAL);
}

bool IsSrcImportedDecl(const AST::Decl& decl, const GlobalOptions& opts)
{
    if (!decl.TestAttr(AST::Attribute::IMPORTED)) {
        return false;
    }
    // 1. imported const func or var
    if (decl.IsConst()) {
        return true;
    }
    // 2. imported frozen func
    if (decl.astKind == AST::ASTKind::FUNC_DECL) {
        if (CollectFrozenFuncDecl(StaticCast<const AST::FuncDecl&>(decl), opts)) {
            return true;
        }
    }
    // 3. static member var, need to check static init func
    if (decl.TestAttr(AST::Attribute::STATIC) && decl.astKind == AST::ASTKind::VAR_DECL &&
        decl.outerDecl != nullptr && decl.outerDecl->IsNominalDecl()) {
        for (auto& member : decl.outerDecl->GetMemberDecls()) {
            if (member->TestAttr(AST::Attribute::CONSTRUCTOR) && member->TestAttr(AST::Attribute::STATIC)) {
                if (IsSrcCodeImportedGlobalDecl(*member, opts)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool IsSrcCodeImportedGlobalDecl(const AST::Decl& decl, const GlobalOptions& opts)
{
    return IsSrcImportedDecl(decl, opts) && !IsLocalVarDecl(decl);
}

bool IsSymbolImportedDecl(const AST::Decl& decl, const GlobalOptions& opts)
{
    return decl.TestAttr(AST::Attribute::IMPORTED) && !IsSrcImportedDecl(decl, opts);
}

AST::Decl* GetOuterDecl(const AST::Decl& decl)
{
    auto outerDecl = decl.outerDecl;
    if (auto func = DynamicCast<const AST::FuncDecl*>(&decl); func && func->ownerFunc != nullptr) {
        outerDecl = func->ownerFunc->outerDecl;
    }
    return outerDecl;
}

bool IsLocalFunc(const AST::FuncDecl& func)
{
    auto outerDecl = GetOuterDecl(func);
    return outerDecl != nullptr &&
        (outerDecl->astKind == AST::ASTKind::FUNC_DECL || outerDecl->astKind == AST::ASTKind::VAR_DECL);
}

const std::unordered_set<std::string> OVERFLOW_OPERATOR_NAMES{"+", "-", "*", "/"};

bool IsOverflowOperator(const std::string& name)
{
    return OVERFLOW_OPERATOR_NAMES.find(name) != OVERFLOW_OPERATOR_NAMES.end();
}

/// Check whether a type can be integer type. This may change when spec changes.
bool CanBeIntegerType(const Type& type)
{
    return type.IsInteger() || type.IsGeneric();
}
///@{
/// These two functions are used by TranslateNonStaticMemberFuncCall
/// Any operator that has different behaviours on integer types when @Overflow changes, if defined in an
/// interface that can be extended by integer types, needs to be split into three operator funcs, so that when
/// called, the integer overflow strategy of this overloaded operator can be match the call site overflow
/// strategy.
/// Operators to be split: +, -, *, /(only in MIN/-1)
/// Definition: when can an interface be implemented by integer types?
/// The current definition is that any interface can, but it may change when spec changes,
/// e.g. interface with This as return type cannot be implemented by
/// structs (to which all integer types belong), but for now that feature has not been implemented.
bool IsOverflowOperator(const std::string& name, const FuncType& type)
{
    auto params = type.GetParamTypes();
    if (params.size() == 1 && IsOverflowOperator(name)) {
        return CanBeIntegerType(*params[0]) && CanBeIntegerType(*type.GetReturnType());
    }
    if (params.size() == 0 && name == "-") {
        return CanBeIntegerType(*type.GetReturnType());
    }
    return false;
}

std::string OverflowStrategyPrefix(OverflowStrategy ovf)
{
    switch (ovf) {
        case OverflowStrategy::WRAPPING:
            return "&";
        case OverflowStrategy::THROWING:
            return "~";
        default:
            return "%";
    }
}

void SetCompileTimeValueFlagRecursivly(Func& initFunc)
{
    auto setConstFlagForLambda = [](BlockGroup& body) {
        std::function<VisitResult(Expression&)> preVisit = [&preVisit](Expression& expr) {
            if (expr.GetExprKind() == CHIR::ExprKind::LAMBDA) {
                auto& lambda = StaticCast<Lambda&>(expr);
                lambda.SetCompileTimeValue();
                Visitor::Visit(*lambda.GetBody(), preVisit);
            }
            return VisitResult::CONTINUE;
        };
        Visitor::Visit(body, preVisit);
    };
    setConstFlagForLambda(*initFunc.GetBody());
}

MemberVarInfo GetMemberVarByName(const CustomTypeDef& def, const std::string& varName)
{
    auto vars = def.GetAllInstanceVars();
    for (auto it = vars.crbegin(); it != vars.crend(); ++it) {
        if (it->name == varName) {
            return *it;
        }
    }
    CJC_ABORT();
    return MemberVarInfo{.attributeInfo = AttributeInfo{}};
}

Type* GetInstMemberTypeByName(const CustomType& rootType, const std::vector<std::string>& names, CHIRBuilder& builder)
{
    return GetInstMemberTypeByNameCheckingReadOnly(rootType, names, builder).first;
}

std::pair<Type*, bool> GetInstMemberTypeByNameCheckingReadOnly(
    const CustomType& rootType, const std::vector<std::string>& names, CHIRBuilder& builder)
{
    if (names.empty()) {
#ifdef NDEBUG
        return {const_cast<CustomType*>(&rootType), false};
#else
        CJC_ABORT();
#endif
    }
    auto customTypeDef = rootType.GetCustomTypeDef();
    std::unordered_map<const GenericType*, Type*> instMap;
    rootType.GetInstMap(instMap, builder);
    auto currentName = names.front();
    auto member = GetMemberVarByName(*customTypeDef, currentName);
    // if one member is readonly in path, the result is readonly
    bool isReadOnly = member.TestAttr(Attribute::READONLY);
    auto memberTy = ReplaceRawGenericArgType(*member.type, instMap, builder);
    if (names.size() > 1) {
        auto pureMemberTy = memberTy->StripAllRefs();
        if (pureMemberTy->IsNominal()) {
            auto subNames = names;
            subNames.erase(subNames.begin());
            auto [memberType, isMemberReadOnly] =
                GetInstMemberTypeByNameCheckingReadOnly(*StaticCast<CustomType*>(pureMemberTy), subNames, builder);
            return {memberType, isReadOnly || isMemberReadOnly};
        } else if (pureMemberTy->IsGeneric()) {
            auto subNames = names;
            subNames.erase(subNames.begin());
            auto [memberType, isMemberReadOnly] =
                GetInstMemberTypeByNameCheckingReadOnly(*StaticCast<GenericType*>(pureMemberTy), subNames, builder);
            return {memberType, isReadOnly || isMemberReadOnly};
        } else {
            CJC_ABORT();
        }
    }
    return {memberTy, isReadOnly};
}

std::pair<Type*, bool> GetInstMemberTypeByNameCheckingReadOnly(
    const GenericType& rootType, const std::vector<std::string>& names, CHIRBuilder& builder)
{
    // Find the most child class type
    Type* concreteType = nullptr;
    for (auto upperBound : rootType.GetUpperBounds()) {
        auto upperBoundCustomType = StaticCast<CustomType*>(upperBound->StripAllRefs());
        if (upperBoundCustomType->GetCustomTypeDef()->IsClassLike()) {
            if (concreteType == nullptr) {
                concreteType = upperBound;
            } else {
                if (upperBoundCustomType->IsEqualOrSubTypeOf(*concreteType, builder)) {
                    concreteType = upperBound;
                }
            }
        }
    }
    CJC_NULLPTR_CHECK(concreteType);
    return GetInstMemberTypeByNameCheckingReadOnly(
        *StaticCast<CustomType*>(concreteType->StripAllRefs()), names, builder);
}

Type* AddRefIfFuncIsMutOrClass(Type& thisType, const AST::FuncDecl& funcDecl, CHIRBuilder& builder)
{
    if (thisType.IsRef()) {
        return &thisType;
    }
    if (thisType.IsClassOrArray() || funcDecl.TestAnyAttr(AST::Attribute::MUT, AST::Attribute::CONSTRUCTOR)) {
        return builder.GetType<RefType>(&thisType);
    } else {
        return &thisType;
    }
}
} // namespace CHIR
} // namespace Cangjie
