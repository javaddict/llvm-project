; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -haydn-m0-slot-or=1 \
; RUN:     -filetype=obj < %s -o %t.o && \
; RUN:     llvm-objdump -d %t.o | FileCheck %s
; Status : previously-XFAIL regression resolved; lit PASS.
; Phase-2 collateral: the 48-bit funct-flat fallback (deleted in
; Phase 2b) was masking decoder gaps — SEXT _S1_M0 Mode-0 bundle
; decode (sext32t64-in-bundle) + x2muls32 MAC-trie entry (m0-slot-or-s1-mac
; m3-s2-mac-emit). The.td defs exist (SEXT_GPR32_TO_DR64_S1_M0, X2MULS32_M0S1)
; but the emitted bytes don't match the trie — an encoding/trie/dispatch
; mismatch to diagnose. Real bug, tracked here; do NOT weaken the CHECKs.
;
; X2MULA32 binary encoder emits 8 zero bytes via the _M0S1 ACC-variant path
; (X2MULS32 emits correctly via the identical path; filetype=asm is correct
; for both). Likely tablegen Inst{} field-encoding or $rd_in tied-def
; the X2MULA32 encoder is fixed; the CHECKs above remain pinned to the
; spec-correct decode.
;
; REGRESSION TEST: MAC Slice 1 — s1 MAC accumulate decode-included tablegen.
;
; Bug: the hand-rolled FU_MC encode path produced GARBAGE for standalone
; X2MULA32/X2MULS32 — opcode field 0x00 (should be 0x21/0x22 per §6.6), which
; decoded as "x2mula32 r0,r0,r0,<?>" (GPR regs + a printOperand OOB placeholder).
; The X2MULA32_M0S1 / X2MULS32_M0S1 slot-OR variants encode the spec-correct
; opcode (Inst{31-24}=0x21/0x22) + DR64 reg fields (rsd1@43:40, rsd2@39:36
; rtd@35:32), decoding cleanly as "x2mula32 dN, dN, dN" (3 DR64 operands, no
; <?>). This test pins the fix under -haydn-m0-slot-or=1: a regression to the
; garbage opcode fails the CHECK (r0 instead of dN, or a <?> 4th operand).
;
; User corrections : (1) decode migrates to tablegen
; (DecoderTableHaydnM0S1MAC64, NOT encode-only); (2) accumulate MAC is legal in
; BOTH s1 and s2 — this slice handles s1 (Mode 0); s2 (Mode 3 wide-s2) is a
; separate later slice; (3) MAC 1c/2c forwarding latency is scheduling, out of
; scope here.

declare { i64, i64 } @llvm.haydn.x2mula32(i64, i64, <2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2muls32(i64, i64, <2 x i32>, <2 x i32>)
; dN += dN * dN (accumulate). Flag-on finalizer rewrites X2MULA32 ->
; X2MULA32_M0S1; the slot-OR encoder emits the spec-correct s1 MAC word.
; (Path B): 2-dest accum — args (acc1, acc2, src1, src2), returns {i64,i64}.
define i64 @test_x2mula32(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2mula32(i64 %acc, i64 %acc2, <2 x i32> %bc.1, <2 x i32> %bc.2)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x2muls32(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2muls32(i64 %acc, i64 %acc2, <2 x i32> %bc.3, <2 x i32> %bc.4)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

; CHECK-LABEL: <test_x2mula32>:
; CHECK: x2mula32 {{d[0-9]+}}, {{d[0-9]+}}, {{d[0-9]+}}
; CHECK-NOT: <?>
; CHECK-LABEL: <test_x2muls32>:
; CHECK: x2muls32 {{d[0-9]+}}, {{d[0-9]+}}, {{d[0-9]+}}
; CHECK-NOT: <?>
