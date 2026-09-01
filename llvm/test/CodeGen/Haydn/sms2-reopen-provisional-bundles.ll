; REQUIRES: asserts
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2=false < %s \
; RUN:     | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 < %s \
; RUN:     | FileCheck %s --check-prefix=ON
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-postra-interblock \
; RUN:     -haydn-postra-region-end-edges -haydn-sms2 \
; RUN:     -debug-only=haydn-post-ra-sched < %s -o /dev/null 2>&1 \
; RUN:     | FileCheck %s --check-prefix=REOPEN
;
; W68.2R S2 reopen pin (STATUS limit #1): -haydn-sms2 is product-on.
; The explicit-off arm is verify-clean S1-only packing.
; The FIRST S2 invocation reopens every provisional S1 BUNDLE whose real
; children carry generated member->logical identity (contracts/pipeline.md
; "S1/S2 repair law" — S2 rebuilds from current bare MIs; unrecoverable
; roots stay committed). Later convergence iterations do NOT reopen their
; own S2 output (the driver's fixed point requires pre-existing roots to
; pin cycles). This kernel forms multi-MI product cycles in S1, so the
; reopen path executes on every -haydn-sms2 compile: one
; "reopened N (N>0)" diagnostic, then a jalr-terminated body.

define i32 @reopen_kernel(ptr nocapture readonly %a, ptr nocapture readonly %b, i32 %n) {
entry:
  br label %body
body:
  %i = phi i32 [ 0, %entry ], [ %i2, %body ]
  %x = phi i32 [ 0, %entry ], [ %m2, %body ]
  %ai = getelementptr i32, ptr %a, i32 %i
  %bi = getelementptr i32, ptr %b, i32 %i
  %av = load i32, ptr %ai, align 4
  %bv = load i32, ptr %bi, align 4
  %s = add i32 %av, %bv
  %m = mul i32 %s, %av
  %m2 = add i32 %m, %x
  %i2 = add i32 %i, 1
  %c = icmp slt i32 %i2, %n
  br i1 %c, label %body, label %exit
exit:
  ret i32 %m2
}

; Default-off packing: S1-only product path (no experimental S2).
; OFF-LABEL: reopen_kernel:
; OFF: { nop; xor32 r0, r0, r0 }
; GR2.1 Kind-A restamp: software-pipelines (guarded peel; kernel packs
; {add32+ld32}, {mull+IV}, {s_lw}, {add32+IV}, {slt+move}).
; OFF: { slt32 r12, r3, r7; ld32 r5, r1, 0 }
; OFF: { addi32 r1, r1, 4; addi32 r6, r4, 1 }
; OFF: { add32 r7, r5, r7; ld32 r12, r1, 0 }
; OFF: { mull r5, r7, r5; addi32 r6, r6, 1 }
; OFF: jalr

; Opt-in S2 is verify-clean and still jalr-terminated. Packing may differ
; because S2 re-evaluates provisional S1 choices.
; ON-LABEL: reopen_kernel:
; ON: jalr

; First S2 invocation reopens a positive number of provisional roots in
; this function; later convergence iterations must not reopen again.
; REOPEN: HaydnPostRASched {{S2}}: reopened {{[1-9][0-9]*}} provisional BUNDLE root(s) in reopen_kernel
; REOPEN-NOT: HaydnPostRASched {{S2}}: reopened {{.*}} in reopen_kernel
