; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — D1.62 v2i32 ISels DR64-native end-to-end.
;
; The datalayout pins v64:64 (scalar i64:32 / f64:32 stay — separate ABI
; surface), so natural <2 x i32> IR mem carries an 8-byte-aligned MMO and:
;   - LD64/ST64 select directly (golden D_LDW_WITH_IMM / D_SDW_WITH_IMM
;     require exactly that 8-byte EA alignment);
;   - G_SELECT's 64-bit arm stays whole-register DR64 (MOVEGPR2SFR +
;     tied MOVT64) instead of unmerging to 2x GPR32 + dual MOVT32 +
;     MOV_GPR_TO_DR64 remerge.
; The legalizer Min AlignInBits=32 row still admits under-aligned
; (align-4) vector mem; the selector split path must keep firing there.

; --- Natural (align-8) vector load/store: single LD64/ST64, no split ---

define <2 x i32> @load_v2i32_natural(ptr %p) nounwind {
; CHECK-LABEL: load_v2i32_natural:
; CHECK: ld64
; CHECK-NOT: ld32
; CHECK-NOT: mov_gpr_to_dr64
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %v = load <2 x i32>, ptr %p
  ret <2 x i32> %v
}

define void @store_v2i32_natural(ptr %p, <2 x i32> %v) nounwind {
; CHECK-LABEL: store_v2i32_natural:
; CHECK: st64
; CHECK-NOT: st32
; CHECK-NOT: move32_dr
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  store <2 x i32> %v, ptr %p
  ret void
}

; --- G_SELECT on v2i32: DR64-native, no GPR unmerge/remerge ---

define <2 x i32> @select_v2i32(i32 %c, <2 x i32> %a, <2 x i32> %b) nounwind {
; CHECK-LABEL: select_v2i32:
; CHECK: andi32
; CHECK: neg32
; CHECK: movegpr2sfr
; CHECK: movt64
; CHECK-NOT: mov_dr64_to_gpr
; CHECK-NOT: mov_gpr_to_dr64
; CHECK-NOT: move32_dr
; CHECK-NOT: movt32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %tobool = icmp ne i32 %c, 0
  %r = select i1 %tobool, <2 x i32> %a, <2 x i32> %b
  ret <2 x i32> %r
}

; s64 G_SELECT uses the same DR64-native arm (one home, not a vector-only
; special case).
define i64 @select_s64_dr64_native(i32 %c, i64 %a, i64 %b) nounwind {
; CHECK-LABEL: select_s64_dr64_native:
; CHECK: movegpr2sfr
; CHECK: movt64
; CHECK-NOT: mov_dr64_to_gpr
; CHECK-NOT: mov_gpr_to_dr64
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %tobool = icmp ne i32 %c, 0
  %r = select i1 %tobool, i64 %a, i64 %b
  ret i64 %r
}

; --- Kernel shape: FFT butterfly accumulator lives on D0-D15, no GPR spill
; --- of the complex value (2x LD32 + pack must not reappear).

declare <2 x i32> @llvm.haydn.x2add32s(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2mulpl32(<2 x i32>, <2 x i32>)

define <2 x i32> @fft_butterfly_dr64(ptr %pi, ptr %pq, i32 %c) nounwind {
; CHECK-LABEL: fft_butterfly_dr64:
; CHECK: ld64
; CHECK: ld64
; CHECK: x2add32s
; CHECK: movt64
; CHECK-NOT: mov_dr64_to_gpr
; CHECK-NOT: mov_gpr_to_dr64
; CHECK-NOT: movt32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %x = load <2 x i32>, ptr %pi
  %y = load <2 x i32>, ptr %pq
  %t = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %x, <2 x i32> %y)
  %f = call <2 x i32> @llvm.haydn.x2mulpl32(<2 x i32> %x, <2 x i32> %y)
  %tobool = icmp ne i32 %c, 0
  %r = select i1 %tobool, <2 x i32> %t, <2 x i32> %f
  ret <2 x i32> %r
}

; --- Legality ladder must NOT regress: under-aligned (align-4) v2i32 stays
; --- legal and still splits to 4-byte halves (no LD64 on a 4-byte EA).

define <2 x i32> @load_v2i32_align4(ptr %p) nounwind {
; CHECK-LABEL: load_v2i32_align4:
; CHECK-NOT: ld64
; CHECK: ld32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %v = load <2 x i32>, ptr %p, align 4
  ret <2 x i32> %v
}

define void @store_v2i32_align4(ptr %p, <2 x i32> %v) nounwind {
; CHECK-LABEL: store_v2i32_align4:
; CHECK-NOT: st64
; CHECK: d_sw_l_with_imm
; CHECK: d_sw_h_with_imm
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  store <2 x i32> %v, ptr %p, align 4
  ret void
}

; align-1 vector store scalarizes to byte stores (guest-safe ladder).
define void @store_v2i32_align1(ptr %p, <2 x i32> %v) nounwind {
; CHECK-LABEL: store_v2i32_align1:
; CHECK-NOT: st64
; CHECK-NOT: d_sw_
; CHECK: st8
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  store <2 x i32> %v, ptr %p, align 1
  ret void
}

; --- Scalar i64 ABI alignment unchanged (i64:32): a 4-byte-aligned i64
; --- load/store still legalizes and splits — no LD64 on align-4 scalar.

define i64 @load_i64_align4_scalar_abi_unchanged(ptr %p) nounwind {
; CHECK-LABEL: load_i64_align4_scalar_abi_unchanged:
; CHECK-NOT: ld64
; CHECK: ld32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %v = load i64, ptr %p, align 4
  ret i64 %v
}

define void @store_i64_align4_scalar_abi_unchanged(ptr %p, i64 %v) nounwind {
; CHECK-LABEL: store_i64_align4_scalar_abi_unchanged:
; CHECK-NOT: st64
; CHECK: d_sw_l_with_imm
; CHECK: d_sw_h_with_imm
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  store i64 %v, ptr %p, align 4
  ret void
}
