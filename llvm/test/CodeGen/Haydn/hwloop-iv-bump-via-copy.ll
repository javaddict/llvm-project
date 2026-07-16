; This test exercises the pre-RA HardwareLoops pass (Stream A,), which
; runs BEFORE SMS on VIRTUAL registers and converts countable loops to
; SET_HWLOOP_REG + HWLOOP_END pseudos. Because the pre-RA pass operates on
; vregs (no physreg copy chains exist yet), the / post-RA IV-init
; copy-source-clobber bug class is NOT directly exercised here anymore; the
; test is retained to (a) document WHY the post-RA resolvers (/) exist
; and (b) guard that the same loop shape (pointer-IV with runtime init arriving
; via an arg copy, plus a loop-entry guard that defeats LD32_POST fusion) still
; converts correctly through the pre-RA path. Pre-RA MIR uses vregs, so CHECKs
; do not pin specific vreg numbers.
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -stop-after=haydn-hwloops < %s | FileCheck %s
;
; REGRESSION TEST: HWLoop IV-init copy-source-clobber WRONG-CODE (/).
;
; Bug (before): for a pointer-IV loop whose IV init arrives via a post-RA
; copy whose source hardreg is REUSED (clobbered) later in the entry block, the
; recognizer produced a WRONG trip count:
; entry:
; $r3 = MOVE32 $r1, $r1; IV $r3 = copy of %p (orig in $r1)
; $r4 = SLTU32 $r3, $r2; guard p < end
; $r1 = ADDI32 $r0, 0; *** $r1 REUSED for accumulator init (clobber)
; The IV-init resolver (findImmediateDefChain) followed the copy $r3→$r1 and
; found the LATER `$r1 = ADDI32 $r0, 0` (a clobbering def AFTER the copy)
; wrongly concluding IVInit=0 and firing Case 4: trip = (end - 0) >> 2.
; The IR loop is `while (p < end) { sum += *p; p += 4; }` with %p a runtime
; function argument. The correct trip is (end - p) >> 2. For any nonzero %p
; the wrong trip caused a buffer overread + wrong sum. The guard `p < end`
; does NOT imply p == 0.
;
; Fix (, HaydnHardwareLoops.cpp): IV-init resolution is now program-point
; aware. findIVInitImmediate anchors the immediate-def search at the IV's
; defining COPY instruction and queries the copy source's value LIVE AT the
; copy (findImmediateDefBefore: last def strictly before the copy, else
; live-in/unknown). The copy source $r1 has NO immediate def strictly before
; the MOVE32 (it is the function-arg live-in), so IVInit is PROVEN non-constant
; and InitIsImm is authoritatively false. The block-global resolvers are SKIPPED
; (they would unsoundly fold to the clobber). With InitIsImm=false, the loop
; falls through to Case 3 (pointer-IV: both init/limit runtime, pow2 stride)
; which emits the CORRECT (end - start) >> 2 = SUB32 r2, r2, r3; SRLI32 r2, r2, 2.
;
; Pre-RA pass update (Stream A,): the pre-RA pass derives the trip count
; directly from the IV init and does NOT emit a SUB32/SRLI32 trip-compute
; sequence in the preheader. The CHECKs below therefore check the conversion
; pseudos (SET_HWLOOP_REG + HWLOOP_END) rather than the post-RA trip-compute
; shape; vreg numbers are intentionally not pinned.
;
; REGRESSION TEST: HWLoop recognition of IV-bump-via-copy (pointer-IV where
; the post-increment is NOT fused into LD32_POST due to a loop-entry guard).
;
; Bug (before): when the post-RA loop body computed the pointer bump
; into a TEMP register and then copied it back to the IV (instead of using
; a fused LD32_POST), the recognizer could not identify the IV:
;
; $r4 = ADDI32 $r1, 4; bump computed into temp from IV
; $r1 = LD32 killed $r1, 0; (load transiently clobbers IV)
; $r3 = ADD32 killed $r1, killed $r3
; $r5 = SLTU32 $r4, $r2; compare next-ptr to end
; $r1 = MOVE32 killed $r4; copy temp back → IV (net: r1 += 4)
; BNEZ $r5, %bb.1
;
; The compare uses $r4 (next-ptr) but the loop-carried IV is $r1. Without
; copy-following, findIVBumpInLoop($r4) returned null (the ADDI32's source
; $r1 ≠ $r4 — correctly rejected by the self-reference check), and
; findIVBumpInLoop($r1) returned null ($r1's def is a MOVE32, not a bump).
; Result: "Cannot determine IV step" → the loop stayed on a BNEZ back-edge.
;
; Fix : matchIVBump now requires self-reference (IV bump defines the
; IV from itself). findIVBumpInLoop gained a copy-following fallback that
; recognizes the circular bump-via-copy pattern:
; $r4 = ADDI32 $r1, imm; $r1 = MOVE32 $r4 → IV=$r1, stride=imm.
; The compare operand ($r4) resolves to the real IV ($r1) via RealIVOut.
; extractIVBump returns the byte stride (4). findTripCount Case 3 (pointer
; IV, power-of-two stride) emits SUB32+SRLI32 in the preheader.
;
; Test design: a while-loop with a loop-entry guard (`while p < end`). The
; guard prevents LSR from fusing the post-increment into LD32_POST (the
; fused form only occurs when the loop is entered unconditionally). After
; post-RA, the bump is computed into a temp ($r4) and copied back to the
; IV ($r1). If the recognizer regresses, SET_HWLOOP_REG disappears and a
; BNEZ back-edge appears instead.
;
; Decision reference: ~/haydn-plans/decisions/-hwloop-nested-explicit-branch.md

define i32 @iv_bump_via_copy(ptr noalias readonly %p, ptr noalias readnone %end) nounwind {
; CHECK-LABEL: name: iv_bump_via_copy
; Post-RA hwloop pass : loop converts to SET_HWLOOP_REG. Latch branch erased.
; CHECK: SET_HWLOOP
; CHECK: PseudoLoopEnd
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
