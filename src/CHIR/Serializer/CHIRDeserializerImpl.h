// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_DESERIALIZER_IMPL_H
#define CANGJIE_CHIR_DESERIALIZER_IMPL_H

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
#include <flatbuffers/PackageFormat_generated.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include <fstream>
#include <iostream>
#include <utility>

#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/CHIRContext.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Serializer/CHIRDeserializer.h"
#include "cangjie/CHIR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie::CHIR {

class CHIRDeserializer::CHIRDeserializerImpl {
public:
    void ConfigBase(const PackageFormat::Base* buffer, Base& obj);
    void ConfigValue(const PackageFormat::Value* buffer, Value& obj);
    void ConfigCustomTypeDef(const PackageFormat::CustomTypeDef* buffer, CustomTypeDef& obj);
    void ConfigExpression(const PackageFormat::Expression* buffer, Expression& obj);
    template <typename T, typename FBT> T Create(const FBT* obj);
    template <typename T, typename FBT> std::vector<T> Create(const flatbuffers::Vector<FBT>* vec);
    template <typename T, typename FBT> T* Deserialize(const FBT* obj);
    template <typename T> std::vector<T*> GetValue(const flatbuffers::Vector<uint32_t>* vec);
    template <typename T> std::vector<T*> GetType(const flatbuffers::Vector<uint32_t>* vec);
    template <typename T> std::vector<T*> GetExpression(const flatbuffers::Vector<uint32_t>* vec);
    template <typename T> std::vector<T*> GetCustomTypeDef(const flatbuffers::Vector<uint32_t>* vec);
    template <typename T> T* GetValue(uint32_t id);
    Value* GetValue(uint32_t id);
    template <typename T> T* GetType(uint32_t id);
    Type* GetType(uint32_t id);
    template <typename T> T* GetExpression(uint32_t id);
    Expression* GetExpression(uint32_t id);
    template <typename T> T* GetCustomTypeDef(uint32_t id);
    CustomTypeDef* GetCustomTypeDef(uint32_t id);

    template <typename T, typename FBT> void Config(const FBT* buffer, T& obj);

    void Run(const PackageFormat::CHIRPackage* package);
    explicit CHIRDeserializerImpl(CHIRBuilder& chirBuilder, bool compilePlatform = false)
        : builder(chirBuilder), compilePlatform(compilePlatform){};

private:
    Cangjie::CHIR::CHIRBuilder& builder;
    bool compilePlatform = false;
    const PackageFormat::CHIRPackage* pool{};

    // Package object maps
    std::unordered_map<uint32_t, Type*> id2Type{{0, nullptr}};
    std::unordered_map<uint32_t, Value*> id2Value{{0, nullptr}};
    std::unordered_map<uint32_t, Expression*> id2Expression{{0, nullptr}};
    std::unordered_map<uint32_t, CustomTypeDef*> id2CustomTypeDef{{0, nullptr}};

    // lazy GenericType config
    std::vector<std::pair<GenericType*, const PackageFormat::GenericType*>> genericTypeConfig;

    void ResetImportedValuesUnderPackage();
    void ResetImportedDefsUnderPackage();
};
}
#endif
