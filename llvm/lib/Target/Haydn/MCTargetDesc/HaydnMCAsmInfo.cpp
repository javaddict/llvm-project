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

  // A.6 / Bundle128: every instruction/parcel is 16 bytes. Code alignment and
  // writeNopData only produce full Bundle128 NOP parcels (all-zero 16 B).
  MinInstAlignment = 16;
}
