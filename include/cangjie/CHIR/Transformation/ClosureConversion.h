// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_CLOSURE_CONVERSION_H
#define CANGJIE_CHIR_TRANSFORMATION_CLOSURE_CONVERSION_H

#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/Option/Option.h"

namespace Cangjie::CHIR {
/**
 * CHIR Normal Pass: mainly change lambda to normal global func and deal with related issues.
 */
class ClosureConversion {
public:
    /**
     * @brief constructor to do closure conversion.
     * @param package input package to do closure conversion.
     * @param builder CHIR builder for generating IR.
     * @param opts global options from Cangjie inputs.
     * @param srcCodeImportedFuncMap
     */
    ClosureConversion(Package& package, CHIRBuilder& builder, const GlobalOptions& opts,
        const std::unordered_set<Func*>& srcCodeImportedFuncs);

    /**
     * @brief Main process to do closure conversion.
     */
    void Convert();

    /**
     * @brief Get mangle name from functions generated from closure conversion.
     * @return mangle names generated from closure conversion.
     */
    std::set<std::string> GetCCOutFuncsRawMangle() const;

    /**
     * @brief Get useless class def after closure conversion, will delete in remove useless pass.
     * @return useless class def after closure conversion.
     */
    std::unordered_set<ClassDef*> GetUselessClassDef() const;

    /**
     * @brief Get useless lambda after closure conversion, will delete in remove useless pass.
     * @return useless lambda after closure conversion.
     */
    std::unordered_set<Func*> GetUselessLambda() const;

private:
    enum class PrintType { NESTED_FUNC, TOP_LEVEL_FUNC, BOX_MUT_VAR };
    void ConvertGlobalFunctions();
    std::vector<Lambda*> CollectNestedFunctions();
    void InlineLambda(const std::vector<Lambda*>& funcs);
    void ConvertNestedFunctions(const std::vector<Lambda*>& funcs);
    void ConvertImportedFunctions();

    Ptr<LocalVar> CreateAutoEnvImplObject(
        Block& parent, ClassType& autoEnvImplType, const std::vector<Value*>& envs, Expression& user, Value& srcFunc);

    void LiftType();
    void LiftGenericTypes();

    Type* ConvertTypeToClosureType(Type& type);
    Type* ConvertCompositionalType(const Type& type);
    FuncType* ConvertFuncArgsAndRetType(const FuncType& oldFuncTy);
    std::vector<Value*> BoxAllMutableVars(const std::vector<Value*>& rawEnvs);
    Value* BoxMutableVar(LocalVar& env);
    ClassDef* CreateBoxClassDef(Type& type);
    LocalVar* CreateBoxClassObj(const LocalVar& env, const ClassDef& classDef);
    void ReplaceEnvWithBoxObjMemberVar(LocalVar& env, LocalVar& boxObj, LocalVar& lValue);
    void LiftNestedFunctionWithCFuncType(Lambda& nestedFunc);
    std::pair<LocalVar*, LocalVar*> SetBoxClassAsMutableVar(LocalVar& rValue);
    void RecordDuplicateLambdaName(const Lambda& func);
    std::string GenerateGlobalFuncIdentifier(const Lambda& lambda);

    ClassType* GenerateInstantiatedClassType(ClassType& autoEnvImplType, const Expression& user, Value& srcFunc);

    std::vector<Type*> ConvertArgsType(const std::vector<Type*>& types);
    Type* ConvertTupleType(const TupleType& type);
    Type* ConvertFuncType(const FuncType& type);
    Type* ConvertEnumType(const EnumType& type);
    Type* ConvertStructType(const StructType& type);
    Type* ConvertClassType(const ClassType& type);
    Type* ConvertRawArrayType(const RawArrayType& type);
    Type* ConvertVArrayType(const VArrayType& type);
    Type* ConvertCPointerType(const CPointerType& type);
    Type* ConvertRefType(const RefType& type);
    Type* ConvertBoxType(const BoxType& type);

    Package& package;
    CHIRBuilder& builder;
    ClassDef& objClass;
    const GlobalOptions& opts;
    const std::unordered_set<Func*>& srcCodeImportedFuncs;

    // key: any type, value: closure type
    std::unordered_map<const Type*, Type*> typeConvertMap;
    std::unordered_map<Type*, ClassDef*> boxClassMap;
    std::unordered_map<const Lambda*, Func*> convertedCache;
    std::unordered_map<std::string, size_t> duplicateLambdaName;

    ClassDef* GetOrCreateGenericAutoEnvBaseDef(size_t paramNum);
    ClassDef* GetOrCreateAutoEnvBaseDef(const FuncType& funcType);
    ClassDef* CreateAutoEnvImplDef(const std::string& className, const std::vector<GenericType*>& genericTypes,
        const Value& srcFunc, ClassDef& superClassDef,
        std::unordered_map<const GenericType*, Type*>& originalTypeToNewType);
    ClassDef* GetOrCreateAutoEnvImplDef(FuncBase& func, ClassDef& superClassDef);
    ClassDef* GetOrCreateAutoEnvImplDef(Lambda& func, ClassDef& superClassDef, const std::vector<Value*>& boxedEnvs);
    void CreateInstOverrideMethodInAutoEnvImplDef(ClassDef& autoEnvImplDef,
        FuncBase& srcFunc, const std::unordered_map<const GenericType*, Type*>& originalTypeToNewType);
    void CreateGenericOverrideMethodInAutoEnvImplDef(ClassDef& autoEnvImplDef, FuncBase& srcFunc,
        const std::unordered_map<const GenericType*, Type*>& originalTypeToNewType);
    void CreateMemberVarInAutoEnvImplDef(ClassDef& parentClass, const std::vector<Value*>& boxedEnvs,
        const std::unordered_map<const GenericType*, Type*>& originalTypeToNewType);
    void ReplaceUserPoint(FuncBase& srcFunc, Expression& user, ClassDef& autoEnvImplDef);
    void ReplaceUserPoint(
        Lambda& srcFunc, Expression& user, const std::vector<Value*>& envs, ClassDef& autoEnvImplDef);
    void ConvertExpressions();
    void ConvertApplyWithExceptionToInvokeWithException(const std::vector<ApplyWithException*>& applyExprs);
    void ConvertApplyToInvoke(const std::vector<Apply*>& applyExprs);
    void CreateVTableForAutoEnvDef();
    bool LambdaCanBeInlined(const Expression& user, const FuncBase& lambda);
    void DoFunctionInlineForLambda();

    ClassDef* GetOrCreateInstAutoEnvBaseDef(const FuncType& funcType, ClassDef& superClass);
    Func* LiftLambdaToGlobalFunc(
        ClassDef& autoEnvImplDef, Lambda& nestedFunc, const std::vector<GenericType*>& genericTypeParams,
        const std::unordered_map<const GenericType*, Type*>& instMap, const std::vector<Value*>& capturedValues);
    void ModifyTypeMismatchInExpr();
    void WrapApplyRetVal(Apply& apply);
    void WrapApplyWithExceptionRetVal(ApplyWithException& apply);
    void WrapInvokeRetVal(Expression& e);
    void WrapInvokeWithExceptionRetVal(Expression& e);
    void WrapGetElementRefRetVal(GetElementRef& getEleRef);
    void WrapFieldRetVal(Field& field);
    void WrapTypeCastSrcVal(TypeCast& typecast);
    ClassDef* GetOrCreateAutoEnvWrapper(ClassType& instAutoEnvBaseType);
    ClassDef* CreateAutoEnvWrapper(const std::string& className, ClassType& superClassType);
    void CreateMemberVarInAutoEnvWrapper(ClassDef& autoEnvWrapperDef);
    Func* CreateGenericMethodInAutoEnvWrapper(ClassDef& autoEnvWrapperDef);
    void CreateInstMethodInAutoEnvWrapper(ClassDef& autoEnvWrapperDef, Func& genericFunc);

    std::unordered_map<std::string, ClassDef*> genericAutoEnvBaseDefs;
    std::unordered_map<std::string, ClassDef*> instAutoEnvBaseDefs;
    std::unordered_map<std::string, ClassDef*> instAutoEnvWrapperDefs;
    std::unordered_map<std::string, ClassDef*> autoEnvImplDefs;

    std::unordered_set<GenericType*> needConvertedGenericTys;
    std::set<std::string> ccOutFuncsRawMangle;

    std::unordered_set<ClassDef*> uselessClasses;
    std::unordered_set<Func*> uselessLambda;
};
} // namespace Cangjie::CHIR
#endif
