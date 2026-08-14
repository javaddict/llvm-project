; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -verify-machineinstrs \
; RUN:   --force-dwarf-frame-section -filetype=obj -o - < %s \
; RUN:   | llvm-dwarfdump -debug-frame - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=FRAME
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -verify-machineinstrs \
; RUN:   -filetype=obj -o - < %s \
; RUN:   | llvm-dwarfdump -eh-frame - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=EH
;
; Role: object / semantic — multi-function frame-chain unwind oracle.
; Pins that one CIE owns multiple FDEs, entry CFA is R13+0 with RA column 15,
; call-site CFA grows with negative CFA-relative CSR/LR slots, and epilogue
; restores CFA to R13+0. Covers SP-CFA (default) and FP-CFA (frame-pointer=all).
;
; DWARF CFA (T-ABI1): after FP = SP + StackSize (incoming SP), CFA is
; `.cfi_def_cfa fp, 0` / DW_CFA_def_cfa R14+0 — not `.cfi_def_cfa_register fp`,
; which keeps the prior StackSize offset and makes CFA = incoming_SP+StackSize.
; Peer: RISCVFrameLowering.cpp emitPrologue CFIBuilder.buildDefCFA(FPReg, 0).

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

declare void @leaf(i32)

; SP-CFA mid-level: callee-saved r8 + lr at negative CFA offsets.
define void @mid_sp(i32 %x) {
; ASM-LABEL: mid_sp:
; ASM:       .cfi_startproc
; ASM:       .cfi_def_cfa_offset
; ASM:       .cfi_offset {{r8|lr}}, -
; ASM:       .cfi_def_cfa {{sp|r13}}, 0
; ASM:       .cfi_endproc
entry:
  call void @leaf(i32 %x)
  call void @leaf(i32 %x)
  ret void
}

define i32 @outer_sp(i32 %a) {
; ASM-LABEL: outer_sp:
; ASM:       .cfi_startproc
; ASM:       .cfi_def_cfa_offset
; ASM:       .cfi_offset {{r8|lr}}, -
; ASM:       jal
; ASM:       .cfi_def_cfa {{sp|r13}}, 0
; ASM:       .cfi_endproc
entry:
  call void @mid_sp(i32 %a)
  call void @mid_sp(i32 %a)
  ret i32 %a
}

; FP-CFA chain: after fp = sp+N, CFA is fp+0 (not def_cfa_register).
define i32 @mid_fp(i32 %n) "frame-pointer"="all" {
; ASM-LABEL: mid_fp:
; ASM:       .cfi_startproc
; ASM:       .cfi_def_cfa_offset
; ASM:       .cfi_def_cfa {{fp|r14}}, 0
; ASM-NOT:   .cfi_def_cfa_register
; ASM:       .cfi_offset {{fp|lr|r8}}, -
; ASM:       .cfi_def_cfa {{sp|r13}}, 0
; ASM:       .cfi_endproc
entry:
  %a = alloca i32, i32 %n
  store i32 1, ptr %a
  call void @leaf(i32 %n)
  %v = load i32, ptr %a
  ret i32 %v
}

define i32 @outer_fp(i32 %n) "frame-pointer"="all" {
; ASM-LABEL: outer_fp:
; ASM:       .cfi_startproc
; ASM:       .cfi_def_cfa {{fp|r14}}, 0
; ASM-NOT:   .cfi_def_cfa_register
; ASM:       .cfi_offset {{fp|lr}}, -
; ASM:       jal
; ASM:       .cfi_def_cfa {{sp|r13}}, 0
; ASM:       .cfi_endproc
entry:
  %r = call i32 @mid_fp(i32 %n)
  %r2 = call i32 @mid_fp(i32 %r)
  ret i32 %r2
}

; Three-level SP nest: outer2 -> outer -> mid -> leaf (deeper frame-chain oracle).
define i32 @outer2_sp(i32 %a) {
; ASM-LABEL: outer2_sp:
; ASM:       .cfi_startproc
; ASM:       .cfi_def_cfa_offset
; ASM:       .cfi_offset {{r8|lr}}, -
; ASM:       jal
; ASM:       .cfi_def_cfa {{sp|r13}}, 0
; ASM:       .cfi_endproc
entry:
  %r = call i32 @outer_sp(i32 %a)
  %r2 = call i32 @outer_sp(i32 %r)
  ret i32 %r2
}

; Three-level FP nest: outer2_fp -> outer_fp -> mid_fp.
define i32 @outer2_fp(i32 %n) "frame-pointer"="all" {
; ASM-LABEL: outer2_fp:
; ASM:       .cfi_startproc
; ASM:       .cfi_def_cfa {{fp|r14}}, 0
; ASM-NOT:   .cfi_def_cfa_register
; ASM:       .cfi_offset {{fp|lr}}, -
; ASM:       jal
; ASM:       .cfi_def_cfa {{sp|r13}}, 0
; ASM:       .cfi_endproc
entry:
  %r = call i32 @outer_fp(i32 %n)
  %r2 = call i32 @outer_fp(i32 %r)
  ret i32 %r2
}

; Mixed nest: FP outer calls SP mid (CFA register handoff across call edge).
define i32 @mixed_fp_calls_sp(i32 %n) "frame-pointer"="all" {
; ASM-LABEL: mixed_fp_calls_sp:
; ASM:       .cfi_startproc
; ASM:       .cfi_def_cfa {{fp|r14}}, 0
; ASM-NOT:   .cfi_def_cfa_register
; ASM:       .cfi_offset {{fp|lr}}, -
; ASM:       jal
; ASM:       .cfi_def_cfa {{sp|r13}}, 0
; ASM:       .cfi_endproc
entry:
  call void @mid_sp(i32 %n)
  %r = call i32 @outer_sp(i32 %n)
  ret i32 %r
}

; Mixed nest reverse: SP outer calls FP mid (CFA register handoff other way).
define i32 @mixed_sp_calls_fp(i32 %n) {
; ASM-LABEL: mixed_sp_calls_fp:
; ASM:       .cfi_startproc
; ASM:       .cfi_def_cfa_offset
; ASM:       .cfi_offset {{r8|lr}}, -
; ASM:       jal
; ASM:       .cfi_def_cfa {{sp|r13}}, 0
; ASM:       .cfi_endproc
entry:
  %r = call i32 @mid_fp(i32 %n)
  %r2 = call i32 @outer_fp(i32 %r)
  ret i32 %r2
}

; Shared CIE: CFA=R13+0, RA column = 15 (LR), code alignment = golden
; two-byte min bundle-address align (not EncodedBytes / not Align=4).
; FRAME: CIE
; FRAME: Code alignment factor: 2
; FRAME: Data alignment factor: -4
; FRAME: Return address column: 15
; FRAME: DW_CFA_def_cfa: R13 +0
; At least two FDEs under that CIE (multi-function frame chain).
; FRAME: FDE cie=
; FRAME: DW_CFA_def_cfa_offset
; FRAME: DW_CFA_offset: R15
; FRAME: DW_CFA_def_cfa: R13 +0
; FRAME: FDE cie=
; FRAME: DW_CFA_def_cfa_offset
; FRAME: DW_CFA_offset: R15
; FRAME: DW_CFA_def_cfa: R13 +0
; FP path: CFA = R14+0 (incoming SP), not DW_CFA_def_cfa_register (keeps offset).
; FRAME: DW_CFA_def_cfa: R14 +0
; FRAME-NOT: DW_CFA_def_cfa_register
; Deeper nest still shares the same CIE (additional FDEs).
; FRAME: FDE cie=

; EH-frame product surface agrees with debug_frame on CIE identity.
; EH: CIE
; EH: Code alignment factor: 2
; EH: Data alignment factor: -4
; EH: Return address column: 15
; EH: DW_CFA_def_cfa: R13 +0
; EH: FDE cie=
; EH: DW_CFA_offset: R15
; EH: FDE cie=
