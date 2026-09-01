; RUN: llc -global-isel-abort=1 -O0 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=COMMON,O0
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=COMMON,O2
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure \
; RUN:     -haydn-enable-hwloops < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=COMMON,HWON
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure \
; RUN:     -haydn-sms2 < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SMS2
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

; 2026-08-22 hwloop product-default flip rebaseline: default is now ON.
; Product default: Fixup + second BR before the mid Finalize+Verify lane;
; W68.2R adds the closure Finalize+Verify at addPostBBSections (after
; every common executable writer) and the terminal read-only freeze
; verifier at addPreEmitPass2.
;
; FileCheck interleaves prefix checks in FILE order, so the HWON mid-lane
; checks precede the shared COMMON tail below.

; O2 forms the IR hwloop (O0 keeps none; IR HardwareLoops inserts at O1+).
; O2: Hardware Loop Insertion
; O2-NOT: Haydn Hardware Loop Detection

; No Finalize/Verify before PostRA (phase firewall). The GR2.7 pre-commit
; normalization BR sits between LatencyStalls and the first Finalize.
; COMMON-NOT: Haydn Bundle Finalization
; COMMON: PostRA Machine Instruction Scheduler
; COMMON-NEXT: Haydn Exposed-Pipeline Latency Stalls
; COMMON-NEXT:      Haydn Long-Branch Normalize
; COMMON-NEXT: Branch relaxation pass
; COMMON-NEXT: Haydn Bundle Finalization
; COMMON-NEXT: Haydn Bundle Invariant Verifier

; O0 keeps no hwloop passes; O2 arms Fixup + second BR in the late lane.
; O0-NOT: Haydn Hardware Loop Fixup
; O0: Branch relaxation pass
; O0-NEXT: Haydn Bundle Finalization
; O0-NEXT: Haydn Bundle Invariant Verifier
; O2: Branch relaxation pass
; O2-NEXT: Haydn Hardware Loop Fixup
; O2-NEXT:      Haydn Long-Branch Normalize
; O2-NEXT: Branch relaxation pass
; O2-NEXT: Haydn Bundle Finalization
; O2-NEXT: Haydn Bundle Invariant Verifier

; Explicit-ON arm is now redundant with the default (kept as forced
; evidence). Mid lane: Fixup + BR then the mid Finalize/Verify pair.
; HWON: Haydn Hardware Loop Fixup
; HWON: Branch relaxation pass
; HWON-NEXT: Haydn Bundle Finalization
; HWON-NEXT: Haydn Bundle Invariant Verifier

; W68.2R closure seat: after the common-tail executable writers
; (FuncletLayout/FakeUses/StackMap/LiveDebug/sanitizer/outliner/
; BB-sections) and before the common CFIFixup + frame-layout analyses.
; COMMON: Contiguously Lay Out Funclets
; COMMON: Machine Sanitizer Binary Metadata
; COMMON: Haydn Bundle Finalization
; COMMON-NEXT: Haydn Bundle Invariant Verifier
; COMMON: Insert CFI remember/restore state instructions
; COMMON: Stack Frame Layout Analysis
; Freeze gate (addPreEmitPass2): terminal read-only verifier; only
; serialization follows.
; COMMON-NEXT: Haydn Bundle Invariant Verifier

; W68.2R: S2 chooses current physical MIs at addPostBBSections, after
; the common tail and immediately before closure Finalize. It must not
; sit in addPreEmitPass (between BR and FuncletLayout).
; SMS2: Branch relaxation pass
; SMS2-NOT: Haydn Late Layout Convergence Loop
; SMS2: Contiguously Lay Out Funclets
; SMS2: Machine Sanitizer Binary Metadata
; LateConvergence requires MDT/MLI; those analyses sit between the
; common tail and the S2 driver. S2 itself is immediately before
; closure Finalize.
; SMS2: Haydn Late Layout Convergence Loop
; SMS2-NEXT: Haydn Bundle Finalization
; SMS2-NEXT: Haydn Bundle Invariant Verifier
; SMS2: Insert CFI remember/restore state instructions
; SMS2: Stack Frame Layout Analysis
; SMS2-NEXT: Haydn Bundle Invariant Verifier

; TM: W68.2R late VLIW closure owner (contracts/pipeline.md "Required
; TM: if (haydnSMS2Enabled())
; TM-NEXT: addPass(createHaydnLateConvergencePass());
; TM-NEXT: if (TargetPassConfig::hasLimitedCodeGenPipeline())
; TM: W68.2R executable freeze gate: terminal read-only VerifyBundles after
