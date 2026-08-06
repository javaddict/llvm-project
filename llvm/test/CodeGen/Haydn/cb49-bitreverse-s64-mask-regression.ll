; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — 64-bit bitreverse must use UNSIGNED byte masks (0x00000000FF000000) for the bswap stage, not sign-extended masks.

; REGRESSION TEST : 64-bit bitreverse must use UNSIGNED byte masks
; (0x00000000FF000000) for the bswap stage, not sign-extended masks
; (0xFFFFFFFFFF000000).
;
; Bug: the generic LegalizerHelper::lowerBswap (LegalizerHelper.cpp:9716)
; computes its byte masks as `0xFF << (i*8)` in SIGNED int arithmetic. For
; i=3 this is signed overflow -> -16777216, which the APInt(64,...) call
; sign-extends to 0xFFFFFFFFFF000000 instead of the correct unsigned
; 0x00000000FF000000. The wrong mask drops the innermost byte pair (bytes
; 3,4) of the 8-byte bswap, so 64-bit bitreverse (bswap + intra-byte swaps)
; miscompiles whenever those two bytes differ between the halves
; (6-stage SWAR bitreverse of 0xFEDCBA9876543210, expected byte 7 = 0x08
; sim returned 0x3B). The low-32 bits were correct because they do not
; involve the i=3 byte mask.
;
; Per project constraint #0 (never modify upstream LLVM), the fix lives in
; HaydnLegalizerInfo::legalizeCustom: G_BSWAP and G_BITREVERSE on s64 are
; custom-lowered with uint64_t byte masks. s16/s32 still use the generic
; path (no i=3 mask exists for sizes < 64 bits, so the bug cannot fire).
;
; LOAD-BEARING CHECKS (asm level -- the decisive guard is the companion
; MIR test cb49-bitreverse-s64-mask-legalizer.mir; this asm test is a
; secondary, end-to-end guard):
;
; The Haydn s64-constant materializer builds an s64 value as two 32-bit
; lanes (low lane, high lane via move32_dr_h); see s64-constant-opt.ll and
; large-imm.ll. When hi32 == 0xFFFFFFFF (the BUGGY mask 0xFFFFFFFFFF000000)
; the high lane is materialised with `addi32 rN, r0, -1` (= 0xFFFFFFFF). When
; hi32 == 0 (the FIXED mask 0x00000000FF000000), the high lane is zero and
; no `addi32..., -1` is needed for it. No other mask in this lowering has
; hi32 == 0xFFFFFFFF:
; byte masks 0xFF / 0xFF00 / 0xFF0000 / 0xFF000000 -> hi32 == 0
; SwapN masks 0xF0F0.. / 0xCCCC.. / 0xAAAA.. -> hi32 == 0xF0F0F0F0
; 0xCCCCCCCC / 0xAAAAAAAA, never 0xFFFFFFFF.
; This function is minimal (bitreverse.i64 then ret), so there is no other
; source of an `addi32..., -1` constant. The CHECK-NOT: addi32..., -1
; below therefore FIRES if the byte-3 mask regresses to 0xFFFFFFFFFF000000.
;
; The positive CHECKs confirm the bswap lowering actually ran: the custom
; lowerer emits s64 shifts by 56, 40, 24, 8 (ShiftVal = 56 - 16*I, I=0..3)
; via G_SHL/G_LSHR, which the selector maps to native DR64 shifts sll64
; srl64. The intra-byte SwapN stages use shifts 4, 2, 1. The bswap
; NEVER uses a 32-bit half-swap (no sll64/srl64 by 32).

define dso_local i64 @cb49_bitreverse_i64(i64 %x) noinline {
; CHECK-LABEL: cb49_bitreverse_i64:

; Positive: native DR64 shifts prove the s64 bswap lowering ran.
; CHECK: sll64
; CHECK: srl64

; The CHECK-NOT below fires if byte-3's mask is sign-extended to
; 0xFFFFFFFFFF000000 (the high lane would be materialised as -1).
; CHECK-NOT: addi32 {{r[0-9]+}}, {{r[0-9]+}}, -1

  %r = call i64 @llvm.bitreverse.i64(i64 %x)
  ret i64 %r
}

declare i64 @llvm.bitreverse.i64(i64)
