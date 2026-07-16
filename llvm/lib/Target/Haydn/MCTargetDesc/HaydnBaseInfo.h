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
#include "llvm/MC/MCInst.h"
#include <cstdint>
#include <optional>

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
// (FU_ALU32=1..FU_MAC=5) was deleted alongside HaydnDClassInfo.h — the FlexMap
// (`HaydnMCFormats::getLegalSlots`) is FU-aware by construction (slot k is
// legal iff a `_S<k>` variant exists, and the.td FU/slot assignment
// produces that variant), so there is no longer a separate resource-model FU
// namespace to keep in sync. FU_MAC0=7 / FU_MAC1=8 were resource-only
// instances of MAC and are NOT encoded (they collapse to MAC=4 at the encode
// boundary).
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

//===----------------------------------------------------------------------===//
// MCInst::Flags slot layout (stage b.1 —)
//===----------------------------------------------------------------------===//
//
// MCInst::Flags (MCInst.h:193) is a 32-bit field the comment at :191-192
// explicitly sanctions for "target subcomponent to target subcomponent" use
// (X86 uses bits 6+ for IP_USE_* prefix flags, X86BaseInfo.h:59-66). Haydn
// owns the LOW bits to carry the HR-committed VLIW slot (0/1/2) as runtime
// metadata — the slot is chosen at packetize (HaydnHazardRecognizer.cpp:625
// AltDescs->setSlot) and propagated across the MI→MCInst boundary at
// MCInstLower, set from source-order position in the AsmParser, and set from
// the decode-slot index in the Disassembler. The encoder (stage b.2) reads
// this instead of scanning the opcode suffix; stage b.1 is additive plumbing
// only (zero consumer, byte-identical).
//
// Bit layout:
// Bit 0 : slot-valid (0 = no slot recorded; 1 = slot bits authoritative)
// Bits 1-2 : slot index (0=S0, 1=S1, 2=S2; 3 reserved)
// Bits 3+ : UNUSED by Haydn (X86 owns high bits; no Haydn MCInst ever
// carries X86 prefix flags — the two namespaces never coexist).
//
// MCInst is a value object — Flags is stored inline, copies free, no pointer
// identity needed. This is the value-stable vessel (codex's correction to the
// DenseMap<MCInst*,unsigned> sketch that lost the slot on copy).
namespace llvm::HaydnMCFlags {

// Bit position of the slot-valid flag.
constexpr unsigned SLOT_VALID_BIT = 0;
// Bit position of the 2-bit slot-index field.
constexpr unsigned SLOT_INDEX_SHIFT = 1;
// Mask for the 2-bit slot index.
constexpr unsigned SLOT_INDEX_MASK = 0x3u;

// Encode a slot index (0/1/2) into an MCInst::Flags word. Sets the valid bit.
inline unsigned encodeSlot(unsigned Slot) {
  return (1u << SLOT_VALID_BIT) | ((Slot & SLOT_INDEX_MASK) << SLOT_INDEX_SHIFT);
}

// \return true iff \p Flags has the slot-valid bit set.
inline bool hasSlot(unsigned Flags) {
  return (Flags & (1u << SLOT_VALID_BIT)) != 0;
}

// \return the slot index encoded in \p Flags. Caller MUST guard with hasSlot.
inline unsigned getSlot(unsigned Flags) {
  return (Flags >> SLOT_INDEX_SHIFT) & SLOT_INDEX_MASK;
}

// Record the HR-committed slot on \p MI. Overwrites any prior slot.
inline void setHaydnSlot(MCInst &MI, unsigned Slot) {
  MI.setFlags(encodeSlot(Slot));
}

// \return the slot recorded on \p MI, or std::nullopt if none.
inline std::optional<unsigned> getHaydnSlot(const MCInst &MI) {
  unsigned F = MI.getFlags();
  if (!hasSlot(F))
    return std::nullopt;
  return getSlot(F);
}

// \return true iff \p MI carries a recorded slot.
inline bool hasHaydnSlot(const MCInst &MI) { return hasSlot(MI.getFlags()); }

} // namespace llvm::HaydnMCFlags

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNBASEINFO_H
