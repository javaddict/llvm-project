; RUN: llc -mtriple=haydn-unknown-elf -verify-machineinstrs=0 < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=ADMIT
; Admission ('#' start-of-statement line comment is zero layout bytes): the
; llvm-libc setjmp/longjmp naked-body class — '# return ...' between public
; mnemonics must not trip the exact Format E layout rejection. Code-bearing
; inline asm is only legal in asm-only naked bodies (Haydn Finalize law), so
; every admission body here is naked. FAIL-CLOSED: mid-statement '#' and any
; '#' inside a braced packet remain opaque (LLVM ERROR ... not an exact typed
; Format E layout sequence); the real assembler rejects the same text
; ('unexpected token in operand' / 'expected instruction mnemonic in
; bundle'), so admission parity holds on both sides. The fail-closed forms
; are exercised by the negative file
; inlineasm-hash-comment-reject.ll (exit-code pinned by lit FAIL-NOT arcs).

define void @hash_line_comment() naked {
entry:
  tail call void asm sideeffect "nop\0A# return 0\0Anop", ""()
  ret void
}
; ADMIT-LABEL: hash_line_comment:
; ADMIT-NOT: LLVM ERROR
; ADMIT: { nop }
; ADMIT: { nop }

define void @hash_after_separator() naked {
entry:
  tail call void asm sideeffect "nop; # c\0Anop", ""()
  ret void
}
; ADMIT-LABEL: hash_after_separator:
; ADMIT-NOT: LLVM ERROR
; ADMIT: { nop }
; ADMIT: { nop }

define i32 @naked_hash_body(i32 %buf) naked {
entry:
  tail call void asm sideeffect "st32 r8, r1, 0\0A# return 0\0Aaddi32_w r1, r0, 0\0Ajalr_w r0, lr, 0", ""()
  ret i32 0
}
; ADMIT-LABEL: naked_hash_body:
; ADMIT-NOT: LLVM ERROR
; ADMIT: st32
; ADMIT: addi32_w
; ADMIT: jalr_w
