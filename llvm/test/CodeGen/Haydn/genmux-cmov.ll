; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s
;
; GenMux retired. Branchy PHI / select-like diamonds lower via
; SSA EarlyIfConversion + insertSelect (or IR select → MOVT at isel).
; Guard: final code uses movt/movf, not a live branch-over-move for simple
; min/max-shaped control flow.

; CHECK-LABEL: cmov_sgt_simple:
define i32 @cmov_sgt_simple(i32 %a, i32 %b) nounwind {
entry:
  %cmp = icmp sgt i32 %a, %b
  br i1 %cmp, label %if.then, label %if.else
if.then:
  br label %if.end
if.else:
  br label %if.end
if.end:
  %result = phi i32 [%a, %if.then], [%b, %if.else]
  ret i32 %result
}
; CHECK: mov{{t|f}}32

; CHECK-LABEL: cmov_slt_simple:
define i32 @cmov_slt_simple(i32 %a, i32 %b) nounwind {
entry:
  %cmp = icmp slt i32 %a, %b
  br i1 %cmp, label %if.then, label %if.else
if.then:
  br label %if.end
if.else:
  br label %if.end
if.end:
  %result = phi i32 [%a, %if.then], [%b, %if.else]
  ret i32 %result
}
; CHECK: mov{{t|f}}32

; CHECK-LABEL: cmov_select_ir:
define i32 @cmov_select_ir(i32 %a, i32 %b, i32 %c, i32 %d) nounwind {
entry:
  %cmp = icmp sgt i32 %a, %b
  %sel = select i1 %cmp, i32 %c, i32 %d
  ret i32 %sel
}
; CHECK: mov{{t|f}}32
; CHECK-NOT: neg32
; CHECK-NOT: not32
