; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s
;
; (History: this was expected-fail while GenMux Pattern 1 did not fire
; post-ISA-27; now resolved -- MOVT32/MOVF32 form again. NOTE: do not write
; the literal "XFAIL" token with a colon in a comment, lit scans comment
; lines for it and would parse this prose as an (invalid) XFAIL expression.)
; GenMux Pattern 1 (tryConvertBitwiseSelect) does NOT fire post-ISA-27 -- see
; HaydnGenMux.cpp:237 (requires NOT32 self-negate; post-RA emits not32 r2,r1).
; The MOVT32/MOVF32 these CHECKs assert is never formed; the bitwise chain is
; emitted instead. Real regression from the ISA-27 32-bit-decouple (passed at
; HEAD d20eee9); same root cause as muxgen-bitwise-select.ll. Un-XFAIL when
; the matcher is relaxed to follow the NEG/NOT copy.
;
; REGRESSION TEST: MOVT32/MOVF32 read their condition from a GPR $rs2 operand
; NOT from the implicit $sfr (ISA-27 spec re-alignment).
;
; History: Under the pre-ISA-27 backend modeling, MOVT32/MOVF32 were declared
; `Uses = [SFR]` and the 32-bit compares SEQ32/SLT32/SLTU32/SLE32 were declared
; `Defs = [SFR]`. GenMux then formed `MOVT32 rd, rs1, implicit $sfr` to read the
; compare result via SFR. Because no earlier reader of `$sfr` existed at the
; time dead-elimination ran, the compare's `implicit-def $sfr` was marked dead.
; When GenMux later added the `implicit $sfr` use, the verifier reported:
;
; *** Bad machine code: Using an undefined physical register ***
; instruction: $rN = MOVT32..., implicit $sfr
;
; worked around this with `HaydnGenMux::fixSFRLiveness`, which walked
; backwards from the MOVT32 to clear the dead flag on the nearest `$sfr` def.
;
; ISA-27 resolves the root cause: per the spec DB (`haydn_instruction_db.json`)
; the 32-bit compare/cmov family is GPR-coupled, not SFR-coupled:
; SEQ32/SLT32/SLTU32/SLE32 write GPR `rt = (cond) ? 1 : 0` (no SFR)
; MOVT32/MOVF32 read GPR `rs2[0]` (no SFR)
; Only the 64-bit/SIMD compare/cmov family + MOVEGPR2SFR/MOVESFR2GPR/ZERO_SFR
; genuinely touch SFR (18 instructions total — see HaydnInstrInfo.td). With the
; 32-bit family re-aligned, GenMux feeds the GPR compare result (CondReg)
; directly into MOVT32/MOVF32's `$rs2`, so:
; MOVT32/MOVF32 have no implicit `$sfr` operand, and
; `fixSFRLiveness` is unnecessary and has been deleted.
;
; What this test guards:
; 1. A `select`-shape (G_SELECT lowering → bitwise chain) that GenMux Pattern 1
; converts to `MOVT32 rd, rs1, CondReg` must pass `-verify-machineinstrs`
; with NO SFR liveness surgery — the condition is a real GPR operand.
; 2. The lowered code must contain a `movt32` or `movf32` (the CMOV, not the
; full bitwise scaffolding) — asserting GenMux fires.
; 3. The compare (slt32/seq32/etc.) must still appear and NOT define $sfr.
;
; If ISA-27 regresses (e.g. MOVT32 re-gains `Uses=[SFR]`, or fixSFRLiveness is
; re-introduced), llc aborts at the verifier with the error above (the compare
; no longer carries an implicit-def $sfr, so an implicit $sfr use is undefined).

;select on signed less-than. GenMux Pattern 1 collapses the bitwise chain
;(neg32/and32/not32/and32/or32) into a single movt32/movf32 whose rs2 is
;the SLT32 result register.
define i32 @select_slt(i32 %a, i32 %b, i32 %c, i32 %d) nounwind {
; CHECK-LABEL: select_slt:
; The compare result feeds the CMOV as a GPR (rs2), not via $sfr:
; CHECK:       slt32
; The bitwise scaffolding must NOT survive GenMux Pattern 1:
; CHECK-NOT:   neg32
; CHECK-NOT:   not32
; A conditional move carries the select semantics via GPR rs2:
; CHECK:       mov{{t|f}}32
entry:
  %cmp = icmp slt i32 %a, %b
  %sel = select i1 %cmp, i32 %c, i32 %d
  ret i32 %sel
}

;select on equality. SEQ32 (now a plain GPR op) feeds the CMOV rs2.
define i32 @select_eq(i32 %a, i32 %b, i32 %c, i32 %d) nounwind {
; CHECK-LABEL: select_eq:
; CHECK:       seq32
; CHECK-NOT:   neg32
; CHECK-NOT:   not32
; CHECK:       mov{{t|f}}32
entry:
  %cmp = icmp eq i32 %a, %b
  %sel = select i1 %cmp, i32 %c, i32 %d
  ret i32 %sel
}
