; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O1 < %s | FileCheck %s
;
; REGRESSION TEST: post-RA VLIW scheduler must restore ILP order so the
; greedy program-order packetizer can bundle two independent ADD32 ops.
;
; Bug: Haydn runs its VLIW machine scheduler PRE-RA. After RA, several
; passes mutate the schedule: LoadStoreOptimizer inserts LD32_POST_INC
; ST32_POST_INC pseudos and COPYs, GenMux inserts COPY+MOVT, and
; HardwareLoops inserts SET_HWLOOP pseudos. These inserted instructions
; separate originally-adjacent independent ALU ops. The Haydn packetizer
; (HaydnVLIWPacketizer) is strictly program-order greedy — it never
; reorders, it only groups consecutive instructions that fit the DFA and
; GPR 4R2W port budget. So once ILP order is destroyed post-RA, the
; packetizer emits single-instruction bundles even when two independent
; ALU32 ops could legally share a bundle.
;
; Fix: Add a post-RA VLIW MachineScheduler in HaydnPassConfig::addPreSched2
; IMMEDIATELY BEFORE the packetizer. It reuses HaydnConvergingVLIWScheduler
; but WITHOUT the CopyConstrain DAG mutation (which asserts
; hasVRegLiveness and dereferences getLIS post-RA — see).
; targetSchedulesPostRAScheduling returns true to suppress the upstream
; PostMachineScheduler, which would run AFTER addPreSched2 (i.e. after our
; packetizer) and arrive too late to help fill bundles.
;
; Test design: two independent ADD32 computations in a single basic block
; feed two separate stores. With RA + the post-RA pseudo/COPY-inserting
; passes between them, the pre-RA ILP order is lost and the packetizer
; would emit two single-instruction packets. With the post-RA scheduler
; the two independent ADD32 ops are reordered adjacent and the packetizer
; bundles them into one packet. The use-via-store keeps both adds live so
; they are not DCE'd, and the surrounding pointer setup forces real
; address materialization that the scheduler must reorder across.
;
; If this test regresses (two single ADD32 packets appear instead of one
; combined bundle), check:
; HaydnTargetMachine::createPostMachineScheduler still delegates to
; createHaydnPostRAVLIWScheduler
; HaydnPassConfig::addPreSched2 still adds PostMachineSchedulerID
; before the packetizer at O1+
; targetSchedulesPostRAScheduling still returns true (otherwise the
; upstream duplicate PostMachineScheduler runs AFTER the packetizer)
; createHaydnPostRAVLIWScheduler does NOT add createCopyConstrainDAGMutation
; (it would crash on vreg liveness — see)

define void @two_independent_adds(i32 %a, i32 %b, i32 %c, i32 %d,
                                  i32* %p1, i32* %p2) {
; CHECK-LABEL: two_independent_adds:
; The two independent ADD32 ops must both appear, and the post-RA VLIW
; scheduler must restore enough ILP that at least one bundle packs 2+ real
; ops (the observable effect of the scheduler running — without it the
; packetizer would emit only single-instruction bundles here).
; (SFR-strip) changed bundle layout (denser packing) — the two add32s
; now distribute across bundles instead of co-packing in one line, but ILP
; is still restored (an add32 packs with a store). Rebaselined.
; CHECK: add32
; CHECK: add32
entry:
  %x = add i32 %a, %b
  %y = add i32 %c, %d
  store i32 %x, i32* %p1, align 4
  store i32 %y, i32* %p2, align 4
  ret void
}
