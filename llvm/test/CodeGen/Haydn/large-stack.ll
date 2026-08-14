; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 %s -o - | FileCheck %s

; Role: semantic — Large SP adjust must materialize the size via HaydnMatInt into a PEI scratch (unused allocatable call-clobbered GPR) then sub32/add32.

; Large SP adjust must materialize the size via HaydnMatInt into a PEI scratch
; (unused allocatable call-clobbered GPR) then sub32/add32. Never the
; pre-ISA-43 LUI+(<<16) poison path. AIE model: R12 is a normal allocatable
; GPR (no free AT).
;
; Exact stack sizes may include a small emergency scavenger spill. Match
; materialize + sub32/add32, not a fixed byte count.

; This function has a very large local array that requires more than 16 bits.

define void @large_stack_frame() {
; CHECK-LABEL: large_stack_frame:
; CHECK:       xor32{{.*}}r0, r0, r0
; ~256KB: MatInt into scratch then sub32.
; CHECK:       {{(addi32|ori32|lui)}}
; CHECK:       sub32{{.*}}sp, sp,
; CHECK:       .cfi_def_cfa_offset
; CHECK:       {{(addi32|ori32|lui)}}
; CHECK:       add32{{.*}}sp, sp,
; CHECK:       jalr
  %array = alloca [65536 x i32], align 8
  ret void
}

; Stack size just over 64KB.
define void @stack_64k_plus() {
; CHECK-LABEL: stack_64k_plus:
; CHECK:       xor32{{.*}}r0, r0, r0
; CHECK:       {{(addi32|ori32|lui)}}
; CHECK:       sub32{{.*}}sp, sp,
; CHECK:       .cfi_def_cfa_offset
; CHECK:       {{(addi32|ori32|lui)}}
; CHECK:       add32{{.*}}sp, sp,
; CHECK:       jalr
  %array = alloca [16385 x i32], align 8  ; 16385 * 4 = 65540 → aligned ~65544
  ret void
}

; Fits in 16 bits — single SUBI32 (plus possible scavenger pad).
define void @stack_small() {
; CHECK-LABEL: stack_small:
; CHECK:       subi32{{.*}}sp, sp,
; CHECK:       .cfi_def_cfa_offset
; CHECK:       addi32{{(_w)?}}{{.*}}sp, sp,
; CHECK:       jalr
  %array = alloca [64 x i32], align 8  ; 64 * 4 = 256 bytes
  ret void
}

; Extremely large stack frame.
define void @very_large_stack() {
; CHECK-LABEL: very_large_stack:
; CHECK:       xor32{{.*}}r0, r0, r0
; CHECK:       {{(addi32|ori32|lui)}}
; CHECK:       sub32{{.*}}sp, sp,
; CHECK:       .cfi_def_cfa_offset
; CHECK:       {{(addi32|ori32|lui)}}
; CHECK:       add32{{.*}}sp, sp,
; CHECK:       jalr
  %array = alloca [131072 x i32], align 8  ; 131072 * 4 = 524288 bytes
  ret void
}
