// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_IMPLICIT_IMPORTED_FUNC_MGR_H
#define CANGJIE_CHIR_IMPLICIT_IMPORTED_FUNC_MGR_H

#include "cangjie/AST/Node.h"

namespace Cangjie::CHIR {
/**
 * The information structure of the imported functions that are implicitly called only in CodeGen.
 */
struct ImplicitImportedFunc {
    AST::ASTKind parentKind;
    std::string identifier{};
    std::string parentName{};
};

class ImplicitImportedFuncMgr {
public:
    enum class FuncKind : uint8_t { GENERIC, NONE_GENERIC };

    static ImplicitImportedFuncMgr& Instance() noexcept;
    void RegImplicitImportedFunc(const ImplicitImportedFunc& func, FuncKind funcKind) noexcept;
    std::vector<ImplicitImportedFunc> GetImplicitImportedFuncs(FuncKind funcKind);

    ImplicitImportedFuncMgr(ImplicitImportedFuncMgr&&) = delete;
    ImplicitImportedFuncMgr(const ImplicitImportedFuncMgr&) = delete;
    ImplicitImportedFuncMgr& operator=(ImplicitImportedFuncMgr&&) = delete;
    ImplicitImportedFuncMgr& operator=(const ImplicitImportedFuncMgr&) = delete;

private:
    ImplicitImportedFuncMgr() noexcept = default;
    ~ImplicitImportedFuncMgr() = default;

    /**
     * This vector is used to store imported generic function information.
     * These imported generic functions, which are called implicitly in CodeGen, are from the "std.core" package.
     * Their generic instances may be in other import packages.
     */
    std::vector<ImplicitImportedFunc> implicitImportedGenericFuncs{};
    /**
     * This vector is used to store imported non-generic function information.
     * These imported functions, which are called implicitly in CodeGen, are from the "std.core" package.
     * If the function has no parent class,
     * its parent class name is an empty string and its parentKind is "INVALID_DECL".
     */
    std::vector<ImplicitImportedFunc> implicitImportedNonGenericFuncs{};
};

class ImplicitImportedFuncRegister {
public:
    ImplicitImportedFuncRegister(const ImplicitImportedFunc& func, ImplicitImportedFuncMgr::FuncKind kind) noexcept
    {
        ImplicitImportedFuncMgr::Instance().RegImplicitImportedFunc(func, kind);
    }
    ~ImplicitImportedFuncRegister() = default;
};

#define REG_IMPLICIT_IMPORTED_NON_GENERIC_FUNC(outDeclKind, identifier, ...) \
static ImplicitImportedFuncRegister g_reg_##identifier##__VA_ARGS__( \
    {outDeclKind, #identifier, #__VA_ARGS__}, ImplicitImportedFuncMgr::FuncKind::NONE_GENERIC)

#define REG_IMPLICIT_IMPORTED_GENERIC_FUNC(outDeclKind, identifier, ...) \
static ImplicitImportedFuncRegister g_reg_##identifier##__VA_ARGS__( \
    {outDeclKind, #identifier, #__VA_ARGS__}, ImplicitImportedFuncMgr::FuncKind::GENERIC)
} // namespace Cangjie::CHIR
#endif // CANGJIE_CHIR_IMPLICIT_IMPORTED_FUNC_MGR_H
