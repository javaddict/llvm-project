; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -debug-only=haydn-hazard-rec < %s 2>&1 | FileCheck %s
; REQUIRES: asserts

; Role: semantic — Haydn scoreboard hazard recognizer (Stream B Phase B1,).

; REGRESSION TEST: Haydn scoreboard hazard recognizer (Stream B Phase B1,).
;
; This test exercises the post-RA MachineScheduler's scoreboard hazard
; recognizer (HaydnHazardRecognizer). The HR enforces:
; * slot exclusivity (one instr per slot per cycle)
; * the 3-issue VLIW cap
; * the GPR 4R2W register-file port budget.
;
; The function below has enough independent ALU ops that the scheduler must
; defer some of them to later cycles (the 4R2W budget caps a single bundle's
; GPR port demand). The -debug-only=haydn-hazard-rec output must contain at
; least one "Hazard for" line proving the HR is consulted during post-RA
; scheduling and is rejecting over-budget issues.
;
; This test requires assertions (-debug-only is only available in +asserts
; builds). If the HR ever stops being consulted (e.g. the
; CreateTargetMIHazardRecognizer override regresses to returning nullptr for
; post-RA), no "Hazard for" line appears and this test fails loudly.
;
; Background: before, createPostMachineScheduler returned nullptr and the
; post-RA scheduler was inert (no hazard recognition at all). installs
; HaydnPostRASchedStrategy + HaydnHazardRecognizer so the scheduler respects
; the VLIW resource model. This is also the path that finally sidesteps the
; UAF in ConvergingVLIWScheduler — see lesson.

define i32 @scoreboard_hazards(ptr %a, ptr %b) nounwind {
; The HaydnHazardRecognizer must fire at least once during post-RA scheduling
; of this function — its debug output contains a "Hazard for" line.
; CHECK: Hazard for
entry:
  %p0 = getelementptr i32, ptr %a, i32 0
  %p1 = getelementptr i32, ptr %a, i32 1
  %p2 = getelementptr i32, ptr %a, i32 2
  %p3 = getelementptr i32, ptr %a, i32 3
  %v0 = load i32, ptr %p0
  %v1 = load i32, ptr %p1
  %v2 = load i32, ptr %p2
  %v3 = load i32, ptr %p3
  ; Four independent adds — each consumes GPR read ports. The scheduler
  ; cannot issue all four in one cycle (4R2W budget on the GPR file plus
  ; 3-issue cap), so the HR must defer at least one to a later cycle.
  %s0 = add i32 %v0, %v1
  %s1 = add i32 %v1, %v2
  %s2 = add i32 %v2, %v3
  %s3 = add i32 %s0, %s1
  %s4 = add i32 %s2, %s3
  ret i32 %s4
}
