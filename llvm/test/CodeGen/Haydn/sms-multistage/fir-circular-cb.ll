; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-analysis-only \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; G006 circular-AR FIR probe (G002 follow-up) — ANALYSIS-ONLY arm.
; Class: circular-buffer (CBR) history walk via llvm.haydn.ldw.cb.imm
; (AGU wraps the pointer within the programmed CBR set; the intrinsic
; returns {data, updated_ptr}), i32 truncation, MAC, store.
;
; REGRESSION TEST LAW / OPEN BUG PIN (owner topics/scheduling):
; The engine ACCEPTS this kernel at analysis (kind=accepted-analysis,
; point-in-time II=8 NS=1), but the COMMIT path fails
; -verify-machineinstrs with tied-operand machine-code errors on the CB
; member (D_LDW_CB_IMM defines $dN plus the tied rs_wb GPR; the committed
; kernel violates the tie constraint 4x). Isolated 2026-08-23 @
; 4d80cd078d67: 0 verifier errors with -haydn-enable-multistage-sms=0;
; 0 with analysis-only; 4 with commit. Do NOT drop -verify-machineinstrs
; or add a commit arm until that is fixed — this fixture pins the
; acceptance so the commit-path fix has a ready home (flip the RUN to
; commit + kind=accepted + an ASM d_ldw_cb_imm parcel arm).
;
; Also NOTE (probe finding): the full i64-accumulating CB FIR
; softexpands (i64 split/recombine) past the engine's 96-instruction
; body cap and reports kind=not-candidate — a BODY-CAP consequence, not
; an engine defect; do not "fix" by raising MaxBodyInstrs without a
; plan-tree change.
;
; RMK: Schedule found II=[[II:[0-9]+]] NS=[[NS:[0-9]+]] prologue=[[P:[0-9]+]] parcels epilogue=[[E:[0-9]+]] parcels kind=accepted-analysis loop=bb.{{[0-9]+}}.loop
;
; ASM: fir_cb_lean:
; ASM: set_hwloop_f2
; ASM: jalr

target triple = "haydn-unknown-elf"

declare { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr, i32 immarg, i32 immarg)

define void @fir_cb_lean(ptr %hist, ptr readonly %coef, ptr %out, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i  = phi i32 [0, %pre], [%i.n, %loop]
  %s  = phi i32 [0, %pre], [%s.n, %loop]
  %hp = phi ptr [%hist, %pre], [%hp.n, %loop]
  %ld = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %hp, i32 0, i32 8)
  %x64 = extractvalue { i64, ptr } %ld, 0
  %hp.n = extractvalue { i64, ptr } %ld, 1
  %xv = trunc i64 %x64 to i32
  %c0 = load i32, ptr %coef, align 4
  %m0 = mul i32 %xv, %c0
  %m1 = mul i32 %xv, %xv
  %a0 = add i32 %m0, %m1
  %s.n = add i32 %a0, %s
  store i32 %s.n, ptr %out, align 4
  %i.n = add i32 %i, 1
  %cc = icmp ult i32 %i.n, %n
  br i1 %cc, label %loop, label %exit, !llvm.loop !0
exit:
  ret void
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}
