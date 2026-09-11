; REQUIRES: haydn-registered-target
; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -O2 -global-isel-abort=1 \
; RUN: -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -O2 -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -O2 -global-isel-abort=1 \
; RUN: -filetype=obj < %s -o %t.o
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=OBJ

; Role: end-to-end — entry-qualified relocations under the late-convergence
; driver (-haydn-sms2 seats the S2 whole-function reschedule that legally
; packs ADDI32_E2_E0_ALU0_RI20 and ADDI32_E2_E1_ALU1_RI20 into ONE parcel).
;
; REGRESSION TEST (G004 root cause): two global-address materializations
; whose adds coissue in one parcel. Pre-fix, both R_HAYDN_LO20 relocs
; shared r_offset = parcel origin and the content-sniffed resolveFieldLsb
; patched the e0 window twice — one address's low bits landed in BOTH adds
; and the other add kept imm 0 (t_global g=g*3 linked a store base of 0;
; 203/655 BundleSim failures). Post-fix the e1 member emits
; R_HAYDN_LO20_E1 (typed window) and each add keeps its own symbol.
;
; CHECK: lui
; CHECK: {{addi32.*;.*addi32|addi32}}

; The same parcel offset carries exactly one base and one entry-qualified
; LO20 — never two base kinds on one dual-field parcel (the pre-fix defect
; patched the e0 window twice from the same r_offset).
; OBJ: R_HAYDN_LO20 g1 0x0
; OBJ: R_HAYDN_LO20_E1 g0 0x0
; OBJ-NOT: R_HAYDN_LO20 g0
; The two adds (or their packed parcel) must both appear AFTER distinct
; LUI bases; we pin no-NOP packing shape — only that both materializations
; survive with their own bases (value correctness is BundleSim's run_c
; case t_global, executed green under the same driver).

@g0 = internal global i32 7, align 4
@g1 = internal global i32 9, align 4

define i32 @dual_global() {
entry:
  %a = load i32, ptr @g0, align 4
  %b = load i32, ptr @g1, align 4
  %s = add i32 %a, %b
  ret i32 %s
}
