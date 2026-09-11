; RUN: llc -global-isel-abort=1 -O0 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=O0
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=O2
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure \
; RUN:     -haydn-enable-hwloops < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=HWON
; REQUIRES: asserts
;
; Role: semantic — GR1.2 folds LatencyStalls into S1 PostMachineScheduler
; (packet+stamp). No sibling stall pass after PostRA. Post-pack
; Normalize then wrap-only Finalize. Generic BR is Structure-only pre-S1.
; LateConvergence still regenerates stalls as an inner pass (not a
; Structure line).
;
; Peer: AIE2TargetMachine.cpp:242-244 is PostMachineScheduler then
; createAIEFinalizeBundle (AIE PreEmit empty).

define i32 @f(i32 %a, i32 %b) {
  %t = add i32 %a, %b
  ret i32 %t
}

; 2026-08-22 hwloop product-default flip rebaseline: default is now ON.
; O0 keeps no hwloop passes (IR HardwareLoops inserts at O1+, AIE-faithful).
; O0-NOT:      Hardware Loop Insertion
; O0:      PostRA Machine Instruction Scheduler
; O0-NOT:      Haydn Exposed-Pipeline Latency Stalls
; O0-NEXT:      Haydn Long-Branch Normalize
; O0-NEXT:      Haydn Bundle Finalization
; O0-NEXT:      Haydn Bundle Invariant Verifier
; O0 late lane: LBN -> Finalize directly (no hwloop passes, no generic BR at O0).
; O0:      Haydn Long-Branch Normalize
; O0-NEXT:      Haydn Bundle Finalization
; O0-NEXT:      Haydn Bundle Invariant Verifier

; O2:      Hardware Loop Insertion
; O2-NOT:      Haydn Hardware Loop Detection
; O2:      PostRA Machine Instruction Scheduler
; O2-NOT:      Haydn Exposed-Pipeline Latency Stalls
; O2-NEXT:      Haydn Long-Branch Normalize
; O2-NEXT:      Haydn Bundle Finalization
; O2-NEXT:      Haydn Bundle Invariant Verifier
; O2:      Haydn Long-Branch Normalize
; O2-NOT:      Haydn Hardware Loop Fixup
; O2-NEXT:      Haydn Bundle Finalization
; O2-NEXT:      Haydn Bundle Invariant Verifier

; Forced-ON is evidence only: IR insertion + late Fixup/recommit appear;
; first commit+verify still sits after S1. Not a product flip.
; HWON:      Hardware Loop Insertion
; HWON:      PostRA Machine Instruction Scheduler
; HWON-NOT:      Haydn Exposed-Pipeline Latency Stalls
; HWON-NEXT:      Haydn Long-Branch Normalize
; HWON-NEXT:      Haydn Bundle Finalization
; HWON-NEXT:      Haydn Bundle Invariant Verifier
; HWON-NOT:      Haydn Hardware Loop Fixup
; HWON:      Haydn Bundle Finalization
; HWON-NEXT:      Haydn Bundle Invariant Verifier
