; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=LOOP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-postra-interblock \
; RUN:     -haydn-postra-region-end-edges -haydn-sms2 < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=LOOP
;
; W68.3R bounded convergence loop: under -haydn-sms2 the seat at
; addPreEmitPass becomes the census-driven repair loop
;   S2 -> stalls -> HWLoop validate/demote -> BranchRelaxation LAST
; repeated to a fixed point (bound = #cond-branches + #hwloop-setups + 2;
; exhaustion is a hard diagnostic). This pin asserts:
;   * the loop runs clean under -verify-machineinstrs at both the plain
;     flag and full inter-block config;
;   * a jalr-terminated body still freezes (long-branch promotion under
;     S2 reorder re-enters BranchRelaxation inside the loop);
;   * default (flag off) output is unchanged.
;
; The chain shape gives multiple conditional sites so the loop exercises
; its bound accounting; no HWLoop here (mattr=-hwloop), the hwloop
; revalidation matrix is late-convergence-hwloop.ll.

define i32 @conv_chain(ptr nocapture readonly %a, i32 %n) {
entry:
  %c = icmp sgt i32 %n, 0
  br i1 %c, label %l1, label %exit
l1:
  %x = load i32, ptr %a, align 4
  %v1 = add i32 %x, 1
  %d1 = icmp sgt i32 %v1, 10
  br i1 %d1, label %l2, label %exit
l2:
  %v2 = mul i32 %v1, 3
  %d2 = icmp ult i32 %v2, 100
  br i1 %d2, label %l3, label %exit
l3:
  %v3 = sub i32 %v2, 7
  br label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %v1, %l1 ], [ %v2, %l2 ], [ %v3, %l3 ]
  ret i32 %r
}

; OFF-LABEL: conv_chain:
; OFF: jalr

; LOOP-LABEL: conv_chain:
; LOOP: jalr

; optnone: the driver skips (quality transform; GR2.4 scopes this to the
; LateConvergence driver only — postmisched itself runs for optnone), the
; function still freezes through the mandatory-scheduler + residual
; Finalize lane; loop must not touch it.
define i32 @conv_optnone(ptr nocapture readonly %a, i32 %n) noinline optnone {
entry:
  %c = icmp sgt i32 %n, 0
  br i1 %c, label %b, label %e
b:
  %x = load i32, ptr %a, align 4
  %v = add i32 %x, %n
  br label %e
e:
  %r = phi i32 [ 0, %entry ], [ %v, %b ]
  ret i32 %r
}

; OFF-LABEL: conv_optnone:
; OFF: jalr

; LOOP-LABEL: conv_optnone:
; LOOP: jalr
