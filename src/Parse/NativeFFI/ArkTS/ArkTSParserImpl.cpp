// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "ArkTSParserImpl.h"

#include <algorithm>
#include <unordered_set>

#include "../../ParserImpl.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace Cangjie::AST;

namespace {
// Before: parsed annotation nodes. After result: whether an ARKTS kind exists;
// this selects modifier preparation without adding or changing annotation nodes.
bool HasArkTSAnnotation(const PtrVector<Annotation>& annotations)
{
    return std::any_of(annotations.begin(), annotations.end(),
        [](const auto& annotation) { return annotation->kind == AnnotationKind::ARKTS; });
}

} // namespace

// Before -> after AST: @ArkTS argument nodes remain exactly as parsed.
// Repeated option names only add a diagnostic; no layers are merged here.
// Each source annotation is one option layer. Inherited block annotations are
// checked before cloning; declaration annotations are checked independently.
void ArkTSParserImpl::CheckAnnotation(const Annotation& annotation) const
{
    std::unordered_set<std::string> seen;
    for (const auto& argument : annotation.args) {
        const auto name = argument->name.Val();
        if (!seen.emplace(name).second) {
            auto diagnostic = p.diag.DiagnoseRefactor(DiagKindRefactor::parse_duplicated_item,
                *argument, "annotation option", name, " in '@ArkTS'");
            diagnostic.AddMainHintArguments("annotation option");
        }
    }
}

// Before: top-level @ArkTS foreign func has FOREIGN in the source modifiers.
// After: return a checking-only copy without FOREIGN and mark FuncDecl FOREIGN;
// ParseFuncDecl keeps the original source modifier on the AST for body parsing.
// This postpones C ABI checks until PrepareTypeCheck can classify a CJ wrapper.
// Other functions return the modifiers unchanged; no glue body is created here.
std::set<Modifier> ArkTSParserImpl::PrepareForeignFunctionModifiers(ScopeKind scopeKind,
    const std::set<Modifier>& modifiers, const PtrVector<Annotation>& annotations, FuncDecl& function) const
{
    // Preserve ordinary C foreign checks. ArkTS foreign will become an
    // ordinary CJ wrapper in NativeFFI before C ABI/type prechecking.
    auto checked = modifiers;
    if (scopeKind == ScopeKind::TOPLEVEL && p.HasModifier(modifiers, TokenKind::FOREIGN) &&
        HasArkTSAnnotation(annotations)) {
        Utils::EraseIf(checked, [](const auto& modifier) { return modifier.modifier == TokenKind::FOREIGN; });
        function.EnableAttr(Attribute::FOREIGN);
    }
    return checked;
}
