//===-- HaydnFormat.cpp - PacketFormats helpers ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// PacketFormats lookup helpers. Hand-authored format tables retired;
// the live PacketFormats instance is generated in HaydnGenFormats.inc.
//
//===----------------------------------------------------------------------===//

#include "HaydnFormat.h"
// getFormatByEntryCount measures a format's SlotKindRange, so MCSlotKind has to
// be complete here; HaydnFormat.h only forward-declares it.
#include "HaydnMCFormats.h"

using namespace llvm;

bool VLIWFormat::covers(SlotBits Slots) const { return !(Slots & ~SlotSet); }

const VLIWFormat *PacketFormats::getFormat(SlotBits Slots) const {
  for (const VLIWFormat *Fmt = FormatsTable; Fmt->getSize(); Fmt++) {
    if (Fmt->covers(Slots)) {
      return Fmt;
    }
  }
  return nullptr;
}

const VLIWFormat *PacketFormats::getFormatByEntryCount(unsigned N) const {
  // The row's own slot range says how many entries it holds; do not map
  // 2 -> BUNDLE_E2 by hand.
  for (const VLIWFormat *Fmt = FormatsTable; Fmt->getSize(); Fmt++) {
    const auto &Slots = Fmt->getSlots();
    if (static_cast<unsigned>(Slots.end() - Slots.begin()) == N)
      return Fmt;
  }
  return nullptr;
}

const VLIWFormat *PacketFormats::getFormatBySize(SlotBits Slots,
                                                 unsigned Size) const {
  for (const VLIWFormat *Fmt = FormatsTable; Fmt->getSize(); Fmt++) {
    unsigned ThisSize = Fmt->getSize();
    if (ThisSize > Size) {
      // Formats are sorted by size. Once we are beyond the requested Size
      // we won't find it.
      break;
    }
    if (ThisSize < Size) {
      continue;
    }
    if (Fmt->covers(Slots)) {
      return Fmt;
    }
  }
  return nullptr;
}
