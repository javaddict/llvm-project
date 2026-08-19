//===-- HaydnPseudos.td - Compiler-only Haydn pseudos -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Authored compiler policy: rematerializable materializers, varargs,
// libcalls, cross-bank copies, and addressing-mode expanders.
// These defs never carry encoding bits. Selection/Patterns stay in
// HaydnInstrInfo.td / HaydnIntrinsics.td. Product members stay generated.
//
// Split out of HaydnInstrInfo.td (W56 file reshape). Matcher-visible
// logical shells that happen to be isPseudo (NOP/WFI/CSRW and similar)
// stay with the logicals.
//
//===----------------------------------------------------------------------===//

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

// Unconditional branch. Expands to BEQZ R0, target.
def B : HaydnPseudo<(outs), (ins brtarget:$offset),
                     "b\t$offset",
                     [(br bb:$offset)]> {
  let isBranch = 1;
  let isTerminator = 1;
  let isBarrier = 1;
  let TSFlags{2-0} = 0b001;
}

// Call pseudos — same caller-saved clobbers as JAL/JALR.
let Defs = [R1, R2, R3, R4, R5, R6, R7, R12,
            D0, D1, D2, D3, D4, D5, D6, D7,
            R15] in {
def PseudoCALL : HaydnPseudo<(outs GPR32:$rd), (ins calltarget:$target),
                              "call\t$target",
                              []> {
  let isCall = 1;
  let TSFlags{2-0} = 0b001;
}
def PseudoCALLIndirect : HaydnPseudo<(outs GPR32:$rd), (ins GPR32:$rs),
                                      "calli\t$rs",
                                      []> {
  let isCall = 1;
  let TSFlags{2-0} = 0b001;
}
}

let isBarrier = 1, isTerminator = 1, isBranch = 1, isIndirectBranch = 1 in
def BR_JT : HaydnPseudo<(outs), (ins GPR32:$addr, i32imm:$jt),
                          "br_jt\t$addr, $jt",
                          []>;

let hasSideEffects = 1, isNotDuplicable = 1, Defs = [SFR] in {
def SET_HWLOOP : HaydnPseudo<(outs), (ins uimm5:$sel, brtarget:$loop_start, brtarget:$loop_end, uimm16:$cnt),
                               "set_hwloop\t$sel, $loop_start, $loop_end, $cnt", []>;
def SET_HWLOOP_REG : HaydnPseudo<(outs), (ins uimm5:$sel, brtarget:$loop_start, brtarget:$loop_end, GPR32:$rs),
                                  "set_hwloop_f2\t$sel, $loop_start, $loop_end, $rs", []>;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def LoopDec : HaydnPseudo<(outs GPR32:$dst), (ins GPR32:$src),
                          "loop_dec\t$dst, $src", []>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0,
    isBranch = 1, isTerminator = 1 in
def LoopJNZ : HaydnPseudo<(outs), (ins GPR32:$tc, brtarget:$target),
                          "loop_jnz\t$tc, $target", []>;

let hasSideEffects = 1, isNotDuplicable = 1 in
def LoopStart : HaydnPseudo<(outs), (ins GPR32:$src, simm6:$adj),
                            "loop_start\t$src, $adj", []>;

let isBranch = 1, isTerminator = 1, isMeta = 1,
    hasSideEffects = 1, isNotDuplicable = 1,
    mayLoad = 0, mayStore = 0 in
def PseudoLoopEnd : HaydnPseudo<(outs), (ins brtarget:$loopBody),
                                "pseudo_loop_end\t$loopBody", []>;

let hasSideEffects = 1 in {
def SETCBR_BEGIN : HaydnPseudo<(outs), (ins i32imm:$cbr_sel, GPR32:$val),
    "setcbr_begin\t$cbr_sel, $val", []>;
def SETCBR_END : HaydnPseudo<(outs), (ins i32imm:$cbr_sel, GPR32:$val),
    "setcbr_end\t$cbr_sel, $val", []>;
}

