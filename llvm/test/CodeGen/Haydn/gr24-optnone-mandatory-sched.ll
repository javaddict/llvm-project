; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -stop-after=postmisched < %s 2>/dev/null | FileCheck %s --check-prefix=POST-O0
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -stop-after=postmisched < %s 2>/dev/null | FileCheck %s --check-prefix=POST-O2
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -stop-after=haydn-verify-bundles < %s 2>/dev/null | FileCheck %s --check-prefix=VERIFY-O0
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -stop-after=haydn-verify-bundles < %s 2>/dev/null | FileCheck %s --check-prefix=VERIFY-O2
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 < %s | FileCheck %s --check-prefix=ASM-O0
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s --check-prefix=ASM-O2
; GR2.4 INV5 probe: optnone full emission under -haydn-postra-interblock
; (+ and without) must reach assembly with no freeze fatal. The
; LateConvergence driver (the only S2 seat) still skipFunctions optnone, so
; without the destructor clearing condition in
; HaydnPostRASchedStrategy::~HaydnPostRASchedStrategy the inter-block DDG
; registry would leak to the addPreEmitPass2 freeze fatal.
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -haydn-postra-interblock < %s | FileCheck %s --check-prefix=IB
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -haydn-postra-interblock < %s | FileCheck %s --check-prefix=IB
;
; GR2.4: the single addPreSched2 PostMachineScheduler invocation is MANDATORY
; for optnone (HaydnSubtarget::forcePostRAScheduling overrides the common
; skipFunction guard in PostMachineSchedulerLegacy). Pins:
;   (a) after postmisched, optnone functions carry scheduler-committed
;       identity: dependent chains are sequential generated members and
;       independent ops a committed multi-MI BUNDLE root — Finalize has NOT
;       run at this stop point, so this is structural proof the scheduler
;       entered (the old quality-skip path left bare logicals here);
;   (b) the independent optnone canary matches the plain product shape
;       (co-issued BUNDLE 1, 0; observed shape, not forced);
;   (c) after haydn-verify-bundles, zero bare encode MIs remain;
;   (d) plain (non-optnone) functions evaluate identically at every level
;       (no behavior change for already-scheduled levels);
;   (e) the -haydn-postra-interblock arms above (INV5).

; --- (a)+(b): scheduler-committed identity for optnone at postmisched ---
; Dependent chain: sequential generated members, no BUNDLE root at this stop.
; POST-O0-LABEL: name: dep_optnone
; POST-O0: ADD32_E{{[23]}}_E{{[0-2]}}_
; POST-O0: ADD32_E{{[23]}}_E{{[0-2]}}_
; POST-O0: BUNDLE
; POST-O0: JALR
; Independent canary: scheduler-committed co-issue root (product shape).
; POST-O0-LABEL: name: indep_gr24
; POST-O0: BUNDLE 1, 0
; POST-O0: ADD32_E{{[23]}}_E{{[0-2]}}_
; POST-O0: ADD32_E{{[23]}}_E{{[0-2]}}_
; POST-O2-LABEL: name: dep_optnone
; POST-O2: ADD32_E{{[23]}}_E{{[0-2]}}_
; POST-O2: ADD32_E{{[23]}}_E{{[0-2]}}_
; POST-O2: BUNDLE
; POST-O2: JALR
; POST-O2-LABEL: name: indep_gr24
; POST-O2: BUNDLE 1, 0
; POST-O2: ADD32_E{{[23]}}_E{{[0-2]}}_
; POST-O2: ADD32_E{{[23]}}_E{{[0-2]}}_

; --- (d): plain functions unchanged at postmisched ---
; POST-O0-LABEL: name: dep_plain
; POST-O0: ADD32_E{{[23]}}_E{{[0-2]}}_
; POST-O0: ADD32_E{{[23]}}_E{{[0-2]}}_
; POST-O2-LABEL: name: dep_plain
; POST-O2: ADD32_E{{[23]}}_E{{[0-2]}}_
; POST-O2: ADD32_E{{[23]}}_E{{[0-2]}}_

; --- (c): after Finalize+Verify, committed cycles only, zero bare encode ---
; VERIFY-O0-LABEL: name: dep_optnone
; VERIFY-O0: BUNDLE {{[01]}}, 0
; VERIFY-O0: ADD32_E{{[23]}}_E{{[0-2]}}_
; VERIFY-O0-NOT: $r{{[0-9]+}} = ADD32{{ }}
; VERIFY-O0-LABEL: name: indep_gr24
; VERIFY-O0: BUNDLE 1, 0
; VERIFY-O0: ADD32_E{{[23]}}_E{{[0-2]}}_
; VERIFY-O0: ADD32_E{{[23]}}_E{{[0-2]}}_
; VERIFY-O0-NOT: $r{{[0-9]+}} = ADD32{{ }}
; VERIFY-O0-LABEL: name: dep_plain
; VERIFY-O0: BUNDLE {{[01]}}, 0
; VERIFY-O0: ADD32_E{{[23]}}_E{{[0-2]}}_
; VERIFY-O0-NOT: $r{{[0-9]+}} = ADD32{{ }}
; VERIFY-O2-LABEL: name: dep_optnone
; VERIFY-O2: BUNDLE {{[01]}}, 0
; VERIFY-O2: ADD32_E{{[23]}}_E{{[0-2]}}_
; VERIFY-O2-NOT: $r{{[0-9]+}} = ADD32{{ }}
; VERIFY-O2-LABEL: name: indep_gr24
; VERIFY-O2: BUNDLE 1, 0
; VERIFY-O2: ADD32_E{{[23]}}_E{{[0-2]}}_
; VERIFY-O2: ADD32_E{{[23]}}_E{{[0-2]}}_
; VERIFY-O2-NOT: $r{{[0-9]+}} = ADD32{{ }}
; VERIFY-O2-LABEL: name: dep_plain
; VERIFY-O2: BUNDLE {{[01]}}, 0
; VERIFY-O2: ADD32_E{{[23]}}_E{{[0-2]}}_
; VERIFY-O2-NOT: $r{{[0-9]+}} = ADD32{{ }}

; --- committed Format E composite print at both levels ---
; ASM-O0-LABEL: dep_optnone:
; ASM-O0: {
; ASM-O0: add32
; ASM-O0-LABEL: indep_gr24:
; ASM-O0: add32{{.*}}add32
; ASM-O2-LABEL: dep_optnone:
; ASM-O2: {
; ASM-O2: add32
; ASM-O2-LABEL: indep_gr24:
; ASM-O2: add32{{.*}}add32

; --- (e): interblock arms emit the same committed composites, no fatal ---
; IB-LABEL: dep_optnone:
; IB: {
; IB: add32
; IB-LABEL: indep_gr24:
; IB: add32{{.*}}add32

define i32 @dep_optnone(i32 %a, i32 %b, i32 %c) #0 {
  %t0 = add i32 %a, %b
  %t1 = add i32 %t0, %c
  ret i32 %t1
}

define i32 @indep_gr24(i32 %a, i32 %b, i32 %c, i32 %d) #0 {
  %x = add i32 %a, %b
  %y = add i32 %c, %d
  %z = xor i32 %x, %y
  ret i32 %z
}

define i32 @dep_plain(i32 %a, i32 %b, i32 %c) {
  %t0 = add i32 %a, %b
  %t1 = add i32 %t0, %c
  ret i32 %t1
}

attributes #0 = { noinline nounwind optnone }
