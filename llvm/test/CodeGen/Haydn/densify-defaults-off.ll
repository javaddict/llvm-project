; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' \
; RUN:   | FileCheck %s --check-prefix=PIPE
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure \
; RUN:     -haydn-enable-hwloops -haydn-enable-multistage-sms < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' \
; RUN:   | FileCheck %s --check-prefix=DUAL
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure \
; RUN:     -mattr=+hwloop < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' \
; RUN:   | FileCheck %s --check-prefix=ATTR
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure \
; RUN:     -haydn-enable-hwloops -haydn-enable-multistage-sms=false < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' \
; RUN:   | FileCheck %s --check-prefix=HWONLY
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' \
; RUN:   | FileCheck %s --check-prefix=SMSONLY
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.h --check-prefix=HWDEF
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp --check-prefix=HWASSERT
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp --check-prefix=HWFLAG
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnPostRAMultiStage.h --check-prefix=SMSDEF
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnPostRAMultiStage.cpp --check-prefix=SMSFLAG
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnFeatures.td --check-prefix=FEAT
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/CMakeLists.txt --check-prefix=CMAKE
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -verify-machineinstrs \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.off.rmk | FileCheck %s --check-prefix=OFFASM
; RUN: FileCheck %s --allow-empty --check-prefix=OFFRMK < %t.off.rmk
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -verify-machineinstrs \
; RUN:     -mattr=+hwloop < %s | FileCheck %s --check-prefix=ATTRASM
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -verify-machineinstrs \
; RUN:     -haydn-enable-hwloops -haydn-enable-multistage-sms=false < %s \
; RUN:   | FileCheck %s --check-prefix=HWASM
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -verify-machineinstrs \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.sms.rmk | FileCheck %s --check-prefix=SMSASM
; RUN: FileCheck %s --check-prefix=SMSRMK < %t.sms.rmk
; REQUIRES: asserts

; Role: semantic — product defaults keep hardware-loop formation and
; multi-stage SMS OFF. Densify / zombie passes stay deleted (not merely
; default-OFF). Dual-ON is flag-forced evidence only: Role-A insert/expand/
; fixup appear, deleted densify stays absent, multi-stage remains inside
; PostRA (no extra Structure pass, no product default flip). Late
; Finalize+Verify after BranchRelaxation is product default (same
; Finalize/Verify; not a second packer). FeatureHWLoop is ISA only.

; YOLO phase-out: densify / zombie passes deleted from pipeline (not merely
; default-OFF). Structure must never list Load/Store, CircularBuffer,
; RedundantCopyElim. Product post-inc form is ISel.
;
; Deleted densify (no flags): PostPipeliner, InterBlock, Role B, formMACs,
; LoadStoreOpt form/phase2.

; Flip sites stay constexpr false; cl::init follows the helpers.
; HWDEF: hardwareLoopsProductDefaultEnabled() { return false; }
; HWASSERT: static_assert(!HaydnTargetMachine::hardwareLoopsProductDefaultEnabled()
; HWFLAG: cl::init(HaydnTargetMachine::hardwareLoopsProductDefaultEnabled())
; SMSDEF: productDefaultEnabled() { return false; }
; SMSFLAG: cl::init(HaydnMultiStageSMS::productDefaultEnabled())
; FEAT: ISA capability only
; FEAT: hardwareLoopsProductDefaultEnabled
; CMAKE: regeneration is an explicit developer step
; CMAKE-NOT: ENV{HOME}
; CMAKE-NOT: HaydnFormatERecordsCheck

; PIPE-NOT:      Hardware Loop Insertion
; PIPE-NOT:      Haydn Load/Store Optimizer
; PIPE-NOT:      Haydn early post-increment pseudo expansion
; PIPE:      Haydn pseudo instruction expansion pass
; Product defaults: hardware loops and multi-stage SMS stay OFF.
; PIPE-NOT:      Haydn Hardware Loop Detection
; PIPE-NOT:      Haydn Hardware Loop Expansion
; PIPE-NOT:      Haydn Hardware Loop Fixup
; PIPE:      PostRA Machine Instruction Scheduler
; PIPE-NEXT:      Haydn Exposed-Pipeline Latency Stalls
; PIPE-NEXT:      Haydn Bundle Finalization
; PIPE-NEXT:      Haydn Bundle Invariant Verifier
; PIPE-NOT:      Haydn Circular Buffer Detection
; PIPE-NOT:      Haydn Redundant Copy Elimination
; PIPE:      Branch relaxation pass
; PIPE-NOT:      Haydn Hardware Loop Fixup
; PIPE-NEXT:      Haydn Bundle Finalization
; PIPE-NEXT:      Haydn Bundle Invariant Verifier

; Dual-ON force: IR insertion + MIR expand + late fixup appear; densify
; remains deleted; multi-stage is not a separate Structure pass.
; DUAL:      Hardware Loop Insertion
; DUAL:      Haydn Hardware Loop Expansion
; DUAL:      PostRA Machine Instruction Scheduler
; DUAL:      Haydn Hardware Loop Fixup
; DUAL:      Branch relaxation pass
; DUAL-NEXT:      Haydn Bundle Finalization
; DUAL-NEXT:      Haydn Bundle Invariant Verifier
; DUAL-NOT:      Haydn Load/Store Optimizer
; DUAL-NOT:      Haydn Circular Buffer Detection
; DUAL-NOT:      Haydn Redundant Copy Elimination
; DUAL-NOT:      Haydn PostPipeliner
; DUAL-NOT:      Haydn InterBlock

; +hwloop attr does not flip product policy.
; ATTR-NOT:      Hardware Loop Insertion
; ATTR-NOT:      Haydn Hardware Loop Detection
; ATTR-NOT:      Haydn Hardware Loop Expansion
; ATTR-NOT:      Haydn Hardware Loop Fixup
; ATTR:      PostRA Machine Instruction Scheduler
; ATTR:      Branch relaxation pass
; ATTR-NOT:      Haydn Hardware Loop Fixup
; ATTR-NEXT:      Haydn Bundle Finalization
;
; Independent force-ON: each CLI flag arms only its own path. AIE inserts
; HardwareLoops at O1+ (AIE2TargetMachine.cpp:81-82); Hexagon defaults ON
; via DisableHardwareLoops (HexagonTargetMachine.cpp:48-49). Haydn stays
; OFF until T3 SMS QUALIFY, T6 hwloop QUALIFY, combined matrix, then two
; separate policy-only patches. Dual-ON QUALIFY is not claimed here.
; HWONLY:      Hardware Loop Insertion
; HWONLY:      Haydn Hardware Loop Expansion
; HWONLY:      Haydn Hardware Loop Fixup
; HWONLY-NOT:      Haydn PostPipeliner
; HWONLY-NOT:      Haydn InterBlock
; SMSONLY-NOT:      Hardware Loop Insertion
; SMSONLY-NOT:      Haydn Hardware Loop Detection
; SMSONLY-NOT:      Haydn Hardware Loop Expansion
; SMSONLY-NOT:      Haydn Hardware Loop Fixup
; SMSONLY:      PostRA Machine Instruction Scheduler
; SMSONLY-NOT:      Haydn PostPipeliner
; SMSONLY-NOT:      Haydn InterBlock

; Product-default asm/remarks: generic FeatureHWLoop (HaydnGeneric.td:78-85)
; is ISA only. AIE inserts HardwareLoops at O1+ (AIE2TargetMachine.cpp:81-82)
; and EnableAIEHardwareLoops cl::init(true) (AIEBaseTargetTransformInfo.cpp:24-26).
; Hexagon defaults ON via DisableHardwareLoops (HexagonTargetMachine.cpp:47-48).
; Haydn stays OFF until independent QUALIFY then two policy-only patches.
; OFFASM-LABEL: sum_loop:
; OFFASM-NOT:   set_hwloop
; OFFASM-NOT:   #<swps>
; OFFASM:       jalr
; OFFRMK-NOT: accepted II=
; OFFRMK-NOT: MultiStageStageMBB
; OFFRMK-NOT: hwloop-combined=on
; OFFRMK-NOT: qualify-or-cut
; ATTRASM-LABEL: sum_loop:
; ATTRASM-NOT:   set_hwloop
; ATTRASM:       jalr
; HWASM-LABEL: sum_loop:
; HWASM:       set_hwloop
; HWASM-NOT:   #<swps> stages={{[2-9]|[1-9][0-9]+}}
; HWASM:       jalr
; SMSASM-LABEL: sum_loop:
; SMSASM-NOT:   set_hwloop
; SMSASM:       jalr
; SMSRMK: product-off
; SMSRMK: hwloop-combined=off
; SMSRMK-NOT: hwloop-combined=on

define i32 @sum_loop(ptr nocapture readonly %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [ 0, %pre ], [ %inext, %loop ]
  %s = phi i32 [ 0, %pre ], [ %s1, %loop ]
  %q = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %q, align 4
  %s1 = add i32 %s, %v
  %inext = add nuw nsw i32 %i, 1
  %cond = icmp eq i32 %inext, %n
  br i1 %cond, label %exit.loopexit, label %loop
exit.loopexit:
  br label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s1, %exit.loopexit ]
  ret i32 %r
}
