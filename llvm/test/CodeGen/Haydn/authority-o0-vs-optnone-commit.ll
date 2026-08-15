; RUN: llc -mtriple=haydn-unknown-elf -O0 -stop-before=regallocfast \
; RUN:   < %s | FileCheck %s --check-prefix=PRERA
; RUN: llc -mtriple=haydn-unknown-elf -O2 -stop-before=greedy \
; RUN:   < %s | FileCheck %s --check-prefix=PRERA
; RUN: llc -mtriple=haydn-unknown-elf -O0 -stop-before=postmisched \
; RUN:   < %s | FileCheck %s --check-prefix=THRU
; RUN: llc -mtriple=haydn-unknown-elf -O2 -stop-before=postmisched \
; RUN:   < %s | FileCheck %s --check-prefix=THRU
; RUN: llc -mtriple=haydn-unknown-elf -O0 -stop-after=postmisched \
; RUN:   < %s | FileCheck %s --check-prefix=PACK
; RUN: llc -mtriple=haydn-unknown-elf -O2 -stop-after=postmisched \
; RUN:   < %s | FileCheck %s --check-prefix=SKIP-OPTNONE
; RUN: llc -mtriple=haydn-unknown-elf -O0 -stop-after=haydn-verify-bundles \
; RUN:   < %s | FileCheck %s --check-prefix=PLAIN
; RUN: llc -mtriple=haydn-unknown-elf -O2 -stop-after=haydn-verify-bundles \
; RUN:   < %s | FileCheck %s --check-prefix=OPT
; RUN: llc -mtriple=haydn-unknown-elf -O0 < %s | FileCheck %s --check-prefix=ASM-O0
; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s --check-prefix=ASM

; Role: ownership — product-shape pin for plain O0 vs skipFunction-skipped
; optnone committed-cycle formation (target-local no-reorder Finalize/Verify).
;
; Live product (re-verify before citing):
;   * PostMachineScheduler still calls skipFunction (quality/reorder only).
;   * FinalizeBundle and VerifyBundles deliberately do NOT call skipFunction:
;     they are the target-local no-reorder commit ownership for optnone and for
;     any bare MI remaining after allowed late growth.
;   * Therefore both plain O0 and optnone leave committed Format-E BUNDLE roots
;     with private member placement and durable row/completion imms after
;     haydn-verify-bundles. Singleton completion is the documented stub identity
;     (BUNDLE 0, 0 == E2 + AllEntriesReal full-slot NOP pad); underfill/top-pad invent remains
;     fail-closed when golden is silent.
;   * Independent multi-op canaries pin the product-shape distinction that
;     dependent multi-op chains miss: plain O0 postmisched packs co-issue
;     (BUNDLE 0, 0 == E2 + AllEntriesReal with two private members); optnone
;     stays sequential bare logicals until Finalize wraps each as BUNDLE 0, 0 (full-slot NOP pad).
;   * Verify also refuses mixed committed-BUNDLE + bare encode residual for
;     every function (partial-commit escape), while all-bare non-optnone MIR
;     unit fixtures remain legal until Finalize runs.
;   * HaydnLatencyStalls never calls skipFunction.
;   * Never change generic skipFunction semantics for quality passes.
;
; Inventory: Inputs/SOURCE-AUTHORITY-ANCHORS.txt
;
; Function order: optnone bodies first so SKIP-OPTNONE / PACK checks stay
; in-function (positive JALR bounds -NOT before plain multi-op setDesc members).

; ---------------------------------------------------------------------------
; Phase firewall: no BUNDLE / private member / row / completion / issue-cycle
; identity before RA or through RA. Finalize/Verify still commit after
; postmisched (including optnone: they do not skipFunction).
; ---------------------------------------------------------------------------
; PRERA: ADD32
; PRERA-NOT: BUNDLE
; PRERA-NOT: {{ADD32|XOR32|JALR_W}}_S{{[0-2]}}
; PRERA-NOT: BUNDLE_E96
; PRERA-NOT: BundleFormatRowID
; PRERA-NOT: CompletionStateID
; THRU: ADD32
; THRU-NOT: BUNDLE
; THRU-NOT: {{ADD32|XOR32|JALR_W}}_S{{[0-2]}}
; THRU-NOT: BUNDLE_E96
; THRU-NOT: BundleFormatRowID
; THRU-NOT: CompletionStateID

; ---------------------------------------------------------------------------
; After postmisched at -O0: plain packs independent multi; optnone is bare.
; ---------------------------------------------------------------------------
; PACK-LABEL: name:{{ +}}with_optnone
; PACK: $r{{[0-9]+}} = ADD32{{ }}
; PACK-NOT: BUNDLE
; PACK: JALR
; PACK-LABEL: name:{{ +}}multi_optnone
; PACK: $r{{[0-9]+}} = ADD32{{ }}
; PACK: $r{{[0-9]+}} = ADD32{{ }}
; PACK-NOT: BUNDLE
; PACK: JALR
; Independent multi-op optnone remains sequential bare logicals (quality skip).
; PACK-LABEL: name:{{ +}}indep_optnone
; PACK: $r{{[0-9]+}} = ADD32{{ }}
; PACK: $r{{[0-9]+}} = ADD32{{ }}
; PACK-NOT: BUNDLE
; PACK: JALR
; Plain O0 independent multi co-issues under postmisched (product shape).
; PACK-LABEL: name:{{ +}}indep_plain
; PACK: BUNDLE 1, 0
; PACK: ADD32_E{{[23]}}_E{{[0-2]}}_
; PACK: ADD32_E{{[23]}}_E{{[0-2]}}_
; PACK: JALR

; ---------------------------------------------------------------------------
; After postmisched at -O2: optnone still quality-skipped (bare logicals).
; ---------------------------------------------------------------------------
; SKIP-OPTNONE-LABEL: name:{{ +}}with_optnone
; SKIP-OPTNONE: $r{{[0-9]+}} = ADD32{{ }}
; SKIP-OPTNONE-NOT: BUNDLE
; SKIP-OPTNONE: JALR
; Multi-op optnone remains sequential bare logicals (no reorder pack).
; SKIP-OPTNONE-LABEL: name:{{ +}}multi_optnone
; SKIP-OPTNONE: $r{{[0-9]+}} = ADD32{{ }}
; SKIP-OPTNONE: $r{{[0-9]+}} = ADD32{{ }}
; SKIP-OPTNONE-NOT: BUNDLE
; SKIP-OPTNONE: JALR
; Independent multi-op optnone still bare after quality skip.
; SKIP-OPTNONE-LABEL: name:{{ +}}indep_optnone
; SKIP-OPTNONE: $r{{[0-9]+}} = ADD32{{ }}
; SKIP-OPTNONE: $r{{[0-9]+}} = ADD32{{ }}
; SKIP-OPTNONE-NOT: BUNDLE
; SKIP-OPTNONE: JALR

; ---------------------------------------------------------------------------
; Plain O0 after Finalize+Verify: committed cycles only.
; Dependent chains are singleton Format-E (BUNDLE 0, 2). Independent multi
; keeps the postmisched full-fill co-issue (BUNDLE 0, 0) and never leaves bare
; encode MIs. optnone is no-reorder singleton commit only.
; ---------------------------------------------------------------------------
; PLAIN-LABEL: name:{{ +}}with_optnone
; PLAIN: BUNDLE {{[01]}}, 0
; PLAIN: ADD32_E{{[23]}}_E{{[0-2]}}_
; PLAIN: JALR_E2
; PLAIN-NOT: $r{{[0-9]+}} = ADD32{{ }}
; PLAIN-LABEL: name:{{ +}}multi_optnone
; PLAIN: BUNDLE {{[01]}}, 0
; PLAIN: ADD32_E{{[23]}}_E{{[0-2]}}_
; PLAIN: BUNDLE {{[01]}}, 0
; PLAIN: ADD32_E{{[23]}}_E{{[0-2]}}_
; PLAIN-NOT: $r{{[0-9]+}} = ADD32{{ }}
; PLAIN: JALR_E2
; Independent optnone: two sequential singleton commits (no co-issue).
; PLAIN-LABEL: name:{{ +}}indep_optnone
; PLAIN: BUNDLE {{[01]}}, 0
; PLAIN: ADD32_E{{[23]}}_E{{[0-2]}}_
; PLAIN: BUNDLE {{[01]}}, 0
; PLAIN: ADD32_E{{[23]}}_E{{[0-2]}}_
; PLAIN-NOT: $r{{[0-9]+}} = ADD32{{ }}
; PLAIN: JALR_E2
; PLAIN-LABEL: name:{{ +}}plain_o0
; PLAIN: BUNDLE {{[01]}}, 0
; PLAIN: ADD32_E{{[23]}}_E{{[0-2]}}_
; PLAIN: JALR_E2
; PLAIN-LABEL: name:{{ +}}multi_plain
; PLAIN: BUNDLE {{[01]}}, 0
; PLAIN: ADD32_E{{[23]}}_E{{[0-2]}}_
; PLAIN: BUNDLE {{[01]}}, 0
; PLAIN: ADD32_E{{[23]}}_E{{[0-2]}}_
; PLAIN-NOT: $r{{[0-9]+}} = ADD32{{ }}
; PLAIN: JALR_E2
; Independent plain O0 multi-MI full-fill (AllEntriesReal full-fill co-issue).
; PLAIN-LABEL: name:{{ +}}indep_plain
; PLAIN: BUNDLE 1, 0
; PLAIN: ADD32_E{{[23]}}_E{{[0-2]}}_
; PLAIN: ADD32_E{{[23]}}_E{{[0-2]}}_
; PLAIN-NOT: $r{{[0-9]+}} = ADD32{{ }}
; PLAIN: JALR_E2

; ---------------------------------------------------------------------------
; Same under -O2: plain may multi-issue in postmisched; optnone is no-reorder
; singleton commit only (including independent multi-op canaries).
; ---------------------------------------------------------------------------
; OPT-LABEL: name:{{ +}}with_optnone
; OPT: BUNDLE {{[01]}}, 0
; OPT: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPT: JALR_E2
; OPT-NOT: $r{{[0-9]+}} = ADD32{{ }}
; OPT-LABEL: name:{{ +}}multi_optnone
; OPT: BUNDLE {{[01]}}, 0
; OPT: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPT: BUNDLE {{[01]}}, 0
; OPT: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPT-NOT: $r{{[0-9]+}} = ADD32{{ }}
; OPT: JALR_E2
; OPT-LABEL: name:{{ +}}indep_optnone
; OPT: BUNDLE {{[01]}}, 0
; OPT: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPT: BUNDLE {{[01]}}, 0
; OPT: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPT-NOT: $r{{[0-9]+}} = ADD32{{ }}
; OPT: JALR_E2
; OPT-LABEL: name:{{ +}}plain_o0
; OPT: BUNDLE {{[0-9]+}}, {{[0-9]+}}
; OPT: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPT: JALR_E2
; OPT-LABEL: name:{{ +}}indep_plain
; OPT: BUNDLE 1, 0
; OPT: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPT: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPT-NOT: $r{{[0-9]+}} = ADD32{{ }}
; OPT: JALR_E2

; ---------------------------------------------------------------------------
; ASM path: committed Format E composite print, not bare uncommitted opcode.
; Plain independent multi co-issues two add32 in one composite; optnone emits
; two separate singleton composites (no-reorder).
; ---------------------------------------------------------------------------
; ASM-O0-LABEL: with_optnone:
; ASM-O0: {
; ASM-O0: add32
; ASM-O0-LABEL: multi_optnone:
; ASM-O0: {
; ASM-O0: add32
; ASM-O0: {
; ASM-O0: add32
; ASM-O0-LABEL: indep_optnone:
; ASM-O0: {
; ASM-O0: add32
; ASM-O0: {
; ASM-O0: add32
; ASM-O0-LABEL: indep_plain:
; ASM-O0: {{{.*}}add32{{.*}}add32
; ASM-LABEL: with_optnone:
; ASM: {
; ASM: add32
; ASM-LABEL: multi_optnone:
; ASM: {
; ASM: add32
; ASM: {
; ASM: add32
; ASM-LABEL: indep_optnone:
; ASM: {
; ASM: add32
; ASM: {
; ASM: add32
; ASM-LABEL: indep_plain:
; ASM: {{{.*}}add32{{.*}}add32

define i32 @with_optnone(i32 %a, i32 %b) #0 {
  %t = add i32 %a, %b
  ret i32 %t
}

define i32 @multi_optnone(i32 %a, i32 %b, i32 %c) #0 {
  %t0 = add i32 %a, %b
  %t1 = add i32 %t0, %c
  ret i32 %t1
}

; Independent multi-op canary: two ADDs with no data edge between them so
; postmisched may co-issue on plain paths while optnone stays sequential.
define i32 @indep_optnone(i32 %a, i32 %b, i32 %c, i32 %d) #0 {
  %x = add i32 %a, %b
  %y = add i32 %c, %d
  %z = xor i32 %x, %y
  ret i32 %z
}

define i32 @plain_o0(i32 %a, i32 %b) {
  %t = add i32 %a, %b
  ret i32 %t
}

define i32 @multi_plain(i32 %a, i32 %b, i32 %c) {
  %t0 = add i32 %a, %b
  %t1 = add i32 %t0, %c
  ret i32 %t1
}

define i32 @indep_plain(i32 %a, i32 %b, i32 %c, i32 %d) {
  %x = add i32 %a, %b
  %y = add i32 %c, %d
  %z = xor i32 %x, %y
  ret i32 %z
}

attributes #0 = { noinline nounwind optnone }
