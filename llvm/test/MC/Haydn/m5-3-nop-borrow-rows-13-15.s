# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s

// CHECK: {{.*}}0: 07 8b 00 21 00 00 48 10 10 32 00 00 { 	x2mul32	d0, d1, d2, d3; 	add32	r0, r1, r2 }
// CHECK: {{.*}}c: 07 8b 30 54 00 00 00 00 00 00 00 00 { 	nop; 	add32	r3, r4, r5 }
// CHECK: {{.*}}18: 47 02 31 54 06 00 00 00 00 00 00 00 { 	nop; 	x2mul32	d3, d4, d5, d6 }
# Role: object — Phase-2 decoder purge collateral (prior revision): the { add32; x2mul32 } bundle packs add32 into the s0 ALU32 sub-row (SURVIVES) and x2mul32 into.

# Phase-2 decoder purge collateral (prior revision): the { add32; x2mul32 }
# bundle packs add32 into the s0 ALU32 sub-row (SURVIVES) and x2mul32 into
# the s1/s2 MAC sub-row (DELETED). The standalone add32/x2mul32 control
# cases: add32 decodes, x2mul32 renders `<unknown>`. Real decoder gap on a
# SURVIVING emit path (Mode-0 s1/s2 MAC sub-row decoder must be restored).
# Do NOT weaken the CHECKs.

# M5-3 INVESTIGATION TEST: NOP-borrow rows 13-15 (encoding_manual.md §6).
#
# STATUS: NOT a clean PASS/round-trip closure. This test documents a REAL
# finding: the NOP-borrow rows 13-15 EXIST in M0Rows (HaydnDClassInfo.h
# rows 12-14, FU_NONE s1 / ST_Reserved) but are DEAD CODE — unreachable
# from the current encoder. The CLAUDE.md item "Mode 0 NOP-borrow rows
# 13-15" is NOT closed; it is reframed as "rows exist but need Wave-2
# encoder work to model S2/destructive forms." See decision §M5-3.
#
# Root cause (verified live): findCompatibleRow (HaydnMCCodeEmitter.cpp:216)
# enforces ChildPrefSlots[ChildIdx] == SlotIdx (the slot-consistency
# check). NOP-borrow rows 13-15 require a child whose getEncOpcodeMap
# preferred slot is S2. But ZERO EM_64BitM0 mappings prefer S2 — all 123
# mappings are S0 (56) or S1 (67). So no MAC or ALU64 op can ever match an
# S2 slot, and rows 13-15 are never selected. A 2-child { ALU; MAC } bundle
# instead matches a full-3-slot row (1-12) or falls through to legacy-flat.
#
# The comment in HaydnDClassOpcodes.cpp:236 ("findCompatibleRow matches
# children by FU type, not by slot index") is STALE — the actual code DOES
# enforce slot index. This is recorded in §M5-3 as a doc/code
# drift to fix alongside the Wave-2 S2 work.
#
# Why NOT fix the encoder here: making S2 reachable is NOT a low-risk
# decoder easy win. It interacts with the destructive-s2-MAC constraint
# (rtd=rsd1, ST_RRR 2-field layout that has NO rsd2 field), the
# scheduler's slot assignment, and the OOB-safety check that prevents
# non-destructive S1 instructions from being silently turned into
# destructive S2 forms after register allocation (where MC cannot insert
# copies to repair the value flow). Per /oh-my-claudecode:ask codex:
# "option (a) [relaxing the slot-consistency check] is too broad — it
# risks turning a non-destructive S1 instruction into a destructive S2
# form after scheduling/regalloc, where MC cannot insert copies or repair
# the value flow." The correct Wave-2 fix: explicitly model which opcodes
# have valid S2/destructive forms in TableGen, enforce tied/destructive
# operands before MC emission, align scheduler slot legality, then add
# encode/decode round-trip tests for rows 13-15. That is encoder-owned
# cross-stream work — out of scope for "M5 decoder easy wins."
#
# What this test DOES verify (current, honest behavior):
# 1. A 2-child { ALU32 (S0-pref); MAC (S1-pref) } bundle does NOT crash.
# 2. It does NOT mis-encode — the encoder falls back to a full row or
# legacy-flat rather than corrupting operands.
# 3. The round-trip produces valid (if not NOP-borrow-row) disassembly.
#
# When the Wave-2 encoder S2-modeling work lands, this test should be
# EXTENDED with a case that asserts a { ALU32; MAC } bundle packs into
# row 13 (s0=ALU, s1=NOP, s2=MAC) and round-trips with s1 as an explicit
# NOP slot. Until then, this test is a regression guard against the
# fallback path silently corrupting operands.
#
# Spec reference: encoding_manual.md §6 rows 13-15, §10 NOP-borrow.
# Related: §M5-3; scoping ~/haydn-plans/reviews/m5-encoding-scoping.md
# §(b) M5-3; codex artifact
# .omc/artifacts/ask/codex-haydn-vliw-dsp-encoder-...-2026-06-16T19-08-54-380Z.md

#===----------------------------------------------------------------------===#
# Case 1: 2-child { ADD32 (S0-pref); X2MUL32 (S1-pref) } bundle.
# Neither child prefers S2, so row 13 (s0=ALU, s1=NOP, s2=MAC) CANNOT match.
# The encoder selects a full row (e.g. row 2: ALU+ALU64+MAC, with the ALU64
# slot left as NOP) or legacy-flat. The critical assertions: no crash, no
# operand corruption (each child round-trips with its original operands).
#===----------------------------------------------------------------------===#
# ADD32 and X2MUL32 must both survive the round-trip with their original
# operands. The exact bundle grouping depends on which row the encoder
# picks; we assert only that BOTH ops appear with their original operands
# (no silent drop, no destructive-MAC operand aliasing).
# (Path B): X2MUL32 is now TRUE 2-output — 4-operand asm form.
# Every op is a 12-byte Format E composite; slot suffix / appears.
# B3.5 S2-first: x2mul32→S1, add32→S2 → print order x2mul32 then add32.

        { add32 r0, r1, r2 ; x2mul32 d0, d1, d2, d3 }

#===----------------------------------------------------------------------===#
# Case 2: standalone versions of the same ops, as a control. If Case 1's
# bundle round-trip matches these standalone renders, the fallback path is
# not corrupting operands.
#===----------------------------------------------------------------------===#
        add32 r3, r4, r5
# (Path B): X2MUL32 is now TRUE 2-output — 4-operand asm form.
        x2mul32 d3, d4, d5, d6

#===----------------------------------------------------------------------===#
# Negative assertion: no operand corruption. The bundle must not produce a
# destructive-MAC artifact (e.g. x2mul32 with rtd forced to rsd1, turning
# "d0,d1,d2" into "d1,d1,d2"). The CHECK lines above with explicit distinct
# operands catch this — if the fallback path wrongly applied the s2
# destructive rtd=rsd1 constraint, the first operand would alias the second.
#===----------------------------------------------------------------------===#
