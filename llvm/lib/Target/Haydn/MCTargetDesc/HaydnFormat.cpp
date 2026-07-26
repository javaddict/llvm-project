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
