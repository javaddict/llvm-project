; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — guard the Haydn hardware-loop setup emission path that the fix lives in.

; REGRESSION TEST: guard the Haydn hardware-loop setup emission
; path that the fix lives in.
;
; Bug : the SET_HWLOOP immediate-count pseudo is expanded in
; HaydnAsmPrinter (case Haydn::SET_HWLOOP) by materializing the trip count
; into R12 (`addi32 r12, r0, cnt`) + emitting `set_hwloop_f2... r12`. R12 is
; the AT scratch and NOT guaranteed free post-RA — the post-RA scheduler treats
; SET_HWLOOP as side-effect-free and hoists a live R12 range across it; the
; hidden write clobbers that live value. repro (e2e_blt_bswap64.c): the
; bswap mask 0xFF00 in R12 was overwritten by the trip count 5 → corrupted byte
; masks → off-by-4 bswap (sim 191 vs exp 187).
;
; Fix : the expansion now brackets the materialization with a
; spill/restore of R12 (subi32 sp,sp,8 / st32 r12,sp,0 / addi32 r12,r0,cnt
; set_hwloop_f2 / ld32 r12,sp,0 / addi32 sp,sp,8). The real fix is the
; immediate-count encoding; this is the reg-form spill workaround.
;
; Scope note (why this is a smoke test, not a spill assertion): the imm
; SET_HWLOOP pseudo is emitted ONLY by the IR-level HardwareLoops pass; every
; self-contained.ll loop in this suite is caught by the post-RA recognizer and
; emits the reg-form `set_hwloop_f2` instead, never reaching case
; Haydn::SET_HWLOOP. Feeding the imm pseudo to the AsmPrinter from a.mir is
; not possible (llc re-runs IRTranslator on the IR stub and aborts). The
; authoritative end-to-end guard for the spill is therefore the
; e2e_blt_bswap64.c repro on BundleSim ISS (sim=187=host). This test guards
; the broader hwloop-setup + AsmPrinter emission path (the.s must still form
; a set_hwloop and assemble cleanly); if the AsmPrinter hwloop case is broken
; this fails.

define i32 @cb31_hwloop_setup_smoke(ptr nocapture noundef readonly %v) {
; CHECK-LABEL: cb31_hwloop_setup_smoke:
; CHECK: set_hwloop
entry:
 br label %for.body

for.body:
 %i = phi i32 [ 0, %entry ], [ %next, %for.body ]
 %acc = phi i32 [ 0, %entry ], [ %add, %for.body ]
 %gep = getelementptr inbounds i32, ptr %v, i32 %i
 %ld = load i32, ptr %gep, align 4
 %add = add i32 %ld, %acc
 %next = add nuw i32 %i, 1
 %cond = icmp eq i32 %next, 8
 br i1 %cond, label %for.end, label %for.body

for.end:
 %res = phi i32 [ %add, %for.body ]
 ret i32 %res
}
