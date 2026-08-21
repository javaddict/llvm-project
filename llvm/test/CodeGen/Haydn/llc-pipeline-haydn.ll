; RUN: llc -global-isel-abort=1 -O0 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' | FileCheck -match-full-lines -strict-whitespace -check-prefixes=O0,O0123 %s
; RUN: llc -global-isel-abort=1 -O1 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' | FileCheck -match-full-lines -strict-whitespace -check-prefixes=O1,O123,O0123 %s
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' | FileCheck -match-full-lines -strict-whitespace -check-prefixes=O23,O123,O0123 %s
; RUN: llc -global-isel-abort=1 -O3 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' | FileCheck -match-full-lines -strict-whitespace -check-prefixes=O23,O123,O0123 %s
; RUN: llc -global-isel-abort=1 -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure \
; RUN:     -haydn-enable-hwloops < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=HWON
; RUN: FileCheck %s --input-file=%S/Inputs/SOURCE-AUTHORITY-ANCHORS.txt --check-prefix=PIPE20
; RUN: FileCheck %s --input-file=%S/Inputs/SOURCE-AUTHORITY-ANCHORS.txt --check-prefix=DG0
; RUN: FileCheck %s --input-file=%S/Inputs/SOURCE-AUTHORITY-ANCHORS.txt --check-prefix=M10
; RUN: FileCheck %s --input-file=%S/Inputs/FAULT-INJECTION-SEATS.txt --check-prefix=FAULT
; RUN: FileCheck %s --input-file=%S/Inputs/CORRUPTION-MATRIX.txt --check-prefix=CORR
; RUN: FileCheck %s --input-file=%S/../../../../lldb/source/Plugins/ABI/Haydn/ABISysV_haydn.h --check-prefix=ABI
; RUN: %python %S/../../../utils/haydn/classify_lldb_step.py --self-test
; RUN: %python %S/../../../utils/haydn/classify_lldb_step.py --json 0x10000 0x10000 | FileCheck %s --check-prefix=SAMEPC
; RUN: %python %S/../../../utils/haydn/classify_lldb_step.py --json 0x10000 0x1000C | FileCheck %s --check-prefix=PARCEL
; RUN: %python %S/../../../utils/haydn/classify_lldb_step.py --return-reg 4 | FileCheck %s --check-prefix=RETR1
; RUN: %python %S/../../../utils/haydn/classify_lldb_step.py --return-reg 8 | FileCheck %s --check-prefix=RETD0
; RUN: %python %S/../../../utils/haydn/check_xfail_ledger.py --inventory-pin --llvm-src %S/../../../..
; RUN: %python %S/../../../utils/haydn/parse_lit_summary.py --self-test
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.h --check-prefix=HWDEF
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp --check-prefix=HWFLAG
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnPostRAMultiStage.h --check-prefix=SMSDEF
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnSubtarget.h --check-prefix=O0POST
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnEnsureTerminators.cpp --check-prefix=ENSURE
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnInstrInfo.cpp --check-prefix=W49
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnInstrInfo.cpp --check-prefix=BRBUF
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnExpandPseudos.cpp --check-prefix=SETDESC
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnInstrInfoManual.td --check-prefix=AUTO
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/CMakeLists.txt --check-prefix=CMAKE
; RUN: FileCheck %s --input-file=%S/Inputs/SOURCE-AUTHORITY-ANCHORS.txt --check-prefix=P13PLAN
; RUN: FileCheck %s --input-file=%S/Inputs/SOURCE-AUTHORITY-ANCHORS.txt --check-prefix=MF0
; RUN: FileCheck %s --input-file=%S/Inputs/SOURCE-AUTHORITY-ANCHORS.txt --check-prefix=W51
; RUN: %python -c "import os,sys; p=sys.argv[1]; assert os.path.islink(p), p+' must remain a symlink to haydn-plans/llvm-claude.md, not a 56K duplicate'" %S/../../../../CLAUDE.md
; RUN: FileCheck %s --input-file=%S/../../../../CLAUDE.md --check-prefix=ISANEXT
; REQUIRES: asserts

; Role: semantic — Haydn codegen pipeline oracle (AIE2-style full-line strict match on the load-bearing custom sequence).

; Haydn codegen pipeline oracle (AIE2-style full-line strict match on the
; load-bearing custom sequence). Locks dual-sched + AIE2 pack order:
;
; EnsureTerminators in addPostRegAlloc (BEFORE PEI) so invented
; RET receives epilogue emission. MBP +
; HardwareLoops + ExpandPseudos live in addPreSched2 (AFTER PEI).
;
; Opt0 - EnsureTerminators (pre-PEI) -> PEI ->
; ExpandPseudos -> PostMachineScheduler -> LatencyStalls ->
; Finalize/Verify -> BranchRelaxation -> late Finalize/Verify
; (no MBP / HardwareLoops / FixupHwLoops / DeadMI / MCP)
;
; Opt1+ - EnsureTerminators (pre-PEI) -> PEI -> late-opt MCP(UseCopyInstr)
; -> DeadMI -> MBP BEFORE HardwareLoops ->
; ExpandPseudos -> PostMachineScheduler -> LatencyStalls -> Finalize/Verify
; PreEmit - BranchRelaxation then late Finalize/Verify (hwloops default
; OFF: no Fixup). AIE PreEmit is empty (no BR).
;
; Opt2+ - MachinePipeliner -> DeadMIElim (pre-RA); PreRALoadPromote deleted
;
; addBlockPlacement is empty (no second MBP after pack).
; YOLO phase-out: LoadStoreOpt / CircularBuffer / RedundantCopyElim / Stage-0
; IB/PP / Role B / formMACs deleted from product pipeline. Form = GISel only.
; densify-defaults-off.ll locks Structure absence of deleted passes.
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
; EnsureTerminators is pre-PEI (addPostRegAlloc); ExpandPseudos is post-PEI
; (addPreSched2). Do not require NEXT across PEI/debug/analysis.
; O0:      Haydn Ensure Dead-End Terminators
; O0:      Haydn pseudo instruction expansion pass
; O0-NOT:      Haydn early post-increment pseudo expansion
; O0-NOT:      Haydn Condition Optimizer
; O0-NOT:      Haydn Copy Elimination
; O0-NOT:      Machine Copy Propagation Pass
; O0-NOT:      Remove dead machine instructions
; O0-NOT:      Branch Probability Basic Block Placement
; O0-NOT:      Haydn Hardware Loop Detection
; O0-NOT:      Haydn Bit Simplification
; O0-NOT:      Haydn PEI Peephole Optimizer
; O0-NOT:      Haydn Bundle Finalization
; O0:      PostRA Machine Instruction Scheduler
; LatencyStalls then first Finalize/Verify (one commit+verify lane;
; never skipFunction). postmisched may quality-skip optnone only.
; No Finalize/Verify before PostRA (no pre-RA bundle identity).
; O0-NEXT:      Haydn Exposed-Pipeline Latency Stalls
; O0-NEXT:      Haydn Bundle Finalization
; O0-NEXT:      Haydn Bundle Invariant Verifier
; O0-NOT:      Branch Probability Basic Block Placement
; O0:      Branch relaxation pass
; O0-NOT:      Haydn Hardware Loop Fixup
; Product default: late Finalize/Verify after BranchRelaxation
; (same Finalize/Verify; insertIndirectBranch LUI+ADDI32_W+JALR_W).
; O0-NEXT:      Haydn Bundle Finalization
; O0-NEXT:      Haydn Bundle Invariant Verifier
; Densify/quarantine absent at product defaults (W0.1):
; O0-NOT:      Haydn Load/Store Optimizer
; O0-NOT:      Haydn Circular Buffer Detection
; O0-NOT:      Haydn Redundant Copy Elimination
; O0-NOT:      Haydn Pre-RA Load-to-Slot1 Promotion
; O0-NOT:      Modulo Software Pipelining

; =============================================================================
; Opt1+ - IR HardwareLoops default-OFF; PostLegalizer + PostSelect; PreRA MIS; dual-sched pack
; =============================================================================
; O123-NOT:      Hardware Loop Insertion
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
; EnsureTerminators pre-PEI; late-opt MCP; DeadMI +
; MBP + hwloop post-PEI. Invented Cond/CopyElim/BitSimplify/PEIPeephole stay deleted.
; O123:      Haydn Ensure Dead-End Terminators
; O123:      Machine Copy Propagation Pass
; O123:      Remove dead machine instructions
; O123-NOT:      Haydn early post-increment pseudo expansion
; O123-NOT:      Haydn Condition Optimizer
; O123-NOT:      Haydn Copy Elimination
; O123:      Branch Probability Basic Block Placement
; O123-NOT:      Haydn Hardware Loop Detection
; O123:      Haydn pseudo instruction expansion pass
; O123-NOT:      Haydn Bit Simplification
; O123-NOT:      Haydn PEI Peephole Optimizer
; O123-NOT:      Haydn Bundle Finalization
; O123:      PostRA Machine Instruction Scheduler
; LatencyStalls then first Finalize/Verify (no-skip commit ownership).
; No Finalize/Verify before PostRA (no pre-RA bundle identity).
; O123-NEXT:      Haydn Exposed-Pipeline Latency Stalls
; O123-NEXT:      Haydn Bundle Finalization
; O123-NEXT:      Haydn Bundle Invariant Verifier
; Sole MBP (addBlockPlacement empty - no second placement after pack):
; O123-NOT:      Branch Probability Basic Block Placement
; PreEmit - BR then late Finalize/Verify at product default
; (Fixup + second BR still hwloops-ON only)
; O123:      Branch relaxation pass
; O123-NOT:      Haydn Hardware Loop Fixup
; O123-NEXT:      Haydn Bundle Finalization
; O123-NEXT:      Haydn Bundle Invariant Verifier
; Densify/quarantine absent at product defaults (W0.1):
; O123-NOT:      Haydn Load/Store Optimizer
; O123-NOT:      Haydn Circular Buffer Detection
; O123-NOT:      Haydn Redundant Copy Elimination
; O123-NOT:      Haydn Pre-RA Load-to-Slot1 Promotion
; IB/PP not separate Structure passes (PostRA-internal, default OFF). Role B is
; a residual flag on Hardware Loop Detection above — not a separate pass line.
; Flag locks: densify-defaults-off.ll.

; Forced-ON is evidence only: IR insertion + late Fixup/recommit appear;
; first commit+verify still sits after LatencyStalls. Not a product flip.
; Peer: AIE2TargetMachine.cpp:242-244 PostMachineScheduler then
; createAIEFinalizeBundle (AIE PreEmit empty at :88).
; HWON:      Hardware Loop Insertion
; HWON:      PostRA Machine Instruction Scheduler
; HWON-NEXT:      Haydn Exposed-Pipeline Latency Stalls
; HWON-NEXT:      Haydn Bundle Finalization
; HWON-NEXT:      Haydn Bundle Invariant Verifier
; HWON:      Haydn Hardware Loop Fixup
; HWON:      Branch relaxation pass
; HWON:      Haydn Bundle Finalization
; HWON-NEXT:      Haydn Bundle Invariant Verifier

; PIPE-20 / DG0 / R15 inventory (Inputs/ is lit-excluded; this file
; is the owner-slice seat). DecisionGuard registry stays absent.
; No second format pipeline.
; PIPE20-DAG: Phase-firewall inventory (PIPE-20
; PIPE20-DAG: no issue-cycle/format identity crosses RA
; PIPE20-DAG: no pre-RA BUNDLE / private member / setDesc
; PIPE20-DAG: inventory only
; PIPE20-DAG: T8-EVID
; PIPE20-DAG: T8-DEBUG-EVIDENCE
; PIPE20-DAG: 98890c529be9
; PIPE20-DAG: 28700d57
; PIPE20-DAG: not an ancestor
; PIPE20-DAG: G_ANYEXT
; PIPE20-DAG: adjustsStack
; PIPE20-DAG: MaxParcels
; PIPE20-DAG: Late Finalize/Verify after BR
; PIPE20-DAG: P19 CMake leftover closed
; DG0-DAG: DecisionGuard product registry remains absent
; DG0-DAG: no G-DECISION-GUARD revive
; DG0-DAG: product_coverage_pin
; DG0-NOT: DecisionGuardRegistry
; M10-DAG: parcel12 preferred
; M10-DAG: never qualified
; M10-DAG: ClassifyStepReport
; M10-DAG: kStepNeverQualified
; ABI: kFormatEParcelBytes = 12
; ABI: kStepNeverQualified = false
; ABI: SamePCResidual
; ABI: ClassifyStepDelta
; ABI: ClassifyStepReport
; ABI: ReturnRegNameForBytes
; SAMEPC: "class": "same-pc-residual"
; SAMEPC: "qualified": false
; SAMEPC: "semantic_qualify": false
; PARCEL: "class": "parcel12"
; PARCEL: "qualified": false
; PARCEL: "semantic_qualify": false
; RETR1: r1
; RETD0: d0
; FAULT-DAG: AR0 leftovers are inventory, not a product registry
; FAULT-DAG: DecisionGuard registry stays absent
; FAULT-DAG: check_xfail_ledger.py
; FAULT-DAG: product_coverage_pin
; CORR-DAG: AR0 leftovers are inventory, not a product registry
; CORR-DAG: DecisionGuard registry stays absent
; CORR-DAG: no host / no force-fail invent
; HWDEF: hardwareLoopsProductDefaultEnabled() { return false; }
; HWFLAG: "haydn-enable-hwloops"
; HWFLAG: cl::init(HaydnTargetMachine::hardwareLoopsProductDefaultEnabled())
; SMSDEF: productDefaultEnabled() { return false; }
; O0POST: enablePostRAMachineScheduler() const override { return true; }
; ENSURE: never call skipFunction
; W49: ensureSoftZeroR0Clean
; W49: withDR64PackBase: soft-zero R0 must be clean before pack-base MatInt
; BRBUF: BranchRelaxSafetyBufferBytes
; BRBUF-NOT: cl::init(200)
; SETDESC: leftover generic SET
; SETDESC: SET_HWLOOP_F2_W
; SETDESC: SET_HWLOOP_W
; AUTO: HaydnInstrInfoManual.td - moved
; AUTO: Former hand-maintained hypothesized encodings
; AUTO: generate_format_e_records.py does not emit this file
; AUTO: Do not include it
; AUTO-NOT: Auto-generated from spec JSON
; CMAKE-NOT: /ssd2/mhyang/haydn-plans/Database/golden
; CMAKE-NOT: $ENV{HOME}/haydn
; CMAKE-NOT: HAYDN_GOLDEN_DIR
; CMAKE-NOT: BUNDLESIM_GOLDEN_DIR
; P13PLAN-DAG: P13 source waves
; P13PLAN-DAG: R13 LANDED
; P13PLAN-DAG: Wave 1 unblocked
; P13PLAN-DAG: leftover `_S*` stay (T4)
; MF0-DAG: MF0 multi-bundle proof
; MF0-DAG: INERT
; MF0-DAG: no second product format family
; W51-DAG: W51 idle-parcel provenance
; W51-DAG: OPEN_BLOCKED
; W51-DAG: no invent
; R15 leftover: monorepo CLAUDE.md is a symlink; ISA-next is 64.
; ISANEXT: **ISA-63**
; ISANEXT: next new file is `ISA-64`
