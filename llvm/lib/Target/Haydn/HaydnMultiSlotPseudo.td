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

def : LogicalMaterialize<ADDI32_W, [ADDI32_W_S0]>;
def : LogicalMaterialize<BEQZ_W, [BEQZ_W_S0]>;
def : LogicalMaterialize<BEQ_W, [BEQ_W_S0]>;
def : LogicalMaterialize<BGEU_W, [BGEU_W_S0]>;
def : LogicalMaterialize<BGEZ_W, [BGEZ_W_S0]>;
def : LogicalMaterialize<BGE_W, [BGE_W_S0]>;
def : LogicalMaterialize<BLTU_W, [BLTU_W_S0]>;
def : LogicalMaterialize<BLTZ_W, [BLTZ_W_S0]>;
def : LogicalMaterialize<BLT_W, [BLT_W_S0]>;
def : LogicalMaterialize<BNEZ_W, [BNEZ_W_S0]>;
def : LogicalMaterialize<BNE_W, [BNE_W_S0]>;
def : LogicalMaterialize<CSRW_W, [CSRW_W_S0]>;
// D_LDW_BREV_* FieldSlots retired. Occupancy Mask 0x7 stays.
def : LogicalMaterialize<D_LDW_CB_IMM, [D_LDW_CB_IMM_S0, D_LDW_CB_IMM_S1, D_LDW_CB_IMM_S2]>;
def : LogicalMaterialize<D_LDW_CB_REG, [D_LDW_CB_REG_S0, D_LDW_CB_REG_S1, D_LDW_CB_REG_S2]>;
// D_LDW_POST_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LDW_POST_REG S0/S1 retired. Format E fills e0/e1. Occupancy Mask 0x7 stays.
def : LogicalMaterialize<D_LDW_POST_REG, [D_LDW_POST_REG_S2]>;
// D_LDW_PRE_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LDW_PRE_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LDW_WITH_* FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LHW_POST_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LHW_POST_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LHW_PRE_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LHW_PRE_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LHW_WITH_* FieldSlots retired. Occupancy Mask 0x7 stays.
def : LogicalMaterialize<D_LQHWUA_POST, [D_LQHWUA_POST_S0, D_LQHWUA_POST_S1, D_LQHWUA_POST_S2]>;
def : LogicalMaterialize<D_LTWUA_POST, [D_LTWUA_POST_S0, D_LTWUA_POST_S1, D_LTWUA_POST_S2]>;
// D_LW_POST_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LW_POST_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LW_PRE_IMM FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LW_PRE_REG FieldSlots retired. Occupancy Mask 0x7 stays.
// D_LW_WITH_* FieldSlots retired. Occupancy Mask 0x7 stays.
// BREV-store S0 retired. Format E e0 fills index 0. Occupancy Mask 0x7 stays.
def : LogicalMaterialize<D_SDW_BREV_IMM, [D_SDW_BREV_IMM_S1, D_SDW_BREV_IMM_S2]>;
def : LogicalMaterialize<D_SDW_BREV_REG, [D_SDW_BREV_REG_S1, D_SDW_BREV_REG_S2]>;
def : LogicalMaterialize<D_SDW_CB_IMM, [D_SDW_CB_IMM_S0, D_SDW_CB_IMM_S1, D_SDW_CB_IMM_S2]>;
def : LogicalMaterialize<D_SDW_CB_REG, [D_SDW_CB_REG_S0, D_SDW_CB_REG_S1, D_SDW_CB_REG_S2]>;
// Store POST/PRE S0 retired. Format E e0 fills index 0. Occupancy Mask 0x7 stays.
def : LogicalMaterialize<D_SDW_POST_IMM, [D_SDW_POST_IMM_S1, D_SDW_POST_IMM_S2]>;
def : LogicalMaterialize<D_SDW_POST_REG, [D_SDW_POST_REG_S1, D_SDW_POST_REG_S2]>;
def : LogicalMaterialize<D_SDW_PRE_IMM, [D_SDW_PRE_IMM_S1, D_SDW_PRE_IMM_S2]>;
def : LogicalMaterialize<D_SDW_PRE_REG, [D_SDW_PRE_REG_S1, D_SDW_PRE_REG_S2]>;
// Store WITH S0 retired. Format E e0 fills index 0. Occupancy Mask 0x7 stays.
def : LogicalMaterialize<D_SDW_WITH_IMM, [D_SDW_WITH_IMM_S1, D_SDW_WITH_IMM_S2]>;
def : LogicalMaterialize<D_SDW_WITH_REG, [D_SDW_WITH_REG_S1, D_SDW_WITH_REG_S2]>;
def : LogicalMaterialize<D_SHW_POST_IMM, [D_SHW_POST_IMM_S1, D_SHW_POST_IMM_S2]>;
def : LogicalMaterialize<D_SHW_POST_REG, [D_SHW_POST_REG_S1, D_SHW_POST_REG_S2]>;
def : LogicalMaterialize<D_SHW_PRE_IMM, [D_SHW_PRE_IMM_S1, D_SHW_PRE_IMM_S2]>;
def : LogicalMaterialize<D_SHW_PRE_REG, [D_SHW_PRE_REG_S1, D_SHW_PRE_REG_S2]>;
def : LogicalMaterialize<D_SHW_WITH_IMM, [D_SHW_WITH_IMM_S1, D_SHW_WITH_IMM_S2]>;
def : LogicalMaterialize<D_SHW_WITH_REG, [D_SHW_WITH_REG_S1, D_SHW_WITH_REG_S2]>;
def : LogicalMaterialize<D_SQHWUA_POST, [D_SQHWUA_POST_S0, D_SQHWUA_POST_S1, D_SQHWUA_POST_S2]>;
def : LogicalMaterialize<D_STWUA_POST, [D_STWUA_POST_S0, D_STWUA_POST_S1, D_STWUA_POST_S2]>;
def : LogicalMaterialize<D_SW_H_POST_IMM, [D_SW_H_POST_IMM_S1, D_SW_H_POST_IMM_S2]>;
def : LogicalMaterialize<D_SW_H_POST_REG, [D_SW_H_POST_REG_S1, D_SW_H_POST_REG_S2]>;
def : LogicalMaterialize<D_SW_H_PRE_IMM, [D_SW_H_PRE_IMM_S1, D_SW_H_PRE_IMM_S2]>;
def : LogicalMaterialize<D_SW_H_PRE_REG, [D_SW_H_PRE_REG_S1, D_SW_H_PRE_REG_S2]>;
def : LogicalMaterialize<D_SW_H_WITH_IMM, [D_SW_H_WITH_IMM_S1, D_SW_H_WITH_IMM_S2]>;
def : LogicalMaterialize<D_SW_H_WITH_REG, [D_SW_H_WITH_REG_S1, D_SW_H_WITH_REG_S2]>;
def : LogicalMaterialize<D_SW_L_POST_IMM, [D_SW_L_POST_IMM_S1, D_SW_L_POST_IMM_S2]>;
def : LogicalMaterialize<D_SW_L_POST_REG, [D_SW_L_POST_REG_S1, D_SW_L_POST_REG_S2]>;
def : LogicalMaterialize<D_SW_L_PRE_IMM, [D_SW_L_PRE_IMM_S1, D_SW_L_PRE_IMM_S2]>;
def : LogicalMaterialize<D_SW_L_PRE_REG, [D_SW_L_PRE_REG_S1, D_SW_L_PRE_REG_S2]>;
def : LogicalMaterialize<D_SW_L_WITH_IMM, [D_SW_L_WITH_IMM_S1, D_SW_L_WITH_IMM_S2]>;
def : LogicalMaterialize<D_SW_L_WITH_REG, [D_SW_L_WITH_REG_S1, D_SW_L_WITH_REG_S2]>;
// FLAR FieldSlots retired. Occupancy Mask 0x7 stays.
def : LogicalMaterialize<JALR_W, [JALR_W_S0]>;
def : LogicalMaterialize<JAL_W, [JAL_W_S0]>;
// LD16 FieldSlot retired. Occupancy Mask 0x1 stays.
// LD32 FieldSlots retired. Occupancy Mask 0x3 stays.
// LD32_REG_M0S0LS FieldSlot retired. Occupancy Mask 0x1 stays.
// LD64 FieldSlots retired. Occupancy Mask 0x3 stays.
// LD64_REG_M0S0LS FieldSlot retired. Occupancy Mask 0x1 stays.
// LD8 / LDU16 / LDU8 FieldSlots retired. Occupancy Mask 0x1 stays.
// MOVEI_H / MOVEI_L FieldSlots retired. Occupancy Mask 0x1 stays;
// formatEMemberAtResidualIndex binds MOVEI_*_E2_E0_ALU0_I32.
def : LogicalMaterialize<NOP, [NOP_S0]>;
def : LogicalMaterialize<ORI32_W, [ORI32_W_S0]>;
def : LogicalMaterialize<PLDWWUA, [PLDWWUA_S0, PLDWWUA_S1, PLDWWUA_S2]>;
def : LogicalMaterialize<SET_HWLOOP_F2_W, [SET_HWLOOP_F2_W_S0]>;
def : LogicalMaterialize<SET_HWLOOP_W, [SET_HWLOOP_W_S0]>;
// SEXT_GPR32_TO_DR64 FieldSlot retired. Occupancy Mask 0x2 stays;
// formatEMemberAtResidualIndex peels to SEXT32T64 e1 members.
// ST16 FieldSlot retired. Occupancy Mask 0x1 stays.
// ST32 FieldSlot retired. Occupancy Mask 0x1 stays.
def : LogicalMaterialize<ST32_POST, [ST32_POST_S1]>;
// ST32_REG_M0S0LS FieldSlot retired. Occupancy Mask 0x1 stays.
// ST64 FieldSlot retired. Occupancy Mask 0x1 stays.
def : LogicalMaterialize<ST64_POST, [ST64_POST_S1]>;
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
def : LogicalMaterialize<S_SB_POST_IMM, [S_SB_POST_IMM_S1, S_SB_POST_IMM_S2]>;
def : LogicalMaterialize<S_SB_POST_REG, [S_SB_POST_REG_S1, S_SB_POST_REG_S2]>;
def : LogicalMaterialize<S_SB_PRE_IMM, [S_SB_PRE_IMM_S1, S_SB_PRE_IMM_S2]>;
def : LogicalMaterialize<S_SB_PRE_REG, [S_SB_PRE_REG_S1, S_SB_PRE_REG_S2]>;
def : LogicalMaterialize<S_SB_WITH_IMM, [S_SB_WITH_IMM_S1, S_SB_WITH_IMM_S2]>;
def : LogicalMaterialize<S_SB_WITH_REG, [S_SB_WITH_REG_S1, S_SB_WITH_REG_S2]>;
def : LogicalMaterialize<S_SHW_POST_IMM, [S_SHW_POST_IMM_S1, S_SHW_POST_IMM_S2]>;
def : LogicalMaterialize<S_SHW_POST_REG, [S_SHW_POST_REG_S1, S_SHW_POST_REG_S2]>;
def : LogicalMaterialize<S_SHW_PRE_IMM, [S_SHW_PRE_IMM_S1, S_SHW_PRE_IMM_S2]>;
def : LogicalMaterialize<S_SHW_PRE_REG, [S_SHW_PRE_REG_S1, S_SHW_PRE_REG_S2]>;
def : LogicalMaterialize<S_SHW_WITH_IMM, [S_SHW_WITH_IMM_S1, S_SHW_WITH_IMM_S2]>;
def : LogicalMaterialize<S_SHW_WITH_REG, [S_SHW_WITH_REG_S1, S_SHW_WITH_REG_S2]>;
def : LogicalMaterialize<S_SW_BREV_IMM, [S_SW_BREV_IMM_S1, S_SW_BREV_IMM_S2]>;
def : LogicalMaterialize<S_SW_BREV_REG, [S_SW_BREV_REG_S1, S_SW_BREV_REG_S2]>;
def : LogicalMaterialize<S_SW_POST_IMM, [S_SW_POST_IMM_S1, S_SW_POST_IMM_S2]>;
def : LogicalMaterialize<S_SW_POST_REG, [S_SW_POST_REG_S1, S_SW_POST_REG_S2]>;
def : LogicalMaterialize<S_SW_PRE_IMM, [S_SW_PRE_IMM_S1, S_SW_PRE_IMM_S2]>;
def : LogicalMaterialize<S_SW_PRE_REG, [S_SW_PRE_REG_S1, S_SW_PRE_REG_S2]>;
def : LogicalMaterialize<S_SW_WITH_IMM, [S_SW_WITH_IMM_S1, S_SW_WITH_IMM_S2]>;
def : LogicalMaterialize<S_SW_WITH_REG, [S_SW_WITH_REG_S1, S_SW_WITH_REG_S2]>;
// WBARWUA S0 retired. Format E e0 Drop-fills index 0. Occupancy Mask 0x7 stays.
def : LogicalMaterialize<WBARWUA, [WBARWUA_S1, WBARWUA_S2]>;
def : LogicalMaterialize<WFI, [WFI_S0]>;
def : LogicalMaterialize<X2SEQ32, [X2SEQ32_S1]>;
def : LogicalMaterialize<X2SLE32, [X2SLE32_S1]>;
def : LogicalMaterialize<X2SLT32, [X2SLT32_S1]>;
def : LogicalMaterialize<X4SEQ16, [X4SEQ16_S1]>;
def : LogicalMaterialize<X4SLE16, [X4SLE16_S1]>;
def : LogicalMaterialize<X4SLT16, [X4SLT16_S1]>;
