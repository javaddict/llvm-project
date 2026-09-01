; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %s | FileCheck %s
;
; REGRESSION TEST: naked functions must compile with ZERO compiler-emitted
; instructions — no prologue, no CSR save/restore, no trailing RET parcel.
;
; Bug (2026-08-18 policy overreach, caught by the B gate): the fail-closed
; wave added Attribute::Naked to hasUnsupportedFnABI
; (GISel/HaydnCallLowering.cpp, e988dabb6a00) and to the
; processFunctionBeforeFrameFinalized fatal (HaydnFrameLowering.cpp,
; c9b60d7dccda). That broke the product llvm-libc setjmp/longjmp build
; ("error in backend: unable to lower arguments: i32 (ptr)") — both files
; in libc/src/setjmp/haydn/ are [[gnu::naked]] asm-only bodies ending in
; their own `jalr_w r0, lr, 0`. The 2026-08-17 sysroot libc.a still
; contains the working objects.
;
; Correct model (RISCV peer — zero special handling; naked-fn-with-frame-
; pointer.ll): naked = default C CC lowering (formals become unused
; vregs), NO frame setup, NO CSR save/restore (generic PEI skips both for
; Naked: PrologEpilogInserter spillCalleeSavedRegs + skip of
; insertPrologEpilogCode), and the body owns control flow — the IR `ret`/
; `unreachable` is the mandatory IR terminator, never an instruction.
; Clang lowers an asm-only naked body to `INLINEASM; unreachable`, so
; HaydnEnsureTerminators (the dead-end soft-RET pass) must stay silent
; too, or a dead `{ nop; jalr r0, lr, 0 }` parcel lands after the body's
; own return (that trailing parcel is visible in the 08-17 object).
;
; Test design: @setjmp_shape mirrors the libc setjmp signature and asm
; (writes CSRs in asm — undeclared clobbers, so no machine CSR use; own
; jalr_w return). @ret_shape pins the lowerReturn gate for hand-written
; IR whose naked body ends in `ret` instead of `unreachable`.
; CHECK-NOT before/after the body proves the "zero compiler
; instructions" law on both sides of the APP region; the CHECK-NOT: jalr
; after the asm's own jalr_w pins that no second (soft-RET / epilogue)
; return parcel follows the body's return.

define i32 @setjmp_shape(ptr %buf) naked {
; CHECK-LABEL: setjmp_shape:
; CHECK-NOT: xor32
; CHECK-NOT: subi32
; CHECK-NOT: addi32{{[ \t]+}}sp,{{[ \t]+}}sp
; CHECK-NOT: st32{{[ \t]+}}r[0-9]+,{{[ \t]+}}sp
; CHECK-NOT: st64{{[ \t]+}}d[0-9]+,{{[ \t]+}}sp
; CHECK-NOT: ld32{{[ \t]+}}r[0-9]+,{{[ \t]+}}sp
; CHECK-NOT: ld64{{[ \t]+}}d[0-9]+,{{[ \t]+}}sp
; CHECK-NOT: .cfi_offset
; CHECK-NOT: .cfi_def_cfa
; CHECK: st32{{[ \t]+}}r8,{{[ \t]+}}r1
; CHECK: st64{{[ \t]+}}d15,{{[ \t]+}}r1
; CHECK: addi32_w{{[ \t]+}}r1,{{[ \t]+}}r0
; CHECK: jalr_w{{[ \t]+}}r0,{{[ \t]+}}lr
; CHECK-NOT: jalr
; CHECK-NOT: xor32
; CHECK-NOT: subi32
; CHECK-NOT: addi32{{[ \t]+}}sp,{{[ \t]+}}sp
; CHECK-NOT: ld32{{[ \t]+}}r[0-9]+,{{[ \t]+}}sp
; CHECK-NOT: ld64{{[ \t]+}}d[0-9]+,{{[ \t]+}}sp
; CHECK-NOT: .cfi_offset
; CHECK-NOT: .cfi_def_cfa
entry:
  tail call void asm sideeffect "
      st32 r8,  r1, 0
      st32 lr,  r1, 5
      st64 d8,  r1, 3
      st64 d15, r1, 10
      addi32_w r1, r0, 0
      jalr_w r0, lr, 0
  ", ""() nounwind
  unreachable
}

define void @ret_shape(ptr %buf, i32 %val) naked {
; CHECK-LABEL: ret_shape:
; CHECK-NOT: xor32
; CHECK-NOT: subi32
; CHECK-NOT: addi32{{[ \t]+}}sp,{{[ \t]+}}sp
; CHECK-NOT: st32{{[ \t]+}}r[0-9]+,{{[ \t]+}}sp
; CHECK-NOT: st64{{[ \t]+}}d[0-9]+,{{[ \t]+}}sp
; CHECK-NOT: .cfi_offset
; CHECK-NOT: .cfi_def_cfa
; CHECK: ld32{{[ \t]+}}r8,{{[ \t]+}}r1
; CHECK: jalr_w{{[ \t]+}}r0,{{[ \t]+}}lr
; CHECK-NOT: jalr
; CHECK-NOT: xor32
; CHECK-NOT: subi32
; CHECK-NOT: addi32{{[ \t]+}}sp,{{[ \t]+}}sp
; CHECK-NOT: ld32{{[ \t]+}}r[0-9]+,{{[ \t]+}}sp
; CHECK-NOT: ld64{{[ \t]+}}d[0-9]+,{{[ \t]+}}sp
; CHECK-NOT: .cfi_offset
; CHECK-NOT: .cfi_def_cfa
entry:
  tail call void asm sideeffect "
      ld32 r8,  r1, 0
      jalr_w r0, lr, 0
  ", ""() nounwind
  ret void
}
