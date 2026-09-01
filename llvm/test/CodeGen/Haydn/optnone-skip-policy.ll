; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -stop-after=postmisched < %s 2>/dev/null | FileCheck %s --check-prefix=POST-O0
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -stop-after=haydn-verify-bundles < %s 2>/dev/null | FileCheck %s --check-prefix=PLAIN-O0
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -stop-after=postmisched < %s 2>/dev/null | FileCheck %s --check-prefix=POST-OPTNONE
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -stop-after=haydn-verify-bundles < %s 2>/dev/null | FileCheck %s --check-prefix=OPTNONE
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s --check-prefix=ASM
;
; Product policy: plain O0 (no optnone) and optnone are distinct.
;   * Generic postmisched may skip optnone (no reorder) via skipFunction.
;   * FinalizeBundle + VerifyBundles never skip — optnone is target-local
;     no-reorder commit (singleton Format E BUNDLEs), not MC standalone escape.
;   * Both shapes must leave only committed BUNDLE roots for real encode MIs.
;   * Independent multi-op canaries: do not force-coissue. Plain O0 may
;     stamp generated members at postmisched; Finalize wraps remaining
;     encode MIs. optnone remains sequential bare until Finalize
;     (full-slot BUNDLE {{[01]}}, 0 each — architectural NOP pad).

; After postmisched: single-op plain may still be bare logical (Finalize wraps);
; independent multi may already be generated members without a BUNDLE wrapper
; (Finalize wraps remaining encode MIs). Do not force-coissue. optnone stays
; bare on all shapes (quality skip).
; POST-O0-LABEL: name: plain_o0
; POST-O0: $r{{[0-9]+}} = ADD32{{(_E2_[^ ]+)?}}{{ }}
; POST-O0-LABEL: name: indep_plain
; POST-O0: ADD32_E{{[23]}}_E{{[0-2]}}_
; POST-O0: ADD32_E{{[23]}}_E{{[0-2]}}_
; POST-OPTNONE-LABEL: name: optnone_fn
; POST-OPTNONE: $r{{[0-9]+}} = ADD32{{(_E2_[^ ]+)?}}{{ }}
; POST-OPTNONE-NOT: BUNDLE
; POST-OPTNONE-LABEL: name: indep_optnone
; POST-OPTNONE: $r{{[0-9]+}} = ADD32{{(_E2_[^ ]+)?}}{{ }}
; POST-OPTNONE: $r{{[0-9]+}} = ADD32{{(_E2_[^ ]+)?}}{{ }}
; POST-OPTNONE-NOT: BUNDLE

; After Finalize+Verify: committed cycles only on both paths.
; PLAIN-O0-LABEL: name: plain_o0
; PLAIN-O0: BUNDLE
; PLAIN-O0: ADD32
; PLAIN-O0-NOT: {{^[ ]+\$r1 = ADD32 }}
; PLAIN-O0-LABEL: name: indep_plain
; PLAIN-O0: BUNDLE {{[01]}}, 0
; PLAIN-O0: ADD32_E{{[23]}}_E{{[0-2]}}_
; PLAIN-O0: ADD32_E{{[23]}}_E{{[0-2]}}_
; PLAIN-O0-NOT: {{^[ ]+\$r[0-9]+ = ADD32 }}
define i32 @plain_o0(i32 %a, i32 %b) nounwind {
  %c = add i32 %a, %b
  ret i32 %c
}

define i32 @indep_plain(i32 %a, i32 %b, i32 %c, i32 %d) nounwind {
  %x = add i32 %a, %b
  %y = add i32 %c, %d
  %z = xor i32 %x, %y
  ret i32 %z
}

; OPTNONE-LABEL: name: optnone_fn
; OPTNONE: BUNDLE
; OPTNONE: ADD32
; Uncommitted bare encode form must not survive Verify.
; OPTNONE-NOT: {{^[ ]+\$r1 = ADD32 }}
; OPTNONE-LABEL: name: indep_optnone
; OPTNONE: BUNDLE {{[01]}}, 0
; OPTNONE: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPTNONE: BUNDLE {{[01]}}, 0
; OPTNONE: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPTNONE-NOT: {{^[ ]+\$r[0-9]+ = ADD32 }}
define i32 @optnone_fn(i32 %a, i32 %b) #0 {
  %c = add i32 %a, %b
  ret i32 %c
}

define i32 @indep_optnone(i32 %a, i32 %b, i32 %c, i32 %d) #0 {
  %x = add i32 %a, %b
  %y = add i32 %c, %d
  %z = xor i32 %x, %y
  ret i32 %z
}

; ASM-LABEL: optnone_fn:
; Committed Format E composite print (entry pad + real op), not bare opcode.
; ASM: {
; ASM: add32
; ASM-LABEL: indep_optnone:
; ASM: {
; ASM: add32
; ASM: {
; ASM: add32
attributes #0 = { noinline nounwind optnone }
