; REQUIRES: asserts
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnLateConvergence.h --check-prefix=LEDGER
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnLateConvergence.cpp --check-prefix=ATTRIB
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnLateConvergence.cpp --check-prefix=UNRELATED
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 \
; RUN:     -debug-only=haydn-late-convergence -o /dev/null < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=DBG
;
; D1.41 exact no-growth accounting: the GR2.6 ledger replaces global
; booleans/counts (StallsChanged || FixupChanged || NormalizeChanged;
; global Promotions/Demotions applied per pair via min()) with exact
; attributed events — each mutating window contributes the per-pair
; charge delta it measured (stalls / Fixup / LongBranchNormalize: stable
; pair keys) or per-MBB deltas (BranchRelaxation: it renumbers keys), and
; credit lands ONLY on the span the event touched. An unrelated prefix
; that grows while an event fired elsewhere is the fatal (red), pinned by
; HaydnLateConvergenceBudgetTest UnrelatedPrefixRefused; the healthy
; compile below stays green through the exact law (no fixed allowance).
;
; LEDGER: an event never grants credit to an unrelated prefix
; LEDGER: unsigned IndirectCount = 0;
; LEDGER: HaydnClosureGrowthEvent
; LEDGER: HasExactPair
; LEDGER: No global booleans/counts
;
; ATTRIB: addPairDeltaEvents
; ATTRIB: capturePrefixBudget(MF, TII);
; ATTRIB: PostNorm.IndirectCount > PostFixup.IndirectCount
; ATTRIB: addVanishedPairEvents
; ATTRIB: addMBBDeltaEvents
;
; The unrelated-prefix fatal text stays the GR2.6 named diagnostic —
; unchanged shape, tighter budget (late-convergence-o0.ll NOGROWTH).
; UNRELATED: "HaydnLateConvergence: prefix budget grew beyond the admitted "
; UNRELATED-NEXT: "closure vocabulary: " +
;
; DBG: HaydnLateConvergence: exact_ledger bound={{[0-9]+}}
; DBG: HaydnLateConvergence: S2 once per driver entry
; DBG: HaydnLateConvergence: closure iteration {{[0-9]+}} events: promotions={{[0-9]+}} demotions={{[0-9]+}} stalls={{[0-9]+}} mbb-growth={{[0-9]+}}
; DBG: HaydnLateConvergence: closed after {{[0-9]+}} iteration(s) (no upward event)
; DBG-NOT: grew beyond the admitted closure vocabulary
;
; Fleet regression pin (cxfir16x16_hifi3 / bundlesim_new_product_library):
; a LongBranchNormalize far-site promotion CONSUMES the promoted pair key
; (the LUI+ADDI+cond+JALR long form carries no range-pair MBB operand), so
; the key vanishes from the budget while the rewrite's own byte growth is
; attributed to the SURVIVING spans that grew. The vanished key's
; migration evidence is a Promotion-class event scoped to exactly that
; key (addVanishedPairEvents), gated on the window's IndirectCount census
; rise — not a global boolean, and never credit on an unrelated span.
; Without it, the exact ledger fatals "vanished pair bb.X->bb.Y with no
; split evidence" on a healthy promotion (working-tree D1.41 regression,
; HEAD's census-derived Ledger.Promotions covered the same class).

define i32 @exact_ledger(ptr nocapture readonly %a, i32 %n) {
entry:
  %c = icmp sgt i32 %n, 0
  br i1 %c, label %b1, label %e
b1:
  %x = load i32, ptr %a, align 4
  %v1 = add i32 %x, %n
  %d1 = icmp sgt i32 %v1, 10
  br i1 %d1, label %b2, label %e
b2:
  %v2 = mul i32 %v1, 3
  br label %e
e:
  %r = phi i32 [ 0, %entry ], [ %v1, %b1 ], [ %v2, %b2 ]
  ret i32 %r
}
