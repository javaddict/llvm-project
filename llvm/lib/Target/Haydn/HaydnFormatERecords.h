//===-- HaydnFormatERecords.h - Inert Format E generated tables -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Include surface for generated Format E placement / inverse / setDesc ledger
// tables. Product disassembler (tryDecodeFormatE) and MC encode placement
// consume the inverse / type-layout / member tables; unit tests pin counts.
//
// Regenerate:
//   FormatE/generate_format_e_records.py
//   FormatE/generate_format_e_records.py --check
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNFORMATERECORDS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNFORMATERECORDS_H

#include <cstdint>
#include <cstring>

namespace llvm {
namespace haydn {
namespace format_e {

// ---------------------------------------------------------------------------
// Golden pins + geometry
// ---------------------------------------------------------------------------
#define GET_FORMAT_E_GOLDEN_PINS
#include "HaydnGenFormatERecords.inc"

#define GET_FORMAT_E_ENUMS
#include "HaydnGenFormatERecords.inc"

#define GET_FORMAT_E_TYPE_LAYOUTS
#include "HaydnGenFormatERecords.inc"

#define GET_FORMAT_E_MEMBERS
#include "HaydnGenFormatERecords.inc"

#define GET_FORMAT_E_ALTERNATIVES
#include "HaydnGenFormatERecords.inc"

#define GET_FORMAT_E_INVERSE
#include "HaydnGenFormatERecords.inc"

#define GET_FORMAT_E_UNIT_INJECTIVITY
#include "HaydnGenFormatERecords.inc"

#define GET_FORMAT_E_SETDESC_LEDGER
#include "HaydnGenFormatESetDescLedger.inc"

/// Linear search for a logical's alternative span (inert tables; not hot path).
inline const FormatEAltSpan *findAltSpan(const char *Logical) {
  for (unsigned I = 0; I < FormatENonNopLogicalCount; ++I) {
    if (std::strcmp(FormatEAltSpans[I].Logical, Logical) == 0)
      return &FormatEAltSpans[I];
  }
  return nullptr;
}

/// Inverse lookup by placement key. Returns MemberId or -1.
inline int findInverseMemberId(uint8_t Mode, uint8_t EntryIdx, uint8_t Unit,
                               uint8_t TypeCode, uint16_t Opcode) {
  for (unsigned I = 0; I < FormatEMemberCount; ++I) {
    const FormatEInverseRec &R = FormatEInverse[I];
    if (R.Mode == Mode && R.EntryIdx == EntryIdx && R.Unit == Unit &&
        R.TypeCode == TypeCode && R.Opcode == Opcode)
      return static_cast<int>(R.MemberId);
  }
  return -1;
}

} // namespace format_e
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNFORMATERECORDS_H
