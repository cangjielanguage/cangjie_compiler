// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the class for calculating the smallest common supertype (or join, or least upper bound) and the
 * greatest common subtype (or meet, or greatest lower bound) of the given set of types.
 */
#ifndef CANGJIE_SEMA_JOINANDMEET_H
#define CANGJIE_SEMA_JOINANDMEET_H

#include <functional>
#include <stack>
#include <variant>

#include "cangjie/AST/Types.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Modules/ImportManager.h"

namespace Cangjie {
class JoinAndMeet;
struct DualMode {
    AST::DataTy bound; // Any for join, Nothing for meet
    AST::DataTy (JoinAndMeet::*coFunc)(const std::set<AST::DataTy>&); // join for join, meet for meet
    AST::ModalTy (JoinAndMeet::*coModal)(const std::set<AST::ModalTy>&);
    AST::DataTy (JoinAndMeet::*contraFunc)(const std::set<AST::DataTy>&); // meet for join, join for meet
    AST::ModalTy (JoinAndMeet::*contraModal)(const std::set<AST::ModalTy>&);
    std::function<bool(AST::DataTy, AST::DataTy)> coSubtyFunc; // is-subtype for join, is-supertype for meet
};

class JoinAndMeet {
    using ErrMsg = std::stack<std::string>;
    using ErrOrTy = std::variant<ErrMsg, AST::ModalTy>;

public:
    // if curFile is given, impMgr must also be given
    JoinAndMeet(TypeManager& tyMgr, const std::initializer_list<AST::ModalTy> tySet,
        const std::initializer_list<Ptr<TyVar>> ignoredTyVars = {}, Ptr<const ImportManager> impMgr = nullptr,
        Ptr<AST::File> curFile = nullptr)
        : tyMgr(tyMgr), tySet(tySet), ignoredTyVars(ignoredTyVars), impMgr(impMgr), curFile(curFile)
    {
    }
    // if curFile is given, impMgr must also be given
    JoinAndMeet(TypeManager& tyMgr, const std::set<AST::ModalTy> tySet, const std::set<Ptr<TyVar>> ignoredTyVars = {},
        Ptr<const ImportManager> impMgr = nullptr, Ptr<AST::File> curFile = nullptr)
        : tyMgr(tyMgr), tySet(tySet), ignoredTyVars(ignoredTyVars), impMgr(impMgr), curFile(curFile)
    {
    }

    /**
     * Calculate the join (i.e. least upper bound) of two types.
     * sprsErr: suppress error messages. We opt in reporting the summary of errors after the join (meet) finishes and
     * the error messages produced along the calculation are regarded as logs for debuging.
     * Turn the sprsErr from true to false when debuging this module.
     */
    ErrOrTy Join(bool sprsErr = true);
    ErrOrTy JoinAsVisibleTy();
    /**
     * Calculate the meet (i.e. greatest lower bound) of two types.
     */
    ErrOrTy Meet(bool sprsErr = true);
    ErrOrTy MeetAsVisibleTy();
    static std::string CombineErrMsg(ErrMsg& msgs);

    static std::pair<std::optional<std::string>, AST::ModalTy> SetJoinedType(
        AST::ModalTy ty, std::variant<std::stack<std::string>, AST::ModalTy>& joinRes);
    static std::pair<std::optional<std::string>, AST::ModalTy> SetMetType(
        AST::ModalTy ty, std::variant<std::stack<std::string>, AST::ModalTy>& metRes);

    /** Join locality modes. */
    static ModalInfo JoinMode(TypeManager& tyMgr, const std::set<AST::ModalTy>& tyms);

    // Convert the input type to a user-visible one by eliminating intersection and union types.
    // Use a boolean value isJoin to distinguish the join and meet mode.
    AST::ModalTy ToUserVisibleTy(AST::ModalTy ty);

private:
    TypeManager& tyMgr;
    const std::set<AST::ModalTy> tySet;
    const TyVars ignoredTyVars;
    Ptr<const ImportManager> impMgr;
    Ptr<AST::File> curFile;
    ErrMsg errMsg;
    bool isForcedToUserVisible = false;

    AST::ModalTy BatchJoin(const std::set<AST::ModalTy>& tys);
    AST::ModalTy BatchMeet(const std::set<AST::ModalTy>& tys);
    AST::DataTy BatchJoin(const std::set<AST::DataTy>& tys);
    AST::DataTy BatchMeet(const std::set<AST::DataTy>& tys);

    AST::DataTy JoinOrMeetFuncTy(const DualMode& mode, const std::set<AST::DataTy>& tys);
    AST::DataTy JoinOrMeetTupleTy(const DualMode& mode, const std::set<AST::DataTy>& tys);

    void AddFinalErrMsgs(AST::ModalTy ty, bool isJoin);
    bool IsInputValid() const;
};
} // namespace Cangjie
#endif // CANGJIE_SEMA_JOINANDMEET_H
