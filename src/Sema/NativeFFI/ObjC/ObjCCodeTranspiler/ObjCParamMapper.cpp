// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements class for objc code generation.
 */

#include "ObjCParamMapper.h"
#include "Emitter.h"
#include "NativeFFI/ObjC/Utils/ASTQuery.h"

namespace Cangjie::Interop::ObjC {

using namespace Cangjie;
using namespace AST;
using namespace Native::FFI;
using std::string;

namespace {
const std::string CAST_TO_VOID_PTR = "(__bridge void*)";
const std::string CAST_TO_VOID_PTR_RETAINED = "(__bridge_retained void*)";
const std::string CAST_TO_VOID_PTR_UNSAFE = "(void*)";

const std::string REGISTRY_ID = "$registryId";
const std::string SELF_NAME = "self";

const std::string UNSUPPORTED_TYPE = "UNSUPPORTED_TYPE";
const std::string INT64_T = "int64_t";
const std::string SETTER_PARAM_NAME = "value";
const std::string VOID_TYPE = "void";
const std::string VOID_POINTER_TYPE = VOID_TYPE + "*";

std::string SelfInfoArgs()
{
    return SELF_NAME + "." + REGISTRY_ID + ", " + CAST_TO_VOID_PTR + SELF_NAME;
}

std::string SelfInfoParamTypes()
{
    return INT64_T + "," + VOID_POINTER_TYPE;
}
} // namespace

ObjCParamMapper::ObjCParamMapper() {}

/*
 *  DECLARATION: :(arg1Type)arg1:(arg2Type)arg2:...(argNType)argN
 *  STATIC_REF: (arg1Type, arg2Type, ...argNType)
 */
std::string ObjCParamMapper::GenerateFuncParamLists(
    std::vector<std::string>& typedefs,
    const std::vector<OwnedPtr<FuncParamList>>& paramLists,
    const std::vector<std::string>& selectorComponents, FunctionListFormat format, const ObjCFunctionType type,
    bool hasForeignNameAnno)
{
    auto componentIterator = std::begin(selectorComponents);
    // skip function name
    componentIterator++;
    std::string genParams = format == FunctionListFormat::DECLARATION ? "" : "(";
    if (paramLists.empty() || !paramLists[0]) {
        return "";
    }
    for (size_t i = 0; i < paramLists[0]->params.size(); i++) {
        OwnedPtr<FuncParam>& cur = paramLists[0]->params[i];
        switch (format) {
            case FunctionListFormat::DECLARATION:
                if (i != 0) {
                    auto name = hasForeignNameAnno ? *componentIterator++ : "";
                    genParams += name + ":"; // label
                } else {
                    genParams += ":";
                }
                genParams += "(" + MapCJTypeToObjCType(typedefs, *cur->GetTy()) + ")";
                genParams += cur->identifier.Val();
                if (i != paramLists[0]->params.size() - 1) {
                    genParams += " ";
                }
                break;
            case FunctionListFormat::STATIC_REF:
                if (paramLists[0]->params.size() == 0 && type != ObjCFunctionType::STATIC) {
                    genParams += VOID_POINTER_TYPE;
                }
                genParams += ")";
                break;
            case FunctionListFormat::CANGJIE_DECL:
                genParams += cur->identifier.Val() + ": " + Ty::ToString(cur->type->DataTy());
                if (i != paramLists[0]->params.size() - 1) {
                    genParams += ", ";
                }
                break;
            default:
                break;
        }
    }
    if (format == FunctionListFormat::STATIC_REF || format == FunctionListFormat::CANGJIE_DECL) {
        if (paramLists[0]->params.size() == 0 && type != ObjCFunctionType::STATIC) {
            genParams += VOID_POINTER_TYPE;
        }
        genParams += ")";
    }

    return genParams;
}

ArgsList ObjCParamMapper::ConvertParamsListToArgsList(
    std::vector<std::string>& typedefs,
    const std::vector<OwnedPtr<FuncParamList>>& paramLists, bool withRegistryId)
{
    ArgsList result = ArgsList();

    if (withRegistryId) {
        result.emplace_back(std::pair<std::string, std::string>(
            INT64_T, string(SELF_NAME) + "." + REGISTRY_ID)
        );
        result.emplace_back(VOID_POINTER_TYPE, CAST_TO_VOID_PTR + SELF_NAME);
    }

    if (!paramLists.empty() && paramLists[0]) {
        for (size_t i = 0; i < paramLists[0]->params.size(); i++) {
            OwnedPtr<FuncParam>& cur = paramLists[0]->params[i];
            auto name = cur->identifier.Val();
            name = GenerateArgumentCast(*cur->GetTy(), std::move(name));
            result.push_back(std::pair<std::string, std::string>(MapCJTypeToObjCType(typedefs, cur), name));
        }
    }
    return result;
}

std::string ObjCParamMapper::ConvertParamsListToArgsListToString(
    const std::vector<OwnedPtr<AST::FuncParamList>>& paramLists, bool withSelfInfo)
{
    std::string result = "";

    if (withSelfInfo) {
        result = SelfInfoArgs();
    }

    if (!paramLists.empty() && paramLists[0]) {
        if (withSelfInfo && paramLists[0]->params.size() != 0) {
            result += ", ";
        }
        for (size_t i = 0; i < paramLists[0]->params.size(); i++) {
            OwnedPtr<FuncParam>& cur = paramLists[0]->params[i];
            auto name = cur->identifier.Val();
            name = GenerateArgumentCast(*cur->GetTy(), std::move(name));
            result += name;
            if (i != paramLists[0]->params.size() - 1) {
                result += ", ";
            }
        }
    }
    return result;
}

std::vector<std::string> ObjCParamMapper::ConvertParamsListToCallableParamsString(
    std::vector<OwnedPtr<FuncParamList>>& paramLists, bool withSelf)
{
    std::vector<string> result = {};

    if (withSelf) {
        result.emplace_back(std::string(CAST_TO_VOID_PTR) + SELF_NAME);
    }

    if (!paramLists.empty() && paramLists[0]) {
        for (size_t i = 0; i < paramLists[0]->params.size(); i++) {
            OwnedPtr<FuncParam>& cur = paramLists[0]->params[i];
            std::string name = GenerateArgumentCast(*cur->GetTy(), cur->identifier.Val());
            result.push_back(std::move(name));
        }
    }
    return result;
}

void ObjCParamMapper::RegisterTypedef(std::vector<std::string>& typedefs, MappedCType cType)
{
    if (cType.decl != "") {
        typedefs.emplace_back(cType.decl);
    }
}

void ObjCParamMapper::CollectTypedefs(std::vector<std::string>& typedefs, MappedCType cType)
{
    for (auto child : cType.dependencies) {
        CollectTypedefs(typedefs, child);
    }
    RegisterTypedef(typedefs, cType);
}

std::string ObjCParamMapper::MapCJTypeToObjCType(std::vector<std::string>& typedefs, const Ty& ty)
{
    auto objctype = TypeMapper::Cj2ObjCForObjC(ty);
    for (auto child : objctype.dependencies) {
        CollectTypedefs(typedefs, child);
    }
    RegisterTypedef(typedefs, objctype);
    return objctype.usage;
}

std::string ObjCParamMapper::MapCJTypeToObjCType(std::vector<std::string>& typedefs, const Ptr<Type>& type)
{
    if (!type) {
        return UNSUPPORTED_TYPE;
    }

    return MapCJTypeToObjCType(typedefs, *type->GetTy());
}

std::string ObjCParamMapper::MapCJTypeToObjCType(std::vector<std::string>& typedefs,
    const Ptr<FuncParam>& param)
{
    if (!param) {
        return UNSUPPORTED_TYPE;
    }

    return MapCJTypeToObjCType(typedefs, *param->type->GetTy());
}

std::string ObjCParamMapper::GenerateArgumentCast(const Ty& paramTy, std::string value)
{
    const auto& actualTy = paramTy.IsCoreOptionType() ? *paramTy.typeArgs[0] : paramTy;
    // An @ObjCImpl hands over its reference exactly like an @ObjCMirror does: the Cangjie wrapper the entry
    // point builds around the handle owns it from here on, and releases it in its finalizer. Passing an impl
    // at +0 instead left that wrapper releasing a reference it never took.
    if (IsObjCForeign(actualTy) || IsObjCExport(actualTy) || IsObjCBlock(actualTy)) {
        return CAST_TO_VOID_PTR_RETAINED + std::move(value);
    }
    if (IsObjCPointer(actualTy)) {
        return CAST_TO_VOID_PTR_UNSAFE + std::move(value);
    }
    return value;
}

struct EmittableObjCFuncMetainfo ObjCParamMapper::GetGetterForProp(
    struct EmittableObjCPropMetainfo prop,
    std::string getterName,
    std::string getterWrapperName,
    bool bridge)
{
    EmittableObjCFuncMetainfo getter;
    getter.isStatic             = prop.isStatic;
    getter.selectorComponents   = {};
    getter.identifier           = getterName;
    getter.mangledIdentifier    = getterWrapperName;
    getter.retType              = prop.type;
    getter.paramsDecl           = "";
    getter.paramStaticRef       = prop.isStatic ? "()" : "(" + SelfInfoParamTypes() + ")";
    getter.callingParams        = prop.isStatic ? "" : SelfInfoArgs();
    getter.convertedParams      = prop.isStatic ? "" : SelfInfoArgs();
    getter.bridge               = bridge;
    return getter;
}

struct EmittableObjCFuncMetainfo ObjCParamMapper::GetSetterForProp(
    struct EmittableObjCPropMetainfo prop, ModalTy ty, std::string getterName, std::string getterWrapperName)
{
    EmittableObjCFuncMetainfo setter;
    setter.isStatic             = prop.isStatic;
    setter.selectorComponents   = {};
    setter.identifier           = getterName;
    setter.mangledIdentifier    = getterWrapperName;
    setter.retType              = VOID_TYPE;
    setter.bridge               = false;
    setter.paramsDecl           = "(" + prop.type + ")" + SETTER_PARAM_NAME;
    setter.paramStaticRef       = (!prop.isStatic
                                    ? "(" + SelfInfoParamTypes() + ","
                                    : "(") + prop.type + ")";
    setter.callingParams        = !prop.isStatic
                                    ? SelfInfoArgs() + ", " + SETTER_PARAM_NAME
                                    : SETTER_PARAM_NAME;
    setter.convertedParams      = (!prop.isStatic
                                    ? SelfInfoArgs() + ", "
                                    : "") + GenerateArgumentCast(*ty, SETTER_PARAM_NAME);
    return setter;
}
} // namespace Cangjie::Interop::ObjC
