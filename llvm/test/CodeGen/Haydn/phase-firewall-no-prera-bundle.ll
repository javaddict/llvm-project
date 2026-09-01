; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=-hwloop \
; RUN:     -O2 -enable-pipeliner=0 -stop-before=greedy -verify-machineinstrs \
; RUN:     < %s -o - | FileCheck %s --check-prefixes=PREGREEDY,COMMON
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=-hwloop \
; RUN:     -O2 -enable-pipeliner=0 -stop-before=postmisched -verify-machineinstrs \
; RUN:     < %s -o - | FileCheck %s --check-prefixes=PREPOST,COMMON
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=-hwloop \
; RUN:     -O2 -enable-pipeliner=0 -stop-after=postmisched -verify-machineinstrs \
; RUN:     < %s -o - | FileCheck %s --check-prefixes=POST,COMMON-POST
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=-hwloop \
; RUN:     -O2 -enable-pipeliner=0 -stats -o /dev/null < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=STATS
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=-hwloop \
; RUN:     -O2 -enable-pipeliner=0 -debug-only=haydn-post-ra-sched \
; RUN:     -o /dev/null < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=ADMIT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=-hwloop \
; RUN:     -O2 -enable-pipeliner=0 -debug-only=haydn-prera-sched \
; RUN:     -o /dev/null < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=PREADMIT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=-hwloop \
; RUN:     -O2 -enable-pipeliner=0 -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=ASM
; REQUIRES: asserts

; Role: structural phase-firewall for ordinary pre-RA + post-RA list-schedule.
; Pins: no pre-RA BUNDLE / private member / setDesc / placement identity;
; post-RA emitted-cycle audit; release-visible pre-RA invent counter;
; fail-closed per-op resource admission; product StageCount1 SMS containment
; pin; post-RA AltDesc clear counter. AR0 phase-firewall inventory closed
; at 98890c529be9: Inputs/SOURCE-AUTHORITY-ANCHORS.txt (not a product registry).
; COMMON-NOT: BUNDLE{{.*}}:: (load{{.*}}{
; COMMON-NOT: BUNDLE_E96
; COMMON-NOT: {{ADD32|ADDI32|OR32|XOR32|LD32|ST32}}_S{{[0-9]}}
; COMMON-NOT: {{ADD32|ADDI32|OR32|XOR32|LD32|ST32}}_E2_
; COMMON-NOT: {{ADD32|ADDI32|OR32|XOR32|LD32|ST32}}_E3_
; COMMON-NOT: BundleFormatRowID
; COMMON-NOT: CompletionStateID
; COMMON-NOT: internal BundleFormat
; PREGREEDY-LABEL: name: ordinary_ilp_three_add
; PREGREEDY-DAG: ADD32
; PREGREEDY-DAG: ADD32
; PREGREEDY-DAG: ADD32
; PREPOST-LABEL: name: ordinary_ilp_three_add
; PREPOST-DAG: $r{{[0-9]+}} = ADD32
; PREPOST-DAG: $r{{[0-9]+}} = ADD32
; PREPOST-DAG: $r{{[0-9]+}} = ADD32
; COMMON-POST-NOT: BundleFormatRowID
; COMMON-POST-NOT: CompletionStateID
; POST-LABEL: name: ordinary_ilp_three_add
; POST-DAG: ADD32
; POST-DAG: ADD32
; POST-DAG: ADD32
; STATS-DAG: haydn-post-ra-sched{{.*}}architectural cycles reconstructed by post-RA leaveMBB
; Sequential same-unit setDesc is not a multi-MI pack requirement; the
; firewall is absence of pre-RA identity plus the cycle/admission audit.
; STATS-DAG: haydn-post-ra-sched{{.*}}cleared transient alternate descriptors after setDesc
; STATS-DAG: haydn-post-ra-sched{{.*}}fail-closed per-op resource admission
; STATS-DAG: haydn-prera-sched{{.*}}pre-RA regions phase-firewall-checked for BUNDLE invent
; STATS-DAG: haydn-latency-stalls{{.*}}fail-closed per-op resource admission
; STATS-NOT: haydn-prera-sched{{.*}}invented new BUNDLE roots
; STATS-NOT: haydn-prera-sched{{.*}}invented bundled private members
; STATS-NOT: haydn-prera-sched{{.*}}invented private placement opcodes
; STATS-NOT: haydn-post-ra-sched{{.*}}residual alternate descriptor leaks
; ADMIT: HaydnPostRASched: resource-admission per_op_records=0 competitive_claims=0
; PREADMIT: HaydnPreRASched: product containment max_stages=3(soft+zol) resource_admission_closed=1
; ASM-LABEL: ordinary_ilp_three_add:
; ASM: {
; ASM: add32
; ASM: }
define void @ordinary_ilp_three_add(i32 %a, i32 %b, i32 %c, i32 %d,
                                    i32 %e, i32 %f,
                                    ptr %p1, ptr %p2, ptr %p3) {
entry:
  %x = add i32 %a, %b
  %y = add i32 %c, %d
  %z = add i32 %e, %f
  store i32 %x, ptr %p1, align 4
  store i32 %y, ptr %p2, align 4
  store i32 %z, ptr %p3, align 4
  ret void
}
