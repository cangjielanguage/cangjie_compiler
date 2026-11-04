// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file defines the version of the serialized CJO format.
 *
 * The CJO version protects the serialized contract between a CJO producer and a
 * CJO reader. The contract includes the serialized layout and any serialized
 * meaning that a reader must understand in order to load declarations safely.
 * Keep the version change in the same change as the format change. A format
 * change without the corresponding version change can make an incompatible
 * CJO appear compatible to an older compiler.
 *
 * Version components are bumped according to the following policy:
 *
 * - Major:
 *   - Bump for a serialized change that is incompatible in both directions.
 *   - The current LTS line does not allow major-version changes.
 *   - The major component must match exactly when reading a CJO file.
 *
 * - Minor:
 *   - Bump for a reviewed serialized change that is backward-compatible for
 *     newer readers but not forward-compatible for older readers.
 *   - An older minor from the same major is accepted; a newer minor is rejected.
 *   - Reset the patch component to zero.
 *
 * - Patch:
 *   - Bump for a CJO schema or serialization-algorithm change that does not
 *     require a major or minor bump.
 *   - The patch component is not checked by the current compatibility gate.
 *
 * During the current LTS line, CJO files are required to remain backward
 * compatible: newer compilers must be able to process older CJO files. A
 * non-forward-compatible change may be introduced only after review and
 * registration, and it requires a minor bump. Every change to the CJO schema
 * or to the CJO serialization algorithm requires at least a patch bump.
 *
 * Detailed bump rules for schema changes:
 *
 * - table:
 *   - Insert, delete, reorder, or change the type of a field:
 *     major++, minor = 0, patch = 0.
 *   - Change a default so that a new reader cannot construct the old value:
 *     major++, minor = 0, patch = 0.
 *   - Change a default when a new reader can branch on the CJO version, but
 *     old readers would change compilation behavior: minor++, patch = 0.
 *   - Change field semantics so that a new reader cannot construct the old
 *     value: major++, minor = 0, patch = 0.
 *   - Change field semantics when a new reader can branch on the CJO version,
 *     but old readers cannot process the new semantics: minor++, patch = 0.
 *   - Append a field when a new reader cannot construct a valid object without
 *     reading it: major++, minor = 0, patch = 0.
 *   - Append a field with forward and backward compatibility: patch++.
 *   - Deprecate a field, stop writing it, and use versioning to read the new or
 *     old field when old readers cannot handle its absence: minor++, patch = 0.
 *   - Deprecate a field while continuing to write it: patch++.
 *   - Add a table type: patch++.
 *   - Rename a field only: patch++.
 *
 * - struct:
 *   - Insert, delete, reorder, or change the type of a field:
 *     major++, minor = 0, patch = 0.
 *   - Change field semantics so that a new reader cannot construct the old
 *     value: major++, minor = 0, patch = 0.
 *   - Change field semantics when a new reader can branch on the CJO version,
 *     but old readers cannot process the new semantics: minor++, patch = 0.
 *   - Add a struct type: patch++.
 *   - Rename a field only: patch++.
 *
 * - enum:
 *   - Insert, delete, reorder, or change an enum value when existing CJO files
 *     have no explicit value: major++, minor = 0, patch = 0.
 *   - Change enum-value semantics so that a new reader cannot construct the old
 *     semantics: major++, minor = 0, patch = 0.
 *   - Change enum-value semantics when a new reader can branch on the CJO
 *     version, but old readers cannot process the new semantics:
 *     minor++, patch = 0.
 *   - Append a member when a new reader cannot handle its absence:
 *     major++, minor = 0, patch = 0.
 *   - Append a member when a new reader can branch around its absence, but old
 *     readers cannot handle the new value; use the compatibility allowlist:
 *     minor++, patch = 0.
 *   - Append a member when old readers handle the new value and new readers
 *     handle its absence; use the compatibility allowlist: patch++.
 *   - Add an enum type: patch++.
 *   - Rename a field only: patch++.
 *
 * - union:
 *   - Insert, delete, or reorder a member when existing CJO files have no
 *     discriminant: major++, minor = 0, patch = 0.
 *   - Change field semantics so that a new reader cannot construct the old
 *     semantics: major++, minor = 0, patch = 0.
 *   - Change field semantics when a new reader can branch on the CJO version,
 *     but old readers cannot process the new semantics: minor++, patch = 0.
 *   - Append a member when a new reader cannot handle its absence:
 *     major++, minor = 0, patch = 0.
 *   - Append a member when a new reader can branch around its absence, but old
 *     readers cannot handle the new value: minor++, patch = 0.
 *   - Append a member when old readers handle the new value and new readers
 *     handle its absence; all current CJO union fields must be compatible:
 *     patch++.
 *   - Add a union type: patch++.
 *   - Rename a field only: patch++.
 *
 * - vector/string/scalar:
 *   - Change the element type or scalar field width:
 *     major++, minor = 0, patch = 0.
 *   - Change a scalar default so that a new reader cannot construct the old
 *     value: major++, minor = 0, patch = 0.
 *   - Change a scalar default when a new reader can branch on the CJO version,
 *     but old readers cannot process the new semantics: minor++, patch = 0.
 *   - Change field semantics so that a new reader cannot construct the old
 *     semantics: major++, minor = 0, patch = 0.
 *   - Change field semantics when a new reader can branch on the CJO version,
 *     but old readers cannot process the new semantics: minor++, patch = 0.
 *
 * - other:
 *   - Change the file identifier: major++, minor = 0, patch = 0.
 *   - Change the root type: major++, minor = 0, patch = 0.
 *   - Rename the namespace only: patch++.
 *
 * When several rules apply to one change, use the version update required by
 * the most restrictive rule.
 *
 * Enum additions require special care. Appending a member is patch-compatible
 * only for enums whose readers are explicitly known to tolerate unknown
 * values. The current patch-compatible allowlist is TypeKind, DeclKind,
 * ExprKind, and PackageKind. Appended members of AnnoKind, OptimizationLevel,
 * PatternKind, AccessLevel, AccessModifier, OverflowPolicy, OperatorKind,
 * CallKind, LitConstKind, StringKind, ForInKind, and BuiltInType require a
 * minor bump because an older reader may fail when it encounters the new
 * value.
 *
 * Major and minor bumps must be reviewed during development. They must not be
 * introduced as ordinary fixes during the LTS line or after release
 * preparation. A major bump resets minor and patch to zero. A minor bump
 * resets patch to zero. Record the compatibility impact and any required
 * recompilation in the release notes.
 */
#ifndef CANGJIE_MODULES_CJO_VERSION_H
#define CANGJIE_MODULES_CJO_VERSION_H

#include <cstdint>

namespace Cangjie {
constexpr uint8_t CJO_MAJOR_VERSION = 0;
constexpr uint8_t CJO_MINOR_VERSION = 1;
constexpr uint8_t CJO_PATCH_VERSION = 1;
// Last modified: 'isImplicitAdd' added to 'table ImportSpec'
} // namespace Cangjie

#endif // CANGJIE_MODULES_CJO_VERSION_H
