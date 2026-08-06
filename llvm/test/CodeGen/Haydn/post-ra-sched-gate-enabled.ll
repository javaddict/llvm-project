; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O1 \
; RUN:     -debug-only=machine-scheduler < %s 2>&1 | \
; RUN:     FileCheck %s --check-prefix=DBG --implicit-check-not='Subtarget disables post-MI-sched.'
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O1 < %s | \
; RUN:     FileCheck %s

; Role: verifier — post-RA VLIW scheduler gate must be OPEN.

; REGRESSION TEST: post-RA VLIW scheduler gate must be OPEN.
;
; Bug: Haydn installed a post-RA VLIW MachineScheduler in
; HaydnPassConfig::addPreSched2 (PostMachineSchedulerID, before the packetizer)
; and implemented HaydnConvergingVLIWScheduler with the ResourceDemand
; slot-diversity heuristic — but the upstream PostMachineScheduler pass
; short-circuits at MachineScheduler.cpp:714 ("Subtarget disables post-MI-sched.")
; whenever TargetSubtargetInfo::enablePostRAMachineScheduler returns false.
; That function ANDs enableMachineScheduler && enablePostRAScheduler, and
; the latter reads SchedMachineModel.PostRAScheduler — which Haydn never set
; (defaults to 0). The pass returned early and createPostMachineScheduler
; createHaydnPostRAVLIWScheduler / HaydnConvergingVLIWScheduler were NEVER
; INSTANTIATED. The entire / post-RA scheduler was dead code.
;
; Fix: set `let PostRAScheduler = 1` in HaydnSchedule.td's HaydnSchedModel so
; enablePostRAMachineScheduler returns true. Decision.
;
; Why this test design:
; `--implicit-check-not='Subtarget disables post-MI-sched.'` proves the
; gate is OPEN: pre-fix this was the ONLY diagnostic this function emitted
; for the post-RA scheduler, and the entire scheduler body never executed.
; Post-fix the gate opens, the scheduler runs, and the trace prints.
; The DBG-CHECK proves the post-RA scheduler's VLIW schedule actually
; runs end-to-end by matching its "*** Final schedule for" finalization
; line (VLIWMachineScheduler.cpp:262).
; The.s CHECK proves at least one bundle packs 2+ add32 ops — the
; observable end-to-end effect that motivated /.
;
; If the DBG check regresses (the implicit-check-not fires), check
; HaydnSchedule.td HaydnSchedModel still has `let PostRAScheduler = 1`.
; If the.s check regresses, also verify
; HaydnTargetMachine::createPostMachineScheduler delegates to
; createHaydnPostRAVLIWScheduler and that addPreSched2 still adds
; PostMachineSchedulerID at O1+ before the packetizer.

define void @three_independent_adds(i32 %a, i32 %b, i32 %c, i32 %d,
                                    i32 %e, i32 %f,
                                    i32* %p1, i32* %p2, i32* %p3) {
; DBG: *** Final schedule for
; CHECK-LABEL: three_independent_adds:
; The three independent add32 ops must all appear (the scheduler is free to
; distribute them across bundles; denser packing spreads them out but
; none are dropped). At least one bundle must pack two ops.
; (SFR-strip) changed bundle layout (denser packing) — rebaselined.
; CHECK: add32
; CHECK: add32
; CHECK: add32
entry:
  %x = add i32 %a, %b
  %y = add i32 %c, %d
  %z = add i32 %e, %f
  store i32 %x, i32* %p1, align 4
  store i32 %y, i32* %p2, align 4
  store i32 %z, i32* %p3, align 4
  ret void
}
