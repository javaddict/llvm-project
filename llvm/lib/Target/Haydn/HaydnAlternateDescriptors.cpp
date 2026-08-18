//===- HaydnAlternateDescriptors.cpp - Alt descriptor side-map --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Anchors HaydnAlternateDescriptors.h and hosts residual Format E placement
// mask helpers declared in HaydnPlacementAlternative.h (E2-only and E3-only
// golden Mode clamps for residualAltCompatibleFormatMask).
//
// The E2-only / E3-only admission truth is the generated
// FormatEE2OnlyNameSet / FormatEE3OnlyNameSet (HaydnGenFormatERecords.inc,
// GET_FORMAT_E_MODE_ONLY_NAMES) — never a hand-transcribed name switch with
// count-only pins and a silent ProductFormatMask fallback on golden drift.
// One mechanism: peel the residual/member spelling to the golden logical
// (peelLogicalOpcodeName, StripWide=false), then classify against the
// generated sets.
//
//===----------------------------------------------------------------------===//

#include "HaydnAlternateDescriptors.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnFormatERecords.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/StringRef.h"
#include <algorithm>
#include <cassert>
#include <cstdint>

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

/// Golden E2-only / E3-only logical-name sets, imported from the generated
/// records (one TU-local copy; same include-section pattern as
/// product_size_detail in HaydnBundlePlan.h).
namespace mode_only_detail {
#define GET_FORMAT_E_MODE_ONLY_NAMES
#include "HaydnGenFormatERecords.inc"
} // namespace mode_only_detail

/// Mode-only admission class of a golden logical name.
enum class ModeOnly : uint8_t { Neither = 0, E2Only = 1, E3Only = 2 };

/// Binary search one generated sorted name set. Unknown names are Neither —
/// they stay unconstrained so residual names outside the golden catalog
/// never false-reject.
bool sortedNameSetContains(const char *const *Set, unsigned Count,
                           StringRef Logical) {
#ifndef NDEBUG
  for (unsigned I = 1; I < Count; ++I)
    assert(StringRef(Set[I - 1]) < StringRef(Set[I]) &&
           "mode-only name set not sorted");
#endif
  const auto *End = Set + Count;
  const auto *It = std::lower_bound(Set, End, Logical,
                                    [](const char *Row, StringRef Key) {
                                      return StringRef(Row) < Key;
                                    });
  return It != End && StringRef(*It) == Logical;
}

ModeOnly classifyModeOnlyLogical(StringRef Logical) {
  using namespace mode_only_detail;
  if (sortedNameSetContains(
          FormatEE2OnlyNameSet,
          sizeof(FormatEE2OnlyNameSet) / sizeof(FormatEE2OnlyNameSet[0]),
          Logical))
    return ModeOnly::E2Only;
  if (sortedNameSetContains(
          FormatEE3OnlyNameSet,
          sizeof(FormatEE3OnlyNameSet) / sizeof(FormatEE3OnlyNameSet[0]),
          Logical))
    return ModeOnly::E3Only;
  return ModeOnly::Neither;
}

/// Mode-only class of any residual/member/reloc spelling of \p OpcodeName.
///
/// Two peel modes, one rule per consumer surface:
///  - Coissue predicates pass \p StripWideForCoissue = true: a reloc `_W`
///    spelling (ADDI32_W, SET_HWLOOP_F2_W) inherits its base golden
///    logical's Mode class, because the base's catalog rows are the
///    placement truth — ADDI32_W is E2-only exactly as ADDI32 is, so the
///    3-wide-E3 coissue rejection keeps holding for the reloc form.
///  - The residual mask passes false: `_W` identities are residual
///    FieldSlot rows with no Format E span of their own (span lookup in
///    enumerateFormatEMemberAlts also peels with StripWide=false), so they
///    keep the product frontier there.
ModeOnly classifyModeOnlySpelling(StringRef OpcodeName,
                                  bool StripWideForCoissue) {
  return classifyModeOnlyLogical(haydn::format_e::peelLogicalOpcodeName(
      OpcodeName, /*StripWide=*/StripWideForCoissue));
}

} // namespace

bool llvm::isFormatEE2OnlyOpcodeName(StringRef OpcodeName) {
  return classifyModeOnlySpelling(OpcodeName, /*StripWideForCoissue=*/true) ==
         ModeOnly::E2Only;
}

bool llvm::isFormatEE3OnlyOpcodeName(StringRef OpcodeName) {
  return classifyModeOnlySpelling(OpcodeName, /*StripWideForCoissue=*/true) ==
         ModeOnly::E3Only;
}

uint64_t llvm::residualAltCompatibleFormatMask(unsigned LogicalOpc,
                                               unsigned AltIndex) {
  switch (classifyModeOnlySpelling(haydnOpcodeName(LogicalOpc),
                                   /*StripWideForCoissue=*/false)) {
  case ModeOnly::E2Only:
    // Golden E2-only logical (ADDI32, ...): residual occupancy above the E2
    // row's encoded entry count has no member; drop it and stamp the E2 row.
    if (AltIndex >= bundleRowEntryCount(BundleFormatRowID::E96TwoEntry))
      return 0;
    return formatRowBit(BundleFormatRowID::E96TwoEntry);
  case ModeOnly::E3Only:
    // Golden E3-only logical (LOG2, ...): no E2 row exists, so stamp E3 only
    // and FeasibleFormatMask can never collapse to E2.
    return formatRowBit(BundleFormatRowID::E96ThreeEntry);
  case ModeOnly::Neither:
    return ProductFormatMask;
  }
  llvm_unreachable("covered ModeOnly switch");
}
