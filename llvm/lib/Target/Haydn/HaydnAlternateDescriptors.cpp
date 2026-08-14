//===- HaydnAlternateDescriptors.cpp - Alt descriptor side-map --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Anchors HaydnAlternateDescriptors.h and hosts residual Format E placement
// mask helpers declared in HaydnPlacementAlternative.h (E2-only and E3-only
// golden Mode clamps for residualAltCompatibleFormatMask).
//
//===----------------------------------------------------------------------===//

#include "HaydnAlternateDescriptors.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/StringRef.h"

using namespace llvm;
using namespace llvm::haydn::bundle;

bool llvm::isFormatEE2OnlyOpcodeName(StringRef OpcodeName) {
  // SET_HWLOOP_F2 / SET_HWLOOP_REG have Format E E3 members; bare SET_HWLOOP
  // does not. Check the E3-bearing forms first.
  if (OpcodeName.starts_with("SET_HWLOOP_F2") ||
      OpcodeName.starts_with("SET_HWLOOP_REG"))
    return false;

  // Longer prefixes first so ADDI32S is not matched as ADDI32.
  // Keep in sync with FormatEE2OnlyNames (HaydnGenFormatERecords.inc).
  static constexpr StringRef E2Only[] = {
      "ADDI32S", "ADDI32", "SUBI32S", "SUBI32", "ANDI32", "XORI32",
      "ORI32",   "MOVEI_H", "MOVEI_L", "SET_HWLOOP",
  };
  for (StringRef Log : E2Only) {
    if (OpcodeName == Log)
      return true;
    if (!OpcodeName.starts_with(Log))
      continue;
    StringRef Rest = OpcodeName.drop_front(Log.size());
    if (Rest.starts_with("_S") || Rest.starts_with("_E2_") ||
        Rest.starts_with("_W"))
      return true;
  }
  return false;
}

bool llvm::isFormatEE3OnlyOpcodeName(StringRef OpcodeName) {
  // Keep in sync with FormatEE3OnlyNames (HaydnGenFormatERecords.inc = 6).
  static constexpr StringRef E3Only[] = {
      "ARCTAN", "EXP2", "LOG2", "RECIP", "SIN_COS", "SQRT",
  };
  for (StringRef Log : E3Only) {
    if (OpcodeName == Log)
      return true;
    if (!OpcodeName.starts_with(Log))
      continue;
    StringRef Rest = OpcodeName.drop_front(Log.size());
    if (Rest.starts_with("_S") || Rest.starts_with("_E3_") ||
        Rest.starts_with("_W"))
      return true;
  }
  return false;
}

uint64_t llvm::residualAltCompatibleFormatMask(unsigned LogicalOpc,
                                               unsigned AltIndex) {
  // Golden Format E members with Mode=E2 only (no E3 row): drop residual S2
  // and stamp E96TwoEntry only.
  switch (LogicalOpc) {
  case Haydn::ADDI32:
  case Haydn::ADDI32S:
  case Haydn::ANDI32:
  case Haydn::MOVEI_H:
  case Haydn::MOVEI_L:
  case Haydn::ORI32:
  case Haydn::SET_HWLOOP:
  case Haydn::SUBI32:
  case Haydn::SUBI32S:
  case Haydn::XORI32:
    if (AltIndex >= 2)
      return 0;
    return formatRowBit(BundleFormatRowID::E96TwoEntry);
  // Golden Format E members with Mode=E3 only (no E2 row): stamp
  // E96ThreeEntry only so FeasibleFormatMask cannot collapse to E2.
  case Haydn::ARCTAN:
  case Haydn::EXP2:
  case Haydn::LOG2:
  case Haydn::RECIP:
  case Haydn::SIN_COS:
  case Haydn::SQRT:
    return formatRowBit(BundleFormatRowID::E96ThreeEntry);
  default:
    return ProductFormatMask;
  }
}
