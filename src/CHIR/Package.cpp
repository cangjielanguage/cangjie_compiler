// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/ToStringUtils.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/Type/EnumDef.h"
#include "cangjie/CHIR/Type/ExtendDef.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/CHIR/Value.h"

using namespace Cangjie::CHIR;

Package::Package(const std::string& name) : name(name)
{
}

std::string Package::GetName() const
{
    return name;
}

void Package::AddGlobalVar(GlobalVar* item)
{
    globalVars.emplace_back(item);
}

void Package::AddGlobalFunc(Func* item)
{
    globalFuncs.emplace_back(item);
}

void Package::AddClass(ClassDef* item)
{
    classes.emplace_back(item);
}

std::vector<ClassDef*> Package::GetClasses() const
{
    return classes;
}

void Package::SetClasses(std::vector<ClassDef*>&& items)
{
    classes = std::move(items);
}

std::vector<EnumDef*> Package::GetEnums() const
{
    return enums;
}

void Package::SetPackageInitFunc(Func* func)
{
    packageInitFunc = func;
}

void Package::SetPackageLiteralInitFunc(Func* func)
{
    packageLiteralInitFunc = func;
}

Func* Package::GetPackageLiteralInitFunc() const
{
    return packageLiteralInitFunc;
}

void Package::SetImportedVarAndFuncs(std::vector<ImportedValue*>&& items)
{
    importedVarAndFuncs = std::move(items);
}

Func* Package::GetPackageInitFunc() const
{
    return packageInitFunc;
}

void Package::SetImportedExtends(std::vector<ExtendDef*>&& items)
{
    importedExtends = std::move(items);
}

void Package::SetExtends(std::vector<ExtendDef*>&& items)
{
    extends = std::move(items);
}

void Package::AddImportedClass(ClassDef* item)
{
    importedClasses.emplace_back(item);
}

void Package::AddImportedExtend(ExtendDef* item)
{
    importedExtends.emplace_back(item);
}

std::vector<EnumDef*> Package::GetImportedEnums() const
{
    return importedEnums;
}

void Package::SetPackageAccessLevel(const AccessLevel& level)
{
    pkgAccessLevel = level;
}

void Package::SetImportedStructs(std::vector<StructDef*>&& s)
{
    importedStructs = std::move(s);
}

void Package::SetStructs(std::vector<StructDef*>&& s)
{
    structs = std::move(s);
}

void Package::SetImportedClasses(std::vector<ClassDef*>&& s)
{
    importedClasses = std::move(s);
}

void Package::SetImportedEnums(std::vector<EnumDef*>&& s)
{
    importedEnums = std::move(s);
}

void Package::SetEnums(std::vector<EnumDef*>&& s)
{
    enums = std::move(s);
}

Package::AccessLevel Package::GetPackageAccessLevel() const
{
    return pkgAccessLevel;
}

std::vector<ExtendDef*> Package::GetImportedExtends() const
{
    return importedExtends;
}

void Package::AddImportedEnum(EnumDef* item)
{
    importedEnums.emplace_back(item);
}

std::vector<ClassDef*> Package::GetImportedClasses() const
{
    return importedClasses;
}

std::vector<StructDef*> Package::GetImportedStructs() const
{
    return importedStructs;
}

void Package::AddImportedStruct(StructDef* item)
{
    importedStructs.emplace_back(item);
}
    
std::vector<ImportedValue*> Package::GetImportedVarAndFuncs() const
{
    return importedVarAndFuncs;
}

std::vector<ExtendDef*> Package::GetExtends() const
{
    return extends;
}

void Package::AddExtend(ExtendDef* item)
{
    extends.emplace_back(item);
}

void Package::AddEnum(EnumDef* item)
{
    enums.emplace_back(item);
}

std::vector<StructDef*> Package::GetStructs() const
{
    return structs;
}

void Package::AddStruct(StructDef* item)
{
    structs.emplace_back(item);
}

void Package::SetGlobalFuncs(const std::vector<Func*>& funcs)
{
    globalFuncs = funcs;
}

void Package::SetGlobalVars(std::vector<GlobalVar*>&& vars)
{
    std::swap(globalVars, vars);
}

std::vector<Func*> Package::GetGlobalFuncs() const
{
    return globalFuncs;
}

std::vector<GlobalVar*> Package::GetGlobalVars() const
{
    return globalVars;
}

std::string Package::ToString() const
{
    std::stringstream ss;
    ss << "package: " << name << "\n";
    ss << "packageAccessLevel: " << PackageAccessLevelToString(pkgAccessLevel) << "\n";
    ss << "packageInitFunc: " << GetPackageInitFunc()->GetIdentifier() << "\n";
    ss << "\n==========================imports===============================\n";
    for (auto& it : importedVarAndFuncs) {
        ss << GetImportedValueStr(*it) << "\n";
    }
    ss << "\n\n";
    for (auto& it : importedStructs) {
        ss << it->ToString() << "\n\n";
    }
    for (auto& it : importedClasses) {
        ss << it->ToString() << "\n\n";
    }
    for (auto& it : importedEnums) {
        ss << it->ToString() << "\n\n";
    }
    for (auto& it : importedExtends) {
        ss << it->ToString() << "\n\n";
    }
    ss << "\n===========================vars=================================\n";
    for (auto& it : globalVars) {
        ss << it->ToString() << "\n";
    }
    ss << "\n==========================types=================================\n";
    for (auto& it : structs) {
        ss << it->ToString() << "\n\n";
    }
    for (auto& it : classes) {
        ss << it->ToString() << "\n\n";
    }
    for (auto& it : enums) {
        ss << it->ToString() << "\n\n";
    }
    for (auto& it : extends) {
        ss << it->ToString() << "\n\n";
    }
    ss << "\n==========================funcs=================================\n";
    for (auto& it : GetGlobalFuncs()) {
        ss << GetFuncStr(*it);
        ss << "\n\n";
    }
    return ss.str();
}

void Package::AddImportedVarAndFunc(ImportedValue* item)
{
    importedVarAndFuncs.emplace_back(item);
}

void Package::Dump() const
{
    std::cout << ToString() << std::endl;
}

std::vector<CustomTypeDef*> Package::GetAllCustomTypeDef() const
{
    std::vector<CustomTypeDef*> all;

    all.insert(all.end(), importedStructs.begin(), importedStructs.end());
    all.insert(all.end(), importedClasses.begin(), importedClasses.end());
    all.insert(all.end(), importedEnums.begin(), importedEnums.end());
    all.insert(all.end(), importedExtends.begin(), importedExtends.end());

    all.insert(all.end(), structs.begin(), structs.end());
    all.insert(all.end(), classes.begin(), classes.end());
    all.insert(all.end(), enums.begin(), enums.end());
    all.insert(all.end(), extends.begin(), extends.end());

    return all;
}

std::vector<CustomTypeDef*> Package::GetAllImportedCustomTypeDef() const
{
    std::vector<CustomTypeDef*> all;

    all.insert(all.end(), importedStructs.begin(), importedStructs.end());
    all.insert(all.end(), importedClasses.begin(), importedClasses.end());
    all.insert(all.end(), importedEnums.begin(), importedEnums.end());
    all.insert(all.end(), importedExtends.begin(), importedExtends.end());

    return all;
}

std::vector<CustomTypeDef*> Package::GetCurPkgCustomTypeDef() const
{
    std::vector<CustomTypeDef*> all;
 
    all.insert(all.end(), structs.begin(), structs.end());
    all.insert(all.end(), classes.begin(), classes.end());
    all.insert(all.end(), enums.begin(), enums.end());
    all.insert(all.end(), extends.begin(), extends.end());
 
    return all;
}

std::vector<StructDef*> Package::GetAllStructDef() const
{
    std::vector<StructDef*> all;
    all.insert(all.end(), structs.begin(), structs.end());
    all.insert(all.end(), importedStructs.begin(), importedStructs.end());

    return all;
}

std::vector<EnumDef*> Package::GetAllEnumDef() const
{
    std::vector<EnumDef*> all;
    all.insert(all.end(), enums.begin(), enums.end());
    all.insert(all.end(), importedEnums.begin(), importedEnums.end());

    return all;
}

std::vector<ClassDef*> Package::GetAllClassDef() const
{
    std::vector<ClassDef*> all;
    all.insert(all.end(), classes.begin(), classes.end());
    all.insert(all.end(), importedClasses.begin(), importedClasses.end());

    return all;
}

std::vector<ExtendDef*> Package::GetAllExtendDef() const
{
    std::vector<ExtendDef*> all;
    all.insert(all.end(), extends.begin(), extends.end());
    all.insert(all.end(), importedExtends.begin(), importedExtends.end());

    return all;
}