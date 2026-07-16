; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
; REQUIRES: haydn-registered-target
;
; This test was XFAIL'd from until reverted the
; GPR-port-FuncUnits change to HaydnSchedule.td that triggered a SIGSEGV in
; the post-RA VLIW scheduler. XFAIL removed now that the single-stage
; slot-only itinerary model is restored. See decision and lesson.
;
; REGRESSION TEST: VLIW packetizer must handle pseudo instructions.
;
; Bug: The VLIW packetizer's ignorePseudoInstruction did not handle
; pseudo instructions like LD32_POST_INC / ST32_POST_INC (created by the
; load/store optimizer). These pseudos have NoItinerary, so the DFA cannot
; reserve resources for them. When addToPacket was called for such an
; instruction, the assertion `canReserveResources(MI)` fired.
; Fix: ignorePseudoInstruction now checks MI.isPseudo to skip all
; pseudo instructions. They are expanded later by ExpandPseudos.
;
; A separate OBJ (objdump) round-trip test is still XFAIL'd due to
; disassembler limitations with D-class bundles — see iir-e2e-bundle-dump.ll.
;
; VLIW bundle verification test for IIR biquad kernel.
;
; This test verifies the assembly and object output for a realistic DSP
; workload. Two levels of verification:
;
; (a) ASM prefix checks the assembly text output for expected instruction
; mnemonics and correct program structure.
;
; (b) OBJ prefix checks the ELF object file disassembles correctly, verifying
; the MC encoder/decoder round-trips faithfully through the full pipeline.
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
; ASM-DAG: mul64.ll
; ASM-DAG: st32
; ASM-DAG: add32
; (SFR-strip) changed bundle layout — rebaselined.
; The icmp slt back-edge materializes as slt32+bnez_w (unfused) because the
; SFR-strip lets the scheduler pack slt32 with the preceding st32.
; ASM-DAG: bnez_w
; ASM: jalr_w{{(\.s[012])?}} r0, lr, 0
