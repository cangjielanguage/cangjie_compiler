// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/CHIR.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Visitor/Visitor.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ProfileRecorder.h"

namespace Cangjie::CHIR {
namespace {
struct UnusedImportAnalysis {
public:
    UnusedImportAnalysis(
        const std::unordered_map<std::string, FuncBase*>& implicitFuncs, bool incr, bool skipVirtualFunc = true)
        : implicitFuncs(implicitFuncs), incr(incr), skipVirtualFunc(skipVirtualFunc) {}
    bool Judge(const ImportedValue& val)
    {
        if (incr || val.TestAttr(Attribute::NON_RECOMPILE)) {
            return false;
        }
        if (auto func = DynamicCast<const ImportedFunc*>(&val)) {
            // 1. implicit imported functions will be used in runtime
            if (implicitFuncs.find(func->GetIdentifierWithoutPrefix()) != implicitFuncs.end()) {
                return false;
            }
            // 2. Future::execute defined in std.core will be used in codegen
            auto parentDef = func->GetParentCustomTypeDef();
            if (parentDef != nullptr && CheckCustomTypeDefIsExpected(*parentDef, CORE_PACKAGE_NAME, STD_LIB_FUTURE) &&
                (func->GetSrcCodeIdentifier() == "execute" || func->GetSrcCodeIdentifier() == "executeClosure")) {
                return false;
            }
            // 3. if func is virtual, it must as a placeholder in vtable right now
            if (skipVirtualFunc && func->IsMemberFunc() && func->IsVirtualFunc()) {
                return false;
            }
            // 4. finalizer may be used in runtime
            if (func->GetFuncKind() == FuncKind::FINALIZER) {
                return false;
            }
        }

        // 5. not used function or static variable can be removed
        return val.GetUsers().empty();
    }

    void SetWhetherSkipVirtualFunc(bool skip)
    {
        skipVirtualFunc = skip;
    }
private:
    const std::unordered_map<std::string, FuncBase*>& implicitFuncs;
    bool incr;
    bool skipVirtualFunc;
};

class CollectUsedImports {
public:
    CollectUsedImports(UnusedImportAnalysis& unusedImportAnalysis, bool incr)
        : unusedImportAnalysis(unusedImportAnalysis), isIncremental(incr) {}
    // used imported decls, separated from source package decls to avoid dynamic_cast
    std::unordered_set<ImportedValue*> used;

    // checked source package decls. These containers exclude used imported decls, as an imported decl is checked iff
    // it is used
    std::unordered_set<GlobalVarBase*> checkedVars;
    std::unordered_set<FuncBase*> checkedFuns;
    std::unordered_set<CustomTypeDef*> checkedDefs;
    std::unordered_set<Type*> checkedTys;
    
    void Collect(Package& package, const std::unordered_map<std::string, FuncBase*>& implicitFuncs)
    {
        for (auto& v : implicitFuncs) {
            // for std.core, these implicit funcs are 'from' the source package. for any other package, they are
            // imported funcs.
            VisitValue(*v.second);
        }
        for (auto v : package.GetImportedVarAndFuncs()) {
            VisitImported(*v);
        }
        for (auto v : package.GetGlobalVars()) {
            VisitVar(*v);
        }
        for (auto v : package.GetGlobalFuncs()) {
            if (v->Get<WrappedRawMethod>() != nullptr && v->TestAttr(Attribute::IMPORTED)) {
                continue;
            }
            VisitFunc(*v);
        }
        for (auto v : package.GetClasses()) {
            VisitTypeDef(*v);
        }
        for (auto v : package.GetStructs()) {
            VisitTypeDef(*v);
        }
        for (auto v : package.GetEnums()) {
            VisitTypeDef(*v);
        }
    }

private:
    void VisitVar(GlobalVarBase& var)
    {
        if (auto [_, ins] = checkedVars.insert(&var); !ins) {
            return;
        }
        VisitType(*var.GetType());
        if (auto gv = var.GetParentCustomTypeDef()) {
            VisitTypeDef(*gv);
        }
    }

    void VisitFunc(FuncBase& func)
    {
        if (auto [_, ins] = checkedFuns.insert(&func); !ins) {
            return;
        }
        VisitType(*func.GetType());
        for (auto ty : func.GetGenericTypeParams()) {
            VisitType(*ty);
        }
        if (auto def = func.GetParentCustomTypeDef()) {
            VisitTypeDef(*def);
        }
        if (auto fun = DynamicCast<Func>(&func)) {
            VisitBG(*fun->GetBody());
        }
    }

    void VisitType(Type& ty)
    {
        if (auto [_, ins] = checkedTys.insert(&ty); !ins) {
            return;
        }
        if (auto classTy = DynamicCast<CustomType>(&ty)) {
            VisitTypeDef(*classTy->GetCustomTypeDef());
        } else if (auto genericTy = DynamicCast<GenericType>(&ty)) {
            for (auto upperBound : genericTy->GetUpperBounds()) {
                VisitType(*upperBound);
            }
        }
        for (auto argTy : ty.GetTypeArgs()) {
            VisitType(*argTy);
        }
    }

    void VisitTypeDef(const CustomTypeDef& def)
    {
        if (auto [_, ins] = checkedDefs.insert(const_cast<CustomTypeDef*>(&def)); !ins) {
            return;
        }
        if (auto ex = DynamicCast<ExtendDef*>(&def)) {
            VisitType(*ex->GetExtendedType());
        } else {
            VisitType(*def.GetType());
        }
        for (auto ty : def.GetGenericTypeParams()) {
            VisitType(*ty);
        }
        for (auto& member : def.GetAllInstanceVars()) {
            VisitType(*member.type);
        }
        for (auto& member : def.GetStaticMemberVars()) {
            VisitType(*member->GetType());
        }
        for (auto& member : def.GetMethods()) {
            VisitType(*member->GetFuncType());
        }
        for (auto type : def.GetImplementedInterfaceTys()) {
            VisitType(*type);
        }
        if (auto cl = DynamicCast<const ClassDef>(&def)) {
            if (cl->GetSuperClassTy()) {
                VisitType(*cl->GetSuperClassTy());
            }
            for (auto& method : cl->GetAbstractMethods()) {
                VisitType(*method.methodTy);
            }
        } else if (auto enu = DynamicCast<const EnumDef>(&def)) {
            for (auto ctor : enu->GetCtors()) {
                VisitType(*ctor.funcType);
            }
        }
        for (auto vTable : def.GetVTable()) {
            for (auto funcInfo : vTable.second) {
                if (funcInfo.instance) {
                    VisitValue(*funcInfo.instance);
                }
            }
        }
    }
    
    /// Begin visit expression methods
    void VisitExpression(Expression& e)
    {
        if (e.GetResult()) {
            VisitType(*e.GetResultType());
        }
        for (unsigned i{0}; i < e.GetNumOfOperands(); ++i) {
            VisitValue(*e.GetOperand(i));
        }
        for (auto bg : e.GetBlockGroups()) {
            VisitBG(*bg);
        }
        if (auto ins = DynamicCast<InstanceOf>(&e)) {
            VisitType(*ins->GetType());
        }
        // we are planning to give the following six classes a common interface.
        if (auto apply = DynamicCast<Apply>(&e)) {
            for (auto type : apply->GetInstantiatedTypeArgs()) {
                VisitType(*type);
            }
            if (apply->GetThisType()) {
                VisitType(*apply->GetThisType());
            }
        }
        if (auto apply = DynamicCast<ApplyWithException>(&e)) {
            for (auto type : apply->GetInstantiatedTypeArgs()) {
                VisitType(*type);
            }
            if (apply->GetThisType()) {
                VisitType(*apply->GetThisType());
            }
        }
        if (auto invoke = DynamicCast<Invoke>(&e)) {
            for (auto type : invoke->GetInstantiatedTypeArgs()) {
                VisitType(*type);
            }
            VisitType(*invoke->GetThisType());
            VisitType(*invoke->GetMethodType());
        }
        if (auto invoke = DynamicCast<InvokeStatic>(&e)) {
            for (auto type : invoke->GetInstantiatedTypeArgs()) {
                VisitType(*type);
            }
            VisitType(*invoke->GetThisType());
            VisitType(*invoke->GetMethodType());
        }
        if (auto invoke = DynamicCast<InvokeWithException>(&e)) {
            for (auto type : invoke->GetInstantiatedTypeArgs()) {
                VisitType(*type);
            }
            VisitType(*invoke->GetThisType());
            VisitType(*invoke->GetMethodType());
        }
        if (auto invoke = DynamicCast<InvokeStaticWithException>(&e)) {
            for (auto type : invoke->GetInstantiatedTypeArgs()) {
                VisitType(*type);
            }
            VisitType(*invoke->GetThisType());
            VisitType(*invoke->GetMethodType());
        }
        if (auto inst = DynamicCast<GetInstantiateValue>(&e)) {
            for (auto type : inst->GetInstantiateTypes()) {
                VisitType(*type);
            }
        }
    }

    void VisitValue(Value& v)
    {
        auto value = &v;
        if (auto fun = DynamicCast<ImportedFunc>(value)) {
            if (!isIncremental || fun->TestAttr(Attribute::NON_RECOMPILE)) {
                VisitFunc(*fun);
            } else {
                VisitImported(*fun);
            }
        }
        if (auto var = DynamicCast<ImportedVar>(value)) {
            if (!isIncremental || var->TestAttr(Attribute::NON_RECOMPILE)) {
                VisitVar(*var);
            } else {
                VisitImported(*var);
            }
        }
        if (auto fun = DynamicCast<Func>(value)) {
            VisitFunc(*fun);
        }
        if (auto var = DynamicCast<GlobalVar>(value)) {
            VisitVar(*var);
        }
    }

    void VisitImported(ImportedValue& var)
    {
        if (auto [_, inserted] = used.insert(&var); !inserted) {
            return;
        }
        if (unusedImportAnalysis.Judge(var)) {
            return;
        }
        VisitType(*var.GetType());
        if (auto gv = DynamicCast<ImportedVar>(&var)) {
            if (auto cl = gv->GetParentCustomTypeDef()) {
                VisitTypeDef(*cl);
            }
        } else {
            auto func = StaticCast<ImportedFunc>(&var);
            if (auto cl = func->GetParentCustomTypeDef()) {
                VisitTypeDef(*cl);
            }
            for (auto ty : func->GetGenericTypeParams()) {
                VisitType(*ty);
            }
        }
    }

    void VisitBG(const BlockGroup& bg)
    {
        for (auto bl : bg.GetBlocks()) {
            VisitBlock(*bl);
        }
    }

    void VisitBlock(const Block& bl)
    {
        for (auto expr : bl.GetExpressions()) {
            VisitExpression(*expr);
        }
        if (bl.IsLandingPadBlock()) {
            for (auto type : bl.GetExceptions()) {
                VisitType(*type);
            }
        }
    }

    UnusedImportAnalysis& unusedImportAnalysis;
    bool isIncremental{false};
};

class UnusedImportRemover {
public:
    UnusedImportRemover(
        bool incr, const GlobalOptions& opts, const std::unordered_map<std::string, FuncBase*>& implicitFuncs)
        : isIncremental(incr), opts(opts), unusedImportAnalysis(implicitFuncs, incr), implicitFuncs(implicitFuncs)
    {
    }

    void Remove(Package& p)
    {
        // 1. remove func with virtual func remained
        unusedImportAnalysis.SetWhetherSkipVirtualFunc(true);
        RemoveImportedValueWithNoUsers(p);

        // 2. remove unused decls no matter whether func is virtual
        unusedImportAnalysis.SetWhetherSkipVirtualFunc(false);
        KeepUsedDecls(p);

        // 3. remove all virtual import func without customDef in package
        // reason: First step remain all virtual func, second step clear all custom type which are not used,
        //         so some virtual funcs does not have their parent custom type and need to be deleted.
        //         Do this only because virtual func need to be kept in vtable if custom type is remained,
        //         need to be deleted if custom type is deleted.
        RemoveAllVirtualFuncWithOutDef(p);
    }

private:
    /// Keep used decls by remove unused decls.
    /// 1. Traverse "roots" to mark all used decls. Roots include:
    ///     (1) implicitly imported values (accidentally they are all funcs)
    ///     (2) all source pkg var/func/typedefs
    ///     (3) imported values that have users (those without users have been removed by
    ///         \ref RemoveImportedValueWithNoUsers)
    /// 2. Rewrite imported collections of \p p with imported decls marked used
    void KeepUsedDecls(Package& p)
    {
        CollectUsedImports impl{unusedImportAnalysis, isIncremental};
        impl.Collect(p, implicitFuncs);
        AddImplicitUsedDef(impl, p);

        p.SetImportedVarAndFuncs(Keep(p.GetImportedVarAndFuncs(), impl.used, isIncremental));
        p.SetImportedStructs(Keep(p.GetImportedStructs(), impl.checkedDefs, isIncremental));
        p.SetImportedClasses(Keep(p.GetImportedClasses(), impl.checkedDefs, isIncremental));
        p.SetImportedEnums(Keep(p.GetImportedEnums(), impl.checkedDefs, isIncremental));
        p.SetImportedExtends(Keep(p.GetImportedExtends(), impl.checkedDefs, isIncremental));
    }

    /// Remove unused imported values (var/func) if they have no users
    void RemoveImportedValueWithNoUsers(Package& p)
    {
        std::vector<ImportedValue*> res;
        for (auto k : p.GetImportedVarAndFuncs()) {
            /// incremental unchanged decls are represented as ImportedValue(var/func) but they are from source package
            /// \ref p, so they should always be kept.
            if (unusedImportAnalysis.Judge(*k)) {
                k->DestroySelf();
            } else {
                res.push_back(k);
            }
        }
        p.SetImportedVarAndFuncs(std::move(res));
    }

    /// Keep \p toKeep of \p allDecls
    /// \param incremental in incremental compilation
    template <class All, class ToKeep>
    std::vector<All> Keep(
        const std::vector<All>& allDecls, const std::unordered_set<ToKeep>& toKeep, bool incremental) const
    {
        static_assert(std::is_convertible_v<All, ToKeep>);
        std::vector<All> res;
        for (auto decl : allDecls) {
            if (incremental && decl->TestAttr(Attribute::NON_RECOMPILE)) {
                res.push_back(decl);
                continue;
            }
            if (toKeep.find(decl) != toKeep.end()) {
                res.push_back(decl);
            }
        }
        return res;
    }

    void AddImplicitUsedDef(CollectUsedImports& impl, const Package& p) const
    {
        if (!opts.sancovOption.IsSancovEnabled()) {
            return;
        }
        for (auto s : p.GetImportedStructs()) {
            if (s->GetPackageName() == "std.core" && s->GetSrcCodeIdentifier() == "Array") {
                impl.checkedDefs.emplace(s);
            } else if (s->GetPackageName() == "std.core" && s->GetSrcCodeIdentifier() == "LibC") {
                impl.checkedDefs.emplace(s);
            }
        }
    }

    void RemoveAllVirtualFuncWithOutDef(Package& p) const
    {
        auto allDefs = p.GetAllCustomTypeDef();
        std::unordered_set<const CustomTypeDef*> allDefSet{allDefs.begin(), allDefs.end()};
        std::vector<ImportedValue*> res;
        for (auto k : p.GetImportedVarAndFuncs()) {
            if (!k->IsFunc()) {
                res.push_back(k);
                continue;
            }
            auto func = StaticCast<ImportedFunc*>(k);
            if (!func->IsVirtualFunc()) {
                res.push_back(k);
                continue;
            }
            if (allDefSet.count(func->GetParentCustomTypeDef()) == 0) {
                k->DestroySelf();
            } else {
                res.push_back(k);
            }
        }
        p.SetImportedVarAndFuncs(std::move(res));
    }

    bool isIncremental{false};
    const GlobalOptions& opts;
    UnusedImportAnalysis unusedImportAnalysis;
    const std::unordered_map<std::string, FuncBase*>& implicitFuncs;
};

void CreateNewExtendDef(Package& package, CustomTypeDef& curDef, ClassType& parentType,
    const std::vector<VirtualFuncInfo>& virtualFunc, CHIRBuilder& builder)
{
    auto mangledName = "extend_" + curDef.GetIdentifier() + "_p_" + parentType.ToString();
    auto genericParams = curDef.GetGenericTypeParams();
    auto extendDef = builder.CreateExtend(
        INVALID_LOCATION, mangledName, package.GetName(), false, genericParams);
    extendDef->SetExtendedType(*curDef.GetType());
    extendDef->AddImplementedInterfaceTy(parentType);
    extendDef->EnableAttr(Attribute::COMPILER_ADD);
    if (curDef.TestAttr(Attribute::GENERIC)) {
        extendDef->EnableAttr(Attribute::GENERIC);
    }

    VTableType vtable;
    vtable.emplace(&parentType, virtualFunc);
    extendDef->SetVTable(vtable);
}

void CreateExtendDefForImportedCustomTypeDef(Package& package, CHIRBuilder& builder, bool incr)
{
    if (incr) {
        return;
    }
    /*  codegen will create extension def according to CHIR's vtable, in order not to create duplicate
        extension def, codegen won't visit vtable from imported CustomTypeDef, these vtables are assumed that
        must be created in imported package. but there is a special case:
        ================ package A ================
        public interface I {}
        open public class A {}

        ================ package B ================
        import package A
        public class B <: A {} // extension def B_ed_A will be created in codegen

        ================ package C ================
        import package A
        extend A <: I {} // extension def A_ed_I will be created in codegen

        ================ package D ================
        import package A, B, C
        // extension def B_ed_I is needed, but there isn't in imported packages

        so we need to create extension def B_ed_I in current package, in order to deal with this case,
        a compiler added extend def is needed:
        [COMPILER_ADD] extend B <: I {}
        this def is create in current package, so extension def B_ed_I will be created in codegen
    */
    for (auto def : package.GetAllImportedCustomTypeDef()) {
        if (def->IsExtend()) {
            continue;
        }
        for (const auto& it : def->GetVTable()) {
            if (ParentDefIsFromExtend(*def, *(it.first->GetClassDef()))) {
                CreateNewExtendDef(package, *def, *it.first, it.second, builder);
                continue;
            }
        }
    }
}

void ReplaceCustomTypeDefVtable(CustomTypeDef& def, const std::unordered_map<Value*, Value*>& symbol)
{
    auto vtable = def.GetVTable();
    for (auto& vtableIt : vtable) {
        for (size_t i = 0; i < vtableIt.second.size(); ++i) {
            auto res = symbol.find(vtableIt.second[i].instance);
            if (res != symbol.end()) {
                vtableIt.second[i].instance = VirtualCast<FuncBase*>(res->second);
            }
        }
    }
    def.SetVTable(vtable);
}

void ReplaceCustomTypeDefAndExtendVtable(CustomTypeDef& def, const std::unordered_map<Value*, Value*>& symbol)
{
    ReplaceCustomTypeDefVtable(def, symbol);
    for (auto exDef : def.GetExtends()) {
        ReplaceCustomTypeDefVtable(*exDef, symbol);
    }
}

void ReplaceParentAndSubClassVtable(CustomTypeDef& def, const std::unordered_map<Value*, Value*>& symbol,
    const std::unordered_map<ClassDef*, std::unordered_set<CustomTypeDef*>>& subClasses)
{
    // replace self vtable
    ReplaceCustomTypeDefAndExtendVtable(def, symbol);

    if (!def.IsClassLike()) {
        return;
    }
    auto& classDef = StaticCast<ClassDef&>(def);
    auto it = subClasses.find(&classDef);
    if (it == subClasses.end()) {
        return;
    }
    // replace sub class vtable
    for (auto subClass : it->second) {
        ReplaceCustomTypeDefAndExtendVtable(*subClass, symbol);
    }
}

void ReplaceMethodAndStaticVar(
    const std::unordered_map<CustomTypeDef*, std::unordered_map<Value*, Value*>>& replaceTable,
    const std::unordered_map<ClassDef*, std::unordered_set<CustomTypeDef*>>& subClasses)
{
    for (auto& it : replaceTable) {
        auto methods = it.first->GetMethods();
        for (size_t i = 0; i < methods.size(); ++i) {
            auto res = it.second.find(methods[i]);
            if (res != it.second.end()) {
                methods[i] = VirtualCast<FuncBase*>(res->second);
            }
        }
        it.first->SetMethods(methods);
        ReplaceParentAndSubClassVtable(*it.first, it.second, subClasses);

        auto staticVars = it.first->GetStaticMemberVars();
        for (size_t i = 0; i < staticVars.size(); ++i) {
            auto res = it.second.find(staticVars[i]);
            if (res != it.second.end()) {
                staticVars[i] = VirtualCast<GlobalVarBase*>(res->second);
            }
        }
        it.first->SetStaticMemberVars(staticVars);

        for (auto& it2 : it.second) {
            if (auto func = DynamicCast<Func*>(it2.first)) {
                func->DestroySelf();
            } else {
                VirtualCast<GlobalVarBase*>(it2.first)->DestroySelf();
            }
        }
    }
}
}

bool IsEmptyInitFunc(Func& func)
{
    if (func.GetFuncKind() != CHIR::FuncKind::GLOBALVAR_INIT) {
        return false;
    }
    bool isEmpty = true;
    auto preVisit = [&isEmpty](Expression& e) {
        if (e.GetExprKind() != CHIR::ExprKind::EXIT && e.GetExprKind() != CHIR::ExprKind::RAISE_EXCEPTION) {
            isEmpty = false;
        }
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(func, preVisit);
    return isEmpty;
}

static std::unordered_map<ClassDef*, std::unordered_set<CustomTypeDef*>> CollectSubClasses(
    const Package& pkg, CHIRBuilder& builder)
{
    //                 parent     sub
    std::unordered_map<ClassDef*, std::unordered_set<CustomTypeDef*>> subClasses;
    for (auto def : pkg.GetAllCustomTypeDef()) {
        for (auto parentType : def->GetSuperTypesRecusively(builder)) {
            subClasses[parentType->GetClassDef()].emplace(def);
        }
    }
    return subClasses;
}
namespace {
void CreateSrcImportedFuncSymbol(
    CHIRBuilder& builder, Func& fn, std::unordered_map<Func*, ImportedFunc*>& srcCodeImportedFuncMap)
{
    auto genericParamTy = fn.GetGenericTypeParams();
    auto pkgName = fn.GetPackageName();
    auto funcTy = fn.GetType();
    auto mangledName = fn.GetIdentifierWithoutPrefix();
    auto srcCodeName = fn.GetSrcCodeIdentifier();
    auto rawMangledName = fn.GetRawMangledName();
    auto importedFunc = builder.CreateImportedVarOrFunc<ImportedFunc>(
        funcTy, mangledName, srcCodeName, rawMangledName, pkgName, genericParamTy);
    importedFunc->AppendAttributeInfo(fn.GetAttributeInfo());
    importedFunc->SetFuncKind(fn.GetFuncKind());
    if (auto hostFunc = fn.GetParamDftValHostFunc()) {
        auto it = srcCodeImportedFuncMap.find(StaticCast<Func*>(hostFunc));
        CJC_ASSERT(it != srcCodeImportedFuncMap.end());
        importedFunc->SetParamDftValHostFunc(*it->second);
    }
    importedFunc->SetFastNative(fn.IsFastNative());
    importedFunc->Set<LinkTypeInfo>(Linkage::EXTERNAL);
    srcCodeImportedFuncMap.emplace(&fn, importedFunc);
}

void CreateSrcImportedVarSymbol(
    CHIRBuilder& builder, Value& gv, std::unordered_map<GlobalVar*, ImportedVar*>& srcCodeImportedVarMap)
{
    auto globalVar = VirtualCast<GlobalVar*>(&gv);
    auto mangledName = globalVar->GetIdentifierWithoutPrefix();
    auto srcCodeName = globalVar->GetSrcCodeIdentifier();
    auto rawMangledName = globalVar->GetRawMangledName();
    auto packageName = globalVar->GetPackageName();
    auto ty = globalVar->GetType();
    auto importedVar =
        builder.CreateImportedVarOrFunc<ImportedVar>(ty, mangledName, srcCodeName, rawMangledName, packageName);
    importedVar->AppendAttributeInfo(globalVar->GetAttributeInfo());
    importedVar->Set<LinkTypeInfo>(gv.Get<LinkTypeInfo>());
    srcCodeImportedVarMap.emplace(globalVar, importedVar);
}

void CreateSrcImpotedValueSymbol(const std::unordered_set<Func*>& srcCodeImportedFuncs,
    const std::unordered_set<GlobalVar*>& srcCodeImportedVars, CHIRBuilder& builder,
    std::unordered_map<Func*, ImportedFunc*>& srcCodeImportedFuncMap,
    std::unordered_map<GlobalVar*, ImportedVar*>& srcCodeImportedVarMap)
{
    for (auto func : builder.GetCurPackage()->GetGlobalFuncs()) {
        CJC_NULLPTR_CHECK(func);
        if (srcCodeImportedFuncs.find(func) != srcCodeImportedFuncs.end()) {
            CreateSrcImportedFuncSymbol(builder, *func, srcCodeImportedFuncMap);
        }
    }
    for (auto gv : builder.GetCurPackage()->GetGlobalVars()) {
        CJC_NULLPTR_CHECK(gv);
        if (srcCodeImportedVars.find(gv) != srcCodeImportedVars.end()) {
            CreateSrcImportedVarSymbol(builder, *gv, srcCodeImportedVarMap);
        }
    }
}
}

void ToCHIR::ReplaceSrcCodeImportedValueWithSymbol()
{
    std::unordered_set<Func*> toBeRemovedFuncs;
    std::unordered_set<GlobalVar*> toBeRemovedVars;
    std::unordered_map<Func*, ImportedFunc*> srcCodeImportedFuncMap;
    std::unordered_map<GlobalVar*, ImportedVar*> srcCodeImportedVarMap;
    CreateSrcImpotedValueSymbol(
        srcCodeImportedFuncs, srcCodeImportedVars, builder, srcCodeImportedFuncMap, srcCodeImportedVarMap);
    for (auto lambda : uselessLambda) {
        for (auto user : lambda->GetUsers()) {
            user->RemoveSelfFromBlock();
        }
        lambda->DestroySelf();
        toBeRemovedFuncs.emplace(lambda);
    }

    for (auto def : uselessClasses) {
        for (auto func : def->GetMethods()) {
            for (auto user : func->GetUsers()) {
                user->RemoveSelfFromBlock();
            }
            auto funcWithBody = StaticCast<Func*>(func);
            funcWithBody->DestroySelf();
            toBeRemovedFuncs.emplace(funcWithBody);
        }
    }
    std::vector<ClassDef*> newClasses;
    auto classes = chirPkg->GetClasses();
    for (auto def : classes) {
        if (uselessClasses.find(def) == uselessClasses.end()) {
            newClasses.emplace_back(def);
        }
    }
    chirPkg->SetClasses(std::move(newClasses));

    std::unordered_map<CustomTypeDef*, std::unordered_map<Value*, Value*>> replaceTable;
    for (auto& it : srcCodeImportedFuncMap) {
        auto funcWithBody = it.first;
        auto importedSymbol = it.second;
        // Attributes may be added in the chir phase. For example, 'final' is added when a virtual table is created. In
        // this case, you need to append the attributes again.
        importedSymbol->AppendAttributeInfo(funcWithBody->GetAttributeInfo());
        for (auto user : funcWithBody->GetUsers()) {
            user->ReplaceOperand(funcWithBody, importedSymbol);
        }
        if (auto parentDef = funcWithBody->GetParentCustomTypeDef()) {
            replaceTable[parentDef][funcWithBody] = importedSymbol;
        }
        toBeRemovedFuncs.emplace(funcWithBody);
        auto implicitIt = implicitFuncs.find(funcWithBody->GetIdentifierWithoutPrefix());
        if (implicitIt != implicitFuncs.end()) {
            implicitIt->second = importedSymbol;
        }
    }
    for (auto& it : srcCodeImportedVarMap) {
        auto varWithInit = it.first;
        auto importedSymbol = it.second;
        // Attributes may be added in the chir phase. For example, 'final' is added when a virtual table is created. In
        // this case, you need to append the attributes again.
        importedSymbol->AppendAttributeInfo(varWithInit->GetAttributeInfo());
        if (auto initFunc = varWithInit->GetInitFunc()) {
            for (auto user : initFunc->GetUsers()) {
                user->RemoveSelfFromBlock();
            }
            initFunc->DestroySelf();
            toBeRemovedFuncs.emplace(initFunc);
        }
        for (auto user : varWithInit->GetUsers()) {
            user->ReplaceOperand(varWithInit, importedSymbol);
        }
        if (auto parentDef = varWithInit->GetParentCustomTypeDef()) {
            replaceTable[parentDef][varWithInit] = importedSymbol;
        }
        toBeRemovedVars.emplace(varWithInit);
    }
    auto subClasses = CollectSubClasses(*chirPkg, builder);
    ReplaceMethodAndStaticVar(replaceTable, subClasses);

    std::vector<Func*> globalFuncs;
    for (auto func : chirPkg->GetGlobalFuncs()) {
        if (toBeRemovedFuncs.find(func) != toBeRemovedFuncs.end()) {
            continue;
        } else if (IsEmptyInitFunc(*func)) {
            for (auto user : func->GetUsers()) {
                user->RemoveSelfFromBlock();
            }
            func->DestroySelf();
            continue;
        }
        globalFuncs.emplace_back(func);
    }
    chirPkg->SetGlobalFuncs(globalFuncs);

    std::vector<GlobalVar*> globalVars;
    for (auto var : chirPkg->GetGlobalVars()) {
        if (toBeRemovedVars.find(var) == toBeRemovedVars.end()) {
            globalVars.emplace_back(var);
        }
    }
    chirPkg->SetGlobalVars(std::move(globalVars));
}

void ToCHIR::RemoveUnusedImports(bool removeSrcCodeImported)
{
    Utils::ProfileRecorder r{"CHIR", "RemoveUnusedImports"};

    if (removeSrcCodeImported) {
        ReplaceSrcCodeImportedValueWithSymbol();
    }
    UnusedImportRemover unusedImportRemover{
        kind == IncreKind::INCR, opts, implicitFuncs};
    unusedImportRemover.Remove(*GetPackage());
    CreateExtendDefForImportedCustomTypeDef(*GetPackage(), builder, kind == IncreKind::INCR);
    DumpCHIRDebug("RemoveUnusedImports");
}
}  // namespace Cangjie::CHIR
