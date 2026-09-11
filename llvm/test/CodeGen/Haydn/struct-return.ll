; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Struct returns follow RetCC_Haydn: ≤2 × i32/ptr in R1–R2 (R0 is soft-zero).

; Struct returns follow RetCC_Haydn:
; ≤2 × i32/ptr in R1–R2 (R0 is soft-zero)
; i64/SIMD in D0
; anything larger is sret-demoted (hidden pointer in R1, stores to memory)
;
; Fixed : lowerReturn used only VRegs[0] → multi-field returns were
; silently truncated. Now uses splitToValueTypes + RetCC_Haydn; canLowerReturn
; drives sret demotion for oversized returns.

;Two i32 fields fit in R1–R2

define { i32, i32 } @return_small_struct() {
; CHECK-LABEL: return_small_struct:
; Default-ON convergence driver packs both materializations into one E2
; parcel; composite operand order is (r2, r1).
; CHECK:       addi32{{(\.s[012])?}} r2, r0, 99;{{.*}}addi32{{(\.s[012])?}} r1, r0, 42
  %r = insertvalue { i32, i32 } undef, i32 42, 0
  %r2 = insertvalue { i32, i32 } %r, i32 99, 1
  ret { i32, i32 } %r2
}

;Call site consumes both return regs
define i32 @call_small_struct() {
; CHECK-LABEL: call_small_struct:
; CHECK:       lui{{.*}}return_small_struct
; CHECK:       addi32{{.*}}return_small_struct
; CHECK:       jalr{{.*}}lr
; CHECK:       add32{{(\.s[012])?}} r1, r1, r2
  %s = call { i32, i32 } @return_small_struct()
  %v1 = extractvalue { i32, i32 } %s, 0
  %v2 = extractvalue { i32, i32 } %s, 1
  %sum = add i32 %v1, %v2
  ret i32 %sum
}

;Four i32s exceed R1–R2 → sret demotion (stores via hidden R1 pointer)
define { i32, i32, i32, i32 } @return_medium_struct() {
; CHECK-LABEL: return_medium_struct:
; sret stores via hidden pointer in r1 (offsets may pack)
; CHECK-DAG: st32{{.*}}r1
  %r = insertvalue { i32, i32, i32, i32 } undef, i32 1, 0
  %r2 = insertvalue { i32, i32, i32, i32 } %r, i32 2, 1
  %r3 = insertvalue { i32, i32, i32, i32 } %r2, i32 3, 2
  %r4 = insertvalue { i32, i32, i32, i32 } %r3, i32 4, 3
  ret { i32, i32, i32, i32 } %r4
}

;i64 + i32: D0 + R1
define { i64, i32 } @return_mixed_struct() {
; CHECK-LABEL: return_mixed_struct:
; CHECK:       addi32{{(\.s[012])?}} r1, r0, 42
  %r = insertvalue { i64, i32 } undef, i64 123456789, 0
  %r2 = insertvalue { i64, i32 } %r, i32 42, 1
  ret { i64, i32 } %r2
}

;Explicit sret (caller-allocated buffer)
define void @return_large_struct(ptr %sret_output) {
; CHECK-LABEL: return_large_struct:
; CHECK-DAG: st32{{.*}}r1
  %v1 = getelementptr { i32, i32, i32, i32, i32, i32 }, ptr %sret_output, i32 0, i32 0
  store i32 1, ptr %v1
  %v2 = getelementptr { i32, i32, i32, i32, i32, i32 }, ptr %sret_output, i32 0, i32 1
  store i32 2, ptr %v2
  %v3 = getelementptr { i32, i32, i32, i32, i32, i32 }, ptr %sret_output, i32 0, i32 2
  store i32 3, ptr %v3
  %v4 = getelementptr { i32, i32, i32, i32, i32, i32 }, ptr %sret_output, i32 0, i32 3
  store i32 4, ptr %v4
  %v5 = getelementptr { i32, i32, i32, i32, i32, i32 }, ptr %sret_output, i32 0, i32 4
  store i32 5, ptr %v5
  %v6 = getelementptr { i32, i32, i32, i32, i32, i32 }, ptr %sret_output, i32 0, i32 5
  store i32 6, ptr %v6
  ret void
}

;Call with explicit sret buffer
define i32 @call_large_struct() {
; CHECK-LABEL: call_large_struct:
; CHECK:       lui{{.*}}return_large_struct
; CHECK:       addi32{{.*}}return_large_struct
; CHECK:       jalr{{.*}}lr
; CHECK:       ld32{{(\.s[012])?}} r1,
  %s = alloca { i32, i32, i32, i32, i32, i32 }
  call void @return_large_struct(ptr %s)
  %elem = getelementptr { i32, i32, i32, i32, i32, i32 }, ptr %s, i32 0, i32 0
  %v = load i32, ptr %elem
  ret i32 %v
}

;Three promoted fields (i8,i16,i32) → sret demotion
define { i8, i16, i32 } @return_mixed_sizes() {
; CHECK-LABEL: return_mixed_sizes:
; CHECK-DAG: {{st8|s_sb}}
; CHECK-DAG: {{st16|s_shw}}
; CHECK-DAG: {{st32|s_sw}}
  %r = insertvalue { i8, i16, i32 } undef, i8 7, 0
  %r2 = insertvalue { i8, i16, i32 } %r, i16 42, 1
  %r3 = insertvalue { i8, i16, i32 } %r2, i32 99, 2
  ret { i8, i16, i32 } %r3
}

;Struct byval parameter; two-field array return in R1–R2
define [2 x i32] @struct_byval([2 x i32] %s) {
; CHECK-LABEL: struct_byval:
; CHECK:       add32{{(\.s[012])?}} r1, r1, r2
; CHECK:       move32{{(\.s[012])?}} r2, r1
  %elem0 = extractvalue [2 x i32] %s, 0
  %elem1 = extractvalue [2 x i32] %s, 1
  %sum = add i32 %elem0, %elem1
  %result = insertvalue [2 x i32] undef, i32 %sum, 0
  %result2 = insertvalue [2 x i32] %result, i32 %sum, 1
  ret [2 x i32] %result2
}

;Nested struct (3 × i32) → sret demotion
define { i32, { i32, i32 } } @return_nested_struct() {
; CHECK-LABEL: return_nested_struct:
; CHECK-DAG: st32{{.*}}r1
  %inner = insertvalue { i32, i32 } undef, i32 10, 0
  %inner2 = insertvalue { i32, i32 } %inner, i32 20, 1
  %outer = insertvalue { i32, { i32, i32 } } undef, i32 5, 0
  %outer2 = insertvalue { i32, { i32, i32 } } %outer, { i32, i32 } %inner2, 1
  ret { i32, { i32, i32 } } %outer2
}
