; REQUIRES: asserts
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-postra-interblock \
; RUN:     -haydn-postra-region-end-edges < %s \
; RUN:     | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-postra-interblock \
; RUN:     -haydn-postra-region-end-edges -haydn-sms2 < %s \
; RUN:     | FileCheck %s --check-prefix=ON
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-postra-interblock \
; RUN:     -haydn-postra-region-end-edges -haydn-sms2 \
; RUN:     -debug-only=haydn-post-ra-sched < %s -o /dev/null 2>&1 \
; RUN:     | FileCheck %s --check-prefix=REG
;
; W68.2R registry lifetime pin (STATUS limit #9): the inter-block DDG
; registry is per-MachineFunction state (HaydnMachineFunctionInfo), never a
; process-static raw-pointer store. Two multi-block functions in ONE llc
; run exercise the function transition: function A's recorded S1 depths
; must not leak into function B's effective-latency cut (stale keys point
; at A's MBBs). Fail mode of the old process-static registry: B's S1
; re-publish saw A's owner set and merged against dead pointers.
;
; Default-off (and inter-block without S2) packing is identical: both
; functions stay verify-clean. Opt-in S2 reopens each function's own
; provisional BUNDLEs independently.

define i32 @chain_a(ptr nocapture readonly %a, i32 %n) {
entry:
  %c = icmp sgt i32 %n, 0
  br i1 %c, label %mid, label %exit
mid:
  %x = load i32, ptr %a, align 4
  %y = add i32 %x, 1
  %z = mul i32 %y, 3
  br label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %z, %mid ]
  ret i32 %r
}

define i32 @chain_b(ptr nocapture readonly %b, i32 %m) {
entry:
  %c = icmp sgt i32 %m, 0
  br i1 %c, label %mid, label %exit
mid:
  %x = load i32, ptr %b, align 4
  %y = add i32 %x, 5
  %z = mul i32 %y, 7
  br label %exit
exit:
  %r = phi i32 [ 1, %entry ], [ %z, %mid ]
  ret i32 %r
}

; Default-off / inter-block-without-S2 packing: each function keeps its
; own immediates; no cross-function merge of schedule state.
; OFF-LABEL: chain_a:
; OFF: { nop; ld32 r1, r1, 0 }
; OFF: { nop; addi32 r2, r0, 3 }
; OFF: jalr
; OFF-LABEL: chain_b:
; OFF: { nop; ld32 r1, r1, 0 }
; OFF: { nop; addi32 r2, r0, 7 }
; OFF: { nop; addi32 r1, r1, 5 }
; OFF: jalr

; Opt-in second scheduler: both functions still terminate; packing may
; densify inside each function independently.
; ON-LABEL: chain_a:
; ON: jalr
; ON-LABEL: chain_b:
; ON: jalr

; Per-function reopen: the second scheduler reports a positive reopen
; count for chain_a, then a separate positive count for chain_b. A leaked
; process-static registry would crash or skip the second function's publish.
; REG: HaydnPostRASched {{S2}}: reopened {{[1-9][0-9]*}} provisional BUNDLE root(s) in chain_a
; REG: HaydnPostRASched {{S2}}: reopened {{[1-9][0-9]*}} provisional BUNDLE root(s) in chain_b
; REG-NOT: HaydnPostRASched {{S2}}: reopened {{.*}} in chain_a
