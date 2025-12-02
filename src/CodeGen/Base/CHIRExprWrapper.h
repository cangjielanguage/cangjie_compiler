// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements codegen Wrappers for CHIR Expression.
 */
#ifndef CANGJIE_CHIREXPRWRAPPER_H
#define CANGJIE_CHIREXPRWRAPPER_H

#include "cangjie/Basic/Print.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/Utils/Utils.h"

namespace Cangjie {
namespace CodeGen {
class CHIRExprWrapper {
public:
    explicit CHIRExprWrapper(const CHIR::Expression& chirExpr) : chirExpr(chirExpr)
    {
    }

    virtual ~CHIRExprWrapper() = default;

    const CHIR::Expression& GetChirExpr() const
    {
        return chirExpr;
    }

    CHIR::ExprMajorKind GetExprMajorKind() const
    {
        return chirExpr.GetExprMajorKind();
    }

    CHIR::ExprKind GetExprKind() const
    {
        return chirExpr.GetExprKind();
    }

    bool IsConstant() const
    {
        return chirExpr.IsConstant();
    }

    bool IsConstantNull() const
    {
        return chirExpr.IsConstantNull();
    }

    bool IsConstantInt() const
    {
        return chirExpr.IsConstantInt();
    }

    bool IsConstantString() const
    {
        return chirExpr.IsConstantString();
    }

    std::string GetExprKindName() const
    {
        return chirExpr.GetExprKindName();
    }

    CHIR::Block* GetParentBlock() const
    {
        return chirExpr.GetParentBlock();
    }

    CHIR::Func* GetTopLevelFunc() const
    {
        return chirExpr.GetTopLevelFunc();
    }

    unsigned GetNumOfOperands() const
    {
        return chirExpr.GetNumOfOperands();
    }

    std::vector<CHIR::Value*> GetOperands() const
    {
        return chirExpr.GetOperands();
    }

    CHIR::Value* GetOperand(unsigned idx) const
    {
        return chirExpr.GetOperand(idx);
    }

    CHIR::LocalVar* GetResult() const
    {
        return chirExpr.GetResult();
    }

    std::string ToString(unsigned indent = 0) const
    {
        return chirExpr.ToString(indent);
    }

    bool IsTerminator() const
    {
        return chirExpr.IsTerminator();
    }

    // Get the value of the annotation T associated to this node
    template <typename T> typename std::invoke_result<decltype(T::Extract), const T*>::type Get() const
    {
        return chirExpr.Get<T>();
    }

    const CHIR::DebugLocation& GetDebugLocation() const
    {
        return chirExpr.GetDebugLocation();
    }

    void Dump() const
    {
        chirExpr.Dump();
    }

protected:
    const CHIR::Expression& chirExpr;
};

class CHIRCallExpr : public CHIRExprWrapper {
public:
    explicit CHIRCallExpr(const CHIR::Expression& chirExpr) : CHIRExprWrapper(chirExpr)
    {
    }
    virtual CHIR::Type* GetThisType() const = 0;
    virtual std::vector<CHIR::Type*> GetInstantiatedTypeArgs() const = 0;
    virtual bool IsCalleeMethod() const = 0;
    virtual bool IsCalleeStructInstanceMethod() const = 0;
    virtual bool IsCalleeStatic() const = 0;
    virtual const CHIR::Type* GetOuterType([[maybe_unused]] CHIR::CHIRBuilder& builder) const = 0;
    virtual const CHIR::Value* GetThisParam() const = 0;
};

class CHIRApplyWrapper : public CHIRCallExpr {
public:
    explicit CHIRApplyWrapper(const CHIR::Apply& apply) : CHIRCallExpr(apply)
    {
        if (GetInstantiatedTypeArgs().size() != GetCalleeTypeArgsNum()) {
#ifndef NDEBUG
            Errorln(chirExpr.ToString() + "\n");
#endif
            CJC_ASSERT(false && "Incorrect ApplyExpr from CHIR, type arguments are missing.");
        }
    }

    explicit CHIRApplyWrapper(const CHIR::ApplyWithException& applyWithException) : CHIRCallExpr(applyWithException)
    {
        if (GetInstantiatedTypeArgs().size() != GetCalleeTypeArgsNum()) {
#ifndef NDEBUG
            Errorln(chirExpr.ToString() + "\n");
#endif
            CJC_ASSERT(false && "Incorrect ApplyExpr from CHIR, type arguments are missing.");
        }
    }

    CHIRApplyWrapper(const CHIRApplyWrapper& chirExprW) : CHIRCallExpr(chirExprW.chirExpr)
    {
    }

    ~CHIRApplyWrapper() = default;

    CHIR::Value* GetCallee() const
    {
        if (GetExprKind() == CHIR::ExprKind::APPLY) {
            return StaticCast<const CHIR::Apply&>(chirExpr).GetCallee();
        } else {
            return StaticCast<const CHIR::ApplyWithException&>(chirExpr).GetCallee();
        }
    }

    std::vector<CHIR::Value*> GetArgs() const
    {
        if (GetExprKind() == CHIR::ExprKind::APPLY) {
            return StaticCast<const CHIR::Apply&>(chirExpr).GetArgs();
        } else {
            return StaticCast<const CHIR::ApplyWithException&>(chirExpr).GetArgs();
        }
    }

    std::vector<CHIR::Type*> GetInstantiatedTypeArgs() const override
    {
        if (GetExprKind() == CHIR::ExprKind::APPLY) {
            return StaticCast<const CHIR::Apply&>(chirExpr).GetInstantiatedTypeArgs();
        } else {
            return StaticCast<const CHIR::ApplyWithException&>(chirExpr).GetInstantiatedTypeArgs();
        }
    }

    CHIR::Type* GetThisType() const override
    {
        if (GetExprKind() == CHIR::ExprKind::APPLY) {
            return StaticCast<const CHIR::Apply&>(chirExpr).GetThisType();
        } else {
            return StaticCast<const CHIR::ApplyWithException&>(chirExpr).GetThisType();
        }
    }

    bool IsCalleeMethod() const override
    {
        bool isCallee = false;
        if (auto func = DynamicCast<CHIR::FuncBase*>(GetCallee())) {
            isCallee = func->IsMemberFunc();
        }
        return isCallee;
    }

    bool IsCalleeStatic() const override
    {
        return GetCallee()->TestAttr(CHIR::Attribute::STATIC);
    }

    bool IsCalleeStructInstanceMethod() const override
    {
        if (!IsCalleeMethod() || IsCalleeStatic()) {
            return false;
        }

        auto outer = VirtualCast<CHIR::FuncBase*>(GetCallee())->GetOuterDeclaredOrExtendedDef();
        return outer && outer->IsStruct();
    }

    bool IsCalleeStructMutOrCtorMethod() const
    {
        return IsCalleeStructInstanceMethod() &&
            (GetCallee()->TestAttr(CHIR::Attribute::MUT) || CHIR::IsConstructor(*GetCallee()));
    }

    const CHIR::Type* GetOuterType(CHIR::CHIRBuilder& builder) const override
    {
        CHIR::Type* res = nullptr;
        if (GetExprKind() == CHIR::ExprKind::APPLY) {
            res = StaticCast<const CHIR::Apply&>(chirExpr).GetInstParentCustomTyOfCallee(builder);
        } else {
            res = StaticCast<const CHIR::ApplyWithException&>(chirExpr).GetInstParentCustomTyOfCallee(builder);
        }
        if (!res) {
#ifndef NDEBUG
            Errorln("Should not get a nullptr:\n", chirExpr.ToString());
#endif
            CJC_ASSERT(false);
        }
        return res;
    }

    const CHIR::Value* GetThisParam() const override
    {
        return IsCalleeMethod() && !IsCalleeStatic() ? GetArgs()[0] : nullptr;
    }

private:
    size_t GetCalleeTypeArgsNum() const
    {
        if (GetCallee()->IsFunc()) {
            return VirtualCast<CHIR::FuncBase*>(GetCallee())->GetGenericTypeParams().size();
        }
        return 0;
    }
};

class CHIRInvokeWrapper : public CHIRCallExpr {
public:
    explicit CHIRInvokeWrapper(const CHIR::Invoke& invoke) : CHIRCallExpr(invoke)
    {
    }

    explicit CHIRInvokeWrapper(const CHIR::InvokeWithException& invokeWithException) : CHIRCallExpr(invokeWithException)
    {
    }

    ~CHIRInvokeWrapper() = default;

    CHIR::Value* GetObject() const
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKE) {
            return StaticCast<const CHIR::Invoke&>(chirExpr).GetObject();
        } else {
            return StaticCast<const CHIR::InvokeWithException&>(chirExpr).GetObject();
        }
    }

    std::string GetMethodName() const
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKE) {
            return StaticCast<const CHIR::Invoke&>(chirExpr).GetMethodName();
        } else {
            return StaticCast<const CHIR::InvokeWithException&>(chirExpr).GetMethodName();
        }
    }

    CHIR::FuncType* GetMethodType() const
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKE) {
            return StaticCast<const CHIR::Invoke&>(chirExpr).GetMethodType();
        } else {
            return StaticCast<const CHIR::InvokeWithException&>(chirExpr).GetMethodType();
        }
    }

    std::vector<CHIR::Value*> GetArgs() const
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKE) {
            return StaticCast<const CHIR::Invoke&>(chirExpr).GetArgs();
        } else {
            return StaticCast<const CHIR::InvokeWithException&>(chirExpr).GetArgs();
        }
    }

    std::vector<CHIR::Type*> GetInstantiatedTypeArgs() const override
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKE) {
            return StaticCast<const CHIR::Invoke&>(chirExpr).GetInstantiatedTypeArgs();
        } else {
            return StaticCast<const CHIR::InvokeWithException&>(chirExpr).GetInstantiatedTypeArgs();
        }
    }

    CHIR::Type* GetThisType() const override
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKE) {
            return StaticCast<const CHIR::Invoke&>(chirExpr).GetObject()->GetType();
        } else {
            return StaticCast<const CHIR::InvokeWithException&>(chirExpr).GetObject()->GetType();
        }
    }

    bool IsCalleeMethod() const override
    {
        return true;
    }

    bool IsCalleeStatic() const override
    {
        return false;
    }

    bool IsCalleeStructInstanceMethod() const override
    {
        return false;
    }

    const CHIR::Type* GetOuterType(CHIR::CHIRBuilder& builder) const override
    {
        CJC_ASSERT(!IsCalleeStatic());
        CHIR::Type* res = nullptr;
        if (GetExprKind() == CHIR::ExprKind::INVOKE) {
            res = StaticCast<const CHIR::Invoke&>(chirExpr).GetInstSrcParentCustomTypeOfMethod(builder);
        } else {
            res = StaticCast<const CHIR::InvokeWithException&>(chirExpr).GetInstSrcParentCustomTypeOfMethod(builder);
        }
        if (!res) {
#ifndef NDEBUG
            Errorln("Should not get a nullptr:\n", chirExpr.ToString());
#endif
            CJC_ASSERT(false);
        }
        return res;
    }

    const CHIR::Value* GetThisParam() const override
    {
        CJC_ASSERT(!IsCalleeStatic());
        return GetObject();
    }

    size_t GetVirtualMethodOffset() const
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKE) {
            return StaticCast<const CHIR::Invoke&>(chirExpr).GetVirtualMethodOffset();
        } else {
            return StaticCast<const CHIR::InvokeWithException&>(chirExpr).GetVirtualMethodOffset();
        }
    }

    bool TestVritualMethodAttr(CHIR::CHIRBuilder& builder, CHIR::Attribute attr) const
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKE) {
            return StaticCast<const CHIR::Invoke&>(chirExpr).GetVirtualMethodAttr(builder).TestAttr(attr);
        } else {
            return StaticCast<const CHIR::InvokeWithException&>(chirExpr).GetVirtualMethodAttr(builder).TestAttr(attr);
        }
    }
};

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
class CHIRInvokeStaticWrapper : public CHIRCallExpr {
public:
    explicit CHIRInvokeStaticWrapper(const CHIR::InvokeStatic& invokeStatic) : CHIRCallExpr(invokeStatic)
    {
    }

    explicit CHIRInvokeStaticWrapper(const CHIR::InvokeStaticWithException& invokeStaticWithException)
        : CHIRCallExpr(invokeStaticWithException)
    {
    }

    ~CHIRInvokeStaticWrapper() = default;

    std::string GetMethodName() const
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKESTATIC) {
            return StaticCast<const CHIR::InvokeStatic&>(chirExpr).GetMethodName();
        } else {
            return StaticCast<const CHIR::InvokeStaticWithException&>(chirExpr).GetMethodName();
        }
    }

    CHIR::FuncType* GetMethodType() const
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKESTATIC) {
            return StaticCast<const CHIR::InvokeStatic&>(chirExpr).GetMethodType();
        } else {
            return StaticCast<const CHIR::InvokeStaticWithException&>(chirExpr).GetMethodType();
        }
    }

    CHIR::Value* GetRTTIValue() const
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKESTATIC) {
            return StaticCast<const CHIR::InvokeStatic&>(chirExpr).GetRTTIValue();
        } else {
            return StaticCast<const CHIR::InvokeStaticWithException&>(chirExpr).GetRTTIValue();
        }
    }

    std::vector<CHIR::Value*> GetArgs() const
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKESTATIC) {
            return StaticCast<const CHIR::InvokeStatic&>(chirExpr).GetArgs();
        } else {
            return StaticCast<const CHIR::InvokeStaticWithException&>(chirExpr).GetArgs();
        }
    }

    CHIR::Type* GetThisType() const override
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKESTATIC) {
            return StaticCast<const CHIR::InvokeStatic&>(chirExpr).GetThisType();
        } else {
            return StaticCast<const CHIR::InvokeStaticWithException&>(chirExpr).GetThisType();
        }
    }

    std::vector<CHIR::Type*> GetInstantiatedTypeArgs() const override
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKESTATIC) {
            return StaticCast<const CHIR::InvokeStatic&>(chirExpr).GetInstantiatedTypeArgs();
        } else {
            return StaticCast<const CHIR::InvokeStaticWithException&>(chirExpr).GetInstantiatedTypeArgs();
        }
    }

    bool IsCalleeMethod() const override
    {
        return true;
    }

    bool IsCalleeStatic() const override
    {
        return true;
    }

    bool IsCalleeStructInstanceMethod() const override
    {
        return false;
    }

    const CHIR::Type* GetOuterType(CHIR::CHIRBuilder& builder) const override
    {
        CJC_ASSERT(IsCalleeStatic());
        CHIR::Type* res = nullptr;
        if (GetExprKind() == CHIR::ExprKind::INVOKESTATIC) {
            res = StaticCast<const CHIR::InvokeStatic&>(chirExpr).GetInstSrcParentCustomTypeOfMethod(builder);
        } else {
            res = StaticCast<const CHIR::InvokeStaticWithException&>(chirExpr).GetInstSrcParentCustomTypeOfMethod(
                builder);
        }
        if (!res) {
#ifndef NDEBUG
            Errorln("Should not get a nullptr:\n", chirExpr.ToString());
#endif
            CJC_ASSERT(false);
        }
        return res;
    }

    const CHIR::Value* GetThisParam() const override
    {
        CJC_ASSERT(false && "InvokeStatic doesn't have this param.");
        return nullptr;
    }

    size_t GetVirtualMethodOffset() const
    {
        if (GetExprKind() == CHIR::ExprKind::INVOKESTATIC) {
            return StaticCast<const CHIR::InvokeStatic&>(chirExpr).GetVirtualMethodOffset();
        } else {
            return StaticCast<const CHIR::InvokeStaticWithException&>(chirExpr).GetVirtualMethodOffset();
        }
    }
};
#endif

class CHIRUnaryExprWrapper : public CHIRExprWrapper {
public:
    explicit CHIRUnaryExprWrapper(const CHIR::UnaryExpression& unaryExpr) : CHIRExprWrapper(unaryExpr)
    {
    }

    explicit CHIRUnaryExprWrapper(const CHIR::IntOpWithException& unaryExprWithException)
        : CHIRExprWrapper(unaryExprWithException)
    {
    }

    ~CHIRUnaryExprWrapper() = default;

    CHIR::Value* GetOperand() const
    {
        if (GetExprMajorKind() == CHIR::ExprMajorKind::UNARY_EXPR) {
            return StaticCast<const CHIR::UnaryExpression&>(chirExpr).GetOperand();
        } else {
            return StaticCast<const CHIR::IntOpWithException&>(chirExpr).GetOperands()[0];
        }
    }

    OverflowStrategy GetOverflowStrategy() const
    {
        if (GetExprMajorKind() == CHIR::ExprMajorKind::UNARY_EXPR) {
            return StaticCast<const CHIR::UnaryExpression&>(chirExpr).GetOverflowStrategy();
        } else {
            return OverflowStrategy::THROWING;
        }
    }

    CHIR::ExprKind GetUnaryExprKind() const
    {
        if (GetExprMajorKind() == CHIR::ExprMajorKind::UNARY_EXPR) {
            return StaticCast<const CHIR::UnaryExpression&>(chirExpr).GetExprKind();
        } else {
            return StaticCast<const CHIR::IntOpWithException&>(chirExpr).GetOpKind();
        }
    }

private:
    CHIR::ExprKind GetExprKind() const = delete;
};

class CHIRBinaryExprWrapper : public CHIRExprWrapper {
public:
    explicit CHIRBinaryExprWrapper(const CHIR::BinaryExpression& binaryExpr) : CHIRExprWrapper(binaryExpr)
    {
    }

    explicit CHIRBinaryExprWrapper(const CHIR::IntOpWithException& binaryExprWithException)
        : CHIRExprWrapper(binaryExprWithException)
    {
    }

    ~CHIRBinaryExprWrapper() = default;

    CHIR::Value* GetLHSOperand() const
    {
        if (GetExprMajorKind() == CHIR::ExprMajorKind::BINARY_EXPR) {
            return StaticCast<const CHIR::BinaryExpression&>(chirExpr).GetLHSOperand();
        } else {
            return StaticCast<const CHIR::IntOpWithException&>(chirExpr).GetOperands()[0];
        }
    }

    CHIR::Value* GetRHSOperand() const
    {
        if (GetExprMajorKind() == CHIR::ExprMajorKind::BINARY_EXPR) {
            return StaticCast<const CHIR::BinaryExpression&>(chirExpr).GetRHSOperand();
        } else {
            return StaticCast<const CHIR::IntOpWithException&>(chirExpr).GetOperands()[1];
        }
    }

    OverflowStrategy GetOverflowStrategy() const
    {
        if (GetExprMajorKind() == CHIR::ExprMajorKind::BINARY_EXPR) {
            return StaticCast<const CHIR::BinaryExpression&>(chirExpr).GetOverflowStrategy();
        } else {
            return StaticCast<const CHIR::IntOpWithException&>(chirExpr).GetOverflowStrategy();
        }
    }

    CHIR::ExprKind GetBinaryExprKind() const
    {
        if (GetExprMajorKind() == CHIR::ExprMajorKind::BINARY_EXPR) {
            return StaticCast<const CHIR::BinaryExpression&>(chirExpr).GetExprKind();
        } else {
            return StaticCast<const CHIR::IntOpWithException&>(chirExpr).GetOpKind();
        }
    }

private:
    CHIR::ExprKind GetExprKind() const = delete;
};

class CHIRSpawnWrapper : public CHIRExprWrapper {
public:
    explicit CHIRSpawnWrapper(const CHIR::Spawn& spawn) : CHIRExprWrapper(spawn)
    {
    }

    explicit CHIRSpawnWrapper(const CHIR::SpawnWithException& spawnWithException) : CHIRExprWrapper(spawnWithException)
    {
    }

    ~CHIRSpawnWrapper() = default;

    CHIR::Value* GetFuture() const
    {
        if (GetExprKind() == CHIR::ExprKind::SPAWN) {
            return StaticCast<const CHIR::Spawn&>(chirExpr).GetFuture();
        } else {
            return StaticCast<const CHIR::SpawnWithException&>(chirExpr).GetFuture();
        }
    }

    CHIR::Value* GetSpawnArg() const
    {
        if (GetExprKind() == CHIR::ExprKind::SPAWN) {
            return StaticCast<const CHIR::Spawn&>(chirExpr).GetSpawnArg();
        } else {
            return StaticCast<const CHIR::SpawnWithException&>(chirExpr).GetSpawnArg();
        }
    }

    CHIR::Value* GetClosure() const
    {
        if (GetExprKind() == CHIR::ExprKind::SPAWN) {
            return StaticCast<const CHIR::Spawn&>(chirExpr).GetClosure();
        } else {
            return StaticCast<const CHIR::SpawnWithException&>(chirExpr).GetClosure();
        }
    }

    CHIR::FuncBase* GetExecuteClosure() const
    {
        if (GetExprKind() == CHIR::ExprKind::SPAWN) {
            return StaticCast<const CHIR::Spawn&>(chirExpr).GetExecuteClosure();
        } else {
            return StaticCast<const CHIR::SpawnWithException&>(chirExpr).GetExecuteClosure();
        }
    }

    bool IsExecuteClosure() const
    {
        if (GetExprKind() == CHIR::ExprKind::SPAWN) {
            return StaticCast<const CHIR::Spawn&>(chirExpr).IsExecuteClosure();
        } else {
            return StaticCast<const CHIR::SpawnWithException&>(chirExpr).IsExecuteClosure();
        }
    }
};

class CHIRTypeCastWrapper : public CHIRExprWrapper {
public:
    explicit CHIRTypeCastWrapper(const CHIR::TypeCast& typeCast) : CHIRExprWrapper(typeCast)
    {
    }

    explicit CHIRTypeCastWrapper(const CHIR::TypeCastWithException& typeCastWithException)
        : CHIRExprWrapper(typeCastWithException)
    {
    }

    ~CHIRTypeCastWrapper() = default;

    CHIR::Value* GetSourceValue() const
    {
        if (GetExprKind() == CHIR::ExprKind::TYPECAST) {
            return StaticCast<const CHIR::TypeCast&>(chirExpr).GetSourceValue();
        } else {
            return StaticCast<const CHIR::TypeCastWithException&>(chirExpr).GetSourceValue();
        }
    }

    CHIR::Type* GetSourceTy() const
    {
        if (GetExprKind() == CHIR::ExprKind::TYPECAST) {
            return StaticCast<const CHIR::TypeCast&>(chirExpr).GetSourceTy();
        } else {
            return StaticCast<const CHIR::TypeCastWithException&>(chirExpr).GetSourceTy();
        }
    }

    CHIR::Type* GetTargetTy() const
    {
        if (GetExprKind() == CHIR::ExprKind::TYPECAST) {
            return StaticCast<const CHIR::TypeCast&>(chirExpr).GetTargetTy();
        } else {
            return StaticCast<const CHIR::TypeCastWithException&>(chirExpr).GetTargetTy();
        }
    }

    OverflowStrategy GetOverflowStrategy() const
    {
        if (GetExprKind() == CHIR::ExprKind::TYPECAST) {
            return StaticCast<const CHIR::TypeCast&>(chirExpr).GetOverflowStrategy();
        } else {
            return OverflowStrategy::THROWING;
        }
    }
};

class CHIRIntrinsicWrapper : public CHIRExprWrapper {
public:
    explicit CHIRIntrinsicWrapper(const CHIR::Intrinsic& intrinsic) : CHIRExprWrapper(intrinsic)
    {
    }

    explicit CHIRIntrinsicWrapper(const CHIR::IntrinsicWithException& intrinsicWithException)
        : CHIRExprWrapper(intrinsicWithException)
    {
    }

    ~CHIRIntrinsicWrapper() = default;

    CHIR::IntrinsicKind GetIntrinsicKind() const
    {
        if (GetExprKind() == CHIR::ExprKind::INTRINSIC) {
            return StaticCast<const CHIR::Intrinsic&>(chirExpr).GetIntrinsicKind();
        } else {
            return StaticCast<const CHIR::IntrinsicWithException&>(chirExpr).GetIntrinsicKind();
        }
    }

    std::vector<CHIR::Type*> GetInstantiatedTypeArgs() const
    {
        if (GetExprKind() == CHIR::ExprKind::INTRINSIC) {
            return StaticCast<const CHIR::Intrinsic&>(chirExpr).GetInstantiatedTypeArgs();
        } else {
            return StaticCast<const CHIR::IntrinsicWithException&>(chirExpr).GetInstantiatedTypeArgs();
        }
    }
};

class CHIRAllocateWrapper : public CHIRExprWrapper {
public:
    explicit CHIRAllocateWrapper(const CHIR::Allocate& allocate) : CHIRExprWrapper(allocate)
    {
    }

    explicit CHIRAllocateWrapper(const CHIR::AllocateWithException& allocateWithException)
        : CHIRExprWrapper(allocateWithException)
    {
    }

    ~CHIRAllocateWrapper() = default;

    CHIR::Type* GetType() const
    {
        if (GetExprKind() == CHIR::ExprKind::ALLOCATE) {
            return StaticCast<const CHIR::Allocate&>(chirExpr).GetType();
        } else {
            return StaticCast<const CHIR::AllocateWithException&>(chirExpr).GetType();
        }
    }
};

class CHIRRawArrayAllocateWrapper : public CHIRExprWrapper {
public:
    explicit CHIRRawArrayAllocateWrapper(const CHIR::RawArrayAllocate& rawArrayAllocate)
        : CHIRExprWrapper(rawArrayAllocate)
    {
    }

    explicit CHIRRawArrayAllocateWrapper(const CHIR::RawArrayAllocateWithException& rawArrayAllocateWithException)
        : CHIRExprWrapper(rawArrayAllocateWithException)
    {
    }

    ~CHIRRawArrayAllocateWrapper() = default;

    CHIR::Value* GetSize() const
    {
        if (GetExprKind() == CHIR::ExprKind::RAW_ARRAY_ALLOCATE) {
            return StaticCast<const CHIR::RawArrayAllocate&>(chirExpr).GetSize();
        } else {
            return StaticCast<const CHIR::RawArrayAllocateWithException&>(chirExpr).GetSize();
        }
    }

    CHIR::Type* GetElementType() const
    {
        if (GetExprKind() == CHIR::ExprKind::RAW_ARRAY_ALLOCATE) {
            return StaticCast<const CHIR::RawArrayAllocate&>(chirExpr).GetElementType();
        } else {
            return StaticCast<const CHIR::RawArrayAllocateWithException&>(chirExpr).GetElementType();
        }
    }
};
} // namespace CodeGen
} // namespace Cangjie
#endif // CANGJIE_CHIREXPRWRAPPER_H
