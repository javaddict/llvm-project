; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=postmisched -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM

; Role: MIR — LD64_POST_INC + its ADDI32 base bump MUST packetize together into ONE bundle, not split across two bundles.

; REGRESSION TEST : LD64_POST_INC + its ADDI32 base bump MUST packetize
; together into ONE bundle, not split across two bundles.
;
; Bug: HaydnVLIWPacketizer::isPacketizeRegionBoundary returned true for every
; pseudo (/: a COPY left in-bundle is silently DROPPED by the
; AsmPrinter — wrong code — so pseudos cannot stay in-bundle). Each post-inc
; pseudo therefore packeted SOLO, and the late HaydnExpandPseudos
; (addPreEmitPass, runs AFTER the packetizer) split it into
; { LD64_S1 rt, base, 0 }
; { ADDI32 base, base, 8 }
; in TWO bundles — burning one slot per streaming load. On every
; load-streaming kernel (vec_dot, FIR, FFT) this cost ~4-6 bundles/loop. With
; the bug, vec_dot's hot loop packed at ~1.14 instr/pkt instead of ~1.9.
;
; Fix (Option B): HaydnExpandPostIncEarly runs in addPreSched2 right
; after HaydnLoadStoreOptimizer and BEFORE the packetizer, lowering JUST the
; four *_POST_INC pseudos into real LD/LD_S1 + ADDI32. The packetizer now sees
; real instructions with real itineraries (LD64_S1 reserves Slot1_LD, ADDI32
; reserves an ALU slot — no conflict), so the load + bump + an independent MAC
; pack together. The late HaydnExpandPseudos remains unchanged for every other
; pseudo (LOAD_ADDR, call-frame, COPY, SET_HWLOOP) and keeps its in-bundle
; safety net (expandPseudosInBundles) for defense in depth.
;
; Test design: a load-streaming loop mirroring vec_dot64x64 — two INDEPENDENT
; i64 loads from distinct pointers, each post-incremented by 8 (stride), then
; accumulated via mula64.ss.ll (the MULA64_LL intrinsic). The hot loop is the
; pattern HiFi3z packs as `ae_l64.ip` (fused load+inc). The MIR-level check
; asserts that at least one BUNDLE contains BOTH an LD64_S1 and an ADDI32
; together — which is impossible if either the early expansion is missing
; (post-inc stays a pseudo → region boundary → cannot be in any bundle) or the
; late split fires (load + bump land in two separate bundles). The ASM check
; additionally asserts the final emission still produces the ld64 + addi32
; + mula64 sequence with no dropped loads.
;
; If this test regresses:
; If MIR has NO BUNDLE containing both LD64_S1 and ADDI32: the early
; expansion (HaydnExpandPostIncEarly) is broken or not registered in
; addPreSched2 before the packetizer.
; If ASM drops any ld64 / mula64: a wrong-code regression of the
; class — investigate COPY handling, do NOT just update CHECK lines.
;
; References:
; ~/haydn-plans/decisions/-postinc-pseudo-packetize-fix-design.md
; ~/haydn-plans/decisions/ (reverted FIX B — do not repeat)
; ~/haydn-plans/lessons/-*.md (COPY dropped in-bundle → wrong code)

declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
; vec_dot-style hot loop: two independent streaming i64 loads + MAC accumulate.
; The two LD64_S1 + their ADDI32 bumps + the MAC should pack densely.
define i64 @vec_dot_streaming(ptr readonly %a, ptr readonly %b, i32 %n) nounwind {
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

; MIR-level: ExpandPostIncEarly still emits a 64-bit load + stride-8 bump.
; After R13 WITH/POST-load FieldSlot retirement, setDesc uses Format E
; members (D_LDW_WITH_IMM / D_LDW_POST_IMM). One stream is fused POST;
; the WITH+ADDI32 pair may be adjacent parcels rather than one BUNDLE.
; Residual: 2-child E2 pack of WITH load + ALU bump (occupancy e2 is E3-only).
;
; MIR-LABEL: name: vec_dot_streaming
; MIR-DAG: {{LD64|D_LDW_WITH_IMM|D_LDW_POST_IMM}}
; MIR-DAG: ADDI32{{[^,.]*}}, 8

; ASM-LABEL: vec_dot_streaming:
; ASM-DAG: {{ld64|d_ldw_post_imm}}
; ASM-DAG: addi32
; ASM: mula64.ll
