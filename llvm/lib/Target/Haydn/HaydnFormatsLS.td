// LS CBRI in S0 (D_LDW_CB_IMM / D_SDW_CB_IMM): cbr_sel(uimm2), rtd(DR64)
// rs(GPR32 base), imm8(simm8).
// Payload 3+7+20+2+8+4+4 = 48b.
class HaydnFU_LS_S0_CBRI<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<20> reserved = 0;     // [37:18] unused
  bits<2> cbr_sel;           // circular-buffer register selector
  bits<8> imm8;              // signed stride (simm8)
  bits<4> rtd;               // DR64 (HIGHEST; dest for LD, source for SD)
  bits<4> rs;                // GPR32 base (LOWEST)
  let s0 = {FU, opcode, reserved, cbr_sel, imm8, rtd, rs};  // s0[47:0]
}

// LS CBRR in S0 (D_LDW_CB_REG / D_SDW_CB_REG): cbr_sel(uimm2), rtd(DR64)
// rs1, rs2 (GPR32).
// Payload 3+7+24+2+4+4+4 = 48b.
class HaydnFU_LS_S0_CBRR<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<24> reserved = 0;     // [37:14] unused
  bits<2> cbr_sel;
  bits<4> rtd;               // DR64 (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s0 = {FU, opcode, reserved, cbr_sel, rtd, rs2, rs1};  // s0[47:0]
}

// LS PLDWWUA in S0 (AR type): ar_sel(uimm2), rs(GPR32). Pre-load AR.
// Payload 3+7+32+2+4 = 48b.

// LS D_LQHWUA_POST / D_LTWUA_POST / D_SQHWUA_POST / D_STWUA_POST in S0
// (AR type): rtd(DR64), ar_sel(uimm2), rs1, rs2 (GPR32), dir_sel(uimm1).
// Payload 3+7+22+1(dir)+2(ar)+4+4+4+1 = 48b.
// Re-split: reserved23 + rtd(4) + ar_sel(2) + rs2(4) + rs1(4) + dir_sel(1)
// + reserved_pad(6) -> 3+7+23+4+2+4+4+1 = 48b. Add pad0 to fill.
// Simpler: reserved22 + rtd(4) + ar_sel(2) + rs2(4) + rs1(4) + dir_sel(1)
// + pad0(3) = 3+7+22+4+2+4+4+1+3 = 50b -- too many.
// Final split: FU(3)+opcode(7)+reserved(20)+rtd(4)+ar_sel(2)+rs2(4)+rs1(4)
// +dir_sel(1)+pad(3) = 3+7+20+4+2+4+4+1+3 = 48b.
class HaydnFU_LS_S0_AR_UA<bits<7> opc, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<20> reserved = 0;     // [37:18] unused
  bits<4> rtd;               // DR64 (HIGHEST; dest for L*, source for S*)
  bits<2> ar_sel;
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  bits<1> dir_sel;
  bits<3> pad0 = 0;          // [2:0] fill to 48b
  let s0 = {FU, opcode, reserved, rtd, ar_sel, rs2, rs1, dir_sel, pad0};  // s0[47:0]
}

// LS D_ST_RI6 in S1 (D_SD_* / D_SW_* / D_SHW_* family): rtd(DR64 SOURCE)
// rs(GPR32 base), imm6.
// Payload 3+7+16+6+4+4 = 40b.
class HaydnFU_LS_S1_D_ST_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rtd;               // DR64 source (HIGHEST)
  let s1 = {FU, opcode, reserved, imm6, rtd, rs};  // MSB-first -> s1[39:0]
}

// LS S_ST_RI6 in S1 (S_SW_* / S_SHW_* / S_SB_* family): rt(GPR32 SOURCE)
// rs(GPR32 base), imm6.
// Payload 3+7+16+6+4+4 = 40b.
class HaydnFU_LS_S1_S_ST_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rt;                // GPR32 source (HIGHEST)
  let s1 = {FU, opcode, reserved, imm6, rt, rs};   // MSB-first -> s1[39:0]
}

// LS D_ST_RR in S1 (D_SD_* / D_SW_* / D_SHW_* family): rtd(DR64 SOURCE)
// rs1, rs2 (GPR32).
// Payload 3+7+18+4+4+4 = 40b.
class HaydnFU_LS_S1_D_ST_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<18> reserved = 0;     // [29:12] unused
  bits<4> rtd;               // DR64 source (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s1 = {FU, opcode, reserved, rtd, rs2, rs1};  // MSB-first -> s1[39:0]
}

// LS S_ST_RR in S1 (S_SW_* / S_SHW_* / S_SB_* family): rt(GPR32 SOURCE)
// rs1, rs2 (GPR32).
// Payload 3+7+18+4+4+4 = 40b.
class HaydnFU_LS_S1_S_ST_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<18> reserved = 0;     // [29:12] unused
  bits<4> rt;                // GPR32 source (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s1 = {FU, opcode, reserved, rt, rs2, rs1};   // MSB-first -> s1[39:0]
}

// LS CBRI in S1: cbr_sel(uimm2), rtd(DR64), rs(GPR32), imm8(simm8).
// Payload 3+7+8+2+8+4+4 = 36b -> reserved12: 3+7+12+2+8+4+4 = 40b.
class HaydnFU_LS_S1_CBRI<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<12> reserved = 0;     // [29:18] unused
  bits<2> cbr_sel;
  bits<8> imm8;
  bits<4> rtd;               // DR64 (HIGHEST)
  bits<4> rs;                // GPR32 base (LOWEST)
  let s1 = {FU, opcode, reserved, cbr_sel, imm8, rtd, rs};  // s1[39:0]
}

// LS CBRR in S1: cbr_sel(uimm2), rtd(DR64), rs1, rs2 (GPR32).
// Payload 3+7+16+2+4+4+4 = 40b.
class HaydnFU_LS_S1_CBRR<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<2> cbr_sel;
  bits<4> rtd;               // DR64 (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s1 = {FU, opcode, reserved, cbr_sel, rtd, rs2, rs1};  // s1[39:0]
}

// LS PLDWWUA in S1 (AR type): ar_sel(uimm2), rs(GPR32).
// Payload 3+7+24+2+4 = 40b.

// LS WBARWUA in S1 (AR type): ar_sel(uimm2), rs(GPR32), dir_sel(uimm1).
// Payload 3+7+23+2+4+1 = 40b.
class HaydnFU_LS_S1_AR_WBAR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<23> reserved = 0;     // [29:7] unused
  bits<2> ar_sel;
  bits<4> rs;
  bits<1> dir_sel;
  let s1 = {FU, opcode, reserved, ar_sel, rs, dir_sel};  // s1[39:0]
}

// LS D_LQHWUA_POST / D_LTWUA_POST / D_SQHWUA_POST / D_STWUA_POST in S1
// (AR type): rtd(DR64), ar_sel(uimm2), rs1, rs2 (GPR32), dir_sel(uimm1).
// Payload 3+7+12+4+2+4+4+1+3 = 40b (pad0 fills the last 3 bits).
class HaydnFU_LS_S1_AR_UA<bits<7> opc, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<12> reserved = 0;     // [29:18] unused
  bits<4> rtd;               // DR64 (HIGHEST)
  bits<2> ar_sel;
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  bits<1> dir_sel;
  bits<3> pad0 = 0;          // [2:0] fill to 40b
  let s1 = {FU, opcode, reserved, rtd, ar_sel, rs2, rs1, dir_sel, pad0};  // s1[39:0]
}

// LS D_LD_RR in S2: rtd(DR64), rs1, rs2 (GPR32). Mirrors S1.
class HaydnFU_LS_S2_D_LD_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let mayLoad = 1;  // setDesc preserves load semantic
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<18> reserved = 0;     // [29:12] unused
  bits<4> rtd;               // DR64 dest (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s2 = {FU, opcode, reserved, rtd, rs2, rs1};  // MSB-first -> s2[39:0]
}

// LS D_ST_RI6 in S2 (D_SD_* / D_SW_* / D_SHW_* family). Mirrors S1.
class HaydnFU_LS_S2_D_ST_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rtd;               // DR64 source (HIGHEST)
  let s2 = {FU, opcode, reserved, imm6, rtd, rs};  // MSB-first -> s2[39:0]
}

// LS S_ST_RI6 in S2 (S_SW_* / S_SHW_* / S_SB_* family). Mirrors S1.
class HaydnFU_LS_S2_S_ST_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rt;                // GPR32 source (HIGHEST)
  let s2 = {FU, opcode, reserved, imm6, rt, rs};   // MSB-first -> s2[39:0]
}

// LS D_ST_RR in S2 (D_SD_* / D_SW_* / D_SHW_* family). Mirrors S1.
class HaydnFU_LS_S2_D_ST_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<18> reserved = 0;     // [29:12] unused
  bits<4> rtd;               // DR64 source (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s2 = {FU, opcode, reserved, rtd, rs2, rs1};  // MSB-first -> s2[39:0]
}

// LS S_ST_RR in S2 (S_SW_* / S_SHW_* / S_SB_* family). Mirrors S1.
class HaydnFU_LS_S2_S_ST_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<18> reserved = 0;     // [29:12] unused
  bits<4> rt;                // GPR32 source (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s2 = {FU, opcode, reserved, rt, rs2, rs1};   // MSB-first -> s2[39:0]
}

// LS CBRI in S2: cbr_sel(uimm2), rtd(DR64), rs(GPR32), imm8(simm8).
// Mirrors S1.
class HaydnFU_LS_S2_CBRI<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<12> reserved = 0;     // [29:18] unused
  bits<2> cbr_sel;
  bits<8> imm8;
  bits<4> rtd;               // DR64 (HIGHEST)
  bits<4> rs;                // GPR32 base (LOWEST)
  let s2 = {FU, opcode, reserved, cbr_sel, imm8, rtd, rs};  // s2[39:0]
}

// LS CBRR in S2: cbr_sel(uimm2), rtd(DR64), rs1, rs2 (GPR32). Mirrors S1.
class HaydnFU_LS_S2_CBRR<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<2> cbr_sel;
  bits<4> rtd;               // DR64 (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s2 = {FU, opcode, reserved, cbr_sel, rtd, rs2, rs1};  // s2[39:0]
}

// LS PLDWWUA in S2 (AR type): ar_sel(uimm2), rs(GPR32). Mirrors S1.

// LS WBARWUA in S2 (AR type): ar_sel(uimm2), rs(GPR32), dir_sel(uimm1).
// Mirrors S1.
class HaydnFU_LS_S2_AR_WBAR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<23> reserved = 0;     // [29:7] unused
  bits<2> ar_sel;
  bits<4> rs;
  bits<1> dir_sel;
  let s2 = {FU, opcode, reserved, ar_sel, rs, dir_sel};  // s2[39:0]
}

// LS D_LQHWUA_POST / D_LTWUA_POST / D_SQHWUA_POST / D_STWUA_POST in S2
// (AR type). Mirrors S1.
class HaydnFU_LS_S2_AR_UA<bits<7> opc, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<12> reserved = 0;     // [29:18] unused
  bits<4> rtd;               // DR64 (HIGHEST)
  bits<2> ar_sel;
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  bits<1> dir_sel;
  bits<3> pad0 = 0;          // [2:0] fill to 40b
  let s2 = {FU, opcode, reserved, rtd, ar_sel, rs2, rs1, dir_sel, pad0};  // s2[39:0]
}

// TODO(golden) per-op field ordering: the exact MSB->LSB split of ar_sel vs
// dir_sel vs cbr_sel in the AR/CB families is not fully specified in Format E
// golden (only FU/opcode widths are frozen). The layout above is best-effort
// (2-bit ar_sel, 1-bit dir_sel, 2-bit cbr_sel). Refine once golden confirms
// bit-exact placement.

//===----------------------------------------------------------------------===//
// LS3 multiclasses — PRE/POST/WITH with tied base def (refactor)
//===----------------------------------------------------------------------===//

// LS3_D_LD_RI6_PostPre retired (R13). Format E members fill e0/e1/e2.
// LS3_D_LD_RI6_With retired (R13). Format E members fill e0/e1/e2.
// LS3_S_LD_RI6_PostPre retired (R13). Format E members fill e0/e1/e2.
// LS3_S_LD_RI6_With retired (R13). Format E members fill e0/e1/e2.
// LS3_D_LD_RR_PostPre retired (R13). Format E members fill e0/e1;
// D_LDW_POST_REG S2 FieldSlot retired — no e2 member, reject-not-migrate.
// LS3_D_LD_RR_With retired (R13). Format E members fill e0/e1/e2.
// LS3_S_LD_RR_PostPre retired (R13). Format E members fill e0/e1/e2.
// LS3_S_LD_RR_With retired (R13). Format E members fill e0/e1/e2.

//===----------------------------------------------------------------------===//
// LS op defs -- 3 per op (s0/s1/s2). Opcodes are golden Index (7-bit, dense).
//
// POST/PRE writeback (golden: base in GPR_Read_Port AND GPR_Write_Port):
// model as tied base def ($rs_wb / $rs1_wb) for LLVM SSA. Encoding fields
// bind to the use ($rs / $rs1). Not a second ISA return; not same-cycle data.
// All PRE/POST must exploit the tied def. Store S1/S2 FieldSlots are
// retired (no Format E e1/e2 member). Product occupancy fills e0.
//===----------------------------------------------------------------------===//

// #1 D_LDW_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #2 D_LDW_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #3 D_LDW_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #4 D_LDW_POST_REG FieldSlots retired (R13). No Format E e2 member —
// S2 occupancy is reject-not-migrate. Format E fills e0/e1. Mask 0x7 stays.
// #5 D_LDW_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #6 D_LDW_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #7 S_LW_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #8 S_LW_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #9 S_LW_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #10 S_LW_POST_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #11 S_LW_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #12 S_LW_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #13 D_LW_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #14 D_LW_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #15 D_LW_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #16 D_LW_POST_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #17 D_LW_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #18 D_LW_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #19 S_LHWS_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #20 S_LHWS_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #21 S_LHWS_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #22 S_LHWS_POST_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #23 S_LHWS_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #24 S_LHWS_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #25 D_LHW_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #26 D_LHW_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #27 D_LHW_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #28 D_LHW_POST_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #29 D_LHW_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #30 D_LHW_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #31 S_LHWU_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #32 S_LHWU_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #33 S_LHWU_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #34 S_LHWU_POST_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #35 S_LHWU_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #36 S_LHWU_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #37 S_LBS_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #38 S_LBS_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #39 S_LBS_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #40 S_LBS_POST_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #41 S_LBS_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #42 S_LBS_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #43 S_LBU_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #44 S_LBU_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #45 S_LBU_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #46 S_LBU_POST_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #47 S_LBU_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #48 S_LBU_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #49 D_SDW_POST_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
// Batch 3: aligned to legacy tied-def shape (HaydnInstrInfoManual.td):
// (outs GPR32:$rs_wb), (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm) with
// Constraints = "$rs = $rs_wb". The input $rs is the base being updated;
// $rs_wb is the writeback output. Format class bits field `rs` binds to the
// input $rs (the encoded base register).
// D_SDW_POST_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #50 D_SDW_WITH_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
// D_SDW_WITH_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #51 D_SDW_PRE_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
// D_SDW_PRE_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #52 D_SDW_POST_REG (RR)
// Batch 3: aligned to legacy tied-def shape. Format class bits field
// `rs1` binds to input $rs1 (the encoded base register).
// D_SDW_POST_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #53 D_SDW_WITH_REG (RR) — S0 retired (R13). Occupancy Mask 0x7 stays.
// D_SDW_WITH_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #54 D_SDW_PRE_REG (RR)
// D_SDW_PRE_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #55 S_SW_POST_IMM (RI6)
// Batch 3: aligned to legacy tied-def shape. Format class bits field
// `rs` binds to input $rs (the encoded base register).
// S_SW_POST_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// ST32_POST / ST64_POST S1 FieldSlots retired (R13). No Format E e1
// member — reject-not-migrate. Occupancy peels to S_SW_POST_IMM /
// D_SDW_POST_IMM e0 LOADSTORE0. Public asm keeps the catalog mnemonic.

// #56 S_SW_WITH_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
// S_SW_WITH_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #57 S_SW_PRE_IMM (RI6)
// S_SW_PRE_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #58 S_SW_POST_REG (RR)
// Batch 3: aligned to legacy tied-def shape. Format class bits field
// `rs1` binds to input $rs1 (the encoded base register).
// S_SW_POST_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #59 S_SW_WITH_REG (RR) — S0 retired (R13). Occupancy Mask 0x7 stays.
// S_SW_WITH_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #60 S_SW_PRE_REG (RR)
// S_SW_PRE_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #61 D_SW_L_POST_IMM (RI6)
// D_SW_L_POST_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #62 D_SW_L_WITH_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
// D_SW_L_WITH_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #63 D_SW_L_PRE_IMM (RI6)
// D_SW_L_PRE_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #64 D_SW_L_POST_REG (RR)
// D_SW_L_POST_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #65 D_SW_L_WITH_REG (RR) — S0 retired (R13). Occupancy Mask 0x7 stays.
// D_SW_L_WITH_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #66 D_SW_L_PRE_REG (RR)
// D_SW_L_PRE_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #67 D_SW_H_POST_IMM (RI6)
// D_SW_H_POST_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #68 D_SW_H_WITH_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
// D_SW_H_WITH_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #69 D_SW_H_PRE_IMM (RI6)
// D_SW_H_PRE_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #70 D_SW_H_POST_REG (RR)
// D_SW_H_POST_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #71 D_SW_H_WITH_REG (RR) — S0 retired (R13). Occupancy Mask 0x7 stays.
// D_SW_H_WITH_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #72 D_SW_H_PRE_REG (RR)
// D_SW_H_PRE_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #73 S_SHW_POST_IMM (RI6)
// S_SHW_POST_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #74 S_SHW_WITH_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
// S_SHW_WITH_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #75 S_SHW_PRE_IMM (RI6)
// S_SHW_PRE_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #76 S_SHW_POST_REG (RR)
// S_SHW_POST_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #77 S_SHW_WITH_REG (RR) — S0 retired (R13). Occupancy Mask 0x7 stays.
// S_SHW_WITH_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #78 S_SHW_PRE_REG (RR)
// S_SHW_PRE_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #79 D_SHW_POST_IMM (RI6)
// D_SHW_POST_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #80 D_SHW_WITH_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
// D_SHW_WITH_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #81 D_SHW_PRE_IMM (RI6)
// D_SHW_PRE_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #82 D_SHW_POST_REG (RR)
// D_SHW_POST_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #83 D_SHW_WITH_REG (RR) — S0 retired (R13). Occupancy Mask 0x7 stays.
// D_SHW_WITH_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #84 D_SHW_PRE_REG (RR)
// D_SHW_PRE_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #85 S_SB_POST_IMM (RI6)
// S_SB_POST_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #86 S_SB_WITH_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
// S_SB_WITH_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #87 S_SB_PRE_IMM (RI6)
// S_SB_PRE_IMM S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #88 S_SB_POST_REG (RR)
// S_SB_POST_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #89 S_SB_WITH_REG (RR) — S0 retired (R13). Occupancy Mask 0x7 stays.
// S_SB_WITH_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #90 S_SB_PRE_REG (RR)
// S_SB_PRE_REG S1/S2 FieldSlots retired (R13). No Format E e1/e2 member — reject-not-migrate. e0 member fills index 0. Occupancy Mask 0x7 stays.
// #91–#94 Circular buffer load/store (CBRI / CBRR)
//
// HW AGU post-updates the base GPR (BundleSim pw_gpr). Model as tied writeback
// so PostRA pack cannot co-issue two CB ops that write the same base (B6
// WRITE_CONFLICT: three d_ldw_cb_imm same r5 in one bundle).
//
// Operand list MUST match public logical D_*_CB_* (HaydnInstrInfoManual.td) and
// GISel: MC encode after setDesc is Desc-as-is (no operand rewrite).
// Bit layout binds by name (cbr_sel, imm8, rtd, rs) — $rs is the encoded base.
//
//   LDW_IMM: (outs rtd, rs_wb), (ins rs, cbr_sel, imm8)  Constraints rs=rs_wb
//   LDW_REG: (outs rtd, rs1_wb), (ins rs1, cbr_sel, rs2)
//   SDW_IMM: (outs rs_wb), (ins rtd, rs, cbr_sel, imm8)
//   SDW_REG: (outs rs1_wb), (ins rtd, rs1, cbr_sel, rs2)
//
// #91–#94 CB S1/S2 FieldSlots retired (R13). No Format E e1/e2 member —
// reject-not-migrate. e0 LOADSTORE0 members fill index 0. S0 occupancy
// Fallback stays until that member Desc matches the logical dag.
// #91 D_LDW_CB_IMM (CBRI) — occupancy Fallback only; public matcher is the
// catalog logical. Format E e0 member fills encode.
// FieldSlot _S* occupancy Fallback retired — Format E e0 fills index 0.

// #92 D_LDW_CB_REG (CBRR) FieldSlot retired.
// #93 D_SDW_CB_IMM (CBRI) FieldSlot retired.
// #94 D_SDW_CB_REG (CBRR) FieldSlot retired.

// BREV-load FieldSlots retired (R13). Occupancy Mask 0x7 stays; Fallback
// is 0. Format E members keep the tied dest2_wb POST-increment shape.

// #99-#102 BREV-store FieldSlots retired (R13). No Format E e1/e2 member —
// reject-not-migrate. Format E e0 LOADSTORE0 fills index 0. Occupancy Mask
// 0x7 stays; Fallback is 0.

// #103 PLDWWUA (AR) — pre-load AR from 8-byte-aligned mem; mayLoad + AR side-effect.
// D-AR-INTRIN: **logical** PLDWWUA is GISel + post-RA HR identity
// (no slot in opcode). Slot = AltDescs from HR auction.
// Never ISel/PEI-emit *_S* for AR stream.
// R13: _S0/_S1/_S2 FieldSlots retired. Operand dag is the golden wire
// order (ar_sel, rs) so occupancy binds PLDWWUA_POST members at e0/e1/e2
// (NumDefs 0, operand classes match positionally). Occupancy Mask 0x7
// stays; Fallback is 0. Public asm prints "pldwwua ar_sel, rs" unchanged.
let mayLoad = 1, hasSideEffects = 1 in
def PLDWWUA : HaydnInst<4, (outs), (ins uimm2:$ar_sel, GPR32:$rs),
                        "pldwwua\t$ar_sel, $rs", []> {
  // Multi-slot placeholder; real exclusivity is alts occupancy + HR tryAdd.
  let Itinerary = Slot012_ALU;
}

// #104 D_LQHWUA_POST (AR) — unaligned quad-half load + post-inc on rs1.
// shape: tied-def POST on base (rs1). MCInst order:
// [rtd, rs1_wb, rs1, rs2, ar_sel, dir_sel]
// Logical base for ISel/HR. S0/S1 FieldSlots retired. S2 stays as the
// live Slot2_LS itinerary producer (memory-cycle table); product encode
// uses Format E members (keep-map drops rs2/dir_sel).
let mayLoad = 1, hasSideEffects = 1 in
def D_LQHWUA_POST : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs1_wb),
    (ins GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel),
    "d_lqhwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
  let Itinerary = Slot012_ALU;
}
let mayLoad = 1, isCodeGenOnly = 1 in
def D_LQHWUA_POST_S2 : HaydnFU_LS_S2_AR_UA<0b1101000, (outs DR64:$rtd, GPR32:$rs1_wb), (ins GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_lqhwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
}

// #105 D_LTWUA_POST (AR) — unaligned two-word load + post-inc on rs1.
let mayLoad = 1, hasSideEffects = 1 in
def D_LTWUA_POST : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs1_wb),
    (ins GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel),
    "d_ltwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
  let Itinerary = Slot012_ALU;
}

// #106 FLAR (AR) — flush aligned register (AR side-effect only).
let hasSideEffects = 1 in
def FLAR : HaydnInst<4, (outs), (ins uimm2:$ar_sel), "flar\t$ar_sel", []> {
  let Itinerary = Slot012_ALU;
}
// FLAR FieldSlots retired (R13). Occupancy Mask 0x7 stays; Fallback is 0.

// #107 WBARWUA (AR) — write-back residual from AR; mayStore + AR side-effect.
let mayStore = 1, mayLoad = 0, hasSideEffects = 1 in
def WBARWUA : HaydnInst<4, (outs), (ins GPR32:$rs, uimm2:$ar_sel, uimm1:$dir_sel),
                        "wbarwua\t$ar_sel, $rs, $dir_sel", []> {
  let Itinerary = Slot012_ALU;
}
// WBARWUA S1/S2 FieldSlots retired (R13). No Format E e1/e2 member —
// reject-not-migrate. e0 Drop-fills index 0 (2-op member vs 3-op logical).
// Occupancy Mask 0x7 stays; Fallback is 0.

// #108 D_SQHWUA_POST (AR) — unaligned quad-half store + post-inc on rs1.
// MCInst order: [rs1_wb, rtd, rs1, rs2, ar_sel, dir_sel]
let mayStore = 1, mayLoad = 0, hasSideEffects = 1 in
def D_SQHWUA_POST : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel),
    "d_sqhwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
  let Itinerary = Slot012_ALU;
}
// S1/S2 FieldSlots retired. No Format E e1/e2 member — reject-not-migrate.
// e0 LOADSTORE0 fills index 0.

// #109 D_STWUA_POST (AR) — unaligned two-word store + post-inc on rs1.
let mayStore = 1, mayLoad = 0, hasSideEffects = 1 in
def D_STWUA_POST : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel),
    "d_stwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
  let Itinerary = Slot012_ALU;
}
// S1/S2 FieldSlots retired. No Format E e1/e2 member — reject-not-migrate.
// e0 LOADSTORE0 fills index 0.

// *_REG_M0S0LS FieldSlots retired (R13). Occupancy Mask 0x1 stays;
// Fallback is 0. Placement uses Format E WITH_REG e0 members.

