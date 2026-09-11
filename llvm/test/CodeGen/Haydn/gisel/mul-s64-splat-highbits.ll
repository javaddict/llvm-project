; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=legalizer -verify-machineinstrs < %s \
; RUN:     | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM

; Role: MIR — T-GS2 + T-GS3: mul i64 %x, 4294967297 with high bits set is
; schoolbook G_HAYDN_MUL64_WIDENU, not splat and not MUL64_* at legalize.
;
; REGRESSION TEST: T-GS2 delete unsound s64 mul splat + LIBCALL_MUL64 selector
; arms; T-GS3 legalizer emits target-generic widening, not MUL64_ULUL.
;
; Bug: selector treated mul X, 0x100000001 as lane-replicate (MOV_GPR_TO_DR64
; of the low 32 bits into both lanes). That is only sound when X's high 32
; bits are zero. Residual G_MUL s64 is legalizer schoolbook of three unsigned
; widening muls. After T-GS3 those are G_HAYDN_MUL64_WIDENU, not MUL64_ULUL
; (legalizer must not construct selected target opcodes). Instruction-select
; maps G_HAYDN_MUL64_WIDENU to MUL64_ULUL (ASM prefix below).
; If the splat returns, this file has no G_HAYDN_MUL64_WIDENU.

; CHECK-LABEL: name: mul_splat_const_high_bits
; CHECK-NOT: LIBCALL_MUL64
; CHECK-NOT: MUL64_LL
; CHECK-NOT: MUL64_ULUL
; CHECK: G_HAYDN_MUL64_WIDENU
; CHECK: G_HAYDN_MUL64_WIDENU
; CHECK: G_HAYDN_MUL64_WIDENU
;
; ASM-LABEL: mul_splat_const_high_bits:
; ASM: mul64.ulul
; ASM: mul64.ulul
; ASM: mul64.ulul
; ASM-NOT: {{lui|addi32|jal}}{{.*}}__muldi3

define i64 @mul_splat_const_high_bits(i64 %x) {
  %y = or i64 %x, 4294967296
  %r = mul i64 %y, 4294967297
  ret i64 %r
}
