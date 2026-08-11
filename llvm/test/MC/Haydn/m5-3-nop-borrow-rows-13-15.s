# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --triple=haydn-unknown-elf %t.o \
# RUN:   | FileCheck %s --implicit-check-not='<unknown>' --implicit-check-not='<?>'
# WHAT THIS TEST NOW IS. It began as the M5-3 investigation into Mode-0
# "NOP-borrow rows 13-15": rows that existed in M0Rows but were dead code,
# because findCompatibleRow enforced ChildPrefSlots[i] == SlotIdx and no
# EM_64BitM0 mapping ever preferred S2. All of that machinery — M0Rows, the
# EM_64BitM0 map, findCompatibleRow, the legacy-flat fallback — belonged to
# Bundle128 and is gone with it, so the question the file was opened to
# answer cannot be asked any more.
#
# Two of its recorded findings are resolved rather than fixed:
#   * standalone `x2mul32` used to render `<unknown>` (a decoder gap on a
#     surviving emit path). It decodes now.
#   * the bundle's grouping used to depend on which row the encoder picked.
#     Format E has no rows; a bundle is entries and units.
#
# What survives is worth keeping and is what the file already said its
# critical assertions were: ADD32 and X2MUL32 co-issue in one bundle, and
# both round-trip with their ORIGINAL operands — no silent drop, and no
# destructive-MAC operand aliasing on the 4-operand form. The standalone
# renders are the control. Placement is not asserted: the file always said
# "the exact bundle grouping depends on..." and then pinned an order anyway.
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
# CHECK-LABEL:      m5-3-nop-borrow-rows-13-15.s
# CHECK:            00000000 <.text>:
# BOTH ops appear on one bundle line with their original operands, in
# either print order. X2MUL32 is TRUE 2-output, so the 4-operand asm form is
# part of what must round-trip: an aliasing regression shows up as a repeated
# or dropped register here, not as a decode failure.
# CHECK:            { {{x2mul32 d0, d1, d2, d3.*add32 r0, r1, r2|add32 r0, r1, r2.*x2mul32 d0, d1, d2, d3}}
        { add32 r0, r1, r2 ; x2mul32 d0, d1, d2, d3 }

#===----------------------------------------------------------------------===#
# Case 2: standalone versions of the same ops, as a control. If Case 1's
# bundle round-trip matches these standalone renders, co-issue is not
# corrupting operands. `x2mul32` standalone is also the one that used to
# render <unknown>; --implicit-check-not below is the hard bar on that.
#===----------------------------------------------------------------------===#
# CHECK:            add32 r3, r4, r5
        add32 r3, r4, r5
# (Path B): X2MUL32 is now TRUE 2-output — 4-operand asm form.
# CHECK:            x2mul32 d3, d4, d5, d6
        x2mul32 d3, d4, d5, d6

#===----------------------------------------------------------------------===#
# Negative assertion: no operand corruption. The bundle must not produce a
# destructive-MAC artifact (e.g. x2mul32 with rtd forced to rsd1, turning
# "d0,d1,d2" into "d1,d1,d2"). The CHECK lines above with explicit distinct
# operands catch this — if the fallback path wrongly applied the s2
# destructive rtd=rsd1 constraint, the first operand would alias the second.
#===----------------------------------------------------------------------===#
