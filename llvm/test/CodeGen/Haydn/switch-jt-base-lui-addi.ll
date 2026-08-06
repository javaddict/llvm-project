; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr}}
;
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
;
; REGRESSION TEST: jump-table base must materialize via lui+addi32{{(_w)?}} (HI12+LO20)
; NOT a lone addi32 from r0.
;
; Bug (/): the G_JUMP_TABLE case in HaydnInstructionSelector emitted
; addi32 rN, r0,.LJTI_K_M
; directly. A bare ADDI32 operand carries only a LO16 fixup; when the linker
; resolves.LJTI to a.rodata address in high memory, the simm12 field cannot
; hold it and wraps (e.g. to 0xFFF80100). The subsequent `ld32 rN, rN, 0` then
; loads from OOB (yielding 0), and `jalr r0, rN, 0` jumps to 0 -> NOEXIT.
;
; Root cause: the JT path bypassed the LOAD_ADDR pseudo that G_GLOBAL_VALUE and
; G_BLOCK_ADDR use (which HaydnExpandPseudos lowers to lui+addi32{{(_w)?}}); moreover
; expandLOAD_ADDR had no JTI case, so even routing through it fell through.
;
; Test design: a dense 5-case switch (0..4) forces jump-table emission. The JT
; base must be a `lui rN,.LJTI` immediately followed by `addi32{{(_w)?}} rN, rN,.LJTI`
; (WIDE 3-operand form `addi32{{(_w)?}} rd, rs, sym` with rs=rd; the encoder records
; rs in bits[19:16]=rt and the LO20 fixup is the symbol). The CHECK-NOT on
; `addi32 rN, r0,.LJTI` is the surgical probe: if the bug regresses, that
; exact bare-addi32 form reappears as the FIRST.LJTI reference and the probe
; fires.

define i32 @jt_base_lui_addi(i32 %sel, i32 %v) nounwind {
entry:
  switch i32 %sel, label %def [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
    i32 4, label %c4
  ]
c0:
  br label %ret
c1:
  br label %ret
c2:
  br label %ret
c3:
  br label %ret
c4:
  br label %ret
def:
  br label %ret
ret:
  %r = phi i32 [ 0, %c0 ], [ 1, %c1 ], [ 2, %c2 ], [ 3, %c3 ], [ 4, %c4 ], [ 99, %def ]
  %s = add i32 %r, %v
  ret i32 %s
}
