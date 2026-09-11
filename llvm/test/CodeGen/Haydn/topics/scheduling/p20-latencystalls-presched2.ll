; RUN: llc -global-isel-abort=1 -O0 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=COMMON,O0
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=COMMON,O2
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -haydn-enable-hwloops -disable-verify \
; RUN:   -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=COMMON,HWON
; REQUIRES: asserts
; REQUIRES: haydn-registered-target
;
; Role: IR — GR1.2 folds LatencyStalls into S1 PostMachineScheduler.
; Post-pack Normalize then wrap-only Finalize+Verify. Generic BR is
; Structure-only pre-S1. Multi-stage SMS and Stage-0 IB/PP stay absent.

define i32 @p20_seat(i32 %a, i32 %b) {
  %t = add i32 %a, %b
  ret i32 %t
}

; 2026-08-22 hwloop product-default flip rebaseline: default is now ON.
; O2 forms IR hwloop; O0 keeps none (IR HardwareLoops inserts at O1+).
; O2: Hardware Loop Insertion
; O2-NOT: Haydn Hardware Loop Detection
; COMMON: PostRA Machine Instruction Scheduler
; COMMON-NOT: Haydn Exposed-Pipeline Latency Stalls
; COMMON-NEXT:      Haydn Long-Branch Normalize
; COMMON-NEXT: Haydn Bundle Finalization
; COMMON-NEXT: Haydn Bundle Invariant Verifier
; COMMON-NOT: InterBlock
; COMMON-NOT: PostPipeliner
; O0: Haydn Long-Branch Normalize
; O0-NOT: Haydn Hardware Loop Fixup
; O0-NEXT: Haydn Bundle Finalization
; O2: Haydn Long-Branch Normalize
; O2-NEXT: Haydn Bundle Finalization
; HWON-NOT: Haydn Hardware Loop Fixup
; HWON: Haydn Bundle Finalization
; HWON: Haydn Bundle Invariant Verifier
