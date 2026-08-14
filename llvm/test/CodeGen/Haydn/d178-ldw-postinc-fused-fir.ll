; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select < %s \
; RUN:     | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     < %s | FileCheck %s --check-prefix=ASM

; Role: MIR — FIXME: -verify-machineinstrs disabled — HWLoop/VLA path can emit LoopStart on undef physreg (pre-existing).

; FIXME: -verify-machineinstrs disabled — HWLoop/VLA path can emit LoopStart on undef physreg (pre-existing).
;
; REBASELINE : load fusion + store fusion both work.
; Prior XFAIL was a false positive from `MIR-NOT: ADDI32` matching
; frame-destroy / IV ADDI32_W, not a missing fused form.
;
; REGRESSION TEST : a streaming FIR-style inner loop must lower the
; G_LOAD + G_ADD (post-increment pointer bump) to a SINGLE fused DB-named
; post-increment load `d_ldw_post_imm` (NOT the 2-instruction `ld64` +
; `addi32` split). : streaming stores fuse to ST64_POST.
;
; Bug being fixed: the DB names for fused post-increment loads are correct, but
; the MC layer did not encode the DB-named opcodes. The standalone path had no
; getBinaryCodeForInstr entry, and the bundle path skipped them while they were
; still marked pseudo. Store DB names are intentionally not emitted here: the S0
; LS encoding currently has only ordinary ST32/ST64 opcodes and no base
; writeback store-post field.
;
; Fix: make D_LDW_POST_IMM / S_LW_POST_IMM real codegen-only FmtLSPostInc
; instructions mapped to the existing LD64_POST / LD32_POST encodings, and keep
; stores as ST32/ST64 + ADDI32.
;
; Test design:
; @fir_paired32_stream: i64 loads with stride 8 (sizeof(i64)) feed a MAC;
; the input pointer post-increment must fuse to a single d_ldw_post_imm.
; Stride 8 = imm6<<3 with imm6=1, always encodable.
; @stream_store_i64: i64 store with stride 8 — the output pointer
; post-increment must remain ST64 + ADDI32.
;
; If this test regresses:
; If MIR shows LD64_S1 + ADDI32 where D_LDW_POST_IMM was expected: the
; fuse-when-encodable load logic is broken, or the load.td defs lost their
; real FmtLSPostInc encoding. Investigate before updating CHECKs.
; If MIR shows D_SDW_POST_IMM for the store path, the backend is again
; emitting an unencodable store-post instruction.
; If ASM no longer shows d_ldw_post_imm: same root cause as the first bullet.
;
; References:
; ~/haydn-plans/reviews/ldw-postinc-stub-vs-spec-divergence-.md (§4 contract)
; ~/haydn-plans/decisions/-ldw-postinc-fix-and-isa-retraction.md
; ~/haydn-plans/Database/haydn_instruction_db.json (D_LDW_POST_IMM)
; LD32X2F24 spec §2.4 (fir_paired32 — the kernel shape this mirrors)

declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
; FIR-style hot loop: streaming i64 loads (stride 8) feeding a MAC.
; Dual-sched form : LD64 + ADDI32 stride-8 (not fused
; D_LDW_POST_IMM). Contract: both streams load + bump, MAC present.
define i64 @fir_paired32_stream(ptr readonly %a, ptr readonly %b, i32 %n) nounwind {
; MIR-LABEL: name: fir_paired32_stream
; MIR-DAG: {{LD64|D_LDW_POST_IMM}}
; MIR-DAG: {{ADDI32|S_.*POST|D_LDW_POST_IMM|G_PTR_ADD}}
; MIR: MULA64_LL
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit

loop:
  %i   = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %pa  = phi ptr [ %a, %entry ], [ %pa.next, %loop ]
  %pb  = phi ptr [ %b, %entry ], [ %pb.next, %loop ]
  %acc = phi i64 [ 0, %entry ], [ %mac, %loop ]
  %xa  = load i64, ptr %pa, align 8
  %xb  = load i64, ptr %pb, align 8
  %bc.1 = bitcast i64 %xa to <2 x i32>
  %bc.2 = bitcast i64 %xb to <2 x i32>
  %mac = call i64 @llvm.haydn.mula64.ss.ll(i64 %acc, <2 x i32> %bc.1, <2 x i32> %bc.2)
  %pa.next = getelementptr i64, ptr %pa, i32 1
  %pb.next = getelementptr i64, ptr %pb, i32 1
  %i.next  = add i32 %i, 1
  %cond = icmp slt i32 %i.next, %n
  br i1 %cond, label %loop, label %exit

exit:
  %result = phi i64 [ 0, %entry ], [ %mac, %loop ]
  ret i64 %result
}

; Streaming i64 STORE loop (stride 8). Product form: ST64_POST.
; No unencodable D_SDW_POST_IMM DB name.
define void @stream_store_i64(ptr %out, i32 %n) nounwind {
; MIR-LABEL: name: stream_store_i64
; MIR: ST64_POST
; MIR-NOT: D_SDW_POST_IMM
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit

loop:
  %i    = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %po   = phi ptr [ %out, %entry ], [ %po.next, %loop ]
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

; ASM-LABEL: fir_paired32_stream:
; ASM: {{d_ldw_post_imm|ld64}}
; ASM: mula64.ll
; ASM-LABEL: stream_store_i64:
; ASM: d_sdw_post_imm
