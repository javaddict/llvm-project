; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
;
; REGRESSION TEST : MOVE32_DR_L/H + ST32 (extract a DR64 lane to a GPR
; then store that GPR) must fold into the single lane-store instruction
; D_SW_L_WITH_IMM / D_SW_H_WITH_IMM — store one 32-bit lane of a DR64 directly
; to memory, skipping the GPR32 intermediate.
;
; Bug being fixed: the Haydn ISA defines lane-store instructions
;   D_SW_L_WITH_IMM: mem32[rs + (imm6<<2)] = rtd[31:00]
;   D_SW_H_WITH_IMM: mem32[rs + (imm6<<2)] = rtd[63:32]
; (Database/haydn_instruction_db.json), but they were asm-only with no codegen
; or encoding support. Every DR64-lane-to-memory store paid 2 instructions
; (MOVE32_DR_L/H + ST32), costing cross-bank extracts across the corpus.
;
; Fix : define D_SW_L/H_WITH_IMM via FmtLaneStore (real encoding bits)
; add encoder entries, and add a post-select combine that detects the
; MOVE32_DR_L/H → ST32 pair (when the GPR feeds only the store) and replaces
; it with the fused lane-store. If the combine regresses, the ASM check loses
; d_sw_l_with_imm and falls back to move32_dr_l + st32.

declare i64 @llvm.haydn.mula64.ss.ll(i64, i64, i64)

; @lane_store_low: store low 32 bits of a DR64 value.
; ASM-LABEL: lane_store_low:
; ASM: d_sw_l_with_imm
; ASM-NOT: move32_dr_l
define void @lane_store_low(ptr %out) nounwind {
  %val = call i64 @llvm.haydn.mula64.ss.ll(i64 0, i64 1, i64 1)
  %lo = trunc i64 %val to i32
  store i32 %lo, ptr %out, align 4
  ret void
}

; @lane_store_high: store high 32 bits of a DR64 value. The exact lane
; (L vs H) depends on how the selector materializes the `lshr 32` — accept
; either lane-store variant; the key check is that it's fused (no move32_dr).
; ASM-LABEL: lane_store_high:
; ASM: d_sw_{{l|h}}_with_imm
; ASM-NOT: move32_dr
define void @lane_store_high(ptr %out) nounwind {
  %val = call i64 @llvm.haydn.mula64.ss.ll(i64 0, i64 1, i64 1)
  %hi32 = lshr i64 %val, 32
  %hi = trunc i64 %hi32 to i32
  store i32 %hi, ptr %out, align 4
  ret void
}

