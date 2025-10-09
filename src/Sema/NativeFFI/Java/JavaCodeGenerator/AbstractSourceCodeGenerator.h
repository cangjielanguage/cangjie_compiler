// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class for java code generation.
 */
#ifndef CANGJIE_SEMA_JAVA_ABSTRACT_GENERATOR
#define CANGJIE_SEMA_JAVA_ABSTRACT_GENERATOR

#include <fstream>
#include <functional>

#include "cangjie/AST/Types.h"
#include "cangjie/Utils/FileUtil.h"

namespace Cangjie::Interop {
using namespace Cangjie::AST;

class AbstractSourceCodeGenerator {
public:
    explicit AbstractSourceCodeGenerator(const std::string& outputFilePath);
    AbstractSourceCodeGenerator(const std::string& outputFolderPath, const std::string& outputFileName);
    virtual ~AbstractSourceCodeGenerator() = default;

    void Generate();

protected:
    std::string res;
    static const std::string TAB;
    static const std::string TAB2;

    template <typename Container, typename Element>
    static std::string Join(const Container& container, const std::string& delimiter,
        const std::function<std::string(Element)>& transformer)
    {
        using E = std::decay_t<Element>;
        using ValueType = std::decay_t<decltype(*std::begin(container))>;
        static_assert(
            std::is_same_v<ValueType, E>, "Transformer argument must have the same type as container's element.");

        std::string result = "";
        bool isFirst = true;

        for (const auto& elem : container) {
            if (!isFirst) {
                result += delimiter;
            }
            isFirst = false;
            result += transformer(elem);
        }

        return result;
    }

    virtual void ConstructResult() = 0;
    void AddWithIndent(const std::string& indent, const std::string& s);

private:
    const std::string outputFilePath;
    bool WriteToOutputFile();
};
} // namespace Cangjie::Interop

#endif // CANGJIE_SEMA_JAVA_ABSTRACT_GENERATOR
