; RUN: llc -global-isel-abort=1 -O0 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=COMMON,O0
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=COMMON,O2
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure \
; RUN:     -haydn-enable-hwloops < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=COMMON,HWON
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp --check-prefix=TM
; REQUIRES: asserts
;
; Role: semantic — late Finalize+Verify after BranchRelaxation at every
; opt level. insertIndirectBranch emits real LUI+ADDI32_W+JALR_W; those
; rejoin the same Finalize/Verify (AIEFinalizeBundle.cpp:40-59 is identity
; on already-bundled). Not a second commit implementation. Hardware-loop
; and multi-stage product defaults stay OFF. No format identity before
; PostRA.

define i32 @f(i32 %a, i32 %b) {
  %t = add i32 %a, %b
  ret i32 %t
}

; No Finalize/Verify before PostRA (phase firewall).
; COMMON-NOT: Haydn Bundle Finalization
; COMMON: PostRA Machine Instruction Scheduler
; COMMON-NEXT: Haydn Exposed-Pipeline Latency Stalls
; COMMON-NEXT: Haydn Bundle Finalization
; COMMON-NEXT: Haydn Bundle Invariant Verifier
; COMMON: Branch relaxation pass

; Product default: no Fixup; late Finalize+Verify immediately after BR.
; O0-NOT: Haydn Hardware Loop Fixup
; O0-NEXT: Haydn Bundle Finalization
; O0-NEXT: Haydn Bundle Invariant Verifier
; O2-NOT: Hardware Loop Insertion
; O2-NOT: Haydn Hardware Loop Detection
; O2-NOT: Haydn Hardware Loop Fixup
; O2-NEXT: Haydn Bundle Finalization
; O2-NEXT: Haydn Bundle Invariant Verifier

; Forced-ON still inserts Fixup + second BR before the same late lane.
; HWON: Haydn Hardware Loop Fixup
; HWON: Branch relaxation pass
; HWON-NEXT: Haydn Bundle Finalization
; HWON-NEXT: Haydn Bundle Invariant Verifier

; TM: Late Finalize+Verify after BR at every opt level (same Finalize/Verify).
