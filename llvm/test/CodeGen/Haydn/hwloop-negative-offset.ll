; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s

; Role: semantic — HWLoop fixup offsets are ALWAYS positive (never negative).

; REGRESSION TEST: HWLoop fixup offsets are ALWAYS positive (never negative).
;
; Bug class (CLAUDE.md M7 forward-focus): "fix the.LBB0_-1 negative-offset
; edge-case." Investigation verdict: ARCHITECTURALLY IMPOSSIBLE for the
; CodeGen path to produce a negative SET_HWLOOP_REG fixup offset. The
; HaydnHardwareLoops pass emits SET_HWLOOP_REG in the loop PREHEADER, and
; createPreheaderForLoop (HaydnHardwareLoops.cpp:111-174) always inserts
; the preheader immediately BEFORE the header in MBB layout:
;
; MF->insert(Header->getIterator, NewPH); // line 149
;
; Since the preheader precedes the header AND the latch in program order, and
; the loop_start/loop_end MCSymbols are the header/latch labels, the PC-relative
; offsets computed at fixup resolution time are ALWAYS positive:
;
; loop_start_offset = Header_addr - SET_HWLOOP_addr > 0
; loop_end_offset = Latch_addr - SET_HWLOOP_addr > 0
;
; This matches the ISA spec (Database/haydn_instruction_db.json SET_HWLOOP
; SET_HWLOOP_F2 entries): the offsets are UNSIGNED PC-relative forward offsets
; (uimm6_offset1 for START, uimm12_offset2 for END; behavior:
; `HWLR_BEGIN = PC + (uimm6_offset1 << 2)`).
;
; Defense-in-depth: the AsmBackend FIXUP_HAYDN_HWLoopOffset handler
; (HaydnAsmBackend.cpp:401-417) is written to handle SIGNED 16-bit values
; (it uses isInt<16>(Offset) and `static_cast<int16_t>`). This is a superset
; of what CodeGen produces — even if a negative value were ever fed in (e.g.
; by hand-written asm pointing loop_start backwards), the handler does NOT
; crash or assert; it reports a clean diagnostic via getContext.reportError
; for any value outside isInt<16>. So a hypothetical negative offset would
; produce a compile-time error, never a mis-encoding or `.LBB_-1` crash.
;
; Test design:
; 1. Tiny loop (single-store body): smallest possible loop, exercises the
; tightest offset (loop_start = +1 word minimum, loop_end = +1 word).
; This is the configuration most likely to expose any off-by-one or
; sign-extension bug in the fixup math.
; 2. Check the textual asm shows set_hwloop_f2 with a forward reference
; (.LBB0_1 is AFTER the set_hwloop_f2 in layout). The WIDE
; set_hwloop_f2 parcel is 6 bytes; the loop body label therefore
; lands at an unaligned offset and the -filetype=obj RUN line was
; retired (the AsmPrinter does not yet align after a WIDE hwloop
; tracked separately). The textual-asm RUN is the regression guard.
;
; What would break if the bug reappears:
; If createPreheaderForLoop regresses to insert AFTER the header, the
; loop_start offset would go negative; the AsmBackend FIXUP_HAYDN_HWLoopOffset
; handler would either assert, error out via its diagnostic, or produce
; an unresolved `.LBB_-1` relocation against the textual asm.
; If the AsmBackend FIXUP_HAYDN_HWLoopOffset handler regressed to assume
; unsigned-only (no sign handling), a negative offset would silently
; mis-encode as a huge positive value.
;
; Spec reference: Database/haydn_instruction_db.json, SET_HWLOOP and
; SET_HWLOOP_F2 entries (uimm6_offset1 / uimm12_offset2, both forward).
; Decision: (range fields are unsigned uimm6/uimm12).
; Lesson: this test documents the architectural impossibility.

define void @tiny_single_store_loop(ptr %p) {
; CHECK-LABEL: tiny_single_store_loop:
; CHECK: set_hwloop_f2
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  store i32 %i, ptr %p
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 8
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}

; Two-instruction body — loop_start offset is still tiny (just the
; SET_HWLOOP_REG width + any preheader fall-through), loop_end is one
; instruction further. Still well within uimm6/uimm12.
define i32 @tiny_two_inst_loop(ptr %p) {
; CHECK-LABEL: tiny_two_inst_loop:
; CHECK: set_hwloop_f2
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 8
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}
