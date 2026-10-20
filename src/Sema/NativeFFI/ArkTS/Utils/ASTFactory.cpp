// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "ASTFactory.h"
#include "../../Utils.h"
#include "../ArkTSInterop.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Parse/Parser.h"

using namespace Cangjie;
using namespace Cangjie::AST;
namespace Cangjie::Native::FFI::ArkTS {
// Before: @ArkTS public func run(): Unit { ... } is an ordinary source entry.
// After: a callback source template saves/restores the JSContext around a call
// to that entry, plus a variable registering the callback with JSModule.
// This function returns text only; CreateEntry binds the original declaration
// and exported name after parsing. The source entry itself remains unchanged.
// Source example:
//   import arkts.lang.*
//   @ArkTS public func run(): Unit { println(42) }
// Generated declarations (SDK names are supplied by the source imports):
//   func __cj_arkts_entry_0(__cj_arkts_ctx: JSContext,
//       _: JSCallInfo): JSValue {
//       let previous = __cj_arkts_enter(__cj_arkts_ctx)
//       try { run(); return __cj_arkts_ctx.undefined().toJSValue() }
//       finally { __cj_arkts_restore(previous) }
//   }
//   let __cj_arkts_registration_0 = JSModule.registerFunc(
//       "run", __cj_arkts_entry_0)
// Generated CJ pseudocode (prefix/suffix are compiler-owned names):
//   func entry(ctx: JSContext, _: JSCallInfo): JSValue {
//       previous = enter(ctx)
//       try { original(); return ctx.undefined().toJSValue() }
//       finally { restore(previous) }
//   }
//   let registration = JSModule.registerFunc("<actual entry name>", entry)
// Parse the skeleton first; rebind original's RefExpr and the registration
// string literal to the source declaration. User text never becomes CJ syntax.
std::string ArkTSEntryWrapperSource(const std::string& suffix, const std::string& prefix)
{
    return "func " + prefix + "entry_" + suffix + "(\n    " + prefix +
        "ctx: JSContext,\n    _: JSCallInfo\n): JSValue {\n    let " + prefix + "previous = __cj_arkts_enter(" +
        prefix + "ctx)\n    try {\n        " + prefix + "original()\n        return " + prefix +
        "ctx.undefined().toJSValue()\n    } finally {\n        " + "__cj_arkts_restore(" + prefix +
        "previous)\n    }\n}\n\nlet " + prefix + "registration_" + suffix +
        " = JSModule.registerFunc(\"__entry_name__\", " + prefix + "entry_" + suffix + ")\n";
}
} // namespace Cangjie::Native::FFI::ArkTS

// Before AST: File.decls = [FuncDecl(run, original body), ...].
// After result: [FuncDecl(callback, context guard + call(run)),
//                VarDecl(registration, JSModule.registerFunc("run", callback))].
// The returned nodes are not yet appended to File.decls. Their curFile/ranges
// refer to run's source file, call(run).ref.target is run, and both roots carry
// COMPILER_ADD. PrepareTypeCheck attaches and mangles them; run is not rewritten.
// AST construction pseudocode:
//   parse the compiler-owned skeleton; retain each declaration only after its
//   source positions and all File pointers refer to the permanent source file;
//   bind the original function and exported-name placeholder as AST values;
//   mark each generated root declaration so preparation can distinguish its
//   callback/registration pair from user declarations during a repeated pass.
// User identifiers and literal strings never become generated source syntax.
std::vector<OwnedPtr<Decl>> Native::FFI::ArkTS::ASTFactory::CreateEntry(
    FuncDecl& entry, const std::string& suffix, const std::string& prefix) const
{
    auto code = ArkTSEntryWrapperSource(suffix, prefix);
    Parser parser(code, diag, sourceManager, entry.begin);
    parser.SetCurFile(entry.curFile);
    auto generated = parser.ParseTopLevel();
    std::vector<OwnedPtr<Decl>> declarations;
    if (!generated) {
        return declarations;
    }
    for (auto& decl : generated->decls) {
        Walker(decl.get(), [&](Ptr<Node> node) {
            node->curFile = entry.curFile;
            node->begin = entry.begin;
            node->end = entry.end;
            if (auto literal = DynamicCast<LitConstExpr*>(node); literal && literal->stringValue == "__entry_name__") {
                literal->stringValue = entry.identifier.Val();
            }
            if (auto ref = DynamicCast<RefExpr*>(node); ref && ref->ref.identifier == prefix + "original") {
                ref->ref.identifier = entry.identifier;
                ref->ref.target = &entry;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        decl->EnableAttr(Attribute::COMPILER_ADD);
        declarations.emplace_back(Native::FFI::WithinFile(std::move(decl), entry.curFile));
    }
    return declarations;
}

// Before: the source FuncDecl has FuncBody.body == nullptr and code contains
// the compiler-owned temporary declaration `func __generated() { <glue> }`.
// After result: the detached Block(<glue>); the temporary FuncDecl is discarded.
// This function does not change the source signature or attach the block.
// LowerArkTSFunctions installs it, rebinds its nodes and collects its locals.
// A non-function, missing body or new parser error returns nullptr instead.
OwnedPtr<Block> Native::FFI::ArkTS::ASTFactory::CreateForeignBody(FuncDecl& function, const std::string& code) const
{
    const auto errorsBefore = diag.GetErrorCount();
    Parser parser(code, diag, sourceManager, function.begin);
    parser.SetCurFile(function.curFile);
    auto generated = parser.ParseDecl(ScopeKind::TOPLEVEL);
    auto wrapper = DynamicCast<FuncDecl*>(generated.get());
    if (!wrapper || !wrapper->funcBody || !wrapper->funcBody->body || diag.GetErrorCount() != errorsBefore) {
        return nullptr;
    }
    return std::move(wrapper->funcBody->body);
}
