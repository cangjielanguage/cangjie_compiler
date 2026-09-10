// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ATTRIBUTEINFO_H
#define CANGJIE_CHIR_ATTRIBUTEINFO_H

#include <bitset>
#include <cstdint>
#include <string>

namespace Cangjie::CHIR {

enum class Attribute {
    // tokens attribute
    STATIC,    ///< Mark whether a member is a static one.
    PUBLIC,    ///< Mark whether a member is a public one.
    PRIVATE,   ///< Mark whether a member is a private one.
    PROTECTED, ///< Mark whether a member is a protected one.

    ABSTRACT, ///< Mark whether a function is an abstract one.
    VIRTUAL,  ///< Mark whether a declaration is in fact open (even if the user does not use `open` keyword).

    OVERRIDE, ///< Mark whether a declaration in fact overrides the inherited one
              ///(even if the user does not use `override` keyword).

    REDEF, ///< Mark whether a declaration in fact overrides the inherited one (even if the user does not use
           ///< `redef` keyword).

    SEALED,  ///< Mark whether a declaration is a sealed one.
    FOREIGN, ///< Mark whether a declaration is a foreign one.

    MUT,      ///< Mark whether a declaration is a mutable one.
    FINAL,    /**< Mark a func override a parent class's func, and this func self does not have VIRTUAL Attribute. */
    OPERATOR, ///< Mark whether a declaration is a operator one.
    READONLY,             ///< 'let x = xxx', 'x' enable READONLY attribute
    CONST,                ///< correspond `const` keyword in Cangjie source code.
    IMPORTED,             ///< Mark whether variable、func、enum、struct、class is imported from other package.
    GENERIC_INSTANTIATED, ///< Mark whether a `GlobalVar/Function/Type` is instantiated.
    NO_DEBUG_INFO,        ///< Mark a `Value` doesn't contain debug info, like line/column number.
    GENERIC,              ///< Mark a declaration is generic
    INTERNAL,             ///< GlobalVar/Function/Enum/Class/Struct/Interface is visible in current and sub package.
    COMPILER_ADD,         ///< Mark a `Value` is added by compiler, like "copied default func from interface".

    // compiler attribute
    NO_REFLECT_INFO, ///< Mark a `Value` is't used by `reflect` feature.
    NO_INLINE,       ///< Mark a Function can't be inlined.
    NON_RECOMPILE,   ///< only used in imported global var/func in incremental compilation, indicate this value is
                     ///< converted from a decl in current package that is not recompiled.
    UNREACHABLE,     ///< Mark a Block is unreachable.
    NO_SIDE_EFFECT,  ///< Mark a Function does't have side effect.
    COMMON,          ///< Mark whether it's common declaration.
    SPECIFIC,        ///< Mark whether it's specific declaration.
    SKIP_ANALYSIS,   ///< Mark node that is not used for analysis.
                     ///< e.g. Node can be skiped if it has no body when creating 'common part'
    DESERIALIZED,    ///< Node deserialized from .chir file
    INITIALIZER,     ///< Mark nodes that related to initialization process.
                     ///< Marked functions are package initializer, file initializers, variable initializer or so.
                     ///< On the block is used to search for it among other blocks of the function.
    UNSAFE,   ///< Mark whether a function that was marked as `unsafe`
    // Native FFI attributes
    JAVA_MIRROR,      ///< Mark whether it's @JavaMirror declaration (binding for a java type).
    JAVA_IMPL,        ///< Mark whether it's @JavaImpl declaration.
    OBJ_C_MIRROR,     ///< Mark whether it's @ObjCMirror declaration (binding for an Objective-C type).
    HAS_INITED_FIELD, ///< Mark whether a node is a special flag, which marks the class instance as initialized.
    JAVA_HAS_DEFAULT, ///< Mark whether JAVA_MIRROR interface has default method.
    PREVIOUSLY_DESERIALIZED, /// Mark that deserialization occurs not in the newly created node, but in an existing one.
    DEMODE, ///< Mark instance member field as @Demode (@~local regardless of receiver modal).
    EXCLAVE, ///< Mark a function whether it's exclave

    ATTR_END
};

constexpr uint64_t ATTR_SIZE = 64;

class AttributeInfo {
public:
    explicit AttributeInfo() : attributesInfo(0)
    {
    }
    explicit AttributeInfo(const std::bitset<ATTR_SIZE>& attrs) : attributesInfo(attrs)
    {
    }
    ~AttributeInfo() = default;

    void SetAttr(Attribute attr, bool enable)
    {
        (void)attributesInfo.set(static_cast<size_t>(attr), enable);
    }

    bool TestAttr(Attribute attr) const
    {
        return attributesInfo.test(static_cast<size_t>(attr));
    }

    std::bitset<ATTR_SIZE> GetRawAttrs() const
    {
        return attributesInfo;
    }

    void AppendAttrs(const AttributeInfo& info)
    {
        attributesInfo |= info.GetRawAttrs();
    }

    void Dump() const;
    std::string ToString() const;

private:
    std::bitset<ATTR_SIZE> attributesInfo; // attribute bitset
};
} // namespace Cangjie::CHIR

#endif
