//===-- HaydnCompositeFormats.td - Product Format E composites -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Bundle composite instruction records.
//
// Product path: Format E 96-bit / 12-byte parcels only.
//   BUNDLE_E96_TWO_ENTRY   — entry_num=0 (E96TwoEntry registry row)
//   BUNDLE_E96_THREE_ENTRY — entry_num=1 (E96ThreeEntry registry row)
// Both rows share EncodedBytes = (96+7)/8 = 12; they are internal geometries
// of one family (FormatE96), not two product formats and not variable length.
//
// CodeGenFormat emits these as the live PacketFormats table (sorted by
// Inst bit-width / 8). Selectors and the packetizer must use these rows;
// the neutral ObjectEncodingProfile (E96) enumerates the same two rows.
//
// FE8: dual-inert 128-bit full composite retired — zero retired product emit
// opcode. Residual `_S*` FU members remain for placement tables only.
//
// Member alternatives / inverse / setDesc ledger: HaydnGenFormatE*.inc
// (FormatE/generate_format_e_records.py).
//
//===----------------------------------------------------------------------===//

// ---------------------------------------------------------------------------
// Product Format E composites (Size = EncodedBytes = 12).
//
// Bit layout (MSB-first TableGen concat, matches golden Overview):
//   E2: pad[95:92] | entry1[91:51] | entry0[50:6] | reserved[5:4] |
//       entry_num[3]=0 | format_indicator[2:0]=111
//   E3: pad[95] | entry2[94:68] | entry1[67:37] | entry0[36:6] |
//       reserved[5:4] | entry_num[3]=1 | format_indicator[2:0]=111
//
// Top-pad bits are fixed zero (fail-closed) until golden publishes pad policy.
// ---------------------------------------------------------------------------

// Two-entry Format E parcel (registry row E96TwoEntry).
//
// Bundle TEXT is written HIGH entry first — `{ e1; e0 }`. That matches the
// MSB-first Inst concat below and the ISA spelling (high field first,
// right-aligned on e0). Only the AsmString order is reversed; the operand
// dag stays $e0/$e1 so encoding and decoding are unaffected (CB-142 / #10).
class HaydnFormatE2Composite<dag entry_ins>
    : HaydnFormatInst<(outs), entry_ins, "$e1; $e0", []> {
  let isComposite = true;
  let Size = 12;
  let DecoderNamespace = "FormatE96";
  bits<45> e0;
  bits<41> e1;
  bits<4> top_pad = 0;
  bits<2> hdr_reserved = 0;
  bits<1> entry_num = 0;
  bits<3> format_indicator = 0b111;
  bits<96> Inst =
      {top_pad, e1, e0, hdr_reserved, entry_num, format_indicator};
}

def BUNDLE_E96_TWO_ENTRY : HaydnFormatE2Composite<
    (ins e2_0_slot:$e0, e2_1_slot:$e1)>;

// Three-entry Format E parcel (registry row E96ThreeEntry).
// Bundle TEXT: `{ e2; e1; e0 }` — high entry first (see E2 comment).
class HaydnFormatE3Composite<dag entry_ins>
    : HaydnFormatInst<(outs), entry_ins, "$e2; $e1; $e0", []> {
  let isComposite = true;
  let Size = 12;
  let DecoderNamespace = "FormatE96";
  bits<31> e0;
  bits<31> e1;
  bits<27> e2;
  bits<1> top_pad = 0;
  bits<2> hdr_reserved = 0;
  bits<1> entry_num = 1;
  bits<3> format_indicator = 0b111;
  bits<96> Inst =
      {top_pad, e2, e1, e0, hdr_reserved, entry_num, format_indicator};
}

def BUNDLE_E96_THREE_ENTRY : HaydnFormatE3Composite<
    (ins e3_0_slot:$e0, e3_1_slot:$e1, e3_2_slot:$e2)>;

// FE8: dual-inert 128-bit full composite deleted. Product emit/select is
// Format E only (BUNDLE_E96_* above).
