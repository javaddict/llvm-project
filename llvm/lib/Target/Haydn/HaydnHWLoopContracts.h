//===-- HaydnHWLoopContracts.h - Shared HWLOOP layout contracts -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Single source of truth for Haydn hardware-loop geometry and recovery policy.
// Included by formation (HaydnHardwareLoops) and pre-emit fixup (HaydnFixupHwLoops).
//
// ISA (Database SET_HWLOOP):
//   SET_HWLOOP sel, uimm6_off1, uimm12_off2, uimm16_cnt
//   HWLR_BEGIN[sel] = PC + (off1 << 2)   // max 252 B forward
//   HWLR_END[sel]   = PC + (off2 << 2)   // max 16380 B forward
//   HWLR_COUNT[sel] = cnt
//
// Geometry (match BundleSim code_image + AIE ZOL setup-distance model):
//   1. Inclusive END: HWLR_END >= HWLR_BEGIN is legal (start <= end).
//   2. Primary hard rule: SET must issue at or before body bundle t−3
//      → PC_SET + MinSetupBundles * productParcelBytes() <= PC_BEGIN
//      (B4.4: EncodedBytes from ProductFormatDesc, not a dual magic 16).
//   3. Body length is not a separate legality floor; short bodies are fine
//      when (1)+(2) hold. Do not invent min-body sprays as product law.
//
// AIE peer (AIEBaseInstrInfo::ZOLSupport + PostRA ExitSU latency + alignment):
//   LoopSetupDistance is enforced first by the post-RA scheduler (edge latency
//   from setup to region exit), then residual gaps are padded. Haydn:
//   MinSetupBundles via PostRA mutation + Fixup deficit NOPs after SET.
//
// Pipeline (no free AT invent; no multi-BB convert invent):
//   Role A IR prefer; expand LoopStart→SET before post-RA pack; Role B residual
//   opt-in; Fixup: t−3 pad → order-preserving shorten → demote-first.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNHWLOOPCONTRACTS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNHWLOOPCONTRACTS_H

#include "HaydnBundlePlan.h"
#include <cstdint>

namespace llvm {
namespace haydn {
namespace hwloop {

// Bundle128 parcel size (bytes). B4.4: alias of ProductFormatDesc.Bytes
// (encodedBytesFor(Bundle128Full)), not an independent magic constant.
// getInstSizeInBytes / Fixup / HardwareLoops share this EncodedBytes oracle.
inline constexpr int64_t Bundle128Bytes =
    static_cast<int64_t>(bundle::productParcelBytes().Value);
static_assert(Bundle128Bytes == 16, "Bundle128 product parcel is 16 bytes");
static_assert(Bundle128Bytes ==
                  static_cast<int64_t>(bundle::Bundle128EncodedBytesValue),
              "hwloop Bundle128Bytes must equal plan EncodedBytes");
static_assert(Bundle128Bytes ==
                  static_cast<int64_t>(bundle::ProductFormatDesc.Bytes.Value),
              "hwloop Bundle128Bytes must equal ProductFormatDesc.Bytes");

// SET_HWLOOP offset field widths (ISA DB).
inline constexpr unsigned Offset1Bits = 6;  // uimm6 → START
inline constexpr unsigned Offset2Bits = 12; // uimm12 → END

// Max forward PC-relative distances in bytes (field × 4).
inline constexpr int64_t MaxStartOffsetBytes =
    ((static_cast<int64_t>(1) << Offset1Bits) - 1) * 4; // 252
inline constexpr int64_t MaxEndOffsetBytes =
    ((static_cast<int64_t>(1) << Offset2Bits) - 1) * 4; // 16380

// Safety margin so Fixup's size estimate does not pass a value that AsmPrinter
// later rejects (label placement, late bundles). Prefer demote over MC fail.
inline constexpr int64_t Off1SafetyMarginBundles = 3;
inline constexpr int64_t Off1SafetyMarginBytes =
    bundle::productBundlesToBytes(
        static_cast<unsigned>(Off1SafetyMarginBundles)); // 48
inline constexpr int64_t MaxStartOffsetBytesSafe =
    MaxStartOffsetBytes - Off1SafetyMarginBytes; // 204

// Spec / BundleSim: SET at or before body bundle t−3.
// AIE peer of ZOLSupport::LoopSetupDistance (AIE2 uses 7 bundles to LEND;
// Haydn measures setup → BEGIN with 3 bundles).
inline constexpr unsigned MinSetupBundles = 3;

// Min setup distance in bytes (MinSetupBundles × product EncodedBytes).
// B4.4: single EncodedBytes path; AIE sums Format->getSize()
// (AIEMachineAlignment.cpp:287+).
inline constexpr int64_t MinSetupBytes =
    bundle::productBundlesToBytes(MinSetupBundles);

// Spec, VLIW_Engine_Compiler_Constraints.md § HW Loop:
//   "Loop Body: It must contain at least 3 instruction bundles."
//
// This was 0 with a note calling it "deprecated as a legality floor" and
// claiming the product law was only MinSetupBundles + END >= BEGIN. It is not
// deprecated — it is a documented hard rule, and dropping it let tiny ZOL
// bodies (1-2 bundles) reach the assembler. HaydnFixupHwLoops now pads short
// bodies with NOPs before the inclusive END.
inline constexpr unsigned MinBodyBundles = 3;

} // namespace hwloop
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNHWLOOPCONTRACTS_H
