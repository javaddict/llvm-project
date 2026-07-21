; RUN: llc -O0 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' | FileCheck -match-full-lines -strict-whitespace -check-prefixes=O0,O0123 %s
; RUN: llc -O1 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' | FileCheck -match-full-lines -strict-whitespace -check-prefixes=O1,O123,O0123 %s
; RUN: llc -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' | FileCheck -match-full-lines -strict-whitespace -check-prefixes=O23,O123,O0123 %s
; RUN: llc -O3 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' | FileCheck -match-full-lines -strict-whitespace -check-prefixes=O23,O123,O0123 %s
;
; REQUIRES: asserts
;
; Haydn codegen pipeline oracle (AIE2-style full-line strict match on the
; load-bearing custom sequence). Locks dual-sched + AIE2 pack order:
;
; EnsureTerminators in addPostRegAlloc (BEFORE PEI) so invented
; RET receives epilogue emission. ExpandPostIncEarly + peeps + MBP +
; HardwareLoops + ExpandPseudos live in addPreSched2 (AFTER PEI).
;
; Opt0 - EnsureTerminators (pre-PEI) -> PEI -> ExpandPostIncEarly ->
; ExpandPseudos -> PostMachineScheduler -> BranchRelaxation
; (no MBP / HardwareLoops / FixupHwLoops / profitability peeps)
;
; Opt1+ - EnsureTerminators (pre-PEI) -> PEI -> ExpandPostIncEarly ->
; CFG/Cond/Copy peeps -> MBP BEFORE HardwareLoops -> ExpandPseudos
; > BitSimplify/PEI peep -> PostMachineScheduler
; PreEmit - BranchRelaxation -> FixupHwLoops -> BranchRelaxation
;
; Opt2+ - MachinePipeliner -> DeadMIElim (pre-RA); PreRALoadPromote deleted
;
; addBlockPlacement is empty (no second MBP after pack).
; LoadStoreOpt form is opt-in (default OFF) - not a dual product path.
;
; Densify landmines (product default OFF — W0.1 / G-RISK-FLAGS):
;   -haydn-enable-interblock       (Stage-0 IB inside PostRA leaveMBB; not a pass)
;   -haydn-enable-post-pipeliner   (Stage-0 PP inside schedule; not a pass)
;   -haydn-hwloop-role-b           (same Haydn Hardware Loop Detection pass)
;   -haydn-enable-ldst-opt         (Haydn Load/Store Optimizer — locked NOT below)
;   -haydn-enable-circular-buffer  (Haydn Circular Buffer Detection — NOT below)
;   -haydn-enable-redundant-copy-elim
; IB/PP/Role-B flag state also locked by densify-defaults-off.ll (-debug-only).
; Style note - no space after CHECK-prefix colon (match-full-lines / AMDGPU).

define i32 @f(i32 %a, i32 %b) {
  %t = add i32 %a, %b
  ret i32 %t
}

; =============================================================================
; Shared scaffolding (all opt levels)
; =============================================================================
; O0123:Target Library Information
; O0123-NEXT:Runtime Library Function Analysis
; O0123-NEXT:Target Pass Configuration
; O0123-NEXT:Machine Module Information
; O0123-NEXT:Target Transform Information

; =============================================================================
; Opt0 - no IR HardwareLoops; no GISel post-select peep; no post-RA hwloop/MBP
; =============================================================================
; O0-NOT:      Hardware Loop Insertion
; O0-NOT:      HaydnPostLegalizerCombiner
; O0-NOT:      Haydn Post-Selection Optimizer

; O0:      HaydnPreLegalizerCombiner
; O0-NEXT:      Legalizer
; O0-NEXT:      RegBankSelect
; O0:      InstructionSelect
; O0-NOT:      Haydn Post-Selection Optimizer

; Opt0 custom post-RA / pre-emit (legal encode only):
; EnsureTerminators is pre-PEI (addPostRegAlloc); Expand* are post-PEI
; (addPreSched2). Do not require NEXT across PEI/debug/analysis.
; O0:      Haydn Ensure Dead-End Terminators
; O0:      Haydn early post-increment pseudo expansion
; O0-NEXT:      Haydn pseudo instruction expansion pass
; O0-NOT:      Haydn CFG Optimizer
; O0-NOT:      Haydn Condition Optimizer
; O0-NOT:      Haydn Copy Elimination
; O0-NOT:      Branch Probability Basic Block Placement
; O0-NOT:      Haydn Hardware Loop Detection
; O0-NOT:      Haydn Bit Simplification
; O0-NOT:      Haydn PEI Peephole Optimizer
; O0:      PostRA Machine Instruction Scheduler
; O0-NOT:      Branch Probability Basic Block Placement
; O0:      Branch relaxation pass
; O0-NOT:      Haydn Hardware Loop Fixup
; Densify/quarantine absent at product defaults (W0.1):
; O0-NOT:      Haydn Load/Store Optimizer
; O0-NOT:      Haydn Circular Buffer Detection
; O0-NOT:      Haydn Redundant Copy Elimination
; O0-NOT:      Haydn Pre-RA Load-to-Slot1 Promotion
; O0-NOT:      Modulo Software Pipelining

; =============================================================================
; Opt1+ - IR HardwareLoops; PostLegalizer + PostSelect; PreRA MIS; dual-sched pack
; =============================================================================
; O123:      Hardware Loop Insertion
; O123:      HaydnPreLegalizerCombiner
; O123:      Legalizer
; O123:      HaydnPostLegalizerCombiner
; O123:      InstructionSelect
; O123:      Haydn Post-Selection Optimizer

; Opt2+/Opt3 - SMS then DeadMI (AIE-faithful), before PreRA MIS.
; PreRALoadPromote deleted - must not appear in the product pipeline.
; O23:      Modulo Software Pipelining
; O23-NEXT:      Remove dead machine instructions
; O23-NOT:      Haydn Pre-RA Load-to-Slot1 Promotion

; Opt1 has no MachinePipeliner (Opt2+ only).
; O1-NOT:      Modulo Software Pipelining

; PreRA MachineScheduler (dual-sched Role A) at Opt1+; after SMS at Opt2+.
; O123:      Machine Instruction Scheduler

; Shared Opt1+/Opt2+/Opt3 custom sequence:
; EnsureTerminators pre-PEI; ExpandPostIncEarly+peeps+MBP+hwloop post-PEI.
; Analysis (MDT/MLI) may appear between CopyElim and MBP / after MBP.
; O123:      Haydn Ensure Dead-End Terminators
; O123:      Haydn early post-increment pseudo expansion
; O123-NEXT:      Haydn CFG Optimizer
; O123-NEXT:      Haydn Condition Optimizer
; O123-NEXT:      Haydn Copy Elimination
; O123:      Branch Probability Basic Block Placement
; O123:      Haydn Hardware Loop Detection
; O123-NEXT:      Haydn pseudo instruction expansion pass
; O123-NEXT:      Haydn Bit Simplification
; O123-NEXT:      Haydn PEI Peephole Optimizer
; O123:      PostRA Machine Instruction Scheduler
; Sole MBP (addBlockPlacement empty - no second placement after pack):
; O123-NOT:      Branch Probability Basic Block Placement
; PreEmit - BR / FixupHwLoops / BR
; O123:      Branch relaxation pass
; O123-NEXT:      Haydn Hardware Loop Fixup
; O123-NEXT:      Branch relaxation pass
; Densify/quarantine absent at product defaults (W0.1):
; O123-NOT:      Haydn Load/Store Optimizer
; O123-NOT:      Haydn Circular Buffer Detection
; O123-NOT:      Haydn Redundant Copy Elimination
; O123-NOT:      Haydn Pre-RA Load-to-Slot1 Promotion
; IB/PP not separate Structure passes (PostRA-internal, default OFF). Role B is
; a residual flag on Hardware Loop Detection above — not a separate pass line.
; Flag locks: densify-defaults-off.ll.
