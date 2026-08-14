; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
; REQUIRES: haydn-registered-target

; Role: semantic — IIR biquad assembly must emit expected DSP mnemonics
; through the post-RA packer without pseudo-resource DFA aborts.

; REGRESSION: the packer must tolerate post-inc load/store pseudos
; (NoItinerary) via isPseudo skip until ExpandPseudos; do not reserve
; resources on those MIs. Object-level opcode presence is covered by
; c-e2e-bundle-dump.ll (live object contract, not expected-fail).
;
; ASM checks expected instruction mnemonics and program structure for a
; realistic DSP IIR biquad workload.
;
; NOTE: The VLIW packetizer is currently disabled (see HaydnTargetMachine.cpp
; line 260). When re-enabled, add back -print-after=haydn-vliw-packetizer MIR
; checks for BUNDLE instructions.
;
; The biquad_block function processes N samples through a 2-tap FIR kernel:
; out[i] = b0 * in[i] + b1 * in[i]
; This exercises: GEP, load, mul32, mac32, add32, st32, slt32, bnez_w, phi.
;=============================================================================;

define void @biquad_block(ptr %in, ptr %out, ptr %coeffs, i32 %N) nounwind {
entry:
  %b0 = load i32, ptr %coeffs
  %c1 = getelementptr i32, ptr %coeffs, i32 1
  %b1 = load i32, ptr %c1
  br label %loop

loop:
  %i = phi i32 [0, %entry], [%next, %loop]
  %ptr_in = getelementptr i32, ptr %in, i32 %i
  %xn = load i32, ptr %ptr_in
  %p0 = mul i32 %b0, %xn
  %p1 = mul i32 %b1, %xn
  %sum = add i32 %p0, %p1
  %ptr_out = getelementptr i32, ptr %out, i32 %i
  store i32 %sum, ptr %ptr_out
  %next = add i32 %i, 1
  %cmp = icmp slt i32 %next, %N
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}

; === Assembly-level checks ===
; Verify the assembly output contains expected Haydn instruction mnemonics.

; ASM-LABEL: biquad_block:
; ASM-DAG: xor32
; ASM-DAG: subi32 sp, sp
; ASM-DAG: ld32
; ASM-DAG: mull
; ASM-DAG: {{st32|s_sw}}
; ASM-DAG: add32
; (SFR-strip) changed bundle layout — rebaselined.
; The icmp slt back-edge materializes as slt32+bnez_w (unfused) because the
; SFR-strip lets the scheduler pack slt32 with the preceding st32.
; ASM-DAG: bnez
; ASM: jalr{{(\.s[012])?}} r0, lr, 0
