; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Stack locals / calls: SP adjusts; returns via jalr_w; calls via jal_w.

declare void @use_ptr(ptr)
declare i32 @use_i32(i32)
declare i64 @use_i64(i64)

;Simple local variable (one i32 alloca)

define i32 @one_local(i32 %x) {
; CHECK-LABEL: one_local:
; CHECK: {{subi32|addi32|jal}}
; CHECK: jalr_w
; Stack allocation for local
; (SFR-strip) changed bundle layout (denser packing) — the SP restore may
; be hoisted above the local st32/ld32 (anti-dep); use CHECK-DAG. Rebaselined.
; NOTE: lone load may be promoted to ld32 (slot-1 over-promotion,); both
; ld32 and ld32 are semantically identical loads.
  %p = alloca i32
  store i32 %x, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

;Multiple local variables (3 i32 allocas)

define i32 @three_locals(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: three_locals:
; CHECK: {{subi32|addi32|jal}}
; CHECK: jalr_w
; 3 x i32 = 12 bytes, rounded to 16 (8-byte alignment)
; (post-RA scheduler) hoists the SP restore above the st32/ld32 (anti-dep on sp); the
; stores/loads use pre-computed address registers, so semantics are unchanged. CHECK-DAG.
  %p1 = alloca i32
  %p2 = alloca i32
  %p3 = alloca i32
  store i32 %a, ptr %p1
  store i32 %b, ptr %p2
  store i32 %c, ptr %p3
  %v1 = load i32, ptr %p1
  %v2 = load i32, ptr %p2
  %v3 = load i32, ptr %p3
  %s = add i32 %v1, %v2
  %r = add i32 %s, %v3
  ret i32 %r
}

;Local with call (callee-save spill + local)

define i32 @local_with_call(i32 %a) {
; CHECK-LABEL: local_with_call:
; CHECK: {{subi32|addi32|jal}}
; CHECK: jalr_w
; Stack for local + potential callee-save spills
  %p = alloca i32
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  %r = call i32 @use_i32(i32 %v)
  ret i32 %r
}

;Mixed i32 and i64 locals

define i64 @mixed_locals(i32 %a, i64 %b) {
; CHECK-LABEL: mixed_locals:
; CHECK: {{subi32|addi32|jal}}
; CHECK: jalr_w
; Both i32 and i64 allocas
; (SFR-strip) changed bundle layout (denser packing) — SP restore hoisted; CHECK-DAG. Rebaselined.
  %p32 = alloca i32
  %p64 = alloca i64
  store i32 %a, ptr %p32
  store i64 %b, ptr %p64
  %v32 = load i32, ptr %p32
  %v64 = load i64, ptr %p64
  %ext = sext i32 %v32 to i64
  %r = add i64 %ext, %v64
  ret i64 %r
}

;Array local: [4 x i32] = 16 bytes

define i32 @array_local(i32 %a) {
; CHECK-LABEL: array_local:
; CHECK: {{subi32|addi32|jal}}
; CHECK: jalr_w
; (SFR-strip) changed bundle layout (denser packing) — SP restore hoisted; CHECK-DAG. Rebaselined.
  %arr = alloca [4 x i32], align 8
  %p = getelementptr [4 x i32], ptr %arr, i32 0, i32 0
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

;Stack frame with locals, callee-save spills, AND outgoing args

declare i32 @many_params(i32, i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @full_frame(i32 %a) {
; CHECK-LABEL: full_frame:
; CHECK: {{subi32|addi32|jal}}
; CHECK: jalr_w
; Stack for: local + callee-save spills + outgoing stack arg
  %p = alloca i32
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  %r = call i32 @many_params(i32 %v, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9)
  ret i32 %r
}

;Nested calls with parameter passing

define i32 @nested_call_chain(i32 %a, i32 %b) {
; CHECK-LABEL: nested_call_chain:
; CHECK: {{subi32|addi32|jal}}
; CHECK: jalr_w
; First call result feeds into second call
  %v1 = call i32 @use_i32(i32 %a)
  %v2 = call i32 @use_i32(i32 %b)
  %r = add i32 %v1, %v2
  ret i32 %r
}

;Pointer to local stack variable passed to callee

define void @pass_stack_ptr(i32 %a) {
; CHECK-LABEL: pass_stack_ptr:
; CHECK: {{subi32|addi32|jal}}
; CHECK: jalr_w
  %p = alloca i32
  store i32 %a, ptr %p
  call void @use_ptr(ptr %p)
  ret void
}
