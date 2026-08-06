//===-- HaydnFormatsE96.td - Format E entry members / bases -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Format E (family FormatE96) entry-window base classes and product-row
// placement scaffolding.
//
// Product geometry (hashed golden Format E layout):
//   bundle_bits = 96, encoded bytes = (bundle_bits + 7) / 8
//   header[2:0] = format_indicator 0b111
//   header[3]   = entry_num (0 = two-entry, 1 = three-entry)
//   header[5:4] = reserved 0
//
// Entry InstSlots live in HaydnSlots.td. Product composite opcodes live in
// HaydnCompositeFormats.td (BUNDLE_E96_TWO_ENTRY / BUNDLE_E96_THREE_ENTRY).
// Placement-member identity tables (type layouts, alternatives, inverse,
// setDesc ledger) are generated into HaydnGenFormatERecords.inc —
// regenerate via FormatE/generate_format_e_records.py.
//
// Golden-member fidelity (G-FORMAT-E-96-CUTOVER):
//   - Catalog / inverse / setDesc rows must match hashed golden JSON only.
//   - TD/table member count may be FEWER than residual `_S*` slot surface;
//     parity with the retired slot-pair encoding is not required and must
//     not invent non-golden logicals or placements.
//   - Live member Inst defs: HaydnFormatsE96Members.td.inc (JSON→TD once via
//     FormatE/generate_format_e_records.py). Product encode/decode uses
//     tblgen getBinaryCode / decoder tables on these members; C++ only frames
//     BUNDLE_E96_* parcels and selects the member.
//
// Entry sub-instructions are never emitted standalone: they are always
// composite operands of a Size=12 Format E parcel. Instruction Size on the
// entry bases is the ceiling byte width of the entry window only.
//
// Canonical idle / underfill / top-pad fill remains fail-closed until the
// golden set publishes completion vectors. Entry bases do not invent
// idle encodings.
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
