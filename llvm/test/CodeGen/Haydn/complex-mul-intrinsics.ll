; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -stop-after=instruction-select < %s | FileCheck %s --check-prefix=MIR

; Role: MIR — Complex multiply intrinsics: mnemonic + two-def MIR order
; (result 0 = first def/rtd1, result 1 = second def/rtd2).

; Golden instruction_type_index (RRR):
;   X2CMUL32 / X2CMUL32S: rtd1 = real, rtd2 = imag (format-1 packing).
;   X2CMUL32_F2 / X2CMUL32S_F2: rtd1 = imag, rtd2 = real (format-2 packing).
; Intrinsic {i64,i64} extractvalue 0/1 must bind to those defs in order.
; X4FCMUL* are single-result binary DR64 ops.

; If any intrinsic fails to lower, llc will crash with -global-isel-abort=1.

;Quad 16-bit complex multiply (binary DR64)

; ASM-LABEL: test_x4fcmul16rs:
; ASM: x4fcmul16rs
; MIR-LABEL: name: test_x4fcmul16rs
; MIR: X4FCMUL16RS

define i64 @test_x4fcmul16rs(i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <4 x i16>
  %bc.2 = bitcast i64 %b to <4 x i16>
  %call.3 = call <4 x i16> @llvm.haydn.x4fcmul16rs(<4 x i16> %bc.1, <4 x i16> %bc.2)
  %r = bitcast <4 x i16> %call.3 to i64
  ret i64 %r
}

; ASM-LABEL: test_x4fcmula16rs:
; ASM: x4fcmula16rs
; MIR-LABEL: name: test_x4fcmula16rs
; MIR: X4FCMULA16RS
; Ternary accumulator (acc, a, b) — reads rtd per DB.
define i64 @test_x4fcmula16rs(i64 %acc, i64 %a, i64 %b) {
  %bc.4 = bitcast i64 %acc to <4 x i16>
  %bc.5 = bitcast i64 %a to <4 x i16>
  %bc.6 = bitcast i64 %b to <4 x i16>
  %call.7 = call <4 x i16> @llvm.haydn.x4fcmula16rs(<4 x i16> %bc.4, <4 x i16> %bc.5, <4 x i16> %bc.6)
  %r = bitcast <4 x i16> %call.7 to i64
  ret i64 %r
}

; ASM-LABEL: test_x4fcmul16rss:
; ASM: x4fcmul16rss
; MIR-LABEL: name: test_x4fcmul16rss
; MIR: X4FCMUL16RSS
define i64 @test_x4fcmul16rss(i64 %a, i64 %b) {
  %bc.8 = bitcast i64 %a to <4 x i16>
  %bc.9 = bitcast i64 %b to <4 x i16>
  %call.10 = call <4 x i16> @llvm.haydn.x4fcmul16rss(<4 x i16> %bc.8, <4 x i16> %bc.9)
  %r = bitcast <4 x i16> %call.10 to i64
  ret i64 %r
}

; ASM-LABEL: test_x4fcmula16rss:
; ASM: x4fcmula16rss
; MIR-LABEL: name: test_x4fcmula16rss
; MIR: X4FCMULA16RSS
; Ternary accumulator form.
define i64 @test_x4fcmula16rss(i64 %acc, i64 %a, i64 %b) {
  %bc.11 = bitcast i64 %acc to <4 x i16>
  %bc.12 = bitcast i64 %a to <4 x i16>
  %bc.13 = bitcast i64 %b to <4 x i16>
  %call.14 = call <4 x i16> @llvm.haydn.x4fcmula16rss(<4 x i16> %bc.11, <4 x i16> %bc.12, <4 x i16> %bc.13)
  %r = bitcast <4 x i16> %call.14 to i64
  ret i64 %r
}

; Dual 32-bit complex multiply — two-def DR64.
; extractvalue 0 -> first def (rtd1); extractvalue 1 -> second def (rtd2).
; Returning both forces both defs live so order cannot be silently dropped.

; ASM-LABEL: test_x2cmul32:
; ASM: x2cmul32
; MIR-LABEL: name: test_x2cmul32
; MIR: {{%.*}}:dr64, {{%.*}}:dr64 = X2CMUL32
define { i64, i64 } @test_x2cmul32(i64 %a, i64 %b) {
  %bc.15 = bitcast i64 %a to <2 x i32>
  %bc.16 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32> %bc.15, <2 x i32> %bc.16)
  ret { i64, i64 } %r
}

; ASM-LABEL: test_x2cmul32s:
; ASM: x2cmul32s
; MIR-LABEL: name: test_x2cmul32s
; MIR: {{%.*}}:dr64, {{%.*}}:dr64 = X2CMUL32S
define { i64, i64 } @test_x2cmul32s(i64 %a, i64 %b) {
  %bc.17 = bitcast i64 %a to <2 x i32>
  %bc.18 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32s(<2 x i32> %bc.17, <2 x i32> %bc.18)
  ret { i64, i64 } %r
}

; Format-2 packing: same def order (rtd1, rtd2) but swapped part meaning.
; ASM-LABEL: test_x2cmul32_f2:
; ASM: x2cmul32.f2
; MIR-LABEL: name: test_x2cmul32_f2
; MIR: {{%.*}}:dr64, {{%.*}}:dr64 = X2CMUL32_F2
define { i64, i64 } @test_x2cmul32_f2(i64 %a, i64 %b) {
  %bc.19 = bitcast i64 %a to <2 x i32>
  %bc.20 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32.f2(<2 x i32> %bc.19, <2 x i32> %bc.20)
  ret { i64, i64 } %r
}

; ASM-LABEL: test_x2cmul32s_f2:
; ASM: x2cmul32s.f2
; MIR-LABEL: name: test_x2cmul32s_f2
; MIR: {{%.*}}:dr64, {{%.*}}:dr64 = X2CMUL32S_F2
define { i64, i64 } @test_x2cmul32s_f2(i64 %a, i64 %b) {
  %bc.21 = bitcast i64 %a to <2 x i32>
  %bc.22 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32s.f2(<2 x i32> %bc.21, <2 x i32> %bc.22)
  ret { i64, i64 } %r
}

; Pin extractvalue 0 uses the first MIR def (rtd1): only the real part of
; non-F2 X2CMUL32 is returned, so the second def may die — still must select
; the two-def opcode with first def live into the return.
; ASM-LABEL: test_x2cmul32_real_only:
; ASM: x2cmul32
; MIR-LABEL: name: test_x2cmul32_real_only
; MIR: {{%.*}}:dr64, {{%.*}}:dr64 = X2CMUL32
define i64 @test_x2cmul32_real_only(i64 %a, i64 %b) {
  %bc.23 = bitcast i64 %a to <2 x i32>
  %bc.24 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32> %bc.23, <2 x i32> %bc.24)
  %real = extractvalue { i64, i64 } %r, 0
  ret i64 %real
}

; extractvalue 1 alone must still select the two-def form (second def = rtd2).
; ASM-LABEL: test_x2cmul32_imag_only:
; ASM: x2cmul32
; MIR-LABEL: name: test_x2cmul32_imag_only
; MIR: {{%.*}}:dr64, {{%.*}}:dr64 = X2CMUL32
define i64 @test_x2cmul32_imag_only(i64 %a, i64 %b) {
  %bc.25 = bitcast i64 %a to <2 x i32>
  %bc.26 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32> %bc.25, <2 x i32> %bc.26)
  %imag = extractvalue { i64, i64 } %r, 1
  ret i64 %imag
}

declare <4 x i16> @llvm.haydn.x4fcmul16rs(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fcmula16rs(<4 x i16>, <4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fcmul16rss(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fcmula16rss(<4 x i16>, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2cmul32s(<2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2cmul32.f2(<2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2cmul32s.f2(<2 x i32>, <2 x i32>)
