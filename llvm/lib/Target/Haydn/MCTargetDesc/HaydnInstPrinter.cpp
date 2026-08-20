//===-- HaydnInstPrinter.cpp - Convert Haydn MCInst to asm syntax ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints an Haydn MCInst to a.s file.
//
//===----------------------------------------------------------------------===//

#include "HaydnInstPrinter.h"
#include "HaydnFormat.h"
#include "HaydnMCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-asm-printer"

// Include the auto-generated portion of the assembly writer.
#define PRINT_ALIAS_INSTR
#include "HaydnGenAsmWriter.inc"

void HaydnInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                 StringRef Annot, const MCSubtargetInfo &STI,
                                 raw_ostream &O) {
  // VLIW / Format E parcels: high-entry-first text
  // `{ e2; e1; e0 }` / `{ e1; e0 }`. Product disasm emits
  // BUNDLE_E96_TWO_ENTRY / BUNDLE_E96_THREE_ENTRY of logical children (public
  // mnemonics, no private member suffix). Generic TargetOpcode::BUNDLE is a
  // non-product composite root (print its isInst children; do not invent a
  // row from child count). Print children via isInst operands — do not rely
  // on generated AsmWriter for multi-entry composites.
  const unsigned Opc = MI->getOpcode();
  if (Opc == Haydn::BUNDLE || Opc == Haydn::BUNDLE_E96_TWO_ENTRY ||
      Opc == Haydn::BUNDLE_E96_THREE_ENTRY) {
    SmallVector<const MCInst *, 3> Children;
    for (unsigned I = 0, E = MI->getNumOperands(); I != E; ++I) {
      const MCOperand &Op = MI->getOperand(I);
      if (Op.isInst() && Op.getInst())
        Children.push_back(Op.getInst());
    }
    // AIE AIECommonInstPrinter.cpp:43-54 prints every composite isInst slot.
    // Haydn overlay: slot count is the stamped row's generated EntryCount
    // (getBundleFormatRow), not child cardinality. Missing children of a
    // stamped row print as nop (AIE empty-slot NOP). Do not drop high-entry
    // NOPs to recover a two-member face. Generic BUNDLE is not a product
    // row — print isInst children only; do not invent TWO vs THREE, and do
    // not collapse an empty composite to a singleton nop.
    unsigned SlotN = 0;
    if (Opc == Haydn::BUNDLE_E96_TWO_ENTRY) {
      if (const haydn::format::BundleFormatRowDesc *Row =
              haydn::format::getBundleFormatRow(
                  haydn::format::BundleFormatRowID::E96TwoEntry))
        SlotN = Row->EntryCount;
    } else if (Opc == Haydn::BUNDLE_E96_THREE_ENTRY) {
      if (const haydn::format::BundleFormatRowDesc *Row =
              haydn::format::getBundleFormatRow(
                  haydn::format::BundleFormatRowID::E96ThreeEntry))
        SlotN = Row->EntryCount;
    }
    const unsigned PrintN = SlotN ? SlotN : Children.size();
    if (PrintN == 0) {
      O << "\t{ }";
      printAnnotation(O, Annot);
      return;
    }
    O << "\t{ ";
    for (unsigned I = PrintN; I-- > 0;) {
      if (I + 1 != PrintN)
        O << "; ";
      if (I < Children.size())
        printSingleInst(Children[I], Address, STI, O);
      else
        O << "nop";
    }
    O << " }";
    printAnnotation(O, Annot);
    return;
  }

  // Non-bundle: single-issue packet, still wrapped in braces for dump parity.
  O << "\t{ ";
  printSingleInst(MI, Address, STI, O);
  O << " }";
  printAnnotation(O, Annot);
}

void HaydnInstPrinter::printSingleInst(const MCInst *MI, uint64_t Address,
                                       const MCSubtargetInfo &STI,
                                       raw_ostream &O) {
  // Special handling for SET_HWLOOP_REG: print as
  // set_hwloop_f2 sel, loop_start, loop_end, rs
  // The MCInst carries: sel(imm), loop_start(expr|imm), loop_end(expr|imm), rs(reg).
  // The expr form comes from the AsmPrinter/asm-parser path (symbolic labels);
  // the imm form comes from the disassembler (dump bytes after RelocLayout
  // ValueShift on HWLR members). Display both expr and imm so offsets stay
  // visible for auditing.
  if (MI->getOpcode() == Haydn::SET_HWLOOP_REG) {
    O << "set_hwloop_f2\t";
    if (MI->getNumOperands() > 0 && MI->getOperand(0).isImm())
      O << MI->getOperand(0).getImm();
    auto printOffset = [&](unsigned OpIdx) {
      if (OpIdx >= MI->getNumOperands())
        return;
      const MCOperand &Op = MI->getOperand(OpIdx);
      if (Op.isExpr()) {
        O << ", ";
        MAI.printExpr(O, *Op.getExpr());
      } else if (Op.isImm()) {
        // Dump bytes (RelocLayout ValueShift already applied on member decode).
        O << ", " << Op.getImm();
      }
    };
    printOffset(1);
    printOffset(2);
    if (MI->getNumOperands() > 3 && MI->getOperand(3).isReg()) {
      O << ", ";
      printRegName(O, MI->getOperand(3).getReg());
    }
    return;
  }

  // Special handling for JAL instruction.
  // Use the enum constant, not a hardcoded number, because tablegen
  // renumbers opcodes when instructions are added/removed.
  if (MI->getOpcode() == Haydn::JAL) {
    O << "jal\t";
    if (MI->getNumOperands() >= 1) {
      const MCOperand &Op0 = MI->getOperand(0);
      if (Op0.isReg())
        printRegName(O, Op0.getReg());
    }
    if (MI->getNumOperands() >= 2) {
      const MCOperand &Op1 = MI->getOperand(1);
      O << ", ";
      if (Op1.isExpr())
        MAI.printExpr(O, *Op1.getExpr());
      else if (Op1.isImm())
        O << Op1.getImm();
    }
    return;
  }

  if (!printAliasInstr(MI, Address, STI, O))
    printInstruction(MI, Address, STI, O);
}

void HaydnInstPrinter::printRegName(raw_ostream &O, MCRegister Reg) {
  // Bounds-safe: generated getRegisterName asserts RegNo!=0 && RegNo in
  // range. Hostile decode or a straddle-filled MCInst can carry NoRegister /
  // an out-of-range id; never abort objdump — print a placeholder instead.
  // The bound is the GENERATED register count — a literal here silently
  // banished every register whose enum value moved when AR2/AR3 came back:
  // lr printed as <?> in every epilogue.
  unsigned RegNo = Reg.id();
  if (RegNo == 0 || RegNo >= Haydn::NUM_TARGET_REGS) {
    O << "<?>";
    return;
  }
  O << getRegisterName(Reg);
}

void HaydnInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                    const MCSubtargetInfo &STI,
                                    raw_ostream &O) {
  // Defensive bounds check: the generated printInstruction indexes operands
  // by fixed OpNo per the AsmWriter string. A malformed MCInst produced by a
  // straddle read in the disassembler (e.g. a D-class bundle decoded from
  // cross-boundary bytes at a +6 cursor, or a slot decoder that filled fewer
  // operands than the printer expects) has fewer operands than printInstruction
  // indexes. Without this guard, getOperand(OpNo) triggers SmallVector's
  // `idx < size` assertion and aborts objdump (dct4/fft/firinterp/ifft
  // NatureDSP objects). Print a placeholder and continue rather than crashing.
  // A well-formed decode never reaches this branch.
  if (OpNo >= MI->getNumOperands()) {
    O << "<?>";
    return;
  }
  const MCOperand &MO = MI->getOperand(OpNo);

  // Composite/packet entry sub-instruction (MCOperand::isInst): recurse into
  // the child printer. Empty sub-MCInst (opcode 0) prints as "nop" rather than
  // entering printInstruction (which would assert on Bits==0). Bounds-safe
  // defense: a genuinely malformed entry still renders benignly.
  if (MO.isInst()) {
    const MCInst *SubInst = MO.getInst();
    if (SubInst && SubInst->getOpcode() != 0)
      printSingleInst(SubInst, /*Address=*/0, STI, O);
    else
      O << "nop";
    return;
  }

  if (MO.isReg()) {
    printRegName(O, MO.getReg());
    return;
  }

  if (MO.isImm()) {
    O << MO.getImm();
    return;
  }

  assert(MO.isExpr() && "Unknown operand kind in printOperand");
  MAI.printExpr(O, *MO.getExpr());
}
