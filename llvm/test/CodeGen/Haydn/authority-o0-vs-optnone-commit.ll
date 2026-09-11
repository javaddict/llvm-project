; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -stop-before=regallocfast \
; RUN:   < %s | FileCheck %s --check-prefix=PRERA
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -stop-before=greedy \
; RUN:   < %s | FileCheck %s --check-prefix=PRERA
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -stop-before=postmisched \
; RUN:   < %s | FileCheck %s --check-prefix=THRU
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -stop-before=postmisched \
; RUN:   < %s | FileCheck %s --check-prefix=THRU
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -stop-after=postmisched \
; RUN:   < %s | FileCheck %s --check-prefix=PACK
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -stop-after=postmisched \
; RUN:   < %s | FileCheck %s --check-prefix=SKIP-OPTNONE
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -stop-after=haydn-verify-bundles \
; RUN:   < %s | FileCheck %s --check-prefix=PLAIN
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -stop-after=haydn-verify-bundles \
; RUN:   < %s | FileCheck %s --check-prefix=OPT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 < %s | FileCheck %s --check-prefix=ASM-O0
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s --check-prefix=ASM

; Role: ownership — product-shape pin for plain O0 vs optnone committed-cycle
; formation after GR2.4 (mandatory scheduler entry; target-local no-reorder
; Finalize/Verify residual lane).
;
; Live product (re-verify before citing):
;   * PostMachineScheduler no longer skips optnone:
;     HaydnSubtarget::forcePostRAScheduling() makes the addPreSched2
;     invocation mandatory for every function. Dependent chains stay
;     sequential generated members (singleton fallback commit); independent
;     ops may co-issue into scheduler-committed multi-MI BUNDLE roots at
;     postmisched already.
;   * FinalizeBundle and VerifyBundles deliberately do NOT call skipFunction:
;     they own true residual commits (late BR parcels). Dest-window stalls
;     fold into S1 PostMachineScheduler with the packet+stamp (GR1.2).
;     Product default also runs late
;     Finalize/Verify after BranchRelaxation so insertIndirectBranch
;     LUI+ADDI32_W+JALR_W rejoin the same lane.
;   * Therefore both plain O0 and optnone leave committed Format-E BUNDLE roots
;     with private member placement and durable row/completion imms after
;     haydn-verify-bundles. Singleton completion is the documented stub identity
;     (BUNDLE 0, 0 == E2 + AllEntriesReal full-slot NOP pad); underfill/top-pad
;     invent remains fail-closed when golden is silent.
;   * Independent multi-op canaries pin the shape distinction dependent
;     multi-op chains miss: since GR2.4 optnone matches plain — the
;     scheduler co-issues independent ADDs into one committed BUNDLE 1, 0
;     root at postmisched, kept through Finalize/Verify (do not
;     force-coissue; this is the observed product shape). Underfill/top-pad
;     invent remains fail-closed.
;   * Verify also refuses mixed committed-BUNDLE + bare encode residual for
;     every function (partial-commit escape), while all-bare non-optnone MIR
;     unit fixtures remain legal until Finalize runs.
;   * HaydnLatencyStalls never calls skipFunction.
;   * Never change generic skipFunction semantics for quality passes.
;
; Inventory: Inputs/SOURCE-AUTHORITY-ANCHORS.txt (T8-EVID restamp)
;
; Function order: optnone bodies first so SKIP-OPTNONE / PACK checks stay
; in-function (GR2.4: optnone now carries generated members too; positive
; JALR bounds -NOT before plain multi-op setDesc members).

; ---------------------------------------------------------------------------
; Phase firewall: no BUNDLE / private member / setDesc / row / completion /
; issue-cycle identity before RA or through RA. After postmisched committed
; identity exists for every function (GR2.4 mandatory scheduling incl.
; optnone; Finalize/Verify never skipFunction).
; ---------------------------------------------------------------------------
; PRERA: ADD32
; PRERA-NOT: BUNDLE
; PRERA-NOT: {{ADD32|XOR32|JALR_W}}_S{{[0-2]}}
; PRERA-NOT: {{ADD32|XOR32|JALR_W}}_E{{[23]}}_
; PRERA-NOT: BUNDLE_E96
; PRERA-NOT: BundleFormatRowID
; PRERA-NOT: CompletionStateID
; THRU: ADD32
; THRU-NOT: BUNDLE
; THRU-NOT: {{ADD32|XOR32|JALR_W}}_S{{[0-2]}}
; THRU-NOT: {{ADD32|XOR32|JALR_W}}_E{{[23]}}_
; THRU-NOT: BUNDLE_E96
; THRU-NOT: BundleFormatRowID
; THRU-NOT: CompletionStateID

; ---------------------------------------------------------------------------
; After postmisched at -O0 (GR2.4 + GR1.2): optnone is scheduled too.
; S1 leaveFunction wraps remaining bares as singleton BUNDLEs (AIE
; Finalize wrap moved into the packet+stamp transaction). Independent
; multi is a scheduler-committed multi-member BUNDLE root.
; ---------------------------------------------------------------------------
; PACK-LABEL: name:{{ +}}with_optnone
; PACK: ADD32_E{{[23]}}_
; PACK: BUNDLE
; PACK: JALR
; Dependent multi-op optnone: sequential generated members, each wrapped
; as a singleton BUNDLE at S1 stamp.
; PACK-LABEL: name:{{ +}}multi_optnone
; PACK: ADD32_E{{[23]}}_E{{[0-2]}}_
; PACK: ADD32_E{{[23]}}_E{{[0-2]}}_
; PACK: BUNDLE
; PACK: JALR
; GR2.4: independent multi-op optnone is a scheduler-committed co-issue root.
; PACK-LABEL: name:{{ +}}indep_optnone
; PACK: BUNDLE 1, 0
; PACK: ADD32_E{{[23]}}_E{{[0-2]}}_
; PACK: ADD32_E{{[23]}}_E{{[0-2]}}_
; PACK: JALR
; Plain O0 independent multi co-issues under postmisched (product shape).
; PACK-LABEL: name:{{ +}}indep_plain
; PACK: BUNDLE 1, 0
; PACK: ADD32_E{{[23]}}_E{{[0-2]}}_
; PACK: ADD32_E{{[23]}}_E{{[0-2]}}_
; PACK: JALR

; ---------------------------------------------------------------------------
; After postmisched at -O2: optnone scheduled identically (GR2.4; no skip).
; S1 wraps remaining bares as singleton BUNDLEs (GR1.2).
; ---------------------------------------------------------------------------
; SKIP-OPTNONE-LABEL: name:{{ +}}with_optnone
; SKIP-OPTNONE: ADD32_E{{[23]}}_
; SKIP-OPTNONE: BUNDLE
; SKIP-OPTNONE: JALR
; Dependent multi-op optnone: sequential generated members, S1-wrapped.
; SKIP-OPTNONE-LABEL: name:{{ +}}multi_optnone
; SKIP-OPTNONE: ADD32_E{{[23]}}_E{{[0-2]}}_
; SKIP-OPTNONE: ADD32_E{{[23]}}_E{{[0-2]}}_
; SKIP-OPTNONE: BUNDLE
; SKIP-OPTNONE: JALR
; GR2.4: independent multi-op optnone is a scheduler-committed co-issue root
; at -O2 as well.
; SKIP-OPTNONE-LABEL: name:{{ +}}indep_optnone
; SKIP-OPTNONE: BUNDLE 1, 0
; SKIP-OPTNONE: ADD32_E{{[23]}}_E{{[0-2]}}_
; SKIP-OPTNONE: ADD32_E{{[23]}}_E{{[0-2]}}_
; SKIP-OPTNONE: JALR

; ---------------------------------------------------------------------------
; Plain O0 after Finalize+Verify: committed cycles only.
; Dependent chains are singleton Format-E wraps. Independent multi is
; sequential committed singletons (do not force-coissue) and never leaves
; bare encode MIs. optnone is no-reorder singleton commit only.
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
; GR2.4: independent optnone keeps the scheduler-committed co-issued root
; (BUNDLE 1, 0) through Finalize/Verify.
; PLAIN-LABEL: name:{{ +}}indep_optnone
; PLAIN: BUNDLE 1, 0
; PLAIN: ADD32_E{{[23]}}_E{{[0-2]}}_
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
; Independent plain O0: committed singleton cycles (BUNDLE 0, 0). O0
; postmisched does not co-issue this canary into E3; Finalize must still
; wrap every encode MI (no bare ADD32). Do not force-coissue here.
; PLAIN-LABEL: name:{{ +}}indep_plain
; PLAIN: BUNDLE {{[01]}}, 0
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
; GR2.4: independent optnone keeps the scheduler-committed co-issued root.
; OPT-LABEL: name:{{ +}}indep_optnone
; OPT: BUNDLE 1, 0
; OPT: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPT: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPT-NOT: $r{{[0-9]+}} = ADD32{{ }}
; OPT: JALR_E2
; OPT-LABEL: name:{{ +}}plain_o0
; OPT: BUNDLE {{[0-9]+}}, {{[0-9]+}}
; OPT: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPT: JALR_E2
; OPT-LABEL: name:{{ +}}indep_plain
; OPT: BUNDLE {{[01]}}, 0
; OPT: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPT: ADD32_E{{[23]}}_E{{[0-2]}}_
; OPT-NOT: $r{{[0-9]+}} = ADD32{{ }}
; OPT: JALR_E2

; ---------------------------------------------------------------------------
; ASM path: committed Format E composite print, not bare uncommitted opcode.
; GR2.4: independent multi (plain and optnone alike) co-issues two add32 in
; one composite; dependent chains emit singleton composites.
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
; ASM-O0: add32{{.*}}add32
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
; ASM: add32{{.*}}add32
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
