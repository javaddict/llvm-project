//===-- HaydnFormat.h - Format utilities for Haydn VLIW bundles -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines the format data necessary for dealing with VLIW bundles on the
// Haydn 3-issue DSP. It is a port of AIE's VLIWFormat / PacketFormats model
// aligned to the AIE schema so that HaydnGenFormats.inc (the CodeGenFormat
// tablegen backend output) can define the generated packet-format tables
// (GET_FORMATS_PACKETS_TABLE region) directly against these class shapes.
//
// The generated sentinel emitted by HaydnGenFormats.inc is:
// `{0, nullptr, {nullptr, nullptr}, 0, 0}`
// matching the 5-field VLIWFormat ctor: {Opcode, Name, SlotKindRange, Size
// SlotSet}. The 4-field Haydn shape is retired (decision §9 Option A).
//
// Adapted to Haydn's slot-bitset convention (Haydn::SLOT0/1/2 in
// HaydnBaseInfo.h) and Haydn's bundle formats: six legacy formats
// (manual §1.3) plus the / Bundle128 single-composite 128-bit format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNFORMAT_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNFORMAT_H

#include "HaydnBaseInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {

// Bitmask type used to represent a set of VLIW slots.
// Haydn uses 3 bits (Haydn::SLOT0/1/2), but a 64-bit width is retained for
// parity with AIE and future-proofing.
using SlotBits = uint64_t;

// Maximum number of slots addressable in a SlotBits word.
const int MaxSlots = 64;

class MCSlotKind;

// Description of a VLIW bundle format. Mirrors AIE's VLIWFormat exactly
// (5-field constexpr ctor) so the generated HaydnGenFormats.inc packet-format
// tables compile against it.
class VLIWFormat {
public:
  // A range to iterate over the slots of a format (AIE pattern).
  class SlotKindRange {
  public:
    constexpr SlotKindRange() = default;
    constexpr SlotKindRange(const MCSlotKind *Begin, const MCSlotKind *End)
        : Begin(Begin), End(End) {}
    constexpr const MCSlotKind *begin() const { return Begin; }
    constexpr const MCSlotKind *end() const { return End; }

  private:
    const MCSlotKind *Begin = nullptr;
    const MCSlotKind *End = nullptr;
  };

  constexpr VLIWFormat(unsigned Opcode, const char *Name, SlotKindRange Range,
                       unsigned Size, SlotBits Bits)
      : Opcode(Opcode), Name(Name), Slots(Range), Size(Size), SlotSet(Bits) {}

  const SlotKindRange &getSlots() const { return Slots; }

  SlotBits getSlotSet() const { return SlotSet; }

  const unsigned &getSize() const { return Size; }

  // \returns whether this format can accommodate all slots in \p Slots.
  // True when `Slots` is a subset of `SlotSet` (no bit outside SlotSet).
  bool covers(SlotBits Slots) const;

  // Opcode reserved for the future composite (packet) instruction.
  // Haydn does not yet have a composite packet opcode (the packetizer is a
  // later slice); this field is 0 for all current rows and reserved for
  // future use. It is NOT used as the table terminator (see Size==0 sentinel).
  unsigned Opcode;

  // Name of the format.
  const char *Name;

private:
  // The slots in the format-specific order.
  SlotKindRange Slots;
  // Encoded parcel size in bits (16/32/48/64/128).
  unsigned Size;

  // Precomputed OR of the Haydn::SLOT* masks this format can hold.
  SlotBits SlotSet = 0;
};

// Wrapper over a NULL-sentinel-terminated array of VLIWFormat rows providing
// format lookups by slot-combo and by size. Mirrors AIE's PacketFormats.
class PacketFormats {
public:
  PacketFormats(const VLIWFormat *Formats) : FormatsTable(Formats) {}

  // \returns the first format (smallest-first by table order) that covers
  // \p SlotSet, or nullptr if none.
  const VLIWFormat *getFormat(SlotBits SlotSet) const;

  // \returns the first format whose size equals \p Size and that covers
  // \p SlotSet. The table is assumed sorted by ascending Size; the scan
  // terminates once it passes \p Size.
  const VLIWFormat *getFormatBySize(SlotBits SlotSet, unsigned Size) const;

private:
  const VLIWFormat *FormatsTable;
};

// G-MC-9: hand-authored HaydnFormats / HaydnPacketFormats
// HaydnFormatAvailable deleted. getPacketFormats / getIsFormatAvailable
// return CodeGenFormat-generated tables (HaydnGenFormats.inc).
// PacketFormats methods remain below for the generated table wrapper.

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNFORMAT_H
