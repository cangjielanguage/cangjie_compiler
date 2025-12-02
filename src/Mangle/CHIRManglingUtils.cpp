// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements some util functions for CHIR mangling.
 */

#include "cangjie/Mangle/CHIRManglingUtils.h"

#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/IR/Type/EnumDef.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Mangle/CHIRTypeManglingUtils.h"
#include <sstream>

using namespace Cangjie::CHIR;
using namespace Cangjie::MangleUtils;
namespace {
std::string ReplaceManglePrefixWith(const std::string& mangledName, const std::string newPrefix)
{
    if (mangledName.find(Cangjie::MANGLE_CANGJIE_PREFIX) == 0) {
        // Replace _C (2 characters) with user specified prefix.
        return newPrefix + mangledName.substr(Cangjie::MANGLE_PREFIX_LEN);
    } else {
        CJC_ASSERT(false && "Mangle name has no _C prefix to be replaced.");
        return mangledName;
    }
}
} // namespace
namespace Cangjie::CHIRMangling {
std::string GenerateVirtualFuncMangleName(
    const FuncBase* rawFunc, const CustomTypeDef& customTypeDef, const ClassType* parentTy, bool isVirtual)
{
    std::stringstream ss;
    // "_CV" represents function wrapper for virtual functions. "_CM" represents function wrapper for mutable functions.
    std::string prefix = isVirtual ? MANGLE_VIRTUAL_PREFIX : MANGLE_MUTABLE_PREFIX;
    std::string originalName = ReplaceManglePrefixWith(rawFunc->GetIdentifierWithoutPrefix(), prefix);
    ss << originalName;
    std::vector<std::string> customTyGenericTypeStack;
    for (auto genericType : customTypeDef.GetGenericTypeParams()) {
        customTyGenericTypeStack.emplace_back(genericType->GetSrcCodeIdentifier());
    }
    if (customTypeDef.IsExtend()) {
        ss << MANGLE_EXTEND_PREFIX << MangleType(*customTypeDef.GetType(), customTyGenericTypeStack);
        std::vector<std::string> implementTypes;
        for (auto& ty : customTypeDef.GetImplementedInterfaceTys()) {
            implementTypes.emplace_back(MangleType(*ty, customTyGenericTypeStack));
        }
        // Sort implement types, so the order of how implement types show up don't affect compatibility.
        std::sort(implementTypes.begin(), implementTypes.end());
        for (auto& implementType : implementTypes) {
            ss << implementType;
        }
        ss << customTypeDef.GetPackageName().size() << customTypeDef.GetPackageName();
    } else {
        ss << MANGLE_DOLLAR_PREFIX << ReplaceManglePrefixWith(customTypeDef.GetIdentifierWithoutPrefix(), "");
    }
    ss << MANGLE_DOLLAR_PREFIX << MangleType(*parentTy, customTyGenericTypeStack);
    return ss.str();
}

std::string GenerateInstantiateFuncMangleName(const std::string& baseName, const std::vector<Type*>& instTysInFunc)
{
    std::stringstream ss;
    // "_CI" represents function wrapper for Instantiate functions.
    ss << ReplaceManglePrefixWith(baseName, MANGLE_INSTANTIATE_PREFIX) << MANGLE_DOLLAR_PREFIX;
    if (instTysInFunc.empty()) {
        ss << MANGLE_VOID_TY_SUFFIX;
    } else {
        for (auto& paramTy : instTysInFunc) {
            ss << MangleType(*paramTy);
        }
    }
    return ss.str();
}

std::string GenerateLambdaFuncMangleName(const Func& baseFunc, size_t counter)
{
    std::stringstream ss;
    // "_CL" represents for compiler generated anonymous functions (lambdas).
    ss << ReplaceManglePrefixWith(baseFunc.GetIdentifierWithoutPrefix(), MANGLE_LAMBDA_PREFIX) <<
        MANGLE_DOLLAR_PREFIX << counter << MANGLE_WILDCARD_PREFIX;
    return ss.str();
}

std::string OverflowStrategyToString(OverflowStrategy ovf)
{
    switch (ovf) {
        case OverflowStrategy::WRAPPING:
            return "&";
        case OverflowStrategy::THROWING:
            return "~";
        default:
            return "%";
    }
}

std::string GenerateOverflowOperatorFuncMangleName(const std::string& name, OverflowStrategy ovf, bool isBinary,
    const BuiltinType& type)
{
    std::stringstream ss;
    // "_CO" represents for compiler generated operator split functions.
    ss << MANGLE_OPERATOR_PREFIX << OverflowStrategyToString(ovf);
    if (MangleUtils::OPERATOR_TYPE_MANGLE.count(name) == 0) {
        CJC_ASSERT(false && "Unsupported name for overflow operator mangling.");
    }
    ss << MangleUtils::OPERATOR_TYPE_MANGLE.at(name) << MANGLE_FUNC_PARAM_TYPE_PREFIX << MangleType(type);
    if (isBinary) {
        ss << MangleType(type);
    }
    return ss.str();
}

std::string GenerateAnnotationFuncMangleName(const std::string& name)
{
    // replace "_CN" with "_CAF"
    return MANGLE_CANGJIE_PREFIX + "AF" + name.substr(Cangjie::MANGLE_PREFIX_LEN + 1);
}

namespace ClosureConversion {
std::string GenerateGenericBaseClassMangleName(size_t paramNum)
{
    std::stringstream ss;
    // `$C` is a special prefix for closure conversion class declarations. `g` stands for generic.
    // `$Cg` is followed by a number with an underscore suffix.
    ss << MANGLE_CLOSURE_GENERIC_PREFIX << paramNum << MANGLE_WILDCARD_PREFIX;
    return ss.str();
}

std::string GenerateInstantiatedBaseClassMangleName(const FuncType& funcType)
{
    std::stringstream ss;
    // `$C` is a special prefix for closure conversion class declarations. `i` stands for instantiate.
    // `$Ci` is followed by a type mangle name.
    ss << MANGLE_CLOSURE_INSTANTIATE_PREFIX << MangleType(funcType);
    return ss.str();
}

std::string GenerateGlobalImplClassMangleName(const FuncBase& func)
{
    std::stringstream ss;
    ss << ReplaceManglePrefixWith(func.GetIdentifierWithoutPrefix(), MANGLE_CLOSURE_FUNC_PREFIX);
    return ss.str();
}

std::string GenerateLambdaImplClassMangleName(const Lambda& func, size_t count)
{
    std::stringstream ss;
    ss << ReplaceManglePrefixWith(func.GetIdentifier(), MANGLE_CLOSURE_LAMBDA_PREFIX) << MANGLE_DOLLAR_PREFIX <<
        count;
    return ss.str();
}

std::string GenerateWrapperClassMangleName(const ClassDef &def)
{
    std::stringstream ss;
    // `$C` is a special prefix for closure conversion class declarations. `w` stands for wrapper.
    // `$Cw` is followed by a class (declaration) mangle name.
    ss << MANGLE_CLOSURE_WRAPPER_PREFIX << def.GetIdentifierWithoutPrefix();
    return ss.str();
}

std::string GenerateGenericAbstractFuncMangleName(const ClassDef &def)
{
    return MANGLE_FUNC_PREFIX + def.GetIdentifier() + MANGLE_ABSTRACT_GENERIC_PREFIX;
}

std::string GenerateInstantiatedAbstractFuncMangleName(const ClassDef &def)
{
    return MANGLE_FUNC_PREFIX + def.GetIdentifier() + MANGLE_ABSTRACT_INSTANTIATED_PREFIX;
}

std::string GenerateGenericOverrideFuncMangleName(const FuncBase &func)
{
    return ReplaceManglePrefixWith(func.GetIdentifierWithoutPrefix(), MANGLE_FUNC_PREFIX) + MANGLE_GENERIC_PREFIX;
}

std::string GenerateInstOverrideFuncMangleName(const FuncBase &func)
{
    return ReplaceManglePrefixWith(func.GetIdentifierWithoutPrefix(), MANGLE_FUNC_PREFIX) +
        MANGLE_ABSTRACT_INST_PREFIX;
}

std::string GenerateWrapperClassGenericOverrideFuncMangleName(const ClassDef &def)
{
    return MANGLE_FUNC_PREFIX + def.GetIdentifierWithoutPrefix() + MANGLE_GENERIC_PREFIX;
}

std::string GenerateWrapperClassInstOverrideFuncMangleName(const ClassDef &def)
{
    return MANGLE_FUNC_PREFIX + def.GetIdentifierWithoutPrefix() + MANGLE_ABSTRACT_INST_PREFIX;
}
} // namespace ClosureConversion
} // namespace Cangjie::CHIR
