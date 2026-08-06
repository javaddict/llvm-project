; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — B1 (fir_blms* freestanding dual): 8th i32 is a stack arg.

; B1 (fir_blms* freestanding dual): 8th i32 is a stack arg. Call frame SP
; adjust must be a multiple of StackAlign(8) so SP stays 8-byte aligned
; across the call — callee DR CSR st64 then never sees ≡4 mod 8 addresses.
;
; Historical bug: ADJCALLSTACKDOWN 4 (Size=4 stack slot) left SP ≡4 mod 8.
; Fix: i32 stack slots Size=8 + alignTo(call frame, StackAlign).

declare i32 @take8(i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @call_take8() {
entry:
  %r = call i32 @take8(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8)
  ret i32 %r
}

; Call-frame adjust is at least 8 (not 4). Accept SUBI/ADDI SP forms.
; CHECK-LABEL: call_take8:
; CHECK: subi32{{(_w)?}}{{.*}}sp{{.*}}, 8
; CHECK: jal
; CHECK: addi32{{(_w)?}}{{.*}}sp{{.*}}, 8
