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
#include "HaydnBundlePlan.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnMemberSetDesc.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include <string>

using namespace llvm;

#define GET_FORMAT_E_MEMBER_OPCODES
#include "HaydnGenFormatEMemberOpcodes.inc"

#define DEBUG_TYPE "haydn-mcinstlower"

/// `_MSP` clones encode as the catalog logical's generated member at the
/// already-stamped (Mode, EntryIdx). Table identity, not occupancy DFS.
/// Standalone clones take the closed-singleton E2 entry 0 member
/// (AIEMachineScheduler.cpp:1126-1132 setDesc of the selected alt; Haydn
/// overlay keeps the clone in MIR so CFG flags survive BranchRelaxation).
static unsigned haydnMemberOpcodeForLogicalModeEntry(StringRef Logical,
                                                     uint8_t Mode,
                                                     unsigned EntryIdx) {
  for (unsigned I = 0; I < haydn::format_e::FormatEMemberCount; ++I) {
    const haydn::format_e::FormatEMemberRec &Mem =
        haydn::format_e::FormatEMembers[I];
    if (Mem.IsNop || Mem.Mode != Mode || Mem.EntryIdx != EntryIdx)
      continue;
    if (!Mem.Logical || !StringRef(Mem.Logical).equals_insensitive(Logical))
      continue;
    if (I < FormatEMemberOpcodeCount && FormatEMemberOpcodes[I])
      return FormatEMemberOpcodes[I];
  }
  return 0;
}

static unsigned haydnMemberOpcodeForMspClone(const MachineInstr &MI) {
  const unsigned Opc = MI.getOpcode();
  StringRef Logical;
  if (Opc == Haydn::BEQZ_W_MSP)
    Logical = "BEQZ";
  else if (Opc == Haydn::JALR_MSP || Opc == Haydn::JALR_W_MSP)
    Logical = "JALR";
  else if (Opc == Haydn::JAL_W_MSP)
    Logical = "JAL";
  else
    return Opc;

  uint8_t Mode = 0;
  unsigned PreferEntry = 0;
  uint8_t UsedEntries = 0;
  unsigned Cap = 2;
  if (MI.isInsideBundle()) {
    const MachineInstr &Root = *getBundleStart(MI.getIterator());
    if (auto Row = haydn::bundle::getBundleRowID(Root)) {
      using haydn::format::BundleFormatRowID;
      Mode = (*Row == BundleFormatRowID::E96ThreeEntry) ? 1 : 0;
    }
    Cap = Mode ? 3u : 2u;
    unsigned Pos = 0;
    for (const MachineInstr *C : haydn::bundle::members(Root)) {
      if (!C)
        continue;
      if (C == &MI) {
        PreferEntry = Pos;
        ++Pos;
        continue;
      }
      if (const haydn::format_e::FormatEMemberRec *Mem =
              haydn::bundle::lookupPrivateFormatEMember(C->getOpcode())) {
        if (Mem->EntryIdx < 8)
          UsedEntries |= static_cast<uint8_t>(1u << Mem->EntryIdx);
      }
      ++Pos;
    }
  }

  unsigned Entry = PreferEntry;
  if (Entry >= Cap || (UsedEntries & (1u << Entry))) {
    Entry = Cap;
    for (unsigned E = 0; E < Cap; ++E) {
      if (!(UsedEntries & (1u << E))) {
        Entry = E;
        break;
      }
    }
  }
  if (Entry >= Cap)
    report_fatal_error(
        "Haydn MCInstLower: `_MSP` clone has no free Format E entry in the "
        "stamped row — refuse first-member occupancy invent",
        /*GenCrashDiag=*/false);
  const unsigned Member =
      haydnMemberOpcodeForLogicalModeEntry(Logical, Mode, Entry);
  if (!Member)
    report_fatal_error(
        Twine("Haydn MCInstLower: no generated ") + Logical +
            " member at mode " + Twine(static_cast<unsigned>(Mode)) +
            " entry " + Twine(Entry) + " for `_MSP` clone",
        /*GenCrashDiag=*/false);
  return Member;
}

static bool isHwloopWideSetup(const MachineInstr &MI) {
  // Wide-setup lowering law — NOT family-identical (see
  // haydnClassifyHwloopSetupOpcode): (Residual ∪ Expanded) minus
  // SET_HWLOOP_REG_W / bare SET_HWLOOP_F2 (no MBB operand rewrite path)
  // and minus LoopStart (never lowers — expanders must install the final
  // SET first). Membership differs per-opcode by operand shape, so keep
  // explicit. Inverse / public opcode only. Residual FieldSlot `*_S*`
  // names are not recovered into SET_HWLOOP (AIE MultiSlot alts,
  // AIEMCFormats.h:376-379).
  const unsigned Opc = haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode());
  return Opc == Haydn::SET_HWLOOP_W || Opc == Haydn::SET_HWLOOP_F2_W ||
         Opc == Haydn::SET_HWLOOP || Opc == Haydn::SET_HWLOOP_REG;
}

void HaydnMCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  // Desc-only lower (AIE serialize-only). Opcode is post-setDesc
  // format-member when materialize succeeded; logical residual otherwise
  // (hand-asm / pseudo expand). Placement is member Desc getSlotKind /
  // Format composite (AIEBaseMCFormats.cpp:66-75) — no Flags re-slot.
  OutMI.setOpcode(haydnMemberOpcodeForMspClone(*MI));

  // Reloc CSR I8 must already be a generated member. A leftover
  // CSRW_W/CSRR FieldSlot would miss findFixupFromFixupFields I8
  // type-opcodes 4/5 and emit untyped NONE. AIE applyFixup is member-Desc
  // fields (AIEMCFixupKinds.cpp:36-65); Haydn overlay refuses the FieldSlot
  // here instead of inventing a specifier. peelLogicalOpcodeName is a
  // detect-and-refuse wall, not a repair.
  if (const MachineFunction *MF =
          MI->getParent() ? MI->getParent()->getParent() : nullptr) {
    const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();
    const StringRef Name = TII.getName(MI->getOpcode());
    if (!isGeneratedFormatEMemberName(Name)) {
      const std::string Log =
          haydn::format_e::peelLogicalOpcodeName(Name);
      if (StringRef(Log).equals_insensitive("CSRW") ||
          StringRef(Log).equals_insensitive("CSRR")) {
        for (const MachineOperand &MO : MI->explicit_operands()) {
          if (MO.isGlobal() || MO.isSymbol() || MO.isMCSymbol() ||
              MO.isBlockAddress() || MO.isCPI() || MO.isJTI() ||
              MO.isTargetIndex())
            report_fatal_error(
                "Haydn MCInstLower: reloc CSR I8 remained FieldSlot — "
                "refuse untyped NONE fixup",
                /*GenCrashDiag=*/false);
        }
      }
    }
  }

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
