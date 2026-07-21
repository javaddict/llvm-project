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

let Namespace = "Haydn" in {
  def s0_slot : InstSlot<"S0", 48> { let FieldToFind = "s0"; }
  def s1_slot : InstSlot<"S1", 40> { let FieldToFind = "s1"; }
  def s2_slot : InstSlot<"S2", 40> { let FieldToFind = "s2"; }
}

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

// Slot-window sub-instruction bases. A per-FU format (P1b/P2) inherits the
// slot base matching its issue slot and packs FU(3b MSB) + opcode + operands
// into the <slot> field. These are sub-instructions: the Bundle128 composite
// (HaydnCompositeFormats.td, P4) holds them as MCOperand::isInst operands.
// Phase 3 (I4): AsmStrings are slot-free (no `.s0`/`.s1`/`.s2`). All three
// slot classes therefore share identical bare mnemonics for multi-slot ops
// (e.g. "add32\t$rt, $rs1, $rs2"). Matcher strategy deliberately does NOT set
// isCodeGenOnly on S1/S2: that bit also drops defs from the decoder trie in
// this tree (see notes on ADDI32_W), which would break Bundle128 decode.
// Ambiguous matchables are accepted (tblgen may warn); AsmParser assigns slot
// from source order via setHaydnSlot, and standalone first-match wins (S0).
// InstPrinter prints the same bare AsmString for every slot; bundle position
// is the sole slot authority for encode.
class HaydnSlotS0<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnFormatInst<outs, ins, asmstr, pattern> {
  let Slot = s0_slot;
  let DecoderNamespace = "S0";
  let Size = 6;  // 48-bit slot window
  bits<48> s0;
  bits<48> Inst = s0;
}

class HaydnSlotS1<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnFormatInst<outs, ins, asmstr, pattern> {
  let Slot = s1_slot;
  let DecoderNamespace = "S1";
  let Size = 5;  // 40-bit slot window
  bits<40> s1;
  bits<40> Inst = s1;
}

class HaydnSlotS2<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnFormatInst<outs, ins, asmstr, pattern> {
  let Slot = s2_slot;
  let DecoderNamespace = "S2";
  let Size = 5;  // 40-bit slot window
  bits<40> s2;
  bits<40> Inst = s2;
}

// The per-FU slot-format classes + per-op defs live in HaydnFormats<FU>.td
// (HaydnFormatsALU32.td, HaydnFormatsALU64.td, HaydnFormatsLS.td
// HaydnFormatsLD.td, HaydnFormatsMAC.td) — fresh per-slot ports from the spec
// (encoding_manual_flex.md + golden). This file holds only the
// slot infrastructure (InstSlots + HaydnFormatInst + slot-window bases).
