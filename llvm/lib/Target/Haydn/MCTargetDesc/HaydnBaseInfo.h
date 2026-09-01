//===-- HaydnBaseInfo.h - Top level definitions for Haydn MC ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone definitions for the Haydn target useful
// for the compiler back-end and the MC libraries: issue-slot occupancy cap,
// VLIW slot masks, and residual FU codes shared across CodeGen and MC.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNBASEINFO_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNBASEINFO_H

#include <cstdint>

namespace llvm::Haydn {

//===----------------------------------------------------------------------===//
// VLIW bundle constants
//===----------------------------------------------------------------------===//

// Number of issue slots in a VLIW bundle. This equals the generated E3
// row entry capacity — the compile-time pin lives beside the generated
// occupancy constants in HaydnPackLegality.h (FormatEE3EntryCapacity /
// MaxIssuePerCycle), which cannot be included here without pulling
// MachineInstr into every MC consumer. CodeGen seats that have that
// header use these names; ISSUE_SLOT_COUNT remains the MC-side spelling
// of the same generated fact.
constexpr unsigned ISSUE_SLOT_COUNT = 3;

// Product parcels are Format E only. Retired variable-width bundle tags and
// encoded-width TSFlags accessors are deleted (never-reintroduce).

//===----------------------------------------------------------------------===//
// VLIW slot masks (TSFlags bits [2:0])
//===----------------------------------------------------------------------===//

// Slot 0 — ALU / Load-Store unit.
constexpr unsigned SLOT0 = 0b001;
// Slot 1 — ALU64/SIMD / Load / MAC.
constexpr unsigned SLOT1 = 0b010;
// Slot 2 — ALU64/SIMD / MAC (shares instruction set with slot 1).
constexpr unsigned SLOT2 = 0b100;
// Mask combining all three slots.
constexpr unsigned SLOT_ALL = SLOT0 | SLOT1 | SLOT2;

//===----------------------------------------------------------------------===//
// Residual FU codes (placement / `_S*` member scaffolding)
//===----------------------------------------------------------------------===//
//
// Product encode is Format E only (96-bit / 12-byte parcels; FE8). Residual
// FU codes below label `_S0`/`_S1`/`_S2` member families for placement tables
// and setDesc alternatives mapped onto E2/E3 entry windows — they are not a
// 128-bit product composite wire format.
//
// 5 FU types; codes 5..7 are RESERVED. `HaydnMCFormats::getLegalSlots` covers
// slot legality (slot k is legal iff a `_S<k>` variant exists).
namespace FlexFU {
constexpr unsigned ALU32 = 0;          //< 000 — 32-bit scalar ALU (GPR). opcode 6b.
constexpr unsigned LS   = 1;           //< 001 — Load/Store. opcode 7b.
constexpr unsigned ALU64 = 2;          //< 010 — 64-bit ALU/SIMD (DR64). opcode 8b.
constexpr unsigned LD   = 3;           //< 011 — Load unit. opcode 6b.
constexpr unsigned MAC  = 4;           //< 100 — Multiply-accumulate. opcode 9b.
constexpr unsigned FIRST_RESERVED = 5; //< 101..111 reserved -> illegal trap.

// Opcode width (bits) for the FU; the opcode occupies the payload MSB.
constexpr unsigned opcodeBits(unsigned Fu) {
  switch (Fu) {
  case ALU32: return 6;
  case LS:    return 7;
  case ALU64: return 8;
  case LD:    return 6;
  case MAC:   return 9;
  default:    return 0; // reserved/illegal
  }
}
} // namespace FlexFU

} // namespace llvm::Haydn

// Placement is member Desc getSlotKind / Bundle SlotMap (AIE shape:
// AIEBaseMCFormats.cpp:66-75, AIEBundle.h:92-104, AIEBaseAsmParser.h:164-211).
// No MCInst::Flags slot path.

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNBASEINFO_H
