; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     < %s | FileCheck %s --check-prefix=ASM
;
; P17(a): MaxLatencyFinder + isSimplifiableReservedReg stay default-off.
; W68.2: the -haydn-postra-interblock IncludeStages brick is superseded by
; the inter-block DDG substrate (fail-closed until its HC#0 walker exception
; lands); the flag arm is retired. P17(c): SWPSolver is Z3; Haydn does not
; ship LLVM_WITH_Z3, so the seat is fail-closed (swpsolver=unavailable). W68.1: post-RA SMS host deleted; only the P17(a) mutation arms remain.
;
; ASM-LABEL: p17_mutations:
; ASM: jalr

define i32 @p17_mutations(ptr nocapture readonly %a, ptr nocapture readonly %b,
                          i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
body:
  %i = phi i32 [ %n, %pre ], [ %inext, %body ]
  %s = phi i32 [ 0, %pre ], [ %add, %body ]
  %inext = add nsw i32 %i, -1
  %pa = getelementptr inbounds i32, ptr %a, i32 %inext
  %pb = getelementptr inbounds i32, ptr %b, i32 %inext
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %t0 = add i32 %va, %vb
  %add = add i32 %s, %t0
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body, !llvm.loop !0
exit:
  %r = phi i32 [ 0, %entry ], [ %add, %body ]
  ret i32 %r
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i64 16}
