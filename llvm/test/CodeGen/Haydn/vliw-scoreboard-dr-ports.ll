; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -debug-only=haydn-hazard-rec < %s 2>&1 | FileCheck %s
; REQUIRES: asserts

; Role: semantic — DR64 7R3W port accounting in the scoreboard hazard recognizer (Stream B Phase 2.3,).

; REGRESSION TEST: DR64 7R3W port accounting in the scoreboard hazard
; recognizer (Stream B Phase 2.3,).
;
; The spec mandates a DR64 port budget of 7R/3W per cycle (an RTL SVA
; assertion: `bundle_dr_read_o <= 7`, `bundle_dr_write_o <= 3`, per
; formal_verification_decoder_sva.json and port_budget_analysis.py). Before
; the scoreboard hazard recognizer enforced DR ports NOWHERE
; countGPRPorts explicitly skipped DR64RegClass, and HaydnFuncUnitWrapper had
; no DR fields.
;
; This test pins the observable contract that DR64 port demand IS accounted
; for in the scoreboard's per-cycle resource footprint. It uses i64 arithmetic
; (selected to ADD64/SUB64/MUL64 on DR64 registers) so each candidate's
; footprint must carry a non-zero `dr:` field. If the countDRPorts wiring or
; the DRReads/DRWrites fields are ever removed or disconnected, the `dr:`
; field in the -debug-only=haydn-hazard-rec output goes to 0R/0W on these
; DR64-heavy instructions and the CHECK lines below fail.
;
; Why this is the right test shape: under the CURRENT slot model DR64 ops are
; restricted to slots 1/2, so at most 2 DR64 ops pack per bundle and max legal
; DR demand is 6R/2W < 7R/3W — the DR cap does not itself trigger a Hazard
; today (see §Finding 1 correction). So we cannot write a "DR cap
; rejects a packet" test against real codegen; instead this test pins the
; accounting, which is the load-bearing contract: when a future fused-MAC op
; (4+ DR reads) or a dual-write DR op arrives and the cap DOES bind, the
; conflict check added in will fire. The accounting must be wired and
; correct-by-construction before that day.
;
; Concretely the debug output for an ADD64 (2 DR64 sources, 1 DR64 dest)
; candidate must show `dr:2R/1W`, and a cycle accumulating two such ops must
; show `dr:4R/2W`.

define i64 @dr_port_accounting(i64 %a, i64 %b, i64 %c, i64 %d) nounwind {
; DR64 port accounting must appear in candidate footprints. A single i64 add
; (selected to ADD64 on DR64 regs) carries dr:2R/1W; two accumulated carry
; dr:4R/2W. The exact slot pattern varies with scheduling; the dr: field is
; the contract being pinned.
; CHECK: dr:2R/1W
; CHECK: dr:4R/2W
entry:
  %x0 = add i64 %a, %b    ; add64 d, d, d -> 2 DR reads, 1 DR write
  %x1 = add i64 %c, %d
  %m = mul i64 %x0, %x1
  %y = add i64 %m, %x0
  ret i64 %y
}
