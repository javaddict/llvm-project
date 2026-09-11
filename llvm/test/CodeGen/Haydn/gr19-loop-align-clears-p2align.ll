; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs %s -o - | FileCheck %s
; REQUIRES: haydn-registered-target
;
; GR1.9 / D1.166 / GR1.7: llvm.loop.align 16 is consumed before freeze
; by stamped LBN closer padInternalMBBAlignment (named
; haydn-machine-alignment pass class deleted). Generic
; emitBasicBlockStart must not emit .p2align 4 for the loop header.
; padInternalMBBAlignment raises MF alignment so W70.2r pre-label fill
; is .p2align 4 (absolute address). Hot-backedge object-address pin is
; d1166-loop-align-absolute.ll. Packet insertion when the header is
; off-grid is pinned by gr19-internal-mbb-align-packets.mir.
;
; CHECK: .p2align 4
; CHECK-LABEL: loop_align16:
; CHECK-NOT: .p2align 4
; CHECK-NOT: .p2align	4

define i32 @loop_align16(ptr nocapture readonly %p, i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %add, %loop ]
  %idx = getelementptr i32, ptr %p, i32 %i
  %val = load i32, ptr %idx, align 4
  %add = add i32 %sum, %val
  %inc = add i32 %i, 1
  %cmp = icmp slt i32 %inc, %n
  br i1 %cmp, label %loop, label %exit, !llvm.loop !0

exit:
  ret i32 %add
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.align", i32 16}
