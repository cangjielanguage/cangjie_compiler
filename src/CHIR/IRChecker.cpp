// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/IRChecker.h"

#include <future>
#include <iostream>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_set>
#include <vector>

#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/ToStringUtils.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/Type/EnumDef.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/CHIR/Value.h"
#include "cangjie/CHIR/Visitor/Visitor.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/TaskQueue.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie::CHIR;

namespace {

bool IsExpectedType(const Type& expectedTy, const Type& curTy)
{
    if (&expectedTy == &curTy) {
        return true;
    }
    if (curTy.IsNothing()) {
        return true;
    }
    auto isRefAny = [](const Type& t1) {
        return t1.IsRef() && Cangjie::StaticCast<const RefType*>(&t1)->GetBaseType()->IsAny();
    };
    if (isRefAny(expectedTy) || isRefAny(curTy)) {
        // Any type should be checked
        return true;
    }
    return false;
}

bool IsExpectedType(const Type& ty, const Type::TypeKind& kind)
{
    if (ty.GetTypeKind() == kind) {
        return true;
    }
    if (ty.IsNothing()) {
        return true;
    }
    return false;
}

bool CheckFuncArgType(const Type& t, bool isCFunc, bool isMutThisType = false)
{
    if (isMutThisType) {
        if (t.IsRef() && !Cangjie::StaticCast<const RefType*>(&t)->GetBaseType()->IsRef()) {
            return true;
        }
        return false;
    }
    if (t.IsValueType() || t.IsFunc() || t.IsGeneric()) {
        // value_type/func/closure/generic pass by value
        return true;
    }
    if (t.IsRef()) {
        if (auto baseType = Cangjie::StaticCast<const RefType*>(&t)->GetBaseType();
            baseType->IsClassOrArray() || baseType->IsBox() || isCFunc) {
            // class and array are treated as reference type
            // in c function, value with reference is valid with inout intrinsic
            return true;
        }
    }
    return false;
}

bool IsAbstractCustomDef(const CustomTypeDef& def)
{
    using namespace Cangjie;
    static auto isInterfaceOrAbstractClass = [](const CustomTypeDef& def) {
        return def.IsClassLike() && (StaticCast<ClassDef*>(&def)->IsInterface() || def.TestAttr(Attribute::ABSTRACT));
    };
    if (isInterfaceOrAbstractClass(def)) {
        return true;
    }
    if (def.IsExtend()) {
        auto extendedDef = StaticCast<ExtendDef*>(&def)->GetExtendedCustomTypeDef();
        // all builtin defs are irrelevant in this case
        return extendedDef != nullptr && isInterfaceOrAbstractClass(*extendedDef);
    }
    return false;
}

bool CheckIfGenericTypeInSet(const Type& type, const std::set<const Type*>& genericTypes)
{
    if (type.IsBox()) {
        return true;
    }
    if (type.IsGeneric()) {
        if (Cangjie::StaticCast<GenericType*>(&type)->skipCheck == true) {
            return true;
        }
        if (genericTypes.count(&type) == 0) {
            return false;
        }
    }
    for (auto it : type.GetTypeArgs()) {
        bool ret = CheckIfGenericTypeInSet(*it, genericTypes);
        if (!ret) {
            return false;
        }
    }
    return true;
}

class IRChecker {
public:
    /** @brief Entry function of the Well-formedness Checker */
    static bool Check(const Package& root, std::ostream& outStream, const Cangjie::GlobalOptions& options,
        CHIRBuilder& builder, const ToCHIR::Phase& phase)
    {
        bool ret = true;
        curCheckPhase = phase;
        auto irChecker = IRChecker(outStream, options, builder);

        ret = ParallelCheck(irChecker, &IRChecker::CheckFunc, root.GetGlobalFuncs()) && ret;
        ret = ParallelCheck(irChecker, &IRChecker::CheckGlobalValue, root.GetGlobalVars()) && ret;
        ret = ParallelCheck(irChecker, &IRChecker::CheckImportedVarAndFuncs, root.GetImportedVarAndFuncs()) && ret;
        ret = ParallelCheck(irChecker, &IRChecker::CheckStruct, root.GetImportedStructs()) && ret;
        ret = ParallelCheck(irChecker, &IRChecker::CheckClass, root.GetImportedClasses()) && ret;
        ret = ParallelCheck(irChecker, &IRChecker::CheckEnum, root.GetImportedEnums()) && ret;
        ret = ParallelCheck(irChecker, &IRChecker::CheckStruct, root.GetStructs()) && ret;
        ret = ParallelCheck(irChecker, &IRChecker::CheckClass, root.GetClasses()) && ret;
        ret = ParallelCheck(irChecker, &IRChecker::CheckEnum, root.GetEnums()) && ret;
        ret = ParallelCheck(irChecker, &IRChecker::CheckExtend, root.GetExtends()) && ret;

        if (root.GetPackageInitFunc() == nullptr) {
            irChecker.Errorln("package " + root.GetName() + " need a global init func.");
            ret = false;
        }
        if (!ret) {
            switch (phase) {
                case ToCHIR::Phase::RAW:
                    irChecker.Errorln("after translation.");
                    break;
                case ToCHIR::Phase::PLUGIN:
                    irChecker.Errorln("after plugin.");
                    break;
                case ToCHIR::Phase::ANALYSIS_FOR_CJLINT:
                    irChecker.Errorln("after analysis for cjlint.");
                    break;
                case ToCHIR::Phase::OPT:
                    irChecker.Errorln("after opt.");
                    break;
            }
        }
        return ret;
    }
    static ToCHIR::Phase curCheckPhase;
private:
    std::ostream& out;
    const Cangjie::GlobalOptions& opts;

    CHIRBuilder& builder;
    explicit IRChecker(std::ostream& outStream, const Cangjie::GlobalOptions& opts, CHIRBuilder& builder)
        : out(outStream), opts(opts), builder(builder) {};

    template <typename F, typename Arg>
    static bool ParallelCheck(IRChecker& irChecker, F check, const std::vector<Arg>& items)
    {
        size_t threadNum = irChecker.opts.GetJobs();

        Cangjie::Utils::TaskQueue taskQueue(threadNum);
        // Place for holding the results.
        std::vector<Cangjie::Utils::TaskResult<bool>> results;

        for (auto item : items) {
            results.emplace_back(
                taskQueue.AddTask<bool>([check, &irChecker, item]() { return (irChecker.*check)(*item); }));
        }

        taskQueue.RunAndWaitForAllTasksCompleted();

        bool res = true;
        for (auto& result : results) {
            res = result.get() && res;
        }
        return res;
    }

    std::unordered_set<std::string> identifiers;
    std::mutex checkIdentMutex;

    void WarningInFunc(const Value* curFunc, const std::string& info) const
    {
        CJC_NULLPTR_CHECK(curFunc);
        Warningln("in function " + curFunc->GetIdentifier() + ", " + info);
    }

    void Errorln(const std::string& info) const
    {
        out << "chir checker error: " + info + "\n";
    }

    void Warningln(const std::string& info) const
    {
        out << "chir checker warning: " + info + "\n";
    }

    void ErrorInFunc(const Value* curFunc, const std::string& info) const
    {
        CJC_NULLPTR_CHECK(curFunc);
        Errorln("in function " + curFunc->GetIdentifier() + ", " + info);
    }
    
    void TypeCheckError(const Value& value, const std::string& expectedTy) const
    {
        auto err = "value " + value.GetIdentifier() + " has type " + value.GetType()->ToString() + " but type " +
            expectedTy + " is expected.";
        ErrorInFunc(GetTopLevelFunc(value), err);
    }

    void TypeCheckError(const Expression& expr, const Value& input, const std::string& expectedTy) const
    {
        std::string location = "";
        if (auto result = expr.GetResult(); result != nullptr) {
            location = result->GetIdentifier();
        } else {
            location = expr.GetExprKindName();
        }
        auto err = "value " + input.GetIdentifier() + " used in " + location + " has type " +
            input.GetType()->ToString() + " but type " + expectedTy + " is expected.";
        ErrorInFunc(expr.GetTopLevelFunc(), err);
    }

    void TypeCheckError(const Expression& expr, size_t index, const Value& child, const std::string& expectedTy) const
    {
        std::string location = "";
        if (auto result = expr.GetResult(); result != nullptr) {
            location = result->GetIdentifier();
        } else {
            location = expr.GetExprKindName();
        }
        auto err = "in " + location + ": the " + std::to_string(index) + "-th element (" + child.GetIdentifier() +
            ") has type " + child.GetType()->ToString() + " but type " + expectedTy + " is expected.";
        ErrorInFunc(expr.GetTopLevelFunc(), err);
    }

    bool MemberFuncCheckError(
        const Value& func, const std::string& expectedTy, const std::string& location = "first parameter type") const
    {
        auto err = "func " + func.GetIdentifier() + " has type " + func.GetType()->ToString() + ", but " + expectedTy +
            " is expected in " + location + ".";
        Errorln(err);
        return false;
    }

    bool CheckIdentifier(const std::string& identifier)
    {
        std::unique_lock<std::mutex> lock(checkIdentMutex);
        if (identifiers.find(identifier) != identifiers.end()) {
            Errorln("Duplicated identifier `" + identifier + "` found.");
            return false;
        }
        identifiers.insert(identifier);
        return true;
    }

    // globalValue mangledName can not be the same as any other node.
    bool CheckGlobalValue(const GlobalVar& value)
    {
        auto ret = CheckIdentifier(value.GetIdentifier());
        ret = CheckValueReferenceType(value) && ret;

        bool noInitializer = value.GetInitFunc() == nullptr && value.GetInitializer() == nullptr;
        if (noInitializer) {
            if (value.TestAttr(Attribute::COMMON)) {
                // common can have no initalizer only when compiling common part.
                return ret;
            }
            Errorln("GlobalVar " + value.GetIdentifier() + " should have either initializer or initFunc");
            ret = false;
        }
        return ret;
    }

    bool CheckImportedVarAndFuncs(const ImportedValue& value)
    {
        auto ret = CheckValueReferenceType(value);
        if (value.GetType()->IsFunc() && Cangjie::StaticCast<FuncType*>(value.GetType())->IsCFunc()) {
            return ret;
        }
        ret = CheckIdentifier(value.GetIdentifier()) && ret;
        return ret;
    }

    bool CheckDeclaredParent(const Value& val, const CustomTypeDef& expectedParent) const
    {
        if (auto var = Cangjie::DynamicCast<const GlobalVarBase*>(&val)) {
            CJC_ASSERT(var->TestAttr(Attribute::STATIC));
            auto realParent = var->GetParentCustomTypeDef();
            if (realParent != &expectedParent) {
                auto err = "static member var " + var->GetIdentifier() + " in " + expectedParent.GetIdentifier();
                if (realParent == nullptr) {
                    err += " doesn't have declared parent.";
                } else {
                    err += " has wrong declared parent: " + realParent->GetIdentifier();
                }
                Errorln(err);
                return false;
            }
        } else if (auto func = Cangjie::DynamicCast<const FuncBase*>(&val)) {
            auto realParent = func->GetParentCustomTypeDef();
            if (realParent != &expectedParent) {
                auto err = "member func " + func->GetIdentifier() + " in " + expectedParent.GetIdentifier();
                if (realParent == nullptr) {
                    err += " doesn't have declared parent.";
                } else {
                    err += " has wrong declared parent: " + realParent->GetIdentifier();
                }
                Errorln(err);
                return false;
            }
        } else {
            auto err = "value " + val.GetIdentifier() + " in " + expectedParent.GetIdentifier() +
                ", shouldn't check declared parent.";
            Errorln(err);
            return false;
        }
        return true;
    }

    bool CheckCustomTypeDefPackageName(const CustomTypeDef& def) const
    {
        if (def.GetGenericDecl() && def.GetGenericDecl()->GetPackageName() != def.GetPackageName()) {
            std::string msg = "customDef package name error, current def " + def.GetIdentifier() +
                ", current package name is " + def.GetPackageName() + ", expected package name is " +
                def.GetGenericDecl()->GetPackageName();
            Errorln(msg);
            return false;
        }
        return true;
    }
    bool CheckCustomTypeDefCommonRules(const CustomTypeDef& def) const
    {
        bool ret = true;
        ret = CheckCustomTypeDefPackageName(def);
        for (auto func : def.GetMethods()) {
            ret = CheckDeclaredParent(*func, def) && ret;
        }
        for (auto var : def.GetStaticMemberVars()) {
            ret = CheckValueReferenceType(*var) && ret;
            ret = CheckDeclaredParent(*var, def) && ret;
        }
        for (auto var : def.GetAllInstanceVars()) {
            ret = CheckMemberVarReferenceType(var, def.GetIdentifier()) && ret;
        }
        return ret;
    }

    void DumpVTableError(const CustomTypeDef& leafDef, const CustomTypeDef& rootDef, const std::string& msg) const
    {
        std::stringstream ss;
        ss << msg << ", derived and base vtables are described below:\n";
        ss << leafDef.ToString() << "\n";
        ss << rootDef.ToString() << "\n";
        Errorln(ss.str());
    }

    void DumpVTableErrorFuncInfo(const CustomTypeDef& customDef, const std::string& funcName,
        const std::string& funcTypeName, const std::string& wrongInfo) const
    {
        auto err = CustomTypeKindToString(customDef) + " " + customDef.GetIdentifier() +
            " has error in vtable, api name: " + funcName + ", api type: " + funcTypeName + ", msg: " + wrongInfo + ".";
        Errorln(err);
    }

    bool CheckImportedCustomDef(const CustomTypeDef& customDef) const
    {
        if (!customDef.TestAttr(Attribute::IMPORTED)) {
            CJC_ABORT();
            return false;
        }
        for (auto& thisVtableIter : customDef.GetVTable()) {
            auto vtableKey = thisVtableIter.first;
            for (size_t funcId = 0; funcId < thisVtableIter.second.size(); ++funcId) {
                if (thisVtableIter.second[funcId].instance) {
                    bool imported = thisVtableIter.second[funcId].instance->TestAttr(Attribute::IMPORTED);
                    bool vImported = thisVtableIter.second[funcId].attr.TestAttr(Attribute::IMPORTED);
                    if (vImported != imported) {
                        DumpVTableError(customDef, *vtableKey->GetCustomTypeDef(), "vtable import error");
                        return false;
                    }
                    if (!thisVtableIter.second[funcId].instance->GetParentCustomTypeDef()) {
                        std::string msg = "vtable instance parent error " +
                            thisVtableIter.second[funcId].instance->GetIdentifier();
                        DumpVTableError(customDef, *vtableKey->GetCustomTypeDef(), msg);
                        return false;
                    }
                    if (auto f = Cangjie::DynamicCast<Func*>(thisVtableIter.second[funcId].instance);
                        f && !f->GetBody()) {
                        std::string msg = "vtable instance func body error " +
                            thisVtableIter.second[funcId].instance->GetIdentifier();
                        DumpVTableError(customDef, *vtableKey->GetCustomTypeDef(), msg);
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool CheckVTable(const CustomTypeDef& customDef)
    {
        if (customDef.TestAttr(Attribute::IMPORTED)) {
            return CheckImportedCustomDef(customDef);
        }
        for (auto& thisVtableIter : customDef.GetVTable()) {
            auto vtableKey = thisVtableIter.first;
            // only interface and class can be inherited
            auto& parentVTable = vtableKey->GetClassDef()->GetVTable();
            auto parentKey = vtableKey->GetCustomTypeDef()->GetType();
            auto parentVTableIter = parentVTable.find(Cangjie::StaticCast<ClassType*>(parentKey));
            if (parentVTableIter == parentVTable.end()) {
                DumpVTableError(customDef, *vtableKey->GetCustomTypeDef(), "vtable match fail");
                return false;
            }
            if (parentVTableIter->second.size() != thisVtableIter.second.size()) {
                DumpVTableError(customDef, *vtableKey->GetCustomTypeDef(), "wrong vtable size");
                return false;
            }
            for (size_t funcId = 0; funcId < thisVtableIter.second.size(); ++funcId) {
                if (auto f = Cangjie::DynamicCast<Func*>(thisVtableIter.second[funcId].instance); f) {
                    if (!f->GetBody()) {
                        std::string msg = "vtable instance func body error " +
                            thisVtableIter.second[funcId].instance->GetIdentifier();
                        DumpVTableError(customDef, *vtableKey->GetCustomTypeDef(), msg);
                        return false;
                    }
                }
                auto thisFuncInfo = thisVtableIter.second[funcId].typeInfo.originalType;
                auto parentFuncInfo = parentVTableIter->second[funcId].typeInfo.originalType;
                auto& funcName = thisVtableIter.second[funcId].srcCodeIdentifier;
                auto funcType = thisVtableIter.second[funcId].typeInfo.originalType->ToString();
                // interface and extend can have unimplemented api
                if (!IsAbstractCustomDef(customDef) && thisVtableIter.second[funcId].instance == nullptr) {
                    DumpVTableErrorFuncInfo(customDef, funcName, funcType, "unimplemented api in vtable");
                    return false;
                }
                if (thisFuncInfo->GetParamTypes().size() != parentFuncInfo->GetParamTypes().size()) {
                    DumpVTableErrorFuncInfo(customDef, funcName, funcType, "wrong arg size");
                    return false;
                }
                if (thisVtableIter.second[funcId].instance == nullptr) {
                    // abstract function do not check its params
                    continue;
                }
                size_t offset = 0;
                if (!thisVtableIter.second[funcId].instance->TestAttr(Attribute::STATIC)) {
                    // check this type if function is marked non-static
                    if (!CheckTypeMatchInVTable(
                        *thisFuncInfo->GetParamType(0), *parentFuncInfo->GetParamType(0), true, true)) {
                        DumpVTableErrorFuncInfo(customDef, funcName, funcType, "wrong arg type");
                        return false;
                    }
                    offset = 1;
                }
                for (size_t paramId = offset; paramId < thisFuncInfo->GetParamTypes().size(); ++paramId) {
                    if (!CheckTypeMatchInVTable(
                        *thisFuncInfo->GetParamType(paramId), *parentFuncInfo->GetParamType(paramId), true)) {
                        DumpVTableErrorFuncInfo(customDef, funcName, funcType, "wrong arg type");
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool CheckIfGenericMemberValid(const CustomTypeDef& def)
    {
        bool ret = true;
        auto genericArgs = def.GetGenericTypeParams();
        std::set<const Type*> genericTypes{genericArgs.begin(), genericArgs.end()};
        for (auto it : def.GetDirectInstanceVars()) {
            if (!it.type->IsGenericRelated()) {
                continue;
            }
            bool res = CheckIfGenericTypeInSet(*it.type, genericTypes);
            if (!res) {
                Errorln("Generic Type of member " + it.name + ": " + it.type->ToString() + " is not found in " +
                    def.GetIdentifier());
            }
            ret = ret && res;
        }
        for (auto it : def.GetStaticMemberVars()) {
            if (!it->GetType()->IsGenericRelated()) {
                continue;
            }
            bool res = CheckIfGenericTypeInSet(*it->GetType(), genericTypes);
            if (!res) {
                Errorln("Generic Type of member " + it->GetSrcCodeIdentifier() + ": " + it->GetType()->ToString() +
                    " is not found in " + def.GetIdentifier());
            }
            ret = ret && res;
        }
        return ret;
    }

    // struct mangledName can not be the same as other node.
    // struct mut function or constructor first input must be Struct&.
    // struct not mut function and not construcor first input must be Struct.
    // struct constructor return type must be Unit
    bool CheckStruct(const StructDef& structT)
    {
        bool ret = CheckIdentifier(structT.GetIdentifier());
        for (auto& func : structT.GetMethods()) {
            if (func->TestAttr(Attribute::STATIC)) {
                continue;
            }
            std::string expectTy = "Struct-" + structT.GetIdentifierWithoutPrefix();
            if (func->IsConstructor() || func->TestAttr(Attribute::MUT) || func->IsInstanceVarInit()) {
                expectTy = expectTy + "&";
            }
            CJC_ASSERT(func->GetType()->IsFunc());
            auto funcTy = Cangjie::StaticCast<FuncType*>(func->GetType());
            if (funcTy->GetParamTypes().size() == 0) {
                ret = MemberFuncCheckError(*func, expectTy) && ret;
                continue;
            }
            Type* baseTy = funcTy->GetParamTypes()[0];
            if (IsConstructor(*func) || func->TestAttr(Attribute::MUT) || func->IsInstanceVarInit()) {
                if (!baseTy->IsRef()) {
                    ret = MemberFuncCheckError(*func, expectTy) && ret;
                    continue;
                }
                baseTy = Cangjie::StaticCast<RefType*>(baseTy)->GetBaseType();
            }
            if (!func->Get<WrappedRawMethod>()) {
                if (!baseTy->IsStruct()) {
                    ret = MemberFuncCheckError(*func, expectTy) && ret;
                    continue;
                }
                if (baseTy != structT.GetType()) {
                    ret = MemberFuncCheckError(*func, expectTy) && ret;
                    continue;
                }
            }
            if (func->IsConstructor() && !funcTy->GetReturnType()->IsVoid()) {
                ret = MemberFuncCheckError(*func, "Void", "return type") && ret;
            }
        }
        ret = CheckCustomTypeDefCommonRules(structT) && ret;
        ret = CheckVTable(structT) && ret;
        ret = CheckIfGenericMemberValid(structT) && ret;
        if (!ret) {
            structT.Dump();
        }
        return ret;
    }

    bool CheckAbstractParamInfo(const ClassDef& classT) const
    {
        for (auto& method : classT.GetAbstractMethods()) {
            if (Cangjie::StaticCast<FuncType*>(method.methodTy)->GetParamTypes().size() != method.paramInfos.size()) {
                return false;
            }
            for (size_t i = 0; i < method.paramInfos.size(); ++i) {
                if (method.paramInfos[i].type != Cangjie::StaticCast<FuncType*>(method.methodTy)->GetParamType(i)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool CheckTypeMatchInVTable(const Type& leaf, const Type& root, bool isFirst, bool isThisType = false) const
    {
        // this type match if both are class refernce types or types are same
        // other parameters match if types are same
        auto isRef = [](const Type& t) {
            return (t.IsRef() && Cangjie::StaticCast<RefType*>(&t)->GetBaseType()->IsClass()) || t.IsGeneric();
        };
        if ((root.IsGeneric() && isRef(leaf)) || (leaf.IsGeneric() && isRef(root))) {
            return true;
        }
        if (&leaf == &root) {
            return true;
        }
        // leaf: Class-CA<Int64>&, root: Class-CA<T>&
        if (!isFirst && root.IsGeneric()) {
            return true;
        }
        if (isThisType && leaf.StripAllRefs()->IsClass() && root.StripAllRefs()->IsClass()) {
            return true;
        }
        if (leaf.GetTypeKind() != root.GetTypeKind()) {
            return false;
        }
        if ((leaf.IsClass() || leaf.IsStruct() || leaf.IsEnum()) &&
            Cangjie::StaticCast<CustomType*>(&leaf)->GetCustomTypeDef() !=
            Cangjie::StaticCast<CustomType*>(&root)->GetCustomTypeDef()) {
            return false;
        }
        auto t1ArgTys = leaf.GetTypeArgs();
        auto t2ArgTys = root.GetTypeArgs();
        if (t1ArgTys.size() != t2ArgTys.size()) {
            return false;
        }
        bool res = true;
        for (size_t i = 0; i < t1ArgTys.size(); ++i) {
            res = res & CheckTypeMatchInVTable(*t1ArgTys[i], *t2ArgTys[i], false, isThisType);
        }
        return res;
    }

    // class mangledName can not be same with other node.
    // class member function first input must be Class&
    // class construcor return type must be Unit
    // class vtable can not has abstract function
    bool CheckClass(const ClassDef& classT)
    {
        bool ret = CheckIdentifier(classT.GetIdentifier());
        for (auto& func : classT.GetMethods()) {
            if (func->TestAttr(Attribute::STATIC)) {
                continue;
            }
            std::string expectTy = "Class-" + classT.GetIdentifierWithoutPrefix() + "&";
            CJC_ASSERT(func->GetType()->IsFunc());
            auto funcTy = Cangjie::StaticCast<FuncType*>(func->GetType());
            if (funcTy->GetParamTypes().size() == 0) {
                ret = MemberFuncCheckError(*func, expectTy) && ret;
                continue;
            }
            Type* param0Ty = funcTy->GetParamTypes()[0];
            if (!param0Ty->IsRef()) {
                ret = MemberFuncCheckError(*func, expectTy) && ret;
                continue;
            }
            auto param0BaseTy = Cangjie::StaticCast<RefType*>(param0Ty)->GetBaseType();
            if (!param0BaseTy->IsClass()) {
                ret = MemberFuncCheckError(*func, expectTy) && ret;
                continue;
            }
            if (param0BaseTy != classT.GetType()) {
                ret = MemberFuncCheckError(*func, expectTy) && ret;
                continue;
            }
            if (func->IsConstructor()) {
                if (func->TestAttr(Attribute::STATIC) && !funcTy->GetReturnType()->IsUnit()) {
                    ret = MemberFuncCheckError(*func, "Unit", "return type") && ret;
                } else if (!funcTy->GetReturnType()->IsVoid()) {
                    ret = MemberFuncCheckError(*func, "Void", "return type") && ret;
                }
            }
            if (func->IsFinalizer() && !funcTy->GetReturnType()->IsVoid()) {
                ret = MemberFuncCheckError(*func, "Void", "return type") && ret;
            }
        }
        for (auto var : classT.GetStaticMemberVars()) {
            ret = CheckValueReferenceType(*var) && ret;
        }
        for (auto var : classT.GetAllInstanceVars()) {
            ret = CheckMemberVarReferenceType(var, classT.GetIdentifier()) && ret;
        }
        ret = CheckCustomTypeDefCommonRules(classT) && ret;
        ret = CheckVTable(classT) && ret;
        ret = CheckIfGenericMemberValid(classT) && ret;
        if (classT.IsInterface()) {
            ret = ret && CheckInterface(classT);
        }
        if (!ret) {
            classT.Dump();
        }
        return ret;
    }

    // member func(include instance member and static member) in interface should be abstract.
    bool CheckInterface(const ClassDef& classT) const
    {
        bool ret = true;
        // skip check the anno factory func in interface.
        for (auto& method : classT.GetAbstractMethods()) {
            if (!method.TestAttr(Attribute::ABSTRACT)) {
                auto err = "func: " + method.methodName + " in Interface:" + classT.GetIdentifier() +
                    "should has attribute: ABSTRACT";
                Errorln(err);
                ret = false;
            }
        }
        return ret;
    }

    // enum mangledName can not be same with other node.
    // enum member function first input must be Enum
    bool CheckEnum(const EnumDef& enumT)
    {
        bool ret = CheckIdentifier(enumT.GetIdentifier());
        for (auto& func : enumT.GetMethods()) {
            if (func->TestAttr(Attribute::STATIC)) {
                continue;
            }
            std::string expectTy = "Enum-" + enumT.GetIdentifierWithoutPrefix();
            CJC_ASSERT(func->GetType()->IsFunc());
            auto funcTy = Cangjie::StaticCast<FuncType*>(func->GetType());
            if (funcTy->GetParamTypes().size() == 0) {
                ret = MemberFuncCheckError(*func, expectTy) && ret;
                continue;
            }
            if (!func->Get<WrappedRawMethod>()) {
                Type* baseTy = funcTy->GetParamTypes()[0];
                if (!baseTy->IsEnum()) {
                    ret = MemberFuncCheckError(*func, expectTy) && ret;
                    continue;
                }
                if (baseTy != enumT.GetType()) {
                    ret = MemberFuncCheckError(*func, expectTy) && ret;
                    continue;
                }
            }
        }
        ret = CheckCustomTypeDefCommonRules(enumT) && ret;
        ret = CheckVTable(enumT) && ret;
        ret = CheckIfGenericMemberValid(enumT) && ret;
        if (!ret) {
            enumT.Dump();
        }
        return ret;
    }

    bool CheckExtend(const ExtendDef& extendT)
    {
        // can't check identifier, maybe repeat, need Sema to fix
        // CHIR will create some extend defs in the end, it's a hack for codegen, will be removed later
        if (extendT.TestAttr(Attribute::COMPILER_ADD)) {
            return true;
        }
        bool ret = CheckVTable(extendT);
        ret = CheckIfGenericMemberValid(extendT) && ret;
        if (!ret) {
            extendT.Dump();
        }
        return ret;
    }

    // 1. type of value semantics, its reference level is one at most
    //    e.g. `int64` and `int64&` are ok, `int64&&` is wrong type
    // 2. type of ref semantics, its reference level is two at most
    //    e.g. `class`, `class&` and `class&&` are ok, `class&&&` is wrong
    bool CheckRefType(const Type& srcType, const std::string& extraMsg) const
    {
        if (!srcType.IsRef()) {
            return true;
        }
        // the first-level ref type
        auto baseType = Cangjie::StaticCast<const RefType&>(srcType).GetBaseType();
        if (!baseType->IsRef()) {
            return true;
        }
        // the second-level ref type
        baseType = Cangjie::StaticCast<RefType*>(baseType)->GetBaseType();
        if (baseType->IsRef()) {
            std::string err = srcType.ToString() + " has more than two level reference, " + extraMsg;
            Errorln(err);
            return false;
        }
        if (!baseType->IsClassOrArray() && !baseType->IsBox()) {
            std::string err =
                srcType.ToString() + " has two level reference, " + extraMsg + ", but base type isn't class or array.";
            Errorln(err);
            return false;
        }
        return true;
    }

    bool CheckMemberVarReferenceType(const MemberVarInfo& memberVar, const std::string& extraMsg) const
    {
        CJC_NULLPTR_CHECK(memberVar.type);
        CJC_ASSERT(!memberVar.name.empty());
        CJC_ASSERT(!extraMsg.empty());
        return CheckRefType(*memberVar.type, "in member " + memberVar.name + " of " + extraMsg);
    }

    bool CheckValueReferenceType(const Value& value) const
    {
        CJC_NULLPTR_CHECK(value.GetType());
        CJC_ASSERT(!value.GetIdentifier().empty());
        return CheckRefType(*value.GetType(), "in value " + value.GetIdentifier());
    }

    bool CheckReferenceTypeInFunc(const Func& func) const
    {
        auto ret = CheckValueReferenceType(func);
        for (auto param : func.GetParams()) {
            ret = CheckValueReferenceType(*param) && ret;
        }
        Visitor::Visit(*func.GetBody(), [this, &ret](Expression& expr) {
            if (!expr.IsTerminator()) {
                ret = CheckValueReferenceType(*expr.GetResult()) && ret;
            }
            return VisitResult::CONTINUE;
        });
        return ret;
    }

    bool CheckLocalId(const BlockGroup& blockGroup, std::unordered_set<std::string>& idSet) const
    {
        const std::vector<Block*>& blocks = blockGroup.GetBlocks();
        for (const Block* block : blocks) {
            const std::vector<Expression*>& expressions = block->GetExpressions();
            for (const Expression* expr : expressions) {
                if (!expr->GetResult()) {
                    continue;
                }
                const std::string& id = expr->GetResult()->GetIdentifier();
                if (id.empty()) {
                    CJC_NULLPTR_CHECK(block->GetTopLevelFunc());
                    Errorln("The result of expression " + expr->ToString() + " in the func " +
                        block->GetTopLevelFunc()->GetIdentifier() + " does not have identifier.");
                    return false;
                }
                if (!idSet.insert(id).second) {
                    CJC_NULLPTR_CHECK(block->GetTopLevelFunc());
                    Errorln(block->GetTopLevelFunc()->GetIdentifier() + " has duplicate id:" + id);
                    return false;
                }
            }
        }
        return true;
    }

    bool CheckApplyExtraTypes(const std::vector<Type*>& instantiateArgs, const Type* thisType,
        const std::set<const Type*>& visibleGenericTypes, const Expression& expr) const
    {
        bool ret = true;
        auto errInfoSuffix = "generic type is unknown in expression " +
            expr.GetResult()->ToString() + ", of function " + expr.GetTopLevelFunc()->GetIdentifier();
        for (size_t i = 0; i < instantiateArgs.size(); ++i) {
            if (!CheckIfGenericTypeInSet(*instantiateArgs[i], visibleGenericTypes)) {
                auto errInfo = "In instantiated type args, " + std::to_string(i) + "-th arg's " + errInfoSuffix;
                Errorln(errInfo);
                ret = false;
            }
        }
        if (thisType != nullptr) {
            if (!CheckIfGenericTypeInSet(*thisType, visibleGenericTypes)) {
                auto errInfo = "Instantiated This type's " + errInfoSuffix;
                Errorln(errInfo);
                ret = false;
            }
        }
        return ret;
    }

    bool CheckGenericTypeValidInFunc(BlockGroup& body, const std::set<const GenericType*>& genericArgs) const
    {
        bool ret = true;
        auto genericTypes = std::set<const Type*>(genericArgs.begin(), genericArgs.end());
        auto func = body.GetTopLevelFunc();
        CJC_NULLPTR_CHECK(func);
        if (func->GetFuncKind() == FuncKind::DEFAULT_PARAMETER_FUNC) {
            //  need check default parameter func
            return ret;
        }
        if (func->GetParentCustomTypeDef() != nullptr) {
            auto def = func->GetParentCustomTypeDef();
            for (auto it : def->GetGenericTypeParams()) {
                genericTypes.insert(it);
            }
        }
        // Check func body
        Visitor::Visit(body, [this, &ret, &genericTypes](Expression& expr) {
            if (expr.GetResult() == nullptr) {
                return VisitResult::CONTINUE;
            }
            if (expr.GetExprKind() == ExprKind::LAMBDA) {
                auto& lambda = Cangjie::StaticCast<Lambda&>(expr);
                auto genericTypesWithLamdba = std::set<const Type*>(lambda.GetGenericTypeParams().begin(),
                    lambda.GetGenericTypeParams().end());
                genericTypesWithLamdba.insert(genericTypes.begin(), genericTypes.end());
                if (!CheckIfGenericTypeInSet(*lambda.GetResult()->GetType(), genericTypesWithLamdba)) {
                    ret = false;
                    Errorln("Generic Type of labmda " + lambda.GetIdentifier() + " is not found in function " +
                        lambda.GetTopLevelFunc()->GetIdentifier());
                }
                // exprs in Lambda body will be check in 'CheckLambda'
                return VisitResult::SKIP;
            }
            if (!CheckIfGenericTypeInSet(*expr.GetResult()->GetType(), genericTypes)) {
                ret = false;
                Errorln("Generic Type of variable " + expr.GetResult()->ToString() + " is not found in function " +
                    expr.GetTopLevelFunc()->GetIdentifier());
            }
            if (expr.GetExprKind() == Cangjie::CHIR::ExprKind::APPLY) {
                auto& apply = Cangjie::StaticCast<Apply&>(expr);
                auto instantiatedTypeArgs = apply.GetInstantiatedTypeArgs();
                if (!CheckApplyExtraTypes(instantiatedTypeArgs, apply.GetThisType(), genericTypes, expr)) {
                    ret = false;
                }
            } else if (expr.GetExprKind() == Cangjie::CHIR::ExprKind::APPLY_WITH_EXCEPTION) {
                auto& apply = Cangjie::StaticCast<ApplyWithException&>(expr);
                auto instantiatedTypeArgs = apply.GetInstantiatedTypeArgs();
                if (!CheckApplyExtraTypes(instantiatedTypeArgs, apply.GetThisType(), genericTypes, expr)) {
                    ret = false;
                }
            }
            return VisitResult::CONTINUE;
        });
        // Check Params
        const auto& params = GetFuncParams(body);
        for (auto param : params) {
            if (!param->GetType()->IsGenericRelated()) {
                continue;
            }
            bool res = CheckIfGenericTypeInSet(*param->GetType(), genericTypes);
            if (!res) {
                Errorln("Generic Type of params " + param->ToString() + " is not found in function " +
                    func->GetIdentifier());
            }
            ret = ret && res;
        }
        return ret;
    }

    // func mangledName can not be same with other node.
    bool CheckFunc(const Func& func)
    {
        if (func.TestAttr(Attribute::SKIP_ANALYSIS)) {
            return true; // Nothing to visit
        }
        auto ret = CheckIdentifier(func.GetIdentifier());
        if (!func.GetBody()) {
            auto err = "func " + func.GetIdentifier() + " doesn't have body";
            Errorln(err);
            ret = false;
        }
        if (!func.GetFuncType()) {
            auto err = "func " + func.GetIdentifier() + " doesn't have type";
            Errorln(err);
            ret = false;
        }
        ret = CheckFuncParams(func.GetParams(), *func.GetFuncType(), func) && ret;
        std::set<const GenericType*> genericTys = {
            func.GetGenericTypeParams().begin(), func.GetGenericTypeParams().end()};
        if (func.GetBody()) {
            ret = CheckFuncBody(*func.GetBody(), genericTys) && ret;
        }
        ret = CheckFuncRetValue(func, *func.GetFuncType()->GetReturnType(), func.GetReturnValue()) && ret;
        ret = CheckReferenceTypeInFunc(func) && ret;
        ret = CheckGenericTypeValidInFunc(*func.GetBody(), genericTys) && ret;
        if (func.IsGVInit() && !func.GetFuncType()->GetReturnType()->IsVoid()) {
            auto err =
                "func " + func.GetIdentifier() + " has type " + func.GetType()->ToString() + ", but Void is expected";
            Errorln(err);
            ret = false;
        }
        if (func.TestAttr(Attribute::ABSTRACT)) {
            auto err = "func " + func.GetIdentifier() + " should not has attribute: ABSTRACT";
            Errorln(err);
            ret = false;
        }
        if (!ret) {
            func.Dump();
        }
        return ret;
    }

    bool CheckFuncParams(
        const std::vector<Parameter*>& params, const FuncType& funcType, const Func& parentFunc) const
    {
        std::string paramsStr = "(";
        for (size_t loop = 0; loop < params.size(); loop++) {
            if (loop > 0) {
                paramsStr += ", ";
            }
            paramsStr += params[loop]->GetType()->ToString();
        }
        paramsStr += ")";
        // check paramTypes equal to funcBody param types
        auto paramTys = funcType.GetParamTypes();
        if (paramTys.size() != params.size()) {
            auto paramSize = "param size is " + std::to_string(params.size());
            auto funcParamSize = "param size is " + std::to_string(paramTys.size());
            auto errInfo = paramSize + " in param type " + paramsStr +
                "but " + funcParamSize + " in funcType: " + funcType.ToString() + ".";
            ErrorInFunc(&parentFunc, errInfo);
            return false;
        }
        for (size_t i = 0; i < paramTys.size(); i++) {
            if (paramTys[i] != params[i]->GetType()) {
                auto errInfo = std::to_string(i) + "-th param in params type " +
                    paramsStr + " mismatch funcType: " + funcType.ToString() + ".";
                ErrorInFunc(&parentFunc, errInfo);
                return false;
            }
        }

        return true;
    }

    // funcBody parameters and func input must have same size and same type.
    // funcBody blocks must have more than one exit or raise
    // funcBody return type must be same with funcBody block exit type.
    bool CheckFuncBody(BlockGroup& body, std::set<const GenericType*>& genericTypes, bool isGlobalBody = true)
    {
        bool ret = true;
        std::unordered_set<std::string> idSet;
        if (body.TestAttr(Attribute::SKIP_ANALYSIS)) {
            return true;
        }
        Visitor::Visit(body, [this, &ret, &idSet](BlockGroup& blockGroup) {
            ret = CheckBlockGroup(blockGroup) && ret;
            ret = CheckLocalId(blockGroup, idSet) && ret;
            return VisitResult::CONTINUE;
        });

        Visitor::Visit(body, [this, &ret](Block& block) {
            ret = CheckBlock(block) && ret;
            return VisitResult::CONTINUE;
        });
        // if control flow is right and current is not the body of the local lambda,
        // check expr operand use-defined access.
        if (ret && isGlobalBody) {
            std::vector<Value*> values;
            const auto& params = GetFuncParams(body);
            for (auto param : params) {
                values.emplace_back(param);
            }
            ret = ExprOperandCheck(body, values) && ret;
        }
        Visitor::Visit(body, [this, &ret, &genericTypes](Expression& expr) {
            ret = CheckExpression(expr, genericTypes) && ret;
            return VisitResult::CONTINUE;
        });
        return ret;
    }

    bool CheckFuncRetValue(const Func& func, const Type& expTy, const Value* retValue) const
    {
        if (retValue == nullptr) {
            if (!expTy.IsUnit() && !expTy.IsNothing() && !expTy.IsVoid()) {
                Errorln(func.GetIdentifier() + " return type Unit is expected.");
                return false;
            }
        } else {
            if (!retValue->GetType()->IsRef()) {
                TypeCheckError(*retValue, expTy.ToString() + "&");
                return false;
            }
            auto baseTy = Cangjie::StaticCast<RefType*>(retValue->GetType())->GetBaseType();
            if (!IsExpectedType(expTy, *baseTy)) {
                TypeCheckError(*retValue, expTy.ToString() + "&");
                return false;
            }
        }
        return true;
    }

    // block group must have block and entry block.
    bool CheckBlockGroup(const BlockGroup& blockGroup) const
    {
        auto curFunc = blockGroup.GetTopLevelFunc();
        CJC_NULLPTR_CHECK(curFunc);
        std::string location = curFunc->GetIdentifier();
        if (!blockGroup.GetUsers().empty()) {
            auto result = blockGroup.GetUsers()[0]->GetResult();
            CJC_NULLPTR_CHECK(result);
            location = result->GetIdentifier();
        }
        auto blocks = blockGroup.GetBlocks();
        if (blocks.size() == 0) {
            ErrorInFunc(blockGroup.GetTopLevelFunc(), "block group in " + location + " has no block.");
            return false;
        } else if (blockGroup.GetEntryBlock() == nullptr) {
            ErrorInFunc(blockGroup.GetTopLevelFunc(), "block group in " + location + " has no entry block.");
            return false;
        }
        bool hasReturn = false;
        for (auto& block : blockGroup.GetBlocks()) {
            auto term = block->GetTerminator();
            if (term == nullptr) {
                continue;
            }
            if (term->GetExprKind() != ExprKind::EXIT && term->GetExprKind() != ExprKind::RAISE_EXCEPTION) {
                continue;
            }
            hasReturn = true;
        }
        if (!hasReturn) {
            // Report warning now, to distinguish more specific scenarios in future.
            WarningInFunc(blockGroup.GetTopLevelFunc(), "block group has no exit or raise terminator.");
            return true;
        }
        return true;
    }

    bool CheckTerminator(const Block& block) const
    {
        // skip dead blocks
        if (!block.IsEntry() && block.GetPredecessors().empty()) {
            return true;
        }
        return block.GetTerminator() && block.GetTerminator()->IsTerminator();
    }

    bool CheckSuccessors(const Block& block) const
    {
        if (!block.GetTerminator()) { // skip dead blocks
            return true;
        }

        auto successors = block.GetTerminator()->GetSuccessors();
        for (auto& successor : successors) {
            auto spredecessors = successor->GetPredecessors();
            bool findFlag = false;
            for (auto& spredecessor : spredecessors) {
                if (spredecessor->GetIdentifierWithoutPrefix() == block.GetIdentifierWithoutPrefix()) {
                    findFlag = true;
                    break;
                }
            }
            if (!findFlag) {
                ErrorInFunc(block.GetTopLevelFunc(),
                    successor->GetIdentifier() + " is " + block.GetIdentifier() + "'s successor, but " +
                        block.GetIdentifier() + " is not " + successor->GetIdentifier() + "'s predecessor.");
                return false;
            }
        }

        return true;
    }

    // block must have expression.
    // block last expression must be terminator.
    // block middle expression must not be terminator.
    bool CheckBlock(const Block& block) const
    {
        CJC_NULLPTR_CHECK(block.GetParentBlockGroup());
        auto exprs = block.GetExpressions();
        if (IsEndPhase() && exprs.size() == 0) {
            ErrorInFunc(block.GetTopLevelFunc(), "block " + block.GetIdentifier() + " has no expression.");
            return false;
        }
        for (size_t loop = 0; loop + 1 < exprs.size(); loop++) {
            auto expr = exprs[loop];
            if (expr->IsTerminator()) {
                ErrorInFunc(
                    block.GetTopLevelFunc(), "terminator found in the middle of block " + block.GetIdentifier() + ".");
                return false;
            }
        }
        if (!CheckTerminator(block)) {
            ErrorInFunc(block.GetTopLevelFunc(), "block " + block.GetIdentifier() + " does not have terminator.");
            return false;
        }
        return CheckSuccessors(block);
    }

    bool ExprOperandCheck(const Expression& expr, std::vector<Value*>& values) const
    {
        const auto& operands = expr.GetOperands();
        for (auto& operand : operands) {
            if (operand->IsLiteral()) {
                continue;
            }
            if (operand->IsGlobal()) {
                continue;
            }
            if (operand->TestAttr(Attribute::IMPORTED)) {
                continue;
            }
            if (IsEndPhase() && std::find(values.begin(), values.end(), operand) == values.end()) {
                if (expr.IsTerminator()) {
                    ErrorInFunc(expr.GetTopLevelFunc(),
                        operand->GetIdentifier() + " in terminator of block " + expr.GetParentBlock()->GetIdentifier() +
                            " is unreachable.");
                } else {
                    ErrorInFunc(expr.GetTopLevelFunc(),
                        operand->GetIdentifier() + " in " + expr.GetResult()->GetIdentifier() + " is unreachable.");
                }
                return false;
            }
        }
        return true;
    }

    bool ExprOperandCheck(const Terminator& terminator, std::vector<Value*>& values, std::set<const Block*>& blocks)
    {
        auto ret = true;
        if (auto res = terminator.GetResult(); res != nullptr) {
            values.emplace_back(res);
        }
        for (auto& suc : terminator.GetSuccessors()) {
            auto size = values.size();
            ret = ExprOperandCheck(*suc, values, blocks) && ret;
            // clear branch values
            if (values.size() > size) {
                values.erase(values.begin() + static_cast<long>(size), values.end());
            }
        }
        return ret;
    }

    bool ExprOperandCheck(const ForIn& forin, std::vector<Value*>& values)
    {
        bool ret = true;
        for (auto bg : forin.GetExecutionOrder()) {
            ret = ExprOperandCheck(*bg, values) && ret;
        }
        return ret;
    }

    bool ExprOperandCheck(const Block& block, std::vector<Value*>& values, std::set<const Block*>& blocks)
    {
        if (blocks.count(&block) != 0) {
            return true;
        }
        blocks.insert(&block);
        auto ret = true;
        for (auto expr : block.GetExpressions()) {
            ret = ExprOperandCheck(*expr, values) && ret;
            if (expr->IsTerminator()) {
                auto terminator = Cangjie::StaticCast<Terminator*>(expr);
                ret = ExprOperandCheck(*terminator, values, blocks) && ret;
            } else {
                auto exprKind = expr->GetExprKind();
                if (exprKind == ExprKind::IF) {
                    auto ifExpr = Cangjie::StaticCast<If*>(expr);
                    ret = ExprOperandCheck(*ifExpr->GetTrueBranch(), values) && ret;
                    ret = ExprOperandCheck(*ifExpr->GetFalseBranch(), values) && ret;
                } else if (exprKind == ExprKind::LOOP) {
                    auto loopExpr = Cangjie::StaticCast<Loop*>(expr);
                    ret = ExprOperandCheck(*loopExpr->GetLoopBody(), values) && ret;
                } else if (auto forin = Cangjie::DynamicCast<ForIn>(expr)) {
                    ret = ExprOperandCheck(*forin, values) && ret;
                } else if (exprKind == ExprKind::LAMBDA) {
                    auto size = values.size();
                    auto lambdaExpr = Cangjie::StaticCast<Lambda*>(expr);
                    if (lambdaExpr->IsLocalFunc()) {
                        // Local function can used itself inside funcBody.
                        values.emplace_back(expr->GetResult());
                    }
                    const auto& params = lambdaExpr->GetParams();
                    values.insert(values.end(), params.begin(), params.end());
                    ret = ExprOperandCheck(*lambdaExpr->GetEntryBlock(), values, blocks);
                    // Clear values add in this lambda.
                    values.erase(values.begin() + static_cast<long>(size), values.end());
                }
                values.emplace_back(expr->GetResult());
            }
        }
        return ret;
    }

    bool ExprOperandCheck(const BlockGroup& groupBody, std::vector<Value*>& values)
    {
        auto size = values.size();
        auto entryBlock = groupBody.GetEntryBlock();
        // already checked blocks
        std::set<const Block*> blocks;
        auto ret = ExprOperandCheck(*entryBlock, values, blocks);
        // clear values add in this block group.
        if (values.size() != 0) {
            values.erase(values.begin() + static_cast<long>(size), values.end());
        }
        return ret;
    }

    bool CheckExpression(const Expression& expr, std::set<const GenericType*>& genericTypes)
    {
        CJC_NULLPTR_CHECK(expr.GetParentBlock());
        auto ret = true;
        const std::unordered_map<ExprMajorKind, std::function<void()>> actionMap = {
            {ExprMajorKind::TERMINATOR,
                [&ret, &expr, this]() { ret = CheckTerminator(Cangjie::StaticCast<const Terminator&>(expr)); }},
            {ExprMajorKind::UNARY_EXPR, [&ret, &expr, this]() { ret = CheckUnaryExpression(expr); }},
            {ExprMajorKind::BINARY_EXPR, [&ret, &expr, this]() { ret = CheckBinaryExpression(expr); }}};
        if (auto iter = actionMap.find(expr.GetExprMajorKind()); iter != actionMap.end()) {
            iter->second();
        } else {
            ret = CheckNormalExpr(expr, genericTypes);
        }
        return ret;
    }

    bool CheckNormalExpr(const Expression& expr, std::set<const GenericType*>& genericTypes)
    {
        bool ret = true;
        const std::unordered_map<ExprKind, std::function<void()>> actionMap = {
            {ExprKind::CONSTANT,
                [&ret, &expr, this]() { ret = CheckConstant(Cangjie::StaticCast<const Constant&>(expr)); }},
            {ExprKind::ALLOCATE,
                [&ret, &expr, this]() { ret = CheckAllocate(Cangjie::StaticCast<const Allocate&>(expr)); }},
            {ExprKind::LOAD, [&ret, &expr, this]() { ret = CheckLoad(Cangjie::StaticCast<const Load&>(expr)); }},
            {ExprKind::STORE, [&ret, &expr, this]() { ret = CheckStore(Cangjie::StaticCast<const Store&>(expr)); }},
            {ExprKind::GET_ELEMENT_REF,
                [&ret, &expr, this]() { ret = CheckGetElementRef(Cangjie::StaticCast<const GetElementRef&>(expr)); }},
            {ExprKind::STORE_ELEMENT_REF,
                [&ret, &expr, this]() {
                    ret = CheckStoreElementRef(Cangjie::StaticCast<const StoreElementRef&>(expr));
                }},
            {ExprKind::IF, [&ret, &expr, this]() { ret = CheckIf(Cangjie::StaticCast<const If&>(expr)); }},
            {ExprKind::LOOP, [&ret, &expr, this]() { ret = CheckLoop(Cangjie::StaticCast<const Loop&>(expr)); }},
            {ExprKind::FORIN_RANGE,
                [&ret, &expr, this]() { ret = CheckForIn(Cangjie::StaticCast<const ForIn&>(expr)); }},
            {ExprKind::FORIN_ITER,
                [&ret, &expr, this]() { ret = CheckForIn(Cangjie::StaticCast<const ForIn&>(expr)); }},
            {ExprKind::FORIN_CLOSED_RANGE,
                [&ret, &expr, this]() { ret = CheckForIn(Cangjie::StaticCast<const ForIn&>(expr)); }},
            {ExprKind::DEBUGEXPR, [&ret, &expr, this]() { ret = CheckDebug(Cangjie::StaticCast<const Debug&>(expr)); }},
            {ExprKind::TUPLE, [&ret, &expr, this]() { ret = CheckTuple(Cangjie::StaticCast<const Tuple&>(expr)); }},
            {ExprKind::FIELD, [&ret, &expr, this]() { ret = CheckField(Cangjie::StaticCast<const Field&>(expr)); }},
            {ExprKind::APPLY, [&ret, &expr, this]() { ret = CheckApply(Cangjie::StaticCast<const Apply&>(expr)); }},
            {ExprKind::INVOKE, [&ret, &expr, this]() { ret = CheckInvoke(Cangjie::StaticCast<const Invoke&>(expr)); }},
            {ExprKind::INVOKESTATIC,
                [&ret, &expr, this]() { ret = CheckInvokeStatic(Cangjie::StaticCast<const InvokeStatic&>(expr)); }},
            {ExprKind::INSTANCEOF,
                [&ret, &expr, this]() { ret = CheckInstanceOf(Cangjie::StaticCast<const InstanceOf&>(expr)); }},
            {ExprKind::TYPECAST, [&ret, &expr, this]() { ret = CheckTypeCast(expr); }},
            {ExprKind::GET_EXCEPTION,
                [&ret, &expr, this]() { ret = CheckGetException(Cangjie::StaticCast<const GetException&>(expr)); }},
            {ExprKind::SPAWN, [&ret, &expr, this]() { ret = CheckSpawn(expr); }},
            {ExprKind::RAW_ARRAY_ALLOCATE,
                [&ret, &expr, this]() {
                    ret = CheckRawArrayAllocate(Cangjie::StaticCast<const RawArrayAllocate&>(expr));
                }},
            {ExprKind::RAW_ARRAY_LITERAL_INIT,
                [&ret, &expr, this]() {
                    ret = CheckRawArrayLiteralInit(Cangjie::StaticCast<const RawArrayLiteralInit&>(expr));
                }},
            {ExprKind::RAW_ARRAY_INIT_BY_VALUE,
                [&ret, &expr, this]() {
                    ret = CheckRawArrayInitByValue(Cangjie::StaticCast<const RawArrayInitByValue&>(expr));
                }},
            {ExprKind::VARRAY, [&ret, &expr, this]() { ret = CheckVArray(Cangjie::StaticCast<const VArray&>(expr)); }},
            {ExprKind::VARRAY_BUILDER,
                [&ret, &expr, this]() { ret = CheckVArrayBuilder(Cangjie::StaticCast<const VArrayBuilder&>(expr)); }},
            {ExprKind::INTRINSIC,
                [&ret, &expr, this]() { ret = CheckIntrinsic(Cangjie::StaticCast<const Intrinsic&>(expr)); }},
            {ExprKind::LAMBDA,
                [&ret, &expr, this, &genericTypes]() {
                    ret = CheckLambda(Cangjie::StaticCast<const Lambda&>(expr), genericTypes);
                }},
            {ExprKind::BOX, [&ret, &expr, this]() { ret = CheckBox(Cangjie::StaticCast<const Box&>(expr)); }},
            {ExprKind::UNBOX, [&ret, &expr, this]() { ret = CheckUnBox(Cangjie::StaticCast<const UnBox&>(expr)); }},
            {ExprKind::TRANSFORM_TO_GENERIC,
                [&ret, &expr, this]() {
                    ret = CheckTransformToGeneric(Cangjie::StaticCast<const TransformToGeneric&>(expr));
                }},
            {ExprKind::TRANSFORM_TO_CONCRETE,
                [&ret, &expr, this]() {
                    ret = CheckTransformToConcrete(Cangjie::StaticCast<const TransformToConcrete&>(expr));
                }},
            {ExprKind::GET_INSTANTIATE_VALUE,
                [&ret, &expr, this]() {
                    ret = CheckGetInstantiateValue(Cangjie::StaticCast<const GetInstantiateValue&>(expr));
                }},
            {ExprKind::UNBOX_TO_REF,
                [&ret, &expr, this]() {
                    ret = CheckUnBoxToRef(Cangjie::StaticCast<const UnBoxToRef&>(expr));
                }},
            {ExprKind::GET_RTTI, [&ret, &expr, this]() { ret = CheckGetRTTI(Cangjie::StaticCast<GetRTTI>(expr)); }},
            {ExprKind::GET_RTTI_STATIC, [&ret, &expr, this]() {
                ret = CheckGetRTTI(Cangjie::StaticCast<GetRTTIStatic>(expr));
            }},
            {ExprKind::GET_ELEMENT_BY_NAME, [&ret, &expr, this]() {
                ret = CheckGetElementByName(Cangjie::StaticCast<GetElementByName>(expr));
            }},
            {ExprKind::STORE_ELEMENT_BY_NAME, [&ret, &expr, this]() {
                ret = CheckStoreElementByName(Cangjie::StaticCast<StoreElementByName>(expr));
            }},
            {ExprKind::FIELD_BY_NAME, [&ret, &expr, this]() {
                ret = CheckFieldByName(Cangjie::StaticCast<FieldByName>(expr));
            }},
        };
        if (auto iter = actionMap.find(expr.GetExprKind()); iter != actionMap.end()) {
            iter->second();
        } else {
            WarningInFunc(expr.GetTopLevelFunc(), "find unrecongnized ExprKind " + expr.GetExprKindName() + ".");
        }
        return ret;
    }

    bool CheckGetRTTI(const GetRTTI& expr) const
    {
        auto result = expr.GetResult();
        if (!result->GetType()->IsUnit()) {
            TypeCheckError(*result, "Unit");
            return false;
        }
        auto operandType = expr.GetOperand()->GetType();
        if (!Cangjie::Is<RefType>(operandType) ||
            (!operandType->StripAllRefs()->IsClass() && !operandType->StripAllRefs()->IsThis())) {
            ErrorInFunc(expr.GetTopLevelFunc(), "GetRTTI must be used on Class& or This&");
            return false;
        }
        return true;
    }

    bool CheckGetRTTI(const GetRTTIStatic& expr) const
    {
        auto result = expr.GetResult();
        if (!result->GetType()->IsUnit()) {
            TypeCheckError(*result, "Unit");
            return false;
        }
        auto func = expr.GetTopLevelFunc();

        bool skipCheckUse = false;
        if (!IsEndPhase()) {
            skipCheckUse = true;
        }
        if (!skipCheckUse && expr.GetRTTIType()->StripAllRefs()->IsThis() && !func->TestAttr(Attribute::STATIC)) {
            ErrorInFunc(func, "Cannot use GetRTTIStatic on This type in non-static function");
            return false;
        }
        return true;
    }

    bool CheckGetElementByName([[maybe_unused]] const GetElementByName& expr) const
    {
        return false;
    }

    bool CheckStoreElementByName([[maybe_unused]] const StoreElementByName& expr) const
    {
        return false;
    }

    bool CheckFieldByName([[maybe_unused]] const FieldByName& expr) const
    {
        return false;
    }

    bool CheckConstant(const Constant& expr) const
    {
        auto result = expr.GetResult();
        auto expTy = expr.GetOperand(0)->GetType();
        if (!IsExpectedType(*result->GetType(), *expTy)) {
            TypeCheckError(expr, *expr.GetOperand(0), result->GetType()->ToString());
            return false;
        }
        return true;
    }

    // Unary Expression
    // UnaryExpression-Not input and result must be Boolean.
    // UnaryExpression result type must be same with operand[0].
    bool CheckUnaryExpression(const Expression& expr) const
    {
        auto ty = expr.GetResult()->GetType();
        auto expTy = expr.GetOperand(0)->GetType();
        if (expTy->IsNothing()) {
            return true;
        }
        if (expr.GetExprKind() == ExprKind::NOT && !expTy->IsBoolean()) {
            TypeCheckError(expr, *expr.GetOperand(0), "Boolean");
            return false;
        }
        if (expr.GetExprKind() == ExprKind::BITNOT && !expTy->IsInteger()) {
            TypeCheckError(expr, *expr.GetOperand(0), "Integer");
            return false;
        }
        if (expr.GetExprKind() == ExprKind::NEG && !(expTy->IsInteger() || expTy->IsFloat())) {
            TypeCheckError(expr, *expr.GetOperand(0), "Integer or Float");
            return false;
        }
        if (!IsExpectedType(*expTy, *ty)) {
            TypeCheckError(*expr.GetResult(), expTy->ToString());
            return false;
        }
        return true;
    }

    // Binary Expression
    bool CheckBinaryExpression(const Expression& expr) const
    {
        auto exprKind = expr.GetExprKind();
        if (exprKind >= ExprKind::ADD && exprKind <= ExprKind::MOD) {
            return CheckCalculExpression(expr);
        } else if (exprKind == ExprKind::EXP) {
            return CheckExpExpression(expr);
        } else if (exprKind >= ExprKind::LSHIFT && exprKind <= ExprKind::BITXOR) {
            return CheckBitExpression(expr);
        } else if (exprKind >= ExprKind::LT && exprKind <= ExprKind::NOTEQUAL) {
            return CheckCmpExpression(expr);
        } else if (exprKind >= ExprKind::AND && exprKind <= ExprKind::OR) {
            return CheckLogicExpression(expr);
        }
        WarningInFunc(expr.GetTopLevelFunc(), "find unrecongnized ExprKind " + expr.GetExprKindName() + ".");
        return true;
    }

    // BinaryExpression-Mod input must be Integer.
    // BinaryExpression-Add Sub Mul Div Mod operands types, result type must be same.
    bool CheckCalculExpression(const Expression& expr) const
    {
        auto expTy0 = expr.GetOperand(0)->GetType();
        auto expTy1 = expr.GetOperand(1)->GetType();
        if (expTy0->IsNothing() && expTy1->IsNothing()) {
            // both operand types are 'Nothing', no need check
            return true;
        }
        if (expr.GetExprKind() == ExprKind::MOD) {
            if (!expTy0->IsNothing() && !expTy0->IsInteger()) {
                TypeCheckError(expr, *expr.GetOperand(0), "Integer");
                return false;
            }
            if (!expTy1->IsNothing() && !expTy1->IsInteger()) {
                TypeCheckError(expr, *expr.GetOperand(0), "Integer");
                return false;
            }
        }
        if (!expTy0->IsNothing() && !expTy1->IsNothing()) {
            // no 'Nothing' type in operands check operand and result
            if (!IsExpectedType(*expTy0, *expTy1)) {
                TypeCheckError(expr, *expr.GetOperand(1), expTy0->ToString());
                return false;
            }
            if (!expTy0->IsNothing() && !IsExpectedType(*expTy0, *expr.GetResult()->GetType())) {
                TypeCheckError(expr, *expr.GetResult(), expTy0->ToString());
                return false;
            }
        } else {
            // 'Nothing' type in operands just check result
            if (!IsExpectedType(*expr.GetResult()->GetType(), *expTy0)) {
                TypeCheckError(expr, *expr.GetOperand(0), expr.GetResult()->GetType()->ToString());
                return false;
            }
            if (!IsExpectedType(*expr.GetResult()->GetType(), *expTy1)) {
                TypeCheckError(expr, *expr.GetOperand(1), expr.GetResult()->GetType()->ToString());
                return false;
            }
        }
        return true;
    }

    // BinaryExpression-Exp must be (Int64, UInt64)->Int64 or (Float64, Int64/Float64)->Float64.
    bool CheckExpExpression(const Expression& expr) const
    {
        auto expTy0 = expr.GetOperand(0)->GetType();
        auto expTy1 = expr.GetOperand(1)->GetType();
        auto result = expr.GetResult();
        if (!IsExpectedType(*expTy0, Type::TypeKind::TYPE_INT64) &&
            !IsExpectedType(*expTy0, Type::TypeKind::TYPE_FLOAT64)) {
            TypeCheckError(expr, *expr.GetOperand(0), "Int64 or Float64");
            return false;
        }
        auto resultTy = result->GetType();
        if (expTy0->GetTypeKind() == Type::TypeKind::TYPE_INT64) {
            if (!IsExpectedType(*expTy1, Type::TypeKind::TYPE_UINT64)) {
                TypeCheckError(expr, *expr.GetOperand(1), "UInt64");
                return false;
            }
        } else if (expTy0->GetTypeKind() == Type::TypeKind::TYPE_FLOAT64) {
            if (!IsExpectedType(*expTy1, Type::TypeKind::TYPE_INT64) &&
                !IsExpectedType(*expTy1, Type::TypeKind::TYPE_FLOAT64)) {
                TypeCheckError(expr, *expr.GetOperand(1), "Int64 or Float64");
                return false;
            }
        }
        if (!IsExpectedType(*resultTy, *expTy0)) {
            TypeCheckError(*result, expTy0->ToString());
            return false;
        }
        return true;
    }

    // BinaryExpression-Lshift Rshift BitAnd BitOr BitXOr operands types must be Integer.
    bool CheckBitExpression(const Expression& expr) const
    {
        auto expTy0 = expr.GetOperand(0)->GetType();
        if (!expTy0->IsInteger() && !expTy0->IsNothing()) {
            TypeCheckError(expr, *expr.GetOperand(0), "Integer");
            return false;
        }
        auto expTy1 = expr.GetOperand(1)->GetType();
        if (!expTy1->IsInteger() && !expTy1->IsNothing()) {
            TypeCheckError(expr, *expr.GetOperand(1), "Integer");
            return false;
        }
        return true;
    }

    // BinaryExpression-Lt Gt Le Ge Equal NotEqual operands types must be same.
    // BinaryExpression-Lt Gt Le Ge Equal NotEqual result type must be Boolean.
    bool CheckCmpExpression(const Expression& expr) const
    {
        auto expTy0 = expr.GetOperand(0)->GetType();
        auto expTy1 = expr.GetOperand(1)->GetType();
        auto result = expr.GetResult();
        if (!IsExpectedType(*expTy0, *expTy1) && !expTy0->IsNothing()) {
            TypeCheckError(expr, *expr.GetOperand(1), expTy0->ToString());
            return false;
        }
        auto resultTy = result->GetType();
        if (!resultTy->IsBoolean()) {
            TypeCheckError(*result, "Boolean");
            return false;
        }
        return true;
    }

    // BinaryExpression-And Or operands and result types must be Boolean.
    bool CheckLogicExpression(const Expression& expr) const
    {
        auto result = expr.GetResult();
        auto expTy0 = expr.GetOperand(0)->GetType();
        auto expTy1 = expr.GetOperand(1)->GetType();
        if (!expTy0->IsNothing() || !expTy1->IsNothing()) {
            if (!expTy0->IsBoolean()) {
                TypeCheckError(expr, *expr.GetOperand(0), "Boolean");
                return false;
            }
            if (!expTy1->IsBoolean()) {
                TypeCheckError(expr, *expr.GetOperand(1), "Boolean");
                return false;
            }
        }
        auto resultTy = result->GetType();
        if (!resultTy->IsBoolean()) {
            TypeCheckError(*result, "Boolean");
            return false;
        }
        return true;
    }

    bool CheckGenericArgs(Type& baseType) const
    {
        auto baseTy = &baseType;
        while (baseTy->IsRef()) {
            baseTy = Cangjie::StaticCast<RefType*>(baseTy)->GetBaseType();
        }
        if (auto customType = Cangjie::DynamicCast<CustomType*>(baseTy)) {
            auto customDef = customType->GetCustomTypeDef();
            if (auto customDefType = Cangjie::DynamicCast<CustomType*>(customDef->GetType());
                customDefType && customType->GetGenericArgs().size() != customDefType->GetGenericArgs().size()) {
                Errorln("arg size of " + customDefType->ToString() + " is not equal to " + customType->ToString());
                return false;
            }
        }
        return true;
    }

    // Memory Expression
    // Allocate result type must be operand[0]'s RefType.
    bool CheckAllocate(const Allocate& expr) const
    {
        auto result = expr.GetResult();
        auto resultTy = result->GetType();
        auto baseType = expr.GetType();
        if (!resultTy->IsRef() || baseType != Cangjie::StaticCast<RefType*>(resultTy)->GetBaseType()) {
            TypeCheckError(*result, baseType->ToString() + "&");
            return false;
        }
        if (baseType->IsVoid()) {
            Errorln("Unexpected allocation of Void type");
            return false;
        }
        if (!CheckGenericArgs(*baseType)) {
            return false;
        }

        return true;
    }

    // Load location type must be result type's RefType.
    bool CheckLoad(const Load& expr) const
    {
        auto location = expr.GetLocation();
        auto result = expr.GetResult();
        if (!location->GetType()->IsRef()) {
            TypeCheckError(expr, *location, result->GetType()->ToString() + "&");
            return false;
        }
        auto expTy = Cangjie::StaticCast<RefType*>(location->GetType())->GetBaseType();
        if (!IsExpectedType(*expTy, *result->GetType())) {
            TypeCheckError(*result, expTy->ToString());
            return false;
        }
        return true;
    }

    // Store location type must be value type's RefType.
    // Store result type must be Unit.
    bool CheckStore(const Store& expr) const
    {
        auto location = expr.GetLocation();
        auto result = expr.GetResult();
        auto value = expr.GetValue();
        if (!location->GetType()->IsRef()) {
            TypeCheckError(expr, *location, location->GetType()->ToString() + "&");
            return false;
        }
        auto expTy = Cangjie::StaticCast<RefType*>(location->GetType())->GetBaseType();
        if (!IsExpectedType(*expTy, *value->GetType())) {
            TypeCheckError(expr, *value, expTy->ToString());
            return false;
        }
        if (!result->GetType()->IsUnit()) {
            TypeCheckError(*result, "Unit");
            return false;
        }
        if (auto res = Cangjie::DynamicCast<LocalVar*>(location)) {
            if (res->GetExpr()->GetExprKind() == Cangjie::CHIR::ExprKind::GET_ELEMENT_REF) {
                auto err = "Location of Store:" + result->GetIdentifier() + " should not be GetElementRef.";
                ErrorInFunc(expr.GetTopLevelFunc(), err);
                return false;
            }
        }
        return true;
    }

    Type* CheckPathIsAvailable(const Expression& expr, const std::vector<uint64_t>& path, Type& locationType) const
    {
        auto baseType = &locationType;
        std::string indexStr = "(";
        for (size_t i = 0; i < path.size(); i++) {
            auto index = path[i];
            if (i != 0) {
                indexStr += ", ";
            }
            indexStr += std::to_string(index);
            baseType = GetFieldOfType(*baseType, index, builder);
            if (baseType == nullptr) {
                ErrorInFunc(expr.GetTopLevelFunc(),
                    "value " + expr.GetResult()->GetIdentifier() + " can not find path " + indexStr + ") from type " +
                    locationType.ToString());
                return nullptr;
            }
        }
        return baseType;
    }

    // GetElementRef baseType must be Class, Struct or RawArray.
    // GetElementRef result type must be same with type of Class/Struct's member var or RawArray's element.
    bool CheckGetElementRef(const GetElementRef&) const
    {
        return true;
    }

    bool CheckStoreElementRef(const StoreElementRef& expr) const
    {
        auto location = expr.GetLocation();
        auto result = expr.GetResult();
        auto baseType = location->GetType();
        if (!baseType->IsRef() || baseType->GetRefDims() != 1) {
            TypeCheckError(expr, *location, "Class& Struct& Tuple& Closure& or RawArray&");
            return false;
        }

        baseType = Cangjie::StaticCast<RefType*>(baseType)->GetBaseType();
        if (baseType->IsGeneric()) {
            ErrorInFunc(expr.GetTopLevelFunc(),
                "Location of StoreElementRef: " + result->GetIdentifier() + "should not be generic");
            return false;
        }
        auto fieldType = CheckPathIsAvailable(expr, expr.GetPath(), *baseType);
        if (fieldType == nullptr) {
            return false;
        }

        if (!CheckType(*expr.GetValue()->GetType(), *fieldType)) {
            auto errInfo = "Stored value type " + expr.GetValue()->GetType()->ToString() + " of value " +
                expr.GetValue()->GetIdentifier() + " does not match pointer operand " + fieldType->ToString() +
                " in StoreElementRef " + expr.GetResult()->GetIdentifier();
            ErrorInFunc(expr.GetTopLevelFunc(), errInfo);
        }

        if (!result->GetType()->IsUnit()) {
            TypeCheckError(*result, "Unit");
            return false;
        }
        return true;
    }

    // Struct Control
    // If condition type must be Boolean.
    // If true and false branch exit type must be same.
    bool CheckIf(const If& expr) const
    {
        auto cond = expr.GetCondition();
        if (!cond->GetType()->IsBoolean() && !cond->GetType()->IsNothing()) {
            TypeCheckError(expr, *cond, "Boolean");
            return false;
        }
        return true;
    }

    bool CheckLoop(const Loop& expr) const
    {
        (void)expr;
        // To be implemented
        return true;
    }

    bool CheckForIn(const ForIn& expr) const
    {
        (void)expr;
        // To be implemented
        return true;
    }

    // Other
    // Debug return type must be Unit.
    bool CheckDebug(const Debug& expr) const
    {
        auto result = expr.GetResult();
        if (!result->GetType()->IsUnit()) {
            TypeCheckError(*result, "Unit");
            return false;
        }
        auto base = expr.GetValue();
        if (!base->IsParameter() && opts.enableCompileDebug) {
            if (!base->GetType()->IsRef()) {
                if (base->GetType()->IsClassOrArray()) {
                    TypeCheckError(*base, base->GetType()->ToString() + "&&");
                    return false;
                } else {
                    TypeCheckError(*base, base->GetType()->ToString() + "&");
                    return false;
                }
            } else {
                auto baseType = Cangjie::StaticCast<RefType*>(base->GetType())->GetBaseType();
                if (baseType->IsClassOrArray()) {
                    TypeCheckError(*base, base->GetType()->ToString() + "&");
                    return false;
                } else if (baseType->IsRef()) {
                    auto baseTypeTemp = Cangjie::StaticCast<RefType*>(baseType)->GetBaseType();
                    if (!baseTypeTemp->IsClassOrArray()) {
                        Errorln("Unexpected type In:" + expr.ToString());
                    }
                }
            }
        }
        return true;
    }
    // Tuple inputs types and output type must be satisfied.
    bool CheckTuple(const Tuple& expr) const
    {
        auto result = expr.GetResult();
        auto resultTy = result->GetType();
        const auto& operands = expr.GetOperands();
        if (resultTy->IsEnum()) {
            if (operands.size() == 0) {
                auto err = result->GetIdentifier() + " first operand type is expected to be UInt32 or Bool.";
                ErrorInFunc(expr.GetTopLevelFunc(), err);
                return false;
            }
            if (!IsEnumSelectorType(*operands[0]->GetType())) {
                TypeCheckError(expr, *operands[0], "Constant UInt32 or Constant Bool");
                return false;
            }
            if (auto operand = Cangjie::DynamicCast<LocalVar*>(operands[0]);
                operand == nullptr || !(operand->GetExpr()->IsConstantInt() || operand->GetExpr()->IsConstantBool())) {
                TypeCheckError(expr, *operands[0], "Constant UInt32 or Constant Bool");
                return false;
            }
            return true;
        } else if (!resultTy->IsTuple() && !resultTy->IsStruct()) {
            TypeCheckError(*result, "Enum, Tuple or Struct");
            return false;
        }
        size_t resultTySize = 0;
        if (resultTy->IsTuple()) {
            resultTySize = Cangjie::StaticCast<TupleType*>(resultTy)->GetElementTypes().size();
        } else {
            resultTySize = Cangjie::StaticCast<StructType*>(resultTy)->GetStructDef()->GetAllInstanceVarNum();
        }
        if (operands.size() != resultTySize) {
            TypeCheckError(*result, "Tuple size " + std::to_string(operands.size()));
            return false;
        }
        for (size_t i = 0; i < resultTySize; i++) {
            auto fieldTy = GetFieldOfType(*resultTy, i, builder);
            if (!IsExpectedType(*fieldTy, *operands[i]->GetType())) {
                TypeCheckError(expr, i, *operands[i], fieldTy->ToString());
                return false;
            }
        }
        return true;
    }

    // Field inputs types and output type must be satisfied.
    bool CheckField(const Field& expr) const
    {
        auto base = expr.GetBase();
        auto index = expr.GetPath();
        auto result = expr.GetResult();
        auto type = base->GetType();
        if (type->IsGeneric()) {
            ErrorInFunc(
                expr.GetTopLevelFunc(), "Location of Field: " + result->GetIdentifier() + "should not be generic");
            return false;
        }
        std::string indexStr = "(";
        CJC_ASSERT(!type->IsEnum() || index.size() > 0);
        if (type->IsEnum() && index.back() == 0) {
            auto kind = result->GetType()->GetTypeKind();
            if (kind != Type::TypeKind::TYPE_UINT32 && kind != Type::TypeKind::TYPE_BOOLEAN) {
                ErrorInFunc(expr.GetTopLevelFunc(), "value " + expr.GetResult()->GetIdentifier() + " has type " +
                    expr.GetResultType()->ToString() + " but type UInt32 or Boolean is expected.");
                return false;
            }
            return true;
        }
        for (size_t i = 0; i < index.size(); i++) {
            if (i != 0) {
                indexStr = indexStr + ", ";
            }
            indexStr = indexStr + std::to_string(index[i]);
            if (type->IsClass() || type->IsRef()) {
                ErrorInFunc(expr.GetTopLevelFunc(),
                    "value " + result->GetIdentifier() + " can not find indexs " + indexStr + ") from input type " +
                        base->GetType()->ToString());
                return false;
            }
            if (type->IsGeneric()) {
                continue;
            }
            type = GetFieldOfType(*type, index[i], builder);
            if (type == nullptr) {
                ErrorInFunc(expr.GetTopLevelFunc(),
                    "value " + result->GetIdentifier() + " can not find indexs " + indexStr + ") from input type " +
                        base->GetType()->ToString());
                return false;
            }
        }
        if (!IsExpectedType(*type, *result->GetType())) {
            TypeCheckError(*result, type->ToString());
            return false;
        }
        return true;
    }

    bool IsFuncType(const Type& type) const
    {
        if (type.IsFunc()) {
            return true;
        }
        if (auto gType = Cangjie::DynamicCast<const GenericType*>(&type); gType) {
            for (auto upperBound : gType->GetUpperBounds()) {
                if (IsFuncType(*upperBound)) {
                    return true;
                }
            }
        }
        return false;
    }

    FuncType* GetFuncType(Type& type) const
    {
        if (auto fType = Cangjie::DynamicCast<FuncType*>(&type); fType) {
            return fType;
        }
        if (auto gType = Cangjie::DynamicCast<GenericType*>(&type); gType) {
            for (auto upperBound : gType->GetUpperBounds()) {
                auto ty = GetFuncType(*upperBound);
                if (ty != nullptr) {
                    return ty;
                }
            }
        }
        CJC_ABORT();
        return nullptr;
    }

    bool IsMemberFunc(const Ptr<Value> func) const
    {
        if (auto calleeFunc = Cangjie::DynamicCast<FuncBase*>(func); calleeFunc && calleeFunc->IsMemberFunc()) {
            return true;
        }
        return false;
    }

    bool CheckReturnType(const FuncType& declaredFuncType, const Type& instRetType) const
    {
        auto declaredRetType = declaredFuncType.GetReturnType();
        return CheckType(*declaredRetType, instRetType) || CheckType(instRetType, *declaredRetType);
    }

    template <typename T>
    bool CheckInstantiatedType(
        const T& expr, Ptr<FuncType> funcType, bool isConstructor) const
    {
        if (!isConstructor) {
            if (!CheckReturnType(*funcType, *expr.GetResult()->GetType())) {
                std::string info = "Value:" + expr.GetResult()->ToString() +
                    " type mismatch between instantiated return type '" + expr.GetResultType()->ToString() +
                    "' and arg type '" + funcType->GetReturnType()->ToString() + "'.";
                ErrorInFunc(expr.GetTopLevelFunc(), info);
                return false;
            }
        }
        return true;
    }

    bool CheckTypeArgs(const Type& newInstantiatedType, const Type& newOriginType) const
    {
        if (newInstantiatedType.GetTypeArgs().size() != newOriginType.GetTypeArgs().size()) {
            return false;
        }
        for (size_t index = 0; index < newInstantiatedType.GetTypeArgs().size(); ++index) {
            if (!CheckType(*newInstantiatedType.GetTypeArgs()[index], *newOriginType.GetTypeArgs()[index])) {
                return false;
            }
        }
        return true;
    }

    bool CheckType(const Type& subType, const Type& parentType) const
    {
        if (subType.IsNothing()) {
            return true;
        }
        if (parentType.IsGeneric() || subType.IsGeneric()) {
            return true;
        }
        if (&subType == &parentType) {
            return true;
        }
        if (subType.GetTypeKind() != parentType.GetTypeKind()) {
            return false;
        }
        if (subType.IsEqualOrSubTypeOf(parentType, builder)) {
            return true;
        }
        if (auto subCustomType = Cangjie::DynamicCast<const CustomType*>(&subType)) {
            auto parentCustomType = Cangjie::StaticCast<const CustomType*>(&parentType);
            if (subCustomType->GetCustomTypeDef() != parentCustomType->GetCustomTypeDef()) {
                return false;
            }
        }
        if (CheckTypeArgs(subType, parentType)) {
            return true;
        }
        return false;
    }

    // Apply callee must be funcType.
    // Apply result type must same with func return types, and input types must same with func input types
    bool CheckApply(const Apply& expr) const
    {
        auto func = expr.GetCallee();
        auto type = func->GetType();
        auto result = expr.GetResult();

        // check callee type must function type
        if (!IsFuncType(*type)) {
            TypeCheckError(expr, *func, "FuncType");
            return false;
        }
        auto funcType = GetFuncType(*type);
        if (funcType->HasVarArg()) {
            return true;
        }

        if (auto callee = Cangjie::DynamicCast<FuncBase*>(func); callee && callee->IsMemberFunc()) {
            if (expr.GetThisType() == nullptr) {
                std::string info = expr.GetResult()->ToString() + ", callee is method func, but don't have ThisType.";
                ErrorInFunc(expr.GetTopLevelFunc(), info);
                return false;
            }
        }
        if (!CheckInstantiatedType(expr,
            Cangjie::StaticCast<FuncType*>(func->GetType()), IsConstructor(*func))) {
            return false;
        }

        // check arg size
        auto args = expr.GetArgs();
        auto funcParamsTy = funcType->GetParamTypes();
        if (args.size() != funcParamsTy.size()) {
            std::string info = "value " + result->GetIdentifier() + " has " + std::to_string(args.size()) +
                " arguments but the function type has " + std::to_string(funcParamsTy.size()) + " parameters.";
            ErrorInFunc(expr.GetTopLevelFunc(), info);
            return false;
        }

        // check ref info of args
        for (size_t i = 0; i < args.size(); i++) {
            bool refOK = true;
            if (i == 0 && (func->TestAttr(Attribute::MUT) || IsConstructor(*func) || IsInstanceVarInit(*func))) {
                refOK = CheckFuncArgType(*args[i]->GetType(), funcType->IsCFunc(), true);
            } else {
                refOK = CheckFuncArgType(*args[i]->GetType(), funcType->IsCFunc());
            }
            if (!refOK) {
                std::string errMsg =
                    "reference info of arg " + args[i]->ToString() + " in " + result->ToString() + " is error.";
                Errorln(errMsg);
                return false;
            }
        }
        return true;
    }

    // used for invoke and tryWithInvoke input object check
    bool IsClassRefSubType(const Type& leaf, const Type& root) const
    {
        if (!leaf.IsRef() || !root.IsRef()) {
            return false;
        }
        auto leafTy = Cangjie::StaticCast<const RefType&>(leaf).GetBaseType();
        auto rootTy = Cangjie::StaticCast<const RefType&>(root).GetBaseType();
        return leafTy->IsEqualOrSubTypeOf(*rootTy, builder);
    }

    // Invoke method type must be funcType.
    // Invoke result type must same with func return type, and input types must same with func input types
    // Invoke object type must be subtype of func first input type
    bool CheckInvoke(const Invoke& expr) const
    {
        // Need to adapt here.
        (void)expr;
        return true;
    }

    // InvokeStatic method type must be funcType.
    // InvokeStatic result type must same with func return type, and input types must same with func input types
    template <typename T> bool CheckInvokeStatic(const T& expr) const
    {
        auto type = expr.GetMethodType();
        auto result = expr.GetResult();
        if (!type->IsFunc()) {
            auto err = "method " + expr.GetMethodName() + " used in " + result->GetIdentifier() + " has type " +
                type->ToString() + " but type FuncType is expected.";
            ErrorInFunc(expr.GetTopLevelFunc(), err);
            return false;
        }
        auto funcType = Cangjie::StaticCast<FuncType*>(type);
        const auto& args = expr.GetOperands();
        if (!CheckInstantiatedType(expr, Cangjie::StaticCast<FuncType*>(type), false)) {
            return false;
        }
        // check ref info of args
        for (size_t i = 0; i < args.size(); i++) {
            bool refOK = CheckFuncArgType(*args[i]->GetType(), funcType->IsCFunc());
            if (!refOK) {
                std::string errMsg =
                    "reference info of arg " + args[i]->ToString() + " in " + result->ToString() + " is error.";
                Errorln(errMsg);
                return false;
            }
        }

        auto rttiValue = expr.GetRTTIValue();
        auto rttiExpr = Cangjie::StaticCast<LocalVar>(rttiValue)->GetExpr();
        if (!Cangjie::Is<GetRTTI>(rttiExpr) && !Cangjie::Is<GetRTTIStatic>(rttiExpr)) {
            ErrorInFunc(rttiExpr->GetTopLevelFunc(),
                "RTTI value of InvokeStatic must be either GetRTTI or GetRTTIStatic, got " + rttiValue->ToString());
            return false;
        }
        return true;
    }

    bool IsClassRefType(const Type& type) const // should be replaced by Type::IsClassRef on dev.
    {
        if (auto reft = Cangjie::DynamicCast<const RefType*>(&type)) {
            return reft->GetBaseType()->IsClass();
        }
        return false;
    }

    bool IsBoxRefType(const Type& type) const // should be replaced by Type::IsClassRef on dev.
    {
        if (auto reft = Cangjie::DynamicCast<const RefType*>(&type)) {
            return reft->GetBaseType()->IsBox();
        }
        return false;
    }

    // InstanceOf return type must be Boolean.
    bool CheckInstanceOf(const InstanceOf& expr) const
    {
        auto result = expr.GetResult();
        if (!result->GetType()->IsBoolean()) {
            TypeCheckError(*result, "Boolean");
            return false;
        }
        // instanceof rule:
        // 1. both value and targetType are & to class type (which may be boxed type)
        // 2. the sourceType or the targetType generic is generic
        auto targetTy = expr.GetType();
        if (!CheckGenericArgs(*targetTy)) {
            return false;
        }
        return true;
    }

    Type* GetRefBaseType(Type& ty)
    {
        if (!ty.IsRef()) {
            return &ty;
        }
        return GetRefBaseType(*Cangjie::StaticCast<RefType&>(ty).GetBaseType());
    }

    bool CheckTypeCastEnum(
        const Expression& expr, const Ptr<Type> srcTy, const Ptr<Type> targetTy, const Ptr<Value> result) const
    {
        auto enumTy = Cangjie::StaticCast<const EnumType*>(srcTy);
        if (enumTy->GetEnumDef()->IsAllCtorsTrivial()) {
            // expected: Trivial Enum -> UInt32
            if (targetTy->GetTypeKind() != Type::TypeKind::TYPE_UINT32) {
                TypeCheckError(*result, "UInt32");
                return false;
            }
        } else {
            // expected: Trivial Enum -> UInt32
            if (targetTy->GetTypeKind() == Type::TypeKind::TYPE_UINT32) {
                TypeCheckError(expr, *expr.GetOperand(0), "trivial Enum");
                return false;
            }
            // expected: Non-Trivial Enum -> Tuple
            if (!targetTy->IsTuple()) {
                TypeCheckError(*result, "Tuple");
                return false;
            }
        }
        return true;
    }

    bool CheckTypeCastInteger(const Ptr<Type> srcTy, const Ptr<Type> targetTy, const Ptr<Value> result) const
    {
        std::string expTy = "Integer Float or Rune";
        // Integer to Rune, Integer, Float
        if (targetTy->IsInteger() || targetTy->IsFloat() || targetTy->IsRune()) {
            return true;
        }
        // UInt32 to Enum
        if (srcTy->GetTypeKind() == Type::TypeKind::TYPE_UINT32) {
            expTy = "Integer, Float, Rune or trivial Enum";
            if (targetTy->IsEnum()) {
                auto enumTy = Cangjie::StaticCast<const EnumType*>(targetTy);
                if (enumTy->GetEnumDef()->IsAllCtorsTrivial()) {
                    return true;
                }
            }
        }
        TypeCheckError(*result, expTy);
        return false;
    }

    bool CheckTypeCast(const Expression& expr) const
    {
        // Please have a try on:
        // class CA<T> {
        //     var x: T
        //     init(_x: T) { x = _x }
        // }
        // class CA2 {
        //     var x: Int64
        //     init(_x: Int64) { x = _x }
        // }
        // interface I1 {
        //     func foo(): Int64
        // }
        // extend<T> CA<T> <: I1  {
        //     public func foo(): Int64 {
        //         6
        //     }
        // }
        // class CC {
        //     var x = CA<Int64>(0)
        //     var y = 5
        // }
        // main() {
        //     var x: I1 = CC().x  // <--- an error reported here
        //     x.foo()
        // }
        (void)expr;
        return true;
    }

    bool CheckGetException(const GetException& expr) const
    {
        auto result = expr.GetResult();
        auto expTy = result->GetType();
        if (!expTy->IsRef() || !expTy->GetTypeArgs()[0]->IsClass()) {
            TypeCheckError(*result, "Class&");
            return false;
        }
        return true;
    }

    // terminal check
    bool JumpFieldCheck(const Ptr<Block> srcBlock, const Ptr<Block> dstBlock) const
    {
        if (IsEndPhase() && dstBlock->GetParentBlockGroup() != srcBlock->GetParentBlockGroup()) {
            ErrorInFunc(srcBlock->GetTopLevelFunc(),
                "terminator in block " + srcBlock->GetIdentifier() + " jump to a unreachable block " +
                    dstBlock->GetIdentifier() + ".");
            return false;
        }
        return true;
    }

    // terminator must jump to reachable blocks
    bool CheckTerminator(const Terminator& expr) const
    {
        auto srcBlock = expr.GetParentBlock();
        bool ret = true;
        auto size = expr.GetNumOfSuccessor();
        for (size_t i = 0; i < size; i++) {
            auto successor = expr.GetSuccessor(i);
            CJC_NULLPTR_CHECK(successor);
            ret = JumpFieldCheck(srcBlock, successor) && ret;
        }
        const std::unordered_map<ExprKind, std::function<void()>> actionMap = {
            {ExprKind::BRANCH, [&ret, &expr, this]() { ret = CheckBranch(Cangjie::StaticCast<const Branch&>(expr)); }},
            {ExprKind::MULTIBRANCH,
                [&ret, &expr, this]() { ret = CheckMultiBranch(Cangjie::StaticCast<const MultiBranch&>(expr)); }},
            {ExprKind::APPLY_WITH_EXCEPTION,
                [&ret, &expr, this]() {
                    ret = CheckApplyWithException(Cangjie::StaticCast<const ApplyWithException&>(expr));
                }},
            {ExprKind::INVOKE_WITH_EXCEPTION,
                [&ret, &expr, this]() {
                    ret = CheckInvokeWithException(Cangjie::StaticCast<const InvokeWithException&>(expr));
                }},
            {ExprKind::INVOKESTATIC_WITH_EXCEPTION,
                [&ret, &expr, this]() {
                    ret = CheckInvokeStatic(Cangjie::StaticCast<const InvokeStaticWithException&>(expr));
                }},
            {ExprKind::GOTO, [&ret, &expr, this]() { ret = CheckGoTo(Cangjie::StaticCast<const GoTo&>(expr)); }},
            {ExprKind::EXIT, [&ret, &expr, this]() { ret = CheckExit(Cangjie::StaticCast<const Exit&>(expr)); }},
            {ExprKind::RAISE_EXCEPTION,
                [&ret, &expr, this]() { ret = CheckRaiseException(Cangjie::StaticCast<const RaiseException&>(expr)); }},
            {ExprKind::INT_OP_WITH_EXCEPTION,
                [&ret, &expr, this]() {
                    ret = CheckIntOpWithException(Cangjie::StaticCast<const IntOpWithException&>(expr));
                }},
            {ExprKind::SPAWN_WITH_EXCEPTION, [&ret, &expr, this]() { ret = CheckSpawnWithException(expr); }},
            {ExprKind::TYPECAST_WITH_EXCEPTION,
                [&ret, &expr, this]() {
                    ret = CheckTypeCastWithException(Cangjie::StaticCast<const TypeCastWithException&>(expr));
                }},
            {ExprKind::INTRINSIC_WITH_EXCEPTION,
                [&ret, &expr, this]() {
                    ret = CheckIntrinsicWithException(Cangjie::StaticCast<const IntrinsicWithException&>(expr));
                }},
            {ExprKind::ALLOCATE_WITH_EXCEPTION,
                [&ret, &expr, this]() {
                    ret = CheckAllocateWithException(Cangjie::StaticCast<const AllocateWithException&>(expr));
                }},
            {ExprKind::RAW_ARRAY_ALLOCATE_WITH_EXCEPTION, [&ret, &expr, this]() {
                 ret = CheckRawArrayAllocateWithException(
                     Cangjie::StaticCast<const RawArrayAllocateWithException&>(expr));
             }}};
        if (auto iter = actionMap.find(expr.GetExprKind()); iter != actionMap.end()) {
            iter->second();
        } else {
            WarningInFunc(expr.GetTopLevelFunc(), "find unrecongnized ExprKind " + expr.GetExprKindName() + ".");
        }
        return ret;
    }

    bool CheckBranch(const Branch& expr) const
    {
        auto cond = expr.GetOperand(0);
        if (!cond->GetType()->IsBoolean() && !cond->GetType()->IsNothing()) {
            TypeCheckError(expr, *cond, "Boolean");
            return false;
        }
        return true;
    }

    bool CheckMultiBranch(const MultiBranch& expr) const
    {
        auto cond = expr.GetCondition();
        if (!cond->GetType()->IsInteger()) {
            TypeCheckError(expr, *cond, "Integer");
            return false;
        }
        return true;
    }

    bool CheckApplyWithException(const ApplyWithException& expr) const
    {
        auto func = expr.GetCallee();
        auto type = func->GetType();
        auto result = expr.GetResult();
        if (!IsFuncType(*type)) {
            TypeCheckError(expr, *func, "FuncType");
            return false;
        }
        auto funcType = GetFuncType(*type);
        auto args = expr.GetArgs();
        auto funcParamsTy = funcType->GetParamTypes();
        if (args.size() != funcParamsTy.size()) {
            std::string info = "value " + result->GetIdentifier() + " has " + std::to_string(args.size()) +
                " arguments but the function type has " + std::to_string(funcParamsTy.size()) + " parameters.";
            ErrorInFunc(expr.GetTopLevelFunc(), info);
            return false;
        }
        if (auto callee = Cangjie::DynamicCast<FuncBase*>(func); callee && callee->IsMemberFunc()) {
            if (expr.GetThisType() == nullptr) {
                std::string info = expr.GetResult()->ToString() + ", callee is method func, but don't have ThisType.";
                ErrorInFunc(expr.GetTopLevelFunc(), info);
                return false;
            }
        }
        if (!CheckInstantiatedType(expr,
            Cangjie::StaticCast<FuncType*>(func->GetType()), IsConstructor(*func))) {
            return false;
        }
        // check ref info of args
        for (size_t i = 0; i < args.size(); i++) {
            bool refOK = true;
            if (i == 0 && (func->TestAttr(Attribute::MUT) || IsConstructor(*func) || IsInstanceVarInit(*func))) {
                refOK = CheckFuncArgType(*args[i]->GetType(), funcType->IsCFunc(), true);
            } else {
                refOK = CheckFuncArgType(*args[i]->GetType(), funcType->IsCFunc());
            }
            if (!refOK) {
                std::string errMsg =
                    "reference info of arg " + args[i]->ToString() + " in " + result->ToString() + " is error.";
                Errorln(errMsg);
                return false;
            }
        }
        return true;
    }

    bool CheckInvokeWithException(const InvokeWithException& expr) const
    {
        // Need to adapt
        (void)expr;
        return true;
    }

    bool CheckGoTo(const GoTo& expr) const
    {
        if (auto result = expr.GetResult(); result != nullptr) {
            TypeCheckError(*result, "nullptr");
            return false;
        }
        return true;
    }

    bool CheckExit(const Exit& expr) const
    {
        if (auto result = expr.GetResult(); result != nullptr) {
            TypeCheckError(*result, "nullptr");
            return false;
        }
        return true;
    }

    bool CheckRaiseException(const RaiseException& expr) const
    {
        if (auto result = expr.GetResult(); result != nullptr) {
            TypeCheckError(*result, "nullptr");
            return false;
        }
        auto expTy = expr.GetExceptionValue()->GetType();
        if (!expTy->IsRef() || !expTy->GetTypeArgs()[0]->IsClass()) {
            if (!expTy->IsGeneric()) {
                TypeCheckError(expr, *expr.GetExceptionValue(), "Class&");
                return false;
            }
        }
        return true;
    }

    bool CheckIntOpWithException(const IntOpWithException& expr) const
    {
        auto exprKind = expr.GetOpKind();
        if (exprKind >= ExprKind::ADD && exprKind <= ExprKind::MOD) {
            return CheckCalculExpression(expr);
        } else if (exprKind == ExprKind::EXP) {
            return CheckExpExpression(expr);
        } else if (exprKind >= ExprKind::LSHIFT && exprKind <= ExprKind::BITXOR) {
            return CheckBitExpression(expr);
        } else if (exprKind >= ExprKind::LT && exprKind <= ExprKind::NOTEQUAL) {
            return CheckCmpExpression(expr);
        } else if (exprKind >= ExprKind::AND && exprKind <= ExprKind::OR) {
            return CheckLogicExpression(expr);
        } else if (exprKind >= ExprKind::NEG && exprKind <= ExprKind::BITNOT) {
            return CheckUnaryExpression(expr);
        } else {
            auto result = expr.GetResult();
            TypeCheckError(*result, "Add, Sub, Mul, Div, Mod or Exp");
            return false;
        }
    }

    bool CheckSpawnWithException(const Expression& expr) const
    {
        return CheckSpawn(expr);
    }

    bool CheckTypeCastWithException(const TypeCastWithException& expr) const
    {
        return CheckTypeCast(expr);
    }

    bool CheckIntrinsicWithException(const IntrinsicWithException& expr) const
    {
        (void)expr;
        // To be implemented
        return true;
    }

    bool CheckAllocateWithException(const AllocateWithException& expr) const
    {
        auto result = expr.GetResult();
        auto resultTy = result->GetType();
        auto baseType = expr.GetType();
        if (!resultTy->IsRef() || baseType != Cangjie::StaticCast<RefType*>(resultTy)->GetBaseType()) {
            TypeCheckError(*result, baseType->ToString() + "&");
            return false;
        }
        if (!CheckGenericArgs(*baseType)) {
            return false;
        }
        return true;
    }

    // check result is RawArray& type
    // check size is Int64 type
    // check element type is RawArray element type
    bool CheckRawArrayAllocateWithException(const RawArrayAllocateWithException& expr) const
    {
        auto result = expr.GetResult();
        if (!result->GetType()->IsRef() || !result->GetType()->GetTypeArgs()[0]->IsRawArray()) {
            TypeCheckError(*result, "RawArray&");
            return false;
        }
        auto size = expr.GetSize();
        if (!IsExpectedType(*size->GetType(), Type::TypeKind::TYPE_INT64)) {
            TypeCheckError(expr, *size, "Int64");
            return false;
        }
        auto rawArrayTy = result->GetType()->GetTypeArgs()[0];
        if (!IsExpectedType(*rawArrayTy->GetTypeArgs()[0], *expr.GetElementType())) {
            TypeCheckError(*result, "RawArray<" + expr.GetElementType()->ToString() + ">&");
            return false;
        }
        auto elementType = expr.GetElementType();
        if (!CheckGenericArgs(*elementType)) {
            return false;
        }
        return true;
    }

    bool CheckSpawn(const Expression& expr) const
    {
        if (expr.GetOperands().size() != 0) {
            auto ty = expr.GetOperand(0)->GetType();
            if (!(ty->IsRef() && ty->GetTypeArgs()[0]->IsClass())) {
                TypeCheckError(expr, *expr.GetOperand(0), "closure or ref to class");
                return false;
            }
        }
        return true;
    }

    // check result is RawArray& type
    // check size is Int64 type
    // check element type is RawArray element type
    bool CheckRawArrayAllocate(const RawArrayAllocate& expr) const
    {
        auto result = expr.GetResult();
        if (!result->GetType()->IsRef() || !result->GetType()->GetTypeArgs()[0]->IsRawArray()) {
            TypeCheckError(*result, "RawArray&");
            return false;
        }
        auto size = expr.GetSize();
        if (!IsExpectedType(*size->GetType(), Type::TypeKind::TYPE_INT64)) {
            TypeCheckError(expr, *size, "Int64");
            return false;
        }
        auto rawArrayTy = result->GetType()->GetTypeArgs()[0];
        if (!IsExpectedType(*rawArrayTy->GetTypeArgs()[0], *expr.GetElementType())) {
            TypeCheckError(*result, "RawArray<" + expr.GetElementType()->ToString() + ">&");
            return false;
        }
        auto elementType = expr.GetElementType();
        if (!CheckGenericArgs(*elementType)) {
            return false;
        }
        return true;
    }

    // check result is Unit type
    // check operand0 is RawArray type
    // check other operands is the element type of operand0
    bool CheckRawArrayLiteralInit(const RawArrayLiteralInit& expr) const
    {
        auto result = expr.GetResult();
        if (!result->GetType()->IsUnit()) {
            TypeCheckError(*result, "Unit");
            return false;
        }
        auto rawArray = expr.GetRawArray();
        if (!rawArray->GetType()->IsRef() || !rawArray->GetType()->GetTypeArgs()[0]->IsRawArray()) {
            TypeCheckError(expr, *rawArray, "RawArray&");
            return false;
        }
        auto rawArrayTy = rawArray->GetType()->GetTypeArgs()[0];
        for (auto elem : expr.GetElements()) {
            if (!IsExpectedType(*rawArrayTy->GetTypeArgs()[0], *elem->GetType())) {
                TypeCheckError(expr, *elem, rawArrayTy->GetTypeArgs()[0]->ToString());
                return false;
            }
        }
        return true;
    }

    // check result is Unit type
    // check size is Int64 type
    // check operand0 is RawArray type
    // check size is Int64 type
    // check operand2 is RawArray element type
    bool CheckRawArrayInitByValue(const RawArrayInitByValue& expr) const
    {
        auto result = expr.GetResult();
        if (!result->GetType()->IsUnit()) {
            TypeCheckError(*result, "Unit");
            return false;
        }
        auto size = expr.GetSize();
        if (!IsExpectedType(*size->GetType(), Type::TypeKind::TYPE_INT64)) {
            TypeCheckError(expr, *size, "Int64");
            return false;
        }
        auto rawArray = expr.GetRawArray();
        if (!rawArray->GetType()->IsRef() || !rawArray->GetType()->GetTypeArgs()[0]->IsRawArray()) {
            TypeCheckError(expr, *rawArray, "RawArray&");
            return false;
        }
        auto rawArrayTy = rawArray->GetType()->GetTypeArgs()[0];
        if (!IsExpectedType(*rawArrayTy->GetTypeArgs()[0], *expr.GetInitValue()->GetType())) {
            TypeCheckError(expr, *expr.GetInitValue(), rawArrayTy->GetTypeArgs()[0]->ToString());
            return false;
        }
        return true;
    }

    bool CheckVArray(const VArray& expr) const
    {
        auto result = expr.GetResult();
        if (!result->GetType()->IsVArray()) {
            TypeCheckError(*result, "VArray");
            return false;
        }
        // size check
        const auto& args = expr.GetOperands();
        auto type = Cangjie::StaticCast<VArrayType*>(result->GetType());
        auto elemType = type->GetElementType();
        if (type->GetSize() != args.size()) {
            TypeCheckError(*result, "VArray<" + elemType->ToString() + ", $" + std::to_string(args.size()) + ">");
            return false;
        }
        // elements type check
        for (size_t i = 0; i < args.size(); i++) {
            auto argtype = args[i]->GetType();
            if (!IsExpectedType(*elemType, *argtype)) {
                TypeCheckError(expr, *args[i], elemType->ToString());
                return false;
            }
        }
        return true;
    }

    bool CheckVArrayBuilder(const VArrayBuilder& expr) const
    {
        auto result = expr.GetResult();
        auto size = expr.GetOperand(0);
        if (!IsExpectedType(*size->GetType(), Type::TypeKind::TYPE_INT64)) {
            TypeCheckError(expr, *size, "Int64");
            return false;
        }
        if (!result->GetType()->IsVArray()) {
            TypeCheckError(*result, "VArray");
            return false;
        }
        return true;
    }

    bool CheckIntrinsic(const Intrinsic& expr) const
    {
        // To be implemented
        (void)expr;
        return true;
    }

    bool CheckLambda(const Lambda& expr, std::set<const GenericType*>& genericTypes)
    {
        auto result = expr.GetResult();
        if (!result->GetType()->IsFunc()) {
            TypeCheckError(*result, "Func");
            return false;
        }
        auto funcTy = expr.GetFuncType();
        CJC_NULLPTR_CHECK(funcTy);
        CJC_NULLPTR_CHECK(expr.GetTopLevelFunc());
        auto ret = !expr.GetIdentifier().empty();
        ret = CheckFuncParams(expr.GetParams(), *funcTy, *expr.GetTopLevelFunc()) && ret;
        genericTypes.insert(expr.GetGenericTypeParams().begin(), expr.GetGenericTypeParams().end());
        ret = CheckFuncBody(*expr.GetBody(), genericTypes, false) && ret;
        ret = CheckFuncRetValue(*expr.GetTopLevelFunc(), *funcTy->GetReturnType(), expr.GetReturnValue()) && ret;
        ret = CheckGenericTypeValidInFunc(*expr.GetBody(), genericTypes) && ret;
        if (!ret) {
            expr.Dump();
        }
        for (auto ty : expr.GetGenericTypeParams()) {
            genericTypes.erase(ty);
        }
        return ret;
    }

    bool CheckGetInstantiateValue(const GetInstantiateValue& expr)
    {
        if (!IsEndPhase()) {
            return true;
        }
        std::string err = "expr: " + expr.ToString() + ", should be removed, can't be seen in CodeGen Stage.";
        ErrorInFunc(expr.GetTopLevelFunc(), err);
        return false;
    }

    Type* GetBaseTypeFromBoxRefType(Type& type)
    {
        auto refBaseType = Cangjie::StaticCast<RefType&>(type).GetBaseType();
        return Cangjie::StaticCast<BoxType*>(refBaseType)->GetBaseType();
    }

    bool CheckBox(const Box& expr)
    {
        auto result = expr.GetResult();
        // Box(%1: Int32, BoxType<Int32>&), is ok
        if (IsBoxRefType(*result->GetType()) &&
            GetBaseTypeFromBoxRefType(*result->GetType()) == expr.GetSourceTy()) {
            return true;
        }
        if (!IsClassRefType(*result->GetType()) && !IsBoxRefType(*result->GetType())) {
            TypeCheckError(*result, "class or box reference type");
            return false;
        }
        if (expr.GetSourceTy()->IsRef() &&
            Cangjie::StaticCast<RefType*>(expr.GetSourceTy())->GetBaseType()->IsReferenceType()) {
            std::stringstream ms{};
            ms << "In value " << expr.ToString()
               << ":\n"
                  "Operand must be value type, got "
               << expr.GetSourceTy()->ToString();
            Errorln(ms.str());
            return false;
        }
        return true;
    }

    bool CheckUnBox(const UnBox& expr) const
    {
        // Need to adapt
        (void)expr;
        return true;
    }

    bool CheckTransformToGeneric(const TransformToGeneric& expr)
    {
        auto result = expr.GetResult();
        if (!result->GetType()->IsGenericRelated()) {
            TypeCheckError(*result, "value type");
            return false;
        }
        return true;
    }

    bool CheckTransformToConcrete(const TransformToConcrete& expr)
    {
        if (!expr.GetSourceTy()->IsGenericRelated()) {
            TypeCheckError(*expr.GetResult(), "value type");
            return false;
        }
        return true;
    }

    bool CheckUnBoxToRef(const UnBoxToRef& expr)
    {
        auto expectTy = expr.GetResult()->GetType();
        auto isExpTyValueReference = expectTy->IsRef() &&
            Cangjie::StaticCast<RefType*>(expectTy)->GetBaseType()->IsValueType();
        if (!isExpTyValueReference) {
            TypeCheckError(*expr.GetResult(), "value type with reference");
            return false;
        }

        auto srcTy = expr.GetSourceTy();
        auto srcIsReferenceTy = srcTy->IsRef() &&
            Cangjie::StaticCast<RefType*>(srcTy)->GetBaseType()->IsReferenceType();
        if (!srcIsReferenceTy) {
            TypeCheckError(*expr.GetSourceValue(), "reference type");
            return false;
        }
        return true;
    }

    bool IsEndPhase() const
    {
        return curCheckPhase == ToCHIR::Phase::OPT || curCheckPhase == ToCHIR::Phase::ANALYSIS_FOR_CJLINT;
    }
};

ToCHIR::Phase IRChecker::curCheckPhase = ToCHIR::Phase::OPT;

} // unnamed namespace

namespace Cangjie::CHIR {
bool IRCheck(const Package& root, const Cangjie::GlobalOptions& opts, CHIRBuilder& builder, const ToCHIR::Phase& phase,
    std::ostream& out)
{
    return IRChecker::Check(root, out, opts, builder, phase);
}
} // namespace Cangjie::CHIR
