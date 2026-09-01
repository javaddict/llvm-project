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
; Role: IR — LatencyStalls sits in addPreSched2 after PostMachineScheduler,
; then the GR2.7 pre-commit normalization BranchRelaxation, then the first
; Finalize+Verify. Product default (hwloops ON since 2026-08-22) runs late
; Finalize/Verify after BranchRelaxation (same Finalize/Verify). Multi-stage
; SMS and Stage-0 IB/PP stay absent.

define i32 @p20_seat(i32 %a, i32 %b) {
  %t = add i32 %a, %b
  ret i32 %t
}

; 2026-08-22 hwloop product-default flip rebaseline: default is now ON.
; O2 forms IR hwloop; O0 keeps none (IR HardwareLoops inserts at O1+).
; O2: Hardware Loop Insertion
; O2-NOT: Haydn Hardware Loop Detection
; COMMON: PostRA Machine Instruction Scheduler
; COMMON-NEXT: Haydn Exposed-Pipeline Latency Stalls
; COMMON-NEXT:      Haydn Long-Branch Normalize
; COMMON-NEXT: Branch relaxation pass
; COMMON-NEXT: Haydn Bundle Finalization
; COMMON-NEXT: Haydn Bundle Invariant Verifier
; COMMON: Branch relaxation pass
; COMMON-NOT: InterBlock
; COMMON-NOT: PostPipeliner
; O0-NOT: Haydn Hardware Loop Fixup
; O0-NEXT: Haydn Bundle Finalization
; O2-NEXT: Haydn Hardware Loop Fixup
; HWON: Haydn Hardware Loop Fixup
; HWON: Haydn Bundle Finalization
; HWON: Haydn Bundle Invariant Verifier
