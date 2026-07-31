; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s
;
; G_UNMERGE_VALUES s64 -> 8 x s8 had no selector case, and the generic fallback
; COPY'd the WHOLE source into every def. That produced `MOVE32 <GPR>, $d0` —
; an illegal cross-bank move the machine verifier rejects — repeated per def,
; so every lane silently read lane 0's low byte. -verify-machineinstrs is part
; of this test on purpose: without it the bad code reaches the assembler.
;
; The correct shape is the two 32-bit halves (MOVE32_DR_L / MOVE32_DR_H, ISA
; "rt = rsd[31:00]" / "rt = rsd[63:32]") plus a shift+mask per lane.

; Lane 0 comes out of the low half with no shift.
; CHECK-LABEL: ext_lane0:
; CHECK: move32_dr_l
; CHECK-NOT: move32{{[[:space:]]}}
define i32 @ext_lane0(ptr %p) nounwind {
  %v = load <8 x i8>, ptr %p, align 8
  %r = extractelement <8 x i8> %v, i32 0
  %z = zext i8 %r to i32
  ret i32 %z
}

; Lane 3 is still the low half, but shifted.
; CHECK-LABEL: ext_lane3:
; CHECK: move32_dr_l
; CHECK: srl
define i32 @ext_lane3(ptr %p) nounwind {
  %v = load <8 x i8>, ptr %p, align 8
  %r = extractelement <8 x i8> %v, i32 3
  %z = zext i8 %r to i32
  ret i32 %z
}

; Lane 7 must read the HIGH half — the old code never did.
; CHECK-LABEL: ext_lane7:
; CHECK: move32_dr_h
define i32 @ext_lane7(ptr %p) nounwind {
  %v = load <8 x i8>, ptr %p, align 8
  %r = extractelement <8 x i8> %v, i32 7
  %z = zext i8 %r to i32
  ret i32 %z
}

; A vector compare reduced to a scalar mask reaches the same unmerge shape
; without going through extractelement (the legalizer scalarizes the icmp),
; which is how `memcmp`-style byte loops hit this. Both halves must be read.
; CHECK-LABEL: any_ne:
; CHECK-DAG: move32_dr_l
; CHECK-DAG: move32_dr_h
define i32 @any_ne(ptr %p) nounwind {
  %v = load <8 x i8>, ptr %p, align 8
  %c = icmp ne <8 x i8> %v, <i8 1, i8 2, i8 3, i8 4, i8 5, i8 6, i8 7, i8 8>
  %m = bitcast <8 x i1> %c to i8
  %r = icmp ne i8 %m, 0
  %z = zext i1 %r to i32
  ret i32 %z
}
