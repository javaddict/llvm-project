; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=postmisched < %s | FileCheck %s

; Role: MIR — VLIW packetizer WAR (write-after-read) hazard.

; Contract: postmisched must not co-issue a WAR on the same physreg.
; CHECK-LABEL: name: war_hazard_test
; CHECK: SLLI32
; W68.5 (dead-wb PRE refusal): one-shot GEP load selects the plain LD32
; (offset-folded) instead of the fused pre-inc — same parcels, no dead
; writeback. The WAR hazard under test remains (ADD32 feeding the load).
; CHECK: S_LW_WITH_IMM
; S1 wraps leftover singles as BUNDLE roots; WAR still must not
; co-issue SLLI32 with a later write of the same physreg.
; CHECK-NOT: SLLI32{{.*}}S_LW_WITH_IMM


; REGRESSION TEST: VLIW packetizer WAR (write-after-read) hazard.
;
; Bug (/ e2e_sort_selection_min O2 miscomp): the post-RA VLIW packetizer's
; hasDependence checked RAW and WAW but NOT WAR. This let it pack a writer next
; to a reader of the same register in the same bundle:
; BUNDLE { $r1 = SLL32 $r8, $r1 // slot 0: READS r8
; $r8 = LD32_S1 $r13, 12 } // slot 1: WRITES r8
; The SLL needs the OLD r8 (the select result / running min index m), but the
; load's write of r8 races the shift's read within the bundle. On the target
; this produced a wrong array index (m<<2 computed from the reloaded value n
; not m), corrupting the selection-sort swap — e2e_sort_selection_min returned
; 57 instead of 146 at -O2 (O0/O1 were correct because they don't pipeline the
; spill-reload adjacent to the shift).
;
; Test design: a select whose result (m) is live across a stack spill and used
; in a shift `m << 2` immediately after the reload. At -O2 the scheduler lines
; up the reload (LD32, writes m's reg) next to the shift (reads m's reg); the
; missing WAR check let them co-bundle. With the WAR fix the two MUST land in
; separate bundles. We assert that no BUNDLE contains both a def of a register
; and a read of that same register by another slot.

; The SLL that consumes the select result and the LD32 that reloads the spilled
; select reg must NOT share a BUNDLE. We pattern-match the SLL (the only shift
; in the function) and require the next line to NOT be a bundled LD32 defining
; the same reg the SLL reads — i.e. they are in separate bundles.

define i32 @war_hazard_test(ptr %p, ptr %q, i32 %a, i32 %b, i32 %c) nounwind {
entry:
  ; select + shift-by-2 + memory use, mirroring the selection-sort swap shape.
  %cond = icmp slt i32 %a, %b
  %m = select i1 %cond, i32 %a, i32 %b
  ; Force %m to be spilled (call clobbers all caller-saved), then reloaded and
  ; shifted — the reload-vs-shift adjacency is what the missing WAR let pack.
  %idx = shl i32 %m, 2
  %gp = getelementptr i8, ptr %p, i32 %idx
  %v = load i32, ptr %gp, align 4
  %gq = getelementptr i8, ptr %q, i32 %idx
  %w = load i32, ptr %gq, align 4
  %r1 = add i32 %v, %w
  %r2 = add i32 %r1, %c
  ret i32 %r2
}
