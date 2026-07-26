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
// Product identity is BUNDLE128_FULL only. Explicit MultiSlot_Pseudo
// defs may be added here over time; until then CodeGenFormat also synthesizes
// the same alternate table from Full-format `_S0`/`_S1`/`_S2` members via
// collectSparseAltSlotMemberRows → GET_ALTERNATE_INST_OPCODE_FUNC
// (alts sparse rows; post-RA setDesc materializes the chosen member).
// PlacementAlternative (HaydnPlacementAlternative.h) is a thin view over
// that vector (CompatibleFormatMask/Priority).
//
//===----------------------------------------------------------------------===//

// Multi-slot logical: materializes into distinct VLIW slot member opcodes.
// Mirrors AIE2InstrFormats.td MultiSlot_Pseudo (AIE2Inst base → HaydnFormatInst).
// Members must each occupy a single InstSlot (CodeGenFormat asserts).
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
  // until post-RA materializeMultiOpcodeInstrs setDesc.
  let isReMaterializable = 0;
}
