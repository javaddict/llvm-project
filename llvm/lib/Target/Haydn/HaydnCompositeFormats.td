//===-- HaydnCompositeFormats.td - Bundle128 composite ( P4) -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: license information.
//
// P4 — the 128-bit Bundle128 composite (packet) format. Mirrors AIE's
// AIE2CompositeFormats.td: a composite (isComposite=true) instruction whose
// operands are the 3 positional slots (s0_slot/s1_slot/s2_slot); each slot
// operand is a sub-instruction (MCOperand::isInst at encode time) whose encoded
// bits fill the slot window. The encoder composes the bundle by OR-ing each
// sub-instruction's encoding at its window offset; the decoder extracts each
// window and re-enters the per-FU sub-trie.
//
// Layout (encoding_manual_flex.md §2): bundle[127:0] = {s2[39:0]
// s1[39:0], s0[47:0]} (s2 HIGH, s0 LOW). 40+40+48 = 128. No format-tag bits
// (§4: NOP = all-zero slot window; the FU field discriminates).
//
// P4-minimal: this defines the composite format so -gen-instr-format emits a
// composite MCFormatDesc (isComposite=true). enabled the
// GET_FORMATS_PACKETS_TABLE consumer include (HaydnMCFormats.cpp):
// getPacketFormats/getIsFormatAvailable now return the generated tables
// and the backend's computeSlotSets derives real ConflictBits (= self-only for
// every slot, since Bundle128 co-emits all three). The hand-authored
// HaydnFormats/HaydnPacketFormats/HaydnFormatAvailable (HaydnFormat.cpp)
// are retained as dormant backup.
//===----------------------------------------------------------------------===//

// Bundle128 composite base: 3 positional slot windows concatenated s2||s1||s0.
// isComposite=true tells -gen-instr-format to read the slot set from the
// operand dag (the InstSlot defs), not from a Slot field.
class HaydnBundle128Composite<dag slot_ins>
    : HaydnFormatInst<(outs), slot_ins, "$s0; $s1; $s2", []> {
  let isComposite = true;
  let Size = 16;                         // 128-bit parcel
  let DecoderNamespace = "Bundle128";
  bits<48> s0;                           // slot 0 window [47:0]
  bits<40> s1;                           // slot 1 window [87:48]
  bits<40> s2;                           // slot 2 window [127:88]
  bits<128> Inst = {s2, s1, s0};         // MSB-first: s2 HIGH, s0 LOW
}

// Full 3-slot Bundle128 (every slot occupied). The packetizer places per-FU
// sub-instructions into the slots; unused slots carry the all-zero NOP encoding.
def BUNDLE128_FULL : HaydnBundle128Composite<
    (ins s0_slot:$s0, s1_slot:$s1, s2_slot:$s2)>;
