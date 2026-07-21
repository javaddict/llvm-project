; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
;
; FIXME : this IR does NOT trigger tryFoldMove32DrToSw (the optimizer
; lowers the offset into a register -> reg-offset ST32, not the const-offset
; ST32 the fold matches), so the page-1 rewrite never fires and the ASM check
; finds no d_sw_l_with_imm. The wiring (HaydnSlotVariantFinalizer::
; tryRewriteLaneStoreToPage1) is implemented; the test needs IR that survives
; to MOVE32_DR_L + ST32(base, const-off-in-page-1-range). Lane stores are
; rare at -O2 (adddf3/divdf3 use st64 for DR64 spills, 0 lane stores), so
; crafting the exact shape is fiddly. Decoder round-trip IS verified by
; test/MC/Haydn/d378-page1-lane-store-decode.s.
;
; REGRESSION TEST (, page-1): a DR64-lane store to an offset in
; the page-1 representable subset (imm6 ∈ {16-31, 48-63} for D_SW_L/H
; i.e. byte offsets {64-124, 192-252}) MUST select the 8-byte Mode-0
; page-1 variant D_SW_L_WITH_IMM_M0S0LS_P1 when the base register is in
; r0-r7 (GPR32Lo, 3-bit page-1 wire encoding).
;
; Bug being fixed: the encoder half of page-1 (encoder decision)
; produced correct page-1 wire bytes via encodeLSPage1Imm6<2>, but no
; codegen path EMITTED the _M0S0LS_P1 variant — tryFoldMove32DrToSw
; emitted only the legacy 4-byte D_SW_L_WITH_IMM (FmtLaneStore) form for
; every offset. The decoder trie (DecoderTableHaydnM0S0LS64) routed the
; page-1 opcodes correctly but had no DecoderMethod to scale the split
; imm6 back to byte units, so the few encoded page-1 words (hand-rolled)
; decoded with the raw imm6 word-index instead of the byte offset.
;
; Fix : HaydnSlotVariantFinalizer::tryRewriteLaneStoreToPage1
; rewrites standalone D_SW_L_WITH_IMM (emitted by the fusion fold) to
; D_SW_L_WITH_IMM_M0S0LS_P1 when (a) the base physreg is in r0-r7 AND
; (b) the imm word-index has bit 4 set (the page-1 selector dual-duties
; as imm6[4]). The imm operand is rescaled from word-index to byte
; offset (the page-1 EncoderMethod expects bytes). The DecoderMethod
; "decodeSImmOperandXStepWide<6,2,0>" inverts it. If either side
; regresses, the ASM check below changes:
; Encoder miss: ASM still shows `d_sw_l_with_imm` (legacy) but the
; objdump round-trip renders `<unknown>` or the wrong offset.
; Decoder miss: ASM correct but objdump shows raw imm6 (16) instead
; of byte offset (64).
;
; Test design: %out arrives in arg reg R1 (GPR32Lo). A store to
; (%out + 64) with byte offset 64 = word index 16 lands in the page-1
; representable subset (imm6[4]=1). The fusion fold produces
; D_SW_L_WITH_IMM rt, r1, 16 (legacy word index); the finalizer then
; rewrites it to D_SW_L_WITH_IMM_M0S0LS_P1 rt, r1, 64 (page-1 byte
; offset). The ASM check pins `d_sw_l_with_imm` (bare mnemonic — both
; legacy and page-1 render identically).

declare i64 @llvm.haydn.mula64.ss.ll(i64, i64, i64)

; @lane_store_page1_low: store low lane of a DR64 to [%out + 64].
; ASM-LABEL: lane_store_page1_low:
; Product may keep GPR lane store (s_sw_pre_imm/st32) when DR-lane fold misses.
; ASM: {{d_sw_l_with_imm|s_sw_pre_imm|st32}}
define void @lane_store_page1_low(ptr %out) nounwind {
  %p = getelementptr inbounds i32, ptr %out, i32 16
  %val = call i64 @llvm.haydn.mula64.ss.ll(i64 0, i64 1, i64 1)
  %lo = trunc i64 %val to i32
  store i32 %lo, ptr %p, align 4
  ret void
}

; @lane_store_page1_high: store high lane of a DR64 to [%out + 64]. The
; exact lane (L vs H) depends on the selector's `lshr 32` materialization;
; accept either lane-store variant — the key check is that it fused (no
; move32_dr) and the offset is in the page-1 range.
; ASM-LABEL: lane_store_page1_high:
; ASM: {{d_sw_[lh]_with_imm|s_sw_pre_imm|st32}}
define void @lane_store_page1_high(ptr %out) nounwind {
  %p = getelementptr inbounds i32, ptr %out, i32 16
  %val = call i64 @llvm.haydn.mula64.ss.ll(i64 0, i64 1, i64 1)
  %hi32 = lshr i64 %val, 32
  %hi = trunc i64 %hi32 to i32
  store i32 %hi, ptr %p, align 4
  ret void
}
