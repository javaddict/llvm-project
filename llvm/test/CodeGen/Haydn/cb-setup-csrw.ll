; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: Circular-buffer setup (setcbr_begin / setcbr_end) must
; lower to `csrw <addr>, rs` and must NOT be DCE'd.
;
; Bug: WUR_AE_CBEGIN0/CEND0 in haydn_dsp.h were no-op macros
; (`#define WUR_AE_CBEGIN0(val) ((void)(val))`), and there was no instruction
; builtin, or intrinsic that could write a CBR boundary register. Every
; circular-buffer load/store therefore wrapped against unconfigured
; (garbage) CBR boundaries — silently wrong on hardware
; (research/circular-buffer-cross-arch-study §2).
;
; Fix: Haydn has no dedicated SETCBR opcode; the CBR boundaries ARE CSRs
; (Rev 2 manual: 2 sets, CBR_BEGIN/CBR_END at CSR 0x2C/0x2D for set 0 and
; 0x2E/0x2F for set 1; CBR_SIZE removed). The setcbr_begin/end intrinsics
; lower to SETCBR_BEGIN/END pseudos, which the AsmPrinter expands to
; `csrw <addr>, rs`. This mirrors Hexagon's `m0=rN; cs0=rN` boundary setup.
; If this regresses, the csrw vanishes and CB loads wrap against garbage.
;
; Test design: call the setup intrinsics then a CB load. The intrinsic is
; side-effecting (IntrHasSideEffects | IntrWriteMem) so it cannot be DCE'd;
; we CHECK that both `csrw` (setup) and `d_ldw_cb_imm` (consumer) appear.
; The cbr_sel immediate drives the CSR address (set 0 -> 0x2C/0x2D).

declare void @llvm.haydn.setcbr.begin(i32, i32)
declare void @llvm.haydn.setcbr.end(i32, i32)
declare { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr, i32, i32)

; Setup CBR set 0, then a CB load on set 0. The setup must survive.
; CSR addresses: CBR_BEGIN[0]=0x2C=44, CBR_END[0]=0x2D=45.
; CHECK-LABEL: test_cb_setup_set0:
; CHECK:       {{.*}}csrw{{(_[pP][23][0-9]_[A-Z0-9]+)?}} 44, {{r[0-9]+}}{{.*}}
; CHECK:       {{.*}}csrw{{(_[pP][23][0-9]_[A-Z0-9]+)?}} 45, {{r[0-9]+}}{{.*}}
; CHECK:       d_ldw_cb_imm
define i64 @test_cb_setup_set0(i32 %begin, i32 %end, ptr %ptr) {
  call void @llvm.haydn.setcbr.begin(i32 0, i32 %begin)
  call void @llvm.haydn.setcbr.end(i32 0, i32 %end)
  %d_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %ptr, i32 0, i32 1)
  %d = extractvalue { i64, ptr } %d_pair, 0
  ret i64 %d
}

; Setup CBR set 1 — must use the set-1 CSR addresses 0x2E=46/0x2F=47.
; CHECK-LABEL: test_cb_setup_set1:
; CHECK:       {{.*}}csrw{{(_[pP][23][0-9]_[A-Z0-9]+)?}} 46, {{r[0-9]+}}{{.*}}
; CHECK:       {{.*}}csrw{{(_[pP][23][0-9]_[A-Z0-9]+)?}} 47, {{r[0-9]+}}{{.*}}
define void @test_cb_setup_set1(i32 %begin, i32 %end) {
  call void @llvm.haydn.setcbr.begin(i32 1, i32 %begin)
  call void @llvm.haydn.setcbr.end(i32 1, i32 %end)
  ret void
}
