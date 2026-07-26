//===-- HaydnBaseInfo.h - Top level definitions for Haydn MC ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone definitions for the Haydn target useful
// for the compiler back-end and the MC libraries. It provides bundle-width
// constants, VLIW slot masks, encoded-width helpers, and TSFlags accessors
// shared across CodeGen, MC, and the assembler/disassembler.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNBASEINFO_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNBASEINFO_H

#include "llvm/MC/MCInstrDesc.h"
#include <cstdint>

namespace llvm::Haydn {

//===----------------------------------------------------------------------===//
// VLIW bundle constants
//===----------------------------------------------------------------------===//

// Number of issue slots in a VLIW bundle.
constexpr unsigned ISSUE_SLOT_COUNT = 3;

// Bundle width tag occupies bits [15:14] of the lead parcel.
constexpr unsigned BUNDLE_WIDTH_TAG_BITS = 2;

// Tag values indicating the total encoded bundle size.
constexpr unsigned BUNDLE_16BIT_TAG = 0b00;
constexpr unsigned BUNDLE_32BIT_TAG = 0b01;
constexpr unsigned BUNDLE_48BIT_TAG = 0b10;
constexpr unsigned BUNDLE_64BIT_TAG = 0b11;

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
// Encoded instruction width (TSFlags bits [4:3])
//===----------------------------------------------------------------------===//

// Encoded width of an individual instruction within a bundle.
enum EncodedWidth : unsigned {
  EW_16Bit = 0, //< Compressed (16-bit) encoding
  EW_32Bit = 1, //< Full-width (32-bit) encoding
  EW_48Bit = 2, //< Extended (32+16-bit) encoding
  EW_64Bit = 3, //< Mode-0/3 bundle slot (64-bit) — slot-OR variants
};

//===----------------------------------------------------------------------===//
// Flex 128-bit encoding — FU field (encoding_manual_flex.md §1.1/§1.2, R1)
//===----------------------------------------------------------------------===//
//
// The 128-bit Flex bundle places a 3-bit FU field at the MSB of each slot
// window (s0 bundle[47:45], s1 bundle[87:85], s2 bundle[127:125]); FU selects
// the per-slot decode table; the opcode sits at the payload MSB just below FU.
// 5 FU types; codes 5..7 are RESERVED (illegal-instruction trap). NOP = an
// all-zero slot window (§4).
//
// These are the ENCODING FU codes (0..4) — the spec's wire format.
// The former parallel "RESOURCE model" enum HaydnDClass::FUType
// (FU_ALU32=1..FU_MAC=5) was deleted alongside HaydnDClassInfo.h — alts-derived
// `HaydnMCFormats::getLegalSlots` covers slot legality (slot k is legal iff a
// `_S<k>` variant exists, and the .td FU/slot assignment produces that
// variant), so there is no longer a separate resource-model FU namespace to keep
// in sync. FU_MAC0=7 / FU_MAC1=8 were resource-only instances of MAC and are
// NOT encoded (they collapse to MAC=4 at the encode boundary).
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

//===----------------------------------------------------------------------===//
// TSFlags field layout
//===----------------------------------------------------------------------===//
//
// Bits Field
// [2:0] Slot mask
// [4:3] Encoded width
//
// Accessor helpers below extract these fields from the 64-bit TSFlags word
// stored in MCInstrDesc.

// Extract the VLIW slot mask from TSFlags.
inline unsigned getSlotMask(uint64_t TSFlags) { return TSFlags & 0x7; }

// Extract the encoded width from TSFlags.
inline EncodedWidth getEncodedWidth(uint64_t TSFlags) {
  return static_cast<EncodedWidth>((TSFlags >> 3) & 0x3);
}

} // namespace llvm::Haydn

// Placement is member Desc getSlotKind / Bundle SlotMap (AIE shape:
// AIEBaseMCFormats.cpp:66-75, AIEBundle.h:92-104, AIEBaseAsmParser.h:164-211).
// No MCInst::Flags slot path.

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNBASEINFO_H
