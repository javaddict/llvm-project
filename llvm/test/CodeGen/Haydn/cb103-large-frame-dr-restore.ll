; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — PEI epilogue DR64 CSR restore must not emit out-of-range ld64 dN, sp, imm (e.g.

; PEI epilogue DR64 CSR restore must not emit out-of-range
; ld64 dN, sp, imm (e.g. 480). Use ld64_reg + R12 materialize instead.
; BundleSim golden scaled simm6 only allows -32..+31 for the scaled field.
;
; Force D8 as a live CSR across a large frame (mirrors the C fixture's
; register asm("d8")).

define i64 @cb103_large_frame_restore(i64 %value, i32 %index) {
entry:
  %frame = alloca [480 x i8], align 1
  ; Keep %value in d8 across the frame (CSR D8 will be saved/restored).
  %d8 = call i64 asm sideeffect "", "={d8},{d8},~{memory}"(i64 %value)
  %idx = and i32 %index, 31
  %p = getelementptr inbounds [480 x i8], ptr %frame, i32 0, i32 %idx
  %trunc = trunc i64 %d8 to i8
  store volatile i8 %trunc, ptr %p, align 1
  %reload = load volatile i8, ptr %p, align 1
  %z = zext i8 %reload to i64
  %keep = call i64 asm sideeffect "", "={d8},{d8},~{memory}"(i64 %d8)
  %sum = add i64 %keep, %z
  ret i64 %sum
}

; CHECK-NOT: ld64{{.*}}sp, 480
; CHECK-NOT: ld64{{.*}}sp, 4{{[0-9][0-9]}}
; Large-frame restore uses register-offset form with offset in a GPR.
; CHECK: ld64_reg
