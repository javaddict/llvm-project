; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; Role: semantic — T-ABI2 static overalign. vector_size 64 / alloca align 64
; must compile (no report_fatal_error) and realign SP to MaxAlign.
;
; REGRESSION TEST: gcc-c-torture pr22141-2 / pr38151 / pr53645 / pr65369 /
; pr70903 / pr85169 ICEd in HaydnFrameLowering::determineFrameLayout:
; "stack object alignment 64 exceeds ABI StackAlign(8); SP realignment is
; unsupported". W5 fail-closed because ANDI32 is uimm20 and there is no BP.
;
; Fix: RISCVFrameLowering.cpp:1142-1153 ANDI SP, -MaxAlign overlayed as
; MatInt(-64) into a scavenged GPR then AND32 SP, SP, tmp. -64 clears the
; low 6 bits so SP is 64-aligned and therefore 8-aligned. Epilogue restores
; pre-realign SP from FP (RISCVFrameLowering.cpp:1259-1308 RestoreSPFromFP)
; before CSR reloads.
;
; Test design: one align-64 local. CHECK the mask materialise and AND32;
; CHECK-NOT the old fatal string. If realign is dropped, llc fatals or the
; and32 CHECK fails. If the mask is not a multiple of 8, SP can land at
; ≡4 mod 8 and ld64/st64 MEMORY_FAULT.

define void @static_align64(ptr %p) {
; CHECK-LABEL: static_align64:
; CHECK:       subi32{{(_w)?}} sp, sp,
; CHECK:       addi32{{(_w)?}} fp, sp,
; CHECK:       addi32{{(_w)?}} {{r[0-9]+}}, r0, -64
; CHECK:       and32 sp, sp, {{r[0-9]+}}
; CHECK-NOT:   stack object alignment
; CHECK:       addi32{{(_w)?}} sp, fp,
; CHECK:       jalr
  %a = alloca <16 x i32>, align 64
  store <16 x i32> zeroinitializer, ptr %a, align 64
  ret void
}

define void @static_align64_i8() {
; CHECK-LABEL: static_align64_i8:
; CHECK:       addi32{{(_w)?}} {{r[0-9]+}}, r0, -64
; CHECK:       and32 sp, sp, {{r[0-9]+}}
; CHECK-NOT:   SP realignment is unsupported
  %p = alloca i8, align 64
  store i8 1, ptr %p, align 64
  ret void
}
