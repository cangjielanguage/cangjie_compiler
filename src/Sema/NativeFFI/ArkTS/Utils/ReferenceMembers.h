// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_PRIVATE_SEMA_ARKTS_UTILS_REFERENCE_MEMBERS_H
#define CANGJIE_PRIVATE_SEMA_ARKTS_UTILS_REFERENCE_MEMBERS_H

#include "cangjie/AST/Node.h"

namespace Cangjie {
class DiagnosticEngine;
class ImportManager;
class SourceManager;
namespace Native::FFI::ArkTS {
// Generated member names shared by class and interface wrappers. A member
// spelling one of these prefixes is reserved for compiler-owned storage.
inline constexpr const char* ARKTS_RESERVED_PREFIX = "__cj_arkts_";
// The type text of one wrapper reference holder: the default JSObject carrier
// of an ordinary wrapper, or the JSValue carrier of the ArkTSString preset (a
// JS string is a primitive, so the holder stores the JSValue handle).
inline constexpr const char* ARKTS_JS_OBJECT = "JSObject";
inline constexpr const char* ARKTS_JS_VALUE_CARRIER = "JSValue";
inline constexpr const char* ARKTS_SUPPORT_HOLDER = "__cj_arkts_support";

// The imported ohos.ark_interop.JSObject declaration, or null when the file
// does not import it. The wrapper members need this declaration to type the
// stored reference.
Ptr<AST::Decl> FindArkTSJSObject(ImportManager& imports, const AST::File& file);

// The same lookup for the JSValue carrier of the ArkTSString preset.
Ptr<AST::Decl> FindArkTSJSValue(ImportManager& imports, const AST::File& file);

// Parse one compiler-owned support holder at the anchor's position, bind its
// JSObject references to the imported declaration, and return the parsed
// top-level declaration. Returns null when parsing fails or the file does not
// import JSObject. The caller moves the members it needs out of the holder;
// classes take the whole body, interfaces take the single accessor.
OwnedPtr<AST::Decl> ParseArkTSReferenceHolder(const std::string& code, const AST::Decl& anchor,
    DiagnosticEngine& diag, SourceManager& sourceManager, ImportManager& imports);

// Declare the reference storage on a wrapper class, parsed from a
// compiler-owned holder shape and reparented onto the class. A root wrapper
// (wrapperParent=false) gets the private carrier field, both wrapping
// constructors and the accessor; a child wrapper (wrapperParent=true) only
// redeclares both wrapping constructors as super(...) delegations, because
// constructors are not inherited. The carrier is ARKTS_JS_OBJECT by default;
// the ArkTSString preset passes ARKTS_JS_VALUE_CARRIER (its stored value is a
// JSValue primitive). Source positions and the permanent file are the class's.
// Returns false when the holder cannot be built or the carrier type is not
// imported.
bool InsertArkTSReferenceMembers(AST::ClassDecl& cd, bool wrapperParent, DiagnosticEngine& diag,
    SourceManager& sourceManager, ImportManager& imports,
    const std::string& carrierType = ARKTS_JS_OBJECT);
} // namespace Native::FFI::ArkTS
} // namespace Cangjie
#endif
