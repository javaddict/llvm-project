; REQUIRES: asserts
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=LOOP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 \
; RUN:     -debug-only=haydn-late-convergence < %s -o /dev/null 2>&1 \
; RUN:     | FileCheck %s --check-prefix=DBG
;
; W68.3R spill-frame exit-row: values live across a call force RA spills
; and a real frame. Under -haydn-sms2 the bounded loop must reach a
; fixed point with BranchRelaxation last (S2 is not rerun on pure
; packing churn). Pins:
;   * spill-kpi is nonzero (frame exists);
;   * folded spill/reload against sp survive S2;
;   * SP adjust (subi32/addi32) still frames the body;
;   * converges without bound exhaustion;
;   * default-off still spills (RA is pre-S2).

declare void @sink(i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @conv_spills(ptr nocapture readonly %p) nounwind {
entry:
  %p0 = getelementptr i32, ptr %p, i32 0
  %v0 = load i32, ptr %p0, align 4
  %p1 = getelementptr i32, ptr %p, i32 1
  %v1 = load i32, ptr %p1, align 4
  %p2 = getelementptr i32, ptr %p, i32 2
  %v2 = load i32, ptr %p2, align 4
  %p3 = getelementptr i32, ptr %p, i32 3
  %v3 = load i32, ptr %p3, align 4
  %p4 = getelementptr i32, ptr %p, i32 4
  %v4 = load i32, ptr %p4, align 4
  %p5 = getelementptr i32, ptr %p, i32 5
  %v5 = load i32, ptr %p5, align 4
  %p6 = getelementptr i32, ptr %p, i32 6
  %v6 = load i32, ptr %p6, align 4
  %p7 = getelementptr i32, ptr %p, i32 7
  %v7 = load i32, ptr %p7, align 4
  %p8 = getelementptr i32, ptr %p, i32 8
  %v8 = load i32, ptr %p8, align 4
  %p9 = getelementptr i32, ptr %p, i32 9
  %v9 = load i32, ptr %p9, align 4
  %p10 = getelementptr i32, ptr %p, i32 10
  %v10 = load i32, ptr %p10, align 4
  %p11 = getelementptr i32, ptr %p, i32 11
  %v11 = load i32, ptr %p11, align 4
  %p12 = getelementptr i32, ptr %p, i32 12
  %v12 = load i32, ptr %p12, align 4
  %p13 = getelementptr i32, ptr %p, i32 13
  %v13 = load i32, ptr %p13, align 4
  %p14 = getelementptr i32, ptr %p, i32 14
  %v14 = load i32, ptr %p14, align 4
  %p15 = getelementptr i32, ptr %p, i32 15
  %v15 = load i32, ptr %p15, align 4
  call void @sink(i32 %v0, i32 %v1, i32 %v2, i32 %v3, i32 %v4, i32 %v5, i32 %v6, i32 %v7)
  call void @sink(i32 %v8, i32 %v9, i32 %v10, i32 %v11, i32 %v12, i32 %v13, i32 %v14, i32 %v15)
  %s = add i32 %v0, %v15
  ret i32 %s
}

; OFF-LABEL: conv_spills:
; OFF: #<spill-kpi> @conv_spills spills={{[1-9][0-9]*}} spill-bytes={{[1-9][0-9]*}} reloads={{[1-9][0-9]*}} reload-bytes={{[1-9][0-9]*}}
; OFF: subi32{{.*}}sp, sp,
; OFF: Folded Spill
; OFF: Folded Reload
; OFF: addi32{{.*}}sp, sp,
; OFF: jalr

; LOOP-LABEL: conv_spills:
; LOOP: #<spill-kpi> @conv_spills spills={{[1-9][0-9]*}} spill-bytes={{[1-9][0-9]*}} reloads={{[1-9][0-9]*}} reload-bytes={{[1-9][0-9]*}}
; LOOP: subi32{{.*}}sp, sp,
; LOOP: Folded Spill
; LOOP: Folded Reload
; LOOP: addi32{{.*}}sp, sp,
; LOOP: jalr

; DBG: HaydnLateConvergence: conv_spills bound={{[0-9]+}} (cond={{[0-9]+}} hwloop=0)
; DBG: HaydnLateConvergence: closed after {{[0-9]+}} iteration(s) (no upward event)
; DBG-NOT: exhausted
