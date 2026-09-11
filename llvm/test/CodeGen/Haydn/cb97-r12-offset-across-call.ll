; RUN: llc -mtriple=haydn-unknown-elf -O1 -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -O1 -global-isel-abort=1 -stop-after=finalize-isel < %s -o - | FileCheck %s --check-prefix=ISEL

; Role: MIR — end-to-end: calls must carry the CSR_Haydn regmask so reserved AT (R12) is modeled as call-clobbered.

; end-to-end: calls must carry the CSR_Haydn regmask so reserved AT
; (R12) is modeled as call-clobbered. Without the mask, only TableGen Defs
; count as clobbers; R12 was missing from those Defs and MachineLateInstrsCleanup
; deleted PEI remats of `LOADI32 R12, off` across `jal __modsi3`, leaving
; `ld32_reg..., sp, r12` with a callee-clobbered offset (CoreMark
; core_init_matrix).
;
; Companion MIR: cb97-r12-offset-across-call.mir exercises LateCleanup on the
; PEI WITH_REG remat shape directly.

; Every call must attach the call-preserved regmask (CSR_Haydn).
; ISEL: LOAD_ADDR
; ISEL: JALR_CALL {{.*}}csr_haydn{{.*}}


define i32 @test_call_has_modsi3(i32 %a, i32 %b) nounwind {
  %r = srem i32 %a, %b
  ret i32 %r
}

; Large-offset stack slot held live across a call. The offset may be placed
; in a normal allocatable/callee-saved register (preferred) or rematerialized
; into R12 after the call; either is correct. Between jal and the epilogue
; CSR restores, a bare `ld32_reg/st32_reg..., sp, r12` without an intervening
; remat of r12 is the failure mode.
;
; CHECK-LABEL: test_large_offset_across_call:
; CHECK: {{__modsi3|and32|andi32|sra32|srai32|srl32|srli32|jal}}
; Between call and epilogue soft-zero re-zero (xor32 r0 before CSR restore)
; no stale sp+r12 access without remat (failure mode when R12 was
; reserved AT and remat was DCE'd across the call).
; CHECK-NOT: { {{ld32_reg|st32_reg}}{{[^}]*}}sp, r12
; F24: at -O1 the srem-by-7 is fully inlined (mulssh/mull magic number), so
; this function is leaf — no call, empty CSI — and the epilogue r0 re-zero
; xor is no longer emitted. The guarded stale sp+r12 failure mode needs a
; callee-clobber boundary, which a leaf cannot have; the loop test below
; still calls __modsi3 and keeps its epilogue xor pinned.
; Epilogue restores CSRs via ld32/ld32_reg with a rematerialized offset in
; some scavenged GPR (not necessarily r12 when AT is optional).
; CHECK: {{ld32|st32|addi32}}

define i32 @test_large_offset_across_call(i32 %n, ptr %out) nounwind {
entry:
  %pad = alloca [48 x i32], align 4
  %p40 = getelementptr inbounds [48 x i32], ptr %pad, i32 0, i32 40
  store volatile i32 %n, ptr %p40
  %v = load volatile i32, ptr %p40
  %r = srem i32 %v, 7
  %w = load volatile i32, ptr %p40
  %sum = add i32 %r, %w
  store i32 %sum, ptr %out
  ret i32 %sum
}

; Loop body calls __modsi3; post-loop block reloads a large-offset slot.
; CHECK-LABEL: test_loop_call_reload:
; CHECK: {{__modsi3|and32|andi32|sra32|srai32|srl32|srli32|jal}}
; CHECK-NOT: { {{ld32_reg|st32_reg}}{{[^}]*}}sp, r12
; CHECK: xor32{{.*}}r0, r0, r0
; CHECK: {{ld32(_reg)?}}

define i32 @test_loop_call_reload(i32 %n, i32 %m, ptr %out) nounwind {
entry:
  %pad = alloca [48 x i32], align 4
  %slot = getelementptr inbounds [48 x i32], ptr %pad, i32 0, i32 40
  store volatile i32 %n, ptr %slot
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %v = load volatile i32, ptr %slot
  %r = srem i32 %v, %m
  %acc.next = add i32 %acc, %r
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  %w = load volatile i32, ptr %slot
  %sum = add i32 %acc.next, %w
  store volatile i32 %sum, ptr %slot
  store i32 %sum, ptr %out
  ret i32 %sum
}
