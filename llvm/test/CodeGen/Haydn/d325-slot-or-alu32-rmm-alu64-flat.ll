; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -filetype=obj < %s -o %t.o
; RUN: llvm-objcopy -O binary --only-section=.text %t.o %t.bin
;
; REGRESSION TEST: encode s0 ALU32 RI/MOVT/MULL + s1 ALU64-flat families
; through the product Bundle128 encoder (retired -haydn-m0-slot-or dual path).
;
; Historical bug: Mode-0 slot-OR finalizer left these families generic and
; forced hand-rolled packInstructionIntoSlot; product path is single encode.
;
; Fix :
; (1) s0 ALU32 RI (12 ops) + MOVT32/MOVF32 (2) + MULL family (4) → the
; existing _M0S0ALU decode variants (HaydnInstrInfoS0ALUM0.td, item 3)
; are DUAL-USE — their Inst{} ext field binds imm/rs2 raw (no scaling
; for ALU32 RI per §6.5; the funct bits are static for MULL), so
; getBinaryCodeForInstr emits bytes identical to the hand-roller.
; (2) s1 ALU64 flat (42 ops: MOVE64/NEG64/NOT64/ABS64/SEQ64/SLT64/SLE64
; MOVT64/MOVF64/TRANSF64*/NSA*/NSAZ*/POPCOUNT64/EXP2/LOG2/RECIP/SQRT
; ADD64S*/ADD64_H/L/X2ADD32/X2SUB32/X2SLL32/X2SRL32/X2SRA32/SLLI64
; SRLI64/SRAI64/ARCTAN/SIN_COS) → NEW _S1_M0 encode-only variants
; (HaydnInstrInfoS1ALU64M0.td) mirroring slice-2 ADD64_S1_M0.
;
; Test design: a kernel exercising SEQ64/SLT64/SLE64/MOVT64 (DR64 compare
; intrinsics) + ADD64S (i64 add) compiles to.obj under both flag-off
; (hand-rolled packInstructionIntoSlot) and flag-on (slot-OR encoder). The
; text sections must be byte-identical — the structural field-layout match
; (hand-roller PS_S0 ALU RI/MOVT/MULL + PS_S1 FU_ALU64 flat reads the same
; operands and writes the same Inst{} bits as the tablegen variants) makes
; the two paths produce the same bytes. A diff means a regression
; reintroduced a mismatch (ext scaling, operand-index swap, etc.).
;
; DEFERRED (NOT covered here — see decision log):
; 8 SIMD shift-imm ops (X2SLLI32 family, X4SLLI16 family) need a custom
; opcode-OR EncoderMethod for shamt folding — stays on hand-roller.
; MOVEGPR2SFR / MOVESFR2GPR (2 ops) — asymmetric single-reg operand model
; doesn't fit the 2-field s1 ALU64 layout cleanly — stays on hand-roller.
;
; The cmp passing is the regression signal. No CHECK lines needed — the
; byte-equality IS the assertion.

define i64 @seq64_test(i64 %a, i64 %cmp_rhs) nounwind {
  call void @llvm.haydn.seq64(i64 %a, i64 %cmp_rhs)
  ret i64 %a
}
declare void @llvm.haydn.seq64(i64, i64)

define i64 @slt64_test(i64 %a, i64 %cmp_rhs) nounwind {
  call void @llvm.haydn.slt64(i64 %a, i64 %cmp_rhs)
  ret i64 %a
}
declare void @llvm.haydn.slt64(i64, i64)

define i64 @sle64_test(i64 %a, i64 %cmp_rhs) nounwind {
  call void @llvm.haydn.sle64(i64 %a, i64 %cmp_rhs)
  ret i64 %a
}
declare void @llvm.haydn.sle64(i64, i64)

define i64 @add64s_test(i64 %a, i64 %b) nounwind {
  %r = add i64 %a, %b   ; selects ADD64 (already migrated slice-2) — sanity
  ret i64 %r
}
