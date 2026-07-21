; This test exercises the pre-RA HardwareLoops pass (Stream A,), which
; runs BEFORE SMS on VIRTUAL registers and converts countable loops to
; SET_HWLOOP_REG + HWLOOP_END pseudos via IV-PHI analysis. Because the
; pass runs pre-RA on vregs, the post-RA copy-chain/clobber resolver that
; P2b originally exercised is NO LONGER directly on this path — there are
; no physreg copy chains to follow yet. The test is retained to guard that the
; SAME loop SHAPES (sibling loops that redefine a shared limit value, and a
; strided GEP loop that earlier required copy-chain IV resolution post-RA) still
; convert correctly through the pre-RA IV-PHI analysis. The /P2a/P2b notes
; below document WHY the shapes exist and remain the failure mode of interest.
; Pre-RA MIR uses virtual registers, so CHECKs do not pin specific vreg numbers.
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -stop-after=haydn-hwloops < %s | FileCheck %s
; REBASELINED : scheduling changed (//) — SWPS now fires
; so LoopStart's $adj (trip-count adjustment) is -2 instead of 0 on iv_via_copy_chain.
;
; REGRESSION TEST: HWLoop IV/step resolution broadening (G1, — P2).
;
; Two recognizer false-negatives fixed in :
;
; (P2a) findImmediateDefOnDomChain rejected loops when a physreg had two
; DIFFERENT immediate defs along the dominator chain — but in multi-loop
; kernels, sibling loops legitimately redefine the same physreg with
; different values in their own preheaders (e.g. loop A sets r8=0, loop B
; sets r8=N). The unscoped walk saw both and bailed. adds
; findImmediateDefOnDomChainScoped, which skips blocks belonging to a
; sibling loop (predicate: getLoopFor(B)==null || contains(ThisLoop)).
; Bail point was HaydnHardwareLoops.cpp:1620 "Cannot determine trip count"
; after the resolver defeat.
;
; (P2b) findIVBumpInLoop copy-following followed only ONE MOVE32 hop.
; extends it to bounded multi-hop (<=4) MOVE32/OR32 + spill/reload
; (ST32/LD32) pairs, so a post-RA IV reached through a copy chain or a
; register-pressure spill is still recognized. Bail point was
; HaydnHardwareLoops.cpp:1462 "Cannot determine IV step" and 1412/1437
; "Cannot identify IV in comparison".
;
; If these regress, SET_HWLOOP(_REG) disappears and a cmp+branch back-edge
; appears.
;
; STATUS (rebaseline for the pre-RA pass, Stream A /):
; * iv_via_copy_chain : FIRES via IV-PHI analysis -> SET_HWLOOP_REG
; in preheader.
; * sibling_loops_shared_reg: FIRES for BOTH loop A and loop B via IV-PHI
; analysis. Under the post-RA recognizer this
; shape DID NOT FIRE (LSR rewrote both count
; down loops to pointer-IV (lsr.iv) shape and
; both bailed "Cannot compute trip count"); the
; pre-RA path recovers the induction on the IV
; PHI and now converts both loops.
;
; Diagnosis: ~/haydn-plans/reviews/g1g4-diag-fir.md (§0.3, §0.4, §4 #2/#3)
; ~/haydn-plans/reviews/g1g4-diag-iir-vec.md (§3.2 GAP-B, §3.4 GAP-D)
; Decision: ~/haydn-plans/decisions/-g1-runtime-tripcount-iv-resolution.md

; P2a: two sibling count-down loops sharing the limit register, where the
; FIRST loop's limit is the constant 0 and the SECOND loop's limit is a
; runtime value. Without MLI-scoping, the dom-chain resolver saw both the
; "limit=0" def (from loop A's preheader) and the "limit=runtime" def (loop
; B) and rejected loop B as a conflicting-constants false negative. With
; 's scoped walk, loop B's preheader is recognized as belonging to a
; sibling loop and skipped, so loop B converts via Case 2 (count-down EQ
; limit=0).
;
; This mirrors the §3.4 case_dom_conflict repro: Loop A converts, Loop B
; (runtime limit) previously bailed "Cannot compute trip count" under the
; post-RA recognizer. The pre-RA pass (Stream A,) converts BOTH loops.
;
; To reproduce the (now-closed) post-RA bail, the old behavior was:
; build/bin/llc -mtriple=haydn-unknown-elf -mattr=+hwloop \
; stop-after=haydn-hwloops -debug-only=haydn-hwloops <this function>
; which logged "Cannot compute trip count" / "Cannot determine trip count"
; for both loops because the IV/Limit were derived from LSR-rewritten pointer
; IV phis. The pre-RA pass operates on the IR-shape IV PHIs and converts both.
define void @sibling_loops_shared_reg(ptr noalias %a, ptr noalias %b, i32 %n,
                                      i32 %m) nounwind {
; CHECK-LABEL: name: sibling_loops_shared_reg
; Post-RA does NOT convert these LSR pointer-IV shapes. Stays BLT.
; CHECK: SET_HWLOOP
; CHECK: PseudoLoopEnd
; CHECK-NOT: BLT

entry:
  ; Loop A: count down from %n to 0 (Case 2, limit=0 imm).
  br label %loopA

loopA:
  %iA = phi i32 [ %n, %entry ], [ %iA.next, %loopA ]
  %pA = getelementptr inbounds i32, ptr %a, i32 %iA
  store i32 %iA, ptr %pA, align 4
  %iA.next = add i32 %iA, -1
  %cmpA = icmp sgt i32 %iA.next, 0
  br i1 %cmpA, label %loopA, label %between

between:
  ; Loop B: count down from %m to 0 (Case 2). Under register pressure the
  ; limit register is reused; the unscoped resolver defeat made this bail.
  br label %loopB

loopB:
  %iB = phi i32 [ %m, %between ], [ %iB.next, %loopB ]
  %pB = getelementptr inbounds i32, ptr %b, i32 %iB
  store i32 %iB, ptr %pB, align 4
  %iB.next = add i32 %iB, -1
  %cmpB = icmp sgt i32 %iB.next, 0
  br i1 %cmpB, label %loopB, label %exit

exit:
  ret void
}

; P2b: count-up loop where the post-RA IV bump is computed into a temp and
; copied back via MOVE32 (the bump-via-copy shape), but here the copy
; chain is exercised through a getelementptr strided loop. The recognizer
; must follow the copy to identify the IV and resolve the step.
define i32 @iv_via_copy_chain(ptr noalias readonly %p, ptr noalias readnone %end) nounwind {
; CHECK-LABEL: name: iv_via_copy_chain
; Post-RA converts. SET_HWLOOP_REG, BLT back-edge gone.
; REBASELINED : scheduling changed (//) — SWPS now fires
; so LoopStart's $adj is -2 instead of 0 (prologue/epilogue trip-count adjust).
; CHECK: SET_HWLOOP
; CHECK-NOT:  BLT
; CHECK:      PseudoLoopEnd %bb.{{[0-9]+}}

entry:
  %cmp3 = icmp ult ptr %p, %end
  br i1 %cmp3, label %while.body, label %while.end

while.body:
  %s.05 = phi i32 [ %add, %while.body ], [ 0, %entry ]
  %p.addr.04 = phi ptr [ %incdec.ptr, %while.body ], [ %p, %entry ]
  %incdec.ptr = getelementptr inbounds i8, ptr %p.addr.04, i32 4
  %0 = load i32, ptr %p.addr.04, align 4
  %add = add i32 %0, %s.05
  %cmp = icmp ult ptr %incdec.ptr, %end
  br i1 %cmp, label %while.body, label %while.end

while.end:
  %s.0.lcssa = phi i32 [ 0, %entry ], [ %add, %while.body ]
  ret i32 %s.0.lcssa
}
