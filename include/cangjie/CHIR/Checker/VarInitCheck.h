// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_CHECKER_VAR_INIT_CHECK_H
#define CANGJIE_CHIR_CHECKER_VAR_INIT_CHECK_H

#include "cangjie/CHIR/Analysis/MaybeInitAnalysis.h"
#include "cangjie/CHIR/Analysis/MaybeUninitAnalysis.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/DiagAdapter.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Package.h"

namespace Cangjie::CHIR {

class VarInitCheck {
public:
    explicit VarInitCheck(DiagAdapter* diag);

    void RunOnPackage(const Package* package, size_t threadNum);

    void RunOnFunc(const Func* func);

private:
    // ================================================================= //
    void UseBeforeInitCheck(const Func* func, const ConstructorInitInfo* ctorInitInfo,
        const std::vector<MemberVarInfo>& members);

    bool CheckLoadToUninitedAllocation(const MaybeUninitDomain& state, const Load& load) const;

    bool CheckGetElementRefToUninitedAllocation(
        const MaybeUninitDomain& state, const GetElementRef& getElementRef) const;

    void CheckLoadToUninitedCustomDefMember(const MaybeUninitDomain& state, const Func* func, const Load* load,
        const std::vector<MemberVarInfo>& members) const;

    void CheckStoreToUninitedCustomDefMember(const MaybeUninitDomain& state, const Func* func,
        const StoreElementRef* store, const std::vector<MemberVarInfo>& members) const;

    void AddMaybeInitedPosNote(
        DiagnosticBuilder& builder, const std::string& identifier, const std::set<unsigned>& maybeInitedPos) const;

    void CheckUninitedDefMember(const MaybeUninitDomain& state, const Expression* expr,
        const std::vector<MemberVarInfo>& members, size_t index, bool onlyCheckSuper = false) const;

    void RaiseUninitedDefMemberError(const MaybeUninitDomain& state, const Func* func,
        const std::vector<MemberVarInfo>& members, const std::vector<size_t>& uninitedMemberIdx) const;

    template <typename TApply>
    void CheckMemberFuncCall(const MaybeUninitDomain& state, const Func& initFunc, const TApply& apply) const;

    void RaiseIllegalMemberFunCallError(const Expression* apply, const Func* memberFunc) const;

    // ================================================================= //
    void ReassignInitedLetVarCheck(const Func* func, const ConstructorInitInfo* ctorInitInfo,
        const std::vector<MemberVarInfo>& members) const;

    void CheckStoreToInitedCustomDefMember(const MaybeInitDomain& state, const Func* func, const StoreElementRef* store,
        const std::vector<MemberVarInfo>& members) const;

private:
    DiagAdapter* diag;
};

} // namespace Cangjie::CHIR

#endif