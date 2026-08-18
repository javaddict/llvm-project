; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -mattr=+frame-pointer < %s | FileCheck %s --check-prefix=FORCEFP

; Role: semantic — Optional register-policy features (ABI-like target options): mattr=+frame-pointer force dedicated FP (R14).

; Optional register-policy features (ABI-like target options):
; mattr=+frame-pointer force dedicated FP (R14)
; Default (AIE model): omit FP when ABI allows; R12 is a normal allocatable GPR
; (no free AT). Post-RA address/const math uses free-reg scavenge
; (PostRAScratchFI only if no free GPR).
;
; leaf_stack: fixed locals only — no VLA/realign/frameaddress.
; large_frame: EFI scavenger must compile with allocatable R12.

define i32 @leaf_stack(i32 %x) nounwind {
; DEFAULT-LABEL: leaf_stack:
; DEFAULT:       subi32{{.*}} sp, sp,
; DEFAULT-NOT:   .cfi_def_cfa_register
; DEFAULT-NOT:   addi32{{.*}} fp, sp,
; DEFAULT-NOT:   addi32{{.*}} r14, r13,
; DEFAULT:       jalr
;
; FORCEFP-LABEL: leaf_stack:
; FORCEFP:       subi32{{.*}} sp, sp,
; FORCEFP:       addi32{{.*}} {{fp|r14}}, {{sp|r13}},
; FORCEFP:       jalr
  %a = alloca i32
  %b = alloca i32
  store i32 %x, ptr %a
  store i32 42, ptr %b
  %va = load i32, ptr %a
  %vb = load i32, ptr %b
  %r = add i32 %va, %vb
  ret i32 %r
}

; ABI still forces FP for VLAs even without +frame-pointer.
define i32 @vla_needs_fp(i32 %n, i32 %x) nounwind {
; DEFAULT-LABEL: vla_needs_fp:
; DEFAULT:       addi32{{.*}} {{fp|r14}}, {{sp|r13}},
; DEFAULT:       jalr
;
; FORCEFP-LABEL: vla_needs_fp:
; FORCEFP:       addi32{{.*}} {{fp|r14}}, {{sp|r13}},
  %p = alloca i32, i32 %n
  store i32 %x, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

; Large enough stack that EFI may need a scratch for FI rebasing.
; Compiles under scavenger with allocatable R12.
define i32 @large_frame_scratch(i32 %x) nounwind {
; DEFAULT-LABEL: large_frame_scratch:
; DEFAULT:       subi32{{.*}} sp, sp,
; DEFAULT:       jalr
  %buf = alloca [256 x i32]
  %p = getelementptr [256 x i32], ptr %buf, i32 0, i32 200
  store i32 %x, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}
