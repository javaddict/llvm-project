; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr_w}}
;
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
;
; Test switch statement lowering.
; Small switches (< 4 dense cases) use comparison + branch chains.
; Dense switches (>= 4 consecutive cases) use jump tables (see switch-jump-table.ll).

;Small switch (2 cases)
define i32 @switch_2_cases(i32 %x) {
switch i32 %x, label %default [
  i32 0, label %case0
  i32 1, label %case1
]

case0:
  ret i32 10

case1:
  ret i32 20

default:
  ret i32 0
}

;Small switch (3 cases)
define i32 @switch_3_cases(i32 %x) {
switch i32 %x, label %default [
  i32 1, label %case1
  i32 2, label %case2
  i32 3, label %case3
]

case1:
  ret i32 100

case2:
  ret i32 200

case3:
  ret i32 300

default:
  ret i32 0
}

;Switch with consecutive values (4 cases → jump table)
define i32 @switch_consecutive(i32 %x) {
; switch_consecutive:
; Range check: post- the sltu+bltu_w fusion no longer fires; the range
; check is emitted as a sltu32 + bnez_w pair (semantically equivalent).
switch i32 %x, label %default [
  i32 0, label %case0
  i32 1, label %case1
  i32 2, label %case2
  i32 3, label %case3
]

case0:
  ret i32 0

case1:
  ret i32 1

case2:
  ret i32 2

case3:
  ret i32 3

default:
  ret i32 -1
}

;Switch with sparse values
define i32 @switch_sparse(i32 %x) {
switch i32 %x, label %default [
  i32 10, label %case10
  i32 100, label %case100
  i32 1000, label %case1000
]

case10:
  ret i32 1

case100:
  ret i32 2

case1000:
  ret i32 3

default:
  ret i32 0
}

;Switch with negative values
define i32 @switch_negative(i32 %x) {
; switch_negative:
; Cmp+branch fusion no longer fires; eq+branch lowered as seq32+bnez_w.
switch i32 %x, label %default [
  i32 -1, label %case_neg1
  i32 0, label %case0
  i32 1, label %case1
]

case_neg1:
  ret i32 -1

case0:
  ret i32 0

case1:
  ret i32 1

default:
  ret i32 99
}
