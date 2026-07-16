; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; Single-BB ZOL uses the *same MBB* for SET start/end operands (Header==Latch).
; That is fine. The START and END *labels* must still resolve to different
; addresses: HWLR_END must be strictly greater than HWLR_BEGIN (inclusive END
; of the last body parcel). A 1-instr body without padding collapses both
; labels onto one PC — illegal. HaydnFixupHwLoops inserts trailing body NOPs
; so START is on the first useful op and END on the last pad.

define void @tiny_body(ptr nocapture %p, i32 %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i2, %loop ]
  %gep = getelementptr inbounds i32, ptr %p, i32 %i
  store i32 %i, ptr %gep, align 4
  %i2 = add nuw i32 %i, 1
  %c = icmp ult i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

; CHECK-LABEL: tiny_body:
; CHECK:       set_hwloop_f2_w{{.*}}[[START:\.LLhwloop_start[0-9]+]], [[END:\.LLhwloop_end[0-9]+]]
; CHECK:       [[START]]:
; Useful work at START; at least one more bundle before END (trailing pads).
; CHECK:       add32
; CHECK:       [[END]]:
; CHECK-NOT:   [[END]]:
; CHECK:       jalr_w
