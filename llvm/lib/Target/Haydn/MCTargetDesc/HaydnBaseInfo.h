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

// Largest number of entries a bundle can hold. Format E has a 2-entry form
// and a 3-entry form; this is the maximum, NOT the number of slot kinds
// (that is SLOT_KIND_COUNT below) and NOT a count you may loop over to
// enumerate slots — the entry count is a property of the chosen composite.
constexpr unsigned ISSUE_SLOT_COUNT = 3;

// A format E bundle is 96 bits / 12 bytes, payload from bit 6.
constexpr unsigned BUNDLE_E_BITS = 96;
constexpr unsigned BUNDLE_E_BYTES = BUNDLE_E_BITS / 8;

// Bundle width tag occupies bits [15:14] of the lead parcel.
constexpr unsigned BUNDLE_WIDTH_TAG_BITS = 2;

// Tag values indicating the total encoded bundle size.
constexpr unsigned BUNDLE_16BIT_TAG = 0b00;
constexpr unsigned BUNDLE_32BIT_TAG = 0b01;
constexpr unsigned BUNDLE_48BIT_TAG = 0b10;
constexpr unsigned BUNDLE_64BIT_TAG = 0b11;

//===----------------------------------------------------------------------===//
// Format E entry-slot masks
//===----------------------------------------------------------------------===//
//
// One bit per MCSlotKind, and the bit position IS the kind: the generated
// HaydnSlots table stamps each slot's SlotOccupancy as 1 << its enumerator, so
// these constants and MCSlotKind::Haydn_SLOT_* are two spellings of one thing.
// haydnSlotMaskToKind bridges them without a table.
//
// A format E bundle is 2 entries (P20,P21) or 3 entries (P30,P31,P32); the two
// sets are mutually exclusive and the generated ConflictBits say so. There is
// deliberately NO all-slots constant: Bundle128's SLOT_ALL meant "a full
// bundle", and under format E that is two different masks depending on which
// composite was chosen. Ask the packet format (VLIWFormat::getSlotSet), which
// is what actually knows.
constexpr unsigned SLOT_P20 = 1u << 0;
constexpr unsigned SLOT_P21 = 1u << 1;
constexpr unsigned SLOT_P30 = 1u << 2;
constexpr unsigned SLOT_P31 = 1u << 3;
constexpr unsigned SLOT_P32 = 1u << 4;

// Number of distinct slot kinds. Matches the MCSlotKind enum emitted into
// HaydnGenFormats.inc; a static_assert in HaydnMCFormats.cpp keeps them tied.
constexpr unsigned SLOT_KIND_COUNT = 5;

// The two composites' occupancy sets, for callers that need to name a whole
// bundle shape. These mirror the generated VLIWFormat SlotSet values (0x3 and
// 0x1c) and are asserted equal to them in HaydnMCFormats.cpp.
constexpr unsigned SLOT_SET_E2 = SLOT_P20 | SLOT_P21;
constexpr unsigned SLOT_SET_E3 = SLOT_P30 | SLOT_P31 | SLOT_P32;
// Every slot bit, for range checks and loop bounds only — NOT a legal
// occupancy, since no bundle holds all five.
constexpr unsigned SLOT_MASK_ANY = SLOT_SET_E2 | SLOT_SET_E3;

//===----------------------------------------------------------------------===//
// Hardware units
//===----------------------------------------------------------------------===//
//
// The seven execution units. An entry of a bundle uses exactly one, and no two
// entries of a bundle may use the same one — that is the constraint the slot
// model cannot express, because which unit an instruction uses is a property
// of the MEMBER chosen, not of the entry it sits in.
//
// Two things vary independently and both are DATA, never hardcoded here:
//
//   * which units an instruction can use — the set of members it has, from
//     the database's bit layout (HaydnFormatEEncoding.td alternates)
//   * which entry positions a unit may appear at — also the bit layout; today
//     18 of the 5x7 (position, unit) pairs exist, not all 35
//
// Those two are the knobs the hardware model moves between the fixed extreme
// (Bundle128: one unit per slot, so unit and slot were the same fact and this
// enum was unnecessary) and the flexible extreme (every unit at every
// position). The delivered format E layout already sits between them, and the
// balance point is expected to move again. Nothing in C++ should encode where
// it currently is: regenerate from a new layout and this all still holds.
enum class Unit : unsigned {
  LOADSTORE0 = 0,
  LOAD1 = 1,
  ALU0 = 2,
  ALU1 = 3,
  ALU2 = 4,
  MAC0 = 5,
  MAC1 = 6,
};

constexpr unsigned UNIT_COUNT = 7;

// Occupancy bitset over Unit, bit k = 1 << unit k. Distinct from SlotBits:
// SlotBits says WHERE in the bundle, UnitBits says WHICH hardware serves it.
using UnitBits = uint32_t;

constexpr UnitBits unitBit(Unit U) {
  return UnitBits(1) << static_cast<unsigned>(U);
}

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
