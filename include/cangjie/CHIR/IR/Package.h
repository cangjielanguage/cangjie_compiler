// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_PACKAGE_H
#define CANGJIE_CHIR_PACKAGE_H

#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/EnumDef.h"
#include "cangjie/CHIR/IR/Type/ExtendDef.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"

#include <string>
#include <vector>

namespace Cangjie::CHIR {
class Package {
    explicit Package(const std::string& name);
    ~Package() = default;
    friend class CHIRContext;
    friend class CHIRBuilder;

public:
    enum class AccessLevel : uint8_t {
        INVALID = 0,
        INTERNAL,
        PROTECTED,
        PUBLIC
    };
    // ===--------------------------------------------------------------------===//
    // Base Infomation
    // ===--------------------------------------------------------------------===//
    void Dump() const;
    std::string ToString() const;
    std::string GetName() const;
    AccessLevel GetPackageAccessLevel() const;
    void SetPackageAccessLevel(const AccessLevel& level);

    // ===--------------------------------------------------------------------===//
    // Global Var API
    // ===--------------------------------------------------------------------===//
    void AddGlobalVar(GlobalVar* item);
    std::vector<GlobalVar*> GetGlobalVars() const;
    void SetGlobalVars(std::vector<GlobalVar*>&& vars);

    // ===--------------------------------------------------------------------===//
    // Global Function API
    // ===--------------------------------------------------------------------===//
    void AddGlobalFunc(Func* item);
    std::vector<Func*> GetGlobalFuncs() const;
    void SetGlobalFuncs(const std::vector<Func*>& funcs);

    Func* GetPackageInitFunc() const;
    void SetPackageInitFunc(Func* func);

    void SetPackageLiteralInitFunc(Func* func);
    Func* GetPackageLiteralInitFunc() const;
    // ===--------------------------------------------------------------------===//
    // Imported Var and Function API
    // ===--------------------------------------------------------------------===//
    void AddImportedVarAndFunc(ImportedValue* item);
    std::vector<ImportedValue*> GetImportedVarAndFuncs() const;
    void SetImportedVarAndFuncs(std::vector<ImportedValue*>&& items);

    // ===--------------------------------------------------------------------===//
    // StructDef API
    // ===--------------------------------------------------------------------===//
    void AddStruct(StructDef* item);
    std::vector<StructDef*> GetStructs() const;
    void SetStructs(std::vector<StructDef*>&& s);

    void AddImportedStruct(StructDef* item);
    std::vector<StructDef*> GetImportedStructs() const;
    void SetImportedStructs(std::vector<StructDef*>&& s);

    std::vector<StructDef*> GetAllStructDef() const;

    // ===--------------------------------------------------------------------===//
    // ClassDef API
    // ===--------------------------------------------------------------------===//
    void AddClass(ClassDef* item);
    std::vector<ClassDef*> GetClasses() const;
    void SetClasses(std::vector<ClassDef*>&& items);

    void AddImportedClass(ClassDef* item);
    std::vector<ClassDef*> GetImportedClasses() const;
    void SetImportedClasses(std::vector<ClassDef*>&& s);

    std::vector<ClassDef*> GetAllClassDef() const;

    // ===--------------------------------------------------------------------===//
    // EnumDef API
    // ===--------------------------------------------------------------------===//
    void AddEnum(EnumDef* item);
    std::vector<EnumDef*> GetEnums() const;
    void SetEnums(std::vector<EnumDef*>&& s);

    void AddImportedEnum(EnumDef* item);
    std::vector<EnumDef*> GetImportedEnums() const;
    void SetImportedEnums(std::vector<EnumDef*>&& s);

    std::vector<EnumDef*> GetAllEnumDef() const;

    // ===--------------------------------------------------------------------===//
    // ExtendDef API
    // ===--------------------------------------------------------------------===//
    void AddExtend(ExtendDef* item);
    std::vector<ExtendDef*> GetExtends() const;
    void SetExtends(std::vector<ExtendDef*>&& items);

    void AddImportedExtend(ExtendDef* item);
    std::vector<ExtendDef*> GetImportedExtends() const;
    void SetImportedExtends(std::vector<ExtendDef*>&& items);

    std::vector<ExtendDef*> GetAllExtendDef() const;

    // ===--------------------------------------------------------------------===//
    // Others API
    // ===--------------------------------------------------------------------===//
    std::vector<CustomTypeDef*> GetAllCustomTypeDef() const;
    std::vector<CustomTypeDef*> GetCurPkgCustomTypeDef() const;
    std::vector<CustomTypeDef*> GetAllImportedCustomTypeDef() const;
private:
    std::string name;                                  // full package name, like "std.core"
    AccessLevel pkgAccessLevel{AccessLevel::INVALID};  // public/internal/protected, get from AST

    // imported decls
    std::vector<ImportedValue*> importedVarAndFuncs;
    std::vector<StructDef*> importedStructs;
    std::vector<ClassDef*> importedClasses;
    std::vector<EnumDef*> importedEnums;
    std::vector<ExtendDef*> importedExtends;

    // decls in current package
    std::vector<GlobalVar*> globalVars;
    std::vector<Func*> globalFuncs;
    std::vector<StructDef*> structs;
    std::vector<ClassDef*> classes;
    std::vector<EnumDef*> enums;
    std::vector<ExtendDef*> extends;
    Func* packageInitFunc = nullptr;
    Func* packageLiteralInitFunc = nullptr; // global literals init function in one package
};

} // namespace Cangjie::CHIR

#endif
