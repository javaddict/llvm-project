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
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include <cassert>

using namespace llvm;

void HaydnMCAsmInfo::anchor() {}

HaydnMCAsmInfo::HaydnMCAsmInfo(const Triple &TargetTriple) {
  AlignmentIsInBytes = false;
  SupportsDebugInformation = true;  // Enable DWARF debug info support
  CommentString = "//";
  UseAtForSpecifier = false;

  // Uses '.section' before '.bss' directive
  UsesELFSectionDirectiveForBSS = true;

  ExceptionsType = ExceptionHandling::DwarfCFI;

  // Product Format E: every parcel is registry EncodedBytes. writeNopData and
  // MaxInstLength use that parcel size. MaxInstLength feeds
  // TargetInstrInfo::getInlineAsmLength so INLINEASM / INLINEASM_BR layout
  // sizes charge one product parcel per textual instruction (conservative;
  // empty side-effect barriers remain 0).
  //
  // MinInstAlignment is the golden two-byte min bundle-address alignment, not
  // EncodedBytes. MCDwarf ScaleAddrDelta / CIE code_alignment_factor divide
  // address deltas by this factor; EncodedBytes=12 as min-align corrupts any
  // code address not ≡0 mod 12 (thunks, hand asm, Align(4) function starts).
  // Do not invent Align=4 (countr_zero(EncodedBytes)) — that is function
  // alignment only (HaydnISelLowering).
  const unsigned ProductBytes =
      haydn::format::maxEncodedBytesInProfile(
          haydn::format::ObjectEncodingProfileID::E96)
          .Value;
  assert(ProductBytes % haydn::format::MinBundleAddressAlignBytes == 0 &&
         "product EncodedBytes must be a multiple of min bundle address align");
  MinInstAlignment = haydn::format::MinBundleAddressAlignBytes;
  MaxInstLength = ProductBytes;

  // W70.2r function-entry path: generic AsmPrinter header emitAlignment
  // so HaydnMCELFStreamer::emitCodeAlignment emits whole-parcel idle fill
  // BEFORE the entry label. That fill is outside every function's committed
  // stream and aligns mid-section symbols in shared .text. Function sections
  // remain optional containment. User aligned(N) is a language guarantee;
  // Min/Pref stay Align(4).
  //
  // GR1.9 / D1.166 internal MBB/ZOL alignment is not this flag:
  // padInternalMBBAlignment inserts complete idle packets and
  // MF.ensureAlignment(A) so this pre-label fill is the absolute-address
  // grid for llvm.loop.align. Stamped LBN closer clears MBB metadata
  // before freeze so emitBasicBlockStart cannot call emitCodeAlignment
  // for those sites. Min/Pref stay Align(4).
  HasFunctionAlignment = true;
}

void HaydnMCAsmInfo::printSpecifierExpr(raw_ostream &OS,
                                        const MCSpecifierExpr &Expr) const {
  StringRef Name = Haydn::getSpecifierName(Expr.getSpecifier());
  if (!Name.empty())
    OS << '%' << Name << '(';
  printExpr(OS, *Expr.getSubExpr());
  if (!Name.empty())
    OS << ')';
}

Haydn::Specifier Haydn::parseSpecifierName(StringRef Name) {
  return StringSwitch<Specifier>(Name)
      .Case("hi12", ELF::R_HAYDN_HI12)
      .Case("lo20", ELF::R_HAYDN_LO20)
      .Case("pc_lo20", ELF::R_HAYDN_PC_LO20)
      .Default(0);
}

StringRef Haydn::getSpecifierName(Specifier Kind) {
  switch (Kind) {
  case ELF::R_HAYDN_HI12:
    return "hi12";
  case ELF::R_HAYDN_LO20:
    return "lo20";
  case ELF::R_HAYDN_PC_LO20:
    return "pc_lo20";
  default:
    return {};
  }
}
