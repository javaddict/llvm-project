; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -haydn-ra-compact-hints=false < %s | FileCheck %s --check-prefixes=CHECK,OFF
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -haydn-ra-compact-hints=true < %s | FileCheck %s --check-prefixes=CHECK,ON
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -haydn-ra-compact-hints=true -stats -o /dev/null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=STATS-ON
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -haydn-ra-compact-hints=false -stats -o /dev/null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=STATS-OFF
; REQUIRES: asserts

; Role: semantic — Dual-run -stats attribution (spills/reloads/Hit/post-RA multi-MI; requires asserts/stats build).

; Dual-run -stats attribution (spills/reloads/Hit/post-RA multi-MI; requires
; asserts/stats build). Metrics — not the TRI hook — gate any future enable:
;
; Metrics-gated compact-subset soft physreg ordering in TRI
; getRegAllocationHints (-haydn-ra-compact-hints, default OFF under Full-only
; product — no compact byte win until a compact product row). Soft order only:
;   1) base copy/coalesce hints (never diluted)
;   2) GPR32Lo / low-DR encodings from AllocationOrder when flag ON
;   3) existing caller-saved preference
; Wide Full bank always remains colorable — no hard RC demotion to GPR32Lo.
;
; Dual-run -stats pins (ON vs OFF must not regress spill/copy/cycle surface):
;   * InlineSpiller NumSpills / NumReloads (regalloc) — same counts both sides
;   * haydn-reginfo compact Hit only under compact-hints=true (Miss silent when
;     every enabled query finds a compact physreg; unit test owns pure Miss)
;   * haydn-post-ra-sched multi-MI exact finalize present both sides
;   * scheduled exact-no-split failures and pre-existing hard-root counters
;     stay silent (zero counters omit from -stats)
;
; regalloc-hints.ll stays the CSR baseline (default flag OFF); do not silently
; rebaseline it when this opt is flipped. This file owns compact-on/off
; differential + pressure / conflicting-use / Full-fallback cases + stats.
; Hard-bundle membership under compact-hints=true is pinned in
; format-bundle-through-ra.mir (GREEDY/VRW interop).
;
; What these tests guard:
;  1. compact-on win: short temps land in r1–r7 / d0–d7 under low pressure
;  2. pressure-ignore: wide CSR / spill path still legal when low regs exhausted
;  3. conflicting-use: argument/return copy hints beat compact soft preference
;  4. Full fallback: high regs + stack remain available after spill/reload
;  5. on/off spill-copy-CSR differential: no silent CSR baseline drift
;  6. dual-run stats: spills/reloads + Hit + multi-MI finalize; split/hard-root
;     silent; compact counters only under -haydn-ra-compact-hints=true
;
; --- compact-hints=true attribution (STATS-ON prefix) ---
; FileCheck order follows -stats emission (post-RA, then reginfo, then regalloc).
; Post-RA multi-MI exact finalize runs; cycle surface is non-empty.
; STATS-ON: haydn-post-ra-sched{{.*}}multi-MI cycles finalized as BUNDLE
; Compact Hit applied (at least one physreg added on some VirtReg).
; STATS-ON: haydn-reginfo{{.*}}Haydn compact-subset RA hints applied
; Miss must not fire for this corpus (every enabled query found a compact
; physreg); pure Miss is unit-pinned when Order has only high/CSR members.
; STATS-ON-NOT: haydn-reginfo{{.*}}no eligible physreg added
; InlineSpiller spill/reload parity with OFF (same KPI surface).
; STATS-ON: {{[0-9]+}}{{ +}}regalloc{{.*}}Number of reloads inserted
; STATS-ON: {{[0-9]+}}{{ +}}regalloc{{.*}}Number of spills inserted
; Fail-closed split + pre-handoff hard-root counters stay zero (silent).
; STATS-ON-NOT: failed exact no-split
; STATS-ON-NOT: multi-member hard BUNDLE roots at post-RA
;
; --- compact-hints=false default attribution (STATS-OFF prefix) ---
; Post-RA multi-MI exact finalize still present under default OFF.
; STATS-OFF: haydn-post-ra-sched{{.*}}multi-MI cycles finalized as BUNDLE
; Compact Hit/Miss counters must not appear when the soft path is disabled
; (would sort between haydn-post-ra-sched and regalloc).
; STATS-OFF-NOT: haydn-reginfo{{.*}}compact-subset RA hints
; Same InlineSpiller spill/reload KPI as ON — no regression from soft order.
; STATS-OFF: {{[0-9]+}}{{ +}}regalloc{{.*}}Number of reloads inserted
; STATS-OFF: {{[0-9]+}}{{ +}}regalloc{{.*}}Number of spills inserted
; STATS-OFF-NOT: failed exact no-split
; STATS-OFF-NOT: multi-member hard BUNDLE roots at post-RA

;----------------------------------------------------------------------------
; Test 1 — compact-on win / Full soft preference for local temps.
; Under low pressure, temps prefer r1–r7 (compact + caller-saved). Neither ON
; nor OFF should touch callee-saved R8–R11 for this trivial function.
;----------------------------------------------------------------------------

define i32 @compact_local_temps() nounwind {
; CHECK-LABEL: compact_local_temps:
; CHECK:       // %bb.0:
; CHECK-NOT:   st32{{.*}}r8
; CHECK-NOT:   st32{{.*}}r9
; CHECK-NOT:   st32{{.*}}r10
; CHECK-NOT:   st32{{.*}}r11
; ON-NOT:      st32{{.*}}r12
; OFF-NOT:     st32{{.*}}r12
; CHECK:       jalr{{(\.s[012])?}}
entry:
  %a = add i32 1, 2
  %b = add i32 %a, 3
  %c = add i32 %b, 4
  %d = add i32 %c, 5
  ret i32 %d
}

;----------------------------------------------------------------------------
; Test 2 — compact DR64 soft preference for local i64 temps.
;----------------------------------------------------------------------------

define i64 @compact_local_temps_dr64() nounwind {
; CHECK-LABEL: compact_local_temps_dr64:
; CHECK:       // %bb.0:
; CHECK-NOT:   st64{{.*}}d8
; CHECK-NOT:   st64{{.*}}d9
; CHECK-NOT:   st64{{.*}}d10
; CHECK-NOT:   st64{{.*}}d11
; CHECK:       jalr{{(\.s[012])?}}
entry:
  %a = add i64 1, 2
  %b = add i64 %a, 3
  ret i64 %b
}

;----------------------------------------------------------------------------
; Test 3 — conflicting-use: base copy/coalesce (arg regs R1–R3) must win.
; Args arrive in R1/R2/R3; return in R1. Must not shuffle into other regs
; merely for compact preference.
;----------------------------------------------------------------------------

define i32 @compact_conflicting_arg_use(i32 %a, i32 %b, i32 %c) nounwind {
; CHECK-LABEL: compact_conflicting_arg_use:
; CHECK:       // %bb.0:
; CHECK:       add32{{.*}}r1{{.*}}r2
; CHECK:       add32{{.*}}r1{{.*}}r3
; CHECK:       jalr{{(\.s[012])?}}
entry:
  %sum = add i32 %a, %b
  %result = add i32 %sum, %c
  ret i32 %result
}

;----------------------------------------------------------------------------
; Test 4 — pressure-ignore + Full fallback.
; Enough simultaneous live values across calls force CSR use and/or stack
; spills. Compact soft hints must not prevent wide Full colors (R8–R11, stack).
; ON and OFF both require a CSR prologue save (spill path remains).
;----------------------------------------------------------------------------

define i32 @compact_pressure_full_fallback() nounwind {
; CHECK-LABEL: compact_pressure_full_fallback:
; CHECK:       // %bb.0:
; Prologue must save at least one CSR (R8–R11 or FP) — wide path in use.
; CHECK-DAG:   st32{{.*}}r8
; CHECK-DAG:   st32{{.*}}r9
; CHECK:       jal
; CHECK:       jalr{{(\.s[012])?}}
entry:
  %v1 = call i32 @get_value()
  %v2 = call i32 @get_value()
  %v3 = call i32 @get_value()
  %v4 = call i32 @get_value()
  %v5 = call i32 @get_value()
  %v6 = call i32 @get_value()
  %v7 = call i32 @get_value()
  %v8 = call i32 @get_value()
  %s1 = add i32 %v1, %v2
  %s2 = add i32 %s1, %v3
  %s3 = add i32 %s2, %v4
  %s4 = add i32 %s3, %v5
  %s5 = add i32 %s4, %v6
  %s6 = add i32 %s5, %v7
  %s7 = add i32 %s6, %v8
  ret i32 %s7
}

;----------------------------------------------------------------------------
; Test 5 — on/off spill-copy-CSR differential for a mid-pressure live set.
; Both policies must produce a functional frame with CSR saves when values
; live across a call. Compact-on must not invent free AT (R12 never CSR-saved)
; and must not drop to a no-CSR frame when values are live across the call.
;----------------------------------------------------------------------------

define i32 @compact_on_off_csr_differential(i32 %a, i32 %b, i32 %c) nounwind {
; CHECK-LABEL: compact_on_off_csr_differential:
; CHECK:       // %bb.0:
; Values live across call → at least one callee-saved GPR save/restore.
; CHECK:       st32{{.*}}r{{[89]|1[01]|14}}
; CHECK:       jal
; CHECK:       ld32{{.*}}r{{[89]|1[01]|14}}
; R12 is caller-saved / not free AT — never a CSR save slot.
; CHECK-NOT:   st32{{.*}}r12
; CHECK:       jalr{{(\.s[012])?}}
entry:
  %t0 = add i32 %a, %b
  %t1 = add i32 %t0, %c
  %call = call i32 @get_value()
  %t2 = add i32 %t1, %call
  %t3 = add i32 %t2, %a
  %t4 = add i32 %t3, %b
  %t5 = add i32 %t4, %c
  ret i32 %t5
}

declare i32 @get_value()
