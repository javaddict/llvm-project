; REQUIRES: asserts
; RUN: rm -rf %t && split-file %s %t
; RUN: %python %t/gen_far.py > %t/far.ll
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs %t/far.ll -o /dev/null
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnLateConvergence.cpp \
; RUN:     --check-prefix=CENSUS
;
; W68.4 HOLD-RUNTIME: S2 identity-bakes insertIndirectBranch JALR_W onto
; generated JALR_E2_/JALR_E3_ members (AIEMachineScheduler.cpp:1126-1132
; setDesc). The LateConvergence monotone census must still see those as
; indirect sites (logicalOpcodeOrSelf). Otherwise a later S2/BR iteration
; reports a false "regressed to a PC-relative form" abort (core_matrix.c).
; Product -haydn-sms2 and hwloops stay on.
;
; D1.32 note: far_loop's stores are %i-DERIVED (rematerializable), not 500
; distinct constants. With distinct constants every GPR is genuinely live
; across the backedge; the long-latch JALR scratch probe then finds no
; computed-dead register and the demote correctly REFUSES (fail-closed —
; the pre-D1.32 code "passed" by silently reusing the countdown register,
; which miscompiled: BEQZ read the LUI-clobbered scratch). This test's
; subject is the census, not GPR-exhaustion; the derived form keeps the
; pressure realistic while still overflowing Off2 (long latch present).

; CENSUS: logicalOpcodeOrSelf
; CENSUS: Name.starts_with("JALR")

;--- gen_far.py
n = 500
print("define void @far_then_ret(i32 %c, ptr %p) {")
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
print("define i32 @far_loop(ptr %p, i32 %n) {")
print("entry:")
print("  br label %loop")
print("loop:")
print("  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]")
for i in range(n):
    print("  %%v%d = add i32 %%i, %d" % (i, i))
    print("  store volatile i32 %%v%d, ptr %%p, align 4" % i)
print("  %inc = add i32 %i, 1")
print("  %c = icmp slt i32 %inc, %n")
print("  br i1 %c, label %loop, label %exit")
print("exit:")
print("  ret i32 %i")
print("}")
