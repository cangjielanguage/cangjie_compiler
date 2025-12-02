// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// This files declares the entry of ComputeAnnotationsBeforeCheck stage.

#ifndef CANGJIE_CHIR_CHECKER_COMPUTEANNOTATIONS_H
#define CANGJIE_CHIR_CHECKER_COMPUTEANNOTATIONS_H

#include "cangjie/AST/Node.h"
#include "cangjie/CHIR/CHIR.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie::CHIR {

using namespace AST;
using Number = unsigned long long;
/// Value representation of Annotation after consteval.
/// All integers/bool/char are represented internally as \ref Number, any object-like is stored in
/// AnnoInstanceClassInst object. Two shared_ptr<AnnoInstanceClassInst> point to the same object when they point to the
/// same CHIR object. Unit is never stored.
/// Enum with args are stored as the index of constructor followed by constructor args together as a vector. Enum
/// withoug args are stored as \ref Number, a simple index of constructor.
struct AnnoInstanceValue
    : public std::variant<Number, double, std::string, std::shared_ptr<struct AnnoInstanceClassInst>, std::monostate> {
    Number Value() const;
    long long SignedValue() const;
    unsigned Rune() const;
    bool Bool() const;
    const std::string& String() const;
    double Float() const
    {
        return std::get<double>(*this);
    }
    std::shared_ptr<struct AnnoInstanceClassInst> Object() const;
    operator bool() const noexcept;

    std::string ToString() const noexcept;
};

/// object representation in AnnoInstanceValue
struct AnnoInstanceClassInst {
    const InheritableDecl* cl;
    std::vector<std::pair<std::string, AnnoInstanceValue>> a;

    const AnnoInstanceValue* GetField(const std::string& s) const
    {
        for (auto& p : a) {
            if (!p.first.empty() && p.first == s) {
                return &p.second;
            }
        }
        return nullptr;
    }
    const AnnoInstanceValue& GetField(size_t i) const { return a[i].second; }
    std::string ToString() const;
};
using AnnoInstance = std::shared_ptr<struct AnnoInstanceClassInst>;
using AnnoMap = std::unordered_map<const Decl*, std::vector<AnnoInstance>>;

/// Temporary results of computing annotations.
/// Fields \ref ctx and \ref builder are used for holding the memory used by other fields.
/// \ref map A map from AST decl to the vector of Annotation Instance of it.
struct ConstEvalResult {
    CHIRContext ctx;
    CHIRBuilder builder;
    CHIR::Package* pkg;
    AnnoMap map;
    ConstEvalResult(std::unordered_map<unsigned int, std::string>& fileNameMap, size_t jobs)
        : ctx{}, builder{ctx, jobs}, pkg{}, map{}
    {
        ctx.SetFileNameMap(&fileNameMap);
        ctx.SetThreadNum(jobs);
    }

    void Dump() const;
};
OwnedPtr<ConstEvalResult> ComputeAnnotations(AST::Package& pkg, CompilerInstance& ci);
}
#endif
