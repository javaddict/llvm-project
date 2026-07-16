; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -haydn-m0-slot-or=0 -filetype=obj < %s -o %t.o && \
; RUN:     llvm-objdump -d %t.o | FileCheck %s
; Status : previously-XFAIL regression resolved; lit PASS.
; Phase-2 collateral: the 48-bit funct-flat fallback (deleted in
; Phase 2b) was masking decoder gaps — SEXT _S1_M0 Mode-0 bundle
; decode (sext32t64-in-bundle) + x2muls32 MAC-trie entry (m0-slot-or-s1-mac
; m3-s2-mac-emit). The.td defs exist (SEXT_GPR32_TO_DR64_S1_M0, X2MULS32_M0S1)
; but the emitted bytes don't match the trie — an encoding/trie/dispatch
; mismatch to diagnose. Real bug, tracked here; do NOT weaken the CHECKs.
;
; X2MULA32 binary encoder emits 8 zero bytes via the _M3S2 ACC-variant path
; (X2MULS32 emits correctly via the identical path). Same root cause as
; m0-slot-or-s1-mac.ll — likely tablegen Inst{} field-encoding or $rd_in
; when the X2MULA32 encoder is fixed; the CHECKs above remain pinned to the
; spec-correct decode.
;
; REGRESSION TEST: Slice B/C — Mode 3 (bits[3:0]=1111, wide-s2) s2 MAC
; accumulate ENCODE + emit-time slot selection.
;
; Context: accumulating MAC (rtd += rsd1*rsd2) needs the full RRR layout (3 DR
; reg fields). Mode 0 s2 is only 18b destructive (2 reg fields) and CANNOT
; represent the accumulator form; Mode 3 widens s2 to 22b with FU(2)+rsd1(4)+
; rsd2(4)+rtd(4)+opcode(8). Slice A (prior revision) shipped DECODE for the
; _M3S2 variants; this test pins the ENCODER side.
;
; Mechanism (Option B emit-time selection, mirroring funct-routed ALU64):
; Under -haydn-m0-slot-or=0 the finalizer does NOT rewrite generic
; X2MULA32 -> X2MULA32_M0S1, so the generic MAC reaches encodeBundle.
; findCompatibleRow assigns the lone MAC child to s2 (Slot enum S2=0 is
; tried first; Row 2 s2=FU_MAC accepts FU_MAC). ChildSlots[0]=0=S2.
; getM3S2Variant(X2MULA32, S2) returns X2MULA32_M3S2; encodeBundle emits
; a Mode-3 bundle (pattern 1111, s2 window bits[63:42]).
;
; What breaks if the encoder regresses:
; If getM3S2Variant is removed, the generic MAC falls to the hand-rolled
; packInstructionIntoSlot (Mode 0 s2 destructive), producing a DIFFERENT
; byte pattern (pattern 0011, 18b s2 with rtd=rsd1 forced equal) — the
; CHECK for x2mula32 with 3 distinct DR64 regs would fail (rsd2 would be
; dropped or rsd1 forced to equal rtd).
; If the Mode-3 pattern bit (1111) is wrong, objdump decodes the bundle
; as <unknown> or a wrong op.
;
; Spec reference: encoding_manual.md §8.1 (Mode 3 wide-s2), §6.6 (s1 MAC
; opcode space reused for s2 MAC per assumption).
; Related decision: (Mode 3 s2 MAC decode + emit), (s1 MAC slice).

declare { i64, i64 } @llvm.haydn.x2mula32(i64, i64, i64, i64)
declare { i64, i64 } @llvm.haydn.x2muls32(i64, i64, i64, i64)

; Accumulate: dN += dN * dN. With the finalizer OFF the generic X2MULA32
; reaches the encoder, which routes it to s2 (Mode 3) via getM3S2Variant.
; (Path B): 2-dest accum — args (acc1, acc2, src1, src2), returns {i64,i64}.
define i64 @test_x2mula32_s2_mode3(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
  %r = call { i64, i64 } @llvm.haydn.x2mula32(i64 %acc, i64 %acc2, i64 %a, i64 %b)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x2muls32_s2_mode3(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
  %r = call { i64, i64 } @llvm.haydn.x2muls32(i64 %acc, i64 %acc2, i64 %a, i64 %b)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

; CHECK-LABEL: <test_x2mula32_s2_mode3>:
; CHECK:      x2mula32 {{d[0-9]+}}, {{d[0-9]+}}, {{d[0-9]+}}
; CHECK-NOT:  <?>
; CHECK-NOT:  <unknown>
; CHECK-LABEL: <test_x2muls32_s2_mode3>:
; CHECK:      x2muls32 {{d[0-9]+}}, {{d[0-9]+}}, {{d[0-9]+}}
; CHECK-NOT:  <?>
; CHECK-NOT:  <unknown>
