; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 \
; RUN:   -mattr=+hwloop -debug-only=haydn-tti < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s

; Role: semantic — TTI hwloop trip gate must test the golden 32-bit
; HWLR_COUNT law on the trip VALUE, not the SCEV type width (GOALS W42 /
; pass-design audit PA2-B2).

; REGRESSION TEST: the TTI hardware-loop trip gate mis-encoded the golden
; 32-bit HWLR_COUNT law as a TYPE-WIDTH test.
;
; Bug: HaydnTTIImpl::isHardwareLoopProfitable rejected a candidate when
;   SE.getUnsignedRangeMax(TripCountSCEV).getBitWidth() > 32
; i.e. when the SCEV *type* was wider than 32 bits — not when the trip
; *value* could exceed HWLR_COUNT's capacity. An i64-typed induction with a
; small trip (100 iterations, e.g. from `for (i64 i = 0; i < 100; ...)`)
; is perfectly encodable — 100 fits in 32-bit HWLR_COUNT regardless of the
; phi type — but was wrongly declined at TTI ("trip count > 32 bits" was
; printed for a value-range max of 100). The golden law is a VALUE bound:
; a trip whose unsigned max exceeds 0xFFFFFFFF can never arm HWLR_COUNT;
; an i64-typed trip of 100 can.
;
; Fix: gate on the unsigned range max VALUE:
;   SE.getUnsignedRangeMax(TripCountSCEV).ugt(0xFFFFFFFFULL)
; (HaydnTargetTransformInfo.cpp, GOALS W42).
;
; Test design: the debug stream has no per-function header lines, so the
; pins are (a) the OLD decline string "trip count > 32 bits" must NEVER
; appear anywhere — pre-fix it was printed by the FIRST function's query,
; before any accept line, so the leading CHECK-NOT fails on the pre-fix
; build; (b) the NEW value-law decline must appear exactly for the
; over-0xFFFFFFFF arm; (c) accepts must appear (the two i64-typed small
; trips + the i32 control). Query multiplicity varies (loops that go on to
; convert get a second TTI query), so only presence is pinned, not counts.
;
; Scope note: TTI acceptance does NOT guarantee end-to-end ZOL formation
; for the i64 arms: the upstream HardwareLoops candidate check
; (HardwareLoopInfo::isHardwareLoopCandidate in llvm/lib/Analysis/
; TargetTransformInfo.cpp) independently applies a SCEV-type-width >
; CountType filter that still drops i64-typed exit counts before intrinsic
; insertion. That filter lives in an upstream file (hard constraint #0) and
; is out of this fix's scope; this test pins the HAYDN gate decision, which
; is what W42 owns. If the upstream filter is ever relaxed, these loops
; must form ZOLs and the ACCEPT pins here keep guarding the Haydn side.

; Old type-width decline string: must never appear (regression pin).
; CHECK-NOT: Haydn HWLoop(IR): trip count > 32 bits

; i64-typed IV, trip 100 — value fits HWLR_COUNT; must be accepted.
define void @i64_small(ptr %p) {
; CHECK: Haydn HWLoop(IR): accepted innermost ZOL candidate
entry:
  br label %body

body:
  %i = phi i64 [ 0, %entry ], [ %next, %body ]
  store i32 0, ptr %p
  %next = add i64 %i, 1
  %cmp = icmp slt i64 %next, 100
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

; u32-max trip: value is exactly 0xFFFFFFFF (fits HWLR_COUNT); i64-typed IV.
; Must still be accepted by the value gate.
define void @i64_max_u32(ptr %p) {
; CHECK: Haydn HWLoop(IR): accepted innermost ZOL candidate
entry:
  br label %body

body:
  %i = phi i64 [ 0, %entry ], [ %next, %body ]
  store i32 0, ptr %p
  %next = add i64 %i, 1
  %cmp = icmp slt i64 %next, 4294967295
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

; Trip VALUE exceeds 0xFFFFFFFF (max = 0x100000000): the true golden
; HWLR_COUNT ceiling. Must be declined — by the value law, not type width.
define void @i64_over_u32(ptr %p) {
; CHECK: Haydn HWLoop(IR): trip count exceeds 32-bit HWLR_COUNT
entry:
  br label %body

body:
  %i = phi i64 [ 0, %entry ], [ %next, %body ]
  store i32 0, ptr %p
  %next = add i64 %i, 1
  %cmp = icmp slt i64 %next, 4294967296
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

; CONTROL (must stay accepted): i32-typed trip — the shape the old type-width
; gate happened to pass; guards against over-tightening the value gate.
define void @i32_small(ptr %p) {
; CHECK: Haydn HWLoop(IR): accepted innermost ZOL candidate
entry:
  br label %body

body:
  %i = phi i32 [ 0, %entry ], [ %next, %body ]
  store i32 0, ptr %p
  %next = add i32 %i, 1
  %cmp = icmp slt i32 %next, 100
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

; The old string must not appear after the decline either (whole-file ban).
; CHECK-NOT: Haydn HWLoop(IR): trip count > 32 bits
