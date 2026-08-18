; RUN: rm -rf %t && split-file %s %t
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -o /dev/null %t/direct_gv.ll 2>&1 | FileCheck %s --check-prefix=TLSGV
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -o /dev/null %t/intrinsic.ll 2>&1 | FileCheck %s --check-prefix=TLSINTRIN
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -o /dev/null %t/gep.ll 2>&1 | FileCheck %s --check-prefix=TLSGEP
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -o - %t/control.ll | FileCheck %s --check-prefix=CTL
;
; Role: semantic — T-ABI10 TLS fail-closed rows. Baremetal Haydn has no TLS
; model (Clang TLSSupported=false; no TLS relocations on any admitted Format E
; row), so a thread_local global has no defined address equation. Both raw-IR
; TLS entry forms must fail closed with a named diagnostic before any address
; materialization, never silently lower.
;
; REGRESSION TEST: direct G_GLOBAL_VALUE on a thread_local global used to
; select as an ordinary global — llc emitted `lui %hi12(tls_gv); addi32
; %lo20(tls_gv)`, i.e. the TLS *template* address, and exited 0. That is
; silent wrong code for every thread at runtime. The intrinsic form was
; fail-closed only by accident with a generic "cannot select" G_INTRINSIC
; crash message.
;
; Fix: HaydnInstructionSelector rejects both forms with one named
; reportGISelFailure diagnostic (RISCV rejects G_GLOBAL_VALUE on TLS the same
; way, RISCVInstructionSelector.cpp selectGConstantOrFrameIndex tail). Hard
; constraint #1: -global-isel-abort=1 turns FailedISel into a hard error.
;
; Test design: direct_gv exercises a bare load of a thread_local global
; (G_GLOBAL_VALUE path); intrinsic exercises the explicit
; @llvm.threadlocal.address call; gep puts the TLS global behind a
; getelementptr so the G_GLOBAL_VALUE still reaches selection; control
; loads a non-TLS global and must still compile with the ordinary
; LOAD_ADDR materialization. If the guard is dropped, direct_gv/gep compile
; silently (TLSGV/TLSGEP lose) and control still passes — the CHECK-NOT in
; CTL additionally proves no %hi12(tls_gv) leaks into ordinary functions.

;--- direct_gv.ll
@tls_gv = thread_local global i32 7

define i32 @rd_direct() {
  ; TLSGV: LLVM ERROR: TLS global address: baremetal Haydn has no TLS model
  %v = load i32, ptr @tls_gv
  ret i32 %v
}

;--- intrinsic.ll
@tls_gv2 = thread_local global i32 7
declare ptr @llvm.threadlocal.address.p0(ptr)

define i32 @rd_intrinsic() {
  ; TLSINTRIN: LLVM ERROR: llvm.threadlocal.address: baremetal Haydn has no TLS model
  %p = call ptr @llvm.threadlocal.address.p0(ptr @tls_gv2)
  %v = load i32, ptr %p
  ret i32 %v
}

;--- gep.ll
@tls_arr = thread_local global [4 x i32] zeroinitializer

define i32 @rd_gep(i32 %i) {
  ; TLSGEP: LLVM ERROR: TLS global address: baremetal Haydn has no TLS model
  %e = getelementptr [4 x i32], ptr @tls_arr, i32 0, i32 %i
  %v = load i32, ptr %e
  ret i32 %v
}

;--- control.ll
@plain_gv = global i32 7

define i32 @rd_plain() {
; CTL-LABEL: rd_plain:
; CTL: lui
; CTL: addi32
; CTL-NOT: tls_gv
  %v = load i32, ptr @plain_gv
  ret i32 %v
}
