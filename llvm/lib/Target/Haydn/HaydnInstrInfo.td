//===-- HaydnInstrInfo.td - Haydn Instruction Definitions -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Main instruction definitions for the Haydn 3-issue VLIW DSP.
// Stream A1: Core instruction set for MVB (scalar C compilation).
//
//===----------------------------------------------------------------------===//

// HaydnInstrFormats.td is included by Haydn.td before this file.
// Do not re-include it here.

//===----------------------------------------------------------------------===//
// Pseudo Instructions (CodeGen only)
//===----------------------------------------------------------------------===//

// Pseudo for loading large immediates. Rematerializable: single imm operand so
// RA re-emits at each use instead of spilling across complex CFG (same class of
// bug as LOADI64). Expanded post-RA via HaydnMatInt (LUI <<20).
let isReMaterializable = 1, isCodeGenOnly = 1, hasNoSchedulingInfo = 1 in
def LOADI32 : HaydnPseudo<(outs GPR32:$rd), (ins i32imm:$imm),
                           "loadi32\t$rd, $imm", []>;

// Pseudo for loading a 64-bit immediate into a DR64 register. Rematerializable
// (single immediate operand, no vreg uses) so RA re-emits it at each use instead
// of cross-block-copying the DR64 via OR64 — the latter fails dominance for i64
// constants used in non-dominating join blocks. Expanded post-RA in
// expandPostRAPseudo: materialise lo32/hi32 into the reserved R12 scratch via
// HaydnMatInt, spill to a transient 8-byte SP slot, LD64.
//
// Code-size cost: because RA now rematerialises i64 constants at each use
// instead of one materialisation + cheap OR64 copies, the NatureDSP gate grows
// ~+1.2% bundles (43303 -> 43811). Accepted for correctness (clears a
// verifier-aborting dominance bug). A follow-up could keep the old
// single-materialisation sequence for provably single-block/single-use consts.
let isReMaterializable = 1, isCodeGenOnly = 1, hasNoSchedulingInfo = 1 in
def LOADI64 : HaydnPseudo<(outs DR64:$rd), (ins i64imm:$imm),
                           "loadi64\t$rd, $imm", []>;

// Global / block / JT address materialization. MUST be rematerializable:
// seed 3148 — without remat, RA spills LOAD_ADDR across a diamond;
// the store is only on one arm and the join reloads an uninitialized slot
// (0xffffffff) → S_LW_WITH_IMM MEMORY_FAULT. Expanded post-RA (ExpandPseudos)
// to LUI+ADDI32_W with HI12/LO20 fixups (or MatInt for imm form).
let isReMaterializable = 1, isCodeGenOnly = 1, hasNoSchedulingInfo = 1 in
def LOAD_ADDR : HaydnPseudo<(outs GPR32:$rd), (ins i32imm:$addr),
                            "load_addr\t$rd, $addr", []>;

// Return pseudo — GISel handles returns via CallLowering, no SDAG pattern.
// Uses R15 (LR, return address). Return value register (R0/D0) is added as
// implicit-use by lowerReturn at the MI level, not via TableGen Uses.
let isReturn = 1, isTerminator = 1, isBarrier = 1, Uses = [R15] in
def RET : HaydnPseudo<(outs), (ins), "ret", []>;

//===----------------------------------------------------------------------===//
// Varargs Pseudo Instructions
//===----------------------------------------------------------------------===//

// VASTART/VACOPY write the va_list struct in memory (a side effect) but have
// no register defs, no Uses, and no memoperands. Without hasSideEffects the
// dead-mi-elimination pass sees them as side-effect-free dead code at -O1/-O2
// and deletes them BEFORE the AsmPrinter can expand them, leaving the va_list
// struct uninitialized so va_arg reads garbage (returns 0 instead of the
// variadic value). hasSideEffects=1 is load-bearing here, exactly as it is for
// CSRW below (which otherwise gets DCE'd and the CSR is never programmed).
// The same trap as the CSRW WUR_AE_CBEGIN0/CEND0 case. VAEND is intentionally
// excluded: it is a genuine no-op on baremetal and SHOULD stay DCE-able.
// hasSideEffects keeps DCE off; mayLoad/mayStore admit real MMOs through
// select→ExpandPseudos so memory-effect ownership is not dropped.
let hasSideEffects = 1, mayStore = 1 in {
// Pseudo for va_start - initializes va_list pointer
// Operands: [va_list_ptr]
// Stores the address of the first variadic argument to the va_list pointer
def VASTART : HaydnPseudo<(outs), (ins GPR32:$ptr),
                           "vastart\t$ptr", []>;
} // hasSideEffects / mayStore

let hasSideEffects = 1, mayLoad = 1, mayStore = 1 in {
// Pseudo for va_copy - copies one va_list to another
// Operands: [dst_ptr, src_ptr]
// Loads from src_ptr and stores to dst_ptr
def VACOPY : HaydnPseudo<(outs), (ins GPR32:$dst, GPR32:$src),
                           "vacopy\t$dst, $src", []>;

// CB-131: G_VAARG → thin ISel to these; ExpandPseudos implements the full
// AArch64-style two-bank + stack-overflow algorithm (reg step 4/8, stack
// step 8 per CCAssignToStack<8,8>). hasSideEffects so DCE cannot delete.
def VAARG_I32 : HaydnPseudo<(outs GPR32:$rd), (ins GPR32:$valist),
                             "vaarg.i32\t$rd, $valist", []>;
def VAARG_I64 : HaydnPseudo<(outs DR64:$rd), (ins GPR32:$valist),
                             "vaarg.i64\t$rd, $valist", []>;
} // hasSideEffects / mayLoad / mayStore

// Pseudo for va_end - no-op for baremetal
def VAEND : HaydnPseudo<(outs), (ins GPR32:$ptr),
                        "vaend\t$ptr", []>;

// Call frame adjustment pseudos. Two immediates: the size and the alignment
// matching the convention used by RISC-V / AArch64 / ARM. ADJCALLSTACKDOWN
// decrements SP by the outgoing stack-arg size; ADJCALLSTACKUP restores it.
let Defs = [R13], Uses = [R13] in {
def ADJCALLSTACKDOWN : HaydnPseudo<(outs), (ins i32imm:$amt, i32imm:$align),
                                     "adjcallstackdown $amt, $align", []>;
def ADJCALLSTACKUP : HaydnPseudo<(outs), (ins i32imm:$amt, i32imm:$align),
                                   "adjcallstackup $amt, $align", []>;
}

// Libcall pseudos for operations without hardware support
// These are emitted by the InstructionSelector and expanded by ExpandPseudos
// into direct calls to compiler-rt functions (__divsi3, __udivsi3, etc.).
//
// Call clobbers (all R* are GPRs). R1–R7, R12, D0–D7, R15(link).
// R14: never in Defs — always CSR via regmask (s0-style). CSR bank:
// R8–R11, R14, R15, D8–D15.
let Defs = [R1, R2, R3, R4, R5, R6, R7, R12,
            D0, D1, D2, D3, D4, D5, D6, D7,
            R15] in {
def LIBCALL_SDIV : HaydnPseudo<(outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                                "libcall_sdiv\t$rd, $rs1, $rs2",
                                []>;

def LIBCALL_UDIV : HaydnPseudo<(outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                                "libcall_udiv\t$rd, $rs1, $rs2",
                                []>;

def LIBCALL_SREM : HaydnPseudo<(outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                                "libcall_srem\t$rd, $rs1, $rs2",
                                []>;

def LIBCALL_UREM : HaydnPseudo<(outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                                "libcall_urem\t$rd, $rs1, $rs2",
                                []>;

// 64-bit multiply libcall — emits a call to __muldi3
def LIBCALL_MUL64 : HaydnPseudo<(outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
                                  "libcall_mul64\t$rd, $rs1, $rs2", []>;
} // Defs = caller-saved clobbers

// Cross-bank copy pseudos for DR64 <-> GPR32 transfers.
// These are expanded by the ExpandPseudos pass or AsmPrinter.
// MOV_GPR_TO_DR64: Move a pair of GPR32 registers into one DR64 register.
def MOV_GPR_TO_DR64 : HaydnPseudo<(outs DR64:$rd), (ins GPR32:$rs_lo, GPR32:$rs_hi),
                                    "mov_gpr_to_dr64\t$rd, $rs_lo, $rs_hi", []>;

// MOV_DR64_TO_GPR: Move one DR64 register into a pair of GPR32 registers.
def MOV_DR64_TO_GPR : HaydnPseudo<(outs GPR32:$rd_lo, GPR32:$rd_hi), (ins DR64:$rs),
                                    "mov_dr64_to_gpr\t$rd_lo, $rd_hi, $rs", []> {
  let hasSideEffects = 0;
  let hasNoSchedulingInfo = 1;
}

//===----------------------------------------------------------------------===//
// Addressing Mode Pseudo Instructions
//===----------------------------------------------------------------------===//
//
// These pseudos represent the Haydn ISA's post-increment and pre-decrement
// addressing modes. They are produced by ISel / HaydnExpandPostIncEarly
// (LD32 + ADDI32 fold) and expanded into real instructions.
//
// The ISA specifies:
// S_LW_POST_IMM: rt, rs, imm6 — load word from [rs], then rs += (imm6 << 2)
// S_SW_POST_IMM: rt, rs, imm6 — store word to [rs], then rs += (imm6 << 2)
// S_LW_PRE_IMM: rt, rs, imm6 — rs += (imm6 << 2), then load word from [rs]
// S_SW_PRE_IMM: rt, rs, imm6 — rs += (imm6 << 2), then store word to [rs]
//
// The immediate in the pseudo is the *stride* (post-increment amount), not
// the scaled imm6. The expansion pass scales it appropriately.
// For 32-bit word access, stride = imm6 << 2 (4-byte aligned).

// Post-increment load 32-bit word: load from [BaseReg + Offset], then
// BaseReg += Stride. Offset defaults to 0 for plain post-inc patterns; it
// is non-zero when this pseudo is formed from an LD32 with a non-zero
// displacement (e.g. the second LD32 of a cross-bank fold producing
// offsets 0,4). Preserving Offset prevents the post-inc formation from
// silently dropping the original displacement.
// Expanded to: LD32 rt, BaseReg, Offset; ADDI32 BaseReg, BaseReg, Stride
// When the ISA encoder supports S_LW_POST_IMM natively, this becomes one
// instruction (imm6 carries the displacement, stride is the post-update).
let mayLoad = 1 in
def LD32_POST_INC : HaydnPseudo<(outs GPR32:$rt),
                                 (ins GPR32:$base, i32imm:$stride, i32imm:$offset),
                                 "ld32_post_inc\t$rt, $base, $stride, $offset", []>;

// Post-increment store 32-bit word: store to [BaseReg + Offset], then
// BaseReg += Stride. See LD32_POST_INC for the offset rationale.
// Expanded to: ST32 rt, BaseReg, Offset; ADDI32 BaseReg, BaseReg, Stride
let mayStore = 1, mayLoad = 0 in
def ST32_POST_INC : HaydnPseudo<(outs),
                                 (ins GPR32:$rt, GPR32:$base, i32imm:$stride,
                                      i32imm:$offset),
                                 "st32_post_inc\t$rt, $base, $stride, $offset", []>;

// Post-increment load 64-bit double-word: load from [BaseReg + Offset]
// then BaseReg += Stride. See LD32_POST_INC for the offset rationale.
// Expanded to: LD64_S1 rt, BaseReg, Offset; ADDI32 BaseReg, BaseReg, Stride
let mayLoad = 1 in
def LD64_POST_INC : HaydnPseudo<(outs DR64:$rt),
                                 (ins GPR32:$base, i32imm:$stride, i32imm:$offset),
                                 "ld64_post_inc\t$rt, $base, $stride, $offset", []>;

// Post-increment store 64-bit double-word: store to [BaseReg + Offset]
// then BaseReg += Stride. See LD32_POST_INC for the offset rationale.
// Expanded to: ST64 rt, BaseReg, Offset; ADDI32 BaseReg, BaseReg, Stride
let mayStore = 1, mayLoad = 0 in
def ST64_POST_INC : HaydnPseudo<(outs),
                                 (ins DR64:$rt, GPR32:$base, i32imm:$stride,
                                      i32imm:$offset),
                                 "st64_post_inc\t$rt, $base, $stride, $offset", []>;

//===----------------------------------------------------------------------===//
// Slot 0 — ALU Instructions (32-bit scalar)
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Arithmetic Instructions
//===----------------------------------------------------------------------===//

def ADD32 : FmtALU32<0x00, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "add32\t$rd, $rs1, $rs2",
                     []> {
  let isCommutable = 1; // peer: RISCVInstrInfo.td:807 ALU_rr Commutable=1
  let TSFlags{2-0} = 0b001;
}

//===----------------------------------------------------------------------===//
// Mode-0 shell islands deleted.
// Former ADD32_M0 / *_M0 / *_S1_M0 / FMULA16_LS00_M0S1 shells removed.
// public REG identity is logical LD32_REG_M0S0LS etc.; private encode
// peers are *_S0 in HaydnFormatsLS.td. ALU public = ADD32 etc.; peers *_S*.
//===----------------------------------------------------------------------===//

def ADD32S : FmtALU32<0x01, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                      "add32s\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
}

def SUB32 : FmtALU32<0x02, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "sub32\t$rd, $rs1, $rs2",
                     []> {
  let TSFlags{2-0} = 0b001;
}

def SUB32S : FmtALU32<0x03, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                      "sub32s\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
}
//===----------------------------------------------------------------------===//
// G-format (FmtG_S0_ALU32 / *_G) shells retired (legacy).
// HaydnGFormatSelect was removed; zero live Haydn::*_G C++ refs.
//===----------------------------------------------------------------------===//

def ADDI32 : FmtI<0x20, (outs GPR32:$rd), (ins GPR32:$rs, simm20:$imm),
                 "addi32\t$rd, $rs, $imm",
                 []> {
  let TSFlags{2-0} = 0b001;
}

def ADDI32S : FmtI<0x21, (outs GPR32:$rd), (ins GPR32:$rs, simm20:$imm),
                   "addi32s\t$rd, $rs, $imm", []> {
  let TSFlags{2-0} = 0b001;
}

def SUBI32 : FmtI<0x22, (outs GPR32:$rd), (ins GPR32:$rs, simm20:$imm),
                 "subi32\t$rd, $rs, $imm",
                 []> {
  let TSFlags{2-0} = 0b001;
}

def SUBI32S : FmtI<0x23, (outs GPR32:$rd), (ins GPR32:$rs, simm20:$imm),
                  "subi32s\t$rd, $rs, $imm", []> {
  let TSFlags{2-0} = 0b001;
}

//===----------------------------------------------------------------------===//
// Logical Instructions
//===----------------------------------------------------------------------===//

def AND32 : FmtALU32<0x04, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "and32\t$rd, $rs1, $rs2",
                     []> {
  let isCommutable = 1; // peer: RISCVInstrInfo.td:827
  let TSFlags{2-0} = 0b001;
}

def OR32 : FmtALU32<0x05, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                    "or32\t$rd, $rs1, $rs2",
                    []> {
  let isCommutable = 1; // peer: RISCVInstrInfo.td:825
  let TSFlags{2-0} = 0b001;
}

def XOR32 : FmtALU32<0x06, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "xor32\t$rd, $rs1, $rs2",
                     []> {
  let isCommutable = 1; // peer: RISCVInstrInfo.td:819
  let TSFlags{2-0} = 0b001;
}

def NOT32 : FmtALU32Unary<0x07, (outs GPR32:$rd), (ins GPR32:$rs1),
                           "not32\t$rd, $rs1",
                           []> {
  let TSFlags{2-0} = 0b001;
}

// Logical public identity for ZEXT imm ALU. Private peers:
// ANDI32_S* / ORI32_S* / XORI32_S* (HaydnFormatsALU32 RI20, uimm20).
// ISA: rt = rs OP ZEXT(imm20). Keep FmtI field names ($rd/$rs/$imm) so
// Inst binds; ImmLeaf is uimm20 (not uimm16) — the ISel contract that
// matches peers' ZEXT imm20. FmtI Inst is dead on the live path (post-RA
// setDesc to member); bits<16> imm field only constrains the
// legacy Inst path, not the slot-member encode.
def ANDI32 : FmtI<0x24, (outs GPR32:$rd), (ins GPR32:$rs, uimm20:$imm),
                  "andi32\t$rd, $rs, $imm", []> {
  let TSFlags{2-0} = 0b001;
}
def ORI32 : FmtI<0x25, (outs GPR32:$rd), (ins GPR32:$rs, uimm20:$imm),
                 "ori32\t$rd, $rs, $imm", []> {
  let TSFlags{2-0} = 0b001;
}
def XORI32 : FmtI<0x26, (outs GPR32:$rd), (ins GPR32:$rs, uimm20:$imm),
                  "xori32\t$rd, $rs, $imm", []> {
  let TSFlags{2-0} = 0b001;
}

//===----------------------------------------------------------------------===//
// Move / Load Immediate Instructions
//===----------------------------------------------------------------------===//

def MOVE32 : FmtALU32<0x08, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                      "move32\t$rd, $rs1",
                      []> {
  let TSFlags{2-0} = 0b001;
}

// User-facing logical `nop`. Product encoding is Format E architectural
// members (NOP_S0 and generated E96 NOP_*). All-zero 16-bit Inst is not a
// valid Format E bundle (indicator 000). Matcher-visible so bare `nop` and
// nop pads assemble; isPseudo so MC does not emit a default-zero Inst field.
let hasSideEffects = 0, isReMaterializable = 1, isPseudo = 1,
    isCodeGenOnly = 0 in
def NOP : HaydnInst<0, (outs), (ins), "nop", []> {
  let TSFlags{2-0} = 0b111;  // Any slot
  let Itinerary = Slot012_ALU;
}

// ZERO_GPR — catalog I8 dest-as-out. Matcher-visible logical (NOP shape);
// product encode is Format E members. Residual ZERO_GPR_S* stay until the
// FieldSlot class hide. GISel MatInt(0) stays ADDI32_W, not this opcode.
let hasSideEffects = 0, isReMaterializable = 1, isPseudo = 1,
    isCodeGenOnly = 0 in
def ZERO_GPR : HaydnInst<0, (outs GPR32:$rd), (ins),
                        "zero_gpr\t$rd", []> {
  let TSFlags{2-0} = 0b111;
  let Itinerary = Slot012_ALU;
}

def LUI : FmtI<0x27, (outs GPR32:$rd), (ins GPR32:$rs, uimm12:$imm),
              "lui\t$rd, $imm", []> {
  let TSFlags{2-0} = 0b001;
}

//===----------------------------------------------------------------------===//
// Shift Instructions
//===----------------------------------------------------------------------===//

def SRLI32 : FmtI<0x29, (outs GPR32:$rd), (ins GPR32:$rs, uimm5:$imm),
                  "srli32\t$rd, $rs, $imm",
                  []> {
  let TSFlags{2-0} = 0b001;
}

def SRAI32 : FmtI<0x2A, (outs GPR32:$rd), (ins GPR32:$rs, uimm5:$imm),
                  "srai32\t$rd, $rs, $imm",
                  []> {
  let TSFlags{2-0} = 0b001;
}

def SRAI32R : FmtI<0x2B, (outs GPR32:$rd), (ins GPR32:$rs, uimm5:$imm),
                   "srai32r\t$rd, $rs, $imm", []> {
  let TSFlags{2-0} = 0b001;
}

def SLLI32 : FmtI<0x2C, (outs GPR32:$rd), (ins GPR32:$rs, uimm5:$imm),
                  "slli32\t$rd, $rs, $imm",
                  []> {
  let TSFlags{2-0} = 0b001;
}

def SRL32 : FmtALU32<0x09, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "srl32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
}

def SRA32 : FmtALU32<0x0A, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "sra32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
}

def SRA32R : FmtALU32<0x0B, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                      "sra32r\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
}

def SLL32 : FmtALU32<0x0C, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "sll32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
}

//===----------------------------------------------------------------------===//
// DSP / Miscellaneous Instructions
//===----------------------------------------------------------------------===//

def ABS32S : FmtALU32<0x0D, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                      "abs32s\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b001;
}

def ABS32 : FmtALU32<0x0E, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "abs32\t$rd, $rs1",
                     []> {
  let TSFlags{2-0} = 0b001;
}

def MAX32 : FmtALU32<0x0F, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "max32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
}

def MAXU32 : FmtALU32<0x10, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                      "maxu32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
}

def MIN32 : FmtALU32<0x11, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "min32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
}

def MINU32 : FmtALU32<0x12, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                      "minu32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
}

def NEG32 : FmtALU32Unary<0x13, (outs GPR32:$rd), (ins GPR32:$rs1),
                           "neg32\t$rd, $rs1",
                           []> {
  let TSFlags{2-0} = 0b001;
}

def NEG32S : FmtALU32Unary<0x14, (outs GPR32:$rd), (ins GPR32:$rs1),
                            "neg32s\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b001;
}

def BREV32 : FmtALU32<0x15, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                      "brev32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
}

def NSA32 : FmtALU32Unary<0x16, (outs GPR32:$rd), (ins GPR32:$rs1),
                          "nsa32\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b001;
}

def NSAU32 : FmtALU32Unary<0x17, (outs GPR32:$rd), (ins GPR32:$rs1),
                           "nsau32\t$rd, $rs1",
                           []> {
  let TSFlags{2-0} = 0b001;
}

def POPCOUNT32 : FmtALU32Unary<0x18, (outs GPR32:$rd), (ins GPR32:$rs1),
                               "popcount32\t$rd, $rs1",
                               []> {
  let TSFlags{2-0} = 0b001;
}

//===----------------------------------------------------------------------===//
// Compare / Conditional Move Instructions (scalar 32-bit).
//
// ISA-27: per the spec DB (`haydn_instruction_db.json`), the 32-bit compare
// conditional-move family is GPR-coupled, NOT SFR-coupled:
// SEQ32/SLT32/SLTU32/SLE32 write GPR `rt = (cond) ? 1 : 0` (no SFR def)
// MOVT32/MOVF32 read GPR `rs2[0]` (no SFR use)
// Only the 64-bit/SIMD compare/cmov family + SFR-transfer instrs touch SFR
// (see the "SFR compare / conditional-move / transfer" section below). The
// former blanket `hasSideEffects=1, Defs=[SFR]`/`Uses=[SFR]` on these scalar
// ops was a latent spec-divergence that forced GenMux to do SFR liveness
// surgery (`fixSFRLiveness`) and serialized compares in the
// scheduler. With the spec-aligned model the compare result flows directly
// through GPR `rt`/`rs2`. See
// isa-improve/ISA-27-32bit-cmp-cmov-sfr-modeling-diverges-from-spec.md.
//===----------------------------------------------------------------------===//

def SLT32 : FmtALU32<0x19, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "slt32\t$rd, $rs1, $rs2",
                     []> {
  let TSFlags{2-0} = 0b001;
}

def SLTU32 : FmtALU32<0x1A, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                      "sltu32\t$rd, $rs1, $rs2",
                      []> {
  let TSFlags{2-0} = 0b001;
}

def SLE32 : FmtALU32<0x1B, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "sle32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
}

def SEQ32 : FmtALU32<0x1C, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "seq32\t$rd, $rs1, $rs2",
                     []> {
  let TSFlags{2-0} = 0b001;
}

// Scalar conditional moves — MOVT32/MOVF32.
// ISA DB semantics: rd is both input (fallthrough) and output (conditional write).
// MOVT32: rd = (rs2[0]==1) ? rs1 : rd
// MOVF32: rd = (rs2[0]==0) ? rs1 : rd
// The tied constraint ($rd = $rd_src) models the read-modify-write so that
// the selector can emit these from G_SELECT without SSA violations.
let Constraints = "$rd = $rd_src" in {
def MOVT32 : FmtALU32<0x1D, (outs GPR32:$rd), (ins GPR32:$rd_src, GPR32:$rs1, GPR32:$rs2),
                      "movt32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
}

def MOVF32 : FmtALU32<0x1E, (outs GPR32:$rd), (ins GPR32:$rd_src, GPR32:$rs1, GPR32:$rs2),
                      "movf32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
}
} // Constraints

//===----------------------------------------------------------------------===//
// Scalar 32-bit multiply — MULL / MULSSH / MULSUH / MULUUH (MAC GRR)
//
// Defined in HaydnInstrInfoAuto.td + HaydnFormatsMAC.td (not the retired
// phantom MUL32). GISel: G_MUL s32 → MULL; G_SMULH/UMULH s32 → MULSSH/MULUUH
// (HaydnInstructionSelector::emitScalarMul32 and G_SMULH/G_UMULH cases).
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// GISel-compatible Aliases
//===----------------------------------------------------------------------===//
// These provide the names that the GISel InstructionSelector references.
// They are aliases that expand to the real instructions above.

let isCodeGenOnly = 1 in {
def SHL32 : FmtALU32<0x0C, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "sll32\t$rd, $rs1, $rs2",
                     []> {
  let TSFlags{2-0} = 0b001;
}

def LSR32 : FmtALU32<0x09, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "srl32\t$rd, $rs1, $rs2",
                     []> {
  let TSFlags{2-0} = 0b001;
}

def ASR32 : FmtALU32<0x0A, (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2),
                     "sra32\t$rd, $rs1, $rs2",
                     []> {
  let TSFlags{2-0} = 0b001;
}
} // isCodeGenOnly

//===----------------------------------------------------------------------===//
// Direct GPR32 → DR64 sign-extending move.
//
// ISA `SEXT32T64 rtd, rs` sign-extends a 32-bit GPR into a 64-bit DR in one
// instruction (DB: `rtd = SEXT32->64(rs)`, slots 1/2 ALU, latency 1). The
// InstructionSelector lowers G_SEXT s32→s64 to this directly, and
// HaydnPostSelectOptimize folds any MOV_GPR_TO_DR64 %rd, %x, %x (sext-of-i32
// shape) into it. This replaces the previous LOADI32 31 + SRA32 +
// MOV_GPR_TO_DR64 sequence, whose MOV_GPR_TO_DR64 expanded post-RA to a 5+
// bundle SP-relative spill chain (SUBI32/ST32/ST32/LD64_S1/ADDI32).
//
// This is a REAL instruction with a REAL encoding (FmtALU64Unary 0x67/0x197)
// so it must NOT be isCodeGenOnly: it must enter the AsmParser
// MatchInstructionImpl table AND the disassembler DecoderTable. The mnemonic
// `sext32t64` round-trips through `llc | llvm-mc` (AsmPrinter emits the
// mnemonic via MCInstLowering using the opcode enum; the AsmParser re-parses
// it via the matcher). isCodeGenOnly here would break that round-trip
// `llvm-mc` would reject `sext32t64 d0, r8` with "failed to match
// instruction in bundle" and abort the whole bundle.
//
// The sibling `SEXT32T64` auto-gen stub in HaydnInstrInfoAuto.td has WRONG
// operand types (`(outs GPR32:$rd)`) and no real encoding; it is kept
// `isCodeGenOnly = 1` so it never enters the AsmParser matcher and never
// shadows THIS real instruction on re-parse. With both the real instruction
// parseable and the stub excluded, `sext32t64 <DR64>, <GPR32>` resolves to
// this def unambiguously.
//
// Encoding bits (FmtALU64Unary 0x67/0x197) are allocated in the 0x67
// unary range alongside SEQ64/SLT64/SLE64/MOVT64/MOVF64/X2ABS32S. The
// GPR32-in / DR64-out operand-type mix is per the ISA spec DB entry
// (`SEXT32T64 rtd, rs`, behavior `rtd = SEXT32->64(rs)`).
def SEXT_GPR32_TO_DR64
    : FmtALU64Unary<0x67, 0x197, (outs DR64:$rd), (ins GPR32:$rs1),
                    "sext32t64\t$rd, $rs1", []>;

//===----------------------------------------------------------------------===//
// Branch Instructions
//===----------------------------------------------------------------------===//

let isBranch = 1, isTerminator = 1, hasSideEffects = 1 in {
let Defs = [SFR] in {
def BEQ : FmtBRCond<0x2D, (outs), (ins GPR32:$rs1, GPR32:$rs2, brtarget:$offset),
              "beq\t$rs1, $rs2, $offset",
              []> {
  let TSFlags{2-0} = 0b001;
}

def BNE : FmtBRCond<0x2E, (outs), (ins GPR32:$rs1, GPR32:$rs2, brtarget:$offset),
              "bne\t$rs1, $rs2, $offset",
              []> {
  let TSFlags{2-0} = 0b001;
}

def BGE : FmtBRCond<0x2F, (outs), (ins GPR32:$rs1, GPR32:$rs2, brtarget:$offset),
              "bge\t$rs1, $rs2, $offset",
              []> {
  let TSFlags{2-0} = 0b001;
}

def BGEU : FmtBRCond<0x30, (outs), (ins GPR32:$rs1, GPR32:$rs2, brtarget:$offset),
               "bgeu\t$rs1, $rs2, $offset",
               []> {
  let TSFlags{2-0} = 0b001;
}

def BLT : FmtBRCond<0x31, (outs), (ins GPR32:$rs1, GPR32:$rs2, brtarget:$offset),
              "blt\t$rs1, $rs2, $offset",
              []> {
  let TSFlags{2-0} = 0b001;
}

def BLTU : FmtBRCond<0x32, (outs), (ins GPR32:$rs1, GPR32:$rs2, brtarget:$offset),
               "bltu\t$rs1, $rs2, $offset",
               []> {
  let TSFlags{2-0} = 0b001;
}
}

def BEQZ : FmtBR<0x33, (outs), (ins GPR32:$rs, brtarget:$offset),
               "beqz\t$rs, $offset",
               []> {
  let TSFlags{2-0} = 0b001;
}

def BNEZ : FmtBR<0x34, (outs), (ins GPR32:$rs, brtarget:$offset),
               "bnez\t$rs, $offset",
               []> {
  let TSFlags{2-0} = 0b001;
}

def BGEZ : FmtBR<0x35, (outs), (ins GPR32:$rs, brtarget:$offset),
               "bgez\t$rs, $offset",
               []> {
  let TSFlags{2-0} = 0b001;
}

def BLTZ : FmtBR<0x36, (outs), (ins GPR32:$rs, brtarget:$offset),
               "bltz\t$rs, $offset",
               []> {
  let TSFlags{2-0} = 0b001;
}
} // end isBranch, isTerminator

//===----------------------------------------------------------------------===//
// Unconditional branch pseudo
//===----------------------------------------------------------------------===//

// Pseudo for unconditional direct branch. Expands to BEQZ R0, target
// (always taken since R0 is hardwired to zero).
def B : HaydnPseudo<(outs), (ins brtarget:$offset),
                     "b\t$offset",
                     [(br bb:$offset)]> {
  let isBranch = 1;
  let isTerminator = 1;
  let isBarrier = 1;
  let TSFlags{2-0} = 0b001;
}

//===----------------------------------------------------------------------===//
// Jump / Link Instructions
//===----------------------------------------------------------------------===//
//
// JAL/JALR isCall clobbers: R1–R7, R12, D0–D7, R15. All R* are GPRs.
// R14 via regmask only (not Defs): always CSR (s0-style); hasFP reserves
// frame base. R12 normal call-clobbered (not free AT).

let Defs = [R1, R2, R3, R4, R5, R6, R7, R12,
            D0, D1, D2, D3, D4, D5, D6, D7,
            R15] in {
def JAL : FmtJ<0x37, (outs GPR32:$rd), (ins calltarget:$target),
              "jal\t$rd, $target",
              []> {
  let isCall = 1;
  let TSFlags{2-0} = 0b001;
  // JAL decodes via DecoderTableHaydn3232. Its opcode 0x37 is disjoint
  // from every other Haydn32 opcode (FmtI 0x20-0x2C, FmtBRCond 0x2D-0x32
  // FmtBR 0x33-0x36, FmtJR 0x38, FmtCSR 0x39-0x3A). The prior "SIMD 0x77
  // conflict" rationale was phantom: the DSP ops that share Inst[31:24]=0x77
  // (D_SDW_*_REG family) are isAsmParserOnly=1, which DecoderEmitter filters
  // out of the decoder trie (DecoderEmitter.cpp:1377). The custom JAL
  // pre-checks in HaydnDisassembler::getInstruction are removed.
  let DecoderNamespace = "Haydn32";
}

def JALR : FmtJR<0x38, (outs GPR32:$rd), (ins GPR32:$rs, calltarget:$target),
               "jalr\t$rd, $rs, $target", []> {
  let isTerminator = 1;
  let isCall = 1;
  let isIndirectBranch = 1;
  let TSFlags{2-0} = 0b001;
}

// Pseudo call instruction - expands to JAL_W (HaydnExpandPseudos).
// Used by CallLowering for direct calls. Must set isCall=1 so pre-RA
// MachineScheduler treats it as a call barrier (isSchedulingBoundary
// mayLoad+mayStore semantics). PseudoCALLIndirect already had isCall=1;
// omitting it here let misched reorder memory/ops across empty calls
// (seed7: empty tf_3_foo flips oracle). Expands after RA.
def PseudoCALL : HaydnPseudo<(outs GPR32:$rd), (ins calltarget:$target),
                              "call\t$target",
                              []> {
  let isCall = 1;
  let TSFlags{2-0} = 0b001;
}

// Pseudo indirect call — expands to JALR. Used by CallLowering for function
// pointer calls (callee is a register). Modeled as a call (same caller-saved
// clobbers as JAL/PseudoCALL) but NOT a terminator, since control returns
// unlike raw JALR which is isTerminator=1 (RET / indirect-branch use).
// a reg callee previously went through JAL, whose calltarget operand
// cannot hold a register, so the printer dropped the target and the ISS saw
// r0=0 -> self-loop on every fnptr call.
def PseudoCALLIndirect : HaydnPseudo<(outs GPR32:$rd), (ins GPR32:$rs),
                                      "calli\t$rs",
                                      []> {
  let isCall = 1;
  let TSFlags{2-0} = 0b001;
}
} // Defs = caller-saved clobbers for call instructions

// Indirect branch pseudo for jump table dispatch.
// Has the same successor list as G_BRJT and is expanded to JALR by AsmPrinter.
let isBarrier = 1, isTerminator = 1, isBranch = 1, isIndirectBranch = 1 in {
def BR_JT : HaydnPseudo<(outs), (ins GPR32:$addr, i32imm:$jt),
                          "br_jt\t$addr, $jt",
                          []>;
}

//===----------------------------------------------------------------------===//
// MC-level long branch pseudos (assembler relaxation)
//===----------------------------------------------------------------------===//
//
// Produced by HaydnAsmBackend::relaxInstruction when a conditional branch
// target exceeds the 16-bit signed offset range. Expanded by
// HaydnMCCodeEmitter to: inverted-branch + JAL (8 bytes total).
//
// Operands match the source conditional branch layout plus the target
// symbol from the original branch. The JAL component provides a 20-bit
// signed offset (±1MB range), far exceeding the conditional ±32KB limit.

// Two-register conditional branches: rs1, rs2, target
// HaydnLongBranchPseudo: branch pseudo relaxed by AsmBackend into inverted-cond + JAL.
// Defined here as alias of HaydnPseudo (codegen-only, not encoded directly).
class HaydnLongBranchPseudo<dag outs, dag ins, string asmstr, list<dag> pattern = []>
    : HaydnPseudo<outs, ins, asmstr, pattern> {
  let isBranch = 1;
  let isTerminator = 1;
}

def PseudoLongBEQ  : HaydnLongBranchPseudo<(outs), (ins GPR32:$rs1, GPR32:$rs2, brtarget:$offset), "long_beq\t$rs1, $rs2, $offset">;
def PseudoLongBNE  : HaydnLongBranchPseudo<(outs), (ins GPR32:$rs1, GPR32:$rs2, brtarget:$offset), "long_bne\t$rs1, $rs2, $offset">;
def PseudoLongBGE  : HaydnLongBranchPseudo<(outs), (ins GPR32:$rs1, GPR32:$rs2, brtarget:$offset), "long_bge\t$rs1, $rs2, $offset">;
def PseudoLongBGEU : HaydnLongBranchPseudo<(outs), (ins GPR32:$rs1, GPR32:$rs2, brtarget:$offset), "long_bgeu\t$rs1, $rs2, $offset">;
def PseudoLongBLT  : HaydnLongBranchPseudo<(outs), (ins GPR32:$rs1, GPR32:$rs2, brtarget:$offset), "long_blt\t$rs1, $rs2, $offset">;
def PseudoLongBLTU : HaydnLongBranchPseudo<(outs), (ins GPR32:$rs1, GPR32:$rs2, brtarget:$offset), "long_bltu\t$rs1, $rs2, $offset">;

// Single-register conditional branches: rs, target
def PseudoLongBEQZ : HaydnLongBranchPseudo<(outs), (ins GPR32:$rs, brtarget:$offset), "long_beqz\t$rs, $offset">;
def PseudoLongBNEZ : HaydnLongBranchPseudo<(outs), (ins GPR32:$rs, brtarget:$offset), "long_bnez\t$rs, $offset">;
def PseudoLongBGEZ : HaydnLongBranchPseudo<(outs), (ins GPR32:$rs, brtarget:$offset), "long_bgez\t$rs, $offset">;
def PseudoLongBLTZ : HaydnLongBranchPseudo<(outs), (ins GPR32:$rs, brtarget:$offset), "long_bltz\t$rs, $offset">;

// Unconditional branch (B pseudo → long B)
def PseudoLongB    : HaydnLongBranchPseudo<(outs), (ins brtarget:$offset), "long_b\t$offset">;

//===----------------------------------------------------------------------===//
// Hardware Loop Instructions
//===----------------------------------------------------------------------===//

// Hardware loop configuration pseudos.
// The asm printer expands these to SET_HWLOOP/SET_HWLOOP_F2 instructions
// with PC-relative offsets computed from the MBB symbols.
// Operands: sel (0=outer, 1=inner), loop_start MBB, loop_end MBB, trip count.
// hasSideEffects=1 matches LoopStart: programs HWLR; must not be DCE'd or
// freely reordered relative to the remat count producer (post-RA free-reg
// remat → SET dep chain).
let hasSideEffects = 1, isNotDuplicable = 1 in {
def SET_HWLOOP : HaydnPseudo<(outs), (ins uimm5:$sel, brtarget:$loop_start, brtarget:$loop_end, uimm16:$cnt),
                               "set_hwloop\t$sel, $loop_start, $loop_end, $cnt", []>;

def SET_HWLOOP_REG : HaydnPseudo<(outs), (ins uimm5:$sel, brtarget:$loop_start, brtarget:$loop_end, GPR32:$rs),
                                  "set_hwloop_f2\t$sel, $loop_start, $loop_end, $rs", []>;
} // hasSideEffects = 1, isNotDuplicable = 1

//===----------------------------------------------------------------------===//
// SMS-safe hardware-loop pseudos (AIE design).
//
// These pseudos are created by the GlobalISel selector when it lowers the
// upstream HardwareLoops IR intrinsics (llvm.set.loop.iterations
// llvm.loop.decrement, llvm.loop.decrement.reg). They participate in the
// standard analyzeBranch/insertBranch/removeBranch round-trip so the SMS
// ModuloScheduleExpander can clone+erase them safely (the fix for the
// expander crash). The post-RA HaydnHardwareLoops pass lowers them to
// the real SET_HWLOOP_REG encoding.
//
// Two loop models (mirrors AIE AIE2InstrInfo.td:550-572):
// ZOL (innermost): LoopStart (preheader) + PseudoLoopEnd (latch)
// JNZD (outer): LoopDec + LoopJNZ (counter-in-reg)
//===----------------------------------------------------------------------===//

// JNZD loops (counter-in-reg, for outer nested loops).
// LoopDec decrements the GPR trip counter and sets the condition for LoopJNZ.
let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def LoopDec : HaydnPseudo<(outs GPR32:$dst), (ins GPR32:$src),
                          "loop_dec\t$dst, $src", []>;

// LoopJNZ: jump-if-nonzero to the loop body. This is the latch terminator for
// JNZD loops — it branches back to the loop header while the counter != 0.
let hasSideEffects = 0, mayLoad = 0, mayStore = 0,
    isBranch = 1, isTerminator = 1 in
def LoopJNZ : HaydnPseudo<(outs), (ins GPR32:$tc, brtarget:$target),
                          "loop_jnz\t$tc, $target", []>;

// Zero-overhead loops (ZOL, innermost).
// LoopStart lives in the preheader and programs the hardware loop registers
// (HWLR_COUNT/BEGIN/END). The $adj immediate is the pipeliner's trip-count
// adjustment (prologue/epilogue correction), edited in-place by
// ZeroOverheadLoop::adjustTripCount.
let hasSideEffects = 1, isNotDuplicable = 1 in
def LoopStart : HaydnPseudo<(outs), (ins GPR32:$src, simm6:$adj),
                            "loop_start\t$src, $adj", []>;

// PseudoLoopEnd: the ZOL latch terminator. Represents the implicit hardware
// loop back-edge. isMeta = true so the scheduler/packetizer treats it as a
// placeholder (zero-cycle, no real encoding). isBranch + isTerminator (but
// NOT isBarrier) so it auto-derives isConditionalBranch — this is what
// makes analyzeBranch recognize it via the standard branch-predicate path.
// Single MBB operand: the loop body (self-back-edge for single-BB loops).
//
// (AIE-first layout): AIE binds the last-bundle END label as a symbol
// operand on PseudoLoopEnd and propagates it via setPreInstrSymbol on the
// preceding real MI. Haydn deliberately does NOT put the END MCSymbol on
// PseudoLoopEnd (keeps analyzeBranch/SMS operand shape stable); instead
// HaydnAsmPrinter::getOrCreateHwloopEndSym binds HWLR_END via
// setPreInstrSymbol on the latch's last real body MI ( pending multi-END).
let isBranch = 1, isTerminator = 1, isMeta = 1,
    hasSideEffects = 1, isNotDuplicable = 1,
    mayLoad = 0, mayStore = 0 in
def PseudoLoopEnd : HaydnPseudo<(outs), (ins brtarget:$loopBody),
                                "pseudo_loop_end\t$loopBody", []>;

//===----------------------------------------------------------------------===//
// System Instructions
//===----------------------------------------------------------------------===//

// WFI — S0 HINT. Matcher-visible logical like NOP; product encode is the
// WFI_S0 occupancy Fallback (Format E WFI<TBD> is not an admitted member).
// hasSideEffects=1; size is one product parcel (not HaydnPseudo / 0 bytes).
// AsmPrinter does not expand it: Finalize cutovers via occupancy, same as NOP.
let hasSideEffects = 1, isPseudo = 1, isCodeGenOnly = 0 in
def WFI : HaydnInst<0, (outs), (ins), "wfi", []> {
  let TSFlags{2-0} = 0b001;
  let Itinerary = Slot0_ALU;
}

// CSRR/CSRW — CSR read/write. The DB says:
// CSRR rt, uimm8 -> rt = CSR[uimm8] (rt is OUTPUT)
// CSRW uimm8, rs -> CSR[uimm8] = rs (rs is INPUT, no GPR out)
// The clean operand shape (CSRR: 1 out + 1 imm; CSRW: 0 out + 1 imm + 1 in)
// does NOT match the shared FmtCSR decoder: FmtCSR decodes three fields
// (rd, csr_addr, rs) into the MCInst for BOTH CSRR and CSRW (decoder table
// case 25), and the auto-generated printer indexes operands by position.
// Reducing the operand list therefore desyncs printer-vs-decoder and crashes
// the disassembler (`idx < size` in printOperand). So we keep the original
// symmetric 3-operand shape (rd + csr_addr + rs) for MC-layer fidelity and
// mark the unused direction with `isDead`/`isUndef` so the *Codegen* side
// treats them correctly. CSRW's hasSideEffects=1 is on the CSRW def
// below (dead CBR setup must not be DCE'd).
//
// CSRR — hasSideEffects=1 matches Format E members and RISCV Zicsr.
// Without it a dead CSRR is DCE'd (HaydnInst default 0) and a later
// CSRW can hoist across a missing read.
let hasSideEffects = 1 in
def CSRR : FmtCSR<0x39, (outs GPR32:$rd), (ins uimm8:$csr_addr, GPR32:$rs),
               "csrr\t$rd, $csr_addr", []> {
  let TSFlags{2-0} = 0b001;
  // $rs is not a real input for CSRR (DB reads only the CSR); it exists for
  // MC operand-count parity with the shared FmtCSR decoder (case 25 decodes
  // rd + csr_addr + rs for BOTH CSRR and CSRW). Regalloc ignores an unused
  // tied input operand; no correctness impact.
}

// CSRW — catalog 2-op (uimm8, rs). Matcher-visible isPseudo shell; product
// encode is Format E members. Residual CSRW_S0 until the FieldSlot hide.
// GISel CBR setup emits CSRW_W, not this opcode. hasSideEffects=1 so a
// dead CSRW is not DCE'd (WUR_AE_CBEGIN0/CEND0). The old 3-op FmtCSR
// dead-$rd shape was decoder-parity only and is gone: members and CSRW_S0
// are already 2-op.
let hasSideEffects = 1, isPseudo = 1, isCodeGenOnly = 0, Defs = [SFR],
    Itinerary = Slot0_ALU in
def CSRW : HaydnInst<0, (outs), (ins uimm8:$csr_addr, GPR32:$rs),
                    "csrw\t$csr_addr, $rs", []> {
  let TSFlags{2-0} = 0b001;
}

//===----------------------------------------------------------------------===//
// Slot 0 — Load/Store Unit (Basic scalar loads/stores)
//===----------------------------------------------------------------------===//

// FmtLS is defined in HaydnInstrFormats.td

let mayLoad = 1 in {
// LD32 is logical-only (public mnemonic ld32). Legal in S0|S1 via
// PlacementAlternative {LD32_S0, LD32_S1}; dual-load packing records slot
// on AltDescs and leaveRegion setDesc(member) — never bakes LD32_S1 into
// pre-pack MIR. Slot01_LD matches LD64.
def LD32 : FmtLS<0x3B, (outs GPR32:$rt), (ins GPR32:$rs, simm16:$offset),
                 "ld32\t$rt, $rs, $offset",
                 []> {
  let Itinerary = Slot01_LD;
}

def LD16 : FmtLS<0x3C, (outs GPR32:$rt), (ins GPR32:$rs, simm16:$offset),
                 "ld16\t$rt, $rs, $offset", []> {
  let TSFlags{2-0} = 0b001;
}

def LD8 : FmtLS<0x3D, (outs GPR32:$rt), (ins GPR32:$rs, simm16:$offset),
                "ld8\t$rt, $rs, $offset", []> {
  let TSFlags{2-0} = 0b001;
}

def LDU16 : FmtLS<0x3E, (outs GPR32:$rt), (ins GPR32:$rs, simm16:$offset),
                  "ldu16\t$rt, $rs, $offset", []> {
  let TSFlags{2-0} = 0b001;
}

def LDU8 : FmtLS<0x3F, (outs GPR32:$rt), (ins GPR32:$rs, simm16:$offset),
                 "ldu8\t$rt, $rs, $offset", []> {
  let TSFlags{2-0} = 0b001;
}
}

// plain 64-bit load — DR64, S0|S1 choice-set (Slot01_LD).
// Dual-load: post-RA HR tryAddProduct PlacementAlternatives → setDesc
// LD64_S0/S1 (no promoteLoads residual).
def LD64 : FmtLS<0x45, (outs DR64:$rt), (ins GPR32:$rs, simm16:$offset),
                "ld64\t$rt, $rs, $offset", []> {
  let Itinerary = Slot01_LD;
}

// FmtLS defaults mayLoad=1 for the load family above. Stores that inherit
// FmtLS must clear it: MayLoad|MayStore poisons AA, isStoreToStackSlot
// (requires mayStore && !mayLoad), and intra-cycle RAW.
let mayStore = 1, mayLoad = 0 in {
def ST32 : FmtLS<0x40, (outs), (ins GPR32:$rt, GPR32:$rs, simm16:$offset),
                 "st32\t$rt, $rs, $offset",
                 []> {
  let TSFlags{2-0} = 0b001;
}

// 64-bit store — DR64 value, GPR32 base + simm16 offset. FmtLS slot 0
// (TSFlags 0b001 / Slot0_LS), same as ST32. Not slot 1.
def ST64 : FmtLS<0x46, (outs), (ins DR64:$rt, GPR32:$rs, simm16:$offset),
                 "st64\t$rt, $rs, $offset",
                 []> {
}

def ST16 : FmtLS<0x41, (outs), (ins GPR32:$rt, GPR32:$rs, simm16:$offset),
                 "st16\t$rt, $rs, $offset", []> {
  let TSFlags{2-0} = 0b001;
}

def ST8 : FmtLS<0x42, (outs), (ins GPR32:$rt, GPR32:$rs, simm16:$offset),
                "st8\t$rt, $rs, $offset", []> {
  let TSFlags{2-0} = 0b001;
}
}

//===----------------------------------------------------------------------===//
// Family 4 — logical reg-offset LS (public MIR only).
//
// Operand shape: rt, base rs, offset-reg off (same as private *_S0 peers).
// AlternateInsts name-peer alts: LD32_REG_M0S0LS → LD32_REG_M0S0LS_S0 (etc.).
// ISel/PEI/FrameLowering emit these logical names; never the private *_S0
// encode peers (leaveRegion setDesc commits the member; Desc-as-is encode).
// Matcher-visible: FieldSlots use AsmVariantName fieldslot, so `ld32_reg`
// must match these shells. Unused DecoderNamespace keeps hypothesized
// Inst{} out of a live decode trie.
//===----------------------------------------------------------------------===//
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in {
let mayLoad = 1 in {
def LD32_REG_M0S0LS : HaydnInst<4, (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$off),
    "ld32_reg\t$rt, $rs, $off", []> {
  let Itinerary = Slot0_LS;
  let TSFlags{2-0} = 0b001;
}
def LD64_REG_M0S0LS : HaydnInst<4, (outs DR64:$rt), (ins GPR32:$rs, GPR32:$off),
    "ld64_reg\t$rt, $rs, $off", []> {
  let Itinerary = Slot0_LS;
  let TSFlags{2-0} = 0b001;
}
} // mayLoad
let mayStore = 1, mayLoad = 0 in {
def ST32_REG_M0S0LS : HaydnInst<4, (outs), (ins GPR32:$rt, GPR32:$rs, GPR32:$off),
    "st32_reg\t$rt, $rs, $off", []> {
  let Itinerary = Slot0_LS;
  let TSFlags{2-0} = 0b001;
}
def ST64_REG_M0S0LS : HaydnInst<4, (outs), (ins DR64:$rt, GPR32:$rs, GPR32:$off),
    "st64_reg\t$rt, $rs, $off", []> {
  let Itinerary = Slot0_LS;
  let TSFlags{2-0} = 0b001;
}
} // mayStore
} // matcher-visible REG logicals

//===----------------------------------------------------------------------===//
// Slot 1/2 — ALU64/SIMD Instructions (Representative subset)
//===----------------------------------------------------------------------===//

// 64-bit arithmetic operations
def ADD64 : FmtALU64<0x50, 0x000, (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
                     "add64\t$rd, $rs1, $rs2",
                     []> {
  let isCommutable = 1;
  let TSFlags{2-0} = 0b110;
}

def SUB64 : FmtALU64<0x51, 0x000, (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
                     "sub64\t$rd, $rs1, $rs2",
                     []> {
  let TSFlags{2-0} = 0b110;
}

def AND64 : FmtALU64<0x52, 0x000, (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
                     "and64\t$rd, $rs1, $rs2",
                     []> {
  let isCommutable = 1;
  let TSFlags{2-0} = 0b110;
}

def OR64 : FmtALU64<0x53, 0x000, (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
                    "or64\t$rd, $rs1, $rs2",
                    []> {
  let isCommutable = 1;
  let TSFlags{2-0} = 0b110;
}

def XOR64 : FmtALU64<0x54, 0x000, (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
                     "xor64\t$rd, $rs1, $rs2",
                     []> {
  let isCommutable = 1;
  let TSFlags{2-0} = 0b110;
}

// SLLI64/SRLI64/SRAI64 are defined as the 48-bit WideImm immediate forms in
// HaydnInstrFormatsC.td (opcodes 0x18/0x17/0x19, "$rtd, $imm"). The old
// register-operand FmtALU64<0x55/0x56/0x57> defs here collided ("def already
// exists") and were dead: empty patterns, no C++ references, and a malformed
// asm string ("$rd, $rs1" while declaring 2 ins). Removed to unblock the
// build; the WideImm forms are what the spec and asm tests use.

// SIMD X2 operations (dual 32-bit)
def X2ADD32 : FmtALU64<0x58, 0x000, (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
                       "x2add32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
}

def X2SUB32 : FmtALU64<0x59, 0x000, (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
                       "x2sub32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
}

// X2MUL32 — DB restricts to slot 1 only (capstone-schedule-port-audit
// §(b)-1). Override FmtALU64_2Dest's Slot12_ALU + slot-1/2 bits with Slot1_ALU
// + slot-1-only bit so two of these cannot pack into one bundle.
// (Path B): TRUE 2-output (golden DR_Write_Port=[rtd1,rtd2]). The legacy
// dag binds 2 dests + 2 srcs so MI.setDesc(legacy -> _S<k>) preserves the
// operand count (4) and def count (2); the 2nd dest $rtd2 is encoded into the
// legacy funct slot (bits{9-6}) — a tablegen-consistency device only, since
// leaveRegion setDesc(member) commits the slot form before emit and the
// Haydn32 decoder table is no longer consulted. See FmtMAC2Dest + the
// FmtALU64_2Dest header comment for the full rationale.
def X2MUL32 : FmtALU64_2Dest<0x5A, 0x00,
                             (outs DR64:$rd, DR64:$rtd2), (ins DR64:$rs1, DR64:$rs2),
                             "x2mul32\t$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot1_ALU;
  let TSFlags{2-0} = 0b010;
}

// SIMD X4 operations (quad 16-bit)
def X4ADD16 : FmtALU64<0x5B, 0x000, (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
                       "x4add16\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
}

// X4MUL16 — golden D-MAC slots 1+2 (X4MUL16_S1/S2 in HaydnFormatsMAC.td).
// (Path B): TRUE 2-output (golden DR_Write_Port=[rtd1,rtd2]). Not G_MUL:
// four signed 16x16->32 products pack into rtd1={p3,p2}, rtd2={p1,p0}.
// C/IR: only llvm.haydn.x4mul16 / haydn_x4mul16 (dpair). Elementwise
// G_MUL <4 x i16> is scalarized in the legalizer — never this opcode.
// $rtd2 is encoded into the legacy funct slot (bits{9-6}) — same device as
// X2MUL32 / FmtMAC2Dest. See FmtALU64_2Dest header comment for the rationale.
def X4MUL16 : FmtALU64_2Dest<0x5C, 0x00,
                             (outs DR64:$rd, DR64:$rtd2), (ins DR64:$rs1, DR64:$rs2),
                             "x4mul16\t$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_ALU;
  let TSFlags{2-0} = 0b110; // slots 1+2 (was 0b010 slot1-only; golden has both)
}

def X4SUB16 : FmtALU64<0x5D, 0x000, (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
                       "x4sub16\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
}

// SIMD X2 shift operations (dual 32-bit)
// X2 SIMD shifts: per spec, rtd=DR64, rsd=DR64, rs=GPR32 (shift amount).
// The shift amount comes from a GPR32 register (signed 6-bit value in rs[5:0]).
def X2SLL32 : FmtALU64<0x64, 0x000, (outs DR64:$rd), (ins DR64:$rs1, GPR32:$rs2),
                      "x2sll32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
}

def X2SRL32 : FmtALU64<0x65, 0x000, (outs DR64:$rd), (ins DR64:$rs1, GPR32:$rs2),
                      "x2srl32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
}

def X2SRA32 : FmtALU64<0x66, 0x000, (outs DR64:$rd), (ins DR64:$rs1, GPR32:$rs2),
                      "x2sra32\t$rd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
}

//===----------------------------------------------------------------------===//
// SFR compare / conditional-move / unary ALU64 logicals.
//
// Logical identities are non-encoding: product Inst bits live only on private
// *_S1/*_S2 members (HaydnFormatsALU64.td). Post-RA leaveRegion/Finalize
// setDesc materializes the chosen member before pack/printer. Do not allocate
// sequential 0x67 funct encodings on these shells (legacy FmtALU64Unary
// fabrication retired). isPseudo=1 keeps residual-executable law fail-closed
// if a logical survives setDesc; members emit after materialize so selected
// ops are not silently dropped.
//
// ISA-27: 32-bit SEQ32/SLT32/…/MOVT32 family is GPR-coupled (no SFR).
// 64-bit/SIMD compare/cmov family uses Defs/Uses=[SFR] with hasSideEffects=0
// so SMS does not treat them as global memory barriers.
//===----------------------------------------------------------------------===//

// Scalar 64-bit SFR compare (unary DR64 → sets SFR).
// Per spec, these set SFR = (condition) ? 4'b1111 : 4'b0000. The DR64 dst
// is the passthrough operand (intrinsic returns the input so the chain
// isn't DCE'd — see dr64-compare-intrinsics.ll).
//
// Explicit SFR Defs/Uses keep cmp→cmov RAW correct without memory barriers
// (hasSideEffects=1 would serialize loads/stores as Ord Barrier).
// Matcher-visible (isCodeGenOnly=0): public `seq64`/`slt64`/`sle64` must
// not depend on residual `_S*` FieldSlots. isPseudo: non-encoding shell.
let hasSideEffects = 0, Defs = [SFR], isCodeGenOnly = 0, isPseudo = 1,
    Itinerary = Slot12_ALU in {
def SEQ64 : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                      "seq64\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def SLT64 : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                      "slt64\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def SLE64 : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                      "sle64\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
}  // hasSideEffects = 0, Defs = [SFR]

// Scalar 64-bit SFR conditional move (unary DR64 → reads SFR, sets dst).
let hasSideEffects = 0, Uses = [SFR], isCodeGenOnly = 0, isPseudo = 1,
    Itinerary = Slot12_ALU in {
def MOVT64 : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                       "movt64\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def MOVF64 : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                       "movf64\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
}  // hasSideEffects = 0, Uses = [SFR]

// Unary ALU64 logicals — non-encoding shells; private peers in FormatsALU64.
// Matcher-visible (NOP pattern): isCodeGenOnly hid ABS64 so `{ abs64 … }`
// matched only ABS64_S1/S2. FieldSlot hide requires these in the matcher.
let isCodeGenOnly = 0, isPseudo = 1, Itinerary = Slot12_ALU in {
def X2ABS32S : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                         "x2abs32s\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}

// ABS64 / ABS64S / NEG64 / NEG64S — golden slot1/2 ALU. GISel selects the
// logical; setDesc commits ABS64*_S1/_S2 (FormatsALU64).
def ABS64 : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                      "abs64\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def ABS64S : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                       "abs64s\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def NEG64 : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                      "neg64\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def NEG64S : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                       "neg64s\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}

// NOT64 — private encode peers NOT64_S1/S2 (FormatsALU64).
def NOT64 : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                      "not64\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}

// X2 abs/neg unary family (golden slot1/2 ALU Index 81-86).
// setDesc → X2*_S1/_S2 members (FormatsALU64).
def X2ABS32 : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                        "x2abs32\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X2NEG32 : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                        "x2neg32\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X2NEG32S : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                         "x2neg32s\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X2NEG32_L : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                          "x2neg32_l\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X2NEG32S_L : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                           "x2neg32s_l\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}

// X4ABS16 / X4ABS16S — setDesc → X4ABS16{,S}_S1/_S2 (FormatsALU64).
def X4ABS16 : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                        "x4abs16\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X4ABS16S : HaydnInst<0, (outs DR64:$rd), (ins DR64:$rs1),
                         "x4abs16s\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}

// R_GD / R_GG logicals — setDesc commits *_S1/*_S2 members.
def NSA64 : HaydnInst<0, (outs GPR32:$rd), (ins DR64:$rs1),
                      "nsa64\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def NSAZ64 : HaydnInst<0, (outs GPR32:$rd), (ins DR64:$rs1),
                       "nsaz64\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def NSA32_L : HaydnInst<0, (outs GPR32:$rd), (ins DR64:$rs1),
                        "nsa32_l\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def NSAZ32_L : HaydnInst<0, (outs GPR32:$rd), (ins DR64:$rs1),
                         "nsaz32_l\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def NSA16_L : HaydnInst<0, (outs GPR32:$rd), (ins DR64:$rs1),
                        "nsa16_l\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def NSAZ16_L : HaydnInst<0, (outs GPR32:$rd), (ins DR64:$rs1),
                         "nsaz16_l\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def POPCOUNT64 : HaydnInst<0, (outs GPR32:$rd), (ins DR64:$rs1),
                           "popcount64\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X2HMAX32 : HaydnInst<0, (outs GPR32:$rd), (ins DR64:$rs1),
                         "x2hmax32\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X2HMIN32 : HaydnInst<0, (outs GPR32:$rd), (ins DR64:$rs1),
                         "x2hmin32\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X4HMAX16 : HaydnInst<0, (outs GPR32:$rd), (ins DR64:$rs1),
                         "x4hmax16\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X4HMIN16 : HaydnInst<0, (outs GPR32:$rd), (ins DR64:$rs1),
                         "x4hmin16\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def LOG2 : HaydnInst<0, (outs GPR32:$rd), (ins GPR32:$rs1),
                     "log2\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def EXP2 : HaydnInst<0, (outs GPR32:$rd), (ins GPR32:$rs1),
                     "exp2\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def RECIP : HaydnInst<0, (outs GPR32:$rd), (ins GPR32:$rs1),
                      "recip\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def SQRT : HaydnInst<0, (outs GPR32:$rd), (ins GPR32:$rs1),
                     "sqrt\t$rd, $rs1", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
} // isPseudo / Slot12_ALU logical unary shells (matcher-visible)

// X2 SIMD compare → SFR only. Golden/ISA: "X2SEQ32 rsd1, rsd2" (2 ops).
// No DR dest write; selector COPY-passthroughs rs1 for the C return value.
let Defs = [SFR] in {
def X2SEQ32 : HaydnInst<4, (outs), (ins DR64:$rs1, DR64:$rs2),
                        "x2seq32\t$rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X2SLT32 : HaydnInst<4, (outs), (ins DR64:$rs1, DR64:$rs2),
                        "x2slt32\t$rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X2SLE32 : HaydnInst<4, (outs), (ins DR64:$rs1, DR64:$rs2),
                        "x2sle32\t$rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
}  // Defs = [SFR]

// X2 SIMD conditional move. Golden/ISA: "X2MOVT32 rtd, rsd" (2 ops, RMW).
// Two-address seed: $rd = $rs1 (false lanes); $rsd is true-path source.
let Uses = [SFR] in {
def X2MOVF32 : HaydnInst<4, (outs DR64:$rd), (ins DR64:$rs1, DR64:$rsd),
                         "x2movf32\t$rd, $rsd", []> {
  let Constraints = "$rd = $rs1";
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X2MOVT32 : HaydnInst<4, (outs DR64:$rd), (ins DR64:$rs1, DR64:$rsd),
                         "x2movt32\t$rd, $rsd", []> {
  let Constraints = "$rd = $rs1";
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
}  // Uses = [SFR]

// X4 SIMD compare → SFR only (same 2-op golden shape as X2).
let Defs = [SFR] in {
def X4SEQ16 : HaydnInst<4, (outs), (ins DR64:$rs1, DR64:$rs2),
                        "x4seq16\t$rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X4SLT16 : HaydnInst<4, (outs), (ins DR64:$rs1, DR64:$rs2),
                        "x4slt16\t$rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X4SLE16 : HaydnInst<4, (outs), (ins DR64:$rs1, DR64:$rs2),
                        "x4sle16\t$rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
}  // Defs = [SFR]

// X4 SIMD conditional move (same RMW 2-op shape as X2MOVT32).
let Uses = [SFR] in {
def X4MOVF16 : HaydnInst<4, (outs DR64:$rd), (ins DR64:$rs1, DR64:$rsd),
                         "x4movf16\t$rd, $rsd", []> {
  let Constraints = "$rd = $rs1";
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def X4MOVT16 : HaydnInst<4, (outs DR64:$rd), (ins DR64:$rs1, DR64:$rsd),
                         "x4movt16\t$rd, $rsd", []> {
  let Constraints = "$rd = $rs1";
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
}  // Uses = [SFR]

// X4SELI16 — per-lane 16-bit select with 4-bit immediate lane mask.
// Two DR64 sources + 4-bit imm. funct6=0x06 distinguishes this from the
// 10-bit-funct space.
def X4SELI16 : FmtALU64SelImm<0x67, 0x06, (outs DR64:$rd),
                              (ins DR64:$rs1, DR64:$rs2, uimm4:$imm),
                              "x4seli16\t$rd, $rs1, $rs2, $imm", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}

// X2/X4 SIMD shift-immediate — proper binary encoding.
//
// Retires the AsmPrinter text-emit hack (prior revision): these 8
// ops were previously bare HaydnInst with no encoding bits, emitted directly
// as assembly text from HaydnAsmPrinter::emitInstruction. They now have real
// binary encodings via FmtALU64ShiftImm (shamt in funct[4:0]) for the
// standalone 32-bit path, and shamt-in-opcode-low-bits for the bundle path.
//
// Operand order (rd, rs, shamt) is preserved from the selector's buildInstr
// calls (HaydnInstructionSelector.cpp) — opaque to the change. Immediate
// narrowed simm16 → uimm5 (X2*, 32-bit lanes, 0..31) / uimm4 (X4*, 16-bit
// lanes, 0..15); no C++ range guard exists, so this is safe.
//
// Standalone opcodes 0x69–0x70 are allocated in a free range (verified: no
// Fmt*<0x69..0x70> anywhere).
def X2SLLI32  : FmtALU64ShiftImm<0x69, (outs DR64:$rd),
                    (ins DR64:$rs1, uimm5:$shamt),
                    "x2slli32\t$rd, $rs1, $shamt", []>;
def X2SRLI32  : FmtALU64ShiftImm<0x6A, (outs DR64:$rd),
                    (ins DR64:$rs1, uimm5:$shamt),
                    "x2srli32\t$rd, $rs1, $shamt", []>;
def X2SRAI32  : FmtALU64ShiftImm<0x6B, (outs DR64:$rd),
                    (ins DR64:$rs1, uimm5:$shamt),
                    "x2srai32\t$rd, $rs1, $shamt", []>;
def X2SRAI32R : FmtALU64ShiftImm<0x6C, (outs DR64:$rd),
                    (ins DR64:$rs1, uimm5:$shamt),
                    "x2srai32r\t$rd, $rs1, $shamt", []>;
def X4SLLI16  : FmtALU64ShiftImm<0x6D, (outs DR64:$rd),
                    (ins DR64:$rs1, uimm4:$shamt),
                    "x4slli16\t$rd, $rs1, $shamt", []>;
def X4SRLI16  : FmtALU64ShiftImm<0x6E, (outs DR64:$rd),
                    (ins DR64:$rs1, uimm4:$shamt),
                    "x4srli16\t$rd, $rs1, $shamt", []>;
def X4SRAI16  : FmtALU64ShiftImm<0x6F, (outs DR64:$rd),
                    (ins DR64:$rs1, uimm4:$shamt),
                    "x4srai16\t$rd, $rs1, $shamt", []>;
def X4SRAI16R : FmtALU64ShiftImm<0x70, (outs DR64:$rd),
                    (ins DR64:$rs1, uimm4:$shamt),
                    "x4srai16r\t$rd, $rs1, $shamt", []>;

// SFR ↔ GPR32 transfer. These have unusual operand shapes:
// MOVESFR2GPR — no input regs (reads SFR implicitly), outputs GPR32.
// MOVEGPR2SFR — no output regs (writes SFR implicitly), inputs GPR32.
// ZERO_SFR — no regs at all (writes SFR implicitly).
// Use HaydnInst with explicit Inst{...}=... so the operand fields encode
// correctly even when the MIR operand list has no $rd / no $rs.
//
// : explicit Uses/Defs=[SFR] so SSA pred expand
// (SLT → MOVESFR2GPR; MOVEGPR2SFR → MOVT) cannot be reordered across
// independent predicate epochs. hasSideEffects alone is not enough for
// dataflow; D169 modeled the same Uses/Defs split.
let hasSideEffects = 1, Uses = [SFR] in {
def MOVESFR2GPR : HaydnInst<4, (outs GPR32:$rd), (ins),
                            "movesfr2gpr\t$rd", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot12_ALU;
  bits<4> rd;
  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = 0x67;
  let Inst{21 - 18} = rd;
  let Inst{17 - 14} = 0;  // no src reg
  let Inst{13 - 10} = 0;
  let Inst{9 - 0} = 0x198;
}
}  // hasSideEffects = 1, Uses = [SFR]
let hasSideEffects = 1, Defs = [SFR] in {
def MOVEGPR2SFR : HaydnInst<4, (outs), (ins GPR32:$rs),
                            "movegpr2sfr\t$rs", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot12_ALU;
  bits<4> rs;
  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = 0x67;
  let Inst{21 - 18} = 0;  // no dst reg (implicit SFR write)
  let Inst{17 - 14} = rs;
  let Inst{13 - 10} = 0;
  let Inst{9 - 0} = 0x199;
}
def ZERO_SFR : HaydnInst<4, (outs), (ins),
                         "zero_sfr", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot12_ALU;
  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = 0x67;
  let Inst{21 - 18} = 0;
  let Inst{17 - 14} = 0;
  let Inst{13 - 10} = 0;
  let Inst{9 - 0} = 0x19A;
}
}  // hasSideEffects = 1, Defs = [SFR]

//===----------------------------------------------------------------------===//
// Cross-RF move: DR64 half-lane → GPR32 (MOVE32_DR_L / MOVE32_DR_H)
//
// DB (haydn_instruction_db.json "MOVE32_DR_L"/"MOVE32_DR_H", added):
// MOVE32_DR_L rt, rsd — rt = rsd[31:00] (slot 0 ALU, DR_Read=[rsd], GPR_Write=[rt])
// MOVE32_DR_H rt, rsd — rt = rsd[63:32] (slot 0 ALU, DR_Read=[rsd], GPR_Write=[rt])
// These are the first *real* DR→GPR cross-bank moves in the ISA. The earlier
// MOV_DR64_TO_GPR pseudo is a SP-spill shim (ISA-10: no prior native
// cross-bank move); these two slot-0 ALU ops are the hardware path that
// shim has been approximating. The encoding is
// hypothesized (no bit layout in the.docx) so they are isAsmParserOnly=1
// matching the wave-2 DSP-op convention (ISA-02). Operand shape (1 DR64 src
// 1 GPR32 dst) does not fit FmtALU32 (all-GPR), so use HaydnInst<4> with
// explicit Inst bits like the SFR transfers above:
// [31:30]=01 | [29:22] opcode(8) | [21:18] rt(GPR32) | [17:14] rsd(DR64)
// [13:10]=0 | [9:0] funct (0x1A0 lower / 0x1A1 upper)
//===----------------------------------------------------------------------===//
// isAsmParserOnly removed — these have real encoding bits (opcode 0x67
// funct 0x1A0/0x1A1) and are the critical DR64→GPR32 path for AE_MOVAD32_H/L.
let isPseudo = 0 in {
def MOVE32_DR_L : HaydnInst<4, (outs GPR32:$rt), (ins DR64:$rsd),
    "move32_dr_l\t$rt, $rsd", []> {
  let TSFlags{2-0} = 0b001;   // Slot 0 ALU
  let TSFlags{4-3} = 0b01;    // 32-bit width
  let Itinerary = Slot0_ALU;
  bits<4> rt;
  bits<4> rsd;
  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = 0x67;
  let Inst{21 - 18} = rt;
  let Inst{17 - 14} = rsd;
  let Inst{13 - 10} = 0;
  let Inst{9 - 0} = 0x1A0;
}

def MOVE32_DR_H : HaydnInst<4, (outs GPR32:$rt), (ins DR64:$rsd),
    "move32_dr_h\t$rt, $rsd", []> {
  let TSFlags{2-0} = 0b001;   // Slot 0 ALU
  let TSFlags{4-3} = 0b01;    // 32-bit width
  let Itinerary = Slot0_ALU;
  bits<4> rt;
  bits<4> rsd;
  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = 0x67;
  let Inst{21 - 18} = rt;
  let Inst{17 - 14} = rsd;
  let Inst{13 - 10} = 0;
  let Inst{9 - 0} = 0x1A1;
}
}  // isAsmParserOnly = 1, isPseudo = 0

//===----------------------------------------------------------------------===//
// Circular-buffer setup pseudos — Hexagon-style.
//
// Haydn configures a CBR set by writing CBR_BEGIN[cbr_sel] / CBR_END[cbr_sel]
// as CSRs (0x2C/0x2D for set 0, 0x2E/0x2F for set 1) via CSRW. There is no
// dedicated SETCBR opcode — the setup IS a CSRW. Pseudos take (cbr_sel, value);
// ExpandPseudos emits final CSRW_W with that CSR map plus implicit-def
// CBR0/CBR1 before post-RA pack. Residual SETCBR at late Verify/AsmPrinter is
// fatal (one-to-one MC). Mirrors Hexagon `m0=rN; cs0=rN` boundary setup via
// existing CSR space. cbr_sel is an i32 immediate (0 or 1).
//===----------------------------------------------------------------------===//
let hasSideEffects = 1 in {
def SETCBR_BEGIN : HaydnPseudo<(outs), (ins i32imm:$cbr_sel, GPR32:$val),
    "setcbr_begin\t$cbr_sel, $val", []>;

def SETCBR_END : HaydnPseudo<(outs), (ins i32imm:$cbr_sel, GPR32:$val),
    "setcbr_end\t$cbr_sel, $val", []>;
}  // hasSideEffects = 1

// Fractional operations
//
// MULQ31 / MACQ31 / MULQ63 REMOVED — they do not exist in the ISA DB
// (the golden slot1_mac/slot2_mac JSONs list 42 FF2MULA32RS_*/FF2MUL32RS_*
// FF2MULF32RS_* variants as the real Q-format MAC family). The phantom defs
// had no real opcode assignment and emitted garbage bundles. The source-level
// builtins/intrinsics (haydn_mulq31/macq31/mulq63) are preserved for
// compatibility and lowered in HaydnInstructionSelector to real ISA sequences
// (MULSSH for mulq31, MULSSH+ADD32 for macq31, MUL64_LL for mulq63). See.
//
// No scalar GPR MAC opcode: MULA* targets DR64, not GPR32. haydn_mac32
// selects to MULL + ADD32. PostSelect formMACs is FATED (deleted).

//===----------------------------------------------------------------------===//
// Slot 1/2 — MAC Instructions (Multiply-Accumulate)
//===----------------------------------------------------------------------===//
// These instructions execute on the MAC units in slots 1 and 2.
// 32-bit multiply instructions (MULL, MULSSH, MULSUH, MULUUH) are defined in
// HaydnInstrInfoAuto.td from the ISA spec JSON.
// 64-bit multiply instructions (MUL64_*) are defined in HaydnInstrInfoAuto.td.
// Multiply-Accumulate instructions (MULA64_*) are defined in HaydnInstrInfoAuto.td.

// SIMD MAC instructions (X2MULA32, X2MULAPL32) are defined in HaydnInstrInfoAuto.td

// no durable MIR opcodes LD32_S1/LD64_S1. Logical loads are LD32/LD64
// only; S1 encode forms live in HaydnFormatsLD.td as encode-only
// peers (`LD32_S1` / `LD64_S1` under DecoderNamespace FlexEmitterOnly).

//===----------------------------------------------------------------------===//
// Fused post-increment loads (AGU writeback, spec D_LDW_POST_IMM
// S_LW_POST_IMM family). Single instruction equivalent of HiFi's ae_l64.ip
// ae_l32.ip: load from [rs], then rs += (imm6 << ScaleShift).
//
// Two-def tied form: $rt (loaded data) AND $rs_wb (base writeback via the AGU
// dedicated write wordline per Format E AGU writeback — consumes one LS
// slot, no GPR-compute write port). The Constraints = "$rs = $rs_wb" forces
// regalloc to coalesce the input base and the writeback so the encoded rs
// field carries both. The MC code-emitter encodes $rs (operand index 2) into
// the rs field; the decoder reconstructs $rs_wb from the same field as a tied
// def of operand 1.
//
// $scaled_imm is the imm6 *element index* (stride_bytes >> ScaleShift); the
// per-width scale (<<3 for D_LDW*, <<2 for S_LW*) is applied in
// HaydnExpandPostIncEarly when the LD64_POST_INC / LD32_POST_INC pseudo is
// fused. Fallback to the 2-instruction LD + ADDI32 split happens when the
// stride is non-zero-offset, not a multiple of the access width, or out of
// imm6 range.
//
// Encoding: same s1 FU=LD layout as LD64_S1/LD32_S1 (4-bit opcode space; only
// 0x0/0x1 used by the plain loads). LD64_POST = 0x2, LD32_POST = 0x3 in the
// s1 FU=LD opcode space. imm6 occupies
// the imm8 field sign-extended (imm6 fits within imm8).
//===----------------------------------------------------------------------===//

// D_LDW_POST_IMM rtd, rs, imm6 — load 64b from [rs], then rs += (imm6<<3).
// Spec: Database/haydn_instruction_db.json "D_LDW_POST_IMM".
// HiFi analog: ae_l64.ip. Two-def: data destination $rt (DR64) and base
// writeback $rs_wb (tied to $rs via the AGU bypass).
let mayLoad = 1, Constraints = "$rs = $rs_wb" in
def LD64_POST : FmtLSPostInc<0x47, (outs DR64:$rt, GPR32:$rs_wb),
                              (ins GPR32:$rs, simm6:$scaled_imm),
                              "ld64.post\t$rt, $rs, $scaled_imm", []>;

// S_LW_POST_IMM rt, rs, imm6 — load 32b word from [rs], then rs += (imm6<<2).
// Spec: Database/haydn_instruction_db.json "S_LW_POST_IMM".
// HiFi analog: ae_l32.ip / C-language *p++. Two-def: data destination $rt
// (GPR32) and base writeback $rs_wb (tied to $rs via the AGU bypass).
let mayLoad = 1, Constraints = "$rs = $rs_wb" in
def LD32_POST : FmtLSPostInc<0x48, (outs GPR32:$rt, GPR32:$rs_wb),
                              (ins GPR32:$rs, simm6:$scaled_imm),
                              "ld32.post\t$rt, $rs, $scaled_imm", []>;

//===----------------------------------------------------------------------===//
// Fused post-increment stores (AGU writeback, spec D_SDW_POST_IMM
// S_SW_POST_IMM family). Mirrors the load post-inc pair above with mayStore=1:
// store $rt to [rs], then rs += (imm6 << ScaleShift).
//
// The spec DB (haydn_instruction_db.json) confirms these are real Load/Store
// instructions:
// S_SW_POST_IMM rt, rs, imm6 — GPR_Read_Port=[rt,rs], GPR_Write_Port=[rs]
// D_SDW_POST_IMM rtd, rs, imm6 — DR_Read_Port=[rtd], GPR_Write_Port=[rs]
// The prior comment in HaydnExpandPostIncEarly ("no MC/silicon-backed fused
// post-increment store encoding") was incorrect — these have always been in
// the ISA. HiFi analog: ae_s32.ip / ae_s64.ip.
//
// Encoding: identical s1 FU=LD layout as LD64_POST / LD32_POST. The
// MCInst operand layout is [rt/rsd, rs_wb, rs, scaled_imm] — the same IsFused
// Post path in HaydnMCCodeEmitter handles both. Opcodes 0x4/0x5 in the 4-bit
// s1 LD opcode space (loads use 0x2/0x3).
//
// The DB-named S_SW_POST_IMM / D_SDW_POST_IMM in HaydnInstrInfoAuto.td model
// these as Slot0_LS; the encoding hypothesis here (matching the load post-inc
// pair) places them in the s1 FU=LD space. Both describe the same operation;
// ST32_POST / ST64_POST are the codegen-selected forms.
//===----------------------------------------------------------------------===//

// D_SDW_POST_IMM rtd, rs, imm6 — store 64b rtd to [rs], then rs += (imm6<<3).
// HiFi analog: ae_s64.ip.
let mayStore = 1, mayLoad = 0, Constraints = "$rs = $rs_wb" in
def ST64_POST : FmtLSPostInc<0x49, (outs GPR32:$rs_wb),
                              (ins DR64:$rt, GPR32:$rs, simm6:$scaled_imm),
                              "st64_post\t$rt, $rs, $scaled_imm", []>;

// S_SW_POST_IMM rt, rs, imm6 — store 32b rt to [rs], then rs += (imm6<<2).
// HiFi analog: ae_s32.ip / C-language *p++ = x.
let mayStore = 1, mayLoad = 0, Constraints = "$rs = $rs_wb" in
def ST32_POST : FmtLSPostInc<0x4A, (outs GPR32:$rs_wb),
                              (ins GPR32:$rt, GPR32:$rs, simm6:$scaled_imm),
                              "st32_post\t$rt, $rs, $scaled_imm", []>;

//===----------------------------------------------------------------------===//
// Lane stores: store one 32-bit lane of a DR64 to memory.
// These eliminate the MOVE32_DR_L/H + ST32 cross-bank round-trip (2 instrs
// > 1). Defined via FmtLaneStore with real encoding bits.
// D_SW_L_WITH_IMM: mem32[rs + (imm6<<2)] = rtd[31:00] (low lane)
// D_SW_H_WITH_IMM: mem32[rs + (imm6<<2)] = rtd[63:32] (high lane)
// Spec: Database/haydn_instruction_db.json "D_SW_L_WITH_IMM" / "D_SW_H_WITH_IMM".
//===----------------------------------------------------------------------===//

def D_SW_L_WITH_IMM : FmtLaneStore<0x4B,
    (outs), (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_sw_l_with_imm\t$rtd, $rs, $scaled_imm", []>;

def D_SW_H_WITH_IMM : FmtLaneStore<0x4C,
    (outs), (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_sw_h_with_imm\t$rtd, $rs, $scaled_imm", []>;


//===----------------------------------------------------------------------===//
// 48-bit WIDE instruction definitions. TableGen size-6 shell; product encode is Format E member.
//
// STATUS : PARTIALLY RETIRED. subsequent the 48-bit MC
// encoder/decoder paths are gutted (legacy). Of the original 54
// Fmt48_* defs, 33 with ZERO live C++ consumers were removed :
// Class 000 GPRImm: ADDI32S_W, SUBI32_W, SUBI32S_W, ANDI32_W, XORI32_W
// LUI_W (6) — KEPT: ADDI32_W, ORI32_W.
// Class 010 DR-Imm RI20: all 7 (ADDI64/SUBI64/ANDI64/ORI64/XORI64
// SLTI64/SLTUI64) + the Fmt48_WideDR_RI20 class.
// Class 011 DRX: MAXI64, MINI64 + the Fmt48_WideDRX class.
// Class 100 CTRL: CSRR_W, ZERO_GPR_W + the Fmt48_WideZeroGPR class.
// Class 101 WIDE-LS: all 14 _WL + the Fmt48_WideLS class.
// MOVEI_H, MOVEI_L + the Fmt48_MOVEI class (HaydnInstrInfoAuto.td).
// The 21 KEPT defs below (ADDI32_W, ORI32_W, JAL_W, JALR_W, BEQ/BNE/BGE
// BGEU/BLT/BLTU_W, BEQZ/BNEZ/BGEZ/BLTZ_W, CSRW_W, SET_HWLOOP_W
// F2_W/REG_W, SLLI64/SRLI64/SRAI64) retain live hand-written C++ consumers
// (Haydn:: enum refs in AsmPrinter, InstrInfo, ConditionOptimizer
// HazardRecognizer, HardwareLoops, ExpandPseudos, MatInt).
//
// Spec-aligned §5 opcodes. The Class 000 GPR WIDE variants (ADDI32_W
// ORI32_W) are re-encoded onto the §5 opcode map; the previous obsolete
// "OLD §5 imm32-in-bits[43:12]" layout was retired in HaydnInstrFormatsC.td.
// These coexist with the existing 32-bit FmtI / FmtBR / FmtJ / FmtCSR forms
// (unchanged for short-offset code).
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Class 000 — GPR Immediate (§5.4). GPR32 read-modify-write, 20-bit imm.
//===----------------------------------------------------------------------===//

// ADDI32→WIDE migration: the §5 Class 000 WIDE layout has INDEPENDENT
// rs@bits[19:16] + rt@bits[15:12] fields. TableGen size-6 shell; product encode is Format E member. The former
// `Constraints = "$rt = $rs"` tie was WRONG (forced rs==rt). Removing it lets
// `addi32_w rt, rs, imm` encode any rt!=rs pair directly (no TwoAddress copy).
// WIDE operands use the dedicated *_wide_abs / *_dr classes (carrying
// EncoderMethod/DecoderMethod) so a tablegen-driven getBinaryCodeForInstr
// reproduces the hand-rolled encode48Bit* bytes AND fixups byte-for-byte.
// legacy 48-bit WIDE def retirement. subsequent the
// encoder/decoder are legacy; the Fmt48_* defs that have ZERO live
// C++ consumers (no hand-written `Haydn::<def>` enum ref in any.cpp/.h, no
// td cross-ref) are DEAD and removed here. Defs KEPT below (ADDI32_W
// ORI32_W) still have live C++ consumers: ADDI32_W is selected by
// HaydnAsmPrinter (block-address LO20 materialization, 53 sites) and
// HaydnMatInt; ORI32_W by HaydnMatInt (uimm20 strategy-2 materialization).
// The other 6 Class-000 GPRImm variants (ADDI32S_W, SUBI32_W, SUBI32S_W
// ANDI32_W, XORI32_W, LUI_W) had zero C++ refs and are removed.
//
// ADDI32_W/ORI32_W are NOT re-based onto an s0
// format class. Their s0 projections already exist as the
// DISTINCT-codepoint isCodeGenOnly=1 defs ADDI32_W_S0/ORI32_W_S0
// (HaydnFormatsALU32.td:1185-1196, derived from HaydnFU_ALU32_S0_RI20).
// Re-basing the legacy defs onto the SAME format would duplicate those defs
// and break the logical→member alt pairing (`ADDI32_W` ->
// `ADDI32_W_S0`). The legacy def's role is to carry the (rd, rs, imm20)
// operand shape and be committed to its _S0 member by post-RA
// materializeMultiOpcodeInstrs setDesc. The Fmt48_WideGPRImm base class is
// therefore RETAINED (it has live derived defs). Fmt48_* class retirement is
// blocked until all 21 live Fmt48_* defs migrate to s0/s1/s2
// projections (see HaydnInstrFormatsC.td note).
def ADDI32_W  : Fmt48_WideGPRImm<0x01, (outs GPR32:$rt),
                                 (ins GPR32:$rs, simm20_wide_abs:$imm),
                                 "addi32_w\t$rt, $rs, $imm", []>;
def ORI32_W   : Fmt48_WideGPRImm<0x06, (outs GPR32:$rt),
                                 (ins GPR32:$rs, uimm20_wide_abs:$imm),
                                 "ori32_w\t$rt, $rs, $imm", []>;

//===----------------------------------------------------------------------===//
// Class 001 — Branch (§5.5). Offsets in 2-byte units (§5.14, D1).
//===----------------------------------------------------------------------===//

// Calls (Phase 1a): JAL_W/JALR_W are the canonical call forms.
// They mirror legacy JAL/JALR — isCall + caller-saved clobber Defs (same set
// as the legacy block, including R12 as normal call-clobbered GPR). JAL_W is NOT
// isTerminator (a call returns); JALR_W is isTerminator (RET/indirect-branch
// use) + isCall + isIndirectBranch. The prior `isBranch=isTerminator=
// hasSideEffects=1` wrapping was wrong: it made JAL_W a terminator so
// ADJCALLSTACKUP landed after a terminator (verifier error) AND omitted the
// caller-saved Defs (lost live-across-call tracking).
// Note: R15 (LR) is the explicit $rt output of JAL_W/JALR_W, not listed here.
// R14 is a GPR; clobber/preserve via regmask only (not Defs).
let Defs = [R1, R2, R3, R4, R5, R6, R7, R12,
            D0, D1, D2, D3, D4, D5, D6, D7] in {
let isCall = 1 in
def JAL_W : Fmt48_WideBR_I20<0x20, (outs GPR32:$rt),
                             (ins brtarget_wide_i20:$offset),
                             "jal_w\t$rt, $offset", []>;

// CB-129: isBarrier required so MachineBlockPlacement does not treat an
// unanalyzable indirect jump/RET as fallthrough (assert in buildCFGChains
// on computed-goto). PseudoCALLIndirect expands to JALR_W only at print time
// (post-MBP); pure jumps/RETs are MIR terminators that never fall through.
let isTerminator = 1, isBarrier = 1, isCall = 1, isIndirectBranch = 1 in
def JALR_W : Fmt48_WideBR_RI12<0x30, (outs GPR32:$rt),
                               (ins GPR32:$rs, calltarget_wide_ri12:$offset),
                               "jalr_w\t$rt, $rs, $offset", []>;
} // Defs = caller-saved clobbers for calls

// Conditional branches (§5.5). isBranch + isTerminator only — branches have no
// side effects (the prior hasSideEffects=1/Defs=[SFR] was a CSR-hazard leftover
// that does not apply to branches).
let isBranch = 1, isTerminator = 1 in {
def BEQ_W  : Fmt48_WideBR_RI12<0x24, (outs),
                               (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$offset),
                               "beq_w\t$rs, $rt, $offset", []>;
def BNE_W  : Fmt48_WideBR_RI12<0x25, (outs),
                               (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$offset),
                               "bne_w\t$rs, $rt, $offset", []>;
def BGE_W  : Fmt48_WideBR_RI12<0x26, (outs),
                               (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$offset),
                               "bge_w\t$rs, $rt, $offset", []>;
def BGEU_W : Fmt48_WideBR_RI12<0x27, (outs),
                               (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$offset),
                               "bgeu_w\t$rs, $rt, $offset", []>;
def BLT_W  : Fmt48_WideBR_RI12<0x28, (outs),
                               (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$offset),
                               "blt_w\t$rs, $rt, $offset", []>;
def BLTU_W : Fmt48_WideBR_RI12<0x29, (outs),
                               (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$offset),
                               "bltu_w\t$rs, $rt, $offset", []>;

def BEQZ_W  : Fmt48_WideBR_I12<0x2C, (outs), (ins GPR32:$rs, brtarget_wide_i12:$offset),
                               "beqz_w\t$rs, $offset", []>;
def BNEZ_W  : Fmt48_WideBR_I12<0x2D, (outs), (ins GPR32:$rs, brtarget_wide_i12:$offset),
                               "bnez_w\t$rs, $offset", []>;
def BGEZ_W  : Fmt48_WideBR_I12<0x2E, (outs), (ins GPR32:$rs, brtarget_wide_i12:$offset),
                               "bgez_w\t$rs, $offset", []>;
def BLTZ_W  : Fmt48_WideBR_I12<0x2F, (outs), (ins GPR32:$rs, brtarget_wide_i12:$offset),
                               "bltz_w\t$rs, $offset", []>;
} // isBranch, isTerminator

//===----------------------------------------------------------------------===//
// Class 010 — DR Immediate (§5.6). DR64 read-modify-write.
//
// the 7 Fmt48_WideDR_RI20 defs (ADDI64/SUBI64/ANDI64
// ORI64/XORI64/SLTI64/SLTUI64) had ZERO live C++ consumers and are removed
// (along with the Fmt48_WideDR_RI20 class). The 3 Fmt48_WideDR_RI6 shift
// defs (SLLI64/SRLI64/SRAI64) are KEPT as live logical opcodes.
//===----------------------------------------------------------------------===//

// AsmString is the DB 3-operand form (rtd, rsd, imm6) even though
// Constraints force rtd==rsd. Printing only "$rtd, $imm" made clang -S
// emit `slli64 d0, 32` while llvm-objdump (flex S1/S2 decode) prints
// `slli64 d0, d0, 32`. BundleSim's.s path parses the DB syntax as
// CC_D_DI (dr[o0]=f(dr[o1], imm[o2])); a 2-token form mis-binds imm as
// rsd → wrong schoolbook half extract → cb50.s exit 96 vs host/ELF 21.
// One mechanism: always print the 3-operand DB shape (matches ISS + objdump).
// InstAlias keeps hand-written 2-op assembly working (in-place form).
let Constraints = "$rtd = $rsd" in {
def SLLI64  : Fmt48_WideDR_RI6<0x50, (outs DR64:$rtd),
                               (ins DR64:$rsd, uimm6_dr:$imm),
                               "slli64\t$rtd, $rsd, $imm", []>;
def SRLI64  : Fmt48_WideDR_RI6<0x51, (outs DR64:$rtd),
                               (ins DR64:$rsd, uimm6_dr:$imm),
                               "srli64\t$rtd, $rsd, $imm", []>;
def SRAI64  : Fmt48_WideDR_RI6<0x52, (outs DR64:$rtd),
                               (ins DR64:$rsd, uimm6_dr:$imm),
                               "srai64\t$rtd, $rsd, $imm", []>;
}
// No InstAlias for the old 2-op print form: uimm6_dr is not an Operand
// subclass usable in InstAlias result args. Hand asm can write the DB
// 3-op form; CodeGen always prints 3-op after the AsmString fix above.

//===----------------------------------------------------------------------===//
// Class 011 — DR Extended (§5.7). DR64 min/max with full 32-bit immediate.
//
// the 2 Fmt48_WideDRX defs (MAXI64/MINI64) had ZERO live
// C++ consumers and are removed (along with the Fmt48_WideDRX class).
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Class 100 — Control (§5.8-5.13). CSR + ZERO_GPR + SET_HWLOOP forms.
//===----------------------------------------------------------------------===//

// fix: model the CSR/HWLR side effect via an explicit SFR implicit def
// instead of `hasSideEffects = 1`. The ScheduleDAG builder serializes every
// `hasUnmodeledSideEffects` (i.e. `hasSideEffects=1`) instruction behind an
// Ord Barrier edge, which shadowed the CSRW<->SET_HWLOOP hazard gate in
// HaydnHazardRecognizer — the gate never load-bearingly fired because the DAG
// had already forced separate cycles. Setting `hasSideEffects = 0` removes the
// MCID::UnmodeledSideEffects flag (so the DAG stops adding barrier edges) and
// `Defs = [SFR]` keeps the side effect modeled on the SFR/CSR space, which
// HaydnHazardRecognizer already treats as slot-ordered-safe (the WAW guard
// excludes SFR). The gate is then the SOLE separator and the control
// case (CSRW@0x10 + SET_HWLOOP) bundles. CSRR_W reads CSR state: model via
// Uses = [SFR].
// CSRR_W (zero live C++ consumers) and ZERO_GPR_W (zero
// live C++ consumers) removed; their Fmt48_WideCSR/Fmt48_WideZeroGPR classes
// are retained (CSRW_W still uses Fmt48_WideCSR — HaydnAsmPrinter +
// HaydnHazardRecognizer). CSRR_W's `Uses = [SFR]` comment is preserved for
// historical context.
let hasSideEffects = 0, Defs = [SFR] in
def CSRW_W : Fmt48_WideCSR<0x81, (outs),
                           (ins uimm8_csr:$uimm8, GPR32:$rt),
                           "csrw_w\t$uimm8, $rt", []>;

let hasSideEffects = 0, isNotDuplicable = 1, Defs = [SFR] in {
def SET_HWLOOP_W : Fmt48_WideSET_HWLOOP<0x88, (outs),
    (ins uimm1:$sel, hwloop_off1:$offset1, hwloop_off2:$offset2,
         uimm16_hwloop_cnt:$cnt),
    "set_hwloop_w\t$sel, $offset1, $offset2, $cnt", []>;
def SET_HWLOOP_F2_W : Fmt48_WideSET_HWLOOP_F2<0x89, (outs),
    (ins uimm1:$sel, hwloop_off1:$offset1, hwloop_off2:$offset2, GPR32:$rs),
    "set_hwloop_f2_w\t$sel, $offset1, $offset2, $rs", []>;
def SET_HWLOOP_REG_W : Fmt48_WideSET_HWLOOP_REG<0x8A, (outs),
    (ins uimm1:$sel, GPR32:$rs1, GPR32:$rs2, GPR32:$rs3),
    "set_hwloop_reg_w\t$sel, $rs1, $rs2, $rs3", []>;
}

//===----------------------------------------------------------------------===//
// Class 101 — WIDE LS (§5.16). RETIRED by.
//
// The 14 Fmt48_WideLS _WL defs (LD/ST 32/64 + ST16/ST8, incl. _POST/_PRE)
// that lived here had ZERO live C++ consumers subsequent (the encoder's
// emitWideLSParcel and the decoder's tryDecode48Bit were gutted by;
// no Haydn::<def> enum ref survived in any.cpp/.h). Removed along with the
// Fmt48_WideLS class (HaydnInstrFormatsC.td). Wide-offset LS is now reached
// via the _S0 family-2 defs (HaydnFormatsLS.td).
//===----------------------------------------------------------------------===//
