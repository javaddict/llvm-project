; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj < %s \
; RUN:   -o %t.o
;
; REGRESSION TEST: hwloop ÷4 alignment of BOTH HWLR_BEGIN and HWLR_END
; (— supersedes the partial SET-only fix in c8013cb34c4d /).
;
; Bug (the case c8013cb34c4d did NOT cover): the SET_HWLOOP parcel is only ONE
; source of misalignment. Haydn parcels are variable-width (2/4/6/8 bytes).
; HWLR_BEGIN / HWLR_END land at 2-mod-4 whenever the cumulative size of parcels
; before them ≡ 2 mod 4 — i.e. an ODD count of {2-byte, 6-byte} parcels
; (2≡2, 6≡2, 4≡0, 8≡0 mod 4). So the c8013cb SET-only pad still leaves the
; stream at 2-mod-4 when a 6-byte WIDE parcel follows the SET before the loop
; body — e.g. a hoisted 48-bit `addi32{{(_w)?}}` constant materialization in the
; preheader (`s += a[i] * 0x12345`):
;
; { set_hwloop_f2_w 1,.LBB0_2,.LLhwloop_end0, r3 } # 6-byte WIDE SET
; p2align 4 # c8013cb SET-pad
; { addi32{{(_w)?}} r1, r0,...;... } # 6-byte WIDE parcel
; LBB0_2: # HWLR_BEGIN at 2-mod-4
;
; The fixup resolver then rejects BOTH offset fields:
; error: hwloop offset must be 16-byte Bundle128 (A.6) aligned (HaydnAsmBackend.cpp:597)
; aborting -filetype=obj / -c. -S textual was fine (no fixup resolution).
;
; Fix : mirror the existing inclusive-END temp-label mechanism
; (PendingHwloopEndLabels + getOrCreateHwloopEndSym) for START. Create a temp
; Lhwloop_start symbol referenced by the SET fixups, and emit
; `OutStreamer->emitCodeAlignment(Align(4), &getSubtargetInfo)` immediately
; BEFORE emitting BOTH the START label (at the loop body's first emitted instr)
; and the END label (at the latch's last real instr). This guarantees 16-byte Bundle128 (A.6)
; alignment regardless of intervening 2/6-byte parcels. MBB setAlignment is NOT
; used — LLVM's AsmPrinter elides alignment for fallthrough blocks.
;
; Test design: three parcel-combo cases, each MUST assemble to a valid object.
; * @sum_arr — all-16-byte Bundle128 (A.6) body (regression of the c8013cb case).
; * @bigimm — 6-byte WIDE parcel (hoisted `* 0x12345` materialization)
; between the SET and the loop body. The original repro:
; failed with 2 align errors before.
; * @nested_outer — nested hwloop (outer uses LoopJNZ software loop, inner
; uses hwloop); exercises a second SET_HWLOOP and confirms
; the per-instance temp labels stay distinct.
;
; ASM confirms a `.p2align 4` (== 16-byte Bundle128 (A.6)) directive precedes the loop-body
; label. The second RUN is the load-bearing one — it aborts pre-fix.

;parcel-combo: all-16-byte Bundle128 (A.6) body (c8013cb regression)
define i32 @sum_arr(ptr %a, i32 %n) {
; ASM-LABEL: sum_arr:
; ASM:       set_hwloop
; 16-byte Bundle128 (A.6) pad so HWLR_BEGIN (the next emitted label) is ÷4-representable:
; ASM:       .p2align 4
entry:
  br label %for.body

for.body:
  %i = phi i32 [ 0, %entry ], [ %next, %for.body ]
  %s = phi i32 [ 0, %entry ], [ %acc, %for.body ]
  %p = getelementptr inbounds i32, ptr %a, i32 %i
  %v = load i32, ptr %p
  %acc = add i32 %s, %v
  %next = add i32 %i, 1
  %cond = icmp slt i32 %next, %n
  br i1 %cond, label %for.body, label %for.end

for.end:
  %r = phi i32 [ %acc, %for.body ]
  ret i32 %r
}

;parcel-combo: 6-byte WIDE parcel (hoisted constant materialization) between
; the SET_HWLOOP and the loop body. Original repro.
define i32 @bigimm(ptr %a, i32 %n) {
; ASM-LABEL: bigimm:
; ASM:       set_hwloop
; ASM:       .p2align 4
entry:
  br label %for.body

for.body:
  %i = phi i32 [ 0, %entry ], [ %next, %for.body ]
  %s = phi i32 [ 0, %entry ], [ %acc, %for.body ]
  %p = getelementptr inbounds i32, ptr %a, i32 %i
  %v = load i32, ptr %p
  %mul = mul i32 %v, 74565           ; 0x12345 — forces a hoisted WIDE addi32{{(_w)?}}
  %acc = add i32 %s, %mul
  %next = add i32 %i, 1
  %cond = icmp slt i32 %next, %n
  br i1 %cond, label %for.body, label %for.end

for.end:
  %r = phi i32 [ %acc, %for.body ]
  ret i32 %r
}

;parcel-combo: nested hwloop — per-instance START/END labels must stay distinct.
define i32 @nested_hwloop(ptr noalias %a, i32 %n, i32 %m) {
; ASM-LABEL: nested_hwloop:
; ASM:       set_hwloop
; ASM:       .p2align 4
entry:
  br label %outer.header

outer.header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %outer.latch ]
  %s = phi i32 [ 0, %entry ], [ %s.inner, %outer.latch ]
  br label %inner.body

inner.body:
  %j = phi i32 [ 0, %outer.header ], [ %j.next, %inner.body ]
  %si = phi i32 [ %s, %outer.header ], [ %si.acc, %inner.body ]
  %idx = add i32 %i, %j
  %p = getelementptr inbounds i32, ptr %a, i32 %idx
  %v = load i32, ptr %p
  %si.acc = add i32 %si, %v
  %j.next = add i32 %j, 1
  %c.j = icmp slt i32 %j.next, %m
  br i1 %c.j, label %inner.body, label %outer.latch

outer.latch:
  %s.inner = phi i32 [ %si.acc, %inner.body ]
  %i.next = add i32 %i, 1
  %c.i = icmp slt i32 %i.next, %n
  br i1 %c.i, label %outer.header, label %for.end

for.end:
  %r = phi i32 [ %s.inner, %outer.latch ]
  ret i32 %r
}
