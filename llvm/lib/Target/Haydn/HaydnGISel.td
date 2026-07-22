//===-- HaydnGISel.td - Haydn GlobalISel Patterns ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// G-CG1: incremental GlobalISel Pats. selectImpl matches these before the C++
// switch in HaydnInstructionSelector. C++ covers G_MUL (MULL / X2MULPL32),
// G_SMULH/UMULH, s64 icmp, SIMD shifts, loads, and other multi-instr lowers.
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
// Prefer NOT32 for IR "not" (PatFrag = xor x, -1). Unary peer FlexMap is
// NOT32_S* (HaydnFormatsALU32, opc 0b0010000 / #16).
def : Pat<(i32 (not GPR32:$rs)), (NOT32 GPR32:$rs)>;

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

def : Pat<(i32 (shl GPR32:$rs1, GPR32:$rs2)), (SLL32 GPR32:$rs1, GPR32:$rs2)>;
def : Pat<(i32 (srl GPR32:$rs1, GPR32:$rs2)), (SRL32 GPR32:$rs1, GPR32:$rs2)>;
def : Pat<(i32 (sra GPR32:$rs1, GPR32:$rs2)), (SRA32 GPR32:$rs1, GPR32:$rs2)>;

// Integer cmp Pats: SDAG setcc type inference fails on GPR32 multi-VT class
// (i32/f32). G_ICMP stays in C++ (already SEQ32/SLT32/SLTU32 + XORI invert).
//
// Select — s32 MOVT32 (tied FalseVal fallthrough). s64 dual-MOVT stays C++.
// G_SELECT (Cond!=0)?T:F ↔ MOVT32 rd=F, T, Cond
//===----------------------------------------------------------------------===//

def : Pat<(i32 (select GPR32:$c, GPR32:$t, GPR32:$f)),
          (MOVT32 GPR32:$f, GPR32:$t, GPR32:$c)>;
