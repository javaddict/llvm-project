; RUN: llc -global-isel-abort=1 -O0 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=O0
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=O2
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure \
; RUN:     -haydn-enable-hwloops < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=HWON
; REQUIRES: asserts
;
; Role: semantic — LatencyStalls sits in addPreSched2 between
; PostMachineScheduler and the first Finalize. Product default (hwloops OFF)
; also runs late Finalize+Verify after BranchRelaxation (same
; Finalize/Verify; insertIndirectBranch LUI+ADDI32_W+JALR_W). Hardware-loop
; and multi-stage product defaults stay OFF.
;
; Peer: AIE2TargetMachine.cpp:242-244 is PostMachineScheduler then
; createAIEFinalizeBundle (AIE PreEmit empty). Haydn overlays the exposed-
; pipeline RAW net in that same seat so stall NOPs are committed there.

define i32 @f(i32 %a, i32 %b) {
  %t = add i32 %a, %b
  ret i32 %t
}

; O0-NOT:      Hardware Loop Insertion
; O0:      PostRA Machine Instruction Scheduler
; O0-NEXT:      Haydn Exposed-Pipeline Latency Stalls
; O0-NEXT:      Haydn Bundle Finalization
; O0-NEXT:      Haydn Bundle Invariant Verifier
; O0:      Branch relaxation pass
; O0-NOT:      Haydn Hardware Loop Fixup
; O0-NEXT:      Haydn Bundle Finalization
; O0-NEXT:      Haydn Bundle Invariant Verifier

; O2-NOT:      Hardware Loop Insertion
; O2-NOT:      Haydn Hardware Loop Detection
; O2:      PostRA Machine Instruction Scheduler
; O2-NEXT:      Haydn Exposed-Pipeline Latency Stalls
; O2-NEXT:      Haydn Bundle Finalization
; O2-NEXT:      Haydn Bundle Invariant Verifier
; O2:      Branch relaxation pass
; O2-NOT:      Haydn Hardware Loop Fixup
; O2-NEXT:      Haydn Bundle Finalization
; O2-NEXT:      Haydn Bundle Invariant Verifier

; Forced-ON is evidence only: IR insertion + late Fixup/recommit appear;
; first commit+verify still sits after LatencyStalls. Not a product flip.
; HWON:      Hardware Loop Insertion
; HWON:      PostRA Machine Instruction Scheduler
; HWON-NEXT:      Haydn Exposed-Pipeline Latency Stalls
; HWON-NEXT:      Haydn Bundle Finalization
; HWON-NEXT:      Haydn Bundle Invariant Verifier
; HWON:      Haydn Hardware Loop Fixup
; HWON:      Branch relaxation pass
; HWON:      Haydn Bundle Finalization
; HWON-NEXT:      Haydn Bundle Invariant Verifier
