; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; Single-BB ZOL: START/END labels must both be present and distinct, and the
; body must satisfy the two hard rules in VLIW_Engine_Compiler_Constraints.md
; § HW Loop:
;
;   * "SET_HWLOOP ... must be issued at or before bundle t-3" where t is the
;     first body bundle  -> 3 bundles between SET and START
;   * "Loop Body: It must contain at least 3 instruction bundles"
;
; The body work sits BETWEEN START and END because END is inclusive: the
; AsmPrinter places it before the LAST real body MI ("END address = last real
; body MI"). The previous version of this test asserted START -> END -> add32,
; i.e. END before the body work, which contradicted that and only held while
; MinBodyBundles was 0 and the body was a single bundle.
;
; Body composition here, all three required:
;   1. the real work (st32_post + add32)
;   2. a latency stall — st32_post writes the base register back, and Haydn has
;      no interlock, so the next bundle must not read it (HaydnLatencyStalls)
;   3. a min-body pad (HaydnFixupHwLoops) carrying the inclusive END

define void @tiny_body(ptr nocapture %p, i32 %n) {
; CHECK-LABEL: tiny_body:
; CHECK: set_hwloop_f2{{.*}}[[START:\.LLhwloop_start[0-9]+]], [[END:\.LLhwloop_end[0-9]+]]
; Setup gap: SET at or before t-3.
; CHECK: { nop; nop; nop }
; CHECK: { nop; nop; nop }
; CHECK: { nop; nop; nop }
; CHECK: [[START]]:
; Body bundle 1: the work.
; CHECK: addi32
; Body bundle 2: latency stall for the st32_post base writeback.
; CHECK: { nop; nop; nop }
; Body bundle 3 carries the inclusive END.
; CHECK: [[END]]:
; CHECK: { nop; nop; nop }
; CHECK: jalr
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i2, %loop ]
  %gep = getelementptr inbounds i32, ptr %p, i32 %i
  store i32 %i, ptr %gep, align 4
  %i2 = add nuw i32 %i, 1
  %c = icmp ult i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  ret void
}
