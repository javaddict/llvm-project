; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=haydn-postlegalizer-combiner -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
;
; Role: semantic — DR64 pair-load promotion (bkfir report §7 ask #3).
;
; REGRESSION TEST: ordinary-C software-pack of a DR64 operand from two
; same-base adjacent i32 loads must become ONE DR64 pair load, not
; 2x ld32 + sext32t64/slli64 + or64.
;
; Bug: pure/v4 bkfir C forms every DR64 operand as
;   d01 = ((i64)(long)p[i+1] << 32) | (u64)(u32)p[i]
; which the backend lowered as two scalar GPR loads plus a multi-op ALU
; pack (ld32 + sext32t64 + slli64 + or64 = 2 LS slots + 4 ALU slots per
; operand). The AE C bodies get one D_LDW* pair load per AE_L32X2_XC
; intrinsic, so the memory side of the FIR loop diverged by 5 issue slots
; per DR operand.
; Fix: post-legalizer combiner rule form_pair_load (HaydnCombine.td +
; matchCombinePairLoad/applyCombinePairLoad) rewrites the pack idiom to a
; single s64 access at the pair base; the existing AGU rule in the same
; pass folds the address (G_HAYDN_PREINC_LOAD) and selection emits the
; golden D_LDW* pair load (here d_ldw_pre_reg / d_ldw_pre_imm), under the
; same golden 8-byte EA alignment law the intrinsic path uses.
;
; Test design:
; @pair_pack is the report's idiom with an 8-aligned pair (variable base,
; low word at the base, high at +4) — the form the pack combine owns.
; @pair_pack_const_off is the constant-offset spelling (pair at p+8).
; The pair must be 8-byte aligned in IR: the golden D_LDW* law requires
; EA 8-aligned, and the selector's LD64 path splits to 2x LD32 otherwise
; (see unaligned-i64-postinc.ll). If the combine regresses, MIR shows
; G_SEXT/G_ZEXT/G_SHL feeding a G_OR and ASM shows sext32t64/or64.
;
; If this test regresses: do not relax the CHECKs — the pack idiom is
; being missed (matcher too narrow) or mis-widened (apply bug); see the
; closed conditions in matchCombinePairLoad (HaydnPostLegalizerCombiner.cpp).

define i64 @pair_pack(ptr %p, i32 %i) {
; MIR-LABEL: name: pair_pack
; MIR: G_HAYDN_PREINC_LOAD {{.*}}(s64)
; MIR-NOT: G_OR
; MIR-NOT: G_SHL
; ASM-LABEL: pair_pack:
; ASM: d_ldw_pre_reg
; ASM-NOT: sext32t64
; ASM-NOT: or64
entry:
  %pi = getelementptr i32, ptr %p, i32 %i
  %plo = getelementptr i32, ptr %pi, i32 0
  %phi = getelementptr i32, ptr %pi, i32 1
  %lo = load i32, ptr %plo, align 8
  %hi = load i32, ptr %phi, align 8
  %lo64 = zext i32 %lo to i64
  %hi64 = sext i32 %hi to i64
  %his = shl i64 %hi64, 32
  %d = or i64 %his, %lo64
  ret i64 %d
}

; Constant-offset spelling: pair at p+8, words at +8/+12. Offset 8 =
; imm6 1 in the golden D_LDW* RI6 law (EA = rs + (imm6 << 3)).
define i64 @pair_pack_const_off(ptr %p) {
; MIR-LABEL: name: pair_pack_const_off
; MIR: G_HAYDN_PREINC_LOAD {{.*}}(s64)
; MIR-NOT: G_OR
; ASM-LABEL: pair_pack_const_off:
; ASM: d_ldw_pre_imm {{.*}}, 1
; ASM-NOT: sext32t64
; ASM-NOT: or64
entry:
  %plo = getelementptr i32, ptr %p, i32 2
  %phi = getelementptr i32, ptr %p, i32 3
  %lo = load i32, ptr %plo, align 8
  %hi = load i32, ptr %phi, align 8
  %lo64 = zext i32 %lo to i64
  %hi64 = zext i32 %hi to i64
  %his = shl i64 %hi64, 32
  %d = or i64 %his, %lo64
  ret i64 %d
}
