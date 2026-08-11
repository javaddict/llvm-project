//===-- HaydnMCAsmInfo.cpp - Haydn Asm properties ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the HaydnMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "HaydnMCAsmInfo.h"
#include "llvm/MC/MCStreamer.h"

using namespace llvm;

void HaydnMCAsmInfo::anchor() {}

HaydnMCAsmInfo::HaydnMCAsmInfo(const Triple &TargetTriple) {
  AlignmentIsInBytes = false;
  SupportsDebugInformation = true;  // Enable DWARF debug info support
  CommentString = "//";

  // Uses '.section' before '.bss' directive
  UsesELFSectionDirectiveForBSS = true;

  ExceptionsType = ExceptionHandling::DwarfCFI;

  // 1, not the parcel size, and this is load-bearing rather than a default.
  //
  // MinInstAlignment reaches exactly one place: the DWARF line program's
  // `minimum_instruction_length`, by which MCDwarf DIVIDES every address
  // advance. It was 16 here — Bundle128's parcel — so after the switch to
  // 12-byte parcels every line-table address was rounded DOWN to a multiple
  // of 16: a `.loc` three bundles in reported 0x20 for an instruction at
  // 0x24. Debuggers then set breakpoints mid-bundle, and BundleSim refuses
  // the request outright ("failed to set breakpoint site"), which is the only
  // reason this surfaced at all — nothing in the compiler's own gates reads
  // the line table.
  //
  // 12 would be wrong too, for a subtler reason: not every advance is a whole
  // number of parcels. Functions align to 4 (FORMAT-E-SWITCH-PLAN.md § 5.9),
  // so the gap from the last bundle of one function to the start of the next
  // need not be a multiple of 12, and the division truncates in silence
  // again. Byte granularity is the only value that cannot lose anything; it
  // costs a slightly larger line program and nothing else. RISC-V and
  // LoongArch assert on this being 1 for the same reason.
  //
  // Parcel-width facts belong to Haydn::BUNDLE_E_BYTES, which is what
  // writeNopData and the packing layer already use.
  MinInstAlignment = 1;
}
