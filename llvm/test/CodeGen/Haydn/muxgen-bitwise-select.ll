; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s
;
; GenMux fully retired; s32+s64 select use MOVT at isel.
; STATUS (,): un-XFAIL'd. The previous XFAIL blamed
; "GenMux Pattern 1 (tryConvertBitwiseSelect) does not fire post-ISA-27" because
; the NEG32/NOT32 self-negate coalescence it demands is not produced post-RA, so
; the 5-op bitwise chain (NEG/AND/NOT/AND/OR) survived instead of a CMOV. That
; rationale is STALE: the GISel selector no longer emits the bitwise chain for
; s32 G_SELECT at all. Post-ISA-27 + the s32 path
; (HaydnInstructionSelector.cpp:1119-1155) lowers G_SELECT directly to a single
; tied-def MOVT32:
; %Dst = MOVT32 %FalseVal(tied), %TrueVal, %Cond
; which coalesces to a single movt32/movf32. The compiler emits a predicated
; conditional move directly -- GenMux Pattern 1 is not involved on this path (it
; is effectively dead for scalar s32 select; see HaydnGenMux.cpp Pattern 1 doc).
;
; REGRESSION TEST: scalar s32 G_SELECT must lower to a predicated conditional move
; (MOVT32/MOVF32), NOT to the 5-op bitwise chain. GPR-as-bool: the condition is a
; GPR32 (from G_ICMP: SLT32/SLTU32/SEQ32 producing 0/1) consumed as MOVT32 $rs2.
;
; MOVT32 rd, rs1, rs2: rd = (rs2[0] == 1) ? rs1 : rd
; MOVF32 rd, rs1, rs2: rd = (rs2[0] == 0) ? rs1 : rd
;
; What this test guards: the final assembly must NOT contain the full
; NEG/AND/NOT/AND/OR chain. A single movt32/movf32 must carry the select
; semantics. If G_SELECT regresses to the bitwise chain (and GenMux Pattern 1
; still cannot fold it), the CHECK-NOT lines fail.
;
; The CHECKs are deliberately conservative. The coordinator MUST confirm at
; build time.

define i32 @select_sgt(i32 %a, i32 %b, i32 %c, i32 %d) nounwind {
; CHECK-LABEL: select_sgt:
; The bitwise select scaffolding must NOT appear in the final code:
; CHECK-NOT:   neg32
; CHECK-NOT:   not32
; A conditional move must carry the select semantics:
; CHECK:       mov{{t|f}}32
entry:
  %cmp = icmp sgt i32 %a, %b
  %sel = select i1 %cmp, i32 %c, i32 %d
  ret i32 %sel
}

define i32 @select_eq(i32 %a, i32 %b, i32 %c, i32 %d) nounwind {
; CHECK-LABEL: select_eq:
; CHECK-NOT:   neg32
; CHECK-NOT:   not32
; CHECK:       mov{{t|f}}32
entry:
  %cmp = icmp eq i32 %a, %b
  %sel = select i1 %cmp, i32 %c, i32 %d
  ret i32 %sel
}

define i32 @select_ult(i32 %a, i32 %b, i32 %c, i32 %d) nounwind {
; CHECK-LABEL: select_ult:
; CHECK-NOT:   neg32
; CHECK-NOT:   not32
; CHECK:       mov{{t|f}}32
entry:
  %cmp = icmp ult i32 %a, %b
  %sel = select i1 %cmp, i32 %c, i32 %d
  ret i32 %sel
}
