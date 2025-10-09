// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares PackageManager related classes, which resolves dependencies between packages.
 */

#ifndef CANGJIE_FRONTENDTOOL_PACKAGEMANAGER_H
#define CANGJIE_FRONTENDTOOL_PACKAGEMANAGER_H

#include <stack>
#include <string>
#include <utility>
#include <vector>

#include "cangjie/Modules/ImportManager.h"

namespace Cangjie {
class PackageManager;

/**
 * PackageInfo stores information about the current package, including to packageName, package dir path, source files
 * and dependencies.
 */
struct PackageInfo {
    std::string fullPackageName;
    /**
     * The dependencies of current package.
     */
    std::unordered_set<PackageInfo*> deps;

    explicit PackageInfo(const std::string& fullPackageName) : fullPackageName(fullPackageName)
    {
    }
};

/**
 * PackageManager is used to do module compilation by resolving package dependencies and generating build commands.
 */
class PackageManager {
public:
    explicit PackageManager(ImportManager& importManager) : importManager(importManager)
    {
    }

    /**
     * Resolve package dependencies using topological sort method. It should be noted that circular dependencies are
     * allowed.
     * @param pkgs packages whose dependencies need to be resolved.
     * @param withCodeGen it is true when call this function in CodeGen staged.
     */
    bool ResolveDependence(std::vector<Ptr<AST::Package>>& pkgs);

    /**
     * Return buildOrders for read private member.
     */
    const std::vector<std::vector<PackageInfo*>>& GetBuildOrders() const
    {
        return orderedPackageInfos;
    }

    /**
     * If a package relies on packages which are from other SCC (Strongly Connected Component), it should be recorded.
     */
    std::unordered_map<std::string, std::set<std::string>> packageToOtherSccMap;
    std::unordered_map<std::string, std::unique_ptr<PackageInfo>> packageInfoMap;

private:
    ImportManager& importManager;
    std::vector<std::vector<PackageInfo*>> orderedPackageInfos;
    struct TarjanContext {
        std::unordered_map<PackageInfo*, size_t> indices;  /**< The discovered order of vertices in a DFS. */
        std::unordered_map<PackageInfo*, size_t> lowlinks; /**< The smallest index reachable from the vertex. */
        std::unordered_map<PackageInfo*, bool> onStack;    /**< Indicate whether the vertex is on stack. */
    };

    void TarjanForSCC(TarjanContext& ctx, std::stack<PackageInfo*>& st, size_t& index, PackageInfo* u);

    /**
     * Check and collect dependencies for each package by parsing source files.
     */
    void CollectDeps(std::vector<Ptr<AST::Package>>& pkgs);
    void CollectDepsInFile(AST::File& file, const std::unique_ptr<PackageInfo>& pkgInfo, const AST::Package& pkg);
};
} // namespace Cangjie
#endif // CANGJIE_FRONTENDTOOL_PACKAGEMANAGER_H
