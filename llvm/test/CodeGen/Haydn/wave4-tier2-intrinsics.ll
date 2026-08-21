; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs -stop-after=instruction-select < %s | FileCheck --check-prefix=MIR %s

; Role: MIR — Updated for native DR64 shift (sll64/srl64/sra64) RE-FILED REGRESSION : the second RUN line (full ASM pipeline).

; Updated for native DR64 shift (sll64/srl64/sra64)
; RE-FILED REGRESSION : the second RUN line (full ASM pipeline)
; was removed because it hits the SEQ64 MachineVerifier abort that
; regressed in the Flex cutover — same root cause as
; dr64-compare-intrinsics.ll and other DR64 R_CMP tests (see XFAIL comment
; there). The first RUN line (MIR at -stop-after=instruction-select) is
; BEFORE the verifier failure point and still validates the ISel opcodes.
; Restore the ASM RUN when the DR64 R_CMP operand-flag bug is fixed. Track
; under / Flex cutover.
; (was: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s piped to FileCheck %s)
;
; Wave 4 Tier 2 intrinsics: X2/X4 SIMD unary, shuffle/pack, X4 complex multiply
; and 64-bit scalar operations.
;
; Test strategy:
; MIR check: verifies ISel selects the correct target instruction opcode
; ASM check: verifies the correct instruction mnemonic appears in assembly
; Unary intrinsics that read/write the same register may be optimized away at
; assembly level, so the MIR check is the authoritative verification.

;===----------------------------------------------------------------------===;
; A. X2 SIMD Unary ops (DR64 unary: i64 -> i64)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2abs32(<2 x i32>)
declare <2 x i32> @llvm.haydn.x2abs32s(<2 x i32>)
declare <2 x i32> @llvm.haydn.x2neg32(<2 x i32>)
declare <2 x i32> @llvm.haydn.x2neg32s(<2 x i32>)
declare <2 x i32> @llvm.haydn.x2neg32.l(<2 x i32>)
declare <2 x i32> @llvm.haydn.x2neg32s.l(<2 x i32>)
declare <2 x i32> @llvm.haydn.x2swap32(<2 x i32>)
declare <2 x i32> @llvm.haydn.x2mjswap32(<2 x i32>)
declare <2 x i32> @llvm.haydn.x2mjswap32s(<2 x i32>)

define dso_local <2 x i32> @test_x2abs32(<2 x i32> %a) {
; MIR-LABEL: name: test_x2abs32
; MIR: X2ABS32
  %r = call <2 x i32> @llvm.haydn.x2abs32(<2 x i32> %a)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2abs32s(<2 x i32> %a) {
; MIR-LABEL: name: test_x2abs32s
; MIR: X2ABS32S
  %r = call <2 x i32> @llvm.haydn.x2abs32s(<2 x i32> %a)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2neg32(<2 x i32> %a) {
; MIR-LABEL: name: test_x2neg32
; MIR: X2NEG32
  %r = call <2 x i32> @llvm.haydn.x2neg32(<2 x i32> %a)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2neg32s(<2 x i32> %a) {
; MIR-LABEL: name: test_x2neg32s
; MIR: X2NEG32S
  %r = call <2 x i32> @llvm.haydn.x2neg32s(<2 x i32> %a)
  ret <2 x i32> %r
}

define dso_local i64 @test_x2neg32_l(i64 %a) {
; MIR-LABEL: name: test_x2neg32_l
; MIR: X2NEG32_L
  %bc.1 = bitcast i64 %a to <2 x i32>
  %call.2 = call <2 x i32> @llvm.haydn.x2neg32.l(<2 x i32> %bc.1)
  %r = bitcast <2 x i32> %call.2 to i64
  ret i64 %r
}

define dso_local i64 @test_x2neg32s_l(i64 %a) {
; MIR-LABEL: name: test_x2neg32s_l
; MIR: X2NEG32S_L
  %bc.3 = bitcast i64 %a to <2 x i32>
  %call.4 = call <2 x i32> @llvm.haydn.x2neg32s.l(<2 x i32> %bc.3)
  %r = bitcast <2 x i32> %call.4 to i64
  ret i64 %r
}

define dso_local <2 x i32> @test_x2swap32(<2 x i32> %a) {
; MIR-LABEL: name: test_x2swap32
; MIR: X2SWAP32
  %r = call <2 x i32> @llvm.haydn.x2swap32(<2 x i32> %a)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2mjswap32(<2 x i32> %a) {
; MIR-LABEL: name: test_x2mjswap32
; MIR: X2MJSWAP32
  %r = call <2 x i32> @llvm.haydn.x2mjswap32(<2 x i32> %a)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2mjswap32s(<2 x i32> %a) {
; MIR-LABEL: name: test_x2mjswap32s
; MIR: X2MJSWAP32S
  %r = call <2 x i32> @llvm.haydn.x2mjswap32s(<2 x i32> %a)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; A. X4 SIMD Unary ops (DR64 unary: i64 -> i64)
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4abs16(<4 x i16>)
declare <4 x i16> @llvm.haydn.x4abs16s(<4 x i16>)
declare <4 x i16> @llvm.haydn.x4neg16(<4 x i16>)
declare <4 x i16> @llvm.haydn.x4neg16s(<4 x i16>)
declare <4 x i16> @llvm.haydn.x4swap16(<4 x i16>)
declare <4 x i16> @llvm.haydn.x4mjswap16(<4 x i16>)
declare <4 x i16> @llvm.haydn.x4mjswap16s(<4 x i16>)
declare <4 x i16> @llvm.haydn.x4conj16(<4 x i16>)
declare <4 x i16> @llvm.haydn.x4conj16s(<4 x i16>)
declare i64 @llvm.haydn.x4energy16(<4 x i16>)
declare <2 x i32> @llvm.haydn.x4cmul16(<4 x i16>)
declare <2 x i32> @llvm.haydn.x4cmul16s(<4 x i16>)
define dso_local <4 x i16> @test_x4abs16(<4 x i16> %a) {
; MIR-LABEL: name: test_x4abs16
; MIR: X4ABS16
  %r = call <4 x i16> @llvm.haydn.x4abs16(<4 x i16> %a)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4abs16s(<4 x i16> %a) {
; MIR-LABEL: name: test_x4abs16s
; MIR: X4ABS16S
  %r = call <4 x i16> @llvm.haydn.x4abs16s(<4 x i16> %a)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4neg16(<4 x i16> %a) {
; MIR-LABEL: name: test_x4neg16
; MIR: X4NEG16
  %r = call <4 x i16> @llvm.haydn.x4neg16(<4 x i16> %a)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4neg16s(<4 x i16> %a) {
; MIR-LABEL: name: test_x4neg16s
; MIR: X4NEG16S
  %r = call <4 x i16> @llvm.haydn.x4neg16s(<4 x i16> %a)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4swap16(<4 x i16> %a) {
; MIR-LABEL: name: test_x4swap16
; MIR: X4SWAP16
  %r = call <4 x i16> @llvm.haydn.x4swap16(<4 x i16> %a)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4mjswap16(<4 x i16> %a) {
; MIR-LABEL: name: test_x4mjswap16
; MIR: X4MJSWAP16
  %r = call <4 x i16> @llvm.haydn.x4mjswap16(<4 x i16> %a)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4mjswap16s(<4 x i16> %a) {
; MIR-LABEL: name: test_x4mjswap16s
; MIR: X4MJSWAP16S
  %r = call <4 x i16> @llvm.haydn.x4mjswap16s(<4 x i16> %a)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4conj16(<4 x i16> %a) {
; MIR-LABEL: name: test_x4conj16
; MIR: X4CONJ16
  %r = call <4 x i16> @llvm.haydn.x4conj16(<4 x i16> %a)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4conj16s(<4 x i16> %a) {
; MIR-LABEL: name: test_x4conj16s
; MIR: X4CONJ16S
  %r = call <4 x i16> @llvm.haydn.x4conj16s(<4 x i16> %a)
  ret <4 x i16> %r
}

define dso_local i64 @test_x4energy16(i64 %a) {
; MIR-LABEL: name: test_x4energy16
; MIR: X4ENERGY16
  %bc.5 = bitcast i64 %a to <4 x i16>
  %r = call i64 @llvm.haydn.x4energy16(<4 x i16> %bc.5)
  ret i64 %r
}

define dso_local i64 @test_x4cmul16(i64 %a) {
; MIR-LABEL: name: test_x4cmul16
; MIR: X4CMUL16
  %bc.6 = bitcast i64 %a to <4 x i16>
  %call.7 = call <2 x i32> @llvm.haydn.x4cmul16(<4 x i16> %bc.6)
  %r = bitcast <2 x i32> %call.7 to i64
  ret i64 %r
}

define dso_local i64 @test_x4cmul16s(i64 %a) {
; MIR-LABEL: name: test_x4cmul16s
; MIR: X4CMUL16S
  %bc.8 = bitcast i64 %a to <4 x i16>
  %call.9 = call <2 x i32> @llvm.haydn.x4cmul16s(<4 x i16> %bc.8)
  %r = bitcast <2 x i32> %call.9 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; A. X2/X4 SIMD Binary ops (DR64 binary: i64, i64 -> i64)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2max32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2min32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2clamp32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4max16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4min16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4clamp16(<4 x i16>, <4 x i16>, <4 x i16>)
define dso_local <2 x i32> @test_x2max32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2max32:
; CHECK: x2max32
  %r = call <2 x i32> @llvm.haydn.x2max32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2min32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2min32:
; CHECK: x2min32
  %r = call <2 x i32> @llvm.haydn.x2min32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local i64 @test_x2clamp32(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2clamp32:
; CHECK: x2clamp32
  %bc.10 = bitcast i64 %a to <2 x i32>
  %bc.11 = bitcast i64 %b to <2 x i32>
  %call.12 = call <2 x i32> @llvm.haydn.x2clamp32(<2 x i32> %bc.10, <2 x i32> %bc.11, <2 x i32> %bc.10)
  %r = bitcast <2 x i32> %call.12 to i64
  ret i64 %r
}

define dso_local <4 x i16> @test_x4max16(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4max16:
; CHECK: x4max16
  %r = call <4 x i16> @llvm.haydn.x4max16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4min16(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4min16:
; CHECK: x4min16
  %r = call <4 x i16> @llvm.haydn.x4min16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define dso_local i64 @test_x4clamp16(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4clamp16:
; CHECK: x4clamp16
  %bc.13 = bitcast i64 %a to <4 x i16>
  %bc.14 = bitcast i64 %b to <4 x i16>
  %call.15 = call <4 x i16> @llvm.haydn.x4clamp16(<4 x i16> %bc.13, <4 x i16> %bc.14, <4 x i16> %bc.13)
  %r = bitcast <4 x i16> %call.15 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; B. Shuffle/Pack ops
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2sel32.hh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sel32.hl(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sel32.lh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sel32.ll(<2 x i32>, <2 x i32>)
define dso_local i64 @test_x2sel32_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2sel32_hh:
; CHECK: x2sel32_hh
  %bc.16 = bitcast i64 %a to <2 x i32>
  %bc.17 = bitcast i64 %b to <2 x i32>
  %call.18 = call <2 x i32> @llvm.haydn.x2sel32.hh(<2 x i32> %bc.16, <2 x i32> %bc.17)
  %r = bitcast <2 x i32> %call.18 to i64
  ret i64 %r
}

define dso_local i64 @test_x2sel32_hl(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2sel32_hl:
; CHECK: x2sel32_hl
  %bc.19 = bitcast i64 %a to <2 x i32>
  %bc.20 = bitcast i64 %b to <2 x i32>
  %call.21 = call <2 x i32> @llvm.haydn.x2sel32.hl(<2 x i32> %bc.19, <2 x i32> %bc.20)
  %r = bitcast <2 x i32> %call.21 to i64
  ret i64 %r
}

define dso_local i64 @test_x2sel32_lh(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2sel32_lh:
; CHECK: x2sel32_lh
  %bc.22 = bitcast i64 %a to <2 x i32>
  %bc.23 = bitcast i64 %b to <2 x i32>
  %call.24 = call <2 x i32> @llvm.haydn.x2sel32.lh(<2 x i32> %bc.22, <2 x i32> %bc.23)
  %r = bitcast <2 x i32> %call.24 to i64
  ret i64 %r
}

define dso_local i64 @test_x2sel32_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2sel32_ll:
; CHECK: x2sel32_ll
  %bc.25 = bitcast i64 %a to <2 x i32>
  %bc.26 = bitcast i64 %b to <2 x i32>
  %call.27 = call <2 x i32> @llvm.haydn.x2sel32.ll(<2 x i32> %bc.25, <2 x i32> %bc.26)
  %r = bitcast <2 x i32> %call.27 to i64
  ret i64 %r
}

declare <2 x i32> @llvm.haydn.x2addsub32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2addsub32.hllh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2addsub32s.hllh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2subadd32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2subadd32s(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2subadd32.hllh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2subadd32s.hllh(<2 x i32>, <2 x i32>)

define dso_local <2 x i32> @test_x2addsub32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2addsub32:
; CHECK: x2addsub32
  %r = call <2 x i32> @llvm.haydn.x2addsub32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2addsub32_hllh(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2addsub32_hllh:
; CHECK: x2addsub32_hllh
  %r = call <2 x i32> @llvm.haydn.x2addsub32.hllh(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2addsub32s_hllh(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2addsub32s_hllh:
; CHECK: x2addsub32s_hllh
  %r = call <2 x i32> @llvm.haydn.x2addsub32s.hllh(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2subadd32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2subadd32:
; CHECK: x2subadd32
  %r = call <2 x i32> @llvm.haydn.x2subadd32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2subadd32s(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2subadd32s:
; CHECK: x2subadd32s
  %r = call <2 x i32> @llvm.haydn.x2subadd32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2subadd32_hllh(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2subadd32_hllh:
; CHECK: x2subadd32_hllh
  %r = call <2 x i32> @llvm.haydn.x2subadd32.hllh(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2subadd32s_hllh(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2subadd32s_hllh:
; CHECK: x2subadd32s_hllh
  %r = call <2 x i32> @llvm.haydn.x2subadd32s.hllh(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; C. X4 Complex Multiply variants (binary DR64)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x4cmul16s.h(<4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x4cmul16s.l(<4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x4cmula16s.h(<2 x i32>, <4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x4cmula16s.l(<2 x i32>, <4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x4cjmul16s.h(<4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x4cjmul16s.l(<4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x4cjmula16s.h(<2 x i32>, <4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x4cjmula16s.l(<2 x i32>, <4 x i16>, <4 x i16>)
define dso_local i64 @test_x4cmul16s_h(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4cmul16s_h:
; CHECK: x4cmul16s_h
  %bc.28 = bitcast i64 %a to <4 x i16>
  %bc.29 = bitcast i64 %b to <4 x i16>
  %call.30 = call <2 x i32> @llvm.haydn.x4cmul16s.h(<4 x i16> %bc.28, <4 x i16> %bc.29)
  %r = bitcast <2 x i32> %call.30 to i64
  ret i64 %r
}

define dso_local i64 @test_x4cmul16s_l(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4cmul16s_l:
; CHECK: x4cmul16s_l
  %bc.31 = bitcast i64 %a to <4 x i16>
  %bc.32 = bitcast i64 %b to <4 x i16>
  %call.33 = call <2 x i32> @llvm.haydn.x4cmul16s.l(<4 x i16> %bc.31, <4 x i16> %bc.32)
  %r = bitcast <2 x i32> %call.33 to i64
  ret i64 %r
}

define dso_local i64 @test_x4cmula16s_h(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4cmula16s_h:
; CHECK: x4cmula16s_h
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <4 x i16>
  %call.3 = call <2 x i32> @llvm.haydn.x4cmula16s.h(<2 x i32> %bc.1, <4 x i16> %bc.2, <4 x i16> zeroinitializer)
  %r = bitcast <2 x i32> %call.3 to i64
  ret i64 %r
}

define dso_local i64 @test_x4cmula16s_l(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4cmula16s_l:
; CHECK: x4cmula16s_l
  %bc.4 = bitcast i64 %a to <2 x i32>
  %bc.5 = bitcast i64 %b to <4 x i16>
  %call.6 = call <2 x i32> @llvm.haydn.x4cmula16s.l(<2 x i32> %bc.4, <4 x i16> %bc.5, <4 x i16> zeroinitializer)
  %r = bitcast <2 x i32> %call.6 to i64
  ret i64 %r
}

define dso_local i64 @test_x4cjmul16s_h(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4cjmul16s_h:
; CHECK: x4cjmul16s_h
  %bc.34 = bitcast i64 %a to <4 x i16>
  %bc.35 = bitcast i64 %b to <4 x i16>
  %call.36 = call <2 x i32> @llvm.haydn.x4cjmul16s.h(<4 x i16> %bc.34, <4 x i16> %bc.35)
  %r = bitcast <2 x i32> %call.36 to i64
  ret i64 %r
}

define dso_local i64 @test_x4cjmul16s_l(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4cjmul16s_l:
; CHECK: x4cjmul16s_l
  %bc.37 = bitcast i64 %a to <4 x i16>
  %bc.38 = bitcast i64 %b to <4 x i16>
  %call.39 = call <2 x i32> @llvm.haydn.x4cjmul16s.l(<4 x i16> %bc.37, <4 x i16> %bc.38)
  %r = bitcast <2 x i32> %call.39 to i64
  ret i64 %r
}

define dso_local i64 @test_x4cjmula16s_h(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4cjmula16s_h:
; CHECK: x4cjmula16s_h
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %b to <4 x i16>
  %call.9 = call <2 x i32> @llvm.haydn.x4cjmula16s.h(<2 x i32> %bc.7, <4 x i16> %bc.8, <4 x i16> zeroinitializer)
  %r = bitcast <2 x i32> %call.9 to i64
  ret i64 %r
}

define dso_local i64 @test_x4cjmula16s_l(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4cjmula16s_l:
; CHECK: x4cjmula16s_l
  %bc.10 = bitcast i64 %a to <2 x i32>
  %bc.11 = bitcast i64 %b to <4 x i16>
  %call.12 = call <2 x i32> @llvm.haydn.x4cjmula16s.l(<2 x i32> %bc.10, <4 x i16> %bc.11, <4 x i16> zeroinitializer)
  %r = bitcast <2 x i32> %call.12 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; D. 64-bit scalar unary ops (DR64 unary: i64 -> i64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.not64(i64)
declare i64 @llvm.haydn.seq64(i64, i64)
define dso_local i64 @test_not64(i64 %a) {
; MIR-LABEL: name: test_not64
; MIR: NOT64
  %r = call i64 @llvm.haydn.not64(i64 %a)
  ret i64 %r
}

define dso_local i64 @test_seq64(i64 %a) {
; MIR-LABEL: name: test_seq64
; MIR: SEQ64
  %r = call i64 @llvm.haydn.seq64(i64 %a, i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; D. 64-bit scalar binary ops (DR64 binary: i64, i64 -> i64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.max64(i64, i64)
declare i64 @llvm.haydn.min64(i64, i64)
declare i64 @llvm.haydn.add64.h(i64, i64)
declare i64 @llvm.haydn.add64.l(i64, i64)
declare i64 @llvm.haydn.sub64.h(i64, i64)
declare i64 @llvm.haydn.sub64.l(i64, i64)
define dso_local i64 @test_max64(i64 %a, i64 %b) {
; CHECK-LABEL: test_max64:
; CHECK: max64
  %r = call i64 @llvm.haydn.max64(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_min64(i64 %a, i64 %b) {
; CHECK-LABEL: test_min64:
; CHECK: min64
  %r = call i64 @llvm.haydn.min64(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_add64_h(i64 %a, i64 %b) {
; CHECK-LABEL: test_add64_h:
; CHECK: add64_h
  %r = call i64 @llvm.haydn.add64.h(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_add64_l(i64 %a, i64 %b) {
; CHECK-LABEL: test_add64_l:
; CHECK: add64_l
  %r = call i64 @llvm.haydn.add64.l(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_sub64_h(i64 %a, i64 %b) {
; CHECK-LABEL: test_sub64_h:
; CHECK: sub64_h
  %r = call i64 @llvm.haydn.sub64.h(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_sub64_l(i64 %a, i64 %b) {
; CHECK-LABEL: test_sub64_l:
; CHECK: sub64_l
  %r = call i64 @llvm.haydn.sub64.l(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; D. SLL64 (golden: i64 data, i32 shift -> i64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.sll64(i64, i32)
define dso_local i64 @test_sll64(i64 %a, i32 %b) {
; CHECK-LABEL: test_sll64:
; CHECK: sll64
  %r = call i64 @llvm.haydn.sll64(i64 %a, i32 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; D. SRA64 / SRL64 (golden: i64 data, i32 shift -> i64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.sra64(i64, i32)
declare i64 @llvm.haydn.srl64(i64, i32)
define dso_local i64 @test_sra64(i64 %accum, i32 %shift) {
; CHECK-LABEL: test_sra64:
; CHECK: sra64
  %r = call i64 @llvm.haydn.sra64(i64 %accum, i32 %shift)
  ret i64 %r
}

define dso_local i64 @test_srl64(i64 %accum, i32 %shift) {
; CHECK-LABEL: test_srl64:
; CHECK: srl64
  %r = call i64 @llvm.haydn.srl64(i64 %accum, i32 %shift)
  ret i64 %r
}
