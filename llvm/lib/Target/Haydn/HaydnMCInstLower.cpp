//===-- HaydnMCInstLower.cpp - Lower MachineInstr to MCInst ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Desc-as-is lower (AIE serialize-only; AIEMCInstLower.cpp:16-72).
// Opcode is post-setDesc format member when materialize succeeded.
// No FieldSlot conversion and no dangling-MBB rewrite onto the parent —
// removed blocks fatal. Reloc CSRW members lower GlobalAddress as expr;
// encode binds typed CSR I8 (FIXUP_HAYDN_CSR_UImm8 / R_HAYDN_CSR_UImm8).
//
//===----------------------------------------------------------------------===//

#include "HaydnMCInstLower.h"
#include "HaydnAsmPrinter.h"
#include "HaydnFormatERecords.h"
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

static bool isHwloopWideSetup(const MachineInstr &MI) {
  // Inverse / public opcode only. Residual FieldSlot `*_S*` names are not
  // recovered into SET_HWLOOP (AIE MultiSlot alts, AIEMCFormats.h:376-379).
  const unsigned Opc = haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode());
  return Opc == Haydn::SET_HWLOOP_W || Opc == Haydn::SET_HWLOOP_F2_W ||
         Opc == Haydn::SET_HWLOOP || Opc == Haydn::SET_HWLOOP_REG;
}

void HaydnMCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  // Desc-only lower (AIE serialize-only). Opcode is post-setDesc
  // format-member when materialize succeeded; logical residual otherwise
  // (hand-asm / pseudo expand). Placement is member Desc getSlotKind /
  // Format composite (AIEBaseMCFormats.cpp:66-75) — no Flags re-slot.
  OutMI.setOpcode(MI->getOpcode());

  // SET_HWLOOP_{W,F2_W} and setDesc members: operands are
  // (sel, start, end, cnt/rs). Start/end are MBB in MIR; emit uses inclusive
  // temp labels (HWLR_BEGIN = first real of body, HWLR_END = last real of
  // latch). Owned by Lower so BUNDLE/standalone stay pure Desc-as-is (no
  // printer dual-path expand).
  const bool Hwloop = isHwloopWideSetup(*MI);

  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (Hwloop && (i == 1 || i == 2) && MO.isMBB()) {
      const MachineBasicBlock *MBB = MO.getMBB();
      // Compiler-origin SET_HWLOOP start/end must name a live MBB. Rewriting
      // a removed block (number < 0) onto the parent was a silent repair.
      if (!MBB || MBB->getNumber() < 0)
        report_fatal_error(
            "Haydn MCInstLower: SET_HWLOOP start/end MBB is not in the "
            "function — refuse dangling-block repair",
            /*GenCrashDiag=*/false);
      auto &HAP = static_cast<HaydnAsmPrinter &>(Printer);
      MachineBasicBlock *Resolved = const_cast<MachineBasicBlock *>(MBB);
      MCSymbol *Sym = (i == 1) ? HAP.getOrCreateHwloopStartSym(Resolved)
                               : HAP.getOrCreateHwloopEndSym(Resolved);
      OutMI.addOperand(
          MCOperand::createExpr(MCSymbolRefExpr::create(Sym, Ctx)));
      continue;
    }
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
    const MachineBasicBlock *MBB = MO.getMBB();
    // Branch/call MBB operands must name a live block. Rewriting a
    // removed block onto the parent was a silent repair.
    if (!MBB || MBB->getNumber() < 0)
      report_fatal_error(
          "Haydn MCInstLower: MachineBasicBlock operand is not in the "
          "function — refuse dangling-block repair",
          /*GenCrashDiag=*/false);
    const MCExpr *Expr = MCSymbolRefExpr::create(MBB->getSymbol(), Ctx);
    return MCOperand::createExpr(Expr);
  }

  case MachineOperand::MO_GlobalAddress: {
    // Reloc CSR I8 members consume this expr as FIXUP_HAYDN_CSR_UImm8.
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
