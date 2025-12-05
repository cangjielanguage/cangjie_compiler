// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Checker/VarInitCheck.h"

#include "cangjie/CHIR/Analysis/Engine.h"
#include "cangjie/CHIR/Analysis/MaybeInitAnalysis.h"
#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/Utils/TaskQueue.h"

using namespace Cangjie::CHIR;

namespace {
bool HasFuncCallWithNothingRetVal(const Block& block)
{
    for (auto expr : block.GetNonTerminatorExpressions()) {
        if (!Cangjie::Is<FuncCall>(expr)) {
            continue;
        }
        auto type = expr->GetResultType()->StripAllRefs();
        if (type->IsNothing()) {
            return true;
        }
    }
    return false;
}
}
VarInitCheck::VarInitCheck(DiagAdapter* diag) : diag(diag)
{
}

void VarInitCheck::RunOnPackage(const Package* package, size_t threadNum)
{
    std::vector<Func*> funcs;
    for (auto func : package->GetGlobalFuncs()) {
        if (func->GetSrcCodeIdentifier().find("__ad_") == std::string::npos &&
            func->GetSrcCodeIdentifier() != STATIC_INIT_FUNC &&
            func->GetIdentifier().find("$Mocked") == std::string::npos &&
            func->GetFuncKind() != FuncKind::INSTANCEVAR_INIT && // only cjnative for cjmp
            func->Get<SkipCheck>() != SkipKind::SKIP_VIC) {
            funcs.emplace_back(func);
        }
    }
    if (funcs.empty()) {
        return;
    }
    if (threadNum == 1) {
        for (auto func : funcs) {
            RunOnFunc(func);
        }
    } else {
        Utils::TaskQueue taskQueue(threadNum);
        for (auto func : funcs) {
            taskQueue.AddTask<void>([this, func]() { return RunOnFunc(func); });
        }
        taskQueue.RunAndWaitForAllTasksCompleted();
    }
}

void VarInitCheck::RunOnFunc(const Func* func)
{
    std::vector<MemberVarInfo> members;
    auto ctorInitInfo = std::make_unique<ConstructorInitInfo>();
    if (func->IsConstructor()) {
        auto customTypeDef = func->GetOuterDeclaredOrExtendedDef();
        CJC_NULLPTR_CHECK(customTypeDef);
        members = customTypeDef->GetAllInstanceVars();
        ctorInitInfo->thisCustomDef = customTypeDef;
        if (customTypeDef->GetCustomKind() == CustomDefKind::TYPE_CLASS) {
            auto classDef = StaticCast<ClassDef*>(customTypeDef);
            ctorInitInfo->localMemberNums = classDef->GetDirectInstanceVarNum();
            // We do a check here as class-Object does not have a super class.
            if (auto superClassDef = classDef->GetSuperClassDef(); superClassDef) {
                ctorInitInfo->superClassDef = superClassDef;
                ctorInitInfo->superMemberNums = members.size() - ctorInitInfo->localMemberNums;
            }
        } else {
            ctorInitInfo->localMemberNums = members.size();
        }
    }

    UseBeforeInitCheck(func, ctorInitInfo.get(), members);
    ReassignInitedLetVarCheck(func, ctorInitInfo.get(), members);
}

template <typename TApply>
void VarInitCheck::CheckMemberFuncCall(const MaybeUninitDomain& state, const Func& initFunc, const TApply& apply) const
{
    // Calling any member function in an initializer before all the member of this class/struct has
    // been initialised is illegal.
    auto callee = apply.GetCallee();
    if (!callee->IsFuncWithBody()) {
        return;
    }
    auto calleeFunc = VirtualCast<Func*>(callee);
    auto thisArg = initFunc.GetParam(0);
    // check the callee is a member function and if all the member has been initialised
    // note: calling other initialiser of this class/struct is ok
    if (calleeFunc->GetOuterDeclaredOrExtendedDef() == initFunc.GetOuterDeclaredOrExtendedDef() &&
        !calleeFunc->IsConstructor() && !calleeFunc->TestAttr(Attribute::STATIC) &&
        !state.GetMaybeUninitedLocalMembers().empty() && !calleeFunc->IsInstanceVarInit()) {
        if (auto arg = apply.GetArgs()[0]; arg == thisArg) {
            // class member function
            // func init(%0 : CA&)
            // ...
            // %1 = Apply(@memberFunc, %0)  illegal if not all the member has been inited currently
            // ...
            RaiseIllegalMemberFunCallError(&apply, calleeFunc);
        } else if (arg->IsLocalVar()) {
            // struct member function
            // func init(%0 : SA&)
            // ...
            // %1 : SA = Load(%0)
            // %2 = Apply(@memberFunc, %1)  illegal if not all the member has been inited currently
            // ...
            auto argExpr = static_cast<LocalVar*>(arg)->GetExpr();
            if (argExpr->GetExprKind() == ExprKind::LOAD && static_cast<Load*>(argExpr)->GetLocation() == thisArg) {
                RaiseIllegalMemberFunCallError(&apply, calleeFunc);
            }
        }
    }
}

// Update only cjnative for cjmp
void VarInitCheck::UseBeforeInitCheck(
    const Func* func, const ConstructorInitInfo* ctorInitInfo, const std::vector<MemberVarInfo>& members)
{
    bool isCommonFunctionWithoutBody = func->TestAttr(Attribute::SKIP_ANALYSIS);
    if (isCommonFunctionWithoutBody) {
        return; // Nothing to visit
    }
    auto analysis = std::make_unique<MaybeUninitAnalysis>(func, ctorInitInfo);
    auto engine = Engine<MaybeUninitDomain>(func, std::move(analysis));
    auto result = engine.IterateToFixpoint();
    CJC_NULLPTR_CHECK(result);

    std::vector<const Func*> callStack{func};

    const auto actionBeforeVisitExpr = [this, &callStack, &members](
                                           const MaybeUninitDomain& state, Expression* expr, size_t) {
        if (expr->GetExprKind() == ExprKind::APPLY) {
            if (auto varInitFunc = TryGetInstanceVarInitFromApply(*expr)) {
                callStack.push_back(varInitFunc);
            }
        }
        auto initialFunc = callStack[0];
        auto curFunc = callStack.back();
        if (expr->GetExprKind() == ExprKind::LOAD) {
            if (CheckLoadToUninitedAllocation(state, *StaticCast<const Load*>(expr))) {
                return;
            }
        }
        if (expr->GetExprKind() == ExprKind::GET_ELEMENT_REF) {
            if (CheckGetElementRefToUninitedAllocation(state, *StaticCast<const GetElementRef*>(expr))) {
                return;
            }
        }
        if (!initialFunc->IsConstructor()) {
            return;
        }
        if (expr->GetExprKind() == ExprKind::LOAD) {
            CheckLoadToUninitedCustomDefMember(state, curFunc, StaticCast<const Load*>(expr), members);
        } else if (expr->GetExprKind() == ExprKind::STORE_ELEMENT_REF) {
            CheckStoreToUninitedCustomDefMember(state, curFunc, StaticCast<const StoreElementRef*>(expr), members);
        } else if (expr->GetExprKind() == ExprKind::APPLY) {
            CheckMemberFuncCall(state, *curFunc, *StaticCast<Apply*>(expr));
        }
    };

    const auto actionAfterVisitExpr = [&callStack](const MaybeUninitDomain&, Expression* expr, size_t) {
        if (expr->GetExprKind() == ExprKind::APPLY) {
            if (TryGetInstanceVarInitFromApply(*expr)) {
                callStack.pop_back();
            }
        }
    };

    const auto actionOnTerminator = [this, func, &members](const MaybeUninitDomain& state, const Terminator* terminator,
                                        std::optional<Block*>) {
        if (!func->IsConstructor()) {
            return;
        }
        // If the current function is a initialiesr of a class/struct. We need to check if all the member
        // variables has been initialised before exiting the initialiser.
        // In CHIR, we will this when the currently processed expression is an Exit. We does not check
        // RaiseException terminators as that means an exception has occurred.
        // However, there are still some unusual Exit terminators that we won't check. These are:
        // a) Exit generated by For-in translator;
        //    During the translation for for-in nodes, there are some compiler-added Exit terminators.
        //    Checking these Exits can lead to false positives as these are `fake` Exits.
        // b) Exit in a local func
        //    An initialiser can not be a local func. We only need to check the exit point of the initialiser.
        // c) Exit due to Nothing expressions
        //    For example,
        //      CJ:
        //          func foo(): Nothing { throw Exception() }
        //          class CA { var a: Int32; init() { foo() } }     => no vic error, as as exception must be thrown
        //    ====================================================
        //      CHIR:
        //          func @CA_init() { ...; %0: Nothing = Apply(@foo); Exit() }  this Exit doesn't need to be checked
        //
        //    In a CHIR function, only function calls with a return type of Nothing can produce a value of Nothing
        //    type (does not include Constant(Null)). And the callee must exit the function by throwing an exception.
        //    Thus, we don't need to check Exit due to Nothing expressions.
        if (terminator->GetExprKind() == ExprKind::EXIT) {
            auto parent = terminator->GetParentBlock();
            bool isForInGeneratedExit = terminator->Get<SkipCheck>() == SkipKind::SKIP_FORIN_EXIT;
            bool isExitForLocalFunc = parent->GetParentBlockGroup() != func->GetBody();
            // if a func call's return type is Nothing, it means this func call is related with Exception
            if (isForInGeneratedExit || isExitForLocalFunc || HasFuncCallWithNothingRetVal(*parent)) {
                return;
            }
            if (auto uninitedMembers = state.GetMaybeUninitedLocalMembers(); !uninitedMembers.empty()) {
                RaiseUninitedDefMemberError(state, func, members, uninitedMembers);
            }
        } else if (terminator->GetExprKind() == ExprKind::APPLY_WITH_EXCEPTION) {
            CheckMemberFuncCall(state, *func, *StaticCast<ApplyWithException*>(terminator));
        }
    };

    result->VisitWith(actionBeforeVisitExpr, actionAfterVisitExpr, actionOnTerminator);
}

bool VarInitCheck::CheckLoadToUninitedAllocation(const MaybeUninitDomain& state, const Load& load) const
{
    auto targetVal = load.GetLocation();
    if (state.IsMaybeUninitedAllocation(targetVal).value_or(false)) {
        auto identifier = targetVal->GetSrcCodeIdentifier();
        auto builder = diag->DiagnoseRefactor(
            DiagKindRefactor::chir_used_before_initialization, ToPosition(load.GetDebugLocation()), identifier);
        AddMaybeInitedPosNote(builder, identifier, state.GetMaybeInitedPos(targetVal));
        return true;
    }
    return false;
}

bool VarInitCheck::CheckGetElementRefToUninitedAllocation(
    const MaybeUninitDomain& state, const GetElementRef& getElementRef) const
{
    auto targetVal = getElementRef.GetLocation();
    auto oriType = targetVal->GetType()->StripAllRefs();
    if (!oriType->IsStruct() && !oriType->IsEnum()) {
        // class need load before use, do not check it, only check struct and enum
        return false;
    }
    if (state.IsMaybeUninitedAllocation(targetVal).value_or(false)) {
        auto identifier = targetVal->GetSrcCodeIdentifier();
        auto builder = diag->DiagnoseRefactor(
            DiagKindRefactor::chir_used_before_initialization, ToPosition(getElementRef.GetDebugLocation()),
            identifier);
        AddMaybeInitedPosNote(builder, identifier, state.GetMaybeInitedPos(targetVal));
        return true;
    }
    return false;
}

namespace {
std::optional<size_t> IsGetElemRefViaDefMember(const Expression& expr, const Parameter& thisArg)
{
    if (expr.GetExprKind() != ExprKind::GET_ELEMENT_REF) {
        return std::nullopt;
    }
    auto getElemRef = Cangjie::StaticCast<const GetElementRef*>(&expr);
    auto location = getElemRef->GetLocation();
    if (location == &thisArg) {
        return getElemRef->GetPath()[0];
    } else if (location->IsLocalVar()) {
        auto locExpr = Cangjie::StaticCast<const LocalVar*>(location)->GetExpr();
        return IsGetElemRefViaDefMember(*locExpr, thisArg);
    }
    return std::nullopt;
}
} // namespace

void VarInitCheck::CheckLoadToUninitedCustomDefMember(
    const MaybeUninitDomain& state, const Func* func, const Load* load, const std::vector<MemberVarInfo>& members) const
{
    auto targetVal = load->GetLocation();
    if (targetVal->IsLocalVar()) {
        auto expr = StaticCast<LocalVar*>(targetVal)->GetExpr();
        // Check if we are accessing an uninitialised class/struct member or a member of an uninitialised member.
        // For example:
        // Func @<init>(%ca: CA&)
        // ...
        // %0 = GetElementRef(%ca, 0) or GetElementRef(%ca, 0, 1)
        // %1 = Load(%0)
        // ...
        // illegal if %ca.0 has not been initialised
        auto index = IsGetElemRefViaDefMember(*expr, *func->GetParam(0));
        if (index.has_value()) {
            return CheckUninitedDefMember(state, load, members, index.value());
        }
    }
}

namespace {
std::optional<size_t> IsStoreViaDefMember(
    const StoreElementRef& store, const Parameter& thisArg, bool shouldBeNested = false)
{
    auto location = store.GetLocation();
    // The parameter `shouldBeNested` means if the target of the xxxGetElementRef must be
    // a nested class/struct member.
    if (location == &thisArg && (!shouldBeNested || store.GetPath().size() > 1)) {
        return store.GetPath()[0];
    } else if (location->IsLocalVar()) {
        auto expr = Cangjie::StaticCast<LocalVar*>(location)->GetExpr();
        return IsGetElemRefViaDefMember(*expr, thisArg);
    }
    return std::nullopt;
}
} // namespace

void VarInitCheck::CheckStoreToUninitedCustomDefMember(const MaybeUninitDomain& state, const Func* func,
    const StoreElementRef* store, const std::vector<MemberVarInfo>& members) const
{
    // a)
    // Check if we are assigning values to a nested member of an unitialised class/struct member.
    // note: assigning values to an unitialised member is totally fine.
    // For example:
    // Func @<init>(%ca: CA&)
    // ...
    // %0 = StoreElementRef(%ca, 0, 1)      illegal if %ca.0 has not been initialised
    // ...
    // %1 = GetElementRef(%ca, 1)
    // %2 = StoreGetElementRef(xx, %1, 0)   illegal if %ca.1 has not been initialised
    // ...
    if (auto index = IsStoreViaDefMember(*store, *func->GetParam(0), /* shouldBeNested = */ true)) {
        return CheckUninitedDefMember(state, store, members, index.value());
    }

    // b)
    // Check if we are assigning values to an uninitialised member of the super class, or a nested
    // member of the uninitialised member.
    // For example:
    // Func @<init>(%ca: CA&)
    // ...
    // %0 = StoreElementRef(%ca, 0) or StoreElementRef(%ca, 0, 1)
    // ...
    // illegal if %ca.0 is an uninitialised super member when super() has not been called.
    if (!func->IsClassMethod()) {
        return;
    }
    if (auto index = IsStoreViaDefMember(*store, *func->GetParam(0))) {
        return CheckUninitedDefMember(state, store, members, index.value(), /* onlyCheckSuper = */ true);
    }
}

void VarInitCheck::RaiseUninitedDefMemberError(const MaybeUninitDomain& state, const Func* func,
    const std::vector<MemberVarInfo>& members, const std::vector<size_t>& uninitedMemberIdx) const
{
    // Skip report error when uninitedMember are all common member for CJMP.
    if (std::all_of(uninitedMemberIdx.begin(), uninitedMemberIdx.end(),
        [&members](size_t idx) {
            auto member = members[idx];
            // Skip COMMON which is just added(not deserialized ones), they may not yet be initialized and it's ok.
            return member.TestAttr(Attribute::COMMON) && !member.TestAttr(Attribute::DESERIALIZED);
        })) {
        return;
    }
    auto builder =
        diag->DiagnoseRefactor(DiagKindRefactor::chir_class_uninitialized_field, ToPosition(func->GetDebugLocation()));
    for (auto idx : uninitedMemberIdx) {
        auto identifier = members[idx].name;
        std::string msg = "in line " + std::to_string(members[idx].loc.GetBeginPos().line) + ", variable '" +
            identifier + "' need to be initialized";
        builder.AddNote(msg);
        AddMaybeInitedPosNote(builder, identifier, state.GetMaybeInitedPos(idx));
    }
}

void VarInitCheck::CheckUninitedDefMember(const MaybeUninitDomain& state, const Expression* expr,
    const std::vector<MemberVarInfo>& members, size_t index, bool onlyCheckSuper) const
{
    auto checkUninitedRes = state.IsMaybeUninitedMember(index);
    if (checkUninitedRes == MaybeUninitDomain::UninitedMemberKind::SUPER_MEMBER) {
        diag->DiagnoseRefactor(DiagKindRefactor::chir_illegal_usage_of_super_member,
            ToPosition(expr->GetDebugLocation()), members[index].name);
    } else if (checkUninitedRes == MaybeUninitDomain::UninitedMemberKind::LOCAL_MEMBER && !onlyCheckSuper) {
        auto identifier = members[index].name;
        auto builder = diag->DiagnoseRefactor(
            DiagKindRefactor::chir_used_before_initialization, ToPosition(expr->GetDebugLocation()), identifier);
        AddMaybeInitedPosNote(builder, identifier, state.GetMaybeInitedPos(index));
    }
}

void VarInitCheck::AddMaybeInitedPosNote(
    DiagnosticBuilder& builder, const std::string& identifier, const std::set<unsigned>& maybeInitedPos) const
{
    if (maybeInitedPos.empty()) {
        return;
    }
    std::stringstream ss;
    ss << "we detected variable '" << identifier << "' is assigned in line ";
    for (auto pos : maybeInitedPos) {
        ss << pos << ", ";
    }
    ss << "but in runtime, these codes might not be executed.";
    builder.AddNote(ss.str());
}

void VarInitCheck::RaiseIllegalMemberFunCallError(const Expression* apply, const Func* memberFunc) const
{
    auto identifier = memberFunc->GetSrcCodeIdentifier();
    auto calleeFuncKind = memberFunc->GetFuncKind();
    if (calleeFuncKind == FuncKind::GETTER || calleeFuncKind == FuncKind::SETTER) {
        constexpr size_t getterNameLength{4}; // length of "$get" or "$set"
        identifier = identifier.substr(1, identifier.length() - getterNameLength);
    }
    diag->DiagnoseRefactor(
        DiagKindRefactor::chir_illegal_usage_of_member, ToPosition(apply->GetDebugLocation()), identifier);
}

void VarInitCheck::ReassignInitedLetVarCheck(const Func* func, const ConstructorInitInfo* ctorInitInfo,
    const std::vector<MemberVarInfo>& members) const
{
    bool isCommonFunctionWithoutBody = func->TestAttr(Attribute::SKIP_ANALYSIS);
    if (isCommonFunctionWithoutBody) {
        return; // Nothing to visit
    }
    auto analysis = std::make_unique<MaybeInitAnalysis>(func, ctorInitInfo);
    auto engine = Engine<MaybeInitDomain>(func, std::move(analysis));
    auto result = engine.IterateToFixpoint();
    CJC_NULLPTR_CHECK(result);

    const auto actionBeforeVisitExpr = [this, func, &members](const MaybeInitDomain& state, Expression* expr, size_t) {
        if (expr->GetExprKind() == ExprKind::STORE) {
            auto store = StaticCast<const Store*>(expr);
            auto targetVal = store->GetLocation();
            // check reassign to let-defined local variables;
            if (!targetVal->IsLocalVar()) {
                return;
            }
            if (targetVal->TestAttr(Attribute::READONLY) && state.IsMaybeInitedAllocation(targetVal).value_or(false)) {
                diag->DiagnoseRefactor(DiagKindRefactor::chir_cannot_assign_initialized_let_variable,
                    ToPosition(expr->GetDebugLocation()));
            }
        } else if (expr->GetExprKind() == ExprKind::STORE_ELEMENT_REF) {
            CheckStoreToInitedCustomDefMember(state, func, StaticCast<const StoreElementRef*>(expr), members);
        }
    };
    const auto actionAfterVisitExpr = [](const MaybeInitDomain&, Expression*, size_t) {};
    const auto actionOnTerminator = [](const MaybeInitDomain&, Terminator*, std::optional<Block*>) {};

    result->VisitWith(actionBeforeVisitExpr, actionAfterVisitExpr, actionOnTerminator);
}
void VarInitCheck::CheckStoreToInitedCustomDefMember(const MaybeInitDomain& state, const Func* func,
    const StoreElementRef* store, const std::vector<MemberVarInfo>& members) const
{
    // If the function is an initialiser, we will check if it's a StoreElementRef to an unitialised member of the
    // class/struct. If it is, this StoreElementRef is the initialisation operation and legal. Otherwise, we will
    // raise an error.
    if (!func->IsConstructor()) {
        return;
    }
    auto& paths = store->GetPath();
    if (store->GetLocation() == func->GetParam(0) && paths.size() == 1) {
        if (members[paths[0]].TestAttr(Attribute::READONLY) &&
            state.IsMaybeInitedMember(paths[0]) != MaybeInitDomain::InitedMemberKind::NA) {
            // this `let-defined` member variable has already been initialised
            diag->DiagnoseRefactor(
                DiagKindRefactor::chir_cannot_assign_initialized_let_variable, ToPosition(store->GetDebugLocation()));
        }
        return;
    }
}
