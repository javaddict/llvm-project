; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:     -verify-machineinstrs < %s 2>&1 | FileCheck %s

; REGRESSION TEST: DR64PackBaseSpillFI huge-frame simm20 overflow (tier 3).
;
; Bug: the DR64 pack-slot fallback spilled the scavenged base GPR to
; DR64PackBaseSpillFI via ADDI32_W (simm20 immediate). When the spill slot's
; own FrameReg-relative byte offset exceeded simm20 (~512KB), the helper
; fatal-ed:
;   LLVM ERROR: Haydn: DR64PackBaseSpillFI offset exceeds simm20
; The SAME ceiling existed in HaydnPostRAScratch's emitScratchMemOp. Both
; aborted compilation mid-stream on huge frames that triggered a DR64 pack
; (LOADI64 both-halves-nonzero constants; MOV_GPR_TO_DR64 two-live-GPR pack).
;
; Fix (one closed rule, HARD #7 - never a fatal, never an SP motion):
;   Addressing of any in-frame spill slot uses THREE monotone tiers tied to
;   offset magnitude, factored as ONE shared helper (emitFrameRelativeMemOp)
;   used by BOTH emitDR64PackBaseSpill AND emitScratchMemOp:
;     tier 1 - short-form element-indexed ST32/LD32 (Off/4 fits isInt<6>)
;     tier 2 - ADDI32_W R0, FrameReg, Off + ST32/LD32 R0, 0 (Off fits simm20)
;     tier 3 - MatInt(Off) real ops chained through R0 + ADD32 R0, FrameReg, R0
;              + ST32/LD32 R0, 0  (any remaining Off)
;   Tier 3 emits REAL HaydnMatInt ops (LUI + ADDI32_W universal 2-instr
;   12+20 split), NEVER the LOADI32 pseudo: the helper runs INSIDE
;   expandPostRAPseudo / PostRAScratch, so a LOADI32 inserted here is never
;   re-expanded and fatals AsmPrinter's residual cycle-forming pseudo check.
;   Same HaydnMatInt::generate mechanism as expandPostRAPseudo's LOADI32 case.
;   Tier 3 uses R0 as a self-contained scratch (chains from soft-zero R0,
;   restores via XOR32 R0,R0,R0 - same R0 contract tier 2 already uses).
;   SP is NEVER moved; the addressed pointer is a plain GPR.
;
; Test design: a non-leaf with a var-sized alloca (forces hasFP -> FrameReg
; is FP), a >simm20-byte static locals array ([131072 x i32] = 524288 bytes)
; that pushes DR64PackBaseSpillFI's FP-relative offset past simm20, clobbered
; Callee-Saved Registers (R8-R11 + D8-D15) to force CSR spills into the frame
; and consume free GPRs (drives withDR64PackBase NeedsSpill=true), and a
; both-halves-nonzero i64 constant (0x2_0000_0001 = 8589934593) which selects
; LOADI64 general -> pack path. Without the fix, llc aborts with:
;   LLVM ERROR: Haydn: DR64PackBaseSpillFI offset exceeds simm20
; With the fix, llc completes and emits the large-offset addressing sequence.
;
; The no-SP-motion invariant guards the WHOLE function body - the pre-fix
; dynamic transient (subi32 sp,sp,8 / addi32 sp,sp,8) must NOT reappear.

; First line of defense: the pre-fix LLVM ERROR must NOT appear.
; CHECK-NOT: LLVM ERROR

define i64 @dr64_pack_base_spill_overflow(i32 %a, i32 %n) nounwind {
; CHECK-LABEL: dr64_pack_base_spill_overflow:
; The no-SP-motion invariant: the pre-fix bug opened a dynamic transient with
; `subi32 sp, sp, 8` before the pack and closed it with `addi32 sp, sp, 8`
; after. These CHECK-NOT directives cover prologue -> pack -> epilogue. The
; legitimate prologue/epilogue SP adjust uses the FULL frame size (not 8), so
; the literal `sp, sp, 8` match stays green there.
; CHECK-NOT:    subi32    sp, sp, 8
; CHECK-NOT:    addi32  sp, sp, 8
entry:
  ; var-sized alloca forces hasFP (FrameReg = FP).
  %slot = alloca i32, i32 %n
  ; > simm20-byte static locals array (524288 bytes = 512KB) pushes
  ; DR64PackBaseSpillFI's FP-relative offset past the simm20 range.
  %arr1 = alloca [131072 x i32], align 8
  ; Clobber R8-R11 + D8-D15: PEI spills all of them into the frame and
  ; consumes free GPRs so withDR64PackBase's scavenger hits NeedsSpill=true.
  %r8 = call i32 asm sideeffect "",
    "={r8},{r8},~{r9},~{r10},~{r11},~{d8},~{d9},~{d10},~{d11},~{d12},~{d13},~{d14},~{d15},~{memory}"(i32 %a)
  ; i64 0x2_0000_0001 (both halves nonzero) selects LOADI64 general -> pack.
  %cmp = icmp eq i32 %a, 0
  %sel = select i1 %cmp, i64 8589934593, i64 0
  store volatile i32 %r8, ptr %slot
  store volatile i32 %a, ptr %arr1
  ret i64 %sel
}

; The DR64 pack signature: two ST32 of the GPR32 halves followed by LD64 of
; the full DR64. The base addressing may use any of the three tiers; the
; load-bearing assertion is that the pack completes (ST32, ST32, LD64 all
; present) without the simm20 fatal and without SP motion.
; CHECK:       st32      r{{[0-9]+}}, [[BASE:r[0-9]+|fp|sp]], {{[0-9]+}}
; CHECK:       st32      r{{[0-9]+}}, [[BASE]], {{[0-9]+}}
; CHECK:       ld64      d{{[0-9]+}}, [[BASE]], {{[0-9]+}}

; Tier 3 large-offset materialization signature (when DR64PackBaseSpillFI
; overflows simm20): the addressed base is built by REAL MatInt ops (LUI +
; ADDI32_W - the universal 2-instr 12+20 split that covers the whole 32-bit
; space), then ADD32 of FrameReg. NEVER the LOADI32 pseudo (it would survive
; as a residual cycle-forming pseudo child - see header). The materialised
; offset value carries the wide displacement that simm20 cannot encode.
; CHECK:       lui       r{{[0-9]+}}, {{[0-9]+}}
; CHECK:       addi32  r{{[0-9]+}}, r{{[0-9]+}}, {{-?[0-9]+}}
; CHECK:       add32     r{{[0-9]+}}, {{fp|sp}}, r{{[0-9]+}}

; Tail of the no-SP-motion window (pack -> epilogue).
; CHECK-NOT:   subi32    sp, sp, 8
; CHECK-NOT:   addi32  sp, sp, 8
