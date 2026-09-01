//===-- HaydnMCInstLower.h - Lower MachineInstr to MCInst -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNMCINSTLOWER_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNMCINSTLOWER_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class AsmPrinter;
class MCInst;
class MachineInstr;
class MachineOperand;
class MCContext;
class MCOperand;

class LLVM_LIBRARY_VISIBILITY HaydnMCInstLower {
  MCContext &Ctx;
  AsmPrinter &Printer;

public:
  HaydnMCInstLower(MCContext &ctx, AsmPrinter &printer)
      : Ctx(ctx), Printer(printer) {}

  /// Desc-as-is lower plus HWLoop MBB→inclusive-label rewrite.
  /// Leftover CSR FieldSlot names are a refuse wall, not a repair.
  void Lower(const MachineInstr *MI, MCInst &OutMI) const;

  MCOperand LowerOperand(const MachineOperand &MO) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNMCINSTLOWER_H
