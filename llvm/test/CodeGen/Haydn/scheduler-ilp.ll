; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:   -haydn-premisched-matching-frontier=false \
; RUN:   -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -stop-before=greedy \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefix=PREGREEDY
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -stop-before=greedy \
; RUN:   -haydn-premisched-matching-frontier=false \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefix=PREGREEDY
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -stop-before=greedy \
; RUN:   -haydn-premisched-finer-rp-tracking=false \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefix=PREGREEDY
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -stats -o /dev/null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=STATS-PROD
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -haydn-premisched-matching-frontier=false \
; RUN:   -stats -o /dev/null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=STATS-BASE
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -haydn-premisched-finer-rp-tracking=false \
; RUN:   -stats -o /dev/null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=STATS-FINER-OFF
; REQUIRES: asserts

; Role: MIR — pre-RA ILP dual-run ranking residual attribution.

; Pre-RA: target HR + pressure-aware list schedule + matching-frontier
; tryCandidate ranking for independent work. Final asm keeps ILP-friendly
; shape; pre-greedy MIR is logical-only (no private member opcodes /
; FormatID stamp — phase identity through RA). Pressure remains primary.
; Soft-exit QoR peer: prera-format-qor-exit.ll (II floors + post-RA exact-pack
; correlation); format scoring must not invent BUNDLE or weaken pressure.
;
; Dual-run ranking residual attribution (plan §8.3 / §8.4 #11 ILP kernel):
;   * PROD  — product defaults (matching-frontier ON, finer RP ON)
;   * BASE  — -haydn-premisched-matching-frontier=false
;             (pressure/critical still primary; NodeOrder after pressure;
;              target HR always installed via CreateTargetMIHazardRecognizer)
;   * FINER — optional -haydn-premisched-finer-rp-tracking=false residual
; KPI freeze:
;   * small ILP kernel uses NodeOrder pre-RA (no ResourceDemand picks under architectural load latency)
;   * residual arms also have no ResourceDemand ranking
;   * post-RA multi-MI exact finalize present both sides (≥1)
;   * spill/reload/split/hard-root counters silent
;   * -stop-before=greedy: no BUNDLE, no _S* member setDesc either arm
; Generic dual-run peer: prera-format-generic-baseline.ll
; Critical peer: scheduler-critical-path.ll
; Unit: HaydnPortModelTest.PreRAIlpCriticalDualRunResidualAttribution

; --- dual-run -stats (ILP ranking residual attribution) ---
; STATS-PROD: haydn-post-ra-sched{{.*}}multi-MI cycles finalized as BUNDLE
; STATS-PROD: machine-scheduler{{.*}}NodeOrder heuristic pre-RA
; STATS-PROD-NOT: machine-scheduler{{.*}}ResourceDemand heuristic pre-RA
; STATS-PROD-NOT: failed exact no-split
; STATS-PROD-NOT: unstamped multi-member hard BUNDLE roots at post-RA
; STATS-PROD-NOT: {{[1-9][0-9]*}}{{ +}}regalloc{{.*}}Number of spills inserted
; STATS-PROD-NOT: {{[1-9][0-9]*}}{{ +}}regalloc{{.*}}Number of reloads inserted
;
; STATS-BASE: haydn-post-ra-sched{{.*}}multi-MI cycles finalized as BUNDLE
; STATS-BASE-NOT: machine-scheduler{{.*}}ResourceDemand heuristic pre-RA
; STATS-BASE-NOT: failed exact no-split
; STATS-BASE-NOT: unstamped multi-member hard BUNDLE roots at post-RA
; STATS-BASE-NOT: {{[1-9][0-9]*}}{{ +}}regalloc{{.*}}Number of spills inserted
; STATS-BASE-NOT: {{[1-9][0-9]*}}{{ +}}regalloc{{.*}}Number of reloads inserted
;
; STATS-FINER-OFF: haydn-post-ra-sched{{.*}}multi-MI cycles finalized as BUNDLE
; STATS-FINER-OFF-NOT: machine-scheduler{{.*}}ResourceDemand heuristic pre-RA
; STATS-FINER-OFF-NOT: failed exact no-split
; STATS-FINER-OFF-NOT: unstamped multi-member hard BUNDLE roots at post-RA
; STATS-FINER-OFF-NOT: {{[1-9][0-9]*}}{{ +}}regalloc{{.*}}Number of spills inserted
; STATS-FINER-OFF-NOT: {{[1-9][0-9]*}}{{ +}}regalloc{{.*}}Number of reloads inserted

; Test that the VLIW list scheduler produces valid code for independent
; arithmetic. IR constant-folds add-immediates (1+2+3 -> 6), so the body is
; a short dependent add chain plus one folded addi32; pin that shape rather
; than a pre-fold three-addi32 layout. Ranking residual must not invent
; pre-handoff BUNDLE or member setDesc.

define i32 @ilp_independent_ops(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: ilp_independent_ops:
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: {{addi32|add32}}
; CHECK-DAG: {{add32|addi32}}
; PREGREEDY-LABEL: name: ilp_independent_ops
; PREGREEDY-NOT: BUNDLE
; PREGREEDY: ADD32
  %r1 = add i32 %a, 1
  %r2 = add i32 %b, 2
  %r3 = add i32 %c, 3
  %r4 = add i32 %r1, %r2
  %result = add i32 %r4, %r3
  ret i32 %result
}

; Test that independent loads are scheduled close together for memory-level
; parallelism. The scheduler should prefer scheduling loads early to overlap
; their latency with subsequent computation. Port HR books multi-cycle under
; 2W regardless of matching-frontier ranking arm.
define i32 @ilp_independent_loads(ptr %p1, ptr %p2, ptr %p3) {
; CHECK-LABEL: ilp_independent_loads:
; CHECK: ld32
; CHECK: ld32
; CHECK: ld32
; PREGREEDY-LABEL: name: ilp_independent_loads
; PREGREEDY-NOT: BUNDLE
; PREGREEDY: LD32
  %v1 = load i32, ptr %p1
  %v2 = load i32, ptr %p2
  %v3 = load i32, ptr %p3
  %r1 = add i32 %v1, %v2
  %r2 = add i32 %r1, %v3
  ret i32 %r2
}
