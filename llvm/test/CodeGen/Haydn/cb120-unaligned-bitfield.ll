; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; BundleSim ALIGNMENT fault on S_LW_WITH_REG / S_LHWU_WITH_IMM when EA%4==1.
;
; Root: Haydn GISel treated G_LOAD/G_STORE as legal by SSA type only. Clang
; bitfield RMW lowers to `load/store i24, align 1` (and unaligned i16/i32);
; widenScalarToNextPow2 made an s32 anyext load of s24 mem "legal", and ISel
; emitted LD32/ST32 (S_LW_WITH_REG) at a byte address.
;
; Fix (peer: RISCVLegalizerInfo !enableUnalignedScalarMem):
; legalForTypesWithMemDesc requiring natural align + matching MemTy, then
; lowerIfMemSizeNotByteSizePow2 / lower so LegalizerHelper splits i24→i16+i8
; and unaligned i32/i16 into byte accesses. Selector keys LDU8/ST8 off MMO size.
;
; If this regresses: bf_rmw_i24 emits ld32/st32 again → BundleSim ALIGNMENT.

; i24 bitfield RMW must use byte loads/stores, never word
define void @bf_rmw_i24(ptr %p) {
; CHECK-LABEL: bf_rmw_i24:
; CHECK-NOT: s_lw_
; CHECK-NOT: s_sw_
; CHECK: s_lbu_{{[a-z_]*}}
; CHECK: s_sb_{{[a-z_]*}}
  %bf.load = load i24, ptr %p, align 1
  %bf.clear = and i24 %bf.load, -1048576
  %bf.set = or disjoint i24 %bf.clear, 989828
  store i24 %bf.set, ptr %p, align 1
  ret void
}

; unaligned i32 load at odd address: byte sequence, not ld32
define i32 @load_i32_align1(ptr %p) {
; CHECK-LABEL: load_i32_align1:
; CHECK-NOT: s_lw_
; CHECK: s_lbu_{{[a-z_]*}}
  %v = load i32, ptr %p, align 1
  ret i32 %v
}

; unaligned i32 store: byte stores, not st32
define void @store_i32_align1(ptr %p, i32 %v) {
; CHECK-LABEL: store_i32_align1:
; CHECK-NOT: s_sw_
; CHECK: s_sb_{{[a-z_]*}}
  store i32 %v, ptr %p, align 1
  ret void
}

; unaligned i16 (F2b half family): must not use ldu16/ld32 on odd EA
define i32 @load_i16_align1(ptr %p) {
; CHECK-LABEL: load_i16_align1:
; CHECK-NOT: s_lhwu_
; CHECK-NOT: s_lw_
; CHECK: s_lbu_{{[a-z_]*}}
  %v = load i16, ptr %p, align 1
  %z = zext i16 %v to i32
  ret i32 %z
}

; naturally aligned i32 still uses ld32
define i32 @load_i32_align4(ptr %p) {
; CHECK-LABEL: load_i32_align4:
; CHECK: s_lw_{{[a-z_]*}}
  %v = load i32, ptr %p, align 4
  ret i32 %v
}
