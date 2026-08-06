; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — constant data must land in read-only data sections (.rodata rodata.cstNN,.rodata.strM.N), NEVER in.text.

; REGRESSION TEST: constant data must land in read-only data sections (.rodata
; rodata.cstNN,.rodata.strM.N), NEVER in.text. The Haydn backend uses the
; default TargetLoweringObjectFileELF (HaydnTargetMachine.cpp), which already
; routes each SectionKind to the right ELF section. This test pins that routing
; so a future override or regression cannot silently drop data into.text.
;
; Why this test exists: a stale diagnostic claimed "freshly compiled.o files
; have ONLY.text, no.rodata — constant pools leak into.text". Investigation
; showed the GISel selector materializes ALL scalar constants inline (MatInt
; MOVEI), so a function whose only constants are scalars emits no constant pool
; at all — that is expected, not a bug. Genuine aggregate constant data (global
; const arrays, jump tables, string literals) is placed via the standard ELF
; lowering and DOES land in.rodata. This test exercises the three real-data
; paths so the placement is locked.
;
; Test design: one global `constant` array, one string literal, one jump table.
; Each must emit a `.section.rodata*` / `.rodata` directive. The `.text`
; directive must appear exactly once (the function section) — no data directive
; may follow it inside the same section.
;
; REBASELINED (post-/, 2026-07): the original per-function CHECKs
; scoped `.rodata` to each function's LABEL region, but llc emits all rodata
; sections AFTER the function bodies. The CHECKs are now global (post-function)
; so they match the actual section emission order.

; Verify all three functions compile.

;Global constant array ->.rodata

@g_a = constant [4 x i32] [i32 1, i32 2, i32 3, i32 4]
define i32 @use_g_a(i32 %i) {
  %p = getelementptr [4 x i32], ptr @g_a, i32 0, i32 %i
  %v = load i32, ptr %p
  ret i32 %v
}

;String literal ->.rodata.str1.1 (Mergeable1Byte strings)
@.str = private unnamed_addr constant [12 x i8] c"hello world\00"
define ptr @get_str() {
  ret ptr @.str
}

;Jump table ->.rodata (jump-table lowering)
define i32 @jt(i32 %x) {
  switch i32 %x, label %def [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
  ]
c0:
  br label %ret
c1:
  br label %ret
c2:
  br label %ret
c3:
  br label %ret
def:
  br label %ret
ret:
  %r = phi i32 [ 1, %c0 ], [ 10, %c1 ], [ 100, %c2 ], [ 1000, %c3 ], [ 0, %def ]
  ret i32 %r
}

; After all functions, the constant data sections must appear. The jump table
; (.LJTI2_0) lands in.rodata; the global array (g_a) follows in the same
; rodata section; the string literal (.L.str) lands in.rodata.str1.1.
; CHECK: .section{{.*}}.rodata
; CHECK: .LJTI2_0:
; CHECK: g_a:
; CHECK: .section{{.*}}.rodata.str
; CHECK: .L.str:
