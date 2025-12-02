// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANNOTATION_H
#define CANGJIE_CHIR_ANNOTATION_H

#include "cangjie/Basic/Linkage.h"
#include "cangjie/CHIR/IR/DebugLocation.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

namespace Cangjie::CHIR {

class FuncBase;
class FuncType;

struct Annotation {
    Annotation() = default;
    Annotation(const Annotation&) = default;
    Annotation& operator=(const Annotation&) = default;
    virtual ~Annotation() = default;
    virtual std::unique_ptr<Annotation> Clone() = 0;

    virtual std::string ToString() = 0;
};

/**
 * R: CodeGen
 * W: ConstAnalysis
 */
struct NeedCheckArrayBound : public Annotation {
public:
    explicit NeedCheckArrayBound() = default;
    explicit NeedCheckArrayBound(bool input) : need(input)
    {
    }

    static bool Extract(const NeedCheckArrayBound* input)
    {
        return input->need;
    }

    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<NeedCheckArrayBound>(need);
    }

    std::string ToString() override
    {
        std::string needStr = need ? "true" : "false";
        return "checkArrayBound: " + needStr;
    }

private:
    bool need = true;
};

/**
 * W: AST2CHIR, Transformation/Devirtualization
 * R: IRChecker
 */
struct NeedCheckCast : public Annotation {
public:
    explicit NeedCheckCast() = default;
    explicit NeedCheckCast(bool checked) : need(checked)
    {
    }

    static bool Extract(const NeedCheckCast* needChecked)
    {
        return needChecked->need;
    }

    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<NeedCheckCast>(need);
    }

    std::string ToString() override
    {
        std::string needStr = need ? "true" : "false";
        return "checkTypeCast: " + needStr;
    }

private:
    bool need = true;
};

struct DebugLocationInfoForWarning : public Annotation {
public:
    explicit DebugLocationInfoForWarning() = default;
    explicit DebugLocationInfoForWarning(const DebugLocation& locationInfo) : location(locationInfo)
    {
    }

    static const DebugLocation Extract(const DebugLocationInfoForWarning* input)
    {
        return input->location;
    }

    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<DebugLocationInfoForWarning>(location);
    }

    std::string ToString() override
    {
        return "warning " + location.ToString();
    }

private:
    DebugLocation location;
};

struct LinkTypeInfo : public Annotation {
public:
    explicit LinkTypeInfo() : linkType(Cangjie::Linkage::EXTERNAL){};
    explicit LinkTypeInfo(const Cangjie::Linkage& linkType) : linkType(linkType){};

    static Cangjie::Linkage Extract(const LinkTypeInfo* li)
    {
        return li->linkType;
    }

    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<LinkTypeInfo>(linkType);
    }

    std::string ToString() override
    {
        std::map<Cangjie::Linkage, std::string> linkToString = {{Cangjie::Linkage::EXTERNAL, "external"},
            {Cangjie::Linkage::WEAK_ODR, "weak_odr"}, {Cangjie::Linkage::INTERNAL, "internal"},
            {Cangjie::Linkage::LINKONCE_ODR, "linkonce_odr"}};
        return "linkType: " + linkToString[linkType];
    }

private:
    Cangjie::Linkage linkType;
};

struct WrappedRawMethod : public Annotation {
public:
    explicit WrappedRawMethod() = default;
    explicit WrappedRawMethod(FuncBase* method) : rawMethod(method)
    {
    }

    static FuncBase* Extract(const WrappedRawMethod* input)
    {
        return input->rawMethod;
    }

    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<WrappedRawMethod>(rawMethod);
    }

    std::string ToString() override;

private:
    FuncBase* rawMethod{nullptr};
};

/**
 * the marks used by some different pass in chir.
 *
 * NOTE: currently the types of nodes using these skip kind will not be duplicated,
 * so we only need to store one skipping kind.
 */
enum class SkipKind : uint8_t {
    NO_SKIP,
    SKIP_DCE_WARNING,
    SKIP_FORIN_EXIT,
    SKIP_VIC,
};

struct SkipCheck : public Annotation {
public:
    explicit SkipCheck() = default;
    explicit SkipCheck(SkipKind kind) : kind(kind)
    {
    }

    static SkipKind Extract(const SkipCheck* input)
    {
        return input->kind;
    }

    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<SkipCheck>(kind);
    }

    std::string ToString() override;

private:
    SkipKind kind{SkipKind::NO_SKIP};
};

struct NeverOverflowInfo : public Annotation {
public:
    explicit NeverOverflowInfo() = default;
    explicit NeverOverflowInfo(bool b) : neverOverflowInfo(b){};

    static bool Extract(const NeverOverflowInfo* info)
    {
        return info->neverOverflowInfo;
    }

    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<NeverOverflowInfo>(neverOverflowInfo);
    }

    std::string ToString() override
    {
        std::string str = neverOverflowInfo ? "true" : "false";
        return "NeverOverflowInfo: " + str;
    }

private:
    bool neverOverflowInfo{false};
};

struct IsAutoEnvClass : public Annotation {
public:
    explicit IsAutoEnvClass() = default;
    explicit IsAutoEnvClass(bool b) : isAutoEnv(b){};

    static bool Extract(const IsAutoEnvClass* info)
    {
        return info->isAutoEnv;
    }

    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<IsAutoEnvClass>(isAutoEnv);
    }

    std::string ToString() override
    {
        std::string str = isAutoEnv ? "true" : "false";
        return "IsAutoEnvClass: " + str;
    }

private:
    bool isAutoEnv{false};
};

struct IsCapturedClassInCC : public Annotation {
public:
    explicit IsCapturedClassInCC() = default;
    explicit IsCapturedClassInCC(bool b) : flag(b){};

    static bool Extract(const IsCapturedClassInCC* info)
    {
        return info->flag;
    }

    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<IsCapturedClassInCC>(flag);
    }

    std::string ToString() override
    {
        std::string str = flag ? "true" : "false";
        return "IsCapturedClassInCC: " + str;
    }

private:
    bool flag{false};
};

struct EnumCaseIndex : public Annotation {
public:
    explicit EnumCaseIndex() = default;
    explicit EnumCaseIndex(std::optional<size_t> b) : index(b){};

    static std::optional<size_t> Extract(const EnumCaseIndex* info)
    {
        return info->index;
    }

    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<EnumCaseIndex>(index);
    }

    std::string ToString() override
    {
        if (index.has_value()) {
            return "EnumCaseIndex: " + std::to_string(index.value());
        }
        return "";
    }

private:
    std::optional<size_t> index{std::nullopt};
};

struct VirMethodOffset : public Annotation {
public:
    explicit VirMethodOffset() = default;
    explicit VirMethodOffset(std::optional<size_t> v) : offset(v){};

    static std::optional<size_t> Extract(const VirMethodOffset* info)
    {
        return info->offset;
    }

    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<VirMethodOffset>(offset);
    }

    std::string ToString() override
    {
        if (offset.has_value()) {
            return "VirMethodOffset: " + std::to_string(offset.value());
        }
        return "";
    }

private:
    std::optional<size_t> offset{std::nullopt};
};

struct OverrideSrcFuncType : public Annotation {
public:
    explicit OverrideSrcFuncType() = default;
    explicit OverrideSrcFuncType(FuncType* ty) : type(ty){};

    static FuncType* Extract(const OverrideSrcFuncType* info)
    {
        return info->type;
    }

    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<OverrideSrcFuncType>(type);
    }

    std::string ToString() override;

private:
    FuncType* type{nullptr};
};

// This class is used to manage CHIR Annotation's.
class AnnotationMap final {
public: // Set annotation T for this node, updating its value if it already exists
    template <typename T, typename... Args> void Set(Args&&... args)
    {
        auto index = std::type_index(typeid(T));
        annotations[index] = std::make_unique<T>(std::forward<Args>(args)...);
    }

    // Get the value of the annotation T associated to this node
    template <typename T> typename std::invoke_result_t<decltype(T::Extract), const T*> Get() const
    {
        auto index = std::type_index(typeid(T));
        auto location = annotations.find(index);
        // Check if we have a recorded annotation. Otherwise we return a default
        // empty value for this kind of annotation.
        if (location != annotations.end()) {
            return T::Extract(static_cast<const T*>(location->second.get()));
        } else {
            auto emptyT = T();
            return T::Extract(&emptyT);
        }
    }
    
    template <typename T> void Remove()
    {
        auto index = std::type_index(typeid(T));
        annotations.erase(index);
    }

    /// Returns a reference to the annotation. Adds a new one if none exists. This API is used to change the associated
    /// annotation value.
    template <class T>
    T& GetAnno()
    {
        if (auto location = annotations.find(std::type_index(typeid(T))); location != annotations.end()) {
            return static_cast<T&>(*location->second);
        } else {
            auto inserted = annotations.emplace(std::type_index(typeid(T)), std::make_unique<T>());
            return static_cast<T&>(*inserted.first->second);
        }
    }

    inline const DebugLocation& GetDebugLocation() const
    {
        return loc;
    }
    inline void SetDebugLocation(const DebugLocation& newLoc)
    {
        loc = newLoc;
    }
    inline void SetDebugLocation(DebugLocation&& newLoc)
    {
        loc = std::move(newLoc);
    }

    std::string ToString() const;

    AnnotationMap() = default;

    AnnotationMap(const AnnotationMap& other)
    {
        for (auto& anno : other.annotations) {
            annotations[anno.first] = anno.second->Clone();
        }
        loc = other.loc;
    }
    AnnotationMap(AnnotationMap&& other) = default;

    AnnotationMap& operator=(const AnnotationMap& other)
    {
        if (this == &other) {
            return *this;
        }
        annotations.clear();
        for (auto& anno : other.annotations) {
            annotations[anno.first] = anno.second->Clone();
        }
        loc = other.loc;
        return *this;
    }
    AnnotationMap& operator=(AnnotationMap&& other)
    {
        swap(annotations, other.annotations);
        loc = other.loc;
        return *this;
    }

    const std::unordered_map<std::type_index, std::unique_ptr<Annotation>>& GetAnnos() const
    {
        return annotations;
    }

private:
    std::unordered_map<std::type_index, std::unique_ptr<Annotation>> annotations;
    // DebugLocation is a specialised field for better performance, since most expression/value/decl/type has one.
    DebugLocation loc{};
};

/// Each annotation object is translated to a global var for consteval requirements.
/// This records all the translated global variables.
struct AnnoFactoryInfo final : public Annotation {
    AnnoFactoryInfo() : value{} {}
    explicit AnnoFactoryInfo(std::vector<class GlobalVar*> values) : value{std::move(values)} {}
    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<AnnoFactoryInfo>(value);
    }
 
    std::string ToString() override;
 
    static const std::vector<class GlobalVar*>& Extract(const AnnoFactoryInfo* label)
    {
        return label->value;
    }
 
private:
    std::vector<class GlobalVar*> value;
};

/// This type is used to track CHIR node generated from for-in expr translation, so const propagation may optimise on
/// them even in O0.
struct GeneratedFromForIn final : public Annotation {
    GeneratedFromForIn() : value{false} {}
    explicit GeneratedFromForIn(bool) : value{true} {}
    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<GeneratedFromForIn>();
    }
 
    std::string ToString() override
    {
        return "// generated-from-forin ";
    }
 
    static bool Extract(const GeneratedFromForIn* label)
    {
        return label->value;
    }
 
private:
    bool value;
};
} // namespace Cangjie::CHIR
#endif
