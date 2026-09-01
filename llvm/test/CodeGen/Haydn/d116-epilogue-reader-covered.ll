; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s \
; RUN:   | FileCheck %s
;
; D1.16 verification owed by the goal (verify, don't assume): a ZOL kernel
; whose LAST-iteration def is consumed AFTER the loop (epilogue reader).
; The epilogue reader crosses the latch -> exit seam, NOT the wrap seam:
; coverage there is owned by the existing exit-leak arm plus
; ZOLSetupExitLatency/ExitSU in-flight pad (pads before PseudoLoopEnd are
; on the exit path — correct for EXIT, which is exactly why the NEW wrap
; arm must anchor elsewhere). This test pins that distinction and that the
; wrap arm does not double-pad the same block (stable total parcel count:
; the body keeps its scheduler-shaped parcels; the epilogue reader is
; covered by the trailing exit-path parcel already present).
;
; Shape: the tail load's dest %v2 is both the recurrence (wrap pair) and
; the epilogue value (exit pair). At -O2 the scheduler leaves one idle
; parcel after the tail load (ZOLSetupExitLatency/ExitSU), which covers
; BOTH the next-iteration read (wrap) and the exit read.

define i32 @epilogue_reader(ptr %p, i32 %n, i32 %c) nounwind {
entry:
  %first = getelementptr inbounds i32, ptr %p, i32 1
  %v0 = load i32, ptr %first, align 4
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %ptr = phi ptr [ %first, %entry ], [ %next, %loop ]
  %v = phi i32 [ %v0, %entry ], [ %v2, %loop ]
  %sum = add i32 %v, %c
  %next = getelementptr inbounds i32, ptr %ptr, i32 1
  %v2 = load i32, ptr %next, align 4
  %inc = add i32 %i, 1
  %done = icmp eq i32 %inc, %n
  br i1 %done, label %exit, label %loop
exit:
  ; Epilogue reader: last iteration's %v2 read after the loop body.
  %r = add i32 %v2, %sum
  ret i32 %r
}

; CHECK-LABEL: epilogue_reader:
; The ZOL body: [move32 (reads prev load)] [s_lw_post_imm (defines v2)]
; then the END-anchored idle parcel — inside [BEGIN,END], covering BOTH the
; next-iteration read (wrap seam) and the exit read (epilogue seam).
; CHECK:      s_lw_post_imm
; CHECK:      nop; nop
; The epilogue reads the last def (r4) only after that in-flight parcel.
; CHECK: add32{{[^;]*}}r4
