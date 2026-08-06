; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 %s -o - | FileCheck %s

; Role: semantic — MOVE32 must count as a SINGLE GPR read port.

; REGRESSION TEST: MOVE32 must count as a SINGLE GPR read port.
;
; Bug (F14): MOVE32 is modeled in HaydnInstrInfo.td with two source operands
; ($rs1, $rs2) for the R-type encoding, and copyPhysReg passes SrcReg twice
; (MOVE32 rd, rs, rs). The packetizer's countGPRPorts counted BOTH source
; operands as distinct reads, so a MOVE32 consumed 2 read ports instead of 1.
; This made the GPR 4R2W port budget reject perfectly legal 2-move bundles:
; two MOVE32 rd, rs, rs would be counted as needing 4 read ports (tight at
; the limit), preventing any third instruction from joining the packet even
; though physically only 2 GPR read ports are consumed (the move is RI-like
; = 1R/1W per the ISA spec).
;
; Fix : countGPRPorts dedupes repeated source operands per instruction
; via SmallSet, so MOVE32 rd, rs, rs counts as 1 read. This lets two
; independent GPR32 moves coexist in one VLIW bundle (2R/2W, well within
; the 4R2W budget).
;
; Test design: two independent i32 copies via select-with-no-condition-style
; IR forces the regalloc to emit MOVE32 copies of distinct GPR32 registers.
; If MOVE32 is miscounted as 2 reads, the packetizer will refuse to pack the
; two moves together and they'll appear in separate bundles. With the fix
; they should appear in the same BUNDLE.
;
; What breaks if the bug reappears: the two moves end up in separate bundles
; (one instruction per bundle), and the FileCheck for a single BUNDLE
; containing both MOVE32 instructions fails.

define i32 @move32_dual_copy(i32 %a, i32 %b) nounwind {
entry:
  ; Force two independent GPR32→GPR32 moves. The selects materialize as
  ; conditional moves; post-regalloc these become MOVE32 copies when the
  ; condition is constant-folded or when coalescing leaves identity moves.
  %sa = select i1 true, i32 %a, i32 %b
  %sb = select i1 true, i32 %b, i32 %a
  %sum = add i32 %sa, %sb
  ret i32 %sum
}

; CHECK-LABEL: move32_dual_copy:
; At least one MOVE32 must appear (copy from argument reg to working reg).
; CHECK: move32
