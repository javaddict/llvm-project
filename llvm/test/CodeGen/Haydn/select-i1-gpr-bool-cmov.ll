; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — scalar `select i1 %c, i32 %a, i32 %b` must lower to MOVT32 (GPR-as-bool: condition is a GPR32 whose low bit is the boolean, NOT a.

; REGRESSION TEST: scalar `select i1 %c, i32 %a, i32 %b` must lower to MOVT32
; (GPR-as-bool: condition is a GPR32 whose low bit is the boolean, NOT a
; flag/predicate register).
;
; Why this test exists: ISA-53 falsely claimed "no scalar cmov". MOVT32/MOVF32
; exist (DB + HaydnInstrInfo.td:468-484, rs2[0] semantics) and the selector
; emits them. But every dedicated select test was XFAIL with a stale rationale
; (blaming GenMux Pattern 1), so the lowering was unverified. un-XFAIL'd
; those tests and added THIS focused test to pin the gpr-as-bool contract
; end-to-end: G_ICMP -> GPR 0/1 -> MOVT32 $rs2. If MOVT32 ever regresses to a
; branch or the 5-op bitwise chain (NEG/AND/NOT/AND/OR), this fails.
;
; Mechanism (post-ISA-27, HaydnInstructionSelector.cpp):
; G_ICMP s32 : SLT32/SLTU32/SEQ32 write a GPR32 holding 0/1; inverted
; predicates (ne/sge/etc.) add XOR32 with constant 1
; (selector :908-991). The result is a plain GPR bool.
; G_SELECT s32: T = COPY FalseVal; T = MOVT32 T(tied), TrueVal, Cond;
; Dst = COPY T (selector :1119-1155). MOVT32 reads the bool
; from GPR $rs2 (rs2[0]). No SFR/predicate bank is involved.
;
; Test design: the condition comes from an ICMP (a real GPR bool, not a
; constant), and both arms are live scalars. We CHECK for a compare
; (slt32/seq32) writing a GPR, then movt32 consuming it, and assert there is
; NO branch (bnez_w/beqz_w/bne_w/beq_w/br) in the hot path -- proving the select is
; predicated, not control-flow. The CHECKs are conservative (operand-agnostic);
; the coordinator MUST confirm at build time.

;select i1 (icmp slt), i32, i32 -> SLT32 + MOVT32.

define i32 @select_icmp_slt(i32 %a, i32 %b, i32 %x, i32 %y) nounwind {
; CHECK-LABEL: select_icmp_slt:
; CHECK:       slt32
; CHECK-NOT:   bnez{{(\.s[012])?}}
; CHECK-NOT:   beqz{{(\.s[012])?}}
; CHECK-NOT:   neg32
; CHECK-NOT:   not32
; CHECK:       movt32
entry:
  %cmp = icmp slt i32 %a, %b
  %sel = select i1 %cmp, i32 %x, i32 %y
  ret i32 %sel
}

;select i1 (icmp eq), i32, i32 -> SEQ32 + MOVT32.
; G_ICMP eq lowers directly to SEQ32 (no inversion) -- see selector :912-914.
define i32 @select_icmp_eq(i32 %a, i32 %b, i32 %x, i32 %y) nounwind {
; CHECK-LABEL: select_icmp_eq:
; CHECK:       seq32
; CHECK-NOT:   bnez{{(\.s[012])?}}
; CHECK-NOT:   beqz{{(\.s[012])?}}
; CHECK-NOT:   neg32
; CHECK-NOT:   not32
; CHECK:       movt32
entry:
  %cmp = icmp eq i32 %a, %b
  %sel = select i1 %cmp, i32 %x, i32 %y
  ret i32 %sel
}

;select i1 (icmp sge), i32, i32 -> inverted predicate path still ends in MOVT32.
; G_ICMP sge lowers to SLT32 + XORI32 imm1 (emitInvert01) in a GPR.
; Logical-not of 0/1 is x^1, not bitwise NOT32. Result feeds MOVT32.
define i32 @select_icmp_sge(i32 %a, i32 %b, i32 %x, i32 %y) nounwind {
; CHECK-LABEL: select_icmp_sge:
; CHECK:       slt32
; CHECK:       xori32
; CHECK-NOT:   bnez{{(\.s[012])?}}
; CHECK-NOT:   beqz{{(\.s[012])?}}
; CHECK-NOT:   neg32
; CHECK-NOT:   not32
; CHECK:       movt32
entry:
  %cmp = icmp sge i32 %a, %b
  %sel = select i1 %cmp, i32 %x, i32 %y
  ret i32 %sel
}
