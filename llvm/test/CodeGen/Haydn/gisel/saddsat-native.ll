; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -enable-misched=false \
; RUN:     -enable-post-misched=false -stop-after=instruction-select \
; RUN:     -o - %s | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %s | FileCheck %s --check-prefix=ASM
;
; Role: semantic — llvm.sadd.sat / llvm.ssub.sat select golden signed-sat
; encodings (ADD32S/SUB32S/ADD64S/SUB64S/X2ADD32S/X2SUB32S/X4ADD16S/
; X4SUB16S). AIE lowers these (AIE2LegalizerInfo.cpp:288-291); Haydn
; overlay matches Hexagon A2_addsat / RISCV QC_ADDSAT. i16 sat stays
; legalizer-lowered (no 16-bit scalar sat encoding). Unsigned sat has
; no ISA seat.

declare i32 @llvm.sadd.sat.i32(i32, i32)
declare i32 @llvm.ssub.sat.i32(i32, i32)
declare i64 @llvm.sadd.sat.i64(i64, i64)
declare i64 @llvm.ssub.sat.i64(i64, i64)
declare <2 x i32> @llvm.sadd.sat.v2i32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.ssub.sat.v2i32(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.sadd.sat.v4i16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.ssub.sat.v4i16(<4 x i16>, <4 x i16>)
declare i16 @llvm.sadd.sat.i16(i16, i16)

define i32 @saddsat_i32(i32 %a, i32 %b) nounwind {
; ISEL-LABEL: name: saddsat_i32
; ISEL: ADD32S
; ASM-LABEL: saddsat_i32:
; ASM: add32s
  %r = call i32 @llvm.sadd.sat.i32(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @ssubsat_i32(i32 %a, i32 %b) nounwind {
; ISEL-LABEL: name: ssubsat_i32
; ISEL: SUB32S
; ASM-LABEL: ssubsat_i32:
; ASM: sub32s
  %r = call i32 @llvm.ssub.sat.i32(i32 %a, i32 %b)
  ret i32 %r
}

define i64 @saddsat_i64(i64 %a, i64 %b) nounwind {
; ISEL-LABEL: name: saddsat_i64
; ISEL: ADD64S
; ASM-LABEL: saddsat_i64:
; ASM: add64s
  %r = call i64 @llvm.sadd.sat.i64(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @ssubsat_i64(i64 %a, i64 %b) nounwind {
; ISEL-LABEL: name: ssubsat_i64
; ISEL: SUB64S
; ASM-LABEL: ssubsat_i64:
; ASM: sub64s
  %r = call i64 @llvm.ssub.sat.i64(i64 %a, i64 %b)
  ret i64 %r
}

define <2 x i32> @saddsat_v2i32(<2 x i32> %a, <2 x i32> %b) nounwind {
; ISEL-LABEL: name: saddsat_v2i32
; ISEL: X2ADD32S
  %r = call <2 x i32> @llvm.sadd.sat.v2i32(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @ssubsat_v2i32(<2 x i32> %a, <2 x i32> %b) nounwind {
; ISEL-LABEL: name: ssubsat_v2i32
; ISEL: X2SUB32S
  %r = call <2 x i32> @llvm.ssub.sat.v2i32(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

define <4 x i16> @saddsat_v4i16(<4 x i16> %a, <4 x i16> %b) nounwind {
; ISEL-LABEL: name: saddsat_v4i16
; ISEL: X4ADD16S
  %r = call <4 x i16> @llvm.sadd.sat.v4i16(<4 x i16> %a, <4 x i16> %b)
  ret <4 x i16> %r
}

define <4 x i16> @ssubsat_v4i16(<4 x i16> %a, <4 x i16> %b) nounwind {
; ISEL-LABEL: name: ssubsat_v4i16
; ISEL: X4SUB16S
  %r = call <4 x i16> @llvm.ssub.sat.v4i16(<4 x i16> %a, <4 x i16> %b)
  ret <4 x i16> %r
}

define i16 @saddsat_i16_lowers(i16 %a, i16 %b) nounwind {
; ISEL-LABEL: name: saddsat_i16_lowers
; ISEL-NOT: ADD32S
; ISEL: RET
  %r = call i16 @llvm.sadd.sat.i16(i16 %a, i16 %b)
  ret i16 %r
}
