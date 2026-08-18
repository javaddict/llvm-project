; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=CHECK
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefixes=CHECK,O0
;
; Role: semantic — advertised supported IR selects without abort at O2
; and O0 (stop-after=instruction-select). Bitfield/select/phi/s64/
; mul-div/struct/const-array/call-indirect plus the add/mul/call
; baseline. Asm print of the same IR is a later-pass seat.

@const_arr = private unnamed_addr constant [4 x i32] [i32 1, i32 2, i32 3, i32 4], align 4

%pair = type { i32, i32 }

declare i32 @ext_i32(i32)

define i32 @sup_add_mul_call(i32 %a, i32 %b) {
; CHECK-LABEL: name: sup_add_mul_call
; CHECK: ADD32
; CHECK: {{MUL|SLLI|ADD}}
; CHECK: JAL
; CHECK: RET
  %s = add i32 %a, %b
  %p = mul i32 %s, 3
  %r = call i32 @ext_i32(i32 %p)
  ret i32 %r
}

define i64 @sup_i64_arith(i64 %a, i64 %b) {
; CHECK-LABEL: name: sup_i64_arith
; CHECK: ADD64
; CHECK: RET
  %s = add i64 %a, %b
  ret i64 %s
}

define i32 @sup_indirect_call(ptr %fp, i32 %x) {
; CHECK-LABEL: name: sup_indirect_call
; CHECK: {{JALR|PseudoCALLIndirect}}
; CHECK: RET
  %r = call i32 %fp(i32 %x)
  ret i32 %r
}

define i32 @sup_bitfield_extract(i32 %value) {
; CHECK-LABEL: name: sup_bitfield_extract
; CHECK: {{ANDI32|AND32}}
; CHECK: {{SRLI32|SRL32}}
; CHECK: RET
  %masked = and i32 %value, 4080
  %shifted = lshr i32 %masked, 4
  ret i32 %shifted
}

define i32 @sup_select_s32(i1 %c, i32 %t, i32 %f) {
; CHECK-LABEL: name: sup_select_s32
; CHECK: MOVT32
; CHECK: RET
  %r = select i1 %c, i32 %t, i32 %f
  ret i32 %r
}

define i64 @sup_select_s64(i1 %c, i64 %t, i64 %f) {
; CHECK-LABEL: name: sup_select_s64
; CHECK: MOVT32
; CHECK: RET
  %r = select i1 %c, i64 %t, i64 %f
  ret i64 %r
}

define i32 @sup_phi(i1 %c, i32 %a, i32 %b) {
; CHECK-LABEL: name: sup_phi
; O0: PHI
; CHECK: RET
entry:
  br i1 %c, label %t, label %join
t:
  br label %join
join:
  %p = phi i32 [ %a, %entry ], [ %b, %t ]
  ret i32 %p
}

define i64 @sup_s64_mul(i64 %a, i64 %b) {
; CHECK-LABEL: name: sup_s64_mul
; CHECK-NOT: LIBCALL_MUL64
; CHECK: {{MUL64|MUL}}
; CHECK: RET
  %r = mul i64 %a, %b
  ret i64 %r
}

define i32 @sup_s32_div(i32 %a, i32 %b) {
; CHECK-LABEL: name: sup_s32_div
; CHECK-NOT: LIBCALL_
; CHECK: JAL
; CHECK: RET
  %r = sdiv i32 %a, %b
  ret i32 %r
}

define i32 @sup_struct_field(ptr %p) {
; CHECK-LABEL: name: sup_struct_field
; CHECK: {{LD32|LW}}
; CHECK: RET
  %f = getelementptr inbounds %pair, ptr %p, i32 0, i32 1
  %v = load i32, ptr %f, align 4
  ret i32 %v
}

define i32 @sup_const_array(i32 %i) {
; CHECK-LABEL: name: sup_const_array
; CHECK: {{LOAD_ADDR|LUI|ADDI32|LD32}}
; CHECK: RET
  %p = getelementptr inbounds [4 x i32], ptr @const_arr, i32 0, i32 %i
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

define i32 @sup_o0_smoke(i32 %a, i32 %b) {
; CHECK-LABEL: name: sup_o0_smoke
; CHECK: ADD32
; CHECK: RET
  %s = add i32 %a, %b
  ret i32 %s
}
