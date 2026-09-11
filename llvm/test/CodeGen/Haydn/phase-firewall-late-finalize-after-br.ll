; RUN: llc -global-isel-abort=1 -O0 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=COMMON,O0
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=COMMON,O2
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure \
; RUN:     -haydn-enable-hwloops < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=COMMON,HWON
; D1.60 Structure census: --implicit-check-not on the four names so a
; hidden extra Normalize/BR/Finalize/Verify fails. GR1.7 deleted the
; LateConvergence driver; there is no inner S2 Structure line.
; RUN: llc -global-isel-abort=1 -O0 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=D160O0 \
; RUN:     --implicit-check-not='Haydn Long-Branch Normalize' \
; RUN:     --implicit-check-not='Branch relaxation pass' \
; RUN:     --implicit-check-not='Haydn Bundle Finalization' \
; RUN:     --implicit-check-not='Haydn Bundle Invariant Verifier'
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure \
; RUN:     -haydn-enable-hwloops=0 < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=D160O0 \
; RUN:     --implicit-check-not='Haydn Long-Branch Normalize' \
; RUN:     --implicit-check-not='Branch relaxation pass' \
; RUN:     --implicit-check-not='Haydn Bundle Finalization' \
; RUN:     --implicit-check-not='Haydn Bundle Invariant Verifier'
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=D160O2 \
; RUN:     --implicit-check-not='Haydn Long-Branch Normalize' \
; RUN:     --implicit-check-not='Branch relaxation pass' \
; RUN:     --implicit-check-not='Haydn Bundle Finalization' \
; RUN:     --implicit-check-not='Haydn Bundle Invariant Verifier'
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure \
; RUN:     -haydn-enable-hwloops < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=D160O2 \
; RUN:     --implicit-check-not='Haydn Long-Branch Normalize' \
; RUN:     --implicit-check-not='Branch relaxation pass' \
; RUN:     --implicit-check-not='Haydn Bundle Finalization' \
; RUN:     --implicit-check-not='Haydn Bundle Invariant Verifier'
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp --check-prefix=TM
; REQUIRES: asserts
;
; Role: semantic — D1.60 late-seat census plus the first/mid/freeze
; Finalize+Verify firewall. S1 PostMachineScheduler stamps
; PostCommitCfgSnapshot; post-pack Normalize then wrap-only Finalize.
; Generic BR is Structure-only pre-S1. insertIndirectBranch emits real
; LUI+ADDI32_W+JALR_W only while unstamped. Hardware-loop product default
; is ON. GR1.7 deleted LateConvergence /. No format identity
; before PostRA.

define i32 @f(i32 %a, i32 %b) {
  %t = add i32 %a, %b
  ret i32 %t
}

; D1.60: hwloop product default is ON. Pre-stamp Normalize+BR sit before
; PostRA; post-pack Normalize then first Finalize; PreEmit is Normalize
; then mid Finalize+Verify; addPostBBSections is empty; freeze Verify
; at addPreEmitPass2.
;
; FileCheck interleaves prefix checks in FILE order, so the HWON mid-lane
; checks precede the shared COMMON tail below.

; O2 forms the IR hwloop (O0 keeps none; IR HardwareLoops inserts at O1+).
; O2: Hardware Loop Insertion
; O2-NOT: Haydn Hardware Loop Detection

; No Finalize/Verify before PostRA (phase firewall). Wave 4 B seats LBN
; before scheduler inventory; analyses (MDT/MLI/AA) sit between that LBN
; and PostRA. Post-pack LBN then wrap-only Finalize; no generic BR after S1.
; COMMON-NOT: Haydn Bundle Finalization
; COMMON: Haydn Long-Branch Normalize
; COMMON-NEXT: Branch relaxation pass
; COMMON: PostRA Machine Instruction Scheduler
; COMMON-NOT: Haydn Exposed-Pipeline Latency Stalls
; COMMON-NEXT:      Haydn Long-Branch Normalize
; COMMON-NEXT: Haydn Bundle Finalization
; COMMON-NEXT: Haydn Bundle Invariant Verifier

; O0 keeps no hwloop passes; O2 arms Fixup + Normalize in the late
; PreEmit lane (not the final commit).
; O0-NOT: Haydn Hardware Loop Fixup
; O0: Haydn Long-Branch Normalize
; O0-NEXT: Haydn Bundle Finalization
; O0-NEXT: Haydn Bundle Invariant Verifier
; O2: Haydn Long-Branch Normalize
; O2-NEXT: Haydn Bundle Finalization
; O2-NEXT: Haydn Bundle Invariant Verifier

; GR1.4: closeRetainedHwLoops is a library inside LBN, not a Structure pass.
; HWON-NOT: Haydn Hardware Loop Fixup
; HWON: Haydn Long-Branch Normalize
; HWON-NEXT: Haydn Bundle Finalization
; HWON-NEXT: Haydn Bundle Invariant Verifier

; GR1.7: empty addPostBBSections. Freeze Verify after CFIFixup.
; COMMON: Contiguously Lay Out Funclets
; COMMON: Machine Sanitizer Binary Metadata
; COMMON-NOT: Haydn Late Layout Convergence Loop
; COMMON-NOT: Haydn Machine Alignment
; COMMON-NOT: Haydn Bundle Finalization
; COMMON: Insert CFI remember/restore state instructions
; COMMON: Stack Frame Layout Analysis
; Freeze gate (addPreEmitPass2): terminal read-only verifier; only
; serialization follows.
; COMMON-NEXT: Haydn Bundle Invariant Verifier

; TM: GR2.9: keep the override as the empty read-only TPC default.
; TM: void HaydnPassConfig::addPostBBSections()
; TM-NOT: createHaydnLateConvergencePass
; TM-NOT: haydnSMS2Enabled
; TM: void HaydnPassConfig::addPreEmitPass2()
; TM: if (LimitedCodeGenPipeline)
; TM: addPass(createHaydnVerifyBundlesPass(/*IsFreezeSeat=*/true));

; D1.60 Structure seat census (counts + order). --implicit-check-not on
; the four names is on the RUN lines above.
;
; O0 / O2-hwloops=0: Normalize x3 (pre-sched + post-pack + PreEmit x1), BR x1,
; Finalize x2, Verify x3
; D160O0: Haydn Long-Branch Normalize
; D160O0-NEXT: Branch relaxation pass
; D160O0: PostRA Machine Instruction Scheduler
; D160O0-NOT: Haydn Exposed-Pipeline Latency Stalls
; D160O0-NEXT: Haydn Long-Branch Normalize
; D160O0-NEXT: Haydn Bundle Finalization
; D160O0-NEXT: Haydn Bundle Invariant Verifier
; D160O0: Haydn Long-Branch Normalize
; D160O0-NEXT: Haydn Bundle Finalization
; D160O0-NEXT: Haydn Bundle Invariant Verifier
; D160O0: Contiguously Lay Out Funclets
; D160O0-NOT: Haydn Late Layout Convergence Loop
; D160O0-NOT: Haydn Machine Alignment
; D160O0-NOT: Haydn Bundle Finalization
; D160O0: Insert CFI remember/restore state instructions
; D160O0: Stack Frame Layout Analysis
; D160O0-NEXT: Haydn Bundle Invariant Verifier
;
; O2 / hwloop-on: Normalize x3 (pre-sched + post-pack + PreEmit x1), BR x1,
; Finalize x2, Verify x3. Extra PreEmit LBN sandwich is deleted.
; D160O2: Haydn Long-Branch Normalize
; D160O2-NEXT: Branch relaxation pass
; D160O2: PostRA Machine Instruction Scheduler
; D160O2-NOT: Haydn Exposed-Pipeline Latency Stalls
; D160O2-NEXT: Haydn Long-Branch Normalize
; D160O2-NEXT: Haydn Bundle Finalization
; D160O2-NEXT: Haydn Bundle Invariant Verifier
; D160O2: Haydn Long-Branch Normalize
; D160O2-NOT: Haydn Hardware Loop Fixup
; D160O2-NEXT: Haydn Bundle Finalization
; D160O2-NEXT: Haydn Bundle Invariant Verifier
; D160O2: Contiguously Lay Out Funclets
; D160O2-NOT: Haydn Late Layout Convergence Loop
; D160O2-NOT: Haydn Machine Alignment
; D160O2-NOT: Haydn Bundle Finalization
; D160O2: Insert CFI remember/restore state instructions
; D160O2: Stack Frame Layout Analysis
; D160O2-NEXT: Haydn Bundle Invariant Verifier
