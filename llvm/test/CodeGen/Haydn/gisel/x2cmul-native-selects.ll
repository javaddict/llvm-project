; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -enable-misched=false \
; RUN:     -enable-post-misched=false -stop-after=instruction-select \
; RUN:     -o - %s | FileCheck %s
;
; Role: semantic — native X2CMUL ISA stays selectable. Public AE wrappers
; (AE_CMUL32*_F2 / AE_MULFC24* / AE_MULC32X16_*) stay fail-closed in
; haydn_dsp.h until the five-file golden set supplies an ordering rule.
; This pin is the ISA side of that split: do not reject the hardware op.

declare { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2cmul32s(<2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2cmul32.f2(<2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2cmul32s.f2(<2 x i32>, <2 x i32>)

define i64 @native_x2cmul32(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: name: native_x2cmul32
; CHECK: X2CMUL32
  %va = bitcast i64 %a to <2 x i32>
  %vb = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32> %va, <2 x i32> %vb)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @native_x2cmul32s(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: name: native_x2cmul32s
; CHECK: X2CMUL32S
  %va = bitcast i64 %a to <2 x i32>
  %vb = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32s(<2 x i32> %va, <2 x i32> %vb)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @native_x2cmul32_f2(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: name: native_x2cmul32_f2
; CHECK: X2CMUL32{{.*}}F2
  %va = bitcast i64 %a to <2 x i32>
  %vb = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32.f2(<2 x i32> %va, <2 x i32> %vb)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @native_x2cmul32s_f2(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: name: native_x2cmul32s_f2
; CHECK: X2CMUL32S{{.*}}F2
  %va = bitcast i64 %a to <2 x i32>
  %vb = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32s.f2(<2 x i32> %va, <2 x i32> %vb)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}
