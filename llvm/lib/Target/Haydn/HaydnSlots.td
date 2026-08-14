// ===-- HaydnSlots.td - CodeGenFormat slot / entry model -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: license information.
//
// CodeGenFormat InstSlot pool for Haydn.
//
// Product (Format E 96-bit / 12-byte):
//   Entry positions are first-class InstSlots. Two product composites own
//   them (HaydnCompositeFormats.td):
//     BUNDLE_E96_TWO_ENTRY   — E2 entry0 (45b) + entry1 (41b)
//     BUNDLE_E96_THREE_ENTRY — E3 entry0 (31b) + entry1 (31b) + entry2 (27b)
//   Header bits (format_indicator / entry_num / reserved) and top-pad live
//   as fixed fields on the composite Inst, not as InstSlots.
//
// Residual S0/S1/S2 windows (not product composites; FE8 deleted 128-bit full):
//   s0/s1/s2 InstSlots remain so residual FU member families
//   (HaydnFormatsALU32/ALU64/LS/LD/MAC.td) type-check for placement tables.
//
// Migrated instructions use HaydnFormatInst (derives InstFormat; the
// backend's isSubClassOf("InstFormat") filter in CodeGenFormat.cpp picks them
// up; legacy HaydnInst defs are skipped).
//===----------------------------------------------------------------------===//

let Namespace = "Haydn" in {
  // ---- Residual S0/S1/S2 windows for FU member placement (not product) ----
  def s0_slot : InstSlot<"S0", 48> { let FieldToFind = "s0"; }
  def s1_slot : InstSlot<"S1", 40> { let FieldToFind = "s1"; }
  def s2_slot : InstSlot<"S2", 40> { let FieldToFind = "s2"; }

  // ---- Product Format E entry positions (golden entry bit-widths) ----
  // E2 (entry_num=0): entry0 [50:6]=45b, entry1 [91:51]=41b
  def e2_0_slot : InstSlot<"E2_0", 45> { let FieldToFind = "e0"; }
  def e2_1_slot : InstSlot<"E2_1", 41> { let FieldToFind = "e1"; }
  // E3 (entry_num=1): entry0 [36:6]=31b, entry1 [67:37]=31b, entry2 [94:68]=27b
  def e3_0_slot : InstSlot<"E3_0", 31> { let FieldToFind = "e0"; }
  def e3_1_slot : InstSlot<"E3_1", 31> { let FieldToFind = "e1"; }
  def e3_2_slot : InstSlot<"E3_2", 27> { let FieldToFind = "e2"; }
}

// Flex-format base. Derives InstFormat so the CodeGenFormat backend tracks it
// (Slot/isComposite/isSlotNOP fields). Does NOT declare Inst -- slot/entry and
// composite subclasses declare the width (entry windows for Format E product
// composites; residual s0/s1/s2 windows for dual-inert members).
class HaydnFormatInst<dag outs, dag ins, string asmstr, list<dag> pattern>
    : Instruction, InstFormat {
  let Namespace = "Haydn";
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
  let Itinerary = NoItinerary;
  // Pure-compute FLEX ops (ALU/MAC/load) carry no unmodeled side effects.
  // TableGen defaults pattern-less defs (Pattern =) to hasSideEffects = 1,
  // surfacing as MCID::UnmodeledSideEffects, which adds spurious WAW/order
  // edges to the SMS DDG and trips the body-scan reject in
  // HaydnInstrInfo::analyzeLoopForPipelining. The default-0 here is
  // truthful for the bulk of FLEX (pure compute). Genuinely-impure formats
  // (stores, branches, JAL/JALR, HWLOOP-set, CSR writes, WFI, AR/CB ops that
  // touch address-generation registers) override this to 1 in their class
  // bodies -- see hasSideEffects audit table.
  let hasSideEffects = 0;
  // No fixed Size here -- entry/slot sub-instructions set Size per window;
  // product composites (HaydnCompositeFormats.td) set Size = EncodedBytes 12.
  // Entry/slot sub-instructions are never emitted standalone.
  let DecoderNamespace = "HaydnFlex";
}

// Dual-inert slot-window bases (legacy FU families). Product
// Format E entry bases live in HaydnFormatsE96.td. AsmStrings are slot-free
// (no `.s0`/`.s1`/`.s2`); bare mnemonics shared across multi-slot ops.
// InstPrinter prints the same bare AsmString for every slot; post-commit
// authority is member Desc getSlotKind / Bundle SlotMap.
//
// AsmVariantName fieldslot: public matcher skips these (same idiom as
// generated members' e96member). Residual S0/S1/S2 decoder tries stay;
// do not use isCodeGenOnly here.
class HaydnSlotS0<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnFormatInst<outs, ins, asmstr, pattern> {
  let Slot = s0_slot;
  let DecoderNamespace = "S0";
  let AsmVariantName = "fieldslot";
  let Size = 6;  // 48-bit slot window
  bits<48> s0;
  bits<48> Inst = s0;
}

class HaydnSlotS1<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnFormatInst<outs, ins, asmstr, pattern> {
  let Slot = s1_slot;
  let DecoderNamespace = "S1";
  let AsmVariantName = "fieldslot";
  let Size = 5;  // 40-bit slot window
  bits<40> s1;
  bits<40> Inst = s1;
}

class HaydnSlotS2<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnFormatInst<outs, ins, asmstr, pattern> {
  let Slot = s2_slot;
  let DecoderNamespace = "S2";
  let AsmVariantName = "fieldslot";
  let Size = 5;  // 40-bit slot window
  bits<40> s2;
  bits<40> Inst = s2;
}

// Per-FU dual-inert slot-format classes + per-op defs: HaydnFormats<FU>.td.
// Product Format E entry bases + composite rows: HaydnFormatsE96.td /
// HaydnCompositeFormats.td. Multi-slot logicals: HaydnMultiSlotPseudo.td.
