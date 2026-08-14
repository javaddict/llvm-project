; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -debug-only=pipeliner \
; RUN:     < %s -o %t.s 2>%t.dbg
; RUN: FileCheck %s --check-prefix=SWP < %t.dbg
; RUN: FileCheck %s --check-prefix=ASM < %t.s
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -stop-after=pipeliner -verify-machineinstrs < %s -o - \
; RUN:     | FileCheck %s --check-prefixes=AFTER-PIPE,NO-PRE-BUNDLE
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -stop-after=machine-scheduler -verify-machineinstrs < %s -o - \
; RUN:     | FileCheck %s --check-prefixes=AFTER-PRE,NO-PRE-BUNDLE
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -stop-before=postmisched -verify-machineinstrs < %s -o - \
; RUN:     | FileCheck %s --check-prefixes=PREPOST,NO-PRE-BUNDLE
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -stop-after=postmisched -verify-machineinstrs < %s -o - \
; RUN:     | FileCheck %s --check-prefix=POST
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -stats -o /dev/null < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=STATS
; REQUIRES: asserts
; Role: StageCount==1 soft SMS accept-path phase firewall.
; SWP-NOT: SMS-SHOULDUSE: accept multi-stage durable
; SWP-NOT: SMS-HANDOFF: materialize done groups={{[1-9][0-9]*}}
; SWP-NOT: Unable to analyzeLoop
; SWP: SMS-HANDOFF: metrics-only freeze
; ASM-LABEL: soft_countdown_sum:
; ASM-NOT: #<swps> stages={{[2-9]|[1-9][0-9]+}}
; ASM: {
; NO-PRE-BUNDLE-NOT: BUNDLE{{.*}}:: (load{{.*}}{
; NO-PRE-BUNDLE-NOT: BUNDLE_E96
; NO-PRE-BUNDLE-NOT: {{ADD32|ADDI32|MUL32|XOR32|OR32|LD32|ST32|SEQ32}}_S{{[0-9]}}
; NO-PRE-BUNDLE-NOT: {{ADD32|ADDI32|MUL32|XOR32|OR32|LD32|ST32|SEQ32}}_E2_
; NO-PRE-BUNDLE-NOT: {{ADD32|ADDI32|MUL32|XOR32|OR32|LD32|ST32|SEQ32}}_E3_
; NO-PRE-BUNDLE-NOT: BundleFormatRowID
; NO-PRE-BUNDLE-NOT: CompletionStateID
; NO-PRE-BUNDLE-NOT: internal BundleFormat
; AFTER-PIPE-LABEL: name: soft_countdown_sum
; AFTER-PIPE-DAG: ADDI32
; AFTER-PRE-LABEL: name: soft_countdown_sum
; PREPOST-LABEL: name: soft_countdown_sum
; POST-LABEL: name: soft_countdown_sum
; POST-NOT: BundleFormatRowID
; POST-NOT: CompletionStateID
; POST-DAG: ADDI32
; STATS-DAG: haydn-post-ra-sched{{.*}}architectural cycles reconstructed by post-RA leaveMBB
; STATS-DAG: haydn-post-ra-sched{{.*}}cleared transient alternate descriptors after setDesc
; STATS-DAG: haydn-prera-sched{{.*}}pre-RA regions phase-firewall-checked for BUNDLE invent
; STATS-NOT: haydn-prera-sched{{.*}}invented new BUNDLE roots
define i32 @soft_countdown_sum(i32 %x) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 32, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ %x, %entry ], [ %s4, %loop ]
  %s1 = mul i32 %s, 3
  %s2 = add i32 %s1, 5
  %s3 = mul i32 %s2, 7
  %s3b = add i32 %s3, 11
  %s3c = mul i32 %s3b, 13
  %s3d = add i32 %s3c, 17
  %s4 = xor i32 %s3d, %x
  %i.next = add i32 %i, -1
  %cond = icmp ne i32 %i.next, 0
  br i1 %cond, label %loop, label %exit
exit:
  ret i32 %s4
}
