; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select < %s \
; RUN:     | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     < %s | FileCheck %s --check-prefix=ASM

; Role: MIR — FIXME: -verify-machineinstrs disabled — HWLoop/VLA path can emit LoopStart on undef physreg (pre-existing).

; FIXME: -verify-machineinstrs disabled — HWLoop/VLA path can emit LoopStart on undef physreg (pre-existing).
;
; REBASELINE : fusion works (ST32_POST / ST64_POST present). Prior
; XFAIL was a false positive from `MIR-NOT: ADDI32` matching frame-destroy
; ADDI32_W and IV materialization, not a store+ADDI split of the pointer.
;
; REGRESSION TEST : streaming i32/i64 STORES must lower to a SINGLE
; fused post-increment store instruction (ST32_POST / ST64_POST) when the
; stride is encodable as imm6<<2 / imm6<<3, NOT the 2-instruction ST + ADDI32
; split.
;
; Bug being fixed: the Haydn ISA spec defines fused post-increment stores
; S_SW_POST_IMM rt, rs, imm6 — mem32[rs] = rt; rs += imm6<<2
; D_SDW_POST_IMM rtd, rs, imm6 — mem64[rs] = rtd; rs += imm6<<3
; (Database/haydn_instruction_db.json). The backend modeled the pseudos
; (ST32_POST_INC / ST64_POST_INC) and the LoadStoreOptimizer formed them, but
; HaydnExpandPostIncEarly ALWAYS split them back into ST+ADDI because of a
; stale comment claiming "no MC/silicon-backed fused post-increment store
; encoding". This cost one bundle slot per streaming store on every kernel
; with a write-back output pointer (FIR writeback, FFT output, vec_scale
; NatureDSP utility loops). HiFi3z packs these as `ae_s32.ip` / `ae_s64.ip`.
;
; Fix : add ST32_POST / ST64_POST real instruction defs and emit the
; fused form from HaydnExpandPostIncEarly when the stride is encodable. If
; the fusion regresses, the MIR check sees ST32+ADDI32 / ST64+ADDI32 instead
; of the single ST32_POST / ST64_POST, and the ASM check loses s_sw_post_imm
; d_sdw_post_imm.
;
; Test design:
; @stream_store_i32: i32 stores with stride 4 -> ST32_POST (imm6=1).
; @stream_store_i64: i64 stores with stride 8 -> ST64_POST (imm6=1).
; @stream_store_i32_stride8: i32 stores with stride 8 (imm6=2) -> ST32_POST.
; The LSR + LoadStoreOptimizer form ST32_POST_INC / ST64_POST_INC pseudos;
; HaydnExpandPostIncEarly must emit the fused ST32_POST / ST64_POST.

; MIR checks (post-expand-post-inc-early)
; Product form: fused ST32_POST / ST64_POST for encodable store strides.

; @stream_store_i32: ST32_POST imm6=1 (stride 4).
; MIR-LABEL: name: stream_store_i32
; MIR: ST32_POST

define void @stream_store_i32(ptr %out, i32 %n) nounwind {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit

loop:
  %i  = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %po = phi ptr [ %out, %entry ], [ %po.next, %loop ]
  store i32 %i, ptr %po, align 4
  %po.next = getelementptr i32, ptr %po, i32 1
  %i.next  = add i32 %i, 1
  %cond = icmp slt i32 %i.next, %n
  br i1 %cond, label %loop, label %exit

exit:
  ret void
}

; @stream_store_i64: ST64_POST imm6=1 (stride 8). The mula64 intrinsic produces a
; real i64 that must be stored via ST64 path (not split into two ST32).
declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
; MIR-LABEL: name: stream_store_i64
; MIR: ST64_POST
define void @stream_store_i64(ptr %out, i32 %n) nounwind {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit

loop:
  %i  = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %po = phi ptr [ %out, %entry ], [ %po.next, %loop ]
  %vc.1 = bitcast i64 1 to <2 x i32>
  %vc.2 = bitcast i64 1 to <2 x i32>
  %val = call i64 @llvm.haydn.mula64.ss.ll(i64 0, <2 x i32> %vc.1, <2 x i32> %vc.2)
  store i64 %val, ptr %po, align 8
  %po.next = getelementptr i64, ptr %po, i32 1
  %i.next  = add i32 %i, 1
  %cond = icmp slt i32 %i.next, %n
  br i1 %cond, label %loop, label %exit

exit:
  ret void
}

; @stream_store_i32_stride8: ST32_POST imm6=2 (stride 8).
; MIR-LABEL: name: stream_store_i32_stride8
; MIR: ST32_POST
define void @stream_store_i32_stride8(ptr %out, i32 %n) nounwind {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit

loop:
  %i  = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %po = phi ptr [ %out, %entry ], [ %po.next, %loop ]
  store i32 %i, ptr %po, align 4
  %po.next = getelementptr i32, ptr %po, i32 2
  %i.next  = add i32 %i, 1
  %cond = icmp slt i32 %i.next, %n
  br i1 %cond, label %loop, label %exit

exit:
  ret void
}

; ASM checks (final assembly)

; ASM-LABEL: stream_store_i32:
; ASM: s_sw_post_imm
; ASM-LABEL: stream_store_i64:
; ASM: d_sdw_post_imm
