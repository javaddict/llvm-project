# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o 2>&1 | \
# RUN:   FileCheck %s

# Role: object — dedicated hostile-parcel probe for the bounds-safe
# printOperand / printRegName `<?>` defense family (CLAUDE.md
# "Disassembler crash defense"). Hard bar: decode-or-degrade, never abort.

# REGRESSION TEST: the Haydn disassembler/printer must never abort on hostile
# .text bytes, and degradation must be PER-PARCEL (a bad parcel never stops
# later parcels from rendering).
#
# Bug class this test family guards (CLAUDE.md "bounds-safe printOperand"):
# the generated printInstruction (HaydnGenAsmWriter.inc) indexes operands by
# FIXED position; historical decode paths could emit an MCInst whose operand
# count was short of what the printer string indexes (straddle read at a +6
# cursor; s2/s1 ALU64 2-operand destructive members vs a 3-operand .td
# printer; reserved/placeholder words like set_hwloop_f2). Without the
# printOperand/printRegName guards, MI->getOperand(OpNo) hits SmallVector's
# `idx < size` assert and objdump aborts (exit 134) — historically on
# dct4/fft/firinterp/ifft NatureDSP objects.
#
# Current-reachability note (verified 2026-08-15 by exhaustive header/second-
# byte sweeps: ZERO `<?>` renders from raw bytes): the current Format E
# decoder soft-underfills every unknown entry to architectural NOP
# (HaydnDisassembler.cpp "Soft underfill: unknown entry -> NOP"), and the
# generated member decoders fill all operands or Fail. So the `?>`-in-lieu
# guards are today defense-in-depth, NOT reachable from raw bytes; the only
# in-tree `<?>` CHECK lines (top of cb76-residual-jt-base-lui-hi12.s) are
# stale unused prefixes — its RUN lines use RELOCS/ELF only. If a future
# decoder change reintroduces short MCInsts, THIS test is the byte-level
# canary: it must still exit 0. Do not "fix" a failure here by relaxing the
# CHECKs — fix the decode/printer to keep the decode-or-degrade contract.
#
# Test design: 12-byte raw Format E parcels covering the hostile surface:
# all-ones payload behind valid E3/E2 headers, high-entropy payloads, and a
# soft-fail header (0x03) interleaved BETWEEN renderable parcels to pin
# per-parcel degradation. Byte sequences are raw .byte data (no relocations)
# and were verified deterministic against the current toolchain: all-ones
# E3 -> `{ nop } // <unresolved:0x1ffffffffffc>`; the 0x03 header parcel ->
# `<unknown>`; high-entropy payloads -> `{ nop }` with/without annotation.
# CHECKs pin the stable surface (completion, nop/<unknown> degradation,
# unresolved annotation) without pinning cosmetic spacing.

.text
.globl hostile_placeholder
.type hostile_placeholder,@function
hostile_placeholder:

# Parcel 1 (0x00): valid E3 header, all-ones payload. Degrades to a single
# soft-NOP with an unresolved-payload annotation.
.byte 0x07, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff

# Parcel 2 (0x0c): valid E3 header, high-entropy payload; soft-NOP, no
# annotation (payload matched no unresolved-annotation condition).
.byte 0x07, 0x00, 0x1f, 0xa1, 0x61, 0x19, 0x95, 0x5e, 0x89, 0x82, 0x05, 0x97

# Parcel 3 (0x18): E2-style header 0x03, all-ones payload — whole parcel
# soft-fails to `<unknown>`. Interleaved to prove a failed parcel does not
# abort or suppress later parcels.
.byte 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff

# Parcel 4 (0x24): two more distinct high-entropy E3 payloads (different
# entry-field patterns than parcel 2), each rendering independently.
.byte 0x07, 0x1c, 0xaf, 0x83, 0x24, 0x00, 0xde, 0x3a, 0xdf, 0x29, 0xbd, 0xfe
.byte 0x07, 0x23, 0xd8, 0xc0, 0x86, 0xa7, 0xb0, 0x3b, 0x8b, 0x7c, 0x20, 0x11

# Parcel 6 (0x3c): all-ones again behind header 0x1f (non-E2/E3 geometry
# bits) — still must not abort the tool.
.byte 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff

# CHECK-LABEL: Disassembly of section .text:

# The hard bar: objdump completes (RUN pipeline exit 0) and never aborts.
# CHECK-NOT: {{Assertion|abort|Stack dump|PLEASE submit a bug report}}

# Parcel 1 degrades to a soft-NOP carrying the unresolved-payload
# annotation (T-MC9 contract: annotate, do not silently swallow).
# CHECK: {{0:.*nop.*unresolved}}

# Parcel 2 renders (soft-NOP, no annotation required).
# CHECK: {{c:.*nop}}

# Parcel 3 soft-fails the whole parcel to <unknown> WITHOUT killing later
# parcels — the per-parcel degradation contract.
# CHECK: {{18:.*<unknown>}}

# Parcels 4/5 still render after the <unknown> parcel.
# CHECK: {{24:.*nop}}
# CHECK: {{30:.*nop}}

# Parcel 6 (odd geometry header 0x1f, not E2/E3 geometry) soft-fails the
# whole parcel to <unknown> — also a legal degradation; no abort.
# CHECK: {{3c:.*<unknown>}}
