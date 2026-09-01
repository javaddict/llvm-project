//===-- HaydnGISel.td - Haydn GlobalISel Patterns ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Style guide:
//   - Logical opcodes only: ADD32, X2ADD32, MOVT32, ABS64, … — never _S0/_S1/_S2
//     encode peers. Slot members come from post-RA tryAdd / AltDesc /
//     leaveRegion setDesc(member); encode prints Desc as-is.
//   - selectImpl (match table from these Pats) runs first; C++ residual is the
//     allowlist for multi-instr / MMO / intrinsic forms only.
//   - No isCodeGenOnly encode-only defs; no Pats that reject GISel.
//
// Incremental GlobalISel Pats. C++ residual covers multi-instr lowers:
// s64 icmp/select, SIMD shifts (lane-0 extract), loads/stores,
// G_HAYDN_MUL64_WIDEN{,U}, and other non-1:1 forms.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Scalar binary — s32 (GPR32)
//===----------------------------------------------------------------------===//

def : Pat<(i32 (add GPR32:$rs1, GPR32:$rs2)), (ADD32 GPR32:$rs1, GPR32:$rs2)>;
def : Pat<(i32 (sub GPR32:$rs1, GPR32:$rs2)), (SUB32 GPR32:$rs1, GPR32:$rs2)>;
def : Pat<(i32 (and GPR32:$rs1, GPR32:$rs2)), (AND32 GPR32:$rs1, GPR32:$rs2)>;
def : Pat<(i32 (or  GPR32:$rs1, GPR32:$rs2)), (OR32  GPR32:$rs1, GPR32:$rs2)>;
def : Pat<(i32 (xor GPR32:$rs1, GPR32:$rs2)), (XOR32 GPR32:$rs1, GPR32:$rs2)>;
// Prefer NOT32 for IR "not" (PatFrag = xor x, -1). Unary slot members are
// NOT32_S* (HaydnFormatsALU32, opc 0b0010000 / #16).
def : Pat<(i32 (not GPR32:$rs)), (NOT32 GPR32:$rs)>;

//===----------------------------------------------------------------------===//
// Scalar binary with immediate — s32. Fold G_CONSTANT into RI20/RI5 forms
// instead of materializing then using RR. Immediate class is NOT uniform:
//   ADDI32 / SUBI32           simm20 (product RI20; max 524287)
//   ANDI32 / ORI32 / XORI32   uimm20 (ISA zero-extends — no negative imm)
//   SLLI32 / SRLI32 / SRAI32  uimm5
// `not` (xor x, -1) is unaffected: -1 fails uimm20, so NOT32 still wins.
//
// GISel Pat-first: legalizer rewrites sN→s32 zext/sext to G_AND /
// G_SHL+G_LSHR / G_SHL+G_ASHR so these Pats cover casts too. trunc→s1
// stays legal (selector residual ANDI32 bit0); cross-bank s32↔s64 /
// anyext remain C++ residual. RI densify can enable multi-stage SMS;
// product multi-stage is OK after BUNDLE-header resync (tied MULL COPY).
//===----------------------------------------------------------------------===//

def : Pat<(i32 (add GPR32:$rs, simm20:$imm)), (ADDI32 GPR32:$rs, simm20:$imm)>;
def : Pat<(i32 (sub GPR32:$rs, simm20:$imm)), (SUBI32 GPR32:$rs, simm20:$imm)>;
def : Pat<(i32 (and GPR32:$rs, uimm20:$imm)), (ANDI32 GPR32:$rs, uimm20:$imm)>;
def : Pat<(i32 (or  GPR32:$rs, uimm20:$imm)), (ORI32  GPR32:$rs, uimm20:$imm)>;
def : Pat<(i32 (xor GPR32:$rs, uimm20:$imm)), (XORI32 GPR32:$rs, uimm20:$imm)>;

//===----------------------------------------------------------------------===//
// Scalar binary — s64 (DR64)
//===----------------------------------------------------------------------===//

def : Pat<(i64 (add DR64:$rs1, DR64:$rs2)), (ADD64 DR64:$rs1, DR64:$rs2)>;
def : Pat<(i64 (sub DR64:$rs1, DR64:$rs2)), (SUB64 DR64:$rs1, DR64:$rs2)>;
def : Pat<(i64 (and DR64:$rs1, DR64:$rs2)), (AND64 DR64:$rs1, DR64:$rs2)>;
def : Pat<(i64 (or  DR64:$rs1, DR64:$rs2)), (OR64  DR64:$rs1, DR64:$rs2)>;
// Prefer NOT64 for IR "not"; keep xor -1 as XOR64 until BundleSim ISS
// golden for NOT64 (yarpgen sensitive). Direct not → NOT64 is still covered.
def : Pat<(i64 (not DR64:$rs)), (NOT64 DR64:$rs)>;
def : Pat<(i64 (xor DR64:$rs1, DR64:$rs2)), (XOR64 DR64:$rs1, DR64:$rs2)>;

//===----------------------------------------------------------------------===//
// SIMD binary — v2i32 / v4i16 in DR64
//===----------------------------------------------------------------------===//

def : Pat<(v2i32 (add DR64:$rs1, DR64:$rs2)), (X2ADD32 DR64:$rs1, DR64:$rs2)>;
def : Pat<(v2i32 (sub DR64:$rs1, DR64:$rs2)), (X2SUB32 DR64:$rs1, DR64:$rs2)>;
def : Pat<(v4i16 (add DR64:$rs1, DR64:$rs2)), (X4ADD16 DR64:$rs1, DR64:$rs2)>;
def : Pat<(v4i16 (sub DR64:$rs1, DR64:$rs2)), (X4SUB16 DR64:$rs1, DR64:$rs2)>;

def : Pat<(v2i32 (and DR64:$rs1, DR64:$rs2)), (AND64 DR64:$rs1, DR64:$rs2)>;
def : Pat<(v2i32 (or  DR64:$rs1, DR64:$rs2)), (OR64  DR64:$rs1, DR64:$rs2)>;
def : Pat<(v2i32 (xor DR64:$rs1, DR64:$rs2)), (XOR64 DR64:$rs1, DR64:$rs2)>;
def : Pat<(v4i16 (and DR64:$rs1, DR64:$rs2)), (AND64 DR64:$rs1, DR64:$rs2)>;
def : Pat<(v4i16 (or  DR64:$rs1, DR64:$rs2)), (OR64  DR64:$rs1, DR64:$rs2)>;
def : Pat<(v4i16 (xor DR64:$rs1, DR64:$rs2)), (XOR64 DR64:$rs1, DR64:$rs2)>;

//===----------------------------------------------------------------------===//
// Scalar min/max / shifts — s32
//===----------------------------------------------------------------------===//

def : Pat<(i32 (smax GPR32:$rs1, GPR32:$rs2)), (MAX32  GPR32:$rs1, GPR32:$rs2)>;
def : Pat<(i32 (smin GPR32:$rs1, GPR32:$rs2)), (MIN32  GPR32:$rs1, GPR32:$rs2)>;
def : Pat<(i32 (umax GPR32:$rs1, GPR32:$rs2)), (MAXU32 GPR32:$rs1, GPR32:$rs2)>;
def : Pat<(i32 (umin GPR32:$rs1, GPR32:$rs2)), (MINU32 GPR32:$rs1, GPR32:$rs2)>;

// Signed s64 min/max — MAX64/MIN64. No MAXU64/MINU64; umin/umax s64
// stay legalizer-lowered. Logical opcodes only (alts + post-RA setDesc).
def : Pat<(i64 (smax DR64:$rs1, DR64:$rs2)), (MAX64 DR64:$rs1, DR64:$rs2)>;
def : Pat<(i64 (smin DR64:$rs1, DR64:$rs2)), (MIN64 DR64:$rs1, DR64:$rs2)>;

def : Pat<(i32 (shl GPR32:$rs1, GPR32:$rs2)), (SLL32 GPR32:$rs1, GPR32:$rs2)>;
def : Pat<(i32 (srl GPR32:$rs1, GPR32:$rs2)), (SRL32 GPR32:$rs1, GPR32:$rs2)>;
def : Pat<(i32 (sra GPR32:$rs1, GPR32:$rs2)), (SRA32 GPR32:$rs1, GPR32:$rs2)>;

// Shift-by-constant → RI5 (uimm5). Out-of-range amounts stay on RR above.
def : Pat<(i32 (shl GPR32:$rs, uimm5:$imm)), (SLLI32 GPR32:$rs, uimm5:$imm)>;
def : Pat<(i32 (srl GPR32:$rs, uimm5:$imm)), (SRLI32 GPR32:$rs, uimm5:$imm)>;
def : Pat<(i32 (sra GPR32:$rs, uimm5:$imm)), (SRAI32 GPR32:$rs, uimm5:$imm)>;

// s64 shifts — data DR64, amount GPR32 (legalizer {S64,S32}). Logical ops
// only (SLL64/SRL64/SRA64); post-RA setDesc → *_S1/*_S2 members. SIMD
// vectors stay C++ (lane-0 extract is multi-instr).
def : Pat<(i64 (shl DR64:$rs1, GPR32:$rs2)), (SLL64 DR64:$rs1, GPR32:$rs2)>;
def : Pat<(i64 (srl DR64:$rs1, GPR32:$rs2)), (SRL64 DR64:$rs1, GPR32:$rs2)>;
def : Pat<(i64 (sra DR64:$rs1, GPR32:$rs2)), (SRA64 DR64:$rs1, GPR32:$rs2)>;

//===----------------------------------------------------------------------===//
// Scalar / SIMD mul — 1:1. Logical opcodes only.
// MULL: $rd=$rs2 tied; TwoAddress inserts MOV when needed.
// v2i32: X2MULPL32 (low-half product per lane). s64 mul is legalizer
// widen/schoolbook then G_HAYDN_MUL64_WIDEN{,U} C++ residual.
//===----------------------------------------------------------------------===//

def : Pat<(i32 (mul GPR32:$rs1, GPR32:$rs2)), (MULL GPR32:$rs1, GPR32:$rs2)>;
def : Pat<(v2i32 (mul DR64:$rs1, DR64:$rs2)),
          (X2MULPL32 DR64:$rs1, DR64:$rs2)>;
def : Pat<(i32 (mulhs GPR32:$rs1, GPR32:$rs2)),
          (MULSSH GPR32:$rs1, GPR32:$rs2)>;
def : Pat<(i32 (mulhu GPR32:$rs1, GPR32:$rs2)),
          (MULUUH GPR32:$rs1, GPR32:$rs2)>;

//===----------------------------------------------------------------------===//
// Scalar abs — s32 / s64. Non-saturating ABS32/ABS64 (= llvm.abs).
// ABS32 logical is dest+src, matching Format E members.
// Logical opcodes only — post-RA setDesc commits ABS32_S* / ABS64_S* members.
//===----------------------------------------------------------------------===//

def : Pat<(i32 (abs GPR32:$rs)), (ABS32 GPR32:$rs)>;
def : Pat<(i64 (abs DR64:$rs)), (ABS64 DR64:$rs)>;

// Integer cmp Pats: SDAG setcc type inference fails on GPR32 multi-VT class
// (i32/f32). G_ICMP stays in C++ (already SEQ32/SLT32/SLTU32 + XORI invert).
//
// Select — s32 MOVT32 when Cond is s32 (tied FalseVal fallthrough).
// C++ residual (non-overlapping): Cond typed s1 after IRTranslator, and
// s64 dual-MOVT multi-instr. G_SELECT (Cond!=0)?T:F ↔ MOVT32 rd=F, T, Cond
//===----------------------------------------------------------------------===//

def : Pat<(i32 (select GPR32:$c, GPR32:$t, GPR32:$f)),
          (MOVT32 GPR32:$f, GPR32:$t, GPR32:$c)>;
