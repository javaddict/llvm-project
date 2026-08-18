; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -haydn-postra-interblock < %s -o /dev/null
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s -o /dev/null
;
; Role: semantic — InterBlock first brick is default-off and does not
; crash when forced on. MaxLatencyFinder drops stage latency only when
; successorsAreScheduled; no remaining-latency invent.

define i32 @pred(i32 %a, i32 %b) {
entry:
  %c = add i32 %a, %b
  br label %exit
exit:
  %d = mul i32 %c, 3
  ret i32 %d
}
