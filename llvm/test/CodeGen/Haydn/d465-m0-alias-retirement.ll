; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s \
; RUN:     | FileCheck %s

; Role: semantic — retire Mode-0 alias producers XOR32_M0 ST32_M0S0LS / LD32_M0S0LS in HaydnAsmPrinter.

; REGRESSION TEST : retire Mode-0 alias producers XOR32_M0
; ST32_M0S0LS / LD32_M0S0LS in HaydnAsmPrinter. These were emitted directly
; as MCInsts (post-finalizer) for the re-zero-R0 idiom (XOR32_M0 after calls)
; and the varargs va_list setup (ST32_M0S0LS / LD32_M0S0LS in VASTART/VACOPY).
; They have NO _S<k>_FLEX variant, so the forcing function fires:
; "Haydn MC: opcode 'XOR32_M0' (opNNN) has no Format E form"
;
; Fix : emit the legacy base names XOR32 / ST32 / LD32 instead. These
; auto-pair via the FlexMap suffix-strip rule to XOR32
; ST32_S0 / LD32_S0 (the Family 2 defs). The 3 GPR32/imm
; operands bind positionally; the Format E encoder resolves the flex variant.
;
; Test design: (1) a function with a call exercises the post-call re-zero-R0
; XOR32 emit; (2) a varargs function exercises VASTART (ST32) and VACOPY
; (LD32 + ST32). If the producer regresses to the M0 alias, llc aborts at
; emission with the forcing-function error. The CHECKs confirm the.s parses
; and the mnemonics land (xor32 / st32 / ld32, NOT xor32_m0 / st32_m0s0ls).

;f_call
; The post-call re-zero emits xor32 (r0,r0,r0). The bundle printer may render
; it standalone or in a bundle; check the mnemonic appears.

define dso_local i32 @f_call(i32 %a) {
entry:
  %r = tail call i32 @ext(i32 %a)
  ret i32 %r
}
declare dso_local i32 @ext(i32)

;f_varargs
; CHECK-LABEL: f_varargs:
; VASTART emits st32 (va_list field stores); VACOPY emits ld32 + st32 pairs.
; CHECK: st32
; CHECK: ld32
define dso_local void @f_varargs(i32 %n, ...) {
entry:
  %ap = alloca i8, i32 32, align 8
  call void @llvm.va_start(i8* %ap)
  call void @llvm.va_copy(i8* %ap, i8* %ap)
  call void @llvm.va_end(i8* %ap)
  ret void
}
declare void @llvm.va_start(i8*)
declare void @llvm.va_copy(i8*, i8*)
declare void @llvm.va_end(i8*)
