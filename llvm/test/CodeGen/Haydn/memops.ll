; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — memory operations: memcpy, memmove, memset.

; Test memory operations: memcpy, memmove, memset.

;memcpy (small, constant size)

define void @test_memcpy_small(ptr %dst, ptr %src) {
; CHECK-LABEL: test_memcpy_small:
; CHECK: lui{{.*}}memcpy
; CHECK: addi32{{.*}}memcpy
; CHECK: jalr{{.*}}lr
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 16, i1 false)
  ret void
}

;memcpy (large size)
define void @test_memcpy_large(ptr %dst, ptr %src, i32 %size) {
; CHECK-LABEL: test_memcpy_large:
; Should call memcpy
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 %size, i1 false)
  ret void
}

;memcpy with volatile
define void @test_memcpy_volatile(ptr %dst, ptr %src) {
; CHECK-LABEL: test_memcpy_volatile:
; CHECK: lui{{.*}}memcpy
; CHECK: addi32{{.*}}memcpy
; CHECK: jalr{{.*}}lr
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 8, i1 true)
  ret void
}

;memmove (overlapping)
define void @test_memmove(ptr %dst, ptr %src, i32 %size) {
; CHECK-LABEL: test_memmove:
; Should call memmove
  call void @llvm.memmove.p0.p0.i32(ptr %dst, ptr %src, i32 %size, i1 false)
  ret void
}

;memset (set to zero)
define void @test_memset_zero(ptr %dst, i32 %size) {
; CHECK-LABEL: test_memset_zero:
; Should call memset
  call void @llvm.memset.p0.i32(ptr %dst, i8 0, i32 %size, i1 false)
  ret void
}

;memset (set to nonzero)
define void @test_memset_nonzero(ptr %dst, i32 %size) {
; CHECK-LABEL: test_memset_nonzero:
; Should call memset
  call void @llvm.memset.p0.i32(ptr %dst, i8 42, i32 %size, i1 false)
  ret void
}

;memset with small constant size
define void @test_memset_small(ptr %dst) {
; CHECK-LABEL: test_memset_small:
; May inline as store(s) or call memset
  call void @llvm.memset.p0.i32(ptr %dst, i8 255, i32 4, i1 false)
  ret void
}

;memcpy with alignment
define void @test_memcpy_aligned(ptr %dst, ptr %src) {
; CHECK-LABEL: test_memcpy_aligned:
; CHECK: lui{{.*}}memcpy
; CHECK: addi32{{.*}}memcpy
; CHECK: jalr{{.*}}lr
; May use wider loads/stores due to alignment
  call void @llvm.memcpy.p0.p0.i32(ptr align 8 %dst, ptr align 8 %src, i32 64, i1 false)
  ret void
}

;memcpy between overlapping regions (undefined if not memmove)
define void @test_overlapping_memcpy(ptr %ptr, i32 %offset) {
; CHECK-LABEL: test_overlapping_memcpy:
; This is undefined behavior but we test what codegen produces
  %src = getelementptr i8, ptr %ptr, i32 %offset
  call void @llvm.memcpy.p0.p0.i32(ptr %ptr, ptr %src, i32 32, i1 false)
  ret void
}

;bzero (alias for memset zero)
define void @test_bzero(ptr %dst, i32 %size) {
; CHECK-LABEL: test_bzero:
  call void @llvm.memset.p0.i32(ptr %dst, i8 0, i32 %size, i1 false)
  ret void
}

;Multiple memory ops in sequence
define void @test_multi_memops(ptr %dst1, ptr %dst2, ptr %src, i32 %size) {
; CHECK-LABEL: test_multi_memops:
; CHECK: lui{{.*}}memcpy
; CHECK: addi32{{.*}}memcpy
; CHECK: jalr{{.*}}lr
; CHECK: lui{{.*}}memset
; CHECK: addi32{{.*}}memset
; CHECK: jalr{{.*}}lr
  call void @llvm.memcpy.p0.p0.i32(ptr %dst1, ptr %src, i32 %size, i1 false)
  call void @llvm.memset.p0.i32(ptr %dst2, i8 0, i32 %size, i1 false)
  ret void
}

;memcpy of struct
define void @test_memcpy_struct(ptr %dst, ptr %src) {
; CHECK-LABEL: test_memcpy_struct:
; Copy 12-byte struct
; CHECK: lui{{.*}}memcpy
; CHECK: addi32{{.*}}memcpy
; CHECK: jalr{{.*}}lr
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 12, i1 false)
  ret void
}

;Variable size memcpy with bounds check pattern
define void @test_checked_memcpy(ptr %dst, ptr %src, i32 %requested, i32 %max_size) {
; CHECK-LABEL: test_checked_memcpy:
; llvm.umin.i32 → minu32 (stale mid-file umin marker removed)
; CHECK:       minu32{{(\.s[012])?}}
; CHECK:       lui{{.*}}memcpy
; CHECK:       addi32{{.*}}memcpy
; CHECK:       jalr{{.*}}lr
  %safe_size = call i32 @llvm.umin.i32(i32 %requested, i32 %max_size)
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 %safe_size, i1 false)
  ret void
}

declare void @llvm.memcpy.p0.p0.i32(ptr nocapture, ptr nocapture, i32, i1)
declare void @llvm.memmove.p0.p0.i32(ptr nocapture, ptr nocapture, i32, i1)
declare void @llvm.memset.p0.i32(ptr nocapture, i8, i32, i1)
declare i32 @llvm.umin.i32(i32, i32)
