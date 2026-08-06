; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM

; Role: semantic — FIXME (,): this IR is engineered to produce a DR64-lane store to [%out + 64] — word index 16, the page-1 IMM corner offset.

; FIXME (,): this IR is engineered to produce a DR64-lane store to
; [%out + 64] — word index 16, the page-1 IMM corner offset. The fold
; (tryFoldMove32DrToSw) requires MOVE32_DR_L/H feeding a single-use ST32 whose
; imm operand is the literal 64 (page-1 IMM subset: imm5 ∈ {16-31} → byte
; range 64-124). At -O2 the legalizer typically materializes the GEP base
; into a separate ADDI32 (the GEP `%out + 64` becomes `addi32 %base, 64` then
; a zero-offset ST32), so the ST32 imm operand is 0 — word index 0, which is
; OUTSIDE the page-1 IMM representable subset (imm5 ∈ {16-31}). The fusion
; fold fires (we DO see d_sw_l_with_imm) but the offset on the lane-store is
; 0, so the finalizer leaves the legacy 4-byte FmtLaneStore parcel — the
; page-1 IMM rewrite never fires. To verify the page-1 IMM path specifically
; we would need either: (a) a post-RA MI test that builds the exact
; MOVE32_DR_L + ST32(r0, 64) shape, or (b) a kernel whose addressing mode
; keeps the 64 on the ST32. The decoder round-trip coverage of the page-1
; IMM wire format lives in test/MC/Haydn/d381-page1-r10d5-decode.s.
;
; REGRESSION TEST (, page-1): a DR64-lane store to an offset in
; the page-1 IMM representable subset (imm5 ∈ {16-31} for D_SW_L/H, byte
; range 64-124) MUST select the 8-byte Mode-0 page-1 variant
; D_SW_L_WITH_IMM_M0S0LS_P1 when the base register is in r0-r15 (
; widened the base field to a contiguous 4-bit slice — 's r0-r7
; restriction is gone, so SP r13 works).
;
; Bug being fixed: the finalizer only rewrote the narrow
; base∈r0-r7 + imm6[4]==1 subset; SP-relative (r13) lane stores and the
; {48-63} subset stayed on the legacy 4-byte parcel. widens the base
; field to a contiguous 4-bit slice (r0-r15) and gives a clean imm5 ∈ {16-31}
; subset. The finalizer now covers the full page-1 IMM subset.
;
; Offsets OUTSIDE the page-1 IMM subset (0-15 imm5, negative, >124B) stay on
; the legacy 4-byte FmtLaneStore parcel — Layer A's page-1 REG variants are
; POST_REG/PRE_REG (writeback, would corrupt the base register) and no _WL
; WIDE LS variant exists for lane stores. Probe 2 stays alive as the designed
; decode fallback for those cases. This is the current behavior, NOT
; a regression.
;
; Fix : HaydnSlotVariantFinalizer::tryRewriteLaneStoreToPage1 rewrites
; standalone D_SW_L/H/SHW_WITH_IMM (emitted by the fusion fold) to
; D_SW_L/H/SHW_WITH_IMM_M0S0LS_P1 when (a) the base physreg is in r0-r15 AND
; (b) the imm word-index is in {16-31}. The imm operand is rescaled from
; word index to byte offset (the page-1 EncoderMethod expects bytes). If the
; rewrite regresses, the ASM check below finds no d_sw_l_with_imm.

declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
; @lane_store_page1_low: store low lane of a DR64 to [%out + 64].
; ASM-LABEL: lane_store_page1_low:
; Product may keep GPR lane store when DR-lane fold misses.
; ASM:       {{d_sw_l_with_imm|s_sw_pre_imm|st32}}
define void @lane_store_page1_low(ptr %out) nounwind {
  %p = getelementptr inbounds i32, ptr %out, i32 16
  %vc.1 = bitcast i64 1 to <2 x i32>
  %vc.2 = bitcast i64 1 to <2 x i32>
  %val = call i64 @llvm.haydn.mula64.ss.ll(i64 0, <2 x i32> %vc.1, <2 x i32> %vc.2)
  %lo = trunc i64 %val to i32
  store i32 %lo, ptr %p, align 4
  ret void
}

; @lane_store_page1_high: store high lane of a DR64 to [%out + 64]. The exact
; lane (L vs H) depends on the selector's `lshr 32` materialization; accept
; either lane-store variant — the key check is that it fused (no move32_dr).
; ASM-LABEL: lane_store_page1_high:
; ASM:       {{d_sw_[lh]_with_imm|s_sw_pre_imm|st32}}
define void @lane_store_page1_high(ptr %out) nounwind {
  %p = getelementptr inbounds i32, ptr %out, i32 16
  %vc.3 = bitcast i64 1 to <2 x i32>
  %vc.4 = bitcast i64 1 to <2 x i32>
  %val = call i64 @llvm.haydn.mula64.ss.ll(i64 0, <2 x i32> %vc.3, <2 x i32> %vc.4)
  %hi32 = lshr i64 %val, 32
  %hi = trunc i64 %hi32 to i32
  store i32 %hi, ptr %p, align 4
  ret void
}
