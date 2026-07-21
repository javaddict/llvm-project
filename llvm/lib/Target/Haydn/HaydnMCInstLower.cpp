//===-- HaydnMCInstLower.cpp - Lower MachineInstr to MCInst ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower Haydn MachineInstrs to their corresponding
// MCInst.
//
//===----------------------------------------------------------------------===//

#include "HaydnMCInstLower.h"
#include "HaydnAsmPrinter.h"
#include "HaydnMachineFunctionInfo.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-mcinstlower"

void HaydnMCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  // Logical opcode only. Flex `_S*` forms materialize at encode.
  OutMI.setOpcode(MI->getOpcode());

  // placement slot → HaydnMCFlags. Single public slot authority.
  // Solitary S0|S1 loads are forced to S0 in encodeBundle128 (matches llvm-mc).
  if (Printer.MF) {
    if (const auto *FuncInfo =
            Printer.MF->getInfo<HaydnMachineFunctionInfo>()) {
      const HaydnAlternateDescriptors &AltDescs = FuncInfo->getAltDescs();
      if (auto Slot =
              AltDescs.getSelectedSlot(const_cast<MachineInstr *>(MI)))
        HaydnMCFlags::setHaydnSlot(OutMI, *Slot);
    }
  }

  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    MCOperand MCOp = LowerOperand(MO);

    if (MCOp.isValid())
      OutMI.addOperand(MCOp);
  }
}

MCOperand HaydnMCInstLower::LowerOperand(const MachineOperand &MO) const {
  switch (MO.getType()) {
  default:
    llvm_unreachable("unknown operand type");

  case MachineOperand::MO_Register:
    // Ignore all implicit register operands
    if (MO.isImplicit())
      return MCOperand();
    return MCOperand::createReg(MO.getReg());

  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());

  case MachineOperand::MO_MachineBasicBlock: {
    const MCExpr *Expr = MCSymbolRefExpr::create(
        MO.getMBB()->getSymbol(), Ctx);
    return MCOperand::createExpr(Expr);
  }

  case MachineOperand::MO_GlobalAddress: {
    const MCExpr *Expr = MCSymbolRefExpr::create(
        Printer.getSymbol(MO.getGlobal()), Ctx);
    if (MO.getOffset()) {
      Expr = MCBinaryExpr::createAdd(
          Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
    }
    return MCOperand::createExpr(Expr);
  }

  case MachineOperand::MO_ExternalSymbol: {
    const MCExpr *Expr = MCSymbolRefExpr::create(
        Printer.GetExternalSymbolSymbol(MO.getSymbolName()), Ctx);
    return MCOperand::createExpr(Expr);
  }

  case MachineOperand::MO_JumpTableIndex: {
    const MCExpr *Expr = MCSymbolRefExpr::create(
        Printer.GetJTISymbol(MO.getIndex()), Ctx);
    return MCOperand::createExpr(Expr);
  }

  case MachineOperand::MO_ConstantPoolIndex: {
    const MCExpr *Expr = MCSymbolRefExpr::create(
        Printer.GetCPISymbol(MO.getIndex()), Ctx);
    if (MO.getOffset()) {
      Expr = MCBinaryExpr::createAdd(
          Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
    }
    return MCOperand::createExpr(Expr);
  }

  case MachineOperand::MO_BlockAddress: {
    const MCExpr *Expr = MCSymbolRefExpr::create(
        Printer.GetBlockAddressSymbol(MO.getBlockAddress()), Ctx);
    if (MO.getOffset()) {
      Expr = MCBinaryExpr::createAdd(
          Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
    }
    return MCOperand::createExpr(Expr);
  }

  case MachineOperand::MO_RegisterMask:
    // Register masks are implicitly handled, no operand needed
    return MCOperand();

  case MachineOperand::MO_FPImmediate:
    // FP immediates are not directly supported in assembly
    llvm_unreachable("FP immediate operands should be lowered to constants");

  case MachineOperand::MO_ShuffleMask:
    // Shuffle masks are pseudo-opcodes for GISel, shouldn't reach here
    llvm_unreachable("Shuffle mask operands should not reach MC lowering");

  case MachineOperand::MO_Metadata:
    // Metadata operands should not reach MC lowering
    llvm_unreachable("Metadata operands should not reach MC lowering");

  case MachineOperand::MO_CFIIndex:
    // CFI indices are handled separately by AsmPrinter
    return MCOperand();

  case MachineOperand::MO_IntrinsicID:
  case MachineOperand::MO_Predicate:
  case MachineOperand::MO_TargetIndex:
    // These should not appear in real instructions
    return MCOperand();
  }

  return MCOperand();
}
