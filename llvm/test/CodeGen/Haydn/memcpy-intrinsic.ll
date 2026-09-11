; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — memcpy, memset, memmove intrinsics (lowered to libcalls for baremetal).

; Test memcpy, memset, memmove intrinsics (lowered to libcalls for baremetal)

; Note: For baremetal MVB, these intrinsics are lowered to libcalls.
; The actual libcall names depend on the AsmPrinter implementation.

;Simple memcpy (lowered to libcall)

define void @test_memcpy(ptr %dst, ptr %src) {
; CHECK-LABEL: test_memcpy:
; CHECK: lui{{.*}}memcpy
; CHECK: addi32{{.*}}memcpy
; CHECK: jalr{{.*}}lr
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 16, i1 false)
  ret void
}

;memcpy with constant size
define void @test_memcpy_const(ptr %dst, ptr %src) {
; CHECK-LABEL: test_memcpy_const:
; CHECK: lui{{.*}}memcpy
; CHECK: addi32{{.*}}memcpy
; CHECK: jalr{{.*}}lr
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 4, i1 false)
  ret void
}

;memset
define void @test_memset(ptr %dst) {
; CHECK-LABEL: test_memset:
; CHECK: lui{{.*}}memset
; CHECK: addi32{{.*}}memset
; CHECK: jalr{{.*}}lr
  call void @llvm.memset.p0.i32(ptr %dst, i8 42, i32 16, i1 false)
  ret void
}

;memset with variable size
define void @test_memset_var(ptr %dst, i8 %val, i32 %n) {
; CHECK-LABEL: test_memset_var:
; CHECK: lui{{.*}}memset
; CHECK: addi32{{.*}}memset
; CHECK: jalr{{.*}}lr
  call void @llvm.memset.p0.i32(ptr %dst, i8 %val, i32 %n, i1 false)
  ret void
}

;Small memcpy (single word - still libcall for baremetal)
define void @test_memcpy_small(ptr %dst, ptr %src) {
; CHECK-LABEL: test_memcpy_small:
; CHECK: lui{{.*}}memcpy
; CHECK: addi32{{.*}}memcpy
; CHECK: jalr{{.*}}lr
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 4, i1 false)
  ret void
}

;memcpy of struct (12 bytes)
%struct.Simple = type { i32, i32, i32 }

define void @test_memcpy_struct(ptr %dst, ptr %src) {
; CHECK-LABEL: test_memcpy_struct:
; CHECK: lui{{.*}}memcpy
; CHECK: addi32{{.*}}memcpy
; CHECK: jalr{{.*}}lr
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 12, i1 false)
  ret void
}

;memset zero (bzero pattern)
define void @test_memset_zero(ptr %dst, i32 %n) {
; CHECK-LABEL: test_memset_zero:
; CHECK: lui{{.*}}memset
; CHECK: addi32{{.*}}memset
; CHECK: jalr{{.*}}lr
  call void @llvm.memset.p0.i32(ptr %dst, i8 0, i32 %n, i1 false)
  ret void
}

declare void @llvm.memcpy.p0.p0.i32(ptr nocapture writeonly, ptr nocapture readonly, i32, i1 immarg)
declare void @llvm.memset.p0.i32(ptr nocapture writeonly, i8, i32, i1 immarg)
