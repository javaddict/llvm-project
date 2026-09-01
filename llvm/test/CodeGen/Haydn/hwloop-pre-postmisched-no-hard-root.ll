; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -haydn-enable-hwloops -mattr=+hwloop -O2 \
; RUN:   -stop-before=postmisched %s -o - | FileCheck %s

; Role: MIR — Pre-SMS-HANDOFF / exact no-split qualification: production post-RA entry must not see a pre-existing hard BUNDLE root.

; Pre-SMS-HANDOFF / exact no-split qualification: production post-RA entry
; must not see a pre-existing hard BUNDLE root. Role A HardwareLoops emits
; final wide SET forms before ExpandPseudos/pack; formation pads are bare
; logical NOPs for the first exact commit at postmisched.
;
; identity so residual generic SET_HWLOOP{,_REG} cannot reappear pre-pack.

define void @count_store(ptr %p, i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %ge = getelementptr i32, ptr %p, i32 %i
  store i32 %i, ptr %ge
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %exit

exit:
  ret void
}

; CHECK-LABEL: name: count_store
; No hard BUNDLE root enters post-RA before SMS-HANDOFF activation.
; CHECK-NOT: BUNDLE
; Final wide form only (not residual generic SET_HWLOOP / SET_HWLOOP_REG).
; CHECK: SET_HWLOOP_{{W|F2_W}}
; CHECK-NOT: {{SET_HWLOOP[^_A-Z]}}
; CHECK-NOT: SET_HWLOOP_REG
; Formation setup pads remain bare logical NOPs for post-RA exact commit.
; CHECK: NOP
