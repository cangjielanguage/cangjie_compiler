// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares some utility functions for Parallel Compile.
 */

#ifndef CANGJIE_UTILS_PARALLELUTIL_H
#define CANGJIE_UTILS_PARALLELUTIL_H

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/UserDefinedType.h"
#include "cangjie/Utils/TaskQueue.h"

#include <memory>
#include <vector>

using namespace Cangjie::CHIR;
namespace Cangjie::Utils {
class ParallelUtil {
public:
    explicit ParallelUtil(CHIR::CHIRBuilder& builder, size_t threadsNum) : threadsNum(threadsNum), builder(builder)
    {
    }

    void RunAST2CHIRInParallel(const std::vector<Ptr<const AST::Decl>>& decls, const CHIRType& chirType,
        const Cangjie::GlobalOptions& opts, const GenericInstantiationManager* gim, AST2CHIRNodeMap<Value>& globalCache,
        const ElementList<Ptr<const AST::Decl>>& localConstVars,
        const ElementList<Ptr<const AST::FuncDecl>>& localConstFuncs, IncreKind& kind,
        const TranslateASTNodeFunc& funcForTranlateASTNode, std::unordered_map<Block*, Terminator*>& maybeUnreachable,
        bool computeAnnotations,
        std::vector<CHIR::Func*>& initFuncsForAnnoFactory,
        std::vector<std::pair<const AST::Decl*, Func*>>& annoFactoryFuncs)
    {
        size_t funcNum = decls.size();
        std::vector<std::unique_ptr<CHIR::CHIRBuilder>> builderList = ConstructSubBuilders(funcNum);
        Utils::TaskQueue taskQueue(threadsNum);
        std::vector<std::unique_ptr<Translator>> trans;
        std::vector<std::unique_ptr<CHIR::CHIRType>> chirTypes;
        CHIR::CHIRTypeCache chirTypeCache(chirType.GetTypeMap(), chirType.GetGlobalNominalCache());
        std::vector<std::unordered_map<Block*, Terminator*>> maybeUnreachableBlocks;
        maybeUnreachableBlocks.resize(funcNum);
        for (size_t idx = 0; idx < funcNum; ++idx) {
            auto decl = decls.at(idx);
            auto subChirType = std::make_unique<CHIRType>(*builderList[idx], chirTypeCache);
            auto tran = std::make_unique<Translator>(
                *builderList[idx], *subChirType, opts, gim, globalCache, localConstVars,
                localConstFuncs, kind, annoFactoryFuncs, maybeUnreachableBlocks[idx],
                computeAnnotations, initFuncsForAnnoFactory);
            tran->SetTopLevel(*decl);
            taskQueue.AddTask<void>(
                [translator = tran.get(), decl, &funcForTranlateASTNode]() {
                    return funcForTranlateASTNode(*decl, *translator);
                });
            if (decl->TestAttr(AST::Attribute::GLOBAL) && !Is<AST::InheritableDecl>(decl)) {
                tran->CollectValueAnnotation(*decl);
            }
            trans.emplace_back(std::move(tran));
            chirTypes.emplace_back(std::move(subChirType));
        }
        taskQueue.RunAndWaitForAllTasksCompleted();
        for (auto& subBd : builderList) {
            (*subBd).MergeAllocatedInstance();
        }
        builder.GetChirContext().MergeTypes();
        for (auto& it : maybeUnreachableBlocks) {
            maybeUnreachable.merge(it);
        }
        // If the function RunAST2CHIRInParallel is invoked again, we need revert chirTypePool to the inital state.
    }

private:
    size_t threadsNum;
    CHIR::CHIRBuilder& builder;

    std::vector<std::unique_ptr<CHIR::CHIRBuilder>> ConstructSubBuilders(const size_t funcNum)
    {
        std::vector<Cangjie::Utils::TaskResult<std::unique_ptr<CHIR::CHIRBuilder>>> results;
        Utils::TaskQueue contextTaskQueue(threadsNum);
        for (size_t i = 0; i < funcNum; i++) {
            results.emplace_back(contextTaskQueue.AddTask<std::unique_ptr<CHIR::CHIRBuilder>>([this, i]() {
                auto subBuilder = std::make_unique<CHIR::CHIRBuilder>(this->builder.GetChirContext(), i);
                return subBuilder;
            }));
        }
        contextTaskQueue.RunAndWaitForAllTasksCompleted();
        std::vector<std::unique_ptr<CHIR::CHIRBuilder>> builderList;
        for (auto& result : results) {
            auto res = result.get();
            builderList.emplace_back(std::move(res));
        }
        return builderList;
    }
};
} // namespace Cangjie::Utils
#endif