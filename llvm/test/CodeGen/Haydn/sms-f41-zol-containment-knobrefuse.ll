; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -debug-only=pipeliner \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms=false \
; RUN:     < %s -o %t.policy.s 2>&1 | FileCheck %s --check-prefix=POLICY
; RUN: FileCheck %s --check-prefix=POLICY-ASM < %t.policy.s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=+hwloop -haydn-enable-hwloops \
; RUN:     -haydn-enable-multistage-sms=false \
; RUN:     -global-isel-abort=1 -O2 -verify-machineinstrs \
; RUN:     -haydn-sms-containment-max=3 -debug-only=pipeliner \
; RUN:     < %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ZOL
; REQUIRES: asserts

; REGRESSION TEST (W34 / F41, CR-H3) — ZOL lift-refusal pin for the
; -haydn-sms-containment-max test knob.
;
; Product hardware-loop and multi-stage flags stay OFF as defaults.
; The ZOL arm below forces hardware loops ON as a test-only vehicle; it
; does not flip the product default and does not enable multi-stage SMS.
; Combined dual-ON qualify waits for independent hwloop and multi-stage
; qualify tracks.
;
; Bug class guarded: the F41 knob lifts the pre-RA StageCount containment
; for SOFT counted loops only (expander-path lit vehicle). ZOL multi-stage
; must stay contained even with the knob lifted: the ZOL expansion law
; (LoopStart $adj / PseudoLoopEnd interplay with the expander's cloned
; terminators) is owned by the post-RA HaydnMultiStageSMS host. The code
; pins the ZOL bound to productSMSContainmentMaxStageCount=1 regardless of
; the knob. If this pin fails, the knob has opened an unowned pre-RA ZOL
; multi-stage expansion path — silent wrong code risk (ZOL cannot emit a
; dynamic guard).
;
; Test design:
;   Policy arm (+hwloop attr, flags false): does not enable product
;     hardware loops. Soft containment still rejects StageCount>1; asm
;     has no SET and no multi-stage SWPS annotation.
;   ZOL arm: constant-trip MAC body whose ZOL MinTripCount (16) passes
;     the ZOL MinTC gate, so the ONLY remaining reject surface is the
;     containment itself — same shape as sms-zol-multistage-containment.ll,
;     but with the knob lifted to 3 to prove refusal. PASS = the reject
;     line still fires. If the ZOL bound silently followed the knob, the
;     loop would expand a bare pre-RA multi-stage ZOL kernel (no dynamic
;     guard possible). Multi-stage product stays explicitly false.

; POLICY: SMS-SHOULDUSE: reject multi-stage stages={{[2-9]|[1-9][0-9]+}} II={{[0-9]+}} (pre-RA StageCount>1 containment; post-RA multi-stage only)
; POLICY-NOT: SMS-SHOULDUSE: accept
; POLICY-NOT: SMS-TC: soft adjustTripCount
; POLICY-ASM-NOT: set_hwloop
; POLICY-ASM-NOT: #<swps> stages={{[2-9]|[1-9][0-9]+}}
; POLICY-ASM: jalr

; ZOL: SMS-SHOULDUSE: reject multi-stage stages={{[2-9]|[1-9][0-9]+}} II={{[0-9]+}} (pre-RA StageCount>1 containment; post-RA multi-stage only)
; ZOL-NOT: SMS-SHOULDUSE: accept
; ZOL-NOT: SMS-TC: soft adjustTripCount

define i32 @sms_f41_zol_knobrefuse(ptr nocapture readonly %a,
                                   ptr nocapture readonly %b) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %pa = getelementptr inbounds i32, ptr %a, i32 %i
  %pb = getelementptr inbounds i32, ptr %b, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %mul = mul i32 %va, %vb
  %acc.next = add i32 %acc, %mul
  %i.next = add nuw i32 %i, 1
  %cond = icmp ult i32 %i.next, 16
  br i1 %cond, label %loop, label %exit
exit:
  ret i32 %acc.next
}
