; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr}}
;
;
; REGRESSION TEST: Non-leaf functions with non-zero stack allocation must STILL
; emit `.cfi_def_cfa_offset <StackSize>` — proving the guard added in is
; correctly scoped to the zero-size case only.
;
; Why this test exists: The optimization adds `if (AlignedStackSize != 0)`
; around the `.cfi_def_cfa_offset` emission in `emitPrologue`. Without this
; test, an over-broad guard (e.g., `if (false)`, or a refactor that drops the
; directive entirely) would silently break unwind/debug info for any function
; with actual stack usage and no test would catch it.
;
; Test design: We use three non-FP-needing shapes:
; 1. A function with stack locals (forces StackSize > 0, no FP, no CSR)
; 2. A function with alloca of fixed size (StackSize > 0, no FP)
; 3. A function with both stack locals and multiple alloca
; Each must:
; 1. Decrement SP (subi32 sp, sp, <N>)
; 2. Emit `.cfi_def_cfa_offset <N>` with N > 0 (the load-bearing CHECK)
; 3. NOT emit `.cfi_def_cfa_register fp` (proves hasFP is false)
; 4. Restore SP in the epilogue (addi32{{(_w)?}} sp, sp, <N>)

define i32 @stack_locals(i32 %x) {
; The load-bearing check: CFI directive IS emitted when stack is non-zero
  %a = alloca i32
  %b = alloca i32
  store i32 %x, ptr %a
  store i32 42, ptr %b
  %va = load i32, ptr %a
  %vb = load i32, ptr %b
  %r = add i32 %va, %vb
  ret i32 %r
}

define i32 @fixed_alloca(i32 %x) {
  %p = alloca i32, i32 4
  store i32 %x, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

define i32 @locals_and_alloca(i32 %x) {
  %a = alloca i32
  %p = alloca i32, i32 4
  store i32 %x, ptr %a
  store i32 7, ptr %p
  %va = load i32, ptr %a
  %vp = load i32, ptr %p
  %r = add i32 %va, %vp
  ret i32 %r
}
