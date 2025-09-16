// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_IRGENERATOR_H
#define CANGJIE_IRGENERATOR_H

#include <memory>
#include <type_traits>

namespace Cangjie {
namespace CHIR {
class Package;
class Func;
class BasicBlock;
class Expression;
} // namespace CHIR

namespace CodeGen {
class IRGeneratorImpl {
public:
    virtual ~IRGeneratorImpl() = default;
    virtual void EmitIR() = 0;

protected:
    IRGeneratorImpl() = default;
};

/**
 * Each `IRGenerator` instantiation needs an implementation class which inherits `IRGeneratorImpl`.
 * The implementation class can have its specific `EmitIR` method.
 */
template <typename Impl = IRGeneratorImpl,
    typename = typename std::enable_if<std::is_base_of<IRGeneratorImpl, Impl>::value>::type>
class IRGenerator {
public:
    void EmitIR()
    {
        impl->EmitIR();
    }
    IRGenerator() = delete;

protected:
    explicit IRGenerator(std::unique_ptr<Impl> impl) : impl(std::move(impl))
    {
    }
    std::unique_ptr<Impl> impl;
};
} // namespace CodeGen
} // namespace Cangjie
#endif // CANGJIE_IRGENERATOR_H
