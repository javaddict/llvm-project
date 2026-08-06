; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — VASTART must survive dead-mi-elimination so the AsmPrinter can expand it; without hasSideEffects the pseudo is DCE'd at -O1/-O2 before.

; REGRESSION TEST: VASTART must survive dead-mi-elimination so the AsmPrinter
; can expand it; without hasSideEffects the pseudo is DCE'd at -O1/-O2 before
; AsmPrinter expansion and the va_list struct is never initialized, so va_arg
; reads garbage (returns 0 instead of the variadic value).
;
; Bug: the VASTART/VACOPY pseudos in HaydnInstrInfo.td had no register defs
; no Uses, no memoperands, and hasSideEffects defaulted to 0. At -O1/-O2 the
; dead-mi-elimination pass therefore treated them as side-effect-free dead code
; and deleted them before the AsmPrinter ran. The va_list struct was left
; uninitialized; va_arg then walked random offsets and returned 0. At -O0 the
; pass is less aggressive so the pseudo survived, masking the bug.
;
; Fix: wrap VASTART/VACOPY in `let hasSideEffects = 1 in {... }` (mirroring
; the CSRW precedent — CSRW without hasSideEffects had the identical DCE trap
; for CSR programming). VAEND is intentionally NOT marked: it is a genuine
; no-op on baremetal and SHOULD stay DCE-able.
;
; Test design: a variadic function that reads one int va_arg and returns it.
; The AsmPrinter's VASTART expansion stores five words into the va_list struct
; at field offsets 0,4,8,12,16 (__stack,__gr_top,__vr_top,__gr_offs,__vr_offs)
; using `st32 rN, rVaListPtr, <FieldOff>`. If the pseudo were DCE'd, NONE of
; these stores would appear and the function body would have no va_list init.
; The CHECKs below assert that at least the @0, @4, and @12 field stores are
; emitted — i.e. VASTART was expanded, not deleted. Compiling at -O1 (the
; default for llc's mid-level pipeline) is what triggers dead-mi-elimination
; so this test reproduces the bug at the exact pass where it occurred.

define dso_local i32 @vone(i32 noundef %n, ...) nounwind {
; CHECK-LABEL: vone:
; Check that VASTART was expanded: the AsmPrinter emits st32 stores into the
; va_list struct at the AArch64-style field offsets. Presence of the @0
; (__stack), @4 (__gr_top), and @12 (__gr_offs) stores proves the pseudo
; survived dead-mi-elimination and reached AsmPrinter expansion. If the pseudo
; were DCE'd, none of these stores would exist.
; CHECK: st32 {{r[0-9]+}}, {{r[0-9]+}}, 0
; CHECK: st32 {{r[0-9]+}}, {{r[0-9]+}}, 1
; CHECK: st32 {{r[0-9]+}}, {{r[0-9]+}}, 3
entry:
  %ap = alloca i8, i32 32, align 8
  %ap.p0 = bitcast i8* %ap to i8*
  call void @llvm.va_start(i8* %ap.p0)
  %v = va_arg i8* %ap.p0, i32
  call void @llvm.va_end(i8* %ap.p0)
  ret i32 %v
}

declare void @llvm.va_start(i8*)
declare void @llvm.va_end(i8*)
