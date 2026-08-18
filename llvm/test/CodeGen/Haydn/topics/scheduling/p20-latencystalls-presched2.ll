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
; Role: IR — LatencyStalls sits in addPreSched2 after PostMachineScheduler
; and before the first Finalize+Verify. Product default (hwloops OFF) still
; runs late Finalize/Verify after BranchRelaxation (same Finalize/Verify).
; Multi-stage SMS and Stage-0 IB/PP stay absent.

define i32 @p20_seat(i32 %a, i32 %b) {
  %t = add i32 %a, %b
  ret i32 %t
}

; COMMON: PostRA Machine Instruction Scheduler
; COMMON-NEXT: Haydn Exposed-Pipeline Latency Stalls
; COMMON-NEXT: Haydn Bundle Finalization
; COMMON-NEXT: Haydn Bundle Invariant Verifier
; COMMON: Branch relaxation pass
; COMMON-NOT: InterBlock
; COMMON-NOT: PostPipeliner
; O0-NOT: Haydn Hardware Loop Fixup
; O2-NOT: Hardware Loop Insertion
; O2-NOT: Haydn Hardware Loop Detection
; O2-NOT: Haydn Hardware Loop Fixup
; HWON: Haydn Hardware Loop Fixup
; HWON: Haydn Bundle Finalization
; HWON: Haydn Bundle Invariant Verifier
