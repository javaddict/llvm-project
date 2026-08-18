; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:     -stop-after=legalizer -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=LEG
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM

; Role: MIR — G_CONSTANT_FOLD_BARRIER is alwaysLegal (same as G_FREEZE).
;
; REGRESSION TEST: legalizer must not abort on G_CONSTANT_FOLD_BARRIER.
;
; Bug: ConstHoist / same-type bitcast of ConstantInt becomes
; G_CONSTANT_FOLD_BARRIER. It sat in a .lowerFor({S16,S32,S64}) bucket, but
; LegalizerHelper has no lower() for this opcode →
; "unable to legalize instruction: G_CONSTANT_FOLD_BARRIER" at
; -global-isel-abort=1 (bit-simplify.ll @and_or_disjoint_full).
; Fix: treat it like G_FREEZE — alwaysLegal; generic InstructionSelect strips
; the barrier. If this regresses, llc dies in the legalizer.
;
; Test design: explicit same-type bitcast of a ConstantInt is the IRTranslator
; producer (IRTranslator::translateBitCast). -O0 -stop-after=legalizer keeps
; the barrier. Full llc of the ConstHoist-shaped or-with-large-imm must not
; abort.

; LEG-LABEL: name: cfb_same_type_bitcast
; LEG: G_CONSTANT_FOLD_BARRIER
; ASM-LABEL: cfb_same_type_bitcast:
; ASM: or32
define i32 @cfb_same_type_bitcast(i32 %a) nounwind {
  %c = bitcast i32 4278255360 to i32
  %r = or i32 %a, %c
  ret i32 %r
}

; LEG-LABEL: name: and_or_disjoint_full
; ASM-LABEL: and_or_disjoint_full:
; ASM: or32
define i32 @and_or_disjoint_full(i32 %a) nounwind {
  %masked = and i32 %a, 16711935
  %r = or i32 %masked, 4278255360
  ret i32 %r
}
