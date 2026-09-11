; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — residual: aggregates pass as Indirect pointers (no byval).

; G-ABI-VEC residual: aggregates pass as Indirect pointers (no byval).
; Clang emits ptr (dead_on_return / sret), never byval(%Big). Backend loads
; fields through the pointer in R1 and stores through the sret pointer.
;
; %Big is 4 x i64 (32 bytes) — too large for R1–R2 / D0 returns.

%Big = type { i64, i64, i64, i64 }

; Indirect arg: pointer in R1, load fields from memory.
define i32 @take_big(ptr %s) nounwind {
; CHECK-LABEL: take_big:
; CHECK: ld32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %a = getelementptr %Big, ptr %s, i32 0, i32 0
  %v0 = load i64, ptr %a
  %b = getelementptr %Big, ptr %s, i32 0, i32 1
  %v1 = load i64, ptr %b
  %sum = add i64 %v0, %v1
  %trunc = trunc i64 %sum to i32
  ret i32 %trunc
}

; sret return: store into caller-provided pointer (R1).
define void @make_big(ptr sret(%Big) %out, i64 %x) nounwind {
; CHECK-LABEL: make_big:
; CHECK: {{d_sw|st}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %a = getelementptr %Big, ptr %out, i32 0, i32 0
  store i64 %x, ptr %a
  ret void
}

; Call passes the aggregate as a plain pointer (no byval memcpy into CC slots).
define i32 @call_take_big(ptr %p) nounwind {
; CHECK-LABEL: call_take_big:
; CHECK: lui{{.*}}take_big
; CHECK: addi32{{.*}}take_big
; CHECK: jalr{{.*}}lr
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %r = call i32 @take_big(ptr %p)
  ret i32 %r
}

; Multiple field loads through the indirect pointer.
define i64 @sum_big_fields(ptr %s) nounwind {
; CHECK-LABEL: sum_big_fields:
; CHECK: ld32
; CHECK: {{add64|add32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %p0 = getelementptr %Big, ptr %s, i32 0, i32 0
  %p1 = getelementptr %Big, ptr %s, i32 0, i32 1
  %p2 = getelementptr %Big, ptr %s, i32 0, i32 2
  %p3 = getelementptr %Big, ptr %s, i32 0, i32 3
  %v0 = load i64, ptr %p0
  %v1 = load i64, ptr %p1
  %v2 = load i64, ptr %p2
  %v3 = load i64, ptr %p3
  %s01 = add i64 %v0, %v1
  %s23 = add i64 %v2, %v3
  %r = add i64 %s01, %s23
  ret i64 %r
}
