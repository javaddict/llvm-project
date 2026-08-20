; RUN: FileCheck %s --input-file=%S/../../../../../lib/Target/Haydn/HaydnSchedule.td \
; RUN:   --check-prefix=MODEL
; RUN: FileCheck %s --input-file=%S/../../../../../lib/Target/Haydn/HaydnMachineScheduler.cpp \
; RUN:   --check-prefix=PIN
; RUN: FileCheck %s --input-file=%S/../../../../../lib/Target/Haydn/HaydnPostRAMultiStage.h \
; RUN:   --check-prefix=SMSDEF
; RUN: FileCheck %s --input-file=%S/../../../../../lib/Target/Haydn/HaydnTargetMachine.h \
; RUN:   --check-prefix=HWDEF
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify \
; RUN:   -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=PASSES
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify %s -o - \
; RUN:   | FileCheck %s
; REQUIRES: asserts
; REQUIRES: haydn-registered-target
;
; Role: IR — park in-order incomplete SchedMachineModel (MicroOpBufferSize=0,
; LoopMicroOpBufferSize=0, CompleteModel=0, IssueWidth=E3). Product
; multi-stage SMS and hardware loops stay OFF. AIE1 in-order peer
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

; PIN: pinHaydnInOrderIncompleteSchedModel
; PIN: SM.MicroOpBufferSize != 0
; PIN: SM.isComplete()
; PIN: SM.IssueWidth != Haydn::ISSUE_SLOT_COUNT
; PIN: pinHaydnInOrderIncompleteSchedModel(*C->MF)

; SMSDEF: productDefaultEnabled() { return false; }
; HWDEF: hardwareLoopsProductDefaultEnabled() { return false; }

; CHECK: add32
; CHECK-NOT: #<swps>
; CHECK-NOT: set_hwloop
; PASSES: PostRA Machine Instruction Scheduler
; PASSES-NOT: InterBlock
; PASSES-NOT: PostPipeliner
; PASSES-NOT: Hardware Loop Insertion
; PASSES-NOT: Haydn Hardware Loop Detection
; PASSES-NOT: Haydn Hardware Loop Fixup
