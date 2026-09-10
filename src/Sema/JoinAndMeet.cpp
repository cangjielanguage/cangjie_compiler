// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements utility functions for JoinAndMeet.
 */

#include "JoinAndMeet.h"

#include "TypeCheckUtil.h"

#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

namespace {
enum class Uniformity {
    UNIFORMED,
    MIXED,
    ALL_IRRELEVANT
};

Uniformity CheckFuncUniformity(const std::set<DataTy>& tys)
{
    size_t paramCnt = 0;
    bool anyFuncTy = false;
    bool anyNonFuncTy = false;
    for (auto& ty : tys) {
        if (auto funcTy = DynamicCast<FuncTy*>(ty)) {
            size_t curCnt = funcTy->paramTys.size();
            if (!anyFuncTy) {
                paramCnt = curCnt;
            } else if (paramCnt != curCnt) {
                return Uniformity::MIXED;
            }
            anyFuncTy = true;
        } else {
            anyNonFuncTy = true;
        }
        if (anyFuncTy && anyNonFuncTy) {
            return Uniformity::MIXED;
        }
    }
    if (!anyFuncTy) {
        return Uniformity::ALL_IRRELEVANT;
    }
    return Uniformity::UNIFORMED;
}

Uniformity CheckTupleUniformity(const std::set<DataTy>& tys)
{
    size_t argCnt = 0;
    bool anyTupleTy = false;
    bool anyNonTupleTy = false;
    for (auto& ty : tys) {
        if (auto tupleTy = DynamicCast<TupleTy*>(ty)) {
            size_t curCnt = tupleTy->typeArgs.size();
            if (!anyTupleTy) {
                argCnt = curCnt;
            } else if (argCnt != curCnt) {
                return Uniformity::MIXED;
            }
            anyTupleTy = true;
        } else {
            anyNonTupleTy = true;
        }
        if (anyTupleTy && anyNonTupleTy) {
            return Uniformity::MIXED;
        }
    }
    if (!anyTupleTy) {
        return Uniformity::ALL_IRRELEVANT;
    }
    return Uniformity::UNIFORMED;
}
} // namespace

JoinAndMeet::ErrOrTy JoinAndMeet::Join(bool sprsErr)
{
    // Hot fix.
    if (!IsInputValid()) {
        return {ModalTy{TypeManager::GetInvalidTy()}};
    }
    auto jTy = BatchJoin(tySet);
    if (sprsErr || errMsg.empty()) {
        return {jTy};
    } else {
        return {errMsg};
    }
}

JoinAndMeet::ErrOrTy JoinAndMeet::JoinAsVisibleTy()
{
    // Hot fix.
    if (!IsInputValid()) {
        return {ModalTy{TypeManager::GetInvalidTy()}};
    }
    auto jTy = BatchJoin(tySet);
    this->isForcedToUserVisible = true;
    jTy = ToUserVisibleTy(jTy);
    AddFinalErrMsgs(jTy, true);
    if (errMsg.empty()) {
        return {jTy};
    } else {
        return {errMsg};
    }
}

ModalInfo JoinAndMeet::JoinMode(TypeManager& tyMgr, const std::set<ModalTy>& tyms)
{
    // Join the modals of all non-Copy types. IDEAL (a literal's pending modal awaiting
    // unification with the expected contextual modal) is transparent in a join: it does not
    // contribute its own (meaningless) mode, so a concrete modal from any sibling determines
    // the result. If every contributing modal is IDEAL (no concrete modal among non-Copy tys),
    // the result stays IDEAL (still pending) so it can be unified with the expected contextual
    // modal downstream; if there are no non-Copy contributors at all (empty or all-Copy), the
    // result defaults to ~local (NOT).
    bool hasConcrete = false;
    bool hasIdeal = false;
    ModalInfo m{};
    for (auto ty : tyms) {
        if (tyMgr.ImplementsCopyInterface(ty.Ty())) {
            continue;
        }
        if (ty.Mode().local == Mode::IDEAL) {
            hasIdeal = true;
            continue;
        }
        if (!hasConcrete) {
            m = ty.Mode();
            hasConcrete = true;
        } else {
            m = m | ty.Mode();
        }
    }
    if (hasConcrete) {
        return m;
    }
    return hasIdeal ? ModalInfo{Mode::IDEAL} : ModalInfo{};
}

ModalTy JoinAndMeet::BatchJoin(const std::set<ModalTy>& tyms)
{
    std::set<DataTy> tys;
    for (auto ty : tyms) {
        tys.insert(ty.Ty());
    }
    auto res = BatchJoin(tys);
    return {res, JoinMode(tyMgr, tyms)};
}

ModalTy JoinAndMeet::BatchMeet(const std::set<ModalTy>& tyms)
{
    std::set<DataTy> tys;
    for (auto ty : tyms) {
        tys.insert(ty.Ty());
    }
    auto res = BatchMeet(tys);
    // Meet the modals of all non-Copy types. IDEAL (a literal's pending modal) is transparent
    // in a meet: it does not constrain, so it does not cause a (false) meet failure. The meet
    // bound starts at HALF (the widest local modal) and is narrowed by each concrete modal.
    bool hasConcrete = false;
    ModalInfo m{Mode::HALF};
    if (tyms.size() > 0) {
        for (auto ty : tyms) {
            if (tyMgr.ImplementsCopyInterface(ty.Ty())) {
                continue;
            }
            if (ty.Mode().local == Mode::IDEAL) {
                continue;
            }
            hasConcrete = true;
            m = m & ty.Mode();
        }
    }
    if (hasConcrete && m.local == Mode::IDEAL) {
        // A concrete meet collapsed to IDEAL (only possible if 0 was produced, i.e. the empty
        // bit-set); @local! and @~local do not have a common submode, meet fails.
        return {TypeManager::GetInvalidTy()};
    }
    // If no concrete modal contributed (all Copy or all IDEAL), keep the HALF bound without
    // introducing an IDEAL result, so no IDEAL modal leaks out of join/meet.
    return {res, m};
}

DataTy JoinAndMeet::BatchJoin(const std::set<DataTy>& tys)
{
    auto isSubtype = [this](DataTy ty1, DataTy ty2) { return tyMgr.IsSubtype(ty1, ty2); };
    auto isSupertype = [this](DataTy ty1, DataTy ty2) { return tyMgr.IsSubtype(ty2, ty1); };
    DualMode joinMode = {.bound = tyMgr.GetAnyTy(),
        .coFunc = static_cast<DataTy (JoinAndMeet::*)(const std::set<DataTy>&)>(&JoinAndMeet::BatchJoin),
        .coModal = static_cast<ModalTy (JoinAndMeet::*)(const std::set<ModalTy>&)>(&JoinAndMeet::BatchJoin),
        .contraFunc = static_cast<DataTy (JoinAndMeet::*)(const std::set<DataTy>&)>(&JoinAndMeet::BatchMeet),
        .contraModal = static_cast<ModalTy (JoinAndMeet::*)(const std::set<ModalTy>&)>(&JoinAndMeet::BatchMeet),
        .coSubtyFunc = isSubtype};
    std::set<DataTy> realTys;
    std::function<void(DataTy)> insertRealTy = [this, &realTys, &insertRealTy](DataTy ty) {
        if (auto tyVar = DynamicCast<TyVar*>(ty); (tyVar && Utils::In(tyVar, ignoredTyVars)) || ty->IsNothing()) {
            return;
        }
        if (auto unionTy = DynamicCast<UnionTy*>(ty)) {
            for (auto uty : unionTy->tys) {
                insertRealTy(uty);
            }
        } else {
            realTys.insert(ty);
        }
    };
    for (auto ty : tys) {
        insertRealTy(ty);
    }
    PData::CommitScope cs(tyMgr.constraints);
    if (auto ret = FindSmallestTy(realTys, isSupertype); ret && !ret->IsInvalid()) {
        return ret;
    }
    PData::Reset(tyMgr.constraints);
    if (auto funcTyJoin = JoinOrMeetFuncTy(joinMode, realTys)) {
        return funcTyJoin;
    }
    PData::Reset(tyMgr.constraints);
    if (auto tupleTyJoin = JoinOrMeetTupleTy(joinMode, realTys)) {
        return tupleTyJoin;
    }
    PData::Reset(tyMgr.constraints);
    auto common = tyMgr.GetAllCommonSuperTys(std::unordered_set<DataTy>(realTys.begin(), realTys.end()));
    if (curFile) {
        Utils::EraseIf(common, [this](ModalTy ty) { return !impMgr->IsTyAccessible(*curFile, *ty); });
    }
    if (common.empty()) {
        return tyMgr.GetAnyTy();
    }
    auto ret = FindSmallestTy(std::set<DataTy>(common.begin(), common.end()), isSubtype);
    // reset unnecessary constaints from finding possible supertypes (e.g. those claimed by conditional extensions),
    // and re-enforce necessary constraints by judging the common supertype again
    PData::Reset(tyMgr.constraints);
    if (ret->IsInvalid() || !LessThanAll(ret, realTys, isSupertype)) {
        PData::Reset(tyMgr.constraints);
    }
    return ret;
}

/**
 * returns:
 *      - the joined/met FuncTy if all tys are FuncTy and the LUB/GLB exists
 *      - AnyTy/Nothing if there exists any FuncTy but the LUB/GLB doesn't exist
 *      - nullptr if there is no FuncTy in tys
 */
DataTy JoinAndMeet::JoinOrMeetFuncTy(const DualMode& mode, const std::set<DataTy>& tys)
{
    auto uniformity = CheckFuncUniformity(tys);
    switch (uniformity) {
        case Uniformity::ALL_IRRELEVANT:
            return nullptr;
        case Uniformity::MIXED:
            return mode.bound;
        default:
            break;
    }
    size_t paramCnt = RawStaticCast<FuncTy*>(*tys.begin())->paramTys.size();
    std::vector<ModalTy> paramTys(paramCnt);
    for (size_t i = 0; i < paramCnt; i++) {
        std::set<ModalTy> operandParamTys;
        for (auto ty : tys) {
            operandParamTys.insert(RawStaticCast<FuncTy*>(ty)->paramTys[i]);
        }
        paramTys[i] = (this->*mode.contraModal)(operandParamTys);
    }
    std::set<ModalTy> operandRetTys;
    for (auto ty : tys) {
        operandRetTys.insert(RawStaticCast<FuncTy*>(ty)->retTy);
    }
    auto retTy = (this->*mode.coModal)(operandRetTys);
    if (Ty::AreTysCorrect(paramTys) && Ty::IsTyCorrect(retTy)) {
        auto resultTy = tyMgr.GetFunctionTy(paramTys, retTy);
        CJC_NULLPTR_CHECK(resultTy);
        for (auto ty : tys) {
            if (!mode.coSubtyFunc(ty, resultTy)) {
                return mode.bound;
            }
        }
        return resultTy;
    }
    return mode.bound;
}

/**
 * returns:
 *      - the joined/met TupleTy if all tys are TupleTy and the LUB/GLB exists
 *      - AnyTy/Nothing if there exists any TupleTy but the LUB/GLB doesn't exist
 *      - nullptr if there is no TupleTy in tys
 */
DataTy JoinAndMeet::JoinOrMeetTupleTy(const DualMode& mode, const std::set<DataTy>& tys)
{
    auto uniformity = CheckTupleUniformity(tys);
    switch (uniformity) {
        case Uniformity::ALL_IRRELEVANT:
            return {};
        case Uniformity::MIXED:
            return mode.bound;
        default:
            break;
    }
    size_t argCnt = RawStaticCast<TupleTy*>(tys.begin()->get())->typeArgs.size();
    std::vector<DataTy> typeArgs(argCnt);
    for (size_t i = 0; i < argCnt; i++) {
        std::set<DataTy> operandTyArgs;
        for (auto& ty : tys) {
            operandTyArgs.insert(RawStaticCast<TupleTy*>(ty.get())->TyArg(i));
        }
        typeArgs[i] = (this->*(mode.coFunc))(operandTyArgs);
    }
    if (Ty::AreTysCorrect(typeArgs)) {
        auto resultTy = tyMgr.GetTupleTy(typeArgs);
        for (auto ty : tys) {
            if (!mode.coSubtyFunc(ty, resultTy)) {
                return mode.bound;
            }
        }
        return resultTy;
    }
    return mode.bound;
}

JoinAndMeet::ErrOrTy JoinAndMeet::Meet(bool sprsErr)
{
    // Hot fix.
    if (!IsInputValid()) {
        return {ModalTy{TypeManager::GetInvalidTy()}};
    }
    auto mTy = BatchMeet(tySet);
    if (sprsErr || errMsg.empty()) {
        return {mTy.Ty()};
    } else {
        return {errMsg};
    }
}

JoinAndMeet::ErrOrTy JoinAndMeet::MeetAsVisibleTy()
{
    // Hot fix.
    if (!IsInputValid()) {
        return {ModalTy{TypeManager::GetInvalidTy()}};
    }
    auto mTy = BatchMeet(tySet);
    this->isForcedToUserVisible = true;
    mTy = ToUserVisibleTy(mTy);
    AddFinalErrMsgs(mTy, false);
    if (errMsg.empty()) {
        return {mTy};
    } else {
        return {errMsg};
    }
}

DataTy JoinAndMeet::BatchMeet(const std::set<DataTy>& tys)
{
    auto isSubtype = [this](ModalTy ty1, ModalTy ty2) { return tyMgr.IsSubtype(ty1, ty2); };
    auto isSupertype = [this](ModalTy ty1, ModalTy ty2) { return tyMgr.IsSubtype(ty2, ty1); };
    DualMode meetMode = {.bound = TypeManager::GetInvalidTy(),
        .coFunc = static_cast<DataTy (JoinAndMeet::*)(const std::set<DataTy>&)>(&JoinAndMeet::BatchMeet),
        .coModal = static_cast<ModalTy (JoinAndMeet::*)(const std::set<ModalTy>&)>(&JoinAndMeet::BatchMeet),
        .contraFunc = static_cast<DataTy (JoinAndMeet::*)(const std::set<DataTy>&)>(&JoinAndMeet::BatchJoin),
        .contraModal = static_cast<ModalTy (JoinAndMeet::*)(const std::set<ModalTy>&)>(&JoinAndMeet::BatchJoin),
        .coSubtyFunc = isSupertype};
    std::set<DataTy> realTys;
    std::function<void(DataTy)> insertRealTy = [this, &realTys, &insertRealTy](DataTy ty) {
        if (auto tyVar = DynamicCast<TyVar>(ty); tyVar && Utils::In(tyVar, ignoredTyVars)) {
            return;
        }
        if (auto unionTy = DynamicCast<UnionTy>(ty)) {
            std::set<DataTy> ums;
            for (auto p : unionTy->tys) {
                ums.insert(p);
            }
            insertRealTy(BatchJoin(ums));
        } else if (auto itsTy = DynamicCast<IntersectionTy*>(ty)) {
            for (auto ity : itsTy->tys) {
                insertRealTy(ity);
            }
        } else {
            realTys.insert(ty);
        }
    };
    for (auto ty : tys) {
        insertRealTy(ty);
    }
    PData::CommitScope cs(tyMgr.constraints);
    if (auto ret = FindSmallestTy(realTys, isSubtype); ret && !ret->IsInvalid()) {
        return ret;
    }
    PData::Reset(tyMgr.constraints);
    if (auto funcTyJoin = JoinOrMeetFuncTy(meetMode, realTys)) {
        return funcTyJoin;
    }
    PData::Reset(tyMgr.constraints);
    if (auto tupleTyJoin = JoinOrMeetTupleTy(meetMode, realTys)) {
        return tupleTyJoin;
    }
    PData::Reset(tyMgr.constraints);
    return {TypeManager::GetInvalidTy()};
}

std::string JoinAndMeet::CombineErrMsg(ErrMsg& msgs)
{
    std::string res{};
    res.append("Traces:\n");

    while (!msgs.empty()) {
        res.append(msgs.top() + "\n");
        msgs.pop();
    }
    return res;
}

ModalTy JoinAndMeet::ToUserVisibleTy(ModalTy ty)
{
    CJC_NULLPTR_CHECK(ty);
    if (ty->IsIntersection()) {
        auto iSecTy = RawStaticCast<IntersectionTy*>(ty.Ty());
        auto isSubtype = [this](ModalTy ty1, ModalTy ty2) { return tyMgr.IsSubtype(ty1, ty2); };
        std::set<DataTy> iMembers;
        for (auto p : iSecTy->tys) {
            iMembers.insert(p);
        }
        ModalTy res = ToUserVisibleTy({FindSmallestTy(iMembers, isSubtype), ty.Mode()});
        // Given C1 <: I1 & I2 and C2 <: I1 & I2, then Join(C1, C2) gives I1 & I2.
        // Meet(I1, I2) gives Nothing but the result for the original Join.
        return res->IsNothing() ? ModalTy{tyMgr.GetAnyTy(), res.Mode()} : res;
    } else if (auto unionTy = DynamicCast<UnionTy>(ty.Ty())) {
        std::set<ModalTy> uTys;
        for (auto& t : unionTy->tys) {
            uTys.insert(ToUserVisibleTy(ModalTy{t}));
        }
        auto res = BatchJoin(uTys);
        // Dual of the above comments.
        return res->IsAny() ? ModalTy{TypeManager::GetNothingTy(), res.Mode()} : res;
    } else if (ty->IsFunc()) {
        auto funcTy = RawStaticCast<FuncTy*>(ty.Ty());
        auto retTy = ToUserVisibleTy(funcTy->retTy);
        auto paramTys = funcTy->paramTys;
        std::transform(funcTy->paramTys.begin(), funcTy->paramTys.end(), paramTys.begin(),
            [this](ModalTy typ) { return ToUserVisibleTy(typ); });
        if (Ty::AreTysCorrect(paramTys) && Ty::IsTyCorrect(retTy)) {
            return {
                tyMgr.GetFunctionTy(paramTys, retTy, {funcTy->isC, funcTy->isClosureTy, funcTy->hasVariableLenArg}),
                ty.Mode()};
        } else {
            return {TypeManager::GetInvalidTy()};
        }
    } else if (ty->IsTuple()) {
        auto tupleTy = RawStaticCast<TupleTy*>(ty.Ty());
        auto elemTys = tupleTy->typeArgs;
        std::transform(tupleTy->typeArgs.begin(), tupleTy->typeArgs.end(), elemTys.begin(),
            [this](ModalTy typ) { return ToUserVisibleTy(typ); });
        if (Ty::AreTysCorrect(elemTys)) {
            std::vector<DataTy> dataElems;
            dataElems.reserve(elemTys.size());
            for (const auto& m : elemTys) {
                dataElems.push_back(m.Ty());
            }
            return {tyMgr.GetTupleTy(dataElems), ty.Mode()};
        } else {
            return {TypeManager::GetInvalidTy()};
        }
    } else {
        return ty;
    }
}

void JoinAndMeet::AddFinalErrMsgs(ModalTy ty, bool isJoin)
{
    auto getTysStr = [this]() {
        auto tyVec = Utils::SetToVec<ModalTy>(tySet);
        std::sort(tyVec.begin(), tyVec.end(), CompTyByNamesModal);
        std::string tysStr;
        for (auto it = tyVec.begin(); it != std::prev(tyVec.end()); ++it) {
            tysStr += (it == tyVec.begin() ? std::string() : ", ") + "'" + it->String() + "'";
        }
        if (tyVec.size() > 1) {
            CJC_NULLPTR_CHECK(tyVec.back());
            tysStr += " and '" + tyVec.back().String() + "'";
        }
        return tysStr;
    };

    if (ty->IsInvalid()) {
        CJC_ASSERT(!tySet.empty());
        std::string newErrMsg = "The types " + getTysStr() + " do not have ";
        newErrMsg += isJoin ? "the smallest common supertype" : "the greatest common subtype";
        errMsg.push(newErrMsg);
        return;
    }
}

std::pair<std::optional<std::string>, ModalTy> JoinAndMeet::SetJoinedType(
    ModalTy ty, std::variant<std::stack<std::string>, ModalTy>& joinRes)
{
    if (std::get_if<ModalTy>(&joinRes)) {
        ty = std::get<ModalTy>(joinRes);
        return {{}, ty};
    }
    ty = {TypeManager::GetInvalidTy()};
    auto errMsgs = std::get<std::stack<std::string>>(joinRes);
    return {JoinAndMeet::CombineErrMsg(errMsgs), ty};
}

std::pair<std::optional<std::string>, ModalTy> JoinAndMeet::SetMetType(
    ModalTy ty, std::variant<std::stack<std::string>, ModalTy>& metRes)
{
    return SetJoinedType(ty, metRes);
}

bool JoinAndMeet::IsInputValid() const
{
    bool isValid = true;
    if (tySet.empty()) {
        isValid = false;
    }
    if (!Ty::AreTysCorrect(tySet)) {
        isValid = false;
    }
    return isValid;
}
