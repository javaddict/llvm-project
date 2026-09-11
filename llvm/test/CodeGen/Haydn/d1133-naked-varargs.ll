; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=irtranslator -verify-machineinstrs -o - %s \
; RUN:     | FileCheck %s --check-prefix=GMIR
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %s | FileCheck %s --check-prefix=ASM
;
; D1.133: naked + varargs must not run saveVarArgRegisters.
; Repro (lane S): `define void @nk(...) naked { unreachable }` emitted
; GPR/DR save-area stores at SP-relative addresses with no frame and no
; RET — PEI skips insertPrologEpilogCode for Naked, so the spills landed
; in the caller. File-local naked law: compiler contributes no
; instructions beyond mandatory terminators (HaydnCallLowering.cpp).
; AIE has no naked seat. RISC-V GISel still saves unconditionally
; (RISCVCallLowering.cpp:568-569); ARMFrameLowering.cpp:2436 refuses CSR
; spill for Naked because there is no frame. Named d1133-* to avoid
; colliding with D1.33 / gr27-d133 comments.
;
; Translator dump must not contain G_FRAME_INDEX or G_STORE (the save-area
; spills). Asm must not contain addi32-from-sp, st32/st64, subi32, or a
; compiler RET/jalr. Named formals still lower to unused vregs (G_COPY of
; the incoming physreg is legal and DCE'd). Asm-only body owns incoming
; regs.

define void @varargs_zero_fixed(...) naked {
; GMIR-LABEL: name: varargs_zero_fixed
; GMIR-NOT: G_FRAME_INDEX
; GMIR-NOT: G_STORE
; GMIR-NOT: G_LOAD
; ASM-LABEL: varargs_zero_fixed:
; ASM-NOT: xor32
; ASM-NOT: subi32
; ASM-NOT: addi32
; ASM-NOT: st32
; ASM-NOT: st64
; ASM-NOT: ld32
; ASM-NOT: ld64
; ASM-NOT: jalr
; ASM-NOT: .cfi_offset
; ASM-NOT: .cfi_def_cfa
entry:
  unreachable
}

define void @varargs_named(i32 %a, ...) naked {
; GMIR-LABEL: name: varargs_named
; GMIR-NOT: G_FRAME_INDEX
; GMIR-NOT: G_STORE
; ASM-LABEL: varargs_named:
; ASM-NOT: xor32
; ASM-NOT: subi32
; ASM-NOT: addi32
; ASM-NOT: st32
; ASM-NOT: st64
; ASM-NOT: jalr
entry:
  unreachable
}

define void @varargs_ret(...) naked {
; GMIR-LABEL: name: varargs_ret
; GMIR-NOT: G_FRAME_INDEX
; GMIR-NOT: G_STORE
; ASM-LABEL: varargs_ret:
; ASM-NOT: xor32
; ASM-NOT: subi32
; ASM-NOT: addi32
; ASM-NOT: st32
; ASM-NOT: st64
; ASM-NOT: jalr
entry:
  ret void
}

define void @varargs_asm(...) naked {
; GMIR-LABEL: name: varargs_asm
; GMIR-NOT: G_FRAME_INDEX
; GMIR-NOT: G_STORE
; ASM-LABEL: varargs_asm:
; ASM-NOT: xor32
; ASM-NOT: subi32
; ASM-NOT: addi32
; ASM-NOT: st32
; ASM-NOT: st64
; ASM: jalr_w{{[ \t]+}}r0,{{[ \t]+}}lr
; ASM-NOT: jalr
; ASM-NOT: xor32
; ASM-NOT: subi32
; ASM-NOT: st32
; ASM-NOT: st64
entry:
  tail call void asm sideeffect "jalr_w r0, lr, 0", ""() nounwind
  unreachable
}
