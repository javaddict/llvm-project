; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs -o - %s \
; RUN:     | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %s | FileCheck %s --check-prefix=ASM
;
; Role: semantic — register-only musttail sibcall uses JAL_W_MSP (AIE2
; PseudoJ_TCO_jump_imm). rt is R12; incoming LR stays live. Soft tail
; stays ordinary JAL+RET. Ineligible musttail (byval/varargs/stack)
; stays fail-closed in tailcall-isr-fail-closed.ll.

declare void @callee(i32)
define void @caller(i32 %x) nounwind {
; ISEL-LABEL: name: caller
; ISEL: JAL_W_MSP
; ISEL-NOT: RET
; ASM-LABEL: caller:
; ASM: {{jal_w|jal}}{{.*}}r12
  musttail call void @callee(i32 %x)
  ret void
}

@fp = external global ptr
define void @caller_indirect(i32 %x) nounwind {
; ISEL-LABEL: name: caller_indirect
; ISEL: JALR_W_MSP
; ISEL-NOT: RET
  %f = load ptr, ptr @fp
  musttail call void (i32) %f(i32 %x)
  ret void
}

declare i32 @callee_i32(i32)
define i32 @caller_ret(i32 %x) nounwind {
; ISEL-LABEL: name: caller_ret
; ISEL: JAL_W_MSP
; ISEL-NOT: RET
; ASM-LABEL: caller_ret:
; ASM: {{jal_w|jal}}{{.*}}r12
  %r = musttail call i32 @callee_i32(i32 %x)
  ret i32 %r
}

define i32 @caller_with_local(i32 %x) nounwind {
; ISEL-LABEL: name: caller_with_local
; ISEL: JAL_W_MSP
; ISEL-NOT: RET
; ASM-LABEL: caller_with_local:
; ASM: {{jal_w|jal}}{{.*}}r12
  %p = alloca i32, align 4
  store i32 %x, ptr %p, align 4
  %v = load i32, ptr %p, align 4
  %r = musttail call i32 @callee_i32(i32 %v)
  ret i32 %r
}
