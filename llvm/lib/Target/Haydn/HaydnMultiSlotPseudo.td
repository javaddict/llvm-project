//===-- HaydnMultiSlotPseudo.td - Multi-slot logicals -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AIE-shaped MultiSlot_Pseudo / materializableInto (port of
// AIE2InstrFormats.td:21-31) plus Haydn LogicalMaterialize bulk coverage.
//
// MultiSlot_Pseudo is a codegen-only logical whose member opcodes are the
// real InstFormat slot forms listed in materializableInto. CodeGenFormat's
// addAlternateInstInMultiSlotPseudo walks that list and emits
// GET_ALTERNATE_INST_OPCODE_FUNC so HaydnMCFormats::getAlternateInstsOpcode
// returns members in materializableInto / slot order — same surface as AIE
// AIEMCFormats.h:376-379.
//
// LogicalMaterialize is the bulk declarative typed alternate relation for
// ordinary logicals (not MultiSlot_Pseudo opcodes). CodeGenFormat emits those
// as sparse size-3 (index==field, 0=hole). Suffix `_S0`/`_S1`/`_S2` name
// discovery is not a product alternate source: any setDesc-safe sparse member
// row without MultiSlot_Pseudo or LogicalMaterialize fails TableGen.
//
// Product identity is Format E composites only (FE8; Size=12 E2/E3).
// PlacementAlternative is a thin view over the emitted vector
// (CompatibleFormatMask / FieldSlots). Format E private members and the
// setDesc ledger remain the post-commit encode authority.
//
// setDesc contract:
// CodeGenFormat rejects MultiSlot_Pseudo materializableInto members that are
// unsafe for unconditional MI.setDesc — explicit operand count/order, def/use/
// tie shape, implicits, semantic flags, and covering itinerary latency/uops.
// LogicalMaterialize drops members that fail structural operand shape (so
// name collisions never reach HR setDesc). Materialize remains
// AIE-unconditional once TableGen has proven the pair.
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
// Format E occupancy alts, not FieldSlots.
// Not selected by ISel; unit/Bundle/ResourceCycle pin.
//===----------------------------------------------------------------------===//
let hasSideEffects = 0, mayLoad = 0, mayStore = 0,
    Itinerary = Slot012_ALU in {
  def ADD32_MSP : MultiSlot_Pseudo<
      (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
      "add32_msp", "$rt, $rs1, $rs2",
      [ADD32_E2_E0_ALU0_RR, ADD32_E3_E1_ALU1_RR, ADD32_E3_E2_ALU2_RR]>;
}


//===----------------------------------------------------------------------===//
// Bulk declarative LogicalMaterialize (typed alternate relation).
// Every multi-slot logical lists its placement members explicitly so
// CodeGenFormat can emit one sparse size-3 alternate table from a typed
// relation. MultiSlot_Pseudo.materializableInto remains the AIE-shaped
// path for true multi-slot pseudos; LogicalMaterialize covers ordinary
// logicals that materialize into Full-format slot members.
//===----------------------------------------------------------------------===//

// Logical -> member opcodes without creating a MultiSlot_Pseudo opcode.
class LogicalMaterialize<Instruction logical,
                         list<Instruction> insts> {
  Instruction BaseLogical = logical;
  list<Instruction> materializableInto = insts;
}

// ADDI32_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// BEQZ_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// BEQ_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// BGEU_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// BGEZ_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// BGE_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// BLTU_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// BLTZ_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// BLT_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// BNEZ_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// BNE_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// CSRW_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// D_LDW_BREV_* FieldSlots retired. Occupancy Mask 0x7 stays.
// CB S1/S2 FieldSlots retired (R13). No Format E e1/e2 member —
// reject-not-migrate. S0 occupancy Fallback stays; e0 members fill index 0.
// D_LDW_CB_IMM FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// D_LDW_CB_REG FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// D_LDW_POST_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LDW_POST_REG FieldSlots retired (R13). Format E fills e0/e1; no e2
// member — S2 is reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_LDW_PRE_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LDW_PRE_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LDW_WITH_* FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LHW_POST_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LHW_POST_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LHW_PRE_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LHW_PRE_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LHW_WITH_* FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LQHWUA_POST FieldSlots retired. Slot2_LS itinerary lives on the logical.
// Format E members fill e0/e1/e2. Occupancy Mask 0x7 stays; Fallback 0.
// D_LTWUA_POST FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// D_LW_POST_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LW_POST_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LW_PRE_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LW_PRE_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LW_WITH_* FieldSlots retired. Occupancy Mask 0x7 stays.
// BREV-store S0 retired. Format E e0 fills index 0. Occupancy Mask 0x7 stays.
// D_SDW_BREV_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SDW_BREV_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SDW_CB S1/S2 FieldSlots retired (R13). No Format E e1/e2 member —
// reject-not-migrate. S0 occupancy Fallback stays.
// D_SDW_CB_IMM FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// D_SDW_CB_REG FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// Store POST/PRE S0 retired. Format E e0 fills index 0. Occupancy Mask 0x7 stays.
// D_SDW_POST_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SDW_POST_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SDW_PRE_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SDW_PRE_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// Store WITH S0 retired. Format E e0 fills index 0. Occupancy Mask 0x7 stays.
// D_SDW_WITH_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SDW_WITH_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SHW_POST_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SHW_POST_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SHW_PRE_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SHW_PRE_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SHW_WITH_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SHW_WITH_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SQHWUA_POST S1/S2 FieldSlots retired (R13). No Format E e1/e2 member —
// reject-not-migrate. S0 occupancy Fallback stays; e0 members fill index 0.
// D_SQHWUA_POST FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// D_STWUA_POST S1/S2 FieldSlots retired. No Format E e1/e2 member —
// reject-not-migrate. e0 members fill index 0. Occupancy Mask 0x7; Fallback S0.
// D_STWUA_POST FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// D_SW_H_POST_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SW_H_POST_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SW_H_PRE_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SW_H_PRE_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SW_H_WITH_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SW_H_WITH_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SW_L_POST_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SW_L_POST_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SW_L_PRE_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SW_L_PRE_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SW_L_WITH_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// D_SW_L_WITH_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// FLAR FieldSlots retired. Occupancy Mask 0x7 stays.
// JALR_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// JAL_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// LD16 FieldSlot retired. Occupancy Mask 0x1 stays.
// LD32 FieldSlots retired. Occupancy Mask 0x3 stays.
// LD32_REG_M0S0LS FieldSlot retired. Occupancy Mask 0x1 stays.
// LD64 FieldSlots retired. Occupancy Mask 0x3 stays.
// LD64_REG_M0S0LS FieldSlot retired. Occupancy Mask 0x1 stays.
// LD8 / LDU16 / LDU8 FieldSlots retired. Occupancy Mask 0x1 stays.
// MOVEI_H / MOVEI_L FieldSlots retired. Occupancy Mask 0x1 stays;
// formatEMemberAtResidualIndex binds MOVEI_*_E2_E0_ALU0_I32.
// NOP FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// ORI32_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// PLDWWUA FieldSlots retired (R13). Logical dag is the golden wire order
// (ar_sel, rs), so occupancy binds PLDWWUA_POST members at e0/e1/e2.
// Occupancy Mask 0x7 stays; Fallback is 0.
// SET_HWLOOP_F2_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// SET_HWLOOP_W FieldSlot occupancy retired. Format E members fill residual indices; no LogicalMaterialize Fallback.
// SEXT_GPR32_TO_DR64 FieldSlot retired. Occupancy Mask 0x2 stays;
// formatEMemberAtResidualIndex peels to SEXT32T64 e1 members.
// ST16 FieldSlot retired. Occupancy Mask 0x1 stays.
// ST32 FieldSlot retired. Occupancy Mask 0x1 stays.
// ST32_POST S1 FieldSlot retired (R13). No Format E e1 member —
// reject-not-migrate. Occupancy peels to S_SW_POST_IMM e0. Mask 0x1.
// ST32_REG_M0S0LS FieldSlot retired. Occupancy Mask 0x1 stays.
// ST64 FieldSlot retired. Occupancy Mask 0x1 stays.
// ST64_POST S1 FieldSlot retired (R13). No Format E e1 member —
// reject-not-migrate. Occupancy peels to D_SDW_POST_IMM e0. Mask 0x1.
// ST64_REG_M0S0LS FieldSlot retired. Occupancy Mask 0x1 stays.
// ST8 FieldSlot retired. Occupancy Mask 0x1 stays.
// S_LBS_POST_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LBS_POST_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LBS_PRE_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LBS_PRE_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LBS_WITH_* FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LBU_POST_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LBU_POST_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LBU_PRE_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LBU_PRE_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LBU_WITH_* FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LHWS_POST_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LHWS_POST_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LHWS_PRE_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LHWS_PRE_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LHWS_WITH_* FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LHWU_POST_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LHWU_POST_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LHWU_PRE_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LHWU_PRE_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LHWU_WITH_* FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LW_BREV_* FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LW_POST_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LW_POST_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LW_PRE_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LW_PRE_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// S_LW_WITH_* FieldSlots retired. Occupancy Mask 0x7 stays.
// S_SB_POST_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SB_POST_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SB_PRE_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SB_PRE_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SB_WITH_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SB_WITH_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SHW_POST_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SHW_POST_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SHW_PRE_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SHW_PRE_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SHW_WITH_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SHW_WITH_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SW_BREV_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SW_BREV_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SW_POST_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SW_POST_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SW_PRE_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SW_PRE_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SW_WITH_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// S_SW_WITH_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. Occupancy Mask 0x7 stays; Fallback 0.
// WBARWUA S1/S2 FieldSlots retired (R13). No Format E e1/e2 member —
// reject-not-migrate. e0 Drop-fills index 0. Occupancy Mask 0x7; Fallback 0.
// WFI FieldSlot occupancy retired. Format E HINT members fill residual indices; no LogicalMaterialize Fallback.
// X2/X4 SFR-only S1 FieldSlots retired. Occupancy Mask 0x6 stays;
// Fallback is 0. Format E 0-def members fill e0/e2.
