; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; Role: semantic — R_GD/R_GG logicals (NSA*/POPCOUNT64/HMAX*/LOG2) must emit
; via setDesc member materialize; residual logical shells are fatal at
; pack/printer under residual-executable law.

declare i32 @llvm.haydn.nsa64(i64)
declare i32 @llvm.haydn.nsa32.l(i64)
declare i32 @llvm.haydn.popcount64(i64)
declare i32 @llvm.haydn.log2(i32)

define i32 @nsa64_emit(i64 %a) {
; CHECK-LABEL: nsa64_emit:
; CHECK: nsa64
; CHECK: jalr{{.*}}lr
entry:
  %r = call i32 @llvm.haydn.nsa64(i64 %a)
  ret i32 %r
}

define i32 @nsa32_l_emit(i64 %a) {
; CHECK-LABEL: nsa32_l_emit:
; CHECK: nsa32_l
; CHECK: jalr{{.*}}lr
entry:
  %r = call i32 @llvm.haydn.nsa32.l(i64 %a)
  ret i32 %r
}

define i32 @popcount64_emit(i64 %a) {
; CHECK-LABEL: popcount64_emit:
; CHECK: popcount64
; CHECK: jalr{{.*}}lr
entry:
  %r = call i32 @llvm.haydn.popcount64(i64 %a)
  ret i32 %r
}

define i32 @log2_emit(i32 %a) {
; CHECK-LABEL: log2_emit:
; CHECK: log2
; CHECK: jalr{{.*}}lr
entry:
  %r = call i32 @llvm.haydn.log2(i32 %a)
  ret i32 %r
}

declare i32 @llvm.haydn.x2hmax32(<2 x i32>)
declare i32 @llvm.haydn.x2hmin32(<2 x i32>)
declare i32 @llvm.haydn.x4hmax16(<4 x i16>)
declare i32 @llvm.haydn.x4hmin16(<4 x i16>)

define i32 @x2hmax32_emit(<2 x i32> %a) {
; CHECK-LABEL: x2hmax32_emit:
; CHECK: x2hmax32
; CHECK: jalr{{.*}}lr
entry:
  %r = call i32 @llvm.haydn.x2hmax32(<2 x i32> %a)
  ret i32 %r
}

define i32 @x2hmin32_emit(<2 x i32> %a) {
; CHECK-LABEL: x2hmin32_emit:
; CHECK: x2hmin32
; CHECK: jalr{{.*}}lr
entry:
  %r = call i32 @llvm.haydn.x2hmin32(<2 x i32> %a)
  ret i32 %r
}

define i32 @x4hmax16_emit(<4 x i16> %a) {
; CHECK-LABEL: x4hmax16_emit:
; CHECK: x4hmax16
; CHECK: jalr{{.*}}lr
entry:
  %r = call i32 @llvm.haydn.x4hmax16(<4 x i16> %a)
  ret i32 %r
}

define i32 @x4hmin16_emit(<4 x i16> %a) {
; CHECK-LABEL: x4hmin16_emit:
; CHECK: x4hmin16
; CHECK: jalr{{.*}}lr
entry:
  %r = call i32 @llvm.haydn.x4hmin16(<4 x i16> %a)
  ret i32 %r
}
