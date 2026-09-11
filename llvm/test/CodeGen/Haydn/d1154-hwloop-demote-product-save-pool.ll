; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -mattr=+hwloop \
; RUN:   -global-isel-abort=1 -O2 -stop-after=prologepilog \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefix=PEI
; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -mattr=+hwloop \
; RUN:   -global-isel-abort=1 -O2 -stop-after=haydn-hwloops \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -mattr=+hwloop \
; RUN:   -global-isel-abort=1 -O2 -verify-machineinstrs < %s 2>&1 \
; RUN:   | FileCheck %s

; D1.154 — product save-pool supply. ISel emits LoopStart; PEI must
; reserve a 4-byte demote-save FI for it (twin of the D1.88 counter
; pool). Occupancy-fragile LatchScr==Prefer is pinned by
; d1154-hwloop-demote-loopstart-savefi-pre-pei.mir (LoopStart-at-PEI
; census + product-timeline demote). This .ll is the full-llc product
; path: oversize body + trip live-after. CHECK-NOT LLVM ERROR.
;
; Call-in-body IR is TTI-declined (hwloop-demote-call-body-e2e.ll).

@arr = global [256 x i32] zeroinitializer

define i32 @d1154_product_save_pool(i32 %n, ptr nocapture %p,
                                    i32 %v0, i32 %v1, i32 %v2, i32 %v3,
                                    i32 %v4, i32 %v5, i32 %v6, i32 %v7,
                                    i32 %v8, i32 %v9) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  store volatile i32 0, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 0)
  store volatile i32 1, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 1)
  store volatile i32 2, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 2)
  store volatile i32 3, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 3)
  store volatile i32 4, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 4)
  store volatile i32 5, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 5)
  store volatile i32 6, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 6)
  store volatile i32 7, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 7)
  store volatile i32 8, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 8)
  store volatile i32 9, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 9)
  store volatile i32 10, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 10)
  store volatile i32 11, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 11)
  store volatile i32 12, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 12)
  store volatile i32 13, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 13)
  store volatile i32 14, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 14)
  store volatile i32 15, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 15)
  store volatile i32 16, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 16)
  store volatile i32 17, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 17)
  store volatile i32 18, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 18)
  store volatile i32 19, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 19)
  store volatile i32 20, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 20)
  store volatile i32 21, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 21)
  store volatile i32 22, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 22)
  store volatile i32 23, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 23)
  store volatile i32 24, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 24)
  store volatile i32 25, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 25)
  store volatile i32 26, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 26)
  store volatile i32 27, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 27)
  store volatile i32 28, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 28)
  store volatile i32 29, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 29)
  store volatile i32 30, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 30)
  store volatile i32 31, ptr getelementptr ([256 x i32], ptr @arr, i32 0, i32 31)
  %inc = add nuw i32 %i, 1
  %c = icmp ult i32 %inc, %n
  br i1 %c, label %loop, label %exit

exit:
  %s0 = add i32 %v0, %v1
  %s1 = add i32 %v2, %v3
  %s2 = add i32 %v4, %v5
  %s3 = add i32 %v6, %v7
  %s4 = add i32 %v8, %v9
  %t0 = add i32 %s0, %s1
  %t1 = add i32 %s2, %s3
  %t2 = add i32 %t0, %t1
  %t3 = add i32 %t2, %s4
  store i32 %n, ptr %p, align 4
  ret i32 %t3
}

; Product ISel LoopStart is present at PEI. Exact spill-slot COUNT is the
; leaf LoopStart MIR pin (CSR/arg spills here would make COUNT-4 at-least,
; not a save-pool proof).
; PEI: LoopStart
; PEI-NOT: LLVM ERROR

; Demote save/counter ST must carry FixedStack MMO. Bundle verifier
; refuses a mayStore member with no MMO (pjpeg_decode_mcu S_SW).
; MIR-LABEL: name: d1154_product_save_pool
; MIR-NOT: LLVM ERROR
; MIR: {{ST32|S_SW}}{{.*}} :: (store (s32) into %stack.
; MIR: {{S_LW_WITH_IMM|LD32}}{{.*}}%stack.

; CHECK-NOT: LLVM ERROR
; CHECK-NOT: report_fatal_error
; CHECK-NOT: store member carries no machine memory operand
; CHECK-LABEL: d1154_product_save_pool:
; Live-after trip is stored:
; CHECK: st32
