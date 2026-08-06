// ===-- HaydnSlots.td - CodeGenFormat slot model --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: license information.
//
// 128-bit-only Bundle128 slot model for the AIE CodeGenFormat backend
// (-gen-instr-format). Mirrors AIE2Slots.td, adapted to Haydn's
// positional-slot x FU model (encoding_manual_flex.md):
//
// bundle[127:0] = { s2[39:0], s1[39:0], s0[47:0] } (s2 HIGH, s0 LOW)
//
// 3 POSITIONAL slots: s0=48b (all 5 FUs + CFG/long-imm), s1=40b, s2=40b
// (s1==s2; no CFG/long-imm). Unlike AIE (one InstSlot per FU lane), each
// Haydn slot hosts all 5 FUs; the 3-bit FU field at each slot's MSB
// discriminates ALU32/LS/ALU64/LD/MAC. The per-slot sub-trie therefore
// dispatches by FU bits first, then per-FU opcode -- same CodeGenFormat
// mechanism as AIE's per-slot decode, just with an FU discriminator.
// 128-bit-only: every parcel is 16 bytes. Slot sub-instructions are always
// emitted as operands of the Bundle128 composite (no narrow standalone
// parcels), unlike AIE's 4-byte standalone slot emits.
//
// Distinct from HaydnInst (which pre-declares bits<64> Inst for the legacy
// encoder). Migrated instructions use HaydnFormatInst (derives InstFormat; the
// backend's isSubClassOf("InstFormat") filter in CodeGenFormat.cpp picks them
// up; legacy HaydnInst defs are skipped during the incremental migration).
//===----------------------------------------------------------------------===//

// The three Bundle128 InstSlots (s0_slot/s1_slot/s2_slot) are GONE. Format E
// declares its own five entry slots in HaydnFormatEEncoding.td (p20/p21 for the
// 2-entry form, p30/p31/p32 for the 3-entry form). Leaving the Bundle128 three
// declared would not merely be dead weight: every InstSlot becomes an
// MCSlotKind enumerator and a row in the generated HaydnSlots table, so they
// would push the format E kinds off 0..4, and each would carry a ConflictBits
// of "everything" (they appear in no live composite) for the packer to trip
// over.

// Flex-format base. Derives InstFormat so the CodeGenFormat backend tracks it
// (Slot/isComposite/isSlotNOP fields). Does NOT declare Inst -- the slot and
// composite subclasses declare the width (bits<48>/40 for a slot window
// bits<128> for Bundle128), since Haydn is 128-bit-only and slot windows are
// wider than HaydnInst's fixed bits<64>.
class HaydnFormatInst<dag outs, dag ins, string asmstr, list<dag> pattern>
    : Instruction, InstFormat {
  let Namespace = "Haydn";
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
  let Itinerary = NoItinerary;
  // Fix 2 / : pure-compute FLEX ops (ALU/MAC/load) carry no
  // unmodeled side effects. TableGen defaults pattern-less defs (Pattern =)
  // to hasSideEffects = 1, surfacing as MCID::UnmodeledSideEffects, which adds
  // spurious WAW/order edges to the SMS DDG and trips the body-scan reject in
  // HaydnInstrInfo::analyzeLoopForPipelining. The default-0 here is
  // truthful for the bulk of FLEX (pure compute). Genuinely-impure formats
  // (stores, branches, JAL/JALR, HWLOOP-set, CSR writes, WFI, AR/CB ops that
  // touch address-generation registers) override this to 1 in their class
  // bodies -- see hasSideEffects audit table.
  let hasSideEffects = 0;
  // NOTE: no fixed Size here -- slot sub-instructions are 48/40-bit (Size set
  // per slot below); only the Bundle128 composite (HaydnCompositeFormats.td
  // P4) is Size=16. Slot sub-instructions are never emitted standalone (they
  // are always composite operands in 128-bit-only Haydn).
  let DecoderNamespace = "HaydnFlex";
}

// The Bundle128 slot-window bases (HaydnSlotS0/S1/S2) are GONE with the five
// HaydnFormats<FU>.td files that derived from them. Format E's entry-window
// bases (HaydnEntryP20/P21/P30/P31/P32) are generated into
// HaydnFormatEEncoding.td alongside the members, because their widths and
// their `let Slot =` come from the same bit layout the members do.
//
// This file now holds only HaydnFormatInst, the base both encodings share.
//
// B2.1 multi-slot logicals (MultiSlot_Pseudo / materializableInto) live in
// HaydnMultiSlotPseudo.td (AIE2InstrFormats.td:21-31 peer). They reference
// HaydnFormatInst members defined after this file is included.
