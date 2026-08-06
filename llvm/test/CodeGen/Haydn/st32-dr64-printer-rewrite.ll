; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — AsmPrinter must emit `st64` (not `st32`) when the store's data operand is a DR64 register (D0-D15).

; REGRESSION TEST: AsmPrinter must emit `st64` (not `st32`) when the store's
; data operand is a DR64 register (D0-D15).
;
; Bug (GAP-VEC-COMPLEX-MATH-MATOP.md / section D, /MC Fix-1): the
; selector sometimes selects ST32 with a DR64 source (a CodeGen-vs-printer
; mismatch — the selector's `ValTy.getSizeInBits==64 ? ST64 : ST32` heuristic
; at HaydnInstructionSelector.cpp:1986 misfires on some vector-intrinsic
; stores routed through haydn_dsp.h, seen in vec_elemult16x16, vec_add16x16
; vec_cplx2real_mult16x16, vec_cplxconj16x16, vec_rsqrt_16x16, vec_sqrt_16x16
; mtx_vecmpy16x16, vec_scale16x16, vec_eleabs16x16). The AsmPrinter's regular
; path emitted `st32 d0` for the DR64 source, but the asm parser's ST32 only
; accepts GPR32, breaking `llc | llvm-mc` round-trip.
;
; Printer no longer rewrites ST32→ST64. Selector must emit ST64 for
; i64 stores. A bad ST32+DR64 MIR fails closed (fatal) instead of silent rewrite.
;
; Guard: CHECK st64 + CHECK-NOT st32 dN for the scalar i64 store path.
;
; Test design: a simple i64 store produces a clean `st64` (the selector
; correctly picks ST64 for a 64-bit scalar store). The regression check is
; any DR64 store via the buggy selector path would emit `st32 dN`.

define void @store_i64(ptr %p, i64 %v) #0 {
entry:
  store i64 %v, ptr %p, align 8
  ret void
}

attributes #0 = { nounwind }

; CHECK-LABEL: store_i64:
; CHECK: st64
; CHECK-NOT: st32 {{d[0-9]}}
