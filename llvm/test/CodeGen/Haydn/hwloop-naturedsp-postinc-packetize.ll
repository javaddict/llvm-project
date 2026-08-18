; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:   -stop-after=postmisched -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM

; Role: MIR — Packing contract (LD64+ADDI32) independent of Role B (deleted).

; Packing contract (LD64+ADDI32) independent of Role B (deleted).
;
; REGRESSION TEST: Load-streaming NatureDSP hot loop (vec_dot64x64i
; fir_xcorr32x32 shape) — the post-increment load + its ADDI32 base bump
; MUST packetize together into ONE bundle. Guards the post-inc
; early-expansion fix so the vec_dot / FIR load-streaming kernels never
; regress to one-load-per-bundle.
;
; Bug (pre-): HaydnVLIWPacketizer::isPacketizeRegionBoundary returned
; true for every pseudo. Each LD64_POST_INC therefore packetized SOLO, and
; the late HaydnExpandPseudos (addPreEmitPass, AFTER the packetizer) split
; each into { LD64_S1 rt,base,0 } + { ADDI32 base,base,8 } in TWO bundles
; burning one slot per streaming load. On vec_dot this cost ~4-6 bundles
; loop (enc_fill ~1.14 instead of ~1.9). Why every pseudo is a boundary:
; — a COPY left in-bundle is silently DROPPED by the AsmPrinter
; (wrong code), so pseudos cannot stay in-bundle; relaxing this for COPY was
; tried ("FIX B") and reverted (wrong-code regression, no ILP gain).
;
; Fix (Option B, applied): HaydnExpandPostIncEarly runs in
; addPreSched2 right after HaydnLoadStoreOptimizer and BEFORE the packetizer
; lowering JUST the four *_POST_INC pseudos into real LD64_S1/LD32 + ADDI32.
; The packetizer now sees real instructions with real itineraries (LD64_S1
; reserves Slot1_LD; ADDI32 reserves an ALU slot — no conflict), so the load
; + bump pack together. The late HaydnExpandPseudos is unchanged for every
; other pseudo (LOAD_ADDR, call-frame, COPY, SET_HWLOOP) and keeps its
; in-bundle safety net (expandPseudosInBundles).
;
; Test design: a load-streaming loop mirroring vec_dot64x64i_hifi3.c — two
; INDEPENDENT i64 loads from distinct pointers, each post-incremented by 8
; (stride), accumulated via the mula64.ss.ll intrinsic (the AE_MULA32U_LL
; analog). The HiFi3 source packs this as `ae_l64.ip` (fused load+inc); with
; the Haydn equivalent is now the single LD64_POST instruction (the spec
; D_LDW_POST_IMM), not the LD64_S1 + ADDI32 pair. (pre-packetizer
; expansion) is still proven: a regressed build (post-inc stays a pseudo →
; region boundary) would not produce any LD64_POST in a BUNDLE.
;
; The MIR-level CHECK asserts that the loop body contains the fused LD64_POST
; (the D_LDW_POST_IMM instruction) — impossible if either the early expansion
; is missing (post-inc stays a pseudo → region boundary → cannot be in any
; bundle) or the late split fires (LD64_S1 + ADDI32 instead of the fused form).
;
; If this test regresses (no LD64_POST in the loop body, or LD64_S1 + ADDI32
; instead):
; HaydnExpandPostIncEarly is broken or not registered in addPreSched2
; before the packetizer.
; OR the fuse-when-encodable logic regressed (stride 8 must fuse).
; OR isPacketizeRegionBoundary was relaxed in a way that reintroduces
; the / COPY-drop wrong-code — investigate, do NOT just update
;
; References:
; ~/haydn-plans/decisions/-postinc-pseudo-packetize-fix-design.md
; ~/haydn-plans/decisions/ (reverted FIX B — do not repeat)
; ~/haydn-plans/lessons/-*.md (COPY dropped in-bundle → wrong code)
; ~/haydn-plans/naturedsp-haydn/disasm/kernel-sdiff/PATTERNS.md §B
; (POST_INC load split — highest-ROI packetization gap).

declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
; vec_dot64x64i hot loop: two independent streaming i64 loads + MAC accumulate.
; HiFi3 source: for(n=0;n<N;n++){ AE_L64_IP(xw0,px,8); AE_L64_IP(yw0,py,8);
; AE_MULA32U_LL(ACC, x0, y0); }
define i64 @vec_dot_streaming_postinc(ptr readonly %x, ptr readonly %y, i32 %N) nounwind {
entry:
  %c0 = icmp sgt i32 %N, 0
  br i1 %c0, label %loop, label %exit

loop:
  %i   = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %px  = phi ptr [ %x, %entry ], [ %px.next, %loop ]
  %py  = phi ptr [ %y, %entry ], [ %py.next, %loop ]
  %acc = phi i64 [ 0, %entry ], [ %mac, %loop ]
  %xa  = load i64, ptr %px, align 8
  %xb  = load i64, ptr %py, align 8
  %bc.1 = bitcast i64 %xa to <2 x i32>
  %bc.2 = bitcast i64 %xb to <2 x i32>
  %mac = call i64 @llvm.haydn.mula64.ss.ll(i64 %acc, <2 x i32> %bc.1, <2 x i32> %bc.2)
  %px.next = getelementptr inbounds i64, ptr %px, i32 1
  %py.next = getelementptr inbounds i64, ptr %py, i32 1
  %i.next   = add i32 %i, 1
  %done     = icmp eq i32 %i.next, %N
  br i1 %done, label %exit, label %loop

exit:
  %r = phi i64 [ 0, %entry ], [ %mac, %loop ]
  ret i64 %r
}

; MIR-level: packing contract — fused/post-inc 64-bit loads in the loop
; body (D_LDW / LD64), independent of Role B (deleted). HWLoop not
; required: SMS may pipeline this shape instead of ZOL form.
;
; MIR-LABEL: name: vec_dot_streaming_postinc
; MIR: bb.{{[0-9]+}}.loop
; Two 64-bit streaming loads remain in the body (post-inc or with-imm).
; MIR-DAG: {{LD64|D_LDW}}
; MIR-DAG: {{LD64|D_LDW}}
; MIR-DAG: {{MULA64|MUL64}}

; ASM-level: co-packed ld64 + MAC + post-inc / addi; no dropped loads.
;
; ASM-LABEL: vec_dot_streaming_postinc:
; Streaming pack: 64-bit load + MAC + post-inc load (or explicit addi).
; ASM: {{ld64|d_ldw}}
; ASM: {{mula64|mul64}}
; ASM: {{d_ldw_post|addi32}}
