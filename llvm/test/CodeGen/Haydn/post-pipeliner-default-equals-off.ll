; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.h \
; RUN:   --check-prefix=HWDEF
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnPostRAMultiStage.h \
; RUN:   --check-prefix=SMSDEF
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp \
; RUN:   --check-prefix=HWASSERT
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp \
; RUN:   --check-prefix=HWFLAG
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnPostRAMultiStage.cpp \
; RUN:   --check-prefix=SMSFLAG
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify \
; RUN:     -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' \
; RUN:   | FileCheck %s --check-prefix=PIPE
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf \
; RUN:     -mattr=+hwloop -verify-machineinstrs \
; RUN:     -haydn-enable-hwloops=0 \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.off.rmk | FileCheck %s --check-prefix=OFF
; RUN: FileCheck %s --allow-empty --check-prefix=OFFRMK < %t.off.rmk
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf \
; RUN:     -mattr=+hwloop -verify-machineinstrs \
; RUN:     -haydn-enable-hwloops -haydn-enable-multistage-sms=false \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.hwon.rmk | FileCheck %s --check-prefix=HWON
; RUN: FileCheck %s --allow-empty --check-prefix=HWONRMK < %t.hwon.rmk
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf \
; RUN:     -mattr=+hwloop -verify-machineinstrs \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.smson.rmk | FileCheck %s --check-prefix=SMSON
; RUN: FileCheck %s --allow-empty --check-prefix=SMSONRMK < %t.smson.rmk
; REQUIRES: asserts

; 2026-08-22 SMS product-default flip rebaseline (second flip of the day:
; hwloop earlier, SMS now): BOTH product defaults are ON (each qualified
; independent + combined). The file name keeps its historical spelling;
; the contract is now "default runs both engines, fail-closed per body" —
; hwloop ON forms SET, SMS ON runs qualify-or-cut and on this body exhausts
; without an accept/stage/swps stamp. Each engine stays independently
; flag-gated: hwloop=0 does not arm SMS accepts, SMS=0 does not suppress
; SET. Combined dual-ON QUALIFY lives in hwloop-multistage-combined-*.
; Stage-0 PostPipeliner stays deleted. Both constexpr flip sites are true;
; each cl::init follows its helper, not a bare literal.
; AIE inserts HardwareLoops at O1+ (AIE2TargetMachine.cpp:81-82); Hexagon
; defaults ON via DisableHardwareLoops (HexagonTargetMachine.cpp:48-49).

; HWDEF: hardwareLoopsProductDefaultEnabled() { return true; }
; SMSDEF: productDefaultEnabled() { return true; }
; HWASSERT: static_assert(HaydnTargetMachine::hardwareLoopsProductDefaultEnabled()
; HWFLAG: cl::init(HaydnTargetMachine::hardwareLoopsProductDefaultEnabled())
; SMSFLAG: cl::init(HaydnMultiStageSMS::productDefaultEnabled())

; PIPE:      Hardware Loop Insertion
; PIPE-NOT:      Haydn Hardware Loop Detection
; PIPE:      Haydn Hardware Loop Expansion
; PIPE:      PostRA Machine Instruction Scheduler
; PIPE:      Haydn Hardware Loop Fixup
; PIPE-NOT:      Haydn PostPipeliner
; PIPE-NOT:      Haydn InterBlock

; Explicit CLI off arms — each engine independently flag-gated.
; OFF (hwloop=0, SMS default ON): software residual; no SET; SMS runs
; fail-closed (qualify-or-cut remarks legal; no accept/stage/swps).
; OFF-LABEL: add_loop:
; OFF-NOT:   set_hwloop
; OFF-NOT:   #<swps>
; OFF:       jalr
; OFFRMK-NOT: accepted II=
; OFFRMK-NOT: MultiStageStageMBB
; OFFRMK-NOT: swps measured-II=
; OFFRMK-NOT: hwloop-combined=on
;
; HWON (hwloop ON, SMS explicitly OFF): SCEV-proven SET forms; SMS silent.
; HWON-LABEL: add_loop:
; HWON:       set_hwloop
; HWON-NOT:   #<swps> stages={{[2-9]|[1-9][0-9]+}}
; HWON:       jalr
; HWONRMK-NOT: accepted II=
; HWONRMK-NOT: MultiStageStageMBB
; HWONRMK-NOT: qualify-or-cut
; HWONRMK-NOT: hwloop-combined=on
;
; SMS CLI only: no SET. Do not claim parcels==II here.
; SMSON-LABEL: add_loop:
; SMSON-NOT:   set_hwloop
; SMSON:       jalr
; SMSONRMK-NOT: hwloop-combined=on

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
