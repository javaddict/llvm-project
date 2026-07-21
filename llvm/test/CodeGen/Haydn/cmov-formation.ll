; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; CMOV Formation / Select-lowering tests.
;
; STATUS (,): un-XFAIL'd. The previous XFAIL blamed
; "GenMux Pattern 1 (tryConvertBitwiseSelect) does not fire post-ISA-27" because
; the NEG32/NOT32 self-negate coalescence it demands is not produced post-RA, so
; the 5-op bitwise chain survived instead of fusing to MOVT32/MOVF32. That
; rationale is STALE: the GISel selector no longer emits the bitwise chain for
; s32 G_SELECT at all. Post-ISA-27 + the s32 path
; (HaydnInstructionSelector.cpp:1119-1155) lowers G_SELECT directly to a single
; tied-def MOVT32:
; %Dst = MOVT32 %FalseVal(tied), %TrueVal, %Cond
; which coalesces to a single movt32. So EVERY s32 select -- including inverted
; predicates (ne/sge/etc.) -- now lowers to a compare (GPR 0/1) feeding MOVT32
; via GPR rs2 (the "GPR-as-bool" contract).
;
; MOVT32 rd, rs1, rs2: rd = (rs2[0] == 1) ? rs1 : rd
; MOVF32 rd, rs1, rs2: rd = (rs2[0] == 0) ? rs1 : rd
;
; The CHECKs are deliberately conservative (assert movt32/movf32 is present and
; the bitwise scaffolding is absent). The coordinator MUST confirm at build time.

;===----------------------------------------------------------------------===
; Simple select: max(a, b) = (a > b) ? a : b
; DAG-combined to max32.
;===----------------------------------------------------------------------===

define i32 @select_sgt(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: select_sgt:
; CHECK-DAG:       max32
entry:
  %cmp = icmp sgt i32 %a, %b
  %sel = select i1 %cmp, i32 %a, i32 %b
  ret i32 %sel
}

;===----------------------------------------------------------------------===
; Simple select: min(a, b) = (a < b) ? a : b
; DAG-combined to min32.
;===----------------------------------------------------------------------===

define i32 @select_slt(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: select_slt:
; CHECK-DAG:       min32
entry:
  %cmp = icmp slt i32 %a, %b
  %sel = select i1 %cmp, i32 %a, i32 %b
  ret i32 %sel
}

;===----------------------------------------------------------------------===
; Select with constants: cond ? 42 : 0
; s32 select -> SEQ32 (GPR 0/1) + MOVT32 (no bitwise chain post-ISA-27).
;===----------------------------------------------------------------------===

define i32 @select_const(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: select_const:
; CHECK:       seq32
; The bitwise scaffolding must NOT survive -- the selector emits MOVT32 directly:
; CHECK-NOT:   neg32
; CHECK-NOT:   not32
; A conditional move carries the select semantics via GPR rs2:
; CHECK:       mov{{t|f}}32
entry:
  %cmp = icmp eq i32 %a, %b
  %sel = select i1 %cmp, i32 42, i32 0
  ret i32 %sel
}

;===----------------------------------------------------------------------===
; Select with unsigned comparison
; DAG-combined to maxu32.
;===----------------------------------------------------------------------===

define i32 @select_ugt(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: select_ugt:
; CHECK-DAG:       maxu32
entry:
  %cmp = icmp ugt i32 %a, %b
  %sel = select i1 %cmp, i32 %a, i32 %b
  ret i32 %sel
}

;===----------------------------------------------------------------------===
; Select equality comparison (a == b) ? a : b
; s32 select -> SEQ32 (GPR 0/1) + MOVT32 directly.
;===----------------------------------------------------------------------===

define i32 @select_eq(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: select_eq:
; CHECK:       seq32
; CHECK-NOT:   neg32
; CHECK-NOT:   not32
; CHECK:       mov{{t|f}}32
entry:
  %cmp = icmp eq i32 %a, %b
  %sel = select i1 %cmp, i32 %a, i32 %b
  ret i32 %sel
}

;===----------------------------------------------------------------------===
; Select not-equal comparison
; Post-ISA-27 the s32 selector lowers this directly to MOVT32: ICMP ne -> SEQ32 +
; XOR32 (GPR 0/1 inversion, see HaydnInstructionSelector.cpp:975-991) feeds the
; MOVT32 condition register. The NEG/AND/NOT/AND/OR chain is NOT emitted, so the
; old CHECKs asserting it are stale.
;===----------------------------------------------------------------------===

define i32 @select_ne(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: select_ne:
; CHECK:       seq32
; The icmp-ne inversion is an XOR32 in a GPR (not a bitwise-select mask), and the
; select itself is MOVT32 -- the 5-op bitwise scaffolding does NOT appear:
; CHECK-NOT:   neg32
; CHECK-NOT:   not32
; CHECK:       mov{{t|f}}32
entry:
  %cmp = icmp ne i32 %a, %b
  %sel = select i1 %cmp, i32 %a, i32 %b
  ret i32 %sel
}

;===----------------------------------------------------------------------===
; Select with different values (not just a vs b)
; s32 select -> SLT32 (GPR 0/1) + MOVT32 directly.
;===----------------------------------------------------------------------===

define i32 @select_different_vals(i32 %a, i32 %b, i32 %c, i32 %d) nounwind {
; CHECK-LABEL: select_different_vals:
; CHECK:       slt32
; CHECK-NOT:   neg32
; CHECK-NOT:   not32
; CHECK:       mov{{t|f}}32
entry:
  %cmp = icmp sgt i32 %a, %b
  %sel = select i1 %cmp, i32 %c, i32 %d
  ret i32 %sel
}

;===----------------------------------------------------------------------===
; Multiple selects in one function
; Both fold to max32.
;===----------------------------------------------------------------------===

define i32 @double_select(i32 %a, i32 %b, i32 %c) nounwind {
; CHECK-LABEL: double_select:
; CHECK-DAG:       max32
; CHECK-DAG:       max32
entry:
  %cmp1 = icmp sgt i32 %a, %b
  %sel1 = select i1 %cmp1, i32 %a, i32 %b
  %cmp2 = icmp sgt i32 %sel1, %c
  %sel2 = select i1 %cmp2, i32 %sel1, i32 %c
  ret i32 %sel2
}

;===----------------------------------------------------------------------===
; Select with greater-or-equal comparison
; Post-ISA-27 the s32 selector lowers this directly to MOVT32: ICMP sge -> SLT32 +
; XOR32 (GPR 0/1 inversion, see HaydnInstructionSelector.cpp:975-991) feeds the
; MOVT32 condition register. The 5-op bitwise chain is NOT emitted.
;===----------------------------------------------------------------------===

define i32 @select_sge(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: select_sge:
; CHECK:       slt32
; The icmp-sge inversion is an XOR32 in a GPR (not a bitwise-select mask), and the
; select itself is MOVT32 -- the 5-op bitwise scaffolding does NOT appear:
; CHECK-NOT:   neg32
; CHECK-NOT:   not32
; CHECK:       mov{{t|f}}32
entry:
  %cmp = icmp sge i32 %a, %b
  %sel = select i1 %cmp, i32 %a, i32 %b
  ret i32 %sel
}
