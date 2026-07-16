; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=postmisched < %s | FileCheck %s
;
; REGRESSION (/): dual independent LD32 must pack without baking
; a second opcode family into MIR. Logical opcodes stay LD32; slot is
; placement (AltDescs / Slot01_LD). No durable LD32_S1 on MIR after promote.
;
; Load-bearing: at least two LD32 appear (both loads remain logical).

@g1 = external global i32, align 4
@g2 = external global i32, align 4

define i32 @two_independent_loads(i32 %a) nounwind {
  ; CHECK-LABEL: name: two_independent_loads
  ; CHECK: LD32
  ; CHECK: LD32
  %p1 = load i32, ptr @g1, align 4
  %p2 = load i32, ptr @g2, align 4
  %sum = add i32 %p1, %p2
  %r = add i32 %sum, %a
  ret i32 %r
}

; Second probe: two loads through distinct incoming pointer arguments. With
; distinct pointer args, the second LD32 is not adjacent to a slot-0 LS op in
; a packetize-region shape currently recognizes, so promotion does not
; fire. Tracking: this is a known limitation, not a regression from.
define i32 @two_ptr_loads(ptr %a, ptr %b) nounwind {
  ; CHECK-LABEL: name: two_ptr_loads
  ; CHECK: LD32
  ; CHECK: LD32
  %va = load i32, ptr %a, align 4
  %vb = load i32, ptr %b, align 4
  %sum = add i32 %va, %vb
  ret i32 %sum
}
