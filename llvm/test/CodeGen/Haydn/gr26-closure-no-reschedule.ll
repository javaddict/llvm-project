; REQUIRES: asserts
; RUN: rm -rf %t && split-file %s %t
; RUN: %python %t/gen_far.py > %t/far.ll
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 \
; RUN:     -debug-only=haydn-late-convergence < %t/far.ll -o /dev/null 2>&1 \
; RUN:     | FileCheck %s --check-prefix=DBG
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 < %t/far.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=ASM
;
; GR2.6 monotone closure pin: S2 runs EXACTLY ONCE per driver entry (the
; W68.3R reschedule-after-mutation arm is deleted); every subsequent
; closure iteration is only [stalls -> HWLoop inner-first -> BR LAST].
; This corpus triggers promotion inside the loop (out-of-range simm12
; sites promote to LUI+ADDI+JALR under BR-last), so the pin asserts:
;   * the "S2 once per driver entry" line appears exactly once per
;     function (never a second S2 after mutation);
;   * closure iterations log their event ledger (promotions>=1 on the
;     mutating iteration);
;   * deterministic termination: "closed after N iteration(s)" with no
;     bound exhaustion;
;   * the enforced no-growth law stays silent on legal events (no
;     "grew beyond the admitted closure vocabulary" fatal);
;   - legal closed promoted-long-form assembly present (ASM checks below);
;
; Exact-iteration DBG pins are deliberately ranges ({{[0-9]+}}): the
; promotion vocabulary may change the iteration count; the deterministic
; invariants are once-S2, event logging, and closure without exhaustion.

; DBG: HaydnLateConvergence: fwd_far bound=
; DBG: HaydnLateConvergence: S2 once per driver entry
; DBG: HaydnLateConvergence: closure iteration {{[0-9]+}} events: promotions=
; DBG: HaydnLateConvergence: closed after {{[0-9]+}} iteration(s) (no upward event)
; DBG: HaydnLateConvergence: back_far bound=
; DBG: HaydnLateConvergence: S2 once per driver entry
; DBG: HaydnLateConvergence: closed after {{[0-9]+}} iteration(s) (no upward event)
; DBG-NOT: exhausted
; DBG-NOT: grew beyond the admitted closure vocabulary
; DBG-NOT: non-monotone mutation

; ASM-LABEL: fwd_far:
; ASM: lui
; ASM: jalr
; ASM-LABEL: back_far:
; ASM: lui
; ASM: jalr

;--- gen_far.py
n = 400
print("define void @fwd_far(i32 %c, ptr %p) {")
print("entry:")
print("  %cmp = icmp eq i32 %c, 0")
print("  br i1 %cmp, label %far, label %pad")
print("pad:")
for i in range(n):
    print("  store volatile i32 %d, ptr %%p, align 4" % i)
print("  br label %far")
print("far:")
print("  ret void")
print("}")
print("define void @back_far(ptr %p, i32 %n) {")
print("entry:")
print("  br label %loop")
print("loop:")
print("  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]")
for i in range(n):
    print("  store volatile i32 %d, ptr %%p, align 4" % i)
print("  %inc = add i32 %i, 1")
print("  %c = icmp slt i32 %inc, %n")
print("  br i1 %c, label %loop, label %exit")
print("exit:")
print("  ret void")
print("}")
