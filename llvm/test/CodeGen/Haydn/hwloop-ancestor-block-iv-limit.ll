; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; REQUIRES: haydn-registered-target

; Role: semantic — count-down EQ loop whose IV init comes from a runtime value (function argument) and whose limit (0) is materialized in the.

; REGRESSION TEST: count-down EQ loop whose IV init comes from a runtime
; value (function argument) and whose limit (0) is materialized in the
; function-entry block — an ancestor of the loop preheader. The preheader
; itself contains NO def of either the IV or the limit register; both flow
; through as live-ins. This is the dominant shape of NatureDSP FFT/DCT
; kernels (fft_real16x16, dct_16x16, ifft_real16x16,...).
;
; Bug : the post-RA HardwareLoops pass used block-global resolvers
; (findImmediateDef, findImmediateDefChain) that scanned FORWARD through a
; block and returned on the FIRST immediate def. When the function-entry
; block redefined the IV register with both an early constant (dead, stored
; to stack) and a later non-immediate (the real LD32 init), the forward scan
; returned the stale early constant. The recognizer then computed a negative
; trip count and bailed with "Cannot determine trip count". The pre-emptive
; limit resolver (v1) made this worse: when findImmediateDefBefore
; returned false (no def in preheader), it skipped the dominator-chain
; fallback that would have walked to the entry block.
;
; HiFi forms a hardware loop here (loopnez) from the same source, so Haydn
; must as well.
;
; Fix (v2):
; * findImmediateDefBefore now reports FoundDef so callers can distinguish
; "no def in preheader" (fall back to dom chain) from
; "non-immediate def in preheader" (authoritative, skip fallback).
; * findImmediateDef walks BACKWARDS so the LAST def wins, not the FIRST.
; * The IV-init fallback path uses the same program-point-aware search.
;
; Test design: the IV is computed from a function argument at runtime
; (`%shr = ashr i32 %n, 3`) so its init in the entry block is a non-immediate
; (SRA32). The limit is materialized in the entry block as a constant 0.
; The loop body decrements the IV by 1 and exits when iv == 0 (SEQ32+BEQZ).
; This is the exact post-RA shape of fft_real16x16's splitPart_x2 loop.
; The loop MUST convert to a hardware loop — either via the pre-RA IV-PHI
; pass or the post-RA SEQ32+BEQZ recognizer.

define void @ancestor_block_iv_limit(ptr nocapture %x, i32 %n) {
; CHECK-LABEL: ancestor_block_iv_limit:
; CHECK:       set_hwloop
entry:
  %shr = ashr i32 %n, 3
  br label %do.body

do.body:
  %i = phi i32 [ %shr, %entry ], [ %dec, %do.body ]
  %px = phi ptr [ %x, %entry ], [ %px.next, %do.body ]
  %v = load i32, ptr %px, align 4
  %mul = mul i32 %v, 7
  store i32 %mul, ptr %px, align 4
  %px.next = getelementptr inbounds i32, ptr %px, i32 4
  %dec = add i32 %i, -1
  %tobool = icmp eq i32 %dec, 0
  br i1 %tobool, label %exit, label %do.body

exit:
  ret void
}
