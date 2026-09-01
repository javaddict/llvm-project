// D1.22 fail-closed fixture defs.
//
// D122GHOST64: crafted name that (a) matches INSTR_NAME_RE, (b) avoids
// MEMBER_RE (_E2_/_E3_) and SUFFIX_RE (_S0/1/2), and (c) has NO golden
// instruction_type_index row under its own name or SHELL_TO_GOLDEN. Its
// Slot012_ALU itinerary therefore has no golden Available row and the
// generator must fail closed naming the row.
//
// ADD64: real golden-backed row (Available ALU0|ALU1|ALU2) that also lives
// in RESIDUAL_OCCUPANCY_EXCEPTIONS, so the same run proves golden-backed
// rows still resolve while the ghost row raises.
let Itinerary = Slot012_ALU in {
  def D122GHOST64;
  def ADD64;
}
