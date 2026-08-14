; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -debug-only=pipeliner < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -stop-after=pipeliner < %s -o - \
; RUN:     | FileCheck %s --check-prefix=PIPE
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 < %s \
; RUN:     | FileCheck %s --check-prefix=ASM
; REQUIRES: asserts

; Role: WP0 reduction — usad/ssad mutual-WAR class has no pre-RA SMS freeze.
; Residual usad-run/ssad-run MEMORY_FAULT at O2: multi-stage SMS hard BUNDLE
; pre-RA + RA mutual cyclic Anti broke loop exit. Option C: reject StageCount>1
; pre-RA; no multi-member SMS BUNDLE freeze.
;
; SWP: Schedule Found? 1
; SWP: SMS-SHOULDUSE: reject multi-stage stages={{[2-9]|[1-9][0-9]+}} II={{[0-9]+}} (pre-RA StageCount>1 containment; post-RA multi-stage only)
; SWP: Target rejected schedule
; SWP-NOT: SMS-SHOULDUSE: accept multi-stage durable
; SWP-NOT: SMS-HANDOFF: materialize done groups={{[1-9][0-9]*}}

; PIPE-LABEL: name: usad_row
; PIPE-DAG: SUB32
; PIPE-DAG: SEQ32
; PIPE-NOT: BUNDLE{{.*}}{
; PIPE-NOT: BUNDLE_E96
; PIPE-NOT: {{SUB32|SEQ32|ADD32|LD8}}_S{{[0-2]}}
; PIPE-NOT: BundleFormatRowID
; PIPE-NOT: CompletionStateID

; ASM-LABEL: usad_row:
; ASM-NOT: #<swps> stages={{[2-9]|[1-9][0-9]+}}
; ASM-DAG: {{(sub32|subi32)}}
; ASM-DAG: seq32
; ASM: jalr

define i32 @usad_row(ptr nocapture readonly %w, ptr nocapture readonly %x) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %tot = phi i32 [ 0, %entry ], [ %tot.next, %loop ]
  %pw = getelementptr inbounds i8, ptr %w, i32 %i
  %px = getelementptr inbounds i8, ptr %x, i32 %i
  %a = load i8, ptr %pw, align 1
  %b = load i8, ptr %px, align 1
  %za = zext i8 %a to i32
  %zb = zext i8 %b to i32
  %d = sub i32 %za, %zb
  %abs = call i32 @llvm.abs.i32(i32 %d, i1 false)
  %tot.next = add i32 %tot, %abs
  %i.next = add nuw nsw i32 %i, 1
  %c = icmp eq i32 %i.next, 16
  br i1 %c, label %exit, label %loop
exit:
  ret i32 %tot.next
}
declare i32 @llvm.abs.i32(i32, i1 immarg)
