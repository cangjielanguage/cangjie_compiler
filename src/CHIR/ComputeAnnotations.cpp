// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// This files declares the entry of ComputeAnnotationsBeforeCheck stage.

#include <variant>
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/CHIR/Checker/ComputeAnnotations.h"

namespace Cangjie::CHIR {
using namespace AST;
namespace {
static_assert(sizeof(Number) == 8);
std::unordered_map<const CustomTypeDef*, const InheritableDecl*> g_typeMap{};
std::unordered_map<const Value*, AnnoInstanceValue> g_valueMap{};
/// map annoFactoryFunc to original Decl, used to write back annotation info
std::unordered_map<const Func*, const AST::Decl*> g_annoFactoryMap{};

AnnoInstanceValue GetValue(const Value& v);
AnnoInstanceValue CreateValue(const LiteralValue& v)
{
    if (auto lit = DynamicCast<CHIR::IntLiteral>(&v)) {
        return {static_cast<Number>(lit->GetUnsignedVal())};
    }
    if (auto lit = DynamicCast<BoolLiteral>(&v)) {
        return {static_cast<Number>(lit->GetVal())};
    }
    if (auto lit = DynamicCast<StringLiteral>(&v)) {
        return {lit->GetVal()};
    }
    if (auto lit = DynamicCast<RuneLiteral>(&v)) {
        return {static_cast<Number>(lit->GetVal())};
    }
    if (auto lit = DynamicCast<FloatLiteral>(&v)) {
        return {lit->GetVal()};
    }
    // null or unit
    return {std::monostate{}};
}

std::vector<std::pair<std::string, AnnoInstanceValue>> GetTupleArgs(const Tuple& tuple)
{
    std::vector<std::pair<std::string, AnnoInstanceValue>> args{};
    for (auto v : tuple.GetOperands()) {
        args.emplace_back("", GetValue(*v));
    }
    return args;
}

AnnoInstanceValue CreateEnumValue(const LocalVar& obj)
{
    if (auto tuple = DynamicCast<Tuple>(obj.GetExpr())) {
        // enum with args
        return {std::make_shared<AnnoInstanceClassInst>(AnnoInstanceClassInst{
            g_typeMap.at(StaticCast<EnumType>(obj.GetType())->GetEnumDef()), GetTupleArgs(*tuple)})};
    }
    // enum by integer
    auto tc = StaticCast<TypeCast>(obj.GetExpr());
    return GetValue(*tc->GetSourceValue());
}

/// create value of CustomTypeDef type, that is, struct/enum/class
AnnoInstanceValue CreateValueOfCustomTypeDef(const Value& v)
{
    auto obj = StaticCast<LocalVar>(&v);
    CustomTypeDef* def;
    auto alloc = DynamicCast<Allocate>(obj->GetExpr());
    if (alloc) {
        // the value is an alloc, so it is a class type object
        def = StaticCast<ClassType>(alloc->GetType()->StripAllRefs())->GetClassDef();
    } else {
        // the value is a load of an alloc, it is a struct type object
        auto loadOfAlloc = StaticCast<Load>(obj->GetExpr());
        alloc = StaticCast<Allocate>(StaticCast<LocalVar>(loadOfAlloc->GetLocation())->GetExpr());
        def = StaticCast<StructType>(obj->GetType())->GetStructDef();
    }
    auto fields = def->GetAllInstanceVars();
    std::vector<std::pair<std::string, AnnoInstanceValue>> ctorArgs{};
    // store StorElementRef values to the result
    for (auto user : alloc->GetResult()->GetUsers()) {
        if (auto store = DynamicCast<StoreElementRef>(user); store && store->GetLocation() == alloc->GetResult()) {
            if (auto ty = store->GetValue()->GetType(); ty->IsUnit() || ty->IsNothing()) {
                continue;
            }
#ifndef NDEBUG
            auto nothingExprCheck = StaticCast<LocalVar>(store->GetValue())->GetExpr();
            if (auto constant = DynamicCast<Constant>(nothingExprCheck)) {
                if (constant->GetValue()->IsNullLiteral()) {
                    std::cerr << nothingExprCheck->GetResult()->ToString() << '\n';
                    CJC_ASSERT(false && "Invalid node from interpreter.");
                }
            }
#endif
            auto value = GetValue(*store->GetValue());
            CJC_ASSERT(value);
            ctorArgs.emplace_back(fields[store->GetPath()[0]].name, std::move(value));
        }
    }
    AnnoInstanceValue res{
        std::make_shared<AnnoInstanceClassInst>(AnnoInstanceClassInst{g_typeMap.at(def), std::move(ctorArgs)})};
    g_valueMap[&v] = res;
    return res;
}

AnnoInstanceValue GetValue(const GlobalVar& gv)
{
    for (auto user : gv.GetUsers()) {
        if (auto store = DynamicCast<Store>(user); store && store->GetLocation() == &gv) {
            return GetValue(*store->GetValue());
        }
    }
    CJC_ABORT();
#ifdef NDEBUG
    return {std::monostate{}};
#endif
}

AnnoInstanceValue CreateValue(const Value& v)
{
    if (auto lit = DynamicCast<LiteralValue>(&v)) {
        return CreateValue(*lit);
    }
    if (auto obj = DynamicCast<LocalVar>(&v)) {
        if (auto c = DynamicCast<Constant>(obj->GetExpr())) {
            return GetValue(*c->GetValue());
        }
        if (obj->GetType()->IsEnum()) {
            return CreateEnumValue(*obj);
        }
        if (obj->GetType()->StripAllRefs()->IsBox()) {
            return GetValue(*StaticCast<Box>(obj->GetExpr())->GetSourceValue());
        }
        if (auto t = DynamicCast<Tuple>(obj->GetExpr())) {
            return {std::make_shared<AnnoInstanceClassInst>(AnnoInstanceClassInst{nullptr, GetTupleArgs(*t)})};
        }
        if (auto c = DynamicCast<TypeCast>(obj->GetExpr())) {
            if (c->GetSourceTy()->IsClassRef() && c->GetTargetTy()->IsClassRef()) {
                // cast between subtypes, we cannot test subtype relation without CHIRBuilder
                // so we skip this assertion
                return GetValue(*c->GetSourceValue());
            }
        }
        if (auto l = DynamicCast<Load>(obj->GetExpr())) {
            if (auto gv = DynamicCast<GlobalVar>(l->GetLocation())) {
                // load of gv, valid iff the location is a const global variable lifted from annotationsArray.
                // Threotically we check it's of Annotation class type, but we do not have such utilities.
                CJC_ASSERT(gv->GetType()->StripAllRefs()->IsClass());
                auto ret = GetValue(*gv);
                CJC_ASSERT(ret.Object());
                return ret;
            }
        }
        // not a constant, v must be of const class/struct/enum type
        return CreateValueOfCustomTypeDef(v);
    }
    CJC_ABORT();
#ifdef NDEBUG
    return {std::monostate{}};
#endif
}
AnnoInstanceValue GetValue(const Value& v)
{
    if (auto it = g_valueMap.find(&v); it != g_valueMap.end()) {
        return it->second;
    }
    return CreateValue(v);
}

std::vector<AnnoInstance> CreateAnnoInstFromConstEval(const Func& func)
{
    auto arrRef = func.GetReturnValue();
    auto users = arrRef->GetUsers();
    CJC_ASSERT(!users.empty());
    auto args = StaticCast<Apply>(arrRef->GetUsers()[0])->GetArgs();
    CJC_ASSERT(args[0] == arrRef);
    auto rawArray = StaticCast<LocalVar>(args[1]);
    size_t last{0};
    std::vector<AnnoInstance> ret{};
    for (auto use : rawArray->GetUsers()) {
        if (auto store = DynamicCast<StoreElementRef>(use); store && store->GetLocation() == rawArray) {
            CJC_ASSERT(store->GetLocation() == rawArray);
            
            auto& path = store->GetPath();
            CJC_ASSERT(path.size() == 1);
            CJC_ASSERT(path[0] >= last);
            auto obj = StaticCast<LocalVar>(store->GetValue());
            if (obj->TestAttr(CHIR::Attribute::NO_REFLECT_INFO)) {
                continue;
            }
            auto typecast = StaticCast<TypeCast>(obj->GetExpr())->GetSourceValue();
            auto load = StaticCast<Load>(StaticCast<LocalVar>(typecast)->GetExpr());
            ret.push_back(GetValue(*load->GetResult()).Object());
            last = path[0];
        }
    }
    return ret;
}

AnnoMap CreateAnnoInstFromConstEvalRes(ConstEvalResult& ev)
{
    std::unordered_map<const Decl*, std::vector<AnnoInstance>> annoInstMap{};
    for (auto func : ev.pkg->GetGlobalFuncs()) {
        if (func->GetFuncKind() == FuncKind::ANNOFACTORY_FUNC) {
            auto v = CreateAnnoInstFromConstEval(*func);
            if (!v.empty()) {
                annoInstMap[g_annoFactoryMap[func]] = std::move(v);
            }
        }
    }
    return annoInstMap;
}
} // namespace

void ConstEvalResult::Dump() const
{
    std::vector<std::pair<const Decl*, const std::vector<AnnoInstance>*>> ordered;
    for (auto& v : map) {
        ordered.emplace_back(v.first, &v.second);
    }
    std::sort(ordered.begin(), ordered.end(), [](auto& l, auto& r) {
        return l.first->begin < r.first->begin;
    });
    for (auto& v : ordered) {
        std::cout << v.first->mangledName << ":\n";
        for (auto& k : *v.second) {
            std::cout << "    " << k->ToString() << '\n';
        }
    }
}

Number AnnoInstanceValue::Value() const
{
    return std::get<Number>(*this);
}
long long AnnoInstanceValue::SignedValue() const
{
    return static_cast<long long>(std::get<Number>(*this));
}
unsigned AnnoInstanceValue::Rune() const
{
    return static_cast<unsigned>(std::get<Number>(*this));
}
bool AnnoInstanceValue::Bool() const
{
    return static_cast<bool>(std::get<Number>(*this));
}
const std::string& AnnoInstanceValue::String() const
{
    return std::get<std::string>(*this);
}
std::shared_ptr<AnnoInstanceClassInst> AnnoInstanceValue::Object() const
{
    return std::get<std::shared_ptr<AnnoInstanceClassInst>>(*this);
}
AnnoInstanceValue::operator bool() const noexcept
{
    return !std::holds_alternative<std::monostate>(*this);
}

std::string AnnoInstanceValue::ToString() const noexcept
{
    if (std::holds_alternative<Number>(*this)) {
        return std::to_string(std::get<Number>(*this));
    }
    if (std::holds_alternative<std::string>(*this)) {
        return std::string{'"'} + std::get<std::string>(*this) + '"';
    }
    if (std::holds_alternative<double>(*this)) {
        return std::to_string(std::get<double>(*this));
    }
    if (std::holds_alternative<std::shared_ptr<AnnoInstanceClassInst>>(*this)) {
        return std::get<std::shared_ptr<AnnoInstanceClassInst>>(*this)->ToString();
    }
    return "UNINITIALIZED";
}
std::string AnnoInstanceClassInst::ToString() const
{
    std::stringstream ss;
    
    if (cl) {
        ss << cl->identifier.Val() << "{";
    } else {
        ss << "(";
    }
    for (size_t i{0}; i < a.size(); ++i) {
        auto& field{a[i]};
        if (!field.first.empty()) {
            ss << '"' << field.first << "\": ";
        }
        ss << field.second.ToString();
        if (i != a.size() - 1) {
            ss << ",";
        }
    }
    if (cl) {
        ss << '}';
    } else {
        ss << ')';
    }
    return ss.str();
}

namespace {
/// The spec states that enum ctor call with all args const expression is a const expression, but does not state which
/// ctors are const expressions.
/// We suppose if a ctor is possibly called in a enum ctor call is a const ctor.
struct EnumCtorConstChecker {
    bool Check(const Decl& cons)
    {
        visited.insert(StaticCast<EnumDecl>(cons.outerDecl));
        return IsConstEnumCtor(cons);
    }

private:
    // enum ctor param type may recursively depend on other types. In such case, this ctor is possibly const.
    std::set<EnumDecl*> visited;
    
    /// A type decl is const, if and only if
    /// 1. it has a const init, or
    /// 2. it has const member and can be constructed in a const
    /// expression without explcit call of init (in this case, it must be a builtin type).
    /// 3. enum decl, if any constructor is 1) either parameterless, 2) or all parameters are of const type, then it is
    /// a const decl.
    bool IsConst(InheritableDecl& type)
    {
        if (type.ty->IsString()) {
            return true;
        }
        if (auto em = DynamicCast<EnumDecl>(&type)) {
            if (auto [_, suc] = visited.insert(em); suc) {
                return true;
            }
            for (auto& cons : em->constructors) {
                if (IsConstEnumCtor(*cons)) {
                    return true;
                }
            }
            return false;
        }
        for (auto decl : type.GetMemberDeclPtrs()) {
            if (decl->IsConst() && decl->TestAttr(AST::Attribute::CONSTRUCTOR)) {
                return true;
            }
        }
        return false;
    }

    bool IsConstEnumCtor(const Decl& cons)
    {
        if (Is<VarDecl>(cons)) {
            return true;
        }
        auto& fun = StaticCast<FuncDecl>(cons);
        for (auto& p : fun.funcBody->paramLists[0]->params) {
            if (!IsConst(p->ty)) {
                return false;
            }
        }
        return true;
    }

    bool IsConst(Ptr<Ty> ty)
    {
        if (Is<GenericsTy>(ty)) {
            return true;
        }
        auto decl = Ty::GetDeclPtrOfTy(ty); // get the generic version decl
        if (!decl) {
            if (ty->kind == TypeKind::TYPE_VARRAY) {
                return false;
            }
            // all other builtin types are const type
            return ty->IsBuiltin();
        }
        return IsConst(*StaticCast<InheritableDecl>(decl));
    }
};

bool IsConstEnumCtor(const Decl& cons)
{
    if (Is<VarDecl>(cons)) {
        return true;
    }
    EnumCtorConstChecker checker{};
    return checker.Check(cons);
}


/// Used to restore the original AST after consteval of const sub-AST.
/// a DeclHolder is either a placeholder, meaning the original decl was selected into sub-AST. This is replaced with
/// the original decl when restoring;
/// or an OwnedPtr<Decl>, meaning the original decl was not selected.
struct DeclHolder : public std::variant<std::monostate, OwnedPtr<Decl>> {
    bool IsPlaceholder() const noexcept
    {
        return std::holds_alternative<std::monostate>(*this);
    }
    OwnedPtr<Decl> GetDecl() &&
    {
        return std::move(std::get<OwnedPtr<Decl>>(*this));
    }

#ifndef NDEBUG
    std::string ToString() const __attribute__((used))
    {
        if (IsPlaceholder()) {
            return "{}";
        }
        return std::get<OwnedPtr<Decl>>(*this)->identifier.Val();
    }
#endif
};

/// Hold the member decls of a decl, to be restored later
struct MemberDeclHolder {
    std::vector<DeclHolder> m;

    /// Test this cache is full, that is, all members are not empty.
    /// In this case, this cache may not be needed, because all members of the original decls are to be discarded.
    bool IsFull() const
    {
        for (auto& holder : m) {
            if (holder.IsPlaceholder()) {
                return false;
            }
        }
        return true;
    }

#ifndef NDEBUG
    std::string ToString() const __attribute__((used))
    {
        std::stringstream out;
        for (auto& holder : m) {
            out << holder.ToString() << ',';
        }
        return out.str();
    }
#endif
};

/// Get const sub-pkg of a package. Only decls that are possibly valid in const expressions are kept.
/// After that, use \ref RestoreOriginalPkg to restore to the previous state of the package.
/// Both the member functions can be called only once.
struct ConstSubPkgGetter {
    explicit ConstSubPkgGetter(AST::Package& p) : pkg{p} {}
    
    void GetSubPkg()
    {
        for (auto& file : pkg.files) {
            TakeSubPkg(*file);
        }
        KeepConstMembersIfAny(pkg.genericInstantiatedDecls, m[&pkg]);
    }

    void RestoreOriginalPkg()
    {
        for (auto& file : pkg.files) {
            RestoreOriginalFile(*file);
        }
        if (auto c = m.find(&pkg); c != m.end()) {
            RestoreOriginalMembers(pkg.genericInstantiatedDecls, c->second);
        }
    }
    std::vector<const AST::Decl*> annoOnlyDecls{};

private:
    AST::Package& pkg;

    // main decl cache
    // For Package, genericInstantiatedDecls. files of pkg are always kept.
    std::unordered_map<Node*, MemberDeclHolder> m;
    // additional cache. Some Node has more than one member decl containers.
    // For EnumDecl, constructors
    // For File, exportedInternalDecls
    std::unordered_map<Node*, MemberDeclHolder> cache2;
    std::unordered_map<InheritableDecl*, bool> isConstCache;

    void RestoreOriginalFile(File& file)
    {
        if (auto c = m.find(&file); c != m.end()) {
            RestoreOriginalMembers(file.decls, c->second);
        }
        if (auto c = cache2.find(&file); c != cache2.end()) {
            RestoreOriginalMembers(file.exportedInternalDecls, c->second);
        }
    }

    void RestoreOriginalMembers(std::vector<OwnedPtr<Decl>>& decls, MemberDeclHolder& cache)
    {
        if (cache.m.empty()) {
            // no cache, nothing to restore
            return;
        }
        std::vector<OwnedPtr<Decl>> res{};
        size_t i{0};
        for (auto& holder : cache.m) {
            if (holder.IsPlaceholder()) {
                res.push_back(std::move(decls[i++]));
            } else {
                res.push_back(std::move(holder).GetDecl());
            }
            RestoreOriginalDecl(*res.back());
        }
        CJC_ASSERT(i == decls.size());
        std::swap(decls, res);
    }

    void RestoreOriginalDecl(Decl& decl)
    {
        // VarWithPatternDecl need not restore, because it is selected as one.
        // all other decls do not have member decls.
        if (!Is<InheritableDecl>(decl)) {
            return;
        }
        if (auto c = m.find(&decl); c != m.end()) {
            RestoreOriginalMembers(decl.GetMemberDecls(), c->second);
        }
        if (auto e = DynamicCast<EnumDecl>(&decl)) {
            if (auto c = cache2.find(&decl); c != cache2.end()) {
                RestoreOriginalMembers(e->constructors, c->second);
            }
        }
    }

    /// Keep only const members of input node, cache the original member decls and order in cache.
    /// \return true if this node is to be kept by its parent decl.
    bool TakeSubPkg(File& file)
    {
        KeepConstMembersIfAny(file.decls, m[&file]);
        KeepConstMembersIfAny(file.exportedInternalDecls, cache2[&file]);
        // keep all files anyway, an empty file is not a performance hit
        return true;
    }

    void KeepConstMembersIfAny(std::vector<OwnedPtr<Decl>>& members, MemberDeclHolder& loc)
    {
        loc = KeepConstMembers(members);
    }

    bool TakeSubPkg(Decl& decl)
    {
        if (decl.HasAnno(AnnotationKind::CONSTSAFE)) {
            return true;
        }
        if (auto d = DynamicCast<InheritableDecl>(&decl)) {
            return TakeSubPkg(*d);
        }
        if (auto t = DynamicCast<TypeAliasDecl>(&decl)) {
            /// type alias are replaced after SEMA
            return false;
        }
        if (decl.IsConst()) {
            return true;
        }
        if (Is<PropDecl>(decl)) {
            return false;
        }
        if (auto var = DynamicCast<VarDecl>(&decl); var && !var->isVar && !var->TestAttr(AST::Attribute::STATIC) &&
            IsConstType(var->ty)) {
            if (var->outerDecl) {
                return true;
            }
            if (auto vp = DynamicCast<VarWithPatternDecl>(decl.outerDecl)) {
                return vp->IsConst();
            }
        }
        if (auto func = DynamicCast<FuncDecl>(&decl); func && func->TestAttr(AST::Attribute::FOREIGN) &&
            func->fullPackageName == "std.core") {
            if (func->identifier.Val().find("CJ_CORE_") == 0) {
                return true;
            }
            if (func->identifier == "memcpy_s" || func->identifier == "strlen") {
                return true;
            }
        }
        return false;
    }

    static bool HasCompileTimeCustomAnno(const Decl& decl)
    {
        if (!decl.annotationsArray) {
            return false;
        }
        for (auto& anno : decl.annotationsArray->children) {
            if (!anno->TestAttr(AST::Attribute::NO_REFLECT_INFO)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] MemberDeclHolder KeepConstMembers(std::vector<OwnedPtr<Decl>>& members)
    {
        std::vector<OwnedPtr<Decl>> selectedDecls{};
        MemberDeclHolder cache;
        for (auto& member : members) {
            if (TakeSubPkg(*member)) {
                selectedDecls.push_back(std::move(member));
                cache.m.push_back({std::monostate{}});
            } else {
                if (HasCompileTimeCustomAnno(*member)) {
                    annoOnlyDecls.push_back(&*member);
                }
                cache.m.push_back({std::move(member)});
            }
        }
        members = std::move(selectedDecls);
        return cache;
    }

    bool IsConstType(Ptr<Ty> ty)
    {
        if (Is<GenericsTy>(ty)) {
            return true;
        }
        auto decl = Ty::GetDeclPtrOfTy(ty); // get the generic version decl
        if (!decl) {
            if (ty->kind == TypeKind::TYPE_VARRAY) {
                return false;
            }
            // all other builtin types are const type
            return ty->IsBuiltin();
        }
        return TakeSubPkg(*StaticCast<InheritableDecl>(decl));
    }

    /// \returns True if constness of \ref type depends fully on its members.
    /// Note that interface/extend are always kept even if they do not have any const members, because a const class
    /// object may cast to it, and such cast is still const expression, although the result value cannot be used to
    /// call any function.
    
    bool MustSave(InheritableDecl& type)
    {
        if (auto extend = DynamicCast<ExtendDecl>(&type)) {
            if (auto decl = DynamicCast<InheritableDecl>(Ty::GetDeclOfTy(extend->ty))) {
                return TakeSubPkg(*decl);
            }
            auto ty = extend->ty;
            return ty->kind != TypeKind::TYPE_VARRAY;
        }
        return (Is<ClassDecl>(type) && type.TestAnyAttr(AST::Attribute::OPEN, AST::Attribute::ABSTRACT)) ||
            Is<InterfaceDecl>(type);
    }

    bool TakeSubPkg(InheritableDecl& type)
    {
        if (auto it = isConstCache.find(&type); it != isConstCache.cend()) {
            return it->second;
        }
        bool ret{false};
        if (type.ty->IsString()) {
            ret = true;
        }
        // save extend because it may contain a member decl with @!Annotations.
        if (MustSave(type)) {
            ret = true;
        }
        /// Check if \ref type is const type
        if (auto em = DynamicCast<EnumDecl>(&type)) {
            MemberDeclHolder ctors{};
            std::vector<OwnedPtr<Decl>> selectedCtors;
            for (auto& cons : em->constructors) {
                if (IsConstEnumCtor(*cons)) {
                    ctors.m.push_back({std::monostate{}});
                    selectedCtors.push_back({std::move(cons)});
                    ret = true;
                } else {
                    ctors.m.push_back({std::move(cons)});
                }
            }
            if (ret) {
                cache2[em] = std::move(ctors);
                std::swap(em->constructors, selectedCtors);
            }
        }
        for (auto decl : type.GetMemberDeclPtrs()) {
            if (decl->IsConst() && decl->TestAttr(AST::Attribute::CONSTRUCTOR)) {
                ret = true;
            }
        }
        KeepConstMembersIfAny(type.GetMemberDecls(), m[&type]);
        if (!ret && !type.GetMemberDecls().empty()) {
            // although the type is itself non-const, it still has const members
            // static const members in this case can still be present in const eval.
            // we do not differ between instance and static const members here.
            ret = true;
            annoOnlyDecls.push_back(&type);
        }
        isConstCache[&type] = ret;
        return ret;
    }
};
}

OwnedPtr<ConstEvalResult> ComputeAnnotations(AST::Package& pkg, CompilerInstance& ci)
{
    // only do compute annotations when its debug option is on
    // set the default value to true when this feature is enabled
    auto& opts = ci.invocation.globalOptions;
    bool doCompute{opts.computeAnnotationsDebug};
    // no need to recheck when no change
    if (opts.enIncrementalCompilation && ci.kind == IncreKind::NO_CHANGE) {
        doCompute = false;
    }
    // CJMP does not fully support Annotation
    if (ci.invocation.globalOptions.commonPartCjo != std::nullopt ||
        ci.invocation.globalOptions.outputMode == GlobalOptions::OutputMode::CHIR) {
        doCompute = false;
    }
    if (!doCompute) {
        return {};
    }
    auto res = MakeOwned<ConstEvalResult>(ci.GetFileNameMap(), ci.invocation.globalOptions.GetJobs());
    CHIR::AnalysisWrapper<CHIR::ConstAnalysis<CHIR::ConstStatePool>, CHIR::ConstDomain>
    constAnalysisWrapper{res->builder};
    
    CHIR::ToCHIR convertor(ci, pkg, constAnalysisWrapper, res->builder);
    bool computeSuccess = convertor.ComputeAnnotations({});
    if (!computeSuccess) {
        return {};
    }
    res->pkg = convertor.GetPackage();
    auto& annoFactorys = convertor.GetAnnoFactoryFuncs();
    for (auto& v : annoFactorys) {
        g_annoFactoryMap[v.second] = v.first;
    }
    for (auto& v : convertor.GetGlobalNominalCache().GetALL()) {
        g_typeMap[v.second] = StaticCast<InheritableDecl>(v.first);
    }
    res->map = CreateAnnoInstFromConstEvalRes(*res);
    if (ci.invocation.globalOptions.computeAnnotationsDebug) {
        res->Dump();
    }
    g_typeMap.clear();
    g_valueMap.clear();
    g_annoFactoryMap.clear();
    return res;
}
} // namespace Cangjie
