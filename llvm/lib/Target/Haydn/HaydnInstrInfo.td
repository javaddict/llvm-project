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

// Compiler-only pseudos live in HaydnPseudos.td (included from
// HaydnGeneric.td before this file). This file keeps logicals,
// Patterns, and matcher-visible shells.

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
// Matcher logicals are generated (HaydnInstrInfoGolden.td.inc). Product
// encode is Format E members, not hypothesized Inst bits. GISel: G_MUL s32
// → MULL; G_SMULH/UMULH s32 → MULSSH/MULUUH
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
// Catalog logical `SEXT32T64` is generated (HaydnAutoNoDecode, no Inst
// bits). This def is the GISel-selected encode form (DR64 out, GPR32 in).
// Both print `sext32t64`; members own the parcel, this opcode owns select.
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

} // Defs = caller-saved clobbers for call instructions

// Long-branch expansion is CodeGen BranchRelaxation only (insertBranch /
// insertIndirectBranch / isBranchOffsetInRange). MC has no PseudoLongB* and
// no expandLongBranch: AsmBackend mayNeedRelaxation is always false. Do not
// reintroduce an MC-layer second long-branch mechanism (F17).

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
// 2026-08-21 latency P0/P1: golden CSRR Available = ALU0|ALU1|ALU2 AND
// Pipeline_Info Data_Latency = 2 — Slot012_ALU_CsrLat carries both (the
// FmtCSR class default was also re-mapped to Slot012_ALU).
let hasSideEffects = 1 in
def CSRR : FmtCSR<0x39, (outs GPR32:$rd), (ins uimm8:$csr_addr, GPR32:$rs),
               "csrr\t$rd, $csr_addr", []> {
  let Itinerary = Slot012_ALU_CsrLat;
  let TSFlags{2-0} = 0b001;
  // $rs is not a real input for CSRR (DB reads only the CSR); it exists for
  // MC operand-count parity with the shared FmtCSR decoder (case 25 decodes
  // rd + csr_addr + rs for BOTH CSRR and CSRW). Regalloc ignores an unused
  // tied input operand; no correctness impact.
}

// CSRW — catalog 2-op (uimm8, rs). Matcher-visible isPseudo shell; product
// encode is Format E I8 members. GISel CBR setup emits CSRW_W, which
// Finalize cutovers to the same members (typed CSR I8 fixup, never an
// untyped NONE FieldSlot). hasSideEffects=1 so a dead CSRW is not DCE'd
// (WUR_AE_CBEGIN0/CEND0). The old 3-op FmtCSR dead-$rd shape was
// decoder-parity only and is gone.
let hasSideEffects = 1, isPseudo = 1, isCodeGenOnly = 0, Defs = [SFR],
    // 2026-08-21 itinerary re-map: golden CSRW Available = ALU0|ALU1|ALU2.
    Itinerary = Slot012_ALU in
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
  // 2026-08-21 itinerary re-map (wrong-family fix): golden X2MUL32
  // Available = MAC0|MAC1 (was Slot1_ALU — the DB "slot 1 only" note is
  // entry geometry, not a unit menu; unit-injectivity booking must reserve
  // the MAC family).
  let Itinerary = Slot12_MAC;
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
  // 2026-08-21 itinerary re-map (wrong-family fix): golden X4MUL16
  // Available = MAC0|MAC1 — books MAC units, not ALU1|ALU2.
  let Itinerary = Slot12_MAC;
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

// Scalar 64-bit SFR compare — TWO-SOURCE pair shape.
// 2026-08-21 (gaps/audit_shapes.md "Scalar trio", 4-layer rework): golden
// Syntax "`SLT64 rsd1, rsd2`", Behavior SFR = (rsd1 < rsd2) ? 4'b1111
// : 4'b0000, DR ports rsd1+rsd2 read / write = SFR only. The prior unary
// (outs DR64:$rd), (ins DR64:$rs1) shape was wrong at this layer too —
// the intrinsic returns rs1 via COPY passthrough (X2SLT32 contract).
//
// Explicit SFR Defs/Uses keep cmp→cmov RAW correct without memory barriers
// (hasSideEffects=1 would serialize loads/stores as Ord Barrier).
// Matcher-visible (isCodeGenOnly=0): public `seq64`/`slt64`/`sle64` must
// not depend on residual `_S*` FieldSlots. isPseudo: non-encoding shell.
// Itinerary: golden Available = ALU0|ALU1|ALU2.
let hasSideEffects = 0, Defs = [SFR], isCodeGenOnly = 0, isPseudo = 1,
    Itinerary = Slot012_ALU in {
def SEQ64 : HaydnInst<0, (outs), (ins DR64:$rs1, DR64:$rs2),
                      "seq64\t$rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def SLT64 : HaydnInst<0, (outs), (ins DR64:$rs1, DR64:$rs2),
                      "slt64\t$rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def SLE64 : HaydnInst<0, (outs), (ins DR64:$rs1, DR64:$rs2),
                      "sle64\t$rs1, $rs2", []> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
}  // hasSideEffects = 0, Defs = [SFR]

// Scalar 64-bit SFR conditional move (unary DR64 → reads SFR, sets dst).
// 2026-08-21 itinerary re-map: golden Available = ALU0|ALU1|ALU2.
let hasSideEffects = 0, Uses = [SFR], isCodeGenOnly = 0, isPseudo = 1,
    Itinerary = Slot012_ALU in {
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
// 2026-08-21 itinerary re-map: golden Available for this whole shell block
// is ALU0|ALU1|ALU2 (was Slot12_ALU) — except the four DSP-unary
// LOG2/EXP2/RECIP/SQRT below, which golden pins to ALU1|ALU2 with fixed
// Data_Latency=2 (Slot12_ALU_DspLat, audit_itinerary.md + latency P0).
let isCodeGenOnly = 0, isPseudo = 1, Itinerary = Slot012_ALU in {
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
// DSP unary LUT family — golden Available = ALU1|ALU2 AND fixed
// Pipeline_Info Data_Latency = 2 (2026-08-21 latency P0): dest
// OperandCycle 2, not the block's latency-1 default.
def LOG2 : HaydnInst<0, (outs GPR32:$rd), (ins GPR32:$rs1),
                     "log2\t$rd, $rs1", []> {
  let Itinerary = Slot12_ALU_DspLat;
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def EXP2 : HaydnInst<0, (outs GPR32:$rd), (ins GPR32:$rs1),
                     "exp2\t$rd, $rs1", []> {
  let Itinerary = Slot12_ALU_DspLat;
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def RECIP : HaydnInst<0, (outs GPR32:$rd), (ins GPR32:$rs1),
                      "recip\t$rd, $rs1", []> {
  let Itinerary = Slot12_ALU_DspLat;
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
def SQRT : HaydnInst<0, (outs GPR32:$rd), (ins GPR32:$rs1),
                     "sqrt\t$rd, $rs1", []> {
  let Itinerary = Slot12_ALU_DspLat;
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
}
} // isPseudo / Slot012_ALU logical unary shells (matcher-visible)

// X2 SIMD compare → SFR only. Golden/ISA: "X2SEQ32 rsd1, rsd2" (2 ops).
// No DR dest write; selector COPY-passthroughs rs1 for the C return value.
// 2026-08-21 itinerary re-map: golden Available = ALU0|ALU1|ALU2 for the
// X2/X4 compare + cmov shells below (previously NoItinerary — no unit
// reservation at all).
let Defs = [SFR], Itinerary = Slot012_ALU in {
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
let Uses = [SFR], Itinerary = Slot012_ALU in {
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
let Defs = [SFR], Itinerary = Slot012_ALU in {
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
let Uses = [SFR], Itinerary = Slot012_ALU in {
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
  // 2026-08-21 itinerary re-map: golden Available = ALU0|ALU1|ALU2.
  let Itinerary = Slot012_ALU;
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
  // 2026-08-21 itinerary re-map: golden Available = ALU0|ALU1|ALU2.
  let Itinerary = Slot012_ALU;
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
  // 2026-08-21 itinerary re-map: golden Available = ALU0|ALU1|ALU2.
  let Itinerary = Slot012_ALU;
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
  // 2026-08-21 itinerary re-map: golden MOVE32_DR_L Available =
  // ALU0|ALU1|ALU2 (was Slot0_ALU).
  let Itinerary = Slot012_ALU;
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
  // 2026-08-21 itinerary re-map: golden MOVE32_DR_H Available =
  // ALU0|ALU1|ALU2 (was Slot0_ALU).
  let Itinerary = Slot012_ALU;
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
// 32/64-bit multiply and MULA64_* matcher logicals are generated
// (HaydnInstrInfoGolden.td.inc). Dual-dest MAC ties (X2MULA32 and siblings)
// stay hand-owned below without Inst bits. X2MULAPL32 is generated.

// no durable MIR opcodes LD32_S1/LD64_S1. Logical loads are LD32/LD64
// only. (The former S1 encode-only peers and their HaydnFormatsLD.td home
// were deleted 2026-08-15 — dead surface, zero instantiations.)

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
// Catalog names S_SW_POST_IMM / D_SDW_POST_IMM are generated logicals
// (HaydnInstrInfoGolden.td.inc). ST32_POST / ST64_POST are the
// codegen-selected forms.
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
// > 1). Matcher-visible HaydnInst shells (no hypothesized Inst bits);
// product encode is Format E members (same shape as generated D_SDW_WITH_IMM).
// D_SW_L_WITH_IMM: mem32[rs + (imm6<<2)] = rtd[31:00] (low lane)
// D_SW_H_WITH_IMM: mem32[rs + (imm6<<2)] = rtd[63:32] (high lane)
//===----------------------------------------------------------------------===//

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode",
    isAsmParserOnly = 0, mayStore = 1 in {
def D_SW_L_WITH_IMM : HaydnInst<4, (outs),
    (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_sw_l_with_imm\t$rtd, $rs, $scaled_imm", []> {
  let Itinerary = Slot0_LS;
}
def D_SW_H_WITH_IMM : HaydnInst<4, (outs),
    (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_sw_h_with_imm\t$rtd, $rs, $scaled_imm", []> {
  let Itinerary = Slot0_LS;
}
}


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
// MOVEI_H / MOVEI_L are hand-owned logicals below (no Inst bits). Fmt48_MOVEI is retired.
// The 20 KEPT Fmt48 defs below (ADDI32_W, ORI32_W, JAL_W, JALR_W, BEQ/BNE/BGE
// BGEU/BLT/BLTU_W, BEQZ/BNEZ/BGEZ/BLTZ_W, SET_HWLOOP_W
// F2_W/REG_W, SLLI64/SRLI64/SRAI64) retain live hand-written C++ consumers
// (Haydn:: enum refs in AsmPrinter, InstrInfo, HazardRecognizer,
// HardwareLoops, ExpandPseudos, MatInt).
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
// live C++ consumers) removed; Fmt48_WideCSR/Fmt48_WideZeroGPR are retired.
// CSRR_W's `Uses = [SFR]` comment is preserved for historical context.
// CSRW_W — GISel CBR / ExpandPseudos SETCBR form. Operand shape matches
// catalog CSRW I8 members (uimm8, rs). Not a 48-bit FieldSlot encoding:
// HaydnInst<0> logical shell like catalog CSRW (isPseudo so MC does not
// emit a default-zero Inst). AltOccupancy mask 0x1 still places it.
// Immediate and reloc addresses cutover to I8 members (2-op identity/swap
// keep-map). Encode binds FIXUP_HAYDN_CSR_UImm8 / R_HAYDN_CSR_UImm8, never
// untyped NONE. Peer: AIE MultiSlot_Pseudo isPseudo=1 (AIE2InstrFormats.td:23-31).
let hasSideEffects = 0, Defs = [SFR], isPseudo = 1, isCodeGenOnly = 0,
    // 2026-08-21 itinerary re-map: golden CSRW Available = ALU0|ALU1|ALU2
    // (CSRW_W cutovers to the same I8 members; dest is SFR-domain so no
    // CsrLat dest window applies — documented P2 residual).
    Itinerary = Slot012_ALU in
def CSRW_W : HaydnInst<0, (outs),
                       (ins uimm8_csr:$uimm8, GPR32:$rt),
                       "csrw_w\t$uimm8, $rt", []> {
  let TSFlags{2-0} = 0b001;
}

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

//===----------------------------------------------------------------------===//
// AR / unaligned UA logicals (moved from HaydnFormatsLS.td).
// Product encode is Format E members. No FieldSlot occupancy defs.
// D_LQHWUA_POST carries Slot2_LS so the published memory-cycle itinerary
// stays live after the last `_S*` FieldSlot was deleted.
//===----------------------------------------------------------------------===//

let mayLoad = 1, hasSideEffects = 1 in
def PLDWWUA : HaydnInst<4, (outs), (ins uimm2:$ar_sel, GPR32:$rs),
                        "pldwwua\t$ar_sel, $rs", []> {
  let Itinerary = Slot012_ALU;
}

let mayLoad = 1, hasSideEffects = 1 in
def D_LQHWUA_POST : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs1_wb),
    (ins GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel),
    "d_lqhwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
  // 2026-08-21 itinerary re-map (wrong-family fix): golden Available =
  // LOAD1|LOADSTORE0 (was Slot2_LS = ALU2 seat). Slot01_LD is a published
  // memory itinerary, so the published Data_Latency=2 memory row is kept.
  let Itinerary = Slot01_LD;
}

let mayLoad = 1, hasSideEffects = 1 in
def D_LTWUA_POST : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs1_wb),
    (ins GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel),
    "d_ltwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
  // 2026-08-21 itinerary re-map (wrong-family fix): golden Available =
  // LOAD1|LOADSTORE0 (was Slot012_ALU, which falsely contended all ALUs).
  let Itinerary = Slot01_LD;
}

let hasSideEffects = 1 in
def FLAR : HaydnInst<4, (outs), (ins uimm2:$ar_sel), "flar\t$ar_sel", []> {
  // 2026-08-21 itinerary re-map (wrong-family fix): golden Available =
  // LOAD1|LOADSTORE0 (was Slot012_ALU).
  let Itinerary = Slot01_LD;
}

let mayStore = 1, mayLoad = 0, hasSideEffects = 1 in
def WBARWUA : HaydnInst<4, (outs), (ins GPR32:$rs, uimm2:$ar_sel, uimm1:$dir_sel),
                        "wbarwua\t$ar_sel, $rs, $dir_sel", []> {
  // 2026-08-21 itinerary re-map (wrong-family fix): golden Available =
  // LOADSTORE0 (was Slot012_ALU).
  let Itinerary = Slot0_LS;
}

let mayStore = 1, mayLoad = 0, hasSideEffects = 1 in
def D_SQHWUA_POST : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel),
    "d_sqhwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
  // 2026-08-21 itinerary re-map (wrong-family fix): golden Available =
  // LOADSTORE0 (was Slot012_ALU).
  let Itinerary = Slot0_LS;
}

let mayStore = 1, mayLoad = 0, hasSideEffects = 1 in
def D_STWUA_POST : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel),
    "d_stwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
  // 2026-08-21 itinerary re-map (wrong-family fix): golden Available =
  // LOADSTORE0 (was Slot012_ALU).
  let Itinerary = Slot0_LS;
}

//===----------------------------------------------------------------------===//
// Hand-owned logicals (formerly HaydnInstrInfoManual.td). No Inst bits —
// product encode is Format E members. Dual-dest MAC ties stay authored
// (overlay); LS WITH lane stores are HaydnInst shells above.
//===----------------------------------------------------------------------===//

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in {
let Constraints = "$rd = $ra, $rtd2 = $ra2" in {
def X2MULA32 : HaydnInst<4, (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x2mula32\t$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}
def X2MULS32 : HaydnInst<4, (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x2muls32\t$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}
def X4FF2MULA16S : HaydnInst<4, (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4ff2mula16s\t$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}
def X4FF2MULS16S : HaydnInst<4, (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4ff2muls16s\t$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}
def X4MULA16 : HaydnInst<4, (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4mula16\t$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}
def X4MULA16S : HaydnInst<4, (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4mula16s\t$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}
def X4MULS16 : HaydnInst<4, (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4muls16\t$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}
def X4MULS16S : HaydnInst<4, (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4muls16s\t$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}
} // Constraints dual-acc

def X2CMUL32 : HaydnInst<4, (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x2cmul32\t$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}
def X2CMUL32S : HaydnInst<4, (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x2cmul32s\t$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}
def X2CMUL32S_F2 : HaydnInst<4, (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x2cmul32s.f2\t$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}
def X2CMUL32_F2 : HaydnInst<4, (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x2cmul32.f2\t$rd, $rtd2, $rs1, $rs2", []> {
  // 2026-08-21 itinerary re-map (wrong-family fix): golden X2CMUL32_F2
  // Available = MAC0|MAC1 (was Slot1_ALU). Non-F2 X2CMUL32 siblings
  // already book Slot12_MAC.
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}
def X4FF2MUL16S : HaydnInst<4, (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x4ff2mul16s\t$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}

// 2026-08-21 itinerary re-map: golden MOVEI_H/MOVEI_L/ZERO_DR Available =
// ALU0|ALU1|ALU2 (previously NoItinerary — no unit reservation).
let TSFlags = 14, Itinerary = Slot012_ALU in {
def MOVEI_H : HaydnInst<4, (outs DR64:$rd),
    (ins simm32_movei:$imm32), "movei_h\t$rd, $imm32", []>;
def MOVEI_L : HaydnInst<4, (outs DR64:$rd),
    (ins simm32_movei:$imm32), "movei_l\t$rd, $imm32", []>;
}

let isPseudo = 0, TSFlags = 14, Itinerary = Slot012_ALU in
def ZERO_DR : HaydnInst<4, (outs DR64:$rd), (ins), "zero_dr\t$rd", []>;

let TSFlags = 14, Itinerary = Slot12_ALU_SinCosLat in {
def SIN_COS : HaydnInst<4, (outs DR64:$rd),
    (ins GPR32:$rs, uimm4:$imm), "sin_cos\t$rd, $rs, $imm", []>;
def ARCTAN : HaydnInst<4, (outs GPR32:$rd),
    (ins DR64:$rs, uimm4:$imm), "arctan\t$rd, $rs, $imm", []>;
}
} // isCodeGenOnly=0 HaydnAutoNoDecode

// Hand Slot01_LD exception for CB-load ResMII (see GOALS M18). No Inst bits.
let isAsmParserOnly = 1, isPseudo = 0, mayLoad = 1,
    isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def D_LDW_CB_IMM : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs_wb),
    (ins GPR32:$rs, i32imm:$cbr_sel, simm8:$imm),
    "d_ldw_cb_imm\t$cbr_sel, $rtd, $rs, $imm", []> {
  let Constraints = "$rs = $rs_wb";
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  // 2026-08-21 itinerary re-map (over-broad fix): golden D_LDW_CB_IMM
  // Available = LOADSTORE0 only (was Slot01_LD). The _REG sibling and all
  // other CB rows are LOADSTORE0-only in golden too.
  let Itinerary = Slot0_LS;
}
