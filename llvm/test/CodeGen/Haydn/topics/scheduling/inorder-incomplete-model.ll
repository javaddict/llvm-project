; RUN: FileCheck %s --input-file=%S/../../../../../lib/Target/Haydn/HaydnSchedule.td \
; RUN:   --check-prefix=MODEL
; RUN: FileCheck %s --input-file=%S/../../../../../lib/Target/Haydn/HaydnMachineScheduler.cpp \
; RUN:   --check-prefix=PIN
; RUN: FileCheck %s --input-file=%S/../../../../../lib/Target/Haydn/HaydnTargetMachine.h \
; RUN:   --check-prefix=HWDEF
; RUN: FileCheck %s --input-file=%S/../../../../../lib/Target/Haydn/HaydnHardwareLoops.cpp \
; RUN:   --check-prefix=HWPIN
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify \
; RUN:   -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=PASSES
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify %s -o - \
; RUN:   | FileCheck %s
; REQUIRES: asserts
; REQUIRES: haydn-registered-target
;
; Role: IR — park in-order incomplete SchedMachineModel (MicroOpBufferSize=0,
; LoopMicroOpBufferSize=0, CompleteModel=0, IssueWidth=E3). 2026-08-22
; product-default flips: hardware loops AND multi-stage SMS are ON
; (each qualified independent + combined). AIE1 in-order peer
; (aie1/AIE1Schedule.td:258); do not adopt AIE2PS buffer=1000 here.

define i32 @inorder_seat(i32 %a, i32 %b) {
  %t = add i32 %a, %b
  ret i32 %t
}

; MODEL-DAG: let IssueWidth = FormatEE3EntryCapacity
; MODEL-DAG: let MicroOpBufferSize = 0
; MODEL-DAG: let LoopMicroOpBufferSize = 0
; MODEL-DAG: let CompleteModel = 0
; MODEL-NOT: let CompleteModel = 1

; W68.1: the post-RA SMS host is deleted (generic pre-RA MachinePipeliner
; owns multi-stage); the hardware-loop product default assert remains.
; PIN: static_assert(HaydnTargetMachine::hardwareLoopsProductDefaultEnabled()
; PIN: pinHaydnInOrderIncompleteSchedModel
; PIN: SM.MicroOpBufferSize != 0
; PIN: SM.isComplete()
; PIN: SM.IssueWidth != Haydn::ISSUE_SLOT_COUNT
; PIN: pinHaydnInOrderIncompleteSchedModel(*C->MF)

; HWDEF: hardwareLoopsProductDefaultEnabled() { return true; }
; HWPIN: static_assert(llvm::HaydnTargetMachine::hardwareLoopsProductDefaultEnabled()

; CHECK: add32
; CHECK-NOT: #<swps>
; CHECK-NOT: set_hwloop
; PASSES: Hardware Loop Insertion
; PASSES-NOT: Haydn Hardware Loop Detection
; PASSES: PostRA Machine Instruction Scheduler
; PASSES-NOT: InterBlock
; PASSES-NOT: PostPipeliner
; PASSES-NOT: Haydn Hardware Loop Fixup
