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
#include "HaydnFormat.h"
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

  // Product Format E: every parcel is registry EncodedBytes (12). Code
  // alignment and writeNopData produce full product NOP parcels. MaxInstLength
  // feeds TargetInstrInfo::getInlineAsmLength so INLINEASM / INLINEASM_BR
  // layout sizes charge one product parcel per textual instruction
  // (conservative; empty side-effect barriers remain 0).
  const unsigned ProductBytes =
      haydn::format::maxEncodedBytesInProfile(
          haydn::format::ObjectEncodingProfileID::E96)
          .Value;
  // MinInstAlignment reaches exactly one place: the DWARF line program's
  // minimum_instruction_length, the unit MCDwarf DIVIDES address advances by.
  // ProductBytes (12) is only exact while every advance is a whole number of
  // parcels — functions align to 4 (the largest power of two dividing 12), so
  // any perturbation (hand asm, .balign, section starts) yields advances that
  // truncate and every later line address drifts. 1 costs a slightly larger
  // line table and is exact for every address; dwarf-line-bundle-addresses.s
  // compares the numbers directly.
  MinInstAlignment = 1;
  MaxInstLength = ProductBytes;

  // Do not let AsmPrinter::emitAlignment(MF, &F) promote function alignment
  // from IR/user attributes (e.g. aligned(256)). Those power-of-two values
  // larger than the product max (largest 2^k | EncodedBytes) force non-parcel
  // pads at LLD input-section boundaries. HaydnAsmPrinter emits the product
  // max explicitly before the entry label instead.
  HasFunctionAlignment = false;
}
