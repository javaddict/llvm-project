; RUN: llc -mtriple=haydn-unknown-elf -haydn-sms-containment-max=1 -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; G007 SEF peel corpus (SF10 completion, 2026-08-23; round-4 verdict).
; Class: SEF window-EMPTY pin — the honest fail-closed contract for the
; current i32-latency body space. This shape HAS a peel-eligible stage-0
; at its II=5/NS=3 schedule (all-SEF, live-in-rooted, prologue-valid):
; the G007 port fires the peel there ("peeled SEF stage, NStages=2
; (deferred...)") — and the schedule is STILL refused downstream, so the
; engine falls through to the plain II=7 NS=2 accept with NO sef-peel.
;
; WHY (three walls, verified on the pre-G007 binary at trip=3 where the
; old engine exercises the SAME II=5/NS=3 schedule with no peel in the
; picture): 13/14 lattice attempts die on scheduleOtherIterations
; (modulo infeasibility: second-copy Earliest > Insert at II=5), and the
; one survivor dies on certificateExactCommitPlan (format): the II=5
; modulo-cycle groups cannot coissue as legal E96 parcels. Identical
; distribution WITH the peel on the new binary — the peel changes none
; of the downstream geometry. Structural conclusion (owner:
; topics/scheduling): with Haydn's shallow i32 latencies (load/MAC
; dest [2]), NS=3 schedules only exist in the II-tight zone where
; modulo feasibility and format coissue both fail; the trip-only
; rescuable window — the ONLY thing peelSideEffectFree can rescue — is
; EMPTY for i32 bodies. AIE's live SEF window comes from its 4-6-cycle
; load latencies. i64 MAC bodies that would span stages at feasible IIs
; softexpand past the 96-instruction body cap. The port is
; correct-but-inert until deeper-latency (or sub-96-instr i64) shapes
; exist.
;
; REGRESSION TEST LAW (this fixture pins the fall-through, not an
; accept): (1) If the engine ever ACCEPTS this shape with sef-peel=1,
; the SEF window became non-empty (a deeper-latency itinerary landed or
; the format seat learned a coissue the II=5 groups allow) — re-derive
; the fixture then, do not delete it. (2) If the engine DECLINES this
; shape entirely (no II=7 accept), the peel's deferred restore
; (scheduleWithStrategy) regressed — the flag must never leak into the
; next II attempt. Pins: kind=accepted at the no-peel baseline, NO
; sef-peel on the accept line.
;
; RMK-NOT: sef-peel=1
; RMK: Schedule found II=[[II:[0-9]+]] NS=[[NS:[0-9]+]] prologue=[[P:[0-9]+]] parcels epilogue=[[E:[0-9]+]] parcels kind=accepted loop=bb.{{[0-9]+}}.loop
;
; ASM: sef:
; ASM: set_hwloop_f2
; ASM: jalr

target triple = "haydn-unknown-elf"

define i32 @sef(ptr readonly %p, i32 %q) {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.n, %loop]
  %s = phi i32 [1, %entry], [%s.n, %loop]
  %u1 = add i32 %q, %q
  %u2 = sub i32 %u1, %q
  %u3 = xor i32 %u2, %q
  %u4 = and i32 %u3, %q
  %u5 = or i32 %u4, %q
  %u6 = add i32 %u5, %q
  %u7 = sub i32 %u6, %q
  %u8 = xor i32 %u7, %q
  %v = load i32, ptr %p, align 4
  %w = add i32 %v, %u8
  %a = mul i32 %s, %w
  %s.n = add i32 %a, 1
  %i.n = add i32 %i, 1
  %cc = icmp ult i32 %i.n, 2
  br i1 %cc, label %loop, label %exit, !llvm.loop !0
exit:
  ret i32 %s.n
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 2}
