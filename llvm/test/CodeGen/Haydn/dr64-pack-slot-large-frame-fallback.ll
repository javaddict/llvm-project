; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -enable-misched=false -enable-post-misched=false \
; RUN:     -verify-machineinstrs < %s 2>&1 | FileCheck %s

; REGRESSION TEST: DR64 pack slot large-frame fallback (Option A: scavenged base).
;
; Bug: resolveDR64PackSlot fatalled when the DR64PackFI's FrameReg-relative
; byte offset overflowed the ST32/LD64 scaled-simm6 element range
; (ST32 high half at Off+4 must fit isInt<6> after /4 -> byte offset <= 124).
; At O0, 70/74 gcc-c-torture failures (plus 4 O1 and 1 O3 -- 75 fail-slots
; total) hit this fatal during Post-RA pseudo expansion because the
; per-function pack slot lands past the simm6 element range when the frame
; is large. The pack slot is the per-function 8-byte temporary for DR64
; construction from two GPR32 halves (LOADI64 both-halves-nonzero constants;
; MOV_GPR_TO_DR64 two-live-GPR general case). The fatal aborted compilation
; mid-stream on every test that triggered the pack at a large frame offset.
;
; Fix (one closed rule, HARD #7 -- never a fatal, never an SP motion):
;   Pack-slot addressing is short-form (FrameReg + scaled simm6 element) when
;   every element fits isInt<6>; otherwise it falls back to a scavenged-GPR
;   base = FrameReg + full byte offset, addressed at ST32 element 0 (low) /
;   element 1 (high, byte +4) / LD64 element 0. SP is NEVER moved -- the base
;   is a plain GPR materialised via ADDI32_W (or LOADI32+ADD32 for huge
;   offsets > simm20). The short-form fast path stays byte-identical for the
;   common case (small frames, optimized code).
;
; Test design: a non-leaf with a var-sized alloca (forces hasFP -> FrameReg is
; FP), a static locals array that pushes DR64PackFI's FP-relative offset past
; the simm6 range, clobbered Callee-Saved Registers (R8-R11 + D8-D15) to
; force CSR spills into the frame, and a both-halves-nonzero i64 constant
; (0x2_0000_0001 = 8589934593) which selects LOADI64 general -> pack path.
; Without the fix, llc aborts with:
;   LLVM ERROR: Haydn: DR64 pack slot offset exceeds scaled simm6 ...
; With the fix, llc completes and emits the scavenged-base pack sequence:
;   ADDI32_W base, fp, <large negative offset>
;   ST32     scr , base, 0    (low GPR32 half at byte 0)
;   ST32     scr , base, 1    (high GPR32 half at byte +4)
;   LD64     dst , base, 0    (DR64 reload at byte 0)
; The pre-fix dynamic SP transient (subi32 sp / addi32 sp mid-function)
; must NOT reappear -- that broke the no-SP-motion invariant (CB
; mac_mula64_all exit 11). The CHECK-NOT directives below guard against it.
;
; Torture sanity (in-tree compile, not part of this lit): 20010118-1.c at O0
; compiles after the fix.

; First line of defense: the pre-fix LLVM ERROR must NOT appear.
; CHECK-NOT: LLVM ERROR

define i64 @dr64_pack_overflow(i32 %a, i32 %n) nounwind {
; CHECK-LABEL: dr64_pack_overflow:
; The no-SP-motion invariant guards the WHOLE function body. The pre-fix bug
; opened the dynamic transient with `subi32 sp, sp, 8` BEFORE the pack and
; closed it with `addi32 sp, sp, 8` AFTER; these CHECK-NOT directives sit
; before the first positive pack check (covering prologue → pack) and after
; the last (covering pack → epilogue). The legitimate prologue/epilogue SP
; adjust uses the FULL frame size (not 8), so the literal `sp, sp, 8` match
; stays green there.
; CHECK-NOT:    subi32    sp, sp, 8
; CHECK-NOT:    addi32  sp, sp, 8
entry:
  ; var-sized alloca forces hasFP (FrameReg = FP). Static locals push
  ; DR64PackFI past the simm6 element range from FP.
  %slot = alloca i32, i32 %n
  %arr1 = alloca [30 x i32], align 8
  ; Clobber R8-R11 + D8-D15 so PEI spills all of them into the frame.
  %r8 = call i32 asm sideeffect "",
    "={r8},{r8},~{r9},~{r10},~{r11},~{d8},~{d9},~{d10},~{d11},~{d12},~{d13},~{d14},~{d15},~{memory}"(i32 %a)
  ; i64 0x2_0000_0001 (both halves nonzero) selects LOADI64 general -> pack.
  %cmp = icmp eq i32 %a, 0
  %sel = select i1 %cmp, i64 8589934593, i64 0
  store volatile i32 %r8, ptr %slot
  store volatile i32 %a, ptr %arr1
  ret i64 %sel
}

; The scavenged-base pack signature. The ADDI32_W materialises base = FP +
; <large negative byte offset>; the three memory ops then reference that
; SAME base register at element 0/1/0. The [[BASE]] capture locks all four
; checks to the same physreg so the pattern cannot accidentally align with
; CSR spills (which use a different base). The pre-fix fatal is gone (no LLVM
; ERROR above) AND the short-form element-indexed ST32 from FP at large
; element (the overflow shape) must NOT appear -- that would mean the fatal
; path was silently downgraded instead of using the scavenged base.
; CHECK:       addi32  [[BASE:r[0-9]+]], fp, -{{[0-9]+}}
; CHECK:       st32      r{{[0-9]+}}, [[BASE]], 0
; CHECK:       st32      r{{[0-9]+}}, [[BASE]], 1
; CHECK:       ld64      d{{[0-9]+}}, [[BASE]], 0

; Tail of the no-SP-motion window (pack → epilogue).
; CHECK-NOT:   subi32    sp, sp, 8
; CHECK-NOT:   addi32  sp, sp, 8
