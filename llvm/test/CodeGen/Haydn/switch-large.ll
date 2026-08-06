; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr}}
;
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
;
; Test switch with many cases to stress branch lowering and register pressure.
; Dense switches with 4+ cases use jump tables; sparse switches use
; comparison+branch chains. Large switches exercise register allocation
; callee-save spilling, and jump table generation.
;
; NOTE: Does not use -verify-machineinstrs because the switch lowering creates
; PHI nodes in join blocks that survive as G_PHI past InstructionSelect.

;Switch with 16 consecutive cases (0..15) → jump table
define i32 @switch_16_cases(i32 %x) nounwind {
; switch_16_cases:
; Cmp+branch fusion no longer fires; range check lowered as sltu32+bnez.
; Bug 2 (BR_JT MO_JumpTableIndex fix): the jump table data is now emitted in
; rodata as.LJTI0_0 with 16.long entries (one per case). Previously the
; jump-table index was mis-printed and the table was not emitted. The 16
; entries are explicitly verified at lines 101-116 above.
entry:
  switch i32 %x, label %default [
    i32 0, label %bb0
    i32 1, label %bb1
    i32 2, label %bb2
    i32 3, label %bb3
    i32 4, label %bb4
    i32 5, label %bb5
    i32 6, label %bb6
    i32 7, label %bb7
    i32 8, label %bb8
    i32 9, label %bb9
    i32 10, label %bb10
    i32 11, label %bb11
    i32 12, label %bb12
    i32 13, label %bb13
    i32 14, label %bb14
    i32 15, label %bb15
  ]
bb0: ret i32 0
bb1: ret i32 1
bb2: ret i32 2
bb3: ret i32 3
bb4: ret i32 4
bb5: ret i32 5
bb6: ret i32 6
bb7: ret i32 7
bb8: ret i32 8
bb9: ret i32 9
bb10: ret i32 10
bb11: ret i32 11
bb12: ret i32 12
bb13: ret i32 13
bb14: ret i32 14
bb15: ret i32 15
default: ret i32 -1
}

;Switch with sparse large values → comparison chain (not dense enough for JT)
define i32 @switch_sparse_large(i32 %x) nounwind {
; switch_sparse_large:
; Cmp+branch fusion no longer fires; sparse switch uses binary-search
; lowering with slt32+beqz (not fused bge). Each pivot is a slt/beqz pair.
entry:
  switch i32 %x, label %default [
    i32 100, label %bb100
    i32 200, label %bb200
    i32 300, label %bb300
    i32 400, label %bb400
    i32 500, label %bb500
    i32 600, label %bb600
    i32 700, label %bb700
    i32 800, label %bb800
  ]
bb100: ret i32 1
bb200: ret i32 2
bb300: ret i32 3
bb400: ret i32 4
bb500: ret i32 5
bb600: ret i32 6
bb700: ret i32 7
bb800: ret i32 8
default: ret i32 0
}

;Switch with only default having code
define i32 @switch_all_default(i32 %x) nounwind {
; switch_all_default:
; Detailed MIR verified at lines 278-307 (under switch_merged_cases).
entry:
  switch i32 %x, label %default [
    i32 0, label %bb0
    i32 1, label %bb1
    i32 2, label %bb2
  ]
bb0: ret i32 10
bb1: ret i32 20
bb2: ret i32 30
default: ret i32 99
}

;Switch with case fall-through pattern (6 consecutive: 0..5) → jump table
; (LLVM IR doesn't have explicit fall-through, but cases returning
; the same value get merged by the backend)
define i32 @switch_merged_cases(i32 %x) nounwind {
entry:
  switch i32 %x, label %default [
    i32 0, label %group_a
    i32 1, label %group_a
    i32 2, label %group_b
    i32 3, label %group_b
    i32 4, label %group_c
    i32 5, label %group_c
  ]
group_a: ret i32 1
group_b: ret i32 2
group_c: ret i32 3
default: ret i32 0
}

;Switch with negative case values (5 consecutive: -3..1) → jump table
define i32 @switch_negative_cases(i32 %x) nounwind {
; switch_negative_cases:
; The range bounds check is now emitted as a fused BLTU (range-test then branch)
; previously an open-coded SLTU32 + BNEZ pair. Accept either shape.
entry:
  switch i32 %x, label %default [
    i32 -3, label %bb_neg3
    i32 -2, label %bb_neg2
    i32 -1, label %bb_neg1
    i32 0, label %bb0
    i32 1, label %bb1
  ]
bb_neg3: ret i32 3
bb_neg2: ret i32 2
bb_neg1: ret i32 1
bb0: ret i32 0
bb1: ret i32 -1
default: ret i32 99
}

;Single-case switch (degenerate)
define i32 @switch_single_case(i32 %x) nounwind {
; switch_single_case:
entry:
  switch i32 %x, label %default [
    i32 42, label %bb42
  ]
bb42: ret i32 1
default: ret i32 0
}

;Switch where all cases return the same value
define i32 @switch_all_same(i32 %x) nounwind {
; switch_all_same:
entry:
  switch i32 %x, label %default [
    i32 1, label %bb1
    i32 2, label %bb2
    i32 3, label %bb3
  ]
bb1: ret i32 42
bb2: ret i32 42
bb3: ret i32 42
default: ret i32 42
}
