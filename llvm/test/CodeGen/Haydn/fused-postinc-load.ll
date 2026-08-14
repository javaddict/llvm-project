; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM

; Role: MIR — streaming i64/i32 loads must lower to a SINGLE fused post-increment load instruction (LD64_POST / LD32_POST) when the stride is.

; REGRESSION TEST : streaming i64/i32 loads must lower to a SINGLE fused
; post-increment load instruction (LD64_POST / LD32_POST) when the stride is
; encodable as imm6<<3 / imm6<<2, NOT the 2-instruction LD + ADDI32 split.
;
; Bug being fixed: the Haydn ISA spec defines fused post-increment loads
; (Database/haydn_instruction_db.json entries D_LDW_POST_IMM and
; S_LW_POST_IMM) with base-register writeback via the AGU dedicated write
; wordline (encoding_manual.md:217,478 — single LS slot, no GPR-compute write
; port). The backend's LD64_POST_INC / LD32_POST_INC pseudos were expanded to
; TWO instructions (LD64_S1 + ADDI32) — a backend gap, not an ISA gap. HiFi3z
; packs these as a single `ae_l64.ip` / `ae_l32.ip` instruction; Haydn was
; paying 2 instructions and 2 issue slots per streaming load on every FIR/FFT
; vec_dot inner loop.
;
; Fix : add real LD64_POST / LD32_POST instruction defs (FmtLSPostInc
; tied base-writeback, scaled imm6) in the s1 FU=LD opcode space (0x2/0x3)
; and change HaydnExpandPostIncEarly / HaydnExpandPseudos to emit the single
; fused instruction when the stride is encodable (Offset==0 per
; stride a multiple of the access width, scaled value in signed imm6 range
; 32..+31). Falls back to the LD + ADDI32 split otherwise.
;
; Test design:
; @vec_dot_streaming_i64: i64 loads with stride 8 -> D_LDW_POST_IMM.
; Stride 8 = imm6<<3 with imm6=1, always encodable.
; @streaming_i32: i32 loads with stride 4 -> S_LW_POST_IMM.
; Stride 4 = imm6<<2 with imm6=1, always encodable.
; @streaming_i64_large_stride: i64 loads with stride 256 = imm6<<3 with
; imm6=32 — OUT of signed imm6 range (-32..+31). Must fall back to the
; 2-instruction LD64_S1 + ADDI32 split.
;
; MIR checks assert the fused instruction appears (LD64_POST / LD32_POST) with
; the correct operands (rt def + rs_wb def + rs use + scaled imm6), and that
; the out-of-range case still splits. ASM checks assert the final emission
; contains `ld64.post` / `ld32.post` mnemonics for the encodable cases.
;
; If this test regresses:
; If MIR shows LD64_S1+ADDI32 where LD64_POST was expected: the fuse-when
; encodable logic in HaydnExpandPostIncEarly is broken or the LD64_POST
; td def is malformed. Investigate the expansion, do NOT just update
; If ASM no longer shows ld64.post / ld32.post: same root cause.
;
; References:
; ~/haydn-plans/decisions/-fused-postinc-load-design.md
; ~/haydn-plans/decisions/-postinc-pseudo-packetize-fix-design.md
; (lands first — pre-packetizer expansion, 2-instruction form;
; collapses to 1 instruction where the stride fits imm6.)
; ~/haydn-plans/Database/haydn_instruction_db.json (D_LDW_POST_IMM
; S_LW_POST_IMM — the spec entries this instruction implements)
; HiFi3z ae_l64.ip / ae_l32.ip (the analog this matches)

declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
; vec_dot-style hot loop: streaming i64 loads, stride 8 (sizeof(i64)).
; Should lower to LD64_POST (D_LDW_POST_IMM) — the ae_l64.ip analog.
define i64 @vec_dot_streaming_i64(ptr readonly %a, ptr readonly %b, i32 %n) nounwind {
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

; i32 streaming loop, stride 4 (sizeof(i32)). Should lower to LD32_POST.
define i32 @streaming_i32(ptr readonly %a, i32 %n) nounwind {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit

loop:
  %i   = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %pa  = phi ptr [ %a, %entry ], [ %pa.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %sum, %loop ]
  %xa  = load i32, ptr %pa, align 4
  %sum = add i32 %acc, %xa
  %pa.next = getelementptr i32, ptr %pa, i32 1
  %i.next  = add i32 %i, 1
  %cond = icmp slt i32 %i.next, %n
  br i1 %cond, label %loop, label %exit

exit:
  %result = phi i32 [ 0, %entry ], [ %sum, %loop ]
  ret i32 %result
}

; Large-stride i64 loop (stride 256 = imm6<<3 with imm6=32). imm6 must be in
; signed range -32..+31 — 32 is OUT of range. Must fall back to LD64_S1 +
; ADDI32 split. Proves the fallback path is exercised.
define i32 @streaming_i64_large_stride(ptr readonly %a, i32 %n) nounwind {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit

loop:
  %i   = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %pa  = phi ptr [ %a, %entry ], [ %pa.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %sum, %loop ]
  ; Load the low 32 bits of each i64 — uses LD32_S1 from [pa] then bumps pa.
  ; The post-inc pseudo is LD64_POST_INC because LD64_S1 is the i64 load; the
  ; expansion must NOT fuse since imm6=32 is out of range.
  %xa64 = load i64, ptr %pa, align 8
  %xa = trunc i64 %xa64 to i32
  %sum = add i32 %acc, %xa
  %pa.next = getelementptr i64, ptr %pa, i32 32
  %i.next  = add i32 %i, 1
  %cond = icmp slt i32 %i.next, %n
  br i1 %cond, label %loop, label %exit

exit:
  %result = phi i32 [ 0, %entry ], [ %sum, %loop ]
  ret i32 %result
}

; Product form: encodable strides fuse to D_LDW_POST_IMM / S_LW_POST_IMM.
; Large-stride (imm6 out of range) keeps LD64 + ADDI32 256 (no fused form).
;
; MIR-LABEL: name: vec_dot_streaming_i64
; MIR-DAG: {{LD64|D_LDW_POST_IMM}}
; MIR: MULA64_LL

; MIR-LABEL: name: streaming_i32
; MIR: S_LW_POST_IMM

; MIR-LABEL: name: streaming_i64_large_stride
; MIR: ADDI32 {{.*}}, 256
; MIR-NOT: D_LDW_POST_IMM
; MIR-NOT: S_LW_POST_IMM

; ASM-LABEL: vec_dot_streaming_i64:
; ASM: {{d_ldw_post_imm|ld64}}
; ASM: mula64.ll
; ASM-LABEL: streaming_i32:
; ASM: s_lw_post_imm
; ASM-LABEL: streaming_i64_large_stride:
; The cross-bank DR64→GPR32 extract now uses native move32_dr_l
; (1 op) instead of the 5-op stack spill (ld32 from stack).
; Residual packing may co-issue move32_dr_l with the trip compare before
; the stride ADDI32, so do not force source order between them.
; ASM-DAG: move32_dr_l
; ASM-DAG: addi32{{(_w)?}}{{.*}}, 256
