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

; Role: MIR — independent chains packable; critical dual-run residual attribution.

; REBASELINED : IR folds a+1+2+3 and b+10+20 into (a+b)+36; scheduler keeps
; independent chains packable. Pin folded immediate + add32 presence.
;
; Pressure/critical-path precedence remains primary under target pre-RA
; HR + matching-frontier tryCandidate (format scoring is below pressure);
; -stop-before=greedy proves logical opcodes only (no member setDesc).
; Soft-exit QoR peer: prera-format-qor-exit.ll — matching-frontier must not
; invent BUNDLE or override critical-path / pressure primary ranking.
;
; Dual-run ranking residual attribution (plan §8.3 / §8.4 #11 critical kernel):
;   * PROD  — product defaults (matching-frontier ON, finer RP ON)
;   * BASE  — -haydn-premisched-matching-frontier=false
;             (pressure/critical still primary — RegMax may fire; NodeOrder
;              after pressure; target HR always installed)
;   * FINER — optional -haydn-premisched-finer-rp-tracking=false residual
; KPI freeze:
;   * product arm fires ResourceDemand (matching-frontier) pre-RA heuristic
;   * residual arms have no ResourceDemand ranking
;   * pressure RegMax primary still present under product and residual arms
;   * post-RA multi-MI exact finalize present both sides
;   * spill/reload/split/hard-root counters silent
;   * -stop-before=greedy: no BUNDLE, no _S* either arm
; Generic dual-run peer: prera-format-generic-baseline.ll
; ILP peer: scheduler-ilp.ll
; Unit: HaydnPortModelTest.PreRAIlpCriticalDualRunResidualAttribution

; --- dual-run -stats (critical ranking residual attribution) ---
; STATS-PROD: machine-scheduler{{.*}}instructions scheduled by post-RA
; STATS-PROD: machine-scheduler{{.*}}instructions scheduled by pre-RA
; STATS-PROD-NOT: failed exact no-split
; STATS-PROD-NOT: unstamped multi-member hard BUNDLE roots at post-RA
; STATS-PROD-NOT: {{[1-9][0-9]*}}{{ +}}regalloc{{.*}}Number of spills inserted
; STATS-PROD-NOT: {{[1-9][0-9]*}}{{ +}}regalloc{{.*}}Number of reloads inserted
;
; STATS-BASE: machine-scheduler{{.*}}instructions scheduled by post-RA
; STATS-BASE: machine-scheduler{{.*}}instructions scheduled by pre-RA
; STATS-BASE-NOT: failed exact no-split
; STATS-BASE-NOT: unstamped multi-member hard BUNDLE roots at post-RA
; STATS-BASE-NOT: {{[1-9][0-9]*}}{{ +}}regalloc{{.*}}Number of spills inserted
; STATS-BASE-NOT: {{[1-9][0-9]*}}{{ +}}regalloc{{.*}}Number of reloads inserted
;
; STATS-FINER-OFF: machine-scheduler{{.*}}instructions scheduled by post-RA
; STATS-FINER-OFF: machine-scheduler{{.*}}instructions scheduled by pre-RA
; STATS-FINER-OFF-NOT: failed exact no-split
; STATS-FINER-OFF-NOT: unstamped multi-member hard BUNDLE roots at post-RA
; STATS-FINER-OFF-NOT: {{[1-9][0-9]*}}{{ +}}regalloc{{.*}}Number of spills inserted
; STATS-FINER-OFF-NOT: {{[1-9][0-9]*}}{{ +}}regalloc{{.*}}Number of reloads inserted

define i32 @critical_path_priority(i32 %a, i32 %b) {
; CHECK-LABEL: critical_path_priority:
; CHECK: .cfi_startproc
; CHECK: // %bb.0:
; CHECK-DAG: xor32{{(_w)?}}
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: addi32{{(_w)?}} {{.*}}, 36
; CHECK-DAG: {{add32|addi32}}
; CHECK: jalr{{(\.s[012])?}}
; CHECK: .Lfunc_end0:
; PREGREEDY-LABEL: name: critical_path_priority
; PREGREEDY-NOT: BUNDLE
; PREGREEDY: ADD32
; PREGREEDY-NOT: ADD32_S
; PREGREEDY-NOT: ADDI32_S
  %a1 = add i32 %a, 1
  %a2 = add i32 %a1, 2
  %a3 = add i32 %a2, 3

  %b1 = add i32 %b, 10
  %b2 = add i32 %b1, 20

  %result = add i32 %a3, %b2
  ret i32 %result
}

; Test that a long chain with memory operations gets scheduled so that
; loads are issued early. The load for the critical path should be
; prioritized over independent computation. Ranking residual must not
; invent pre-handoff BUNDLE or override pressure/critical primary ranking.
define i32 @critical_path_with_memory(ptr %p, i32 %x) {
; CHECK-LABEL: critical_path_with_memory:
; The load should appear in the function body.
; CHECK: ld32
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: {{add32|addi32}}
; PREGREEDY-LABEL: name: critical_path_with_memory
; PREGREEDY-NOT: BUNDLE
; PREGREEDY: LD32
; PREGREEDY-NOT: LD32_S
; PREGREEDY-NOT: ADD32_S
  %v = load i32, ptr %p
  %r1 = add i32 %v, 1
  %r2 = add i32 %r1, 2
  %r3 = add i32 %r2, %x
  ret i32 %r3
}
