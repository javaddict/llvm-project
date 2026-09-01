; REQUIRES: asserts
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-postra-interblock=false < %s \
; RUN:     | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -haydn-postra-interblock=false -stats -o /dev/null < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=OFF-STATS
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-postra-interblock < %s \
; RUN:     | FileCheck %s --check-prefix=ON
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -haydn-postra-interblock -stats -o /dev/null < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=ON-STATS
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -haydn-postra-interblock -haydn-sms2 -stats -o /dev/null < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=ON-STATS
;
; W68.2R Bot scoreboard replay (AIE initializeBotScoreBoard peer,
; AIEMachineScheduler.cpp:260-405). Product default ON; explicit-off pin above.
; Layout: hdr is scheduled first; body has a single back-edge successor
; that already carries S1 depths, so initializeBotScoreBoard replays hdr's
; committed cycles into body's Bot HR. Unscheduled/unknown successors take
; the full-latency path (no static-depth fill).

; OFF-LABEL: ib_bot_replay:
; OFF: jalr
; ON-LABEL: ib_bot_replay:
; ON: jalr
; OFF-STATS-NOT: scheduled successor cycle members replayed
; ON-STATS: Number of scheduled successor cycle members replayed into the post-RA Bot scoreboard

define i32 @ib_bot_replay(ptr nocapture %p, i32 %n) {
entry:
  br label %hdr
hdr:
  %i = phi i32 [ 0, %entry ], [ %i2, %body ]
  %a = load i32, ptr %p, align 4
  %b = add i32 %a, 1
  %c = mul i32 %b, 3
  store i32 %c, ptr %p, align 4
  %cmp = icmp slt i32 %i, %n
  br i1 %cmp, label %body, label %exit
body:
  %d = load i32, ptr %p, align 4
  %e = add i32 %d, %c
  store i32 %e, ptr %p, align 4
  %i2 = add i32 %i, 1
  br label %hdr
exit:
  ret i32 %c
}
