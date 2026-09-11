; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 < %s | FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -haydn-enable-hwloops < %s | FileCheck %s --check-prefix=HWON

; Role: semantic — Full-only residual baseline for hardware-loop setup geometry
; under product Format E 12-byte parcels.
;
; Format E typed HWLoopOff reloc residual is closed. Product default is ON
; (2026-08-22). DEFAULT and HWON both arm set_hwloop_f2; D1.88 counter-FI
; grows the frame 8→16.
;
; HWON setup floor (cycle-primary):
;   * set_hwloop_f2 emitted (Role A / expand)
;   * InterveningCycles=2 size-bearing parcels after SET before body label
;   * Inclusive END (END labels last body cycle)
;   * Fixed zero spill-kpi on this kernel
;   * Soft back-edge gone once formed
;
; No golden invent of idle/pad completion values.

define i32 @vf0_full_hwloop_baseline(i32 %n, ptr %p) {
; DEFAULT-LABEL: vf0_full_hwloop_baseline:
; DEFAULT:       // #<spill-kpi> @vf0_full_hwloop_baseline spills=0 spill-bytes=0 reloads=0 reload-bytes=0
; DEFAULT:       // %bb.0: // %entry
; DEFAULT:       xor32 r0, r0, r0
; DEFAULT:       subi32{{(_w)?}}{{.*}}sp, sp, 16
; DEFAULT:       .cfi_def_cfa_offset 16
; DEFAULT:       set_hwloop_f2
; DEFAULT:       jalr
;
; HWON-LABEL: vf0_full_hwloop_baseline:
; HWON:       // #<spill-kpi> @vf0_full_hwloop_baseline spills=0 spill-bytes=0 reloads=0 reload-bytes=0
; HWON:       // %bb.0: // %entry
; HWON:       xor32 r0, r0, r0
; HWON:       subi32{{(_w)?}}{{.*}}sp, sp, 16
; HWON:       .cfi_def_cfa_offset 16
; HWON:       // %bb.1: // %loop.preheader
; HWON:       set_hwloop_f2
; Two size-bearing parcels after SET (InterveningCycles=2).
; HWON-NEXT:  {
; HWON-NEXT:  {
; HWON:       LLhwloop_start
; Inclusive END remains product law (END labels last body cycle).
; HWON:       LLhwloop_end
; Soft back-edge must not remain for a formed ZOL.
; HWON-NOT:   bnez_w
; HWON:       jalr
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %ge, align 4
  %acc.next = add i32 %acc, %v
  %i.next = add i32 %i, 1
  %cont = icmp slt i32 %i.next, %n
  br i1 %cont, label %loop, label %exit

exit:
  %r = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  ret i32 %r
}
