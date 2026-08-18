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

define void @reg_only_call() {
; CHECK-LABEL: reg_only_call:
; CHECK-NOT:   addi32{{.*}}fp
; CHECK-NOT:   .cfi_def_cfa {{fp|r14}}
; CHECK:       jal{{.*}}callee
; CHECK:       jalr
  call void @callee()
  ret void
}
