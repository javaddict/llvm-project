; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp \
; RUN:   --check-prefix=TM
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnPostRAMultiStage.cpp \
; RUN:   --check-prefix=SMS
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnHardwareLoops.cpp \
; RUN:   --check-prefix=HWLOOP
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/GISel/HaydnInstructionSelector.cpp \
; RUN:   --check-prefix=ARSEL
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/GISel/HaydnPostLegalizerCombiner.cpp \
; RUN:   --check-prefix=GREEDY
; RUN: FileCheck %s --input-file=%S/../../../../clang/include/clang/Basic/BuiltinsHaydn.td \
; RUN:   --check-prefix=AE
; RUN: FileCheck %s --input-file=%S/../../../include/llvm/IR/IntrinsicsHaydn.td \
; RUN:   --check-prefix=INTRIN
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify \
; RUN:     -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' \
; RUN:   | FileCheck %s --check-prefix=PIPE
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf \
; RUN:     -mattr=+hwloop -verify-machineinstrs \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.off.rmk | FileCheck %s --check-prefix=OFF
; RUN: FileCheck %s --allow-empty --check-prefix=OFFRMK < %t.off.rmk
; REQUIRES: asserts

; Product park ledger, restamped for the 2026-08-22 flips: hardware
; loops AND post-RA multi-stage SMS are now product ON (each qualified
; independent + combined; SMS flipped same day, later). Combined
; dual-ON qualification LANDED (G004); Stage-0 PostPipeliner/InterBlock
; and AR encodings 2/3 stay off. Native X2CMUL stays selectable;
; unpublished AE bag stays off haydn.h. AIE inserts HardwareLoops at O1+
; (AIE2TargetMachine.cpp:81-82, :234-235); Haydn now matches that default.
; Finalize/Verify skipFunction stays closed.

; TM-DAG: PostPipeliner Stage-0, InterBlock
; TM-DAG: Combined hwloop+SMS stays productHwloopCombinedEnabled()
; TM-DAG: skipFunction — do not reopen that skip
; TM-DAG: cl::init(HaydnTargetMachine::hardwareLoopsProductDefaultEnabled())
; TM-DAG: setCFIFixup(true)
; TM-DAG: Do not reopen skipFunction on Finalize/Verify

; 2026-08-22 SMS product-default flip rebaseline: both static_asserts are
; now positive (default ON; combined pin still negative — the combined
; *policy* seat stays false while the qualified matrix is tracked in
; hwloop-multistage-combined-*).
; SMS-DAG: cl::init(HaydnMultiStageSMS::productDefaultEnabled())
; SMS-DAG: static_assert(HaydnMultiStageSMS::productDefaultEnabled()
; SMS-DAG: static_assert(!HaydnMultiStageSMS::productHwloopCombinedEnabled()

; HWLOOP-DAG: static_assert(llvm::HaydnTargetMachine::hardwareLoopsProductDefaultEnabled()
; HWLOOP-DAG: Never skipFunction here
; HWLOOP-DAG: +hwloop does not flip product policy

; ARSEL-DAG: static bool admitProductArSel(uint64_t ArSel) { return ArSel <= 1; }
; ARSEL-DAG: 2/3 stay unmapped
; ARSEL-DAG: does not invent AR2/AR3 identity
; ARSEL-DAG: ARRegClass is isAllocatable=0

; GREEDY-DAG: haydn-enable-gisel-greedy-addr
; GREEDY-DAG: cl::init(false)

; AE-DAG: Default PublicEnabled=0: not published on haydn.h (fail-closed)
; AE-DAG: Unpublished AE bag names stay off haydn.h
; AE-DAG: let PublicEnabled = 0;

; INTRIN-DAG: Native ISA X2CMUL stays selectable
; INTRIN-DAG: Public AE complex-mul wrappers stay
; INTRIN-DAG: selectors 2/3 fail-closed at product
; INTRIN-DAG: do not invent AR2/AR3

; PIPE:      Hardware Loop Insertion
; PIPE-NOT:      Haydn Hardware Loop Detection
; PIPE:      Haydn Hardware Loop Expansion
; PIPE:      PostRA Machine Instruction Scheduler
; PIPE:      Haydn Hardware Loop Fixup
; PIPE-NOT:      Haydn PostPipeliner
; PIPE-NOT:      Haydn InterBlock

; OFF-LABEL: add_loop:
; OFF:   set_hwloop
; OFF-NOT:   #<swps>
; OFF:       jalr
; Default arm (both engines ON): the combined-candidate tag is legal
; remark output; this body exhausts fail-closed — no accept/stamp.
; OFFRMK-NOT: accepted II=
; OFFRMK-NOT: MultiStageStageMBB
; OFFRMK-NOT: swps measured-II=

define i32 @add_loop(ptr nocapture readonly %a, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %for.body.preheader, label %exit
for.body.preheader:
  br label %for.body
exit.loopexit:
  %sum.lcssa = phi i32 [ %add, %for.body ]
  br label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %sum.lcssa, %exit.loopexit ]
  ret i32 %r
for.body:
  %i = phi i32 [ %i.next, %for.body ], [ 0, %for.body.preheader ]
  %sum = phi i32 [ %add, %for.body ], [ 0, %for.body.preheader ]
  %p = getelementptr inbounds i32, ptr %a, i32 %i
  %v = load i32, ptr %p, align 4
  %add = add i32 %sum, %v
  %i.next = add nuw nsw i32 %i, 1
  %cond = icmp eq i32 %i.next, %n
  br i1 %cond, label %exit.loopexit, label %for.body
}
