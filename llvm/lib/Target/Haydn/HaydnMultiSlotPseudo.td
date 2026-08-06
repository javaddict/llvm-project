//===-- HaydnMultiSlotPseudo.td - Multi-slot logicals -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AIE-shaped MultiSlot_Pseudo / materializableInto (port of
// AIE2InstrFormats.td:21-31). A MultiSlot_Pseudo is a codegen-only logical
// whose member opcodes are the real InstFormat slot forms listed in
// materializableInto. CodeGenFormat's addAlternateInstInMultiSlotPseudo
// (CodeGenFormat.cpp) walks that list and emits GET_ALTERNATE_INST_OPCODE_FUNC
// so HaydnMCFormats::getAlternateInstsOpcode(logical) returns the member
// opcodes in materializableInto / slot order — same surface as AIE
// AIEMCFormats.h:376-379.
//
// Product identity is Format E composites only (FE8; Size=12 E2/E3). Explicit
// MultiSlot_Pseudo defs may be added here over time; until then CodeGenFormat
// also synthesizes the same alternate table from residual `_S0`/`_S1`/`_S2`
// FU members via collectSparseAltSlotMemberRows → GET_ALTERNATE_INST_OPCODE_FUNC
// (alts sparse rows; post-RA setDesc materializes the chosen member onto a
// Format E entry). These `_S*` names are E96 placement alternatives — not
// 16-byte Bundle128 packing (retired). PlacementAlternative is a thin view
// over that vector (CompatibleFormatMask/Priority).
//
// setDesc contract (durable-rule 8e / plan §3.1 §8.4.2):
// CodeGenFormat rejects MultiSlot_Pseudo materializableInto members that are
// unsafe for unconditional MI.setDesc — explicit operand count/order, def/use/
// tie shape, implicits, semantic flags, and covering itinerary latency/uops.
// Sparse `_S*` discovery additionally drops members that fail structural
// operand shape (so name collisions never reach HR setDesc). Materialize
// remains AIE-unconditional once TableGen has proven the pair.
//
//===----------------------------------------------------------------------===//

// Multi-slot logical: materializes into distinct VLIW slot member opcodes.
// Mirrors AIE2InstrFormats.td MultiSlot_Pseudo (AIE2Inst base → HaydnFormatInst).
// Members must each occupy a single InstSlot (CodeGenFormat asserts) and must
// be setDesc-compatible with this logical (CodeGenFormat full gate).
class MultiSlot_Pseudo<dag outs, dag ins, string opcodestr = "",
                       string argstr = "", list<HaydnFormatInst> insts = []>
    : HaydnFormatInst<outs, ins,
                      !if(!eq(argstr, ""),
                          opcodestr # "\t" # argstr,
                          opcodestr),
                      []> {
  let isPseudo = 1;
  bits<1> isMultiSlotPseudo = 1;
  list<HaydnFormatInst> materializableInto = insts;
  let isCodeGenOnly = 1;
  let isComposite = 0;
  // Slot members carry real itineraries; the logical is schedule-neutral
  // until post-RA materializeMultiOpcodeInstrs setDesc. When an itinerary is
 // declared on the logical, requires it to cover every member's
  // OperandCycles/NumMicroOps.
  let isReMaterializable = 0;
}

//===----------------------------------------------------------------------===//
// pin: one real MultiSlot_Pseudo (isPseudo=1) that must still book
// issue/stages/ports. HR uses isNoHazardMetaInstruction (IMPLICIT_DEF/KILL)
// — never blanket MCInstrDesc::isPseudo — so ADD32_MSP is hazard-accounted
// like sparse ADD32 while true meta is not. materializableInto members are
// the product Full-format ADD32_S0/S1/S2 (same setDesc shape as sparse alts;
// full contract). Not selected by ISel; unit/Bundle/ResourceCycle pin.
//===----------------------------------------------------------------------===//
let hasSideEffects = 0, mayLoad = 0, mayStore = 0,
    Itinerary = Slot012_ALU in {
  def ADD32_MSP : MultiSlot_Pseudo<
      (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
      "add32_msp", "$rt, $rs1, $rs2",
      [ADD32_S0, ADD32_S1, ADD32_S2]>;
}
