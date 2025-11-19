// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements utils API for CHIR
 */

#include "cangjie/CHIR/Utils.h"

#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Visitor/Visitor.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ConstantsUtils.h"

#include <iostream>
#include <stack>

namespace Cangjie::CHIR {
bool CheckFuncName(const FuncBase& func, const FuncInfo& funcInfo)
{
    return func.GetSrcCodeIdentifier() == funcInfo.funcName;
}

bool CheckParentTy(const FuncBase& func, const FuncInfo& funcInfo)
{
    if (funcInfo.parentTy == NOT_CARE) {
        return true;
    }

    auto parentTy = func.GetParentCustomTypeDef();
    if (auto extendDef = DynamicCast<const ExtendDef*>(parentTy); extendDef) {
        auto extendedType = extendDef->GetExtendedType();
        if (extendedType == nullptr) {
            parentTy = nullptr;
        } else if (auto customTy = DynamicCast<const CustomType*>(extendedType); customTy) {
            parentTy = customTy->GetCustomTypeDef();
        } else {
            parentTy = nullptr;
        }
    }
    bool parentMatch = false;
    if (parentTy == nullptr) {
        if (funcInfo.parentTy.empty()) {
            parentMatch = true;
        }
    } else {
        if (funcInfo.parentTy == ANY_TYPE) {
            parentMatch = true;
        } else if (funcInfo.parentTy == parentTy->GetSrcCodeIdentifier()) {
            parentMatch = true;
        } else if (funcInfo.parentTy.find(Cangjie::BOX_DECL_PREFIX) != std::string::npos &&
            parentTy->GetSrcCodeIdentifier().find(funcInfo.parentTy) == 0) {
            parentMatch = true;
        }
    }
    return parentMatch;
}

bool CheckParametersTy(const FuncBase& funcBase, const FuncInfo& funcInfo)
{
    bool needToMatchArgsNum = true;
    auto paramTys = funcBase.GetFuncType()->GetParamTypes();
    size_t loopCnt = funcInfo.params.size() < paramTys.size() ? funcInfo.params.size() : paramTys.size();
    for (size_t i = 0; i < loopCnt; ++i) {
        std::string expectedArgTyStr = funcInfo.params[i];
        if (expectedArgTyStr == ANY_TYPE) {
            continue;
        }
        if (expectedArgTyStr == NOT_CARE) {
            needToMatchArgsNum = false;
            break;
        }
        // The ToString() function will return the mangled type name, but
        // the expected name is not mangled. This is tracked by issue 2385.
        if (expectedArgTyStr != paramTys[i]->ToString()) {
            return false;
        }
    }
    // expect:  func foo(a, b, c, NOT_CARE)
    // in fact: func foo(a, b, c)
    // then, `funcInfo.params.size()` is 4 and `args.size()` is 3, but we set `NOT_CARE` as 4th arg,
    // that means we don't care about what's next or if there is next
    // well, if we expect `func foo(a, b, c, NOT_CARE, d, e, f)`, and we still can match `func foo(a, b, c)`
    // so we should use `ANY_TYPE` instead of `NOT_CARE` in this case
    if (funcInfo.params.size() > loopCnt && funcInfo.params[loopCnt] == NOT_CARE) {
        needToMatchArgsNum = false;
    }
    if (needToMatchArgsNum && funcInfo.params.size() != paramTys.size()) {
        return false;
    }
    return true;
}

bool CheckReturnTy(const FuncBase& func, const FuncInfo& funcInfo)
{
    if (funcInfo.returnTy == NOT_CARE || funcInfo.returnTy == ANY_TYPE) {
        return true;
    }
    auto returnTy = func.GetReturnType();
    CJC_NULLPTR_CHECK(returnTy);
    // The ToString() function will return the mangled type name, but
    // the expected name is not mangled. This is tracked by issue 2385.
    return returnTy->ToString() == funcInfo.returnTy;
}

bool CheckPkgName(const FuncBase& func, const FuncInfo& funcInfo)
{
    if (funcInfo.pkgName == NOT_CARE || funcInfo.pkgName == ANY_TYPE) {
        return true;
    }
    // Note: 现在还没有泛型声明版本的包名，待添加
    return funcInfo.pkgName == func.GetPackageName();
}

bool IsExpectedFunction(const FuncBase& func, const FuncInfo& funcInfo)
{
    return CheckFuncName(func, funcInfo) && CheckParentTy(func, funcInfo) && CheckParametersTy(func, funcInfo) &&
        CheckReturnTy(func, funcInfo) && CheckPkgName(func, funcInfo);
}

void PrintOptInfo(const Expression& e, bool debug, const std::string& optName)
{
    if (!debug) {
        return;
    }
    auto& position = e.GetDebugLocation();
    std::string strPos = "in the line:" + std::to_string(position.GetBeginPos().line) +
        " and the column:" + std::to_string(position.GetBeginPos().column);
    std::string message = "The " + e.GetExprKindName() + " node " + strPos + ", was optimized by " + optName;
    std::cout << message << std::endl;
}

FuncKind GetFuncKindFromAST(const AST::FuncDecl& func)
{
    if (func.isGetter) {
        return FuncKind::GETTER;
    }
    if (func.isSetter) {
        return FuncKind::SETTER;
    }
    if (func.IsFinalizer()) {
        return FuncKind::FINALIZER;
    }
    if (func.TestAttr(AST::Attribute::MAIN_ENTRY)) {
        return FuncKind::MAIN_ENTRY;
    }
    if (func.TestAttr(AST::Attribute::MACRO_FUNC)) {
        return FuncKind::MACRO_FUNC;
    }
    if (func.TestAttr(AST::Attribute::PRIMARY_CONSTRUCTOR)) {
        if (func.TestAttr(AST::Attribute::IN_STRUCT)) {
            return FuncKind::PRIMAL_STRUCT_CONSTRUCTOR;
        } else if (func.TestAttr(AST::Attribute::IN_CLASSLIKE)) {
            return FuncKind::PRIMAL_CLASS_CONSTRUCTOR;
        } else {
            CJC_ABORT();
        }
    } else if (func.TestAttr(AST::Attribute::CONSTRUCTOR)) {
        if (func.TestAttr(AST::Attribute::IN_STRUCT)) {
            return FuncKind::STRUCT_CONSTRUCTOR;
        } else if (func.TestAttr(AST::Attribute::IN_CLASSLIKE)) {
            return FuncKind::CLASS_CONSTRUCTOR;
        } else {
            CJC_ABORT();
        }
    }
    if (func.TestAnyAttr(AST::Attribute::HAS_INITIAL)) {
        return FuncKind::DEFAULT_PARAMETER_FUNC;
    }
    return FuncKind::DEFAULT;
}

bool IsVirtualFunction(const FuncBase& funcDecl)
{
    // rule 1: global function is not virtual function
    auto parent = funcDecl.GetParentCustomTypeDef();
    if (parent == nullptr) {
        return false;
    }

    // rule 2: function declared in struct, enum and extend def is not virtual function
    auto parentClass = DynamicCast<const ClassDef*>(parent);
    if (parentClass == nullptr) {
        return false;
    }

    // function declared in interface, must be virtual function
    if (parentClass->IsInterface()) {
        return true;
    }

    /**
     * A special case:
     * class A {
     *     // The following functions are not semantic virtual functions since the class is not `open`,
     *     // they will never be overrode even if they have `open` flag.
     *     public open func foo() {}
     *     open func goo() {}
     * }
     */
    // rule 3: if parent class is not open, this function is not virtual function
    auto isInNonOpenClass = !parentClass->CanBeInherited();
    if (isInNonOpenClass) {
        return false;
    }

    // static function declared in open class or interface, also need add to vtable
    if (funcDecl.TestAttr(Attribute::STATIC)) {
        return true;
    }

    // rule 4: constructors and de-constructors are not virtual function
    if (funcDecl.GetFuncKind() == FuncKind::CLASS_CONSTRUCTOR ||
        funcDecl.GetFuncKind() == FuncKind::FINALIZER) {
        return false;
    }

    // rule 5: generic instantiated function, is not virtual function
    if (funcDecl.TestAttr(Attribute::GENERIC_INSTANTIATED)) {
        return false;
    }

    // rule 6: only public or protected function has the opportunity to be overridden
    if (!funcDecl.TestAttr(Attribute::PUBLIC) && !funcDecl.TestAttr(Attribute::PROTECTED)) {
        return false;
    }

    // rule 7: if the base function has open modifier, or override modifier, or is abstract,
    // it must be virtual function
    return funcDecl.TestAttr(Attribute::VIRTUAL) || funcDecl.TestAttr(Attribute::ABSTRACT) ||
        funcDecl.TestAttr(Attribute::OVERRIDE);
}

bool IsInterfaceStaticMethod(const Func& func)
{
    auto parent = func.GetParentCustomTypeDef();
    if (parent == nullptr) {
        return false;
    }
    auto parentClass = DynamicCast<const ClassDef*>(parent);
    if (parentClass == nullptr) {
        return false;
    }
    if (!parentClass->IsInterface()) {
        return false;
    }
    return func.TestAttr(Attribute::STATIC);
}

struct VisitStackElem {
    Block* bb;
    std::vector<Block*> successors;
    VisitStackElem(Block* bb, std::vector<Block*>&& successors) : bb(bb), successors(std::move(successors))
    {
    }
};

std::deque<Block*> TopologicalSort(Block* entrybb)
{
    CJC_NULLPTR_CHECK(entrybb);
    std::deque<Block*> res;

    std::unordered_set<Block*> visited;
    std::stack<VisitStackElem> visitStack;

    visited.insert(entrybb);
    visitStack.emplace(entrybb, entrybb->GetSuccessors());

    while (!visitStack.empty()) {
        while (!visitStack.top().successors.empty()) {
            auto lastSuc = visitStack.top().successors.back();
            visitStack.top().successors.pop_back();
            if (auto [_, hasInserted] = visited.emplace(lastSuc); hasInserted) {
                visitStack.emplace(lastSuc, lastSuc->GetSuccessors());
            }
        }

        do {
            res.emplace_front(visitStack.top().bb);
            visitStack.pop();
        } while (!visitStack.empty() && visitStack.top().successors.empty());
    };

    return res;
}

void AddExpressionsToGlobalInitFunc(const Func& initFunc, const std::vector<Expression*>& insertExpr)
{
    auto visitAction = [&insertExpr](Expression& expr) {
        if (expr.GetExprKind() != ExprKind::STORE) {
            return VisitResult::CONTINUE;
        }
        auto storeNode = static_cast<Store*>(&expr);
        if (storeNode->GetLocation()->GetSrcCodeIdentifier() != GV_PKG_INIT_ONCE_FLAG) {
            return VisitResult::CONTINUE;
        }
        auto parent = expr.GetParentBlock();
        const auto& exprs = parent->GetExpressions();
        for (auto it : insertExpr) {
            it->MoveBefore(exprs.back());
        }
        return VisitResult::STOP;
    };
    Visitor::Visit(initFunc, visitAction);
}

std::vector<Ptr<AST::VarDecl>> GetStaticMemberVars(const AST::InheritableDecl& decl)
{
    std::vector<Ptr<AST::VarDecl>> memberVars;
    auto members = decl.GetMemberDeclPtrs();
    for (auto& member : members) {
        if (member->astKind == AST::ASTKind::VAR_DECL && member->TestAttr(AST::Attribute::STATIC)) {
            memberVars.emplace_back(StaticCast<AST::VarDecl*>(member.get()));
        }
    }
    return memberVars;
}

std::vector<Ptr<AST::VarDecl>> GetNonStaticMemberVars(const AST::InheritableDecl& decl)
{
    std::vector<Ptr<AST::VarDecl>> memberVars;
    auto members = decl.GetMemberDeclPtrs();
    for (auto& member : members) {
        if (member->astKind == AST::ASTKind::VAR_DECL && !member->TestAttr(AST::Attribute::STATIC)) {
            memberVars.emplace_back(StaticCast<AST::VarDecl*>(member.get()));
        }
    }
    return memberVars;
}

std::vector<Ptr<AST::VarDecl>> GetNonStaticSuperMemberVars(const AST::ClassLikeDecl& classLikeDecl)
{
    if (auto classDecl = DynamicCast<const AST::ClassDecl*>(&classLikeDecl); classDecl) {
        auto superClassDecl = classDecl->GetSuperClassDecl();
        if (superClassDecl != nullptr) {
            auto grandSuperMemberVars = GetNonStaticSuperMemberVars(*superClassDecl);
            auto superMemberVars = GetNonStaticMemberVars(*superClassDecl);
            grandSuperMemberVars.insert(grandSuperMemberVars.end(), superMemberVars.begin(), superMemberVars.end());
            return grandSuperMemberVars;
        }
    }

    return std::vector<Ptr<AST::VarDecl>>{};
}

bool IsCrossPackage(const Cangjie::Position& pos, const std::string& currentPackage, DiagAdapter& diag)
{
    if (diag.GetSourceManager().GetSource(pos.fileID).packageName.has_value()) {
        if (currentPackage != diag.GetSourceManager().GetSource(pos.fileID).packageName.value()) {
            return true;
        }
    }
    return false;
}

bool IsNestedBlockOf(const BlockGroup* blockGroup, const BlockGroup* scope)
{
    CJC_NULLPTR_CHECK(blockGroup);
    CJC_NULLPTR_CHECK(scope);
    if (blockGroup == scope) {
        return true;
    }
    if (auto parentLambda = DynamicCast<Lambda*>(blockGroup->GetOwnerExpression())) {
        return IsNestedBlockOf(parentLambda->GetParentBlockGroup(), scope);
    }
    return false;
}

Type* CreateNewTypeWithArgs(Type& oldType, const std::vector<Type*>& newArgs, CHIRBuilder& builder)
{
    if (newArgs.empty()) {
        return &oldType;
    }
    Type* newType = nullptr;
    switch (oldType.GetTypeKind()) {
        case Type::TypeKind::TYPE_TUPLE:
            newType = builder.GetType<TupleType>(newArgs);
            break;
        case Type::TypeKind::TYPE_STRUCT:
            newType = builder.GetType<StructType>(StaticCast<const StructType&>(oldType).GetStructDef(), newArgs);
            break;
        case Type::TypeKind::TYPE_ENUM: {
            auto& enumType = StaticCast<const EnumType&>(oldType);
            newType = builder.GetType<EnumType>(enumType.GetEnumDef(), newArgs);
            break;
        }
        case Type::TypeKind::TYPE_FUNC: {
            std::vector<Type*> paramTys{newArgs.begin(), newArgs.end() - 1};
            Type* retTy = newArgs.back();
            auto hasVarArg = StaticCast<const FuncType&>(oldType).HasVarArg();
            newType = builder.GetType<FuncType>(paramTys, retTy, hasVarArg, oldType.IsCFunc());
            break;
        }
        case Type::TypeKind::TYPE_CLASS:
            newType = builder.GetType<ClassType>(StaticCast<const ClassType&>(oldType).GetClassDef(), newArgs);
            break;
        case Type::TypeKind::TYPE_REFTYPE:
            CJC_ASSERT(newArgs.size() == 1);
            newType = builder.GetType<RefType>(newArgs[0]);
            break;
        case Type::TypeKind::TYPE_CPOINTER:
            CJC_ASSERT(newArgs.size() == 1);
            newType = builder.GetType<CPointerType>(newArgs[0]);
            break;
        case Type::TypeKind::TYPE_RAWARRAY:
            CJC_ASSERT(newArgs.size() == 1);
            newType = builder.GetType<RawArrayType>(newArgs[0], StaticCast<const RawArrayType&>(oldType).GetDims());
            break;
        case Type::TypeKind::TYPE_VARRAY:
            CJC_ASSERT(newArgs.size() == 1);
            newType = builder.GetType<VArrayType>(newArgs[0], StaticCast<const VArrayType&>(oldType).GetSize());
            break;
        case Type::TypeKind::TYPE_BOXTYPE:
            CJC_ASSERT(newArgs.size() == 1);
            newType = builder.GetType<BoxType>(newArgs[0]);
            break;
        default:
            CJC_ABORT();
            break;
    }
    return newType;
}

// replace generic type with instantiated type
Type* ReplaceRawGenericArgType(
    Type& type, const std::unordered_map<const GenericType*, Type*>& replaceTable, CHIRBuilder& builder)
{
    // nothing to replace, just return `type` itself
    if (replaceTable.empty()) {
        return &type;
    }
    // e.g. class A<T> { func foo<T, U>() {} }
    // if `type` is `foo::T`, return instantiated type
    // if `type` is `foo::U`, return `U`
    auto genericTy = DynamicCast<const GenericType*>(&type);
    if (genericTy != nullptr) {
        auto it = replaceTable.find(genericTy);
        if (it != replaceTable.end()) {
            return it->second;
        } else {
            return &type;
        }
    }
    std::vector<Type*> newArgs;
    for (auto argTy : type.GetTypeArgs()) {
        newArgs.emplace_back(ReplaceRawGenericArgType(*argTy, replaceTable, builder));
    }
    return CreateNewTypeWithArgs(type, newArgs, builder);
}

Type* ReplaceThisTypeToConcreteType(Type& type, Type& concreteType, CHIRBuilder& builder)
{
    if (auto refTy = DynamicCast<RefType*>(&type); refTy && refTy->GetBaseType()->IsThis()) {
        return &concreteType;
    } else if (type.IsThis()) {
        return &concreteType;
    }
    std::vector<Type*> newArgs;
    for (auto argTy : type.GetTypeArgs()) {
        newArgs.emplace_back(ReplaceThisTypeToConcreteType(*argTy, concreteType, builder));
    }
    return CreateNewTypeWithArgs(type, newArgs, builder);
}

FuncType* ConvertRealFuncTypeToVirtualFuncType(const FuncType& type, CHIRBuilder& builder)
{
    auto realParams = type.GetParamTypes();
    CJC_ASSERT(!realParams.empty());
    auto paramInVtable = std::vector<Type*>{realParams.begin() + 1, realParams.end()};
    return builder.GetType<FuncType>(paramInVtable, builder.GetType<UnitType>());
}

Value* TypeCastIfNeeded(
    Value& val, Type& expectedTy, CHIRBuilder& builder, Block& parentBlock, const DebugLocation& loc, bool needCheck)
{
    // Do not cast a nothing type value, otherwise it will mess up the dead code elimination
    if (val.GetType()->IsNothing()) {
        return &val;
    }
    if (val.GetType() != &expectedTy) {
        Value* tmpValue = &val;
        if (val.GetType()->StripAllRefs()->IsGeneric() && val.GetType()->IsRef()) {
            auto load = builder.CreateExpression<Load>(val.GetType()->StripAllRefs(), &val, &parentBlock);
            parentBlock.AppendExpression(load);
            tmpValue = load->GetResult();
        }
        auto ret = builder.CreateExpression<TypeCast>(loc, &expectedTy, tmpValue, &parentBlock);
        parentBlock.AppendExpression(ret);
        ret->Set<NeedCheckCast>(needCheck);
        return ret->GetResult();
    }
    return &val;
}

static bool HasGenericInNonFuncScope(const Type& type)
{
    bool hasGenericInNonFuncScope = false;
    auto visitor = [&hasGenericInNonFuncScope](const Type& type) {
        if (type.IsFunc()) {
            return false;
        }
        if (type.IsGeneric()) {
            hasGenericInNonFuncScope = true;
        }
        return true;
    };
    type.VisitTypeRecursively(visitor);
    return hasGenericInNonFuncScope;
}

Ptr<Value> TransformGenericIfNeeded(
    Value& val, Type& expectedTy, CHIRBuilder& builder, Block& parentBlock, const DebugLocation& loc, bool needCheck)
{
    (void)needCheck;
    // Specially, the cast for generation/de-generation enum still use type-cast
    if (val.GetType()->IsEnum() && (expectedTy.IsTuple() || expectedTy.IsUnsignedInteger())) {
        return nullptr;
    }
    if ((val.GetType()->IsTuple() || val.GetType()->IsUnsignedInteger()) && expectedTy.IsEnum()) {
        return nullptr;
    }

    // this should be deleted after supported by codegen
    if (val.GetType()->IsStructArray() && expectedTy.IsStructArray()) {
        return nullptr;
    }

    if (HasGenericInNonFuncScope(expectedTy) && !HasGenericInNonFuncScope(*val.GetType())) {
        auto ret = builder.CreateExpression<TransformToGeneric>(loc, &expectedTy, &val, &parentBlock);
        parentBlock.AppendExpression(ret);
        return ret->GetResult();
    } else if (HasGenericInNonFuncScope(*val.GetType()) && !HasGenericInNonFuncScope(expectedTy)) {
        auto ret = builder.CreateExpression<TransformToConcrete>(loc, &expectedTy, &val, &parentBlock);
        parentBlock.AppendExpression(ret);
        return ret->GetResult();
    }
    return nullptr;
}

bool LeftTypeIsBoxOfRightType(const Type& left, const Type& right)
{
    // BoxType must be ref
    if (!left.IsRef()) {
        return false;
    }
    auto boxType = DynamicCast<const BoxType*>(StaticCast<const RefType&>(left).GetBaseType());
    if (boxType == nullptr) {
        return false;
    }

    return &right == boxType->GetBaseType();
}

Ptr<Value> BoxIfNeeded(
    Value& val, Type& expectedTy, CHIRBuilder& builder, Block& parentBlock, const DebugLocation& loc, bool needCheck)
{
    (void)needCheck;
    auto srcTy = val.GetType();
    auto srcIsValueTy = srcTy->IsValueType();
    if (auto ref = DynamicCast<RefType*>(srcTy)) {
        CJC_ASSERT(!ref->GetBaseType()->IsRef());
        srcIsValueTy = ref->GetBaseType()->IsValueType();
    }
    auto srcIsReferenceTy = srcTy->IsRef() && StaticCast<RefType*>(srcTy)->GetBaseType()->IsReferenceType();

    auto dstIsValueTy = expectedTy.IsValueType();
    if (auto ref = DynamicCast<RefType*>(&expectedTy)) {
        CJC_ASSERT(!ref->GetBaseType()->IsRef());
        dstIsValueTy = ref->GetBaseType()->IsValueType();
    }
    auto dstIsReferenceTy = expectedTy.IsRef() && StaticCast<RefType*>(&expectedTy)->GetBaseType()->IsReferenceType();
    if (srcIsValueTy && dstIsReferenceTy) {
        auto boxSrcVal = &val;
        if (srcTy->IsRef()) {
            auto loadSrc = builder.CreateExpression<Load>(
                loc, StaticCast<RefType*>(srcTy)->GetBaseType(), boxSrcVal, &parentBlock);
            parentBlock.AppendExpression(loadSrc);
            boxSrcVal = loadSrc->GetResult();
        }
        auto ret = builder.CreateExpression<Box>(loc, &expectedTy, boxSrcVal, &parentBlock);
        parentBlock.AppendExpression(ret);
        return ret->GetResult();
    } else if (srcIsReferenceTy && dstIsValueTy) {
        if (expectedTy.IsRef()) {
            auto ret = builder.CreateExpression<UnBoxToRef>(loc, &expectedTy, &val, &parentBlock);
            parentBlock.AppendExpression(ret);
            return ret->GetResult();
        } else {
            auto ret = builder.CreateExpression<UnBox>(loc, &expectedTy, &val, &parentBlock);
            parentBlock.AppendExpression(ret);
            return ret->GetResult();
        }
    } else if (LeftTypeIsBoxOfRightType(*srcTy, expectedTy)) {
        auto ret = builder.CreateExpression<UnBox>(loc, &expectedTy, &val, &parentBlock);
        parentBlock.AppendExpression(ret);
        return ret->GetResult();
    } else if (LeftTypeIsBoxOfRightType(expectedTy, *srcTy)) {
        auto ret = builder.CreateExpression<Box>(loc, &expectedTy, &val, &parentBlock);
        parentBlock.AppendExpression(ret);
        return ret->GetResult();
    }
    return nullptr;
}

// this API should not force to insert the generated expression into the end of block
Ptr<Value> TypeCastOrBoxIfNeeded(
    Value& val, Type& expectedTy, CHIRBuilder& builder, Block& parentBlock, const DebugLocation& loc, bool needCheck)
{
    Ptr<Value> ret;
    ret = BoxIfNeeded(val, expectedTy, builder, parentBlock, loc, needCheck);
    if (ret != nullptr) {
        return ret;
    }
    ret = TransformGenericIfNeeded(val, expectedTy, builder, parentBlock, loc, needCheck);
    if (ret != nullptr) {
        return ret;
    }
    return TypeCastIfNeeded(val, expectedTy, builder, parentBlock, loc, needCheck);
}

bool HasNothingType(Type& type)
{
    if (type.IsNothing()) {
        return true;
    }
    if (type.IsFunc()) {
        auto funcType = StaticCast<FuncType*>(&type);
        for (auto param : funcType->GetParamTypes()) {
            if (HasNothingType(*param)) {
                return true;
            }
        }
        return HasNothingType(*funcType->GetReturnType());
    } else if (type.IsTuple()) {
        auto tupleType = StaticCast<TupleType*>(&type);
        for (auto item : tupleType->GetElementTypes()) {
            if (HasNothingType(*item)) {
                return true;
            }
        }
    }
    return false;
}

void ReplaceUsesWithWrapper(Value& curFunc, const Apply* apply, Value& wrapperFunc, bool isForeign)
{
    for (auto user : curFunc.GetUsers()) {
        if (user == apply) {
            continue;
        }
        if (user->GetExprKind() == ExprKind::APPLY) {
            auto applyNode = StaticCast<Apply*>(user);
            if (!isForeign && applyNode->GetCallee() == &curFunc) {
                continue;
            }
        }
        if (user->GetExprKind() == ExprKind::APPLY_WITH_EXCEPTION) {
            auto applyNode = StaticCast<ApplyWithException*>(user);
            if (!isForeign && applyNode->GetCallee() == &curFunc) {
                continue;
            }
        }
        user->ReplaceOperand(&curFunc, &wrapperFunc);
    }
}

bool IsStaticInit(const AST::FuncDecl& func)
{
    return func.TestAttr(AST::Attribute::STATIC, AST::Attribute::CONSTRUCTOR) && func.identifier == STATIC_INIT_FUNC;
}

bool IsStaticInit(const FuncBase& func)
{
    return func.TestAttr(Attribute::STATIC) &&
        (func.GetFuncKind() == FuncKind::CLASS_CONSTRUCTOR || func.GetFuncKind() == FuncKind::STRUCT_CONSTRUCTOR) &&
        func.GetSrcCodeIdentifier() == STATIC_INIT_FUNC;
}

bool IsSuperOrThisCall(const AST::CallExpr& expr)
{
    if (expr.callKind == AST::CallKind::CALL_SUPER_FUNCTION) {
        return true;
    }
    if (auto baseFunc = DynamicCast<AST::RefExpr*>(expr.baseFunc.get())) {
        return baseFunc->isThis;
    }
    return false;
}

std::unordered_map<const GenericType*, Type*> GetInstMapFromCurDefToCurType(const CustomType& curType)
{
    auto genericTypeArgs = curType.GetCustomTypeDef()->GetGenericTypeParams();
    auto instTypeArgs = curType.GetTypeArgs();
    if (!genericTypeArgs.empty()) {
        CJC_ASSERT(genericTypeArgs.size() == instTypeArgs.size());
    }
    std::unordered_map<const GenericType*, Type*> replaceTable;
    for (size_t i = 0; i < genericTypeArgs.size(); ++i) {
        replaceTable.emplace(genericTypeArgs[i], instTypeArgs[i]);
    }

    return replaceTable;
}

std::unordered_map<const GenericType*, Type*> GetInstMapFromCurDefAndExDefToCurType(const CustomType& curType)
{
    auto replaceTable = GetInstMapFromCurDefToCurType(curType);
    for (auto extendDef : curType.GetCustomTypeDef()->GetExtends()) {
        // maybe we can meet `extend<T> A<B<T>> {}`, and `curType` is A<Int32>, then ignore this def,
        // so not need to check `res`
        auto [res, tempTable] = extendDef->GetExtendedType()->CalculateGenericTyMapping(curType);
        replaceTable.merge(tempTable);
    }
    return replaceTable;
}

void GetAllInstantiatedParentType(ClassType& cur, CHIRBuilder& builder, std::vector<ClassType*>& parents,
    std::set<std::pair<const Type*, const Type*>>* visited)
{
    if (std::find(parents.begin(), parents.end(), &cur) != parents.end()) {
        return;
    }

    for (auto ex : cur.GetCustomTypeDef()->GetExtends()) {
        if (!cur.IsEqualOrInstantiatedTypeOf(*ex->GetExtendedType(), builder, visited)) {
            continue;
        }
        // maybe we can meet `extend<T> A<B<T>> {}`, and `curType` is A<Int32>, then ignore this def,
        // so not need to check `res`
        auto [res, replaceTable] = ex->GetExtendedType()->CalculateGenericTyMapping(cur);
        for (auto interface : ex->GetImplementedInterfaceTys()) {
            std::vector<ClassType*> extendParents;
            GetAllInstantiatedParentType(*interface, builder, extendParents, visited);
            for (size_t i = 0; i < extendParents.size(); ++i) {
                extendParents[i] =
                    Cangjie::StaticCast<ClassType*>(ReplaceRawGenericArgType(*extendParents[i], replaceTable, builder));
                if (std::find(parents.begin(), parents.end(), extendParents[i]) == parents.end()) {
                    parents.emplace_back(extendParents[i]);
                }
            }
        }
    }
    for (auto interface : cur.GetImplementedInterfaceTys(&builder, visited)) {
        GetAllInstantiatedParentType(*interface, builder, parents, visited);
    }
    if (auto superClassTy = cur.GetSuperClassTy(&builder)) {
        GetAllInstantiatedParentType(*superClassTy, builder, parents, visited);
    }
    parents.emplace_back(&cur);
}

void GetInstMapFromCustomDefAndParent(
    const CustomTypeDef& def, std::unordered_map<const GenericType*, CHIR::Type*>& instMap, CHIRBuilder& builder)
{
    if (def.IsExtend()) {
        for (auto parentInterfaceTy : def.GetImplementedInterfaceTys()) {
            auto parentInterfaceDef = parentInterfaceTy->GetCustomTypeDef();
            auto parentInterfaceDefGenericParams = parentInterfaceDef->GetGenericTypeParams();
            auto instParentInterfaceTy =
                StaticCast<ClassType*>(ReplaceRawGenericArgType(*parentInterfaceTy, instMap, builder));
            auto parentInterfaceTypeGenericArgs = instParentInterfaceTy->GetGenericArgs();
            for (size_t i = 0; i < parentInterfaceDefGenericParams.size(); ++i) {
                instMap.emplace(parentInterfaceDefGenericParams[i], parentInterfaceTypeGenericArgs[i]);
            }
            parentInterfaceTy->GetInstMap(instMap, builder);
        }
    } else {
        StaticCast<CustomType*>(def.GetType())->GetInstMap(instMap, builder);
    }
}

Type* CreateBoxTypeRef(Type& baseTy, CHIRBuilder& builder)
{
    auto boxTy = builder.GetType<BoxType>(&baseTy);
    return builder.GetType<RefType>(boxTy);
}

Type* GenericTypeConvertor::ConvertToInstantiatedType(Type& type)
{
    return ReplaceRawGenericArgType(type, instMap, builder);
}

FuncType* ConvertFuncParamsAndRetType(FuncType& input, ConvertTypeFunc& convertor, CHIRBuilder& builder)
{
    std::vector<Type*> newParamTys;
    for (auto oldParamTy : input.GetParamTypes()) {
        newParamTys.emplace_back(convertor(*oldParamTy));
    }
    auto newRetTy = convertor(*input.GetReturnType());
    return builder.GetType<FuncType>(newParamTys, newRetTy, input.HasVarArg(), input.IsCFunc());
}

void CollectNonExtendParentDefs(ClassDef& cur, std::unordered_set<const ClassDef*>& allParents)
{
    auto [_, res] = allParents.emplace(&cur);
    if (!res) {
        return;
    }
    for (auto p : cur.GetImplementedInterfaceDefs()) {
        CollectNonExtendParentDefs(*p, allParents);
    }
    if (auto clsDef = DynamicCast<ClassDef*>(&cur)) {
        if (clsDef->GetSuperClassDef() != nullptr) {
            CollectNonExtendParentDefs(*clsDef->GetSuperClassDef(), allParents);
        }
    }
}

bool ParentDefIsFromExtend(const CustomTypeDef& cur, const ClassDef& parent)
{
    std::unordered_set<const ClassDef*> allParents;
    for (auto p : cur.GetImplementedInterfaceDefs()) {
        CollectNonExtendParentDefs(*p, allParents);
    }
    if (auto clsDef = DynamicCast<const ClassDef*>(&cur)) {
        if (clsDef->GetSuperClassDef() != nullptr) {
            CollectNonExtendParentDefs(*clsDef->GetSuperClassDef(), allParents);
        }
    }
    return &cur != &parent && allParents.find(&parent) == allParents.end();
}

void VisitFuncBlocksInTopoSort(const BlockGroup& funcBody, std::function<VisitResult(Expression&)> preVisit)
{
    auto cmp = [](const Ptr<const Block> b1, const Ptr<const Block> b2) {
        return b1->GetIdentifier() < b2->GetIdentifier();
    };
    auto blocks = Utils::VecToSortedSet<decltype(cmp)>(funcBody.GetBlocks(), cmp);
    auto sortedBlock = TopologicalSort(funcBody.GetEntryBlock());
    for (auto block : sortedBlock) {
        Visitor::Visit(*block, preVisit);
        blocks.erase(block);
    }
    for (auto block : blocks) {
        // for orphan block
        Visitor::Visit(*block, preVisit);
    }
}

std::vector<Type*> GetOutDefDeclaredTypes(const Value& innerDef)
{
    /** normally, we should collect all visible generic types, like:
     * class A<T1> { func foo<T2>() {} }  ==> we need to collect {T1, T2} in order
     * but if `innerDef` is in extend def, we need to handle following cases:
     *   1. extend CPointer<Bool> { static func foo() {} }
     *      there is no visible generic types for function `foo`, but we need to collect `Bool`, and then
     *      for class $Auto_Env_Impl, we need to create class def like `class $Auto_Env_foo<T>`
     *      in user point, assume cj code is like `var a = CPointer<Bool>.foo`, we will create `GetInstantiateValue`
     *      with instantiate type `Bool`(something like `GetInstantiateValue(foo, Bool)`)
     *      so generic type size in class def equals to instantiate type size in `GetInstantiateValue`
     *   2. class B<T1, T2> {}; extend<U1, U2> B<U2, U1> { static func foo() {...} }
     *      if we collect visible generic types for `foo`, it must be {U1, U2} in order, but in user point,
     *      cj code must be like `var a = B<Bool, Int32>.foo`, CHIR expr is `GetInstantiateValue(foo, Bool, Int32)`
     *      so the order is {Bool, Int32}, that means we will use Bool to replace U1, use Int32 to replace U2,
     *      obviously, this is wrong, in fact, we need to use Bool to replace U2, use Int32 to replace U1
     * so, if out decl is extend def, we must modify the following `instArgs`
    */
    auto visiableGenericTypes = GetVisiableGenericTypes(innerDef);
    std::vector<Type*> instArgs(visiableGenericTypes.begin(), visiableGenericTypes.end());
    auto parentFunc = DynamicCast<const FuncBase*>(&innerDef);
    if (parentFunc == nullptr) {
        parentFunc = GetTopLevelFunc(innerDef);
    }
    CJC_NULLPTR_CHECK(parentFunc);
    if (auto exDef = DynamicCast<ExtendDef*>(parentFunc->GetParentCustomTypeDef())) {
        auto exGenericTypes = exDef->GetGenericTypeParams();
        auto exTypeArgs = exDef->GetExtendedType()->GetTypeArgs();
        for (size_t i = 0; i < exGenericTypes.size(); ++i) {
            instArgs.erase(instArgs.begin());
        }
        instArgs.insert(instArgs.begin(), exTypeArgs.begin(), exTypeArgs.end());
    }
    return instArgs;
}

std::pair<std::string, FuncType*> GetFuncTypeFromAutoEnvBaseDef(const ClassDef& autoEnvBaseDef)
{
    auto abstractMethods = autoEnvBaseDef.GetAbstractMethods();
    CJC_ASSERT(abstractMethods.size() == 1);
    return {abstractMethods[0].methodName, StaticCast<FuncType*>(abstractMethods[0].methodTy)};
}

std::pair<std::vector<Type*>, Type*> GetFuncTypeWithoutThisPtrFromAutoEnvBaseType(const ClassType& autoEnvBaseType)
{
    auto typeArgs = autoEnvBaseType.GetGenericArgs();
    if (typeArgs.empty()) {
        auto [_, funcType] = GetFuncTypeFromAutoEnvBaseDef(*autoEnvBaseType.GetClassDef());
        auto paramTypes = funcType->GetParamTypes();
        paramTypes.erase(paramTypes.begin());
        auto retType = funcType->GetReturnType();
        return {paramTypes, retType};
    } else {
        auto paramTypes = std::vector<Type*>(typeArgs.begin(), typeArgs.end() - 1);
        auto retType = typeArgs.back();
        return {paramTypes, retType};
    }
}

size_t GetMethodIdxInAutoEnvObject(const std::string& methodName)
{
    size_t methodIdx = 0;
    if (methodName == GENERIC_VIRTUAL_FUNC) {
        methodIdx = 0;
    } else if (methodName == INST_VIRTUAL_FUNC) {
        methodIdx = 1;
    } else {
        CJC_ABORT();
    }
    return methodIdx;
}

Type* GetExpectedInstType(const GenericType& gType, const Type& extendedType, Type& instType)
{
    if (&extendedType == &gType) {
        return &instType;
    }
    if (extendedType.IsGeneric()) {
        return nullptr;
    }
    auto extendedTypeArgs = extendedType.GetTypeArgs();
    auto instTypeArgs = instType.GetTypeArgs();
    CJC_ASSERT(extendedTypeArgs.size() == instTypeArgs.size());
    for (size_t i = 0; i < extendedTypeArgs.size(); ++i) {
        auto result = GetExpectedInstType(gType, *extendedTypeArgs[i], *instTypeArgs[i]);
        if (result != nullptr) {
            return result;
        }
    }
    return nullptr;
}

static constexpr int OPTION_CTOR_SIZE{2};
static bool IsOptionLike(const AST::EnumDecl& decl)
{
    auto& ctors = decl.constructors;
    if (ctors.size() != OPTION_CTOR_SIZE) {
        return false;
    }
    auto isOptionSome = [](const AST::Decl& cons) {
        if (auto func = DynamicCast<AST::FuncDecl>(&cons)) {
            auto& params = func->funcBody->paramLists[0]->params;
            return params.size() == 1;
        }
        return false;
    };
    return (Is<AST::VarDecl>(ctors[0]) && isOptionSome(*ctors[1])) ||
        (Is<AST::VarDecl>(ctors[1]) && isOptionSome(*ctors[0]));
}
Type::TypeKind GetSelectorType(const AST::EnumDecl& decl)
{
    if (decl.hasEllipsis) {
        return Type::TypeKind::TYPE_UINT32;
    }
    if (IsOptionLike(decl)) {
        return Type::TypeKind::TYPE_BOOLEAN;
    }
    return Type::TypeKind::TYPE_UINT32;
}

bool IsEnumSelectorType(const Type& type)
{
    return type.GetTypeKind() == Type::TypeKind::TYPE_UINT32 ||
        type.GetTypeKind() == Type::TypeKind::TYPE_BOOLEAN;
}

static bool IsOptionLike(const EnumDef& def)
{
    auto ctors = def.GetCtors();
    if (ctors.size() != OPTION_CTOR_SIZE) {
        return false;
    }
    return (ctors[0].funcType->GetNumOfParams() == 0 && ctors[1].funcType->GetNumOfParams() == 1) ||
        (ctors[1].funcType->GetNumOfParams() == 0 && ctors[0].funcType->GetNumOfParams() == 1);
}
Type::TypeKind GetSelectorType(const EnumDef& def)
{
    if (!def.IsExhaustive()) {
        return Type::TypeKind::TYPE_UINT32;
    }
    if (IsOptionLike(def)) {
        return Type::TypeKind::TYPE_BOOLEAN;
    }
    return Type::TypeKind::TYPE_UINT32;
}

std::vector<ClassDef*> GetExtendedInterfaceDefs(const CustomTypeDef& def)
{
    std::vector<ClassDef*> defs;
    for (auto extend : def.GetExtends()) {
        auto extendInterface = extend->GetImplementedInterfaceDefs();
        defs.insert(defs.end(), extendInterface.begin(), extendInterface.end());
    }
    return defs;
}

bool CheckCustomTypeDefIsExpected(
    const CustomTypeDef& def, const std::string& packageName, const std::string& defSrcCodeName)
{
    auto pkgName = def.GetPackageName();
    if (auto genericDecl = def.GetGenericDecl()) {
        pkgName = genericDecl->GetPackageName();
    }
    return pkgName == packageName && def.GetSrcCodeIdentifier() == defSrcCodeName;
}

bool IsCoreAny(const CustomTypeDef& def)
{
    return CheckCustomTypeDefIsExpected(def, CORE_PACKAGE_NAME, ANY_NAME);
}

bool IsCoreObject(const CustomTypeDef& def)
{
    return CheckCustomTypeDefIsExpected(def, CORE_PACKAGE_NAME, OBJECT_NAME);
}

bool IsCoreOption(const CustomTypeDef& def)
{
    return CheckCustomTypeDefIsExpected(def, CORE_PACKAGE_NAME, STD_LIB_OPTION);
}

bool IsCoreFuture(const CustomTypeDef& def)
{
    return CheckCustomTypeDefIsExpected(def, CORE_PACKAGE_NAME, STD_LIB_FUTURE);
}

bool IsClosureConversionEnvClass(const ClassDef& def)
{
    return def.Get<IsAutoEnvClass>();
}

bool IsCapturedClass(const ClassDef& def)
{
    return def.Get<IsCapturedClassInCC>();
}

Func* GetTopLevelFunc(const Value& value)
{
    if (value.IsParameter()) {
        return StaticCast<const Parameter&>(value).GetTopLevelFunc();
    } else if (value.IsLocalVar()) {
        return StaticCast<const LocalVar&>(value).GetTopLevelFunc();
    } else if (value.IsBlock()) {
        return StaticCast<const Block&>(value).GetTopLevelFunc();
    } else if (value.IsBlockGroup()) {
        return StaticCast<const BlockGroup&>(value).GetTopLevelFunc();
    }
    CJC_ABORT();
    return nullptr;
}

void GetVisiableGenericTypes(const FuncBase& value, std::vector<GenericType*>& result)
{
    auto curGenericTypeParams = value.GetGenericTypeParams();
    result.insert(result.begin(), curGenericTypeParams.begin(), curGenericTypeParams.end());
    auto parentDef = value.GetParentCustomTypeDef();
    if (parentDef != nullptr) {
        auto outerGenericTypeParams = parentDef->GetGenericTypeParams();
        result.insert(result.begin(), outerGenericTypeParams.begin(), outerGenericTypeParams.end());
    }
}

void GetVisiableGenericTypes(const LocalVar& value, std::vector<GenericType*>& result)
{
    auto lambda = DynamicCast<Lambda*>(value.GetExpr());
    // lambda's return value will see generic type declared in lambda
    if (lambda != nullptr) {
        auto genericTypeParams = lambda->GetGenericTypeParams();
        result.insert(result.begin(), genericTypeParams.begin(), genericTypeParams.end());
    }
    auto curPos = value.GetExpr()->GetParentBlockGroup();
    CJC_NULLPTR_CHECK(curPos);
    while (curPos->GetOwnerFunc() == nullptr) {
        auto ownedExpr = curPos->GetOwnerExpression();
        if (auto lam = DynamicCast<Lambda>(ownedExpr)) {
            auto genericTypeParams = lam->GetGenericTypeParams();
            result.insert(result.begin(), genericTypeParams.begin(), genericTypeParams.end());
            curPos = lam->GetParentBlockGroup();
        } else {
            curPos = ownedExpr->GetParentBlockGroup();
        }
    }
    CJC_NULLPTR_CHECK(curPos);
    auto ownFunc = curPos->GetOwnerFunc();
    CJC_NULLPTR_CHECK(ownFunc);
    GetVisiableGenericTypes(*ownFunc, result);
}

void GetVisiableGenericTypes(const Parameter& value, std::vector<GenericType*>& result)
{
    if (value.GetTopLevelFunc() != nullptr) {
        GetVisiableGenericTypes(*value.GetTopLevelFunc(), result);
    } else {
        CJC_NULLPTR_CHECK(value.GetOwnerLambda());
        GetVisiableGenericTypes(*value.GetOwnerLambda()->GetResult(), result);
    }
}

void GetVisiableGenericTypes(const BlockGroup& value, std::vector<GenericType*>& result)
{
    if (value.GetOwnerFunc() != nullptr) {
        GetVisiableGenericTypes(*value.GetOwnerFunc(), result);
    } else {
        auto parentLambda = StaticCast<Lambda*>(value.GetOwnerExpression());
        GetVisiableGenericTypes(*parentLambda->GetResult(), result);
    }
}

void GetVisiableGenericTypes(const Block& value, std::vector<GenericType*>& result)
{
    GetVisiableGenericTypes(*value.GetParentBlockGroup(), result);
}

std::vector<GenericType*> GetVisiableGenericTypes(const Value& value)
{
    std::vector<GenericType*> result;
    if (value.IsFunc()) {
        GetVisiableGenericTypes(VirtualCast<const FuncBase&>(value), result);
    } else if (value.IsBlock()) {
        GetVisiableGenericTypes(StaticCast<const Block&>(value), result);
    } else if (value.IsBlockGroup()) {
        GetVisiableGenericTypes(StaticCast<const BlockGroup&>(value), result);
    } else if (value.IsParameter()) {
        GetVisiableGenericTypes(StaticCast<const Parameter&>(value), result);
    } else if (value.IsLocalVar()) {
        GetVisiableGenericTypes(StaticCast<const LocalVar&>(value), result);
    }
    return result;
}

const std::vector<Parameter*>& GetFuncParams(const BlockGroup& funcBody)
{
    if (auto func = funcBody.GetOwnerFunc()) {
        return func->GetParams();
    } else {
        auto lambda = StaticCast<Lambda*>(funcBody.GetOwnerExpression());
        return lambda->GetParams();
    }
}

LocalVar* GetReturnValue(const BlockGroup& funcBody)
{
    if (auto func = funcBody.GetOwnerFunc()) {
        return func->GetReturnValue();
    } else {
        auto lambda = StaticCast<Lambda*>(funcBody.GetOwnerExpression());
        return lambda->GetReturnValue();
    }
}

FuncType* GetFuncType(const BlockGroup& funcBody)
{
    if (auto func = funcBody.GetOwnerFunc()) {
        return func->GetFuncType();
    } else {
        auto lambda = StaticCast<Lambda*>(funcBody.GetOwnerExpression());
        return lambda->GetFuncType();
    }
}

bool IsStructOrExtendMethod(const Value& value)
{
    auto func = DynamicCast<const FuncBase*>(&value);
    if (func == nullptr) {
        return false;
    }
    auto declaredParent = func->GetParentCustomTypeDef();
    if (declaredParent == nullptr) {
        return false;
    }
    if (declaredParent->GetCustomKind() == CustomDefKind::TYPE_STRUCT) {
        return true;
    } else if (auto extendDef = DynamicCast<ExtendDef*>(declaredParent); extendDef) {
        if (extendDef->GetExtendedType() != nullptr) {
            return extendDef->GetExtendedType()->IsStruct();
        }
    }
    return false;
}

bool IsConstructor(const Value& value)
{
    if (auto func = DynamicCast<FuncBase>(&value)) {
        return func->IsConstructor();
    }
    return false;
}

Value* GetCastOriginalTarget(const Expression& expr)
{
    if (expr.GetExprKind() == ExprKind::TYPECAST) {
        return StaticCast<TypeCast*>(&expr)->GetSourceValue();
    }
    if (expr.GetExprKind() == ExprKind::UNBOX_TO_REF) {
        // from struct& -> class&
        return StaticCast<UnBoxToRef*>(&expr)->GetSourceValue();
    }
    if (expr.GetExprKind() == ExprKind::TRANSFORM_TO_GENERIC) {
        return StaticCast<TransformToGeneric*>(&expr)->GetSourceValue();
    }
    if (expr.GetExprKind() == ExprKind::TRANSFORM_TO_CONCRETE) {
        return StaticCast<TransformToConcrete*>(&expr)->GetSourceValue();
    }
    if (expr.GetExprKind() == ExprKind::BOX) {
        return StaticCast<Box*>(&expr)->GetSourceValue();
    }
    return nullptr;
}

bool IsInstanceVarInit(const Value& value)
{
    if (auto func = DynamicCast<FuncBase>(&value)) {
        return func->IsInstanceVarInit();
    }
    return false;
}

std::vector<ClassType*> GetSuperTypesRecusively(Type& subType, CHIRBuilder& builder)
{
    std::vector<ClassType*> result;
    if (auto customType = DynamicCast<CustomType*>(&subType)) {
        result = customType->GetSuperTypesRecusively(builder);
    } else if (auto builtinType = DynamicCast<BuiltinType*>(&subType)) {
        result = builtinType->GetSuperTypesRecusively(builder);
    } else if (auto genericType = DynamicCast<GenericType*>(&subType)) {
        for (auto upperBound : genericType->GetUpperBounds()) {
            auto classType = StaticCast<ClassType*>(upperBound->StripAllRefs());
            result.emplace_back(classType);
            auto tempParents = GetSuperTypesRecusively(*classType, builder);
            for (auto p : tempParents) {
                if (std::find(result.begin(), result.end(), p) == result.end()) {
                    result.emplace_back(p);
                }
            }
        }
    } else {
        CJC_ABORT();
    }
    return result;
}

static bool MeetAutoEnvBase(const Type& subType, const Type& superType)
{
    return subType.IsAutoEnvInstBase() && superType.IsAutoEnvGenericBase() &&
        Cangjie::StaticCast<const ClassType&>(subType).GetClassDef()->GetSuperClassTy() == &superType;
}

bool ParamTypeIsEquivalent(const Type& paramType, const Type& argType)
{
    if (&paramType == &argType) {
        return true;
    }
    if (auto g = DynamicCast<const GenericType*>(&paramType); g && g->orphanFlag) {
        auto upperBounds = g->GetUpperBounds();
        CJC_ASSERT(upperBounds.size() == 1);
        return upperBounds[0] == &argType;
    }
    if (auto g = DynamicCast<const GenericType*>(&argType); g && g->orphanFlag) {
        auto upperBounds = g->GetUpperBounds();
        CJC_ASSERT(upperBounds.size() == 1);
        return upperBounds[0] == &paramType;
    }
    return MeetAutoEnvBase(*argType.StripAllRefs(), *paramType.StripAllRefs());
}

ClassType* GetInstParentType(const std::vector<ClassType*>& candidate,
    const FuncBase& callee, const std::vector<Value*>& args, CHIRBuilder& builder)
{
    CJC_ASSERT(!candidate.empty());
    if (candidate.size() == 1) {
        return candidate[0];
    }
    std::unordered_set<ClassDef*> tempDef;
    for (auto p : candidate) {
        tempDef.emplace(p->GetClassDef());
    }
    CJC_ASSERT(tempDef.size() == 1);

    size_t offset = callee.TestAttr(Attribute::STATIC) ? 0 : 1;
    std::vector<Type*> argTypes;
    for (size_t i = offset; i < args.size(); ++i) {
        argTypes.emplace_back(args[i]->GetType());
    }
    auto paramTypes = callee.GetFuncType()->GetParamTypes();
    if (!callee.TestAttr(Attribute::STATIC)) {
        paramTypes.erase(paramTypes.begin());
    }
    CJC_ASSERT(argTypes.size() == paramTypes.size());
    for (auto p : candidate) {
        auto [ret, instMap] = p->GetClassDef()->GetType()->CalculateGenericTyMapping(*p);
        CJC_ASSERT(ret);
        bool argTypeMatched = true;
        for (size_t i = 0; i < paramTypes.size(); ++i) {
            auto instType = ReplaceRawGenericArgType(*paramTypes[i], instMap, builder);
            if (!ParamTypeIsEquivalent(*instType, *argTypes[i])) {
                argTypeMatched = false;
                break;
            }
        }
        if (argTypeMatched) {
            return p;
        }
    }
    CJC_ABORT();
    return nullptr;
}

Type* GetInstParentCustomTyOfCallee(
    const Value& value, const std::vector<Value*>& args, const Type* thisType, CHIRBuilder& builder)
{
    auto callee = DynamicCast<FuncBase*>(&value);
    if (callee == nullptr) {
        return nullptr;  // call a value
    }
    if (!callee->IsMemberFunc()) {
        return nullptr; // global function
    }

    auto calleeParentDef = callee->GetParentCustomTypeDef();
    CJC_NULLPTR_CHECK(calleeParentDef);
    if (thisType == nullptr) {
        return calleeParentDef->GetType(); // call a member method with implicit `this`
    }
    
    if (!calleeParentDef->GetType()->IsBuiltinType() && calleeParentDef->IsExtend()) {
        calleeParentDef = StaticCast<CustomType*>(calleeParentDef->GetType())->GetCustomTypeDef();
    }
    auto derefThisType = thisType->StripAllRefs();
    // `thisType` is sub class, `calleeParentDef` is parent class, a CustomType can't inherit BuiltinType
    CJC_ASSERT(!(derefThisType->IsCustomType() && calleeParentDef->GetType()->IsBuiltinType()));
    auto typeAndDefIsEquivalent = [](const CustomType& type, const CustomTypeDef& def) {
        /**
         * 1. def equals def, maybe `def` is generic, `type` is instantiated
         * 2. type equals type, maybe `def` is instantiated
         */
        return type.GetCustomTypeDef() == &def || &type == def.GetType();
    };
    if (auto customType = DynamicCast<CustomType*>(derefThisType); customType &&
        typeAndDefIsEquivalent(*customType, *calleeParentDef)) {
        /**
         * 1. a function declared in generic custom type def, is called by instantiated custom type
         *  class A<T> {
         *      func foo() {}
         *  }
         *  A<Bool>.foo()  // `thisType` is A<Bool>, `foo`'s parent def is A<T>
         *
         * 2. a function declared in instantiated custom type def, is called by instantiated custom type
         *  class A<T> {
         *      func foo() {}
         *  }
         *  class A_Bool {  // A<T> is instantiated by Bool
         *      func foo_Bool() {}
         *  }
         *  A<Bool>.foo_Bool()  // `thisType` is A<Bool>, `foo`'s parent def is A_Bool
         */
        return derefThisType;
    } else if (auto builtinType = DynamicCast<BuiltinType*>(derefThisType);
        builtinType && builtinType->IsSameTypeKind(*calleeParentDef->GetType())) {
        // we should compare type pointer, but CPointer<T> is generic type, so we have to compare type kind
        return derefThisType;
    } else {
        /**
         * a function declared in parent def, but called by sub type, then we need to compute instantiated parent type
         * 1. a function declared in generic custom type def, is called by instantiated sub custom type
         *  interface A<T> {
         *      func foo() {}
         *  }
         *  class B<T> <: A<T> {}
         *  B<Bool>.foo()  // `thisType` is B<Bool>, `foo`'s parent def is A<T>, then parent type is A<Bool>
         *
         * 2. a function declared in instantiated custom type def, is called by instantiated sub custom type
         *  interface A<T> {
         *      func foo() {}
         *  }
         *  class B<T> <: A<T> {}
         *  interface A_Bool {
         *      func foo_Bool() {}
         *  }
         *  class B_Bool <: A_Bool {}
         *  B<Bool>.foo_Bool()  // `thisType` is B<Bool>, `foo`'s parent def is A_Bool, then parent type is A<Bool>
         *
         * 3. sub class inherit the same parent class more than once with different instantiated type
         *  interface A<T> {
         *      func foo(a: T) {}
         *  }
         *  class B <: A<Bool> & A<Int64> {}
         *  B().foo(1)  // `thisType` is B<Bool>, `foo`'s parent def is A<T>, then parent type is A<Int64>
         */
        auto parentTypes = GetSuperTypesRecusively(*derefThisType, builder);
        std::vector<ClassType*> matchedTypes;
        for (auto pType : parentTypes) {
            if (typeAndDefIsEquivalent(*pType, *calleeParentDef)) {
                matchedTypes.emplace_back(pType);
            }
        }
        return GetInstParentType(matchedTypes, *callee, args, builder);
    }
}

Type* GetInstParentCustomTypeForApplyCallee(const Apply& expr, CHIRBuilder& builder)
{
    return GetInstParentCustomTyOfCallee(*expr.GetCallee(), expr.GetArgs(), expr.GetThisType(), builder);
}

Type* GetInstParentCustomTypeForAweCallee(const ApplyWithException& expr, CHIRBuilder& builder)
{
    return GetInstParentCustomTyOfCallee(*expr.GetCallee(), expr.GetArgs(), expr.GetThisType(), builder);
}

std::vector<VTableSearchRes> GetFuncIndexInVTable(
    Type& root, const FuncCallType& funcCallType, bool isStatic, CHIRBuilder& builder)
{
    std::vector<VTableSearchRes> result;
    if (auto genericTy = DynamicCast<GenericType*>(&root)) {
        auto& upperBounds = genericTy->GetUpperBounds();
        CJC_ASSERT(!upperBounds.empty());
        for (auto upperBound : upperBounds) {
            ClassType* upperClassType = StaticCast<ClassType*>(StaticCast<RefType*>(upperBound)->GetBaseType());
            result = GetFuncIndexInVTable(*upperClassType, funcCallType, isStatic, builder);
            if (!result.empty()) {
                break;
            }
        }
    } else if (auto classTy = DynamicCast<CustomType*>(&root)) {
        result = classTy->GetFuncIndexInVTable(funcCallType, isStatic, builder);
    } else {
        std::unordered_map<const GenericType*, Type*> empty;
        auto extendDefs = root.GetExtends(&builder);
        CJC_ASSERT(!extendDefs.empty());
        for (auto ex : extendDefs) {
            result = ex->GetFuncIndexInVTable(funcCallType, isStatic, empty, builder);
            if (!result.empty()) {
                break;
            }
        }
    }
    return result;
}

BuiltinType* GetBuiltinTypeWithVTable(BuiltinType& type, CHIRBuilder& builder)
{
    if (type.IsCPointer()) {
        return builder.GetType<CPointerType>(builder.GetUnitTy());
    }
    return &type;
}


Type* GetInstParentType(Type& instSubType, Type& genericParentType, CHIRBuilder& builder)
{
    if (!genericParentType.IsGenericRelated()) {
        return &genericParentType;
    }
    if (&instSubType == &genericParentType) {
        return &genericParentType;
    }
    auto parentDerefType = genericParentType.StripAllRefs();
    auto [res, replaceTable] = parentDerefType->CalculateGenericTyMapping(instSubType);
    if (!res) {
        for (auto p : GetSuperTypesRecusively(instSubType, builder)) {
            std::tie(res, replaceTable) = parentDerefType->CalculateGenericTyMapping(*p);
            if (res) {
                break;
            }
        }
    }
    CJC_ASSERT(res);
    return ReplaceRawGenericArgType(genericParentType, replaceTable, builder);
}
} // namespace Cangjie::CHIR
