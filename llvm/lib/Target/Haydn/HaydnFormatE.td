//===-- HaydnFormatE.td - Format E entry bases + composites -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Authored Format E (family E96) product TableGen: entry-window bases,
// live generated members include, and Size=12 composites.
//
// Geometry (hashed golden Format E layout pair — not invented here):
//   bundle_bits = 96, encoded bytes = (bundle_bits + 7) / 8
//   header[2:0] = format_indicator 0b111
//   header[3]   = entry_num (0 = two-entry, 1 = three-entry)
//   header[5:4] = reserved 0
//
// Entry InstSlots live in HaydnSlots.td. Family identity lives in
// HaydnFamilies.td. Placement-member identity tables are generated
// (HaydnGenFormatERecords.inc via FormatE/generate_format_e_records.py).
//
// Live member Inst defs: HaydnFormatsE96Members.td.inc.
// Product encode/decode uses tblgen on those members; C++ frames
// BUNDLE_E96_* parcels and selects the member.
//
// Merged from HaydnFormatsE96.td + HaydnCompositeFormats.td (W56 file reshape).
//
//===----------------------------------------------------------------------===//

// ---------------------------------------------------------------------------
// Entry-window bases (mirror HaydnSlotS0/S1/S2 shape for Format E entries).
// Decoder namespaces are per entry position so CodeGenFormat can grow
// placement members independently of the legacy S0/S1/S2 tries.
// ---------------------------------------------------------------------------

// Entry window field name must match InstSlot FieldToFind (e0/e1/e2) at the
// golden entry width. Inst is Size*8 with high-zero pad so DecoderEmitter
// accepts Size vs Inst (Size is bytes; entry widths are not multiples of 8).
// encodeSlotSubInst takes the low entry-width bits of the tblgen result.
class HaydnEntryE2E0<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnFormatInst<outs, ins, asmstr, pattern> {
  let Slot = e2_0_slot;
  let DecoderNamespace = "E2E0";
  let Size = 6; // 48b container; golden entry is 45b
  bits<45> e0;
  bits<3> e0_hi_pad = 0;
  bits<48> Inst = {e0_hi_pad, e0};
}

class HaydnEntryE2E1<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnFormatInst<outs, ins, asmstr, pattern> {
  let Slot = e2_1_slot;
  let DecoderNamespace = "E2E1";
  let Size = 6; // 48b container; golden entry is 41b
  bits<41> e1;
  bits<7> e1_hi_pad = 0;
  bits<48> Inst = {e1_hi_pad, e1};
}

class HaydnEntryE3E0<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnFormatInst<outs, ins, asmstr, pattern> {
  let Slot = e3_0_slot;
  let DecoderNamespace = "E3E0";
  let Size = 4; // 32b container; golden entry is 31b
  bits<31> e0;
  bits<1> e0_hi_pad = 0;
  bits<32> Inst = {e0_hi_pad, e0};
}

class HaydnEntryE3E1<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnFormatInst<outs, ins, asmstr, pattern> {
  let Slot = e3_1_slot;
  let DecoderNamespace = "E3E1";
  let Size = 4;
  bits<31> e1;
  bits<1> e1_hi_pad = 0;
  bits<32> Inst = {e1_hi_pad, e1};
}

class HaydnEntryE3E2<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnFormatInst<outs, ins, asmstr, pattern> {
  let Slot = e3_2_slot;
  let DecoderNamespace = "E3E2";
  let Size = 4; // 32b container; golden entry is 27b
  bits<27> e2;
  bits<5> e2_hi_pad = 0;
  bits<32> Inst = {e2_hi_pad, e2};
}

// LIVE Format E entry members (Inst{} from golden). JSON→TD once:
//   FormatE/generate_format_e_records.py
include "HaydnFormatsE96Members.td.inc"

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
// right-aligned on e0). Unused entries of a selected row are architectural
// NOP (full-bundle fill): an e0 member prints `{ nop; $e0 }`. Only the
// AsmString order is reversed; the operand dag stays $e0/$e1 so encoding
// and decoding are unaffected (CB-142 / #10).
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
// An e0 member with architectural NOP in the other entries prints
// `{ nop; nop; $e0 }`.
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
