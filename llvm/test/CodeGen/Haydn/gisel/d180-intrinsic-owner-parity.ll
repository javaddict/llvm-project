; RUN: %python %S/d180-intrinsic-owner-parity.py --self-test
; RUN: %python %S/d180-intrinsic-owner-parity.py --llvm-src %S/../../../../.. | FileCheck %s --check-prefix=PROBE
; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 \
; RUN:     -enable-misched=false -enable-post-misched=false \
; RUN:     -stop-after=instruction-select -o - %s | FileCheck %s --check-prefix=ISEL
;
; Role: D1.80 — AIE2 G_INTRINSIC order (C++ ID switch, default selectImpl)
; plus generated td-Pat ↔ C++ 1:1 opcode parity. Generic 1:1 stays Pat-first.
;
; PROBE: D1.80 dispatch: C++-first
; PROBE: overlap=
; PROBE: opcode-parity: ok

declare i32 @llvm.haydn.add32s(i32, i32)
declare i32 @llvm.haydn.brev32(i32, i32)
declare i32 @llvm.haydn.slli32(i32, i32)
declare i64 @llvm.haydn.slt64(i64, i64)

; Dual-covered: C++ owns after invert; opcode matches the live Pat (ADD32S).
define i32 @d180_dual_add32s(i32 %a, i32 %b) nounwind {
; ISEL-LABEL: name: d180_dual_add32s
; ISEL: ADD32S
  %r = call i32 @llvm.haydn.add32s(i32 %a, i32 %b)
  ret i32 %r
}

; Pat-only: unmatched C++ ID must still hit selectImpl (AIE default).
define i32 @d180_pat_only_brev32(i32 %a, i32 %b) nounwind {
; ISEL-LABEL: name: d180_pat_only_brev32
; ISEL: BREV32
  %r = call i32 @llvm.haydn.brev32(i32 %a, i32 %b)
  ret i32 %r
}

; C++-only ImmArg: owned ID, no Pat fallback. In-range still selects.
define i32 @d180_cpp_only_slli32(i32 %a) nounwind {
; ISEL-LABEL: name: d180_cpp_only_slli32
; ISEL: SLLI32
  %r = call i32 @llvm.haydn.slli32(i32 %a, i32 5)
  ret i32 %r
}

; W_SIDE_EFFECTS C++-owned (SFR). Must hit selectIntrinsic, not the outer
; selectImpl default (D1.80 allowlist hole).
define i64 @d180_wse_slt64(i64 %a, i64 %b) nounwind {
; ISEL-LABEL: name: d180_wse_slt64
; ISEL: SLT64
  %r = call i64 @llvm.haydn.slt64(i64 %a, i64 %b)
  ret i64 %r
}
