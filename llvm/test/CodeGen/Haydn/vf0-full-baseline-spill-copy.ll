; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — Full-only residual baseline — spill / reload / copy shape under INFRASTRUCTURE_ONLY product BUNDLE_E96.

; Full-only residual baseline — spill / reload / copy shape under
; INFRASTRUCTURE_ONLY product BUNDLE_E96.
;
; Pins fixed Full-row spill-kpi counts (not a soft nonzero regex) and the
; CSR/frame spill surface for later multi-format size accounting
; (EncodedBytes vs CycleCount stay distinct). Observe baseline only — not a
; densify/compact activation.
;
; Hand FileCheck only. No second product format row.

declare void @ext(i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @vf0_full_spill_copy_baseline(i32 %a, i32 %b, ptr %p) {
entry:
  %v0 = load i32, ptr %p
  %p1 = getelementptr i32, ptr %p, i32 1
  %v1 = load i32, ptr %p1
  %p2 = getelementptr i32, ptr %p, i32 2
  %v2 = load i32, ptr %p2
  %p3 = getelementptr i32, ptr %p, i32 3
  %v3 = load i32, ptr %p3
  %p4 = getelementptr i32, ptr %p, i32 4
  %v4 = load i32, ptr %p4
  %p5 = getelementptr i32, ptr %p, i32 5
  %v5 = load i32, ptr %p5
  %p6 = getelementptr i32, ptr %p, i32 6
  %v6 = load i32, ptr %p6
  %p7 = getelementptr i32, ptr %p, i32 7
  %v7 = load i32, ptr %p7
  %s0 = add i32 %v0, %a
  %s1 = add i32 %v1, %b
  %s2 = add i32 %v2, %a
  %s3 = add i32 %v3, %b
  %s4 = add i32 %v4, %a
  %s5 = add i32 %v5, %b
  %s6 = add i32 %v6, %a
  %s7 = add i32 %v7, %b
  call void @ext(i32 %s0, i32 %s1, i32 %s2, i32 %s3, i32 %s4, i32 %s5, i32 %s6, i32 %s7)
  %r = add i32 %s0, %s1
  %r2 = add i32 %r, %s2
  %r3 = add i32 %r2, %s3
  %r4 = add i32 %r3, %s4
  %r5 = add i32 %r4, %s5
  %r6 = add i32 %r5, %s6
  %r7 = add i32 %r6, %s7
  ret i32 %r7
}

; CHECK-LABEL: vf0_full_spill_copy_baseline:
; Fixed Full-only spill-kpi (CSR + folded spills across the call). Soft
; nonzero regex is false-green; pin exact durable counts.
; CHECK: #<spill-kpi> @vf0_full_spill_copy_baseline spills=10 spill-bytes=40 reloads=10 reload-bytes=40
; Frame allocate and CSR save surface present (Full 16 B parcels).
; CHECK: subi32
; CHECK: st32
; Call site survives (forces spill/copy pressure).
; CHECK: {{lui|addi32}}{{.*}}ext
; CHECK: jalr{{.*}}lr
; Reloads + epilogue restore.
; CHECK: ld32
; CHECK: jalr
; Full-only: no compact multi-width residue.
