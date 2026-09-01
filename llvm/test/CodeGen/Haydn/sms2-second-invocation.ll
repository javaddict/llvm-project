; REQUIRES: asserts
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-postra-interblock \
; RUN:     -haydn-postra-region-end-edges -haydn-sms2 < %s \
; RUN:     | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-postra-interblock \
; RUN:     -haydn-postra-region-end-edges -haydn-sms2 \
; RUN:     -debug-only=haydn-post-ra-sched,haydn-late-convergence < %s \
; RUN:     -o /dev/null 2>&1 | FileCheck %s --check-prefix=WHOLE
;
; W68.2R S2 pin: the SAME scheduler implementation is invoked a second
; time AFTER the late range/layout mutations, as a whole-function pass
; (contracts/pipeline.md "S1/S2 repair law" — running the whole function
; is conservative and legal; calling that "changed-BB-only repair" when
; no such filter exists is not). Flags stay default-off.
;
; This kernel's opt-in packing is identical to default-off, so both RUN
; arms share the OFF checks. The WHOLE arm proves S1 schedules every
; block, then HaydnLateConvergence runs S2 on the same function (again
; every remaining block) to a fixed point — not a changed-BB driver.

; OFF-LABEL: mb_chain2:
; OFF: { nop; addi32 r3, r0, 0 }
; OFF: { nop; bnez r2, .LBB0_2 }
; OFF: { ld32 r1, r1, 0; ld32 r2, r1, 0 }
; OFF: { nop; addi32 r3, r0, 3 }
; OFF: { nop; addi32 r2, r2, 1 }
; OFF: { nop; mull r3, r2, r3 }
; OFF: jalr

; S1 post-RA schedules the whole function (every MBB that materializes
; cycles). Late-convergence then invokes the same PostMachineScheduler
; on the whole function (not a changed-BB worklist) to a fixed point.
; WHOLE: HaydnPostRASched: emitted-cycle audit bb.{{[0-9]+}}
; WHOLE: HaydnLateConvergence: mb_chain2 bound={{[0-9]+}} (cond={{[0-9]+}} hwloop=0)
; WHOLE: HaydnPostRASched: emitted-cycle audit bb.{{[0-9]+}}
; WHOLE: HaydnPostRASched: emitted-cycle audit bb.{{[0-9]+}}
; WHOLE: HaydnLateConvergence: fixed point after {{[0-9]+}} iteration(s)
; WHOLE-NOT: changed-BB
; WHOLE-NOT: exhausted

define i32 @mb_chain2(ptr nocapture readonly %a, i32 %n) {
entry:
  %c = icmp sgt i32 %n, 0
  br i1 %c, label %first, label %exit
first:
  %x = load i32, ptr %a, align 4
  %y = add i32 %x, 1
  %z = mul i32 %y, 3
  br label %second
second:
  %w = load i32, ptr %a, align 4
  %v = add i32 %z, %w
  %u = add i32 %v, %y
  br label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %u, %second ]
  ret i32 %r
}
