//===- HaydnPackLegality.h - packet oracle ------------*- C++ -*-===
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Single product contract for "may these ops share one Format E issue cycle?"
// Densify / post-RA pack / exhaustive tests consume this pin.
//
// Dual authority (must stay consistent; do not invent a third table):
//
//   Schedule-time  HaydnHazardRecognizer (units, ports, WAW/RAW, locked DSP,
//                  format tryAdd, CSRW↔SET_HWLOOP)
//   Encode-time    Haydn::Bundle + HaydnMCFormats (alts-derived placement /
//                  format coverage; leaveRegion setDesc(member))
//
// Product rules vs constraints / golden residual:
//
//   1. Dual R0 defs in one cycle are ILLEGAL. R0 is soft-zero (not hardwired);
//      XOR32 R0,R0,R0 and LD into R0 are real write-port consumers.
//   2. Dual live same-reg WAW is ILLEGAL.
//   3. Dual dead implicit-def $sfr in one cycle is LEGAL (slot-ordered SFR
//      writes; excluding SFR from WAW is intentional product law — not a
//      golden "single SFR write" reopen without product activation).
//   4. ARCTAN / SIN_COS issue alone in their cycle only until multi-cycle
//      (uimm4+2) unit lock is product-enabled.
//   5. Issue ≤ 3 entries; seven-unit injectivity; GPR 4R2W; DR 7R3W; AR 2R2W;
//      live SFR 2R1W vocabulary (dead flag defs do not serialize packs).
//   6. Format placement: no two ops forced onto an illegal entry/unit pair
//      (exact tryAdd / generated alternatives — entry ≠ unit resource).
//   7. CSRW CSR 0x20–0x25 must not share a cycle with SET_HWLOOP family.
//
// Exhaustive unit coverage lives in:
//   unittests/Target/Haydn/HaydnHazardRecognizerTest.cpp
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPACKLEGALITY_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPACKLEGALITY_H

#include "HaydnHazardRecognizer.h"
#include "HaydnPortModel.h"

namespace llvm {
namespace haydn {
namespace pack {

/// Port / issue / unit-footprint conflict (rule 5). Pure data — no MI needed.
inline bool resourcesConflict(const HaydnFuncUnitWrapper &A,
                              const HaydnFuncUnitWrapper &B) {
  return A.conflict(B);
}

/// Product issue cap: max Format E entries per cycle (rule 5).
inline constexpr unsigned MaxIssuePerCycle = 3;

/// Number of named Format E execution units (resource identity).
inline constexpr unsigned NumExecutionUnits = HAYDN_NUM_FU_BITS;

// Rule 4 (ARCTAN/SIN_COS alone): implemented as
// HaydnHazardRecognizer::isLockedSlotDspOp — not duplicated here so opcode
// enums stay in the .cpp / MIR layer (single authority).

} // namespace pack
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPACKLEGALITY_H
