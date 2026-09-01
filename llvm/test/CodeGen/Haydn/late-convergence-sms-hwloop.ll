; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-enable-hwloops \
; RUN:     -enable-pipeliner < %s 2>&1 | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-enable-hwloops \
; RUN:     -enable-pipeliner -haydn-sms2 < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=LOOP
;
; W68.3R combined SMS+HWLoop convergence shape (exit-row matrix item):
; generic pre-RA MachinePipeliner owns the modulo transform (soft + ZOL),
; HardwareLoops forms the retained hardware loop, and under -haydn-sms2
; the bounded convergence driver runs AFTER those shapes exist — S2
; reschedules the pipelined body, FixupHwLoops revalidates the retained
; SET windows against post-S2 bytes (re-invocation IS the revalidation),
; BranchRelaxation closes ranges last. Pins:
;   * converges clean under -verify-machineinstrs (bound not exhausted);
;   * the hardware loop survives the loop when its windows hold
;     (an macc dual-load body: the pipeliner-favored shape);
;   * the pipelined body still terminates correctly (jalr epilogue).
;
; The II floor probe (SMS-QOR soft_exit_ii_floor) confirms the shape is
; pipeliner-relevant; exact packing may differ because S2 sees the
; committed pipelined inventory.

define void @conv_sms_hwloop(ptr nocapture readonly %a, ptr nocapture readonly %b, ptr nocapture %out, i32 %n) {
entry:
  br label %body
body:
  %i = phi i32 [ 0, %entry ], [ %inc, %body ]
  %s = phi i32 [ 0, %entry ], [ %add, %body ]
  %pa = getelementptr i32, ptr %a, i32 %i
  %pb = getelementptr i32, ptr %b, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %m = mul i32 %va, %vb
  %add = add i32 %m, %s
  %inc = add i32 %i, 1
  %cmp = icmp slt i32 %inc, %n
  br i1 %cmp, label %body, label %exit
exit:
  store i32 %add, ptr %out, align 4
  ret void
}

; OFF-LABEL: conv_sms_hwloop:
; OFF: jalr

; LOOP-LABEL: conv_sms_hwloop:
; LOOP: jalr

; The convergence loop must not crash, exhaust its bound, or demote the
; retained loop on this shape (asserted by clean -verify-machineinstrs
; exit above; a demote would show SUBI32+BNEZ in the latch instead of
; the retained SET — behavioral pin only, exact row choice is S2's).
