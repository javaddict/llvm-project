; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION: G_FENCE must legalize (alwaysLegal) and select to MEMBARRIER.
; Previously G_FENCE shared legalFor({S32,P0}) with typed ops; zero type
; indices caused Legalizer ArrayRef index OOB (same class as old G_TRAP).
; __atomic_signal_fence → fence syncscope("singlethread") → G_FENCE.

define void @signal_fence_seq_cst() {
; CHECK-LABEL: signal_fence_seq_cst:
; CHECK:       //MEMBARRIER
  fence syncscope("singlethread") seq_cst
  ret void
}

define void @system_fence_seq_cst() {
; CHECK-LABEL: system_fence_seq_cst:
; CHECK:       //MEMBARRIER
  fence seq_cst
  ret void
}

define void @system_fence_acquire() {
; CHECK-LABEL: system_fence_acquire:
; CHECK:       //MEMBARRIER
  fence acquire
  ret void
}
