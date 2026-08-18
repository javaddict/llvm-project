; RUN: rm -rf %t && split-file %s %t
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/pldwwua2.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=PLD2
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/pldwwua3.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=PLD3
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/flar2.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=FLAR2
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/wbar2.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=WBAR2
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -o - %t/pldwwua0.ll \
; RUN:     | FileCheck %s --check-prefix=PLD0
;
; Role: semantic — product ISel admits ar_sel 0/1 only. Encodings 2/3 stay
; unmapped (no product AR2/AR3 identity). ar_sel=0 remains an immediate.

;--- pldwwua2.ll
declare void @llvm.haydn.pldwwua(i32, ptr)
define void @pldwwua_sel2(ptr %p) {
  ; PLD2: {{cannot select|unable to translate|failed to select}}
  call void @llvm.haydn.pldwwua(i32 2, ptr %p)
  ret void
}

;--- pldwwua3.ll
declare void @llvm.haydn.pldwwua(i32, ptr)
define void @pldwwua_sel3(ptr %p) {
  ; PLD3: {{cannot select|unable to translate|failed to select}}
  call void @llvm.haydn.pldwwua(i32 3, ptr %p)
  ret void
}

;--- flar2.ll
declare void @llvm.haydn.flar(i32)
define void @flar_sel2() {
  ; FLAR2: {{cannot select|unable to translate|failed to select}}
  call void @llvm.haydn.flar(i32 2)
  ret void
}

;--- wbar2.ll
declare void @llvm.haydn.wbarwua(i32, ptr, i32)
define void @wbar_sel2(ptr %p) {
  ; WBAR2: {{cannot select|unable to translate|failed to select}}
  call void @llvm.haydn.wbarwua(i32 2, ptr %p, i32 0)
  ret void
}

;--- pldwwua0.ll
declare void @llvm.haydn.pldwwua(i32, ptr)
define void @pldwwua_sel0(ptr %p) {
; PLD0-LABEL: name: pldwwua_sel0
; Logical dag is (uimm2 ar_sel, GPR32 rs). ar_sel=0 is an immediate, not a
; register. The verifier must not demand a register on the selector field.
; PLD0: PLDWWUA 0, %{{[0-9]+}}
; PLD0-SAME: implicit-def {{(dead )?}}$ar0
  call void @llvm.haydn.pldwwua(i32 0, ptr %p)
  ret void
}
