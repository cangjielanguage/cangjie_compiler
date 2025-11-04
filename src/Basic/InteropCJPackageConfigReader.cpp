// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
* @file
*
* This document aims to parse PackageConfig.toml (interop CJ package configuration information),
* which primarily involves the symbols that the target language can expose in interoperability scenarios,
* as well as the specific type sets for generic instantiation.
*/

#include "cangjie/Basic/InteropCJPackageConfigReader.h"
#include <iostream>
#include <stdexcept>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#include <toml.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Cangjie {
using namespace toml;

InteropCJStrategy InteropCJPackageConfigReader::StringToStrategy(const std::string& str) const
{
    if (str == "Full")
        return InteropCJStrategy::FULL;
    if (str == "None")
        return InteropCJStrategy::NONE;
    return InteropCJStrategy::UNKNOWN;
}

InteropCJGenericStrategyType InteropCJPackageConfigReader::StringToGenericStrategy(const std::string& str) const
{
    if (str == "None")
        return InteropCJGenericStrategyType::NONE;
    if (str == "Partial")
        return InteropCJGenericStrategyType::PARTIAL;
    return InteropCJGenericStrategyType::UNKNOWN;
}

bool InteropCJPackageConfigReader::Parse(const std::string& filePath)
{
    try {
        toml::Table tbl = toml::parseFile(filePath).value.as<toml::Table>();
        // Analyze default configuration
        if (tbl.find("default") != tbl.end()) {
            auto isDefaultTable = tbl.find("default")->second.is<Table>();
            if (isDefaultTable) {
                auto defaultTable = tbl.find("default")->second.as<Table>();
                if (defaultTable["APIStrategy"].is<std::string>()) {
                    auto strategy = defaultTable["APIStrategy"].as<std::string>();
                    defaultApiStrategy = StringToStrategy(strategy);
                }
                if (defaultTable["GenericTypeStrategy"].is<std::string>()) {
                    auto strategy = defaultTable["GenericTypeStrategy"].as<std::string>();
                    defaultGenericTypeStrategy = StringToGenericStrategy(strategy);
                }
            }
        }

        // Parse package configuration
        if (tbl.find("package") != tbl.end()) {
            auto isPackageArray = tbl.find("package")->second.is<Array>();
            if (isPackageArray) {
                auto packageArray = tbl.find("package")->second.as<Array>();
                for (const auto& packageItem : packageArray) {
                    auto isPackageTable = packageItem.is<Table>();
                    if (isPackageTable) {
                        PackageConfig pkgConfig;
                        auto packageTable = packageItem.as<Table>();
                        // packageName
                        if (packageTable["name"].is<std::string>()) {
                            auto name = packageTable["name"].as<std::string>();
                            pkgConfig.name = name;
                        } else {
                            // There must be a package name.
                            continue;
                        }

                        // API Strategy
                        if (packageTable["APIStrategy"].is<std::string>()) {
                            auto strategy = packageTable["APIStrategy"].as<std::string>();
                            pkgConfig.apiStrategy = StringToStrategy(strategy);
                        } else {
                            pkgConfig.apiStrategy = defaultApiStrategy;
                        }

                        // Generic Strategy
                        if (packageTable["GenericTypeStrategy"].is<std::string>()) {
                            auto strategy = packageTable["GenericTypeStrategy"].as<std::string>();
                            pkgConfig.genericTypeStrategy = StringToGenericStrategy(strategy);
                        } else {
                            pkgConfig.genericTypeStrategy = defaultGenericTypeStrategy;
                        }

                        // List of Included APIs
                        if (packageTable.find("included_apis") != packageTable.end()) {
                            if (packageTable["included_apis"].is<Array>()) {
                                auto includedApis = packageTable["included_apis"].as<Array>();
                                for (const auto& item : includedApis) {
                                    if (item.is<std::string>()) {
                                        auto api = item.as<std::string>();
                                        pkgConfig.interopCJIncludedApis.push_back(api);
                                    }
                                }
                            }
                        }

                        // Excluded API List
                        if (packageTable.find("excluded_apis") != packageTable.end()) {
                            if (packageTable["excluded_apis"].is<Array>()) {
                                auto excludedApis = packageTable["excluded_apis"].as<Array>();
                                for (const auto& item : excludedApis) {
                                    if (item.is<std::string>()) {
                                        auto api = item.as<std::string>();
                                        pkgConfig.interopCJExcludedApis.push_back(api);
                                    }
                                }
                            }
                        }

                        // Generic instanceization
                        if (packageTable.find("generic_object_configuration") != packageTable.end()) {
                            if (packageTable["generic_object_configuration"].is<Array>()) {
                                auto allowedGenerics = packageTable["generic_object_configuration"].as<Array>();
                                // Temporary Storage Type Paramter List.
                                std::unordered_map<std::string, std::vector<std::string>> typeArgumentsMap;

                                // Collect all types of paramter lists.
                                for (const auto& item : allowedGenerics) {
                                    if (item.is<Table>()) {
                                        auto genTable = item.as<Table>();
                                        // Check the name field.
                                        if (genTable.find("name") != genTable.end() &&
                                            genTable["name"].is<std::string>()) {
                                            std::string name = genTable["name"].as<std::string>();

                                            // Check if it is a type parameter definition.
                                            if (genTable.find("type_arguments") != genTable.end() &&
                                                genTable["type_arguments"].is<Array>()) {
                                                auto typeArgs = genTable["type_arguments"].as<Array>();
                                                std::vector<std::string> types;

                                                for (const auto& type : typeArgs) {
                                                    if (type.is<std::string>()) {
                                                        std::string typeStr = type.as<std::string>();
                                                        types.push_back(typeStr);
                                                        GenericTypeArguments visibleFuncs;
                                                        pkgConfig.allowedInteropCJGenericInstantiations[name][typeStr] = visibleFuncs;
                                                    }
                                                }

                                                typeArgumentsMap[name] = types;
                                            }
                                        }
                                    }
                                }

                                // Processing Symbol Configuration.
                                for (const auto& item : allowedGenerics) {
                                    if (item.is<Table>()) {
                                        auto genTable = item.as<Table>();
                                        // Check the name field.
                                        if (genTable.find("name") != genTable.end() &&
                                            genTable["name"].is<std::string>()) {
                                            std::string name = genTable["name"].as<std::string>();

                                            // Check if it is a symbol configuration (including angle brackets).
                                            size_t pos = name.find('<');
                                            if (pos != std::string::npos && name.back() == '>') {
                                                // Extract generic type names and types parameters.
                                                std::string outerType = name.substr(0, pos);
                                                std::string innerType = name.substr(pos + 1, name.size() - pos - 2);

                                                // Check if there is a corresponding type parameter definition.
                                                if (typeArgumentsMap.find(outerType) != typeArgumentsMap.end()) {
                                                    // Check whether the parameter of this type is in the allowed list.
                                                    const auto& allowedTypes = typeArgumentsMap[outerType];
                                                    if (std::find(allowedTypes.begin(), allowedTypes.end(),
                                                        innerType) != allowedTypes.end()) {
                                                        // Analysis of Symbol Set
                                                        if (genTable.find("symbols") != genTable.end() &&
                                                            genTable["symbols"].is<Array>()) {
                                                            GenericTypeArguments typeArgs;
                                                            auto symbolsArray = genTable["symbols"].as<Array>();
                                                            for (const auto& symbol : symbolsArray) {
                                                                if (symbol.is<std::string>()) {
                                                                    typeArgs.symbols.insert(symbol.as<std::string>());
                                                                }
                                                            }
                                                            pkgConfig.allowedInteropCJGenericInstantiations[outerType]
                                                                                                           [innerType] =
                                                                typeArgs;
                                                        }
                                                    }
                                                } // Handling Symbol Configuration for Non-Generic Classes.
                                            } else if (genTable.find("symbols") != genTable.end() &&
                                                genTable["symbols"].is<Array>()) {
                                                // Non-generic classes use an empty string as the internal type.
                                                GenericTypeArguments typeArgs;
                                                auto symbolsArray = genTable["symbols"].as<Array>();

                                                for (const auto& symbol : symbolsArray) {
                                                    if (symbol.is<std::string>()) {
                                                        typeArgs.symbols.insert(symbol.as<std::string>());
                                                    }
                                                }
                                                // Non-generic classes use "" as he built-in key.
                                                pkgConfig.allowedInteropCJGenericInstantiations[name][""] = typeArgs;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        // Add to Configuration
                        packages[pkgConfig.name] = pkgConfig;
                    }
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config: " << e.what() << std::endl;
        return false;
    }
}

std::optional<PackageConfig> InteropCJPackageConfigReader::GetPackage(const std::string& name) const
{
    auto it = packages.find(name);
    if (it != packages.end()) {
        return it->second;
    }
    return std::nullopt;
}

InteropCJStrategy InteropCJPackageConfigReader::GetApiStrategy(const std::string& packageName) const
{
    if (auto pkg = GetPackage(packageName)) {
        return pkg->apiStrategy;
    }
    return defaultApiStrategy;
}

InteropCJGenericStrategyType InteropCJPackageConfigReader::GetGenericTypeStrategy(const std::string& packageName) const
{
    if (auto pkg = GetPackage(packageName)) {
        return pkg->genericTypeStrategy;
    }
    return defaultGenericTypeStrategy;
}

bool InteropCJPackageConfigReader::Validate() const
{
    // Verifying Default Policies
    if (defaultApiStrategy == InteropCJStrategy::UNKNOWN) {
        std::cerr << "Validation failed: Default API strategy is unknown" << std::endl;
        return false;
    }
    if (defaultGenericTypeStrategy == InteropCJGenericStrategyType::UNKNOWN) {
        std::cerr << "Validation failed: Default generic type  strategy is unknown" << std::endl;
        return false;
    }

    // Verify each package
    for (const auto& [name, pkg] : packages) {
        // Verify policy value
        if (pkg.apiStrategy == InteropCJStrategy::UNKNOWN) {
            std::cerr << "Validation failed: '" << name << "' API strategy is unknown" << std::endl;
            return false;
        }

        if (pkg.genericTypeStrategy == InteropCJGenericStrategyType::UNKNOWN) {
            std::cerr << "Validation failed: '" << name << "' generic type strategy is unknown" << std::endl;
            return false;
        }

        // Verify the consistency between the validation strategy and the API list.
        if (pkg.apiStrategy == InteropCJStrategy::FULL && !pkg.interopCJIncludedApis.empty()) {
            std::cerr << "Validation failed for package '" << name
                      << "': API strategy is Full but IncludedApis is Configured " << std::endl;
            return false;
        }

        if (pkg.apiStrategy == InteropCJStrategy::NONE && !pkg.interopCJExcludedApis.empty()) {
            std::cerr << "Validation failed for package '" << name
                      << "': API strategy is None but ExcludedApis is Configured " << std::endl;
            return false;
        }

        if (!pkg.interopCJIncludedApis.empty() && !pkg.interopCJExcludedApis.empty()) {
            std::cerr << "Validation failed for package '" << name << "': Cannot hava both included and excluded APIs"
                      << std::endl;
            return false;
        }

        // Verify Generic Strategy Consistency
        if (pkg.genericTypeStrategy == InteropCJGenericStrategyType::NONE &&
            !pkg.allowedInteropCJGenericInstantiations.empty()) {
            // The "None" strategy does not allow for generic configurations.
            std::cerr << "Validation failed for package '" << name
                      << "': None generic strategy cannot hava generic instantiations" << std::endl;
            return false;
        }
    }
    return true;
}
} // namespace Cangjie