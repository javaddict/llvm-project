//===- HaydnPackLegality.h - G-PACK-LEGAL packet oracle ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// G-PACK-LEGAL — single product contract for "may these ops share one Bundle128
// cycle?" Densify W2.3 / post-RA pack / exhaustive tests consume this pin.
//
// Dual authority (must stay consistent; do not invent a third table):
//
//   Schedule-time  HaydnHazardRecognizer (ports, WAW/RAW, locked DSP,
//                  PlacementAlternative tryAdd FieldSlots, CSRW↔SET_HWLOOP)
//   Encode-time    Haydn::Bundle + HaydnMCFormats (alts-derived getLegalSlots /
//                  PlacementAlternative FieldSlots + format coverage;
//                  leaveRegion setDesc(member); Desc-only AsmPrinter)
//
// Product rules vs BundleSim WRITE_CONFLICT / golden residual (2026-07-24):
//
//   1. Dual R0 defs in one cycle are ILLEGAL. R0 is soft-zero (not hardwired);
//      XOR32 R0,R0,R0 and LD into R0 are real write-port consumers.
//   2. Dual live same-reg WAW is ILLEGAL (spec §Constraints).
//   3. Dual dead implicit-def $sfr in one cycle is LEGAL (slot-ordered SFR
//      writes; excluding SFR from WAW is intentional product law — not a
//      golden "single SFR write" reopen without RTL decision).
//   4. ARCTAN / SIN_COS issue alone in their cycle only (no multi-cycle
//      slot lock in current design).
//   5. Issue ≤ 3; GPR 4R2W; DR64 7R3W; AR 2R2W (HaydnFuncUnitWrapper::conflict).
//   6. PlacementAlternative FieldSlots / alts-derived getLegalSlots: no two
//      ops forced onto the same filled field (getLegalSlots = OR of sparse
//      AlternateInsts; leaveRegion setDesc(member); AIE shape).
//   7. CSRW CSR 0x20–0x25 must not share a cycle with SET_HWLOOP (spec §5.10).
//
// Exhaustive unit/MIR coverage lives in:
//   unittests/Target/Haydn/HaydnHazardRecognizerTest.cpp   (ports/slots/issue)
//   unittests/Target/Haydn/HaydnBundleTest.cpp             (format/canAdd/hint)
//   unittests/Target/Haydn/HaydnMCFormatsTest.cpp          (alts/getLegalSlots/formats)
//   unittests/Target/Haydn/HaydnAlternateDescriptorsTest.cpp (slot side-map)
//   test/CodeGen/Haydn/postmisched-*-hazard*.mir          (WAW/RAW/locked)
//   test/CodeGen/Haydn/postmisched-r0-waw-hazard.mir      (rule 1)
//   test/CodeGen/Haydn/packetizer-sfr-hazard-regression.ll (rule 3)
// Gap map vs full format-aware pipeline:
//   haydn-plans/plans/open/bundle-format-aware-pipeline.md §4.7–§4.9
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPACKLEGALITY_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPACKLEGALITY_H

#include "HaydnHazardRecognizer.h"
#include "HaydnPortModel.h"

namespace llvm {
namespace haydn {
namespace pack {

/// Port / issue / slot-footprint conflict (rule 5). Pure data — no MI needed.
inline bool resourcesConflict(const HaydnFuncUnitWrapper &A,
                              const HaydnFuncUnitWrapper &B) {
  return A.conflict(B);
}

/// Product issue cap (rule 5).
inline constexpr unsigned MaxIssuePerCycle = 3;

// Rule 4 (ARCTAN/SIN_COS alone): implemented as
// HaydnHazardRecognizer::isLockedSlotDspOp — not duplicated here so opcode
// enums stay in the .cpp / MIR layer (single authority).

} // namespace pack
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPACKLEGALITY_H
