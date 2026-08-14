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
#include "HaydnFormatERecords.h"
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
  // `{ e2; e1; e0 }` / `{ e1; e0 }` (CB-142 / #10). Product disasm emits
  // BUNDLE_E96_TWO_ENTRY / BUNDLE_E96_THREE_ENTRY of logical children (public
  // mnemonics, no private member suffix). Generic TargetOpcode::BUNDLE is a
  // fallback for non-product composite roots. Print children via isInst
  // operands — do not rely on generated AsmWriter for multi-entry composites
  // (avoids empty / <unknown>-adjacent dumps).
  const unsigned Opc = MI->getOpcode();
  if (Opc == Haydn::BUNDLE || Opc == Haydn::BUNDLE_E96_TWO_ENTRY ||
      Opc == Haydn::BUNDLE_E96_THREE_ENTRY) {
    SmallVector<const MCInst *, 3> Children;
    for (unsigned I = 0, E = MI->getNumOperands(); I != E; ++I) {
      const MCOperand &Op = MI->getOperand(I);
      if (Op.isInst() && Op.getInst())
        Children.push_back(Op.getInst());
    }
    if (Children.empty()) {
      // Composite with no children: still emit a braced nop so the line is
      // never `<unknown>` / empty for a successfully decoded parcel.
      O << "\t{ nop }";
      printAnnotation(O, Annot);
      return;
    }
        // Drop high-entry architectural NOP pads from the print face only when
    // at least two real members remain. Dual single-unit packs commit as E3
    // (unit cover) with a high-entry NOP pad for encode; the historical
    // two-member brace face stays stable for dumps/FileCheck. Singleton E2
    // size==2 `{ nop; real }` is preserved. Encode remains full-slot.
    auto isPrintNop = [&](const MCInst *C) {
      if (!C)
        return true;
      const unsigned CO = C->getOpcode();
      if (CO == Haydn::NOP)
        return true;
      return StringRef(haydn::format_e::peelLogicalOpcodeName(MII.getName(CO)))
          .equals_insensitive("NOP");
    };
    while (Children.size() > 2 && isPrintNop(Children.back()))
      Children.pop_back();
// High entry first (`{ e2; e1; e0 }` / `{ e1; e0 }`), matching the
    // BUNDLE_E96_* AsmStrings and the ISA bundle spelling (CB-142 / #10).
    // Children[] is still encode-order e0..eN from the composite operand dag.
    O << "\t{ ";
    for (unsigned I = Children.size(); I-- > 0;) {
      if (I + 1 != Children.size())
        O << "; ";
      printSingleInst(Children[I], Address, STI, O);
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
  // the imm form comes from the disassembler (decoded raw offset field values
  // in WORDS — i.e. the encoded off16 field, already divided by 4 from bytes).
  // We display both forms so the offsets are always visible for auditing. See
  // (Bug C — previously the imm form was silently dropped because only
  // isExpr was checked, making objdump show "set_hwloop_f2 1, r12" with no
  // offsets, hiding the START/END correctness bug).
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
        // Disassembler path: the immediate is the encoded off16 field value
        // (word offset). Display as "<N>w" to make clear it is a word offset
        // and also show the byte equivalent for debugging.
        int64_t WordOff = Op.getImm();
        O << ", <off" << WordOff << "w=" << (WordOff * 4) << "B>";
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
  // Bounds-safe: generated getRegisterName asserts RegNo!=0 && RegNo<39.
  // Hostile decode or a straddle-filled MCInst can carry NoRegister / an
  // out-of-range id; never abort objdump — print a placeholder instead.
  unsigned RegNo = Reg.id();
  if (RegNo == 0 || RegNo >= 39) {
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
