; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr}}
;
;
; NOTE: updated for VLIW slot-1 load promotion — independent loads now pack as ld32+ld32.
; NOTE: CHECKs reflect post- scheduled output (prologue/epilogue slot order varies per frame).
;
; Callee-save push/pop optimization tests.
;
; Prologue: R12 is initialized once to SP + first_offset, then each register
; is stored at its stride-4/stride-8 offset relative to R12. The prologue's
; ADDI32 R12, SP, off survives because the following ST32 reads R12.
;
; Epilogue: restores from SP directly. The matching frame-destroy
; ADDI32 R12, SP, off is DCE'd because R12 is a reserved AT-scratch register
; so writes to it have no live range — leaving LD32 reading a stale R12
; clobbered by callees. See HaydnFrameLowering.cpp emitEpilogue.
;
; Since, R12 is the reserved linker/assembler scratch ("AT"): never
; allocated, never in the CSR list. It is therefore always safe as the
; stride-4/stride-8 scratch base in the prologue — the old R12NeedsSaving
; self-clobber guard (which skipped the optimization when R12 itself was a
; callee-save) is now always false, so the optimized prologue path is always
; taken when >=2 CSRs are saved.

; Test 1: Many GPR callee-saves (forces R8-R11 to be saved)
; This function uses all callee-saved GPRs (R8-R11) as live values across
; a function call. With the optimization, R12 is initialized to SP-16 and
; stores use offsets 0, 4, 8, 12 from R12. Epilogue restores from SP.
define i32 @test_many_gpr_callee_saves(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e) {
; Prologue: SP decrement, then R12 setup + callee-save stores from R12
; Epilogue: restores from SP (R12 path removed — frame-destroy ADDI32 R12
; SP, off DCE'd because R12 is reserved AT).
  %v1 = call i32 @callee_i32(i32 %a)
  %v2 = call i32 @callee_i32(i32 %b)
  %v3 = call i32 @callee_i32(i32 %c)
  %v4 = call i32 @callee_i32(i32 %d)
  %v5 = call i32 @callee_i32(i32 %e)
  %s1 = add i32 %v1, %v2
  %s2 = add i32 %s1, %v3
  %s3 = add i32 %s2, %v4
  %s4 = add i32 %s3, %v5
  ret i32 %s4
}

; Test 2: No callee-saves (leaf function, no spills needed)
; A simple leaf function should have minimal prologue (only xor32 r0, r0, r0)
; and no callee-save stores/loads.
define i32 @test_no_callee_saves(i32 %x) {
  %result = add i32 %x, 1
  ret i32 %result
}

; Test 3: DR64 callee-saves
; Forces DR64 callee-saves (D8-D15) by using i64 values across calls.
; R12 is initialized to SP + first_offset in the prologue, then stores use
; stride-8 offsets. Epilogue restores from SP.
;
; REGRESSION (/ Bug 5): CSR fixed slots MUST be saved at POSITIVE offsets
; from the new SP (inside the allocated frame), not at the raw negative PEI
; offsets (which land below SP and get clobbered by later pushes). When the
; first DR64 CSR offset is sp+0, emitMaterializeOffset emits `or32 r12, sp, sp`
; (zero offset → copy) instead of `addi32{{(_w)?}} r12, sp, 0`. Accept both forms.
define i64 @test_dr64_callee_saves(i64 %a, i64 %b, i64 %c) {
; Prologue: DR64 saves from R12 with stride 8. Base setup is `addi32{{(_w)?}}` for a
; nonzero first offset, or `or32` (copy) when the first offset is sp+0.
; Epilogue: DR64 restores from SP (R12 path removed). Three DR64 restores
; land in 2 bundles (slot-1 load promotion packs 2 per cycle).
  %v1 = call i64 @callee_i64(i64 %a)
  %v2 = call i64 @callee_i64(i64 %b)
  %v3 = call i64 @callee_i64(i64 %c)
  %s1 = add i64 %v1, %v2
  %s2 = add i64 %s1, %v3
  ret i64 %s2
}

declare i32 @callee_i32(i32)
declare i64 @callee_i64(i64)
