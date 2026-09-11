; RUN: not llc -mtriple=haydn-unknown-elf -verify-machineinstrs=0 < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=REJECT
; FAIL-CLOSED side of the '#' admission law: only start-of-statement '#'
; outside braces is a zero-byte comment. Mid-statement '#' (after a mnemonic
; and its operands) stays operand text — the typed layout parser does not
; interpret it, and the real MC parse rejects the same text during emission
; ('unexpected token in operand'). Any '#' inside a braced packet is opaque
; executable asm rejected by the exact Format E layout law itself. Admission
; never opens a form MC would refuse. Bodies are naked so the earlier
; Finalize admission (asm-only naked) does not mask the classes under test.
; The run must exit nonzero: the second body's layout rejection is fatal.

define void @hash_mid_statement() naked {
entry:
  tail call void asm sideeffect "nop # c\0Anop", ""()
  ret void
}
; REJECT: error: unexpected token in operand

define void @hash_inside_brace() naked {
entry:
  tail call void asm sideeffect "{ nop\0A# c\0Anop }", ""()
  ret void
}
; REJECT: LLVM ERROR: Haydn: inline asm is not an exact typed Format E layout sequence
