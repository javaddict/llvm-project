; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s
;
; REGRESSION TEST (F34): Hardware-loop offset fields must be unsigned uimm6/uimm12.
;
; Bug: HaydnHardwareLoops modeled the SET_HWLOOP PC-relative offsets as a
; single 16-bit SIGNED field (MaxHWLoopOffsetBytes = (1<<15) * 4 = ±128KB).
; The ISA DB (Database/haydn_instruction_db.json, SET_HWLOOP entry) defines
; them as TWO distinct UNSIGNED fields:
; uimm6_offset1 — loop START offset, 6-bit unsigned (max 63 words = 252 bytes)
; uimm12_offset2 — loop END offset, 12-bit unsigned (max 4095 words = 16380 bytes)
; Both are PC-positive (forward) word offsets. The signed-16-bit model (a) had
; the wrong sign semantics (offsets are never negative), (b) had the wrong
; width for the START field (16 vs 6 bits — would silently accept loops whose
; start offset overflows uimm6), and (c) used a single blanket threshold
; instead of validating the two fields independently.
;
; Fix : replace the single MaxHWLoopOffsetBytes constant with two
; unsigned-width constants (MaxHWLoopStartOffsetBytes=252, MaxHWLoopEndOffsetBytes
; =16380) and validate the start and end offsets independently in
; loopBodyFitsRange. Loops exceeding either range are declined and keep their
; compare-and-branch sequence (branch-relaxation handles far targets).
;
; Test design:
; A small loop (well under both ranges) MUST be converted to a hardware
; loop. This guards against the fix accidentally over-rejecting.
; The test does NOT try to construct a loop near the 252-byte uimm6 limit
; because the START offset is always tiny (just the SET_HWLOOP instruction
; width + preheader padding) — it cannot overflow uimm6 in practice. The
; dominant constraint is the END offset (uimm12), and a body of a few
; instructions is nowhere near 16380 bytes.
;
; What breaks if the bug reappears:
; If the START field width regresses to >uimm6, large-preheader loops
; would be silently mis-encoded.
; If the END field width regresses to signed-16, the sign handling would
; be wrong (negative offsets are impossible for forward PC-relative loop
; ends), and the range threshold would drift from the spec's 4095 words.
;
; This test guards the "small loop MUST convert" path. The range-overflow
; path is exercised by the NumHWLoopRangeOverflow statistic in debug builds.

define i32 @hwloop_small_body(ptr %p, i32 %n) {
; CHECK-LABEL: hwloop_small_body:
; CHECK:       set_hwloop_f2
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %gep = getelementptr i32, ptr %p, i32 %i
  %val = load i32, ptr %gep
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}
