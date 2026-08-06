; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — SSA EarlyIfConversion + Haydn insertSelect (MOVT/MOVF).

; SSA EarlyIfConversion + Haydn insertSelect (MOVT/MOVF).
;
; Diamond with a PHI of two values must be if-converted to a conditional
; move when the branch is a single-register zero-test (BEQZ/BNEZ after icmp).
; CFG ownership is EarlyIfConverter's — not GenMux Pattern 2.


define i32 @diamond_select(i32 %c, i32 %a, i32 %b) nounwind {
entry:
  %tobool = icmp ne i32 %c, 0
  br i1 %tobool, label %if.then, label %if.else

if.then:
  br label %if.end

if.else:
  br label %if.end

if.end:
  %r = phi i32 [ %a, %if.then ], [ %b, %if.else ]
  ret i32 %r
}

; Either EarlyIfConv insertSelect or isel of a folded select — result is cmov.
; CHECK: mov{{t|f}}32

; CHECK-LABEL: triangle_select:
define i32 @triangle_select(i32 %c, i32 %a, i32 %b) nounwind {
entry:
  %tobool = icmp eq i32 %c, 0
  br i1 %tobool, label %if.then, label %if.end

if.then:
  br label %if.end

if.end:
  %r = phi i32 [ %a, %if.then ], [ %b, %entry ]
  ret i32 %r
}

; CHECK: mov{{t|f}}32
