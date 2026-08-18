//===- HaydnMCChecker.h - Parse-time Format E bundle check ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Hexagon MCChecker analog (HexagonMCChecker.h:33 / HexagonMCChecker.cpp).
// Parse-time legality for public hand-asm bundles: unit injectivity, same-
// register WAW, RF-port ceilings (GPR 4R/2W, DR 7R/3W, AR 2R/2W, SFR 2R/1W), SET_HWLOOP
// same-sel. Does not require committed entry identity — standalone encode
// still places bare logicals. Compiler composites stay serialize-only.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCCHECKER_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCCHECKER_H

#include "llvm/ADT/ArrayRef.h"
#include <optional>
#include <string>

namespace llvm {

class MCInst;
class MCInstrInfo;
class MCRegisterInfo;

/// Shared WAW / SET_HWLOOP-sel / RF-port law (HexagonMCChecker.cpp register
/// + loop-setup checks; Haydn overlay is Format E units + PortModel ceilings).
/// One predicate: parse-time haydnCheckParsedBundle and verifyParsedBundle.
std::optional<std::string>
haydnCheckParsedBundleRegs(ArrayRef<const MCInst *> Reals,
                           const MCInstrInfo &MII, const MCRegisterInfo *MRI);

/// Check a parsed hand-asm bundle. \p Reals are non-NOP children in source
/// order. \p TextEntries is the textual entry count including NOP fillers
/// (3 forces E3; 1–2 may use E2 or E3). Returns a diagnostic or nullopt.
std::optional<std::string> haydnCheckParsedBundle(ArrayRef<const MCInst *> Reals,
                                                  unsigned TextEntries,
                                                  const MCInstrInfo &MII,
                                                  const MCRegisterInfo *MRI);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCCHECKER_H
