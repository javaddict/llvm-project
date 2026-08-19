; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnFrameLowering.cpp --check-prefix=SRC
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     < %s | FileCheck %s
; REQUIRES: haydn-registered-target
;
; Role: semantic — a register-only call does not force a frame pointer.
; 38bd405 shipped `if (MFI.adjustsStack()) return true` so outgoing f64
; slots were not overwritten. That reserved R14 on every caller. Live
; law matches AIE/RISCV: adjustsStack is not enough; post-RA FI users
; add getCallFrameSPAdj instead.
; Peer: AIEBaseFrameLowering.cpp:40-43, RISCVFrameLowering.cpp:464-470.
;
; SRC: A call (ADJCALLSTACK / adjustsStack) is not enough to keep FP
; SRC: AIEBaseFrameLowering.cpp:40-43
; SRC: RISCVFrameLowering.cpp:464-470
; SRC-NOT: if (MFI.adjustsStack())

declare void @callee()
declare void @many(i32, i32, i32, i32, i32, i32, i32, i32, i32)

define void @reg_only_call() {
; CHECK-LABEL: reg_only_call:
; CHECK-NOT:   addi32{{.*}}fp
; CHECK-NOT:   .cfi_def_cfa {{fp|r14}}
; CHECK:       jal{{.*}}callee
; CHECK:       jalr
  call void @callee()
  ret void
}

; Outgoing stack args set adjustsStack / ADJCALLSTACK. Pre-fix, hasFPImpl
; returned true and reserved R14. Live law: still SP-relative; SPAdj
; covers the live call-frame window.
define void @outgoing_stack_args() {
; CHECK-LABEL: outgoing_stack_args:
; CHECK-NOT:   .cfi_def_cfa {{fp|r14}}
; CHECK:       jal{{.*}}many
; CHECK:       jalr
  call void @many(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9)
  ret void
}

; VLA still requires FP (AIE/RISCV peer).
define void @vla_needs_fp(i32 %n) {
; CHECK-LABEL: vla_needs_fp:
; CHECK:       .cfi_def_cfa {{fp|r14}}
  %p = alloca i32, i32 %n
  ret void
}
