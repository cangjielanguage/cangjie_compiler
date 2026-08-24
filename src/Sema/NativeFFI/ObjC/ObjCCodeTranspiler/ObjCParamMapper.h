// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class for Objective-C code generation.
 */

#ifndef CANGJIE_SEMA_NATIVE_FFI_OBJC_INTEROP_PARAM_MAPPER
#define CANGJIE_SEMA_NATIVE_FFI_OBJC_INTEROP_PARAM_MAPPER

#include <fstream>
#include <string_view>
#include <unordered_set>

#include "NativeFFI/ObjC/AfterTypeCheck/Interop/Context.h"
#include "NativeFFI/ObjC/Utils/Handler.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Types.h"
#include "Utils.h"
namespace Cangjie::Interop::ObjC {

using ArgsList = std::vector<std::pair<std::string, std::string>>;

/*
    This class contains a set of static operations on FuncParamLists that
    convert it to ObjC representation of a requested format. We needed some
    entity that is aware of both AST (FuncPramLists) and ObjC syntax
*/
class ObjCParamMapper {
public:
    explicit ObjCParamMapper();

    static ArgsList ConvertParamsListToArgsList(
        std::vector<std::string>& typedefs,
        const std::vector<OwnedPtr<AST::FuncParamList>>& paramLists, bool withRegistryId);
    static std::string ConvertParamsListToArgsListToString(
        const std::vector<OwnedPtr<AST::FuncParamList>>& paramLists, bool withRegistryId);
    static std::vector<std::string> ConvertParamsListToCallableParamsString(
        std::vector<OwnedPtr<AST::FuncParamList>>& paramLists, bool withSelf);

    static std::string GenerateSetterParamLists(const std::string& type);

    static std::string GenerateFuncParamLists(
        std::vector<std::string>& typedefs,
        const std::vector<OwnedPtr<AST::FuncParamList>>& paramLists,
        const std::vector<std::string>& selectorComponents,
        FunctionListFormat format = FunctionListFormat::DECLARATION,
        const ObjCFunctionType type = ObjCFunctionType::INSTANCE,
        bool hasForeignNameAnno = true);
    static std::string GenerateArgumentCast(const AST::Ty& retTy, std::string value);
    static std::string MapCJTypeToObjCType(std::vector<std::string>& typedefs, const AST::Ty& ty);
    static std::string MapCJTypeToObjCType(std::vector<std::string>& typedefs,
        const Ptr<AST::Type>& type);
    static std::string MapCJTypeToObjCType(std::vector<std::string>& typedefs,
        const Ptr<AST::FuncParam>& param);
    static void RegisterTypedef(std::vector<std::string>& typedefs, MappedCType cType);
    static void CollectTypedefs(std::vector<std::string>& typedefs, MappedCType cType);

    static struct EmittableObjCFuncMetainfo GetGetterForProp(
        struct EmittableObjCPropMetainfo prop,
        std::string getterName,
        std::string getterWrapperName,
        bool bridge
    );

    static struct EmittableObjCFuncMetainfo GetSetterForProp(
        struct EmittableObjCPropMetainfo prop, AST::ModalTy ty, std::string getterName, std::string getterWrapperName);

    inline static const std::string ID_TYPE = "id";
private:
    inline static const std::string CAST_TO_VOID_PTR = "(__bridge void*)";
    inline static const std::string CAST_TO_VOID_PTR_RETAINED = "(__bridge_retained void*)";
    inline static const std::string CAST_TO_VOID_PTR_UNSAFE = "(void*)";

    inline static const std::string REGISTRY_ID = "$registryId";
    inline static const std::string SELF_NAME = "self";

    inline static const std::string UNSUPPORTED_TYPE = "UNSUPPORTED_TYPE";
    inline static const std::string INT64_T = "int64_t";
    inline static const std::string SETTER_PARAM_NAME = "value";
    inline static const std::string VOID_TYPE = "void";
    inline static const std::string VOID_POINTER_TYPE = VOID_TYPE + "*";
};

} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_NATIVE_FFI_OBJC_INTEROP_PARAM_MAPPER
