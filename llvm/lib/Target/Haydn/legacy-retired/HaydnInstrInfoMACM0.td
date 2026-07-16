//===----------------------------------------------------------------------===//
//
// Mode-0 Slot 1 MAC accumulate decode-included defs (spike).
//
// Mirrors the funct-routed ALU64 decode-included pattern
// (HaydnInstrInfoM0.td MAX64_M0S1 / MIN64_M0S1 + HaydnInstrInfo.td
// FMULA16_LS00_M0S1 ACC spike). User corrections (NON-NEGOTIABLE):
// 1. Decode migrates to tablegen too (NOT encode-only). The generic
// X2MULA32/X2MULS32 4-operand model (rd, rs1, rs2, ra) is preserved
// here as (rtd, rd_in, rsd1, rsd2) with rd_in unencoded + tied.
// 2. Accumulate MAC is legal in BOTH s1 and s2. NO getLegalSlots
// restriction is added. This spike handles s1 (Mode 0); s2 (Mode 3
// wide-s2) is a separate later slice and stays hand-rolled until then.
// 3. MAC latency (1c fast-path vs 2c) is scheduling/scoreboard, NOT
// encode/decode — out of scope here.
//
// Mechanism (identical to MAX64_M0S1):
// FmtM0_S1_MAC carries the Inst s1 MAC layout (FU=10, rsd1@43:40
// rsd2@39:36, rtd@35:32, opcode@31:24) — byte-equal to the hand-rolled
// packInstructionIntoSlot PS_S1 FU_MAC path.
// isCodeGenOnly is 0 (DEFAULT — do NOT set =1, or the decoder table
// won't generate); Pattern =.
// DecoderNamespace = "HaydnM0S1MAC" isolates these defs from the DEFAULT
// asm-match namespace. Asm parsing of "x2mula32 d0,d1,d2,d0" still
// matches the generic FmtMAC def (which lives in the default namespace);
// the _M0S1 defs only DECODE the encoded s1 MAC window. This is the same
// isolation that lets MAX64_M0S1 / FMULA16_LS00_M0S1 avoid hijacking
// `max64` / `fmula16_ls00` asm parse.
// The def NAME carries the _M0S1 suffix to keep the enum entry distinct.
// The asm string uses the BARE generic mnemonic so objdump output matches
// the generic FmtMAC instruction (decode-included, asm-string-neutral).
//
// ACC accumulate operand model (validated for ALU64Acc by prior revision
// mirror of FMULA16_LS00_M0S1 in HaydnInstrInfo.td:325-337):
// (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in)
// (ins order MUST be rsd1, rsd2, rd_in — NOT rd_in first — so blind setDesc
// from generic [rd,rs1,rs2,ra] lands rsd1<-rs1, rsd2<-rs2, rd_in<-ra
// matching the hand-roller FU_MAC pack (rtd<-op0, rsd1<-op1, rsd2<-op2).)
// bits<4> rd_in; declared, NOT assigned (tied to rtd).
// let Constraints = "$rtd = $rd_in";
// asm: "x2mula32\t$rtd, $rsd1, $rsd2" (rd_in not printed — equals rtd).
// The generic X2MULA32 4-operand model (rd, rs1, rs2, ra) maps DIRECTLY:
// generic operand 0 (rd) -> $rtd
// generic operand 1 (rs1) -> $rsd1 (mul source 1, packed to Inst{43-40})
// generic operand 2 (rs2) -> $rsd2 (mul source 2, packed to Inst{39-36})
// generic operand 3 (ra) -> $rd_in (accumulator, unencoded, tied to rtd)
// So the finalizer rewriteToM0 is a blind setDesc — 4 ops = 4 ops, same order.
//
// Spike scope: 2 ops only (X2MULA32, X2MULS32). Bulk MAC migration follows.
//===----------------------------------------------------------------------===//

def X2MULA32_M0S1 : FmtM0_S1_MAC<0x21,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x2mula32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;   // declared, not assigned (tied to rtd)
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

def X2MULS32_M0S1 : FmtM0_S1_MAC<0x22,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x2muls32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;   // declared, not assigned (tied to rtd)
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

//===----------------------------------------------------------------------===//
// BULK: remaining s1 (Mode 0) MAC accumulate decode-included defs.
//
// 6 ACC ops — same operand model + mechanism as the X2MULA32/X2MULS32 spike
// above (rd_in unencoded + tied). These are the accumulating MAC family:
// the destination IS the accumulator (rd += product per
// haydn_instruction_db.json Behavior for X4MULA16/X4MULA16S/X4MULS16
// X4MULS16S/X4FF2MULA16S/X4FF2MULS16S — all show `rtd = rtd OP product`).
//
// s1 MAC opcodes from getEncOpcodeMap (HaydnDClassOpcodes.cpp):
// X4MULA16 0x61, X4MULS16 0x62, X4MULA16S 0x63, X4MULS16S 0x64
// X4FF2MULA16S 0x6B, X4FF2MULS16S 0x6C.
//
// DEFERRED (NOT migrated — ISA/encoding-architecture gap, see
// "DEFERRED-NOT-MIGRATED audit "):
// X2CMUL32/X2CMUL32S/X2CMUL32_F2/X2CMUL32S_F2 (0x40-0x43)
// X4FF2MUL16S (0x6A): DB syntax `rtd1, rtd2, rsd1, rsd2` — these are
// TWO-DESTINATION instructions. The generic td misrepresents the 2nd
// destination `rtd2` as an input `ra`; the s1 MAC layout has only ONE
// destination field (rtd@35:32), so neither ACC (rd_in=rtd) nor a
// non-accum 4-op model is correct. Needs a new FmtM0_S1_MAC2D format.
// X4SEL16 (0x0F): DB syntax `rtd, rsd1, rsd2, rs` — single dest + a
// GPR32 lane SELECTOR; FmtM0_S1_MAC has only DR64 fields. Needs its own
// format class with a GPR32 selector field.
// All 6 stay on the hand-rolled FU_MAC decode fallback until the encoding
// spec defines a real 2-dest / GPR32-selector MAC format. The hand-rolled
// path itself has a latent bug here (encoder drops op3; decoder fabricates
// op3=op0) — not worsened by leaving these deferred.
//
// X2MULA32/X2MULS32 are NOT duplicated (spike defs above).
//===----------------------------------------------------------------------===//

// X4MULA16: rtd += rsd1[63:48]*rsd2[63:48] (4-lane Q1.15 accumulate).
def X4MULA16_M0S1 : FmtM0_S1_MAC<0x61,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4mula16\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4MULS16: rtd -= rsd1[63:48]*rsd2[63:48] (4-lane Q1.15 subtract-accumulate).
def X4MULS16_M0S1 : FmtM0_S1_MAC<0x62,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4muls16\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4MULA16S: saturating variant of X4MULA16.
def X4MULA16S_M0S1 : FmtM0_S1_MAC<0x63,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4mula16s\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4MULS16S: saturating variant of X4MULS16.
def X4MULS16S_M0S1 : FmtM0_S1_MAC<0x64,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4muls16s\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4FF2MULA16S: fixed-point 4-lane Q1.15 accumulate with saturation.
def X4FF2MULA16S_M0S1 : FmtM0_S1_MAC<0x6B,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4ff2mula16s\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4FF2MULS16S: fixed-point 4-lane Q1.15 subtract-accumulate with saturation.
def X4FF2MULS16S_M0S1 : FmtM0_S1_MAC<0x6C,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4ff2muls16s\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

//===----------------------------------------------------------------------===//
// PAIR-WRITE: 5 two-destination MAC ops (X2CMUL32 family + X4FF2MUL16S).
//
// ISA-65 Resolution : pair-write — rtd2 = rtd+1 implicitly via the
// WP0+WP1 hardware write path. NO encoding change to FmtM0_S1_MAC (still
// FU(2)=10 | rsd1(4) | rsd2(4) | rtd(4) | opcode(8)); the second destination
// is IMPLICIT (rtd+1, modulo 16). The generic.td models the spec syntax
// `rtd1, rtd2, rsd1, rsd2` as (rd, rs1, rs2, ra) where `ra` represents rtd2.
//
// Operand model (3-op VARIANTS — encode/decode-only):
// (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2)
// NO Constraints tie (NON-accum — these are product ops, not rd += product).
// FmtM0_S1_MAC only has rsd1@43:40, rsd2@39:36, rtd@35:32 — there is NO
// field for ra. The generic X2CMUL32 keeps its 4-op model (ra=rtd2 for
// CodeGen/regalloc); these _M0S1 VARIANTS use the 3 encoded fields only.
// The finalizer's blind setDesc from the generic 4-op MI → this 3-op variant
// is safe: getBinaryCodeForInstr reads op0/op1/op2 per the variant's fields
// and ignores the extra generic op3.
// asm: "x2cmul32\t$rtd, $rsd1, $rsd2" — 3 operands (matches the variant's
// ins list); pair-write dest rtd+1 NOT modeled in the MC layer.
//
// Decode: the tablegen decoder produces 3-op MCInsts directly (no ra
// reconstruction — pair-write dest is implicit, not in the encoded fields).
//
// Constraint C2d (encoding_manual.md §13.2, ISA-65): regalloc must ensure
// rtd+1 is available; rtd=15 wraps to rtd2=0 (may overwrite soft-zero r0).
// Even-aligned rtd preferred (not modeled here — regalloc/scheduling concern).
//
// Opcodes (HaydnDClassOpcodes.cpp): X2CMUL32 0x40, X2CMUL32_F2 0x41
// X2CMUL32S 0x42, X2CMUL32S_F2 0x43, X4FF2MUL16S 0x6A.
//
// X4SEL16 (the 6th deferred op) is NOT migrated here — its 4th operand is a
// GPR32 lane selector (not a DR64 pair-write dest), and FmtM0_S1_MAC only
// carries DR64 fields. X4SEL16 stays on the hand-rolled FU_MAC decode fallback
// until a dedicated format with a GPR32 selector field exists.
//===----------------------------------------------------------------------===//

// X2CMUL32: complex multiply, pair-write (rtd=Re, rtd+1=Im).
def X2CMUL32_M0S1 : FmtM0_S1_MAC<0x40,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2CMUL32_F2: complex multiply (fractional variant 2), pair-write.
def X2CMUL32_F2_M0S1 : FmtM0_S1_MAC<0x41,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32_f2\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2CMUL32S: saturating complex multiply, pair-write.
def X2CMUL32S_M0S1 : FmtM0_S1_MAC<0x42,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32s\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2CMUL32S_F2: saturating complex multiply (fractional variant 2), pair-write.
def X2CMUL32S_F2_M0S1 : FmtM0_S1_MAC<0x43,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32s_f2\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4FF2MUL16S: fixed-point 4-lane Q1.15 multiply, pair-write (2 dests).
def X4FF2MUL16S_M0S1 : FmtM0_S1_MAC<0x6A,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4ff2mul16s\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

//===----------------------------------------------------------------------===//
// BULK: remaining FU_MAC S1 ops migrated to tablegen.
//
// Covers every MAC op whose operand model fits one of the three shipped
// patterns (RRR 3-op / ACC rd_in-tied / pair-write 3-op). Sources:
// opcodes: HaydnDClassOpcodes.cpp getEncOpcodeMap EM_64BitM0/FU_MAC/S1
// Behavior: haydn_instruction_db.json (truth for accumulate vs product)
// generic operand order: HaydnInstrInfoAuto.td (FmtMAC vs FmtALU64
// FmtALU64Acc)
//
// Three operand-model classes (setDesc safety preserved per-class):
//
// (P) PAIR-WRITE 3-op — DB Behavior writes `rtd1` AND `rtd2` (2 dests).
// Generic.td is FmtMAC 4-op (rd, rs1, rs2, ra) where ra represents rtd2.
// Variant ins (rsd1, rsd2) — op3 (ra/rtd2) is implicit per ISA-65
// (pair-write WP0+WP1, rtd2 = rtd+1). Blind setDesc from generic 4-op
// to variant 3-op is safe: getBinaryCodeForInstr reads variant op0/op1
// op2, ignores generic op3. NO Constraint tie (NON-accum — product op).
// (Mirror of the X2CMUL32 family pattern.)
//
// (A) ACC rd_in-FIRST 4-op — DB Behavior `rtd = rtd +...` (single-dest
// accumulate, rtd in/out). Generic.td is FmtALU64Acc 4-op with ins
// order (rd_in, rs1, rs2) — rd_in FIRST. Variant ins (rd_in, rsd1, rsd2)
// preserves the order so blind setDesc from generic [rd, rd_in, rs1, rs2]
// lands rd_in<-op1, rsd1<-op2, rsd2<-op3. Constraint "$rtd = $rd_in".
// bits<4> rd_in declared-not-assigned (tied to rtd). asm 3-op (rd_in not
// printed — equals rtd). (Distinct from the FmtMAC ACC pattern in
// spike whose generic order is (rd, rs1, rs2, ra) with ra LAST — the
// variant ins order (rsd1, rsd2, rd_in) was setDesc-safe THERE; for these
// FmtALU64Acc generics rd_in MUST come first to match the generic order.)
//
// (R) RRR 3-op — DB Behavior `rtd =...` (single-dest product, no accumulate).
// Generic.td is FmtALU64 3-op (rd, rs1, rs2). Variant ins (rsd1, rsd2).
// NO Constraint tie. asm 3-op. Includes X4ADD16/X4SUB16 (lane-wise add
// sub on DR64, scheduled via FU_MAC per the encoding table).
//
// DEFERRED (NOT migrated here — operand model does not fit FmtM0_S1_MAC):
// X4SEL16 0x0F: 4th operand is a GPR32 lane selector (not DR64). Needs a
// dedicated format with a GPR32 field.
// X4CMUL16 0x48, X4CMUL16S 0x49, X4CMUL16_F2 0x4A, X4CMUL16S_F2 0x4B
// X4ENERGY16 0x66: DB syntax `rtd, rsd` (TWO-input). Generic.td is
// 2-op (rd, rs). The s1 MAC layout has rsd1/rsd2/rtd fields; the single
// source `rsd` cannot be represented by a 3-op variant without an
// encoder-side fold (rsd1=rsd2=rsd). Deferred until a 2-in MAC format
// or an encoder fold exists.
// All 6 stay on the hand-rolled FU_MAC decode fallback.
//===----------------------------------------------------------------------===//

//=== (P) PAIR-WRITE 3-op =====================================================//

// X2MUL32: dual 32x32->32 product, pair-write (rtd1=hi*hi, rtd2=lo*lo).
// NON-accumulating (DB Behavior `rtd1 = rsd1[63:32]*rsd2[63:32]` — NOT +=).
def X2MUL32_M0S1 : FmtM0_S1_MAC<0x20,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mul32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// Same op as X2MUL32_M0S1 but in the 26-bit s1 dense-MAC format (NO $rtd=$rs1
// Constraint — resolves the destructive-tie that blocked the cutover).
// class=001 (X2), variant=00000, sub_hi=00 (no sub-encoding for basic X2 ops).

// The accumulate tie ($rtd=$rd_in) is the standard two-address pattern — the
// generic is already two-address (TwoAddressRewriter marked it), so post-RA
// setDesc preserves the tie flag → verifier passes. NOT the destructive tie.


//=== (A) ACC rd_in-FIRST 4-op (FmtALU64Acc generic order) ====================//
//
// ins order is (rd_in, rsd1, rsd2) — rd_in FIRST — to match the generic
// FmtALU64Acc ins (rd, rd_in, rs1, rs2). Blind setDesc lands:
// generic op1 rd_in -> variant rd_in
// generic op2 rs1 -> variant rsd1
// generic op3 rs2 -> variant rsd2

// X2MULAPL32: accumulate pair-low product (rtd[31:00] += lo*lo, rtd[63:32] += hi*hi).
def X2MULAPL32_M0S1 : FmtM0_S1_MAC<0x2E,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2mulapl32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2MULSPL32: subtract-accumulate pair-low product.
def X2MULSPL32_M0S1 : FmtM0_S1_MAC<0x2F,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2mulspl32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2MULAPH32: accumulate pair-high product.
def X2MULAPH32_M0S1 : FmtM0_S1_MAC<0x31,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2mulaph32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2MULSPH32: subtract-accumulate pair-high product.
def X2MULSPH32_M0S1 : FmtM0_S1_MAC<0x32,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2mulsph32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2FMULA32RS: fractional mul accumulate, rounding.
def X2FMULA32RS_M0S1 : FmtM0_S1_MAC<0x26,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fmula32rs\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2FMULA32RSS: fractional mul accumulate, symmetric rounding.
def X2FMULA32RSS_M0S1 : FmtM0_S1_MAC<0x27,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fmula32rss\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2FMULA32TS: fractional mul accumulate, truncation.
def X2FMULA32TS_M0S1 : FmtM0_S1_MAC<0x2B,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fmula32ts\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2FCMULA32RS: fractional complex mul accumulate, rounding.
def X2FCMULA32RS_M0S1 : FmtM0_S1_MAC<0x46,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fcmula32rs\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2FCMULA32RSS: fractional complex mul accumulate, symmetric rounding.
def X2FCMULA32RSS_M0S1 : FmtM0_S1_MAC<0x47,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fcmula32rss\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4FCMULA16RS: 4-lane fractional complex mul accumulate, rounding.
def X4FCMULA16RS_M0S1 : FmtM0_S1_MAC<0x4E,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4fcmula16rs\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4FCMULA16RSS: 4-lane fractional complex mul accumulate, symmetric rounding.
def X4FCMULA16RSS_M0S1 : FmtM0_S1_MAC<0x4F,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4fcmula16rss\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4CMULA16S_H: 4-lane complex mul accumulate, high half, saturating.
def X4CMULA16S_H_M0S1 : FmtM0_S1_MAC<0x52,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4cmula16s_h\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4CMULA16S_L: 4-lane complex mul accumulate, low half, saturating.
def X4CMULA16S_L_M0S1 : FmtM0_S1_MAC<0x53,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4cmula16s_l\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4CJMULA16S_H: 4-lane conjugate complex mul accumulate, high half, sat.
def X4CJMULA16S_H_M0S1 : FmtM0_S1_MAC<0x56,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4cjmula16s_h\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4CJMULA16S_L: 4-lane conjugate complex mul accumulate, low half, sat.
def X4CJMULA16S_L_M0S1 : FmtM0_S1_MAC<0x57,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4cjmula16s_l\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

//=== (R) RRR 3-op (FmtALU64 generic 3-op) ===================================//
//
// ins order (rsd1, rsd2) matches generic (rs1, rs2). Blind setDesc 3-op = 3-op.

// X4ADD16: lane-wise 4x16 add (FU_MAC-routed per encoding table, not a mul).
def X4ADD16_M0S1 : FmtM0_S1_MAC<0x00,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4add16\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4SUB16: lane-wise 4x16 sub (FU_MAC-routed).
def X4SUB16_M0S1 : FmtM0_S1_MAC<0x01,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4sub16\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2DOT32: dual 32x32 dot product (rtd = hi*hi + lo*lo).
def X2DOT32_M0S1 : FmtM0_S1_MAC<0x23,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2dot32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2MULPL32: pair-low product (rtd[63:32]=hi*lo, rtd[31:00]=lo*lo of low halves).
def X2MULPL32_M0S1 : FmtM0_S1_MAC<0x2D,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulpl32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2MULPH32: pair-high product.
def X2MULPH32_M0S1 : FmtM0_S1_MAC<0x30,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulph32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2FMUL32RS: fractional mul, rounding.
def X2FMUL32RS_M0S1 : FmtM0_S1_MAC<0x24,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2FMUL32RSS: fractional mul, symmetric rounding.
def X2FMUL32RSS_M0S1 : FmtM0_S1_MAC<0x25,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2FMUL32TS: fractional mul, truncation.
def X2FMUL32TS_M0S1 : FmtM0_S1_MAC<0x2A,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32ts\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2FMULS32RS: signed fractional mul, rounding.
def X2FMULS32RS_M0S1 : FmtM0_S1_MAC<0x28,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2FMULS32RSS: signed fractional mul, symmetric rounding.
def X2FMULS32RSS_M0S1 : FmtM0_S1_MAC<0x29,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2FMULS32TS: signed fractional mul, truncation.
def X2FMULS32TS_M0S1 : FmtM0_S1_MAC<0x2C,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32ts\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2FCMUL32RS: fractional complex mul, rounding.
def X2FCMUL32RS_M0S1 : FmtM0_S1_MAC<0x44,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fcmul32rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X2FCMUL32RSS: fractional complex mul, symmetric rounding.
def X2FCMUL32RSS_M0S1 : FmtM0_S1_MAC<0x45,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fcmul32rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4CMUL16S_H: 4-lane complex mul, high half, saturating.
def X4CMUL16S_H_M0S1 : FmtM0_S1_MAC<0x50,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmul16s_h\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4CMUL16S_L: 4-lane complex mul, low half, saturating.
def X4CMUL16S_L_M0S1 : FmtM0_S1_MAC<0x51,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmul16s_l\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4CJMUL16S_H: 4-lane conjugate complex mul, high half, saturating.
def X4CJMUL16S_H_M0S1 : FmtM0_S1_MAC<0x54,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cjmul16s_h\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4CJMUL16S_L: 4-lane conjugate complex mul, low half, saturating.
def X4CJMUL16S_L_M0S1 : FmtM0_S1_MAC<0x55,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cjmul16s_l\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4FCMUL16RS: 4-lane fractional complex mul, rounding.
def X4FCMUL16RS_M0S1 : FmtM0_S1_MAC<0x4C,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fcmul16rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4FCMUL16RSS: 4-lane fractional complex mul, symmetric rounding.
def X4FCMUL16RSS_M0S1 : FmtM0_S1_MAC<0x4D,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fcmul16rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4FMUL16TS: 4-lane fractional mul, truncation.
def X4FMUL16TS_M0S1 : FmtM0_S1_MAC<0x67,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16ts\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4FMUL16RS: 4-lane fractional mul, rounding.
def X4FMUL16RS_M0S1 : FmtM0_S1_MAC<0x68,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4FMUL16RSS: 4-lane fractional mul, symmetric rounding.
def X4FMUL16RSS_M0S1 : FmtM0_S1_MAC<0x69,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4MUL16: 4-lane 16x16->16 product (rtd = lane-wise product).
// DB Behavior `rtd = rsd1 * rsd2` (NON-accumulating — single-dest product).
// Generic.td (HaydnInstrInfo.td:1259) is FmtALU64 3-op (rd, rs1, rs2);
// variant 3-op (rtd, rsd1, rsd2) — blind setDesc, same count + order.
// Was the confirmed miss in the subsequent FU_MAC hand-roller retirement
// audit (the bulk-46 migration claimed it but did not add the def).
def X4MUL16_M0S1 : FmtM0_S1_MAC<0x60,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4mul16\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4DOT16: 4-lane dot product (rtd = sum of 4 lane products).
def X4DOT16_M0S1 : FmtM0_S1_MAC<0x65,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4dot16\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

//===----------------------------------------------------------------------===//
// TWO-INPUT MAC: 5 single-source ops migrated via dual-field-bind.
//
// DB syntax `rtd, rsd` (ONE DR64 source). slot1_mac_opcode_table.md L614
// confirms: "Single-source (X4CMUL16 variants, X4ENERGY16)". The s1 MAC wire
// format FU(2)+rsd1(4)+rsd2(4)+rtd(4)+opcode(8) (encoding_manual.md §6.6
// has separate rsd1/rsd2 fields; for a 1-source op the architectural
// source `rsd` populates BOTH (silicon reads both as MAC sources).
//
// Mechanism: FmtM0_S1_MAC_1SR binds one `bits<4> rsd` operand to BOTH
// Inst{43-40} (rsd1) and Inst{39-36} (rsd2). This is the dual-field-bind
// idiom (avoids input-input `Constraints` ties, which tablegen rejects).
// Byte-equal to what the hand-rolled encoder SHOULD emit (it currently has a
// latent drop-rsd2 bug — HaydnMCCodeEmitter.cpp FU_MAC L677 `Rsd2 =... : 0`;
// these ops were never reachable from CodeGen so the bug never fired).
//
// Finalizer rewrite: blind setDesc, 2-op generic (rd, rs) -> 2-op variant
// (rtd, rsd), same operand count + order. NO operand rebuild.
//
// X4SEL16 (the 7th deferred op) is NOT migrated here — it is an ALU64 op per
// DB (not MAC), its GPR32 lane selector has no defined encoding field, and it
// has no CodeGen path. See + ISA-66. REMOVED X4SEL16 from the
// FU_MAC routing (getEncOpcodeMap now returns makeEncInvalid for it) and
// RETIRED the hand-rolled FU_MAC decode fallback entirely; X4SEL16 is now
// asm-only with no encodable wire format (the manual's §6.6.2 funct table
// does not list it).
//===----------------------------------------------------------------------===//

// X4CMUL16: complex 16x16 mul, single 64-bit source (4 packed 16-bit lanes).
def X4CMUL16_M0S1 : FmtM0_S1_MAC_1SR<0x48,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16\t$rtd, $rsd", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4CMUL16S: complex 16x16 mul, saturating.
def X4CMUL16S_M0S1 : FmtM0_S1_MAC_1SR<0x49,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16s\t$rtd, $rsd", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4CMUL16_F2: complex 16x16 mul, Format-2 result layout.
def X4CMUL16_F2_M0S1 : FmtM0_S1_MAC_1SR<0x4A,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16_f2\t$rtd, $rsd", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4CMUL16S_F2: complex 16x16 mul, Format-2 + saturating.
def X4CMUL16S_F2_M0S1 : FmtM0_S1_MAC_1SR<0x4B,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16s_f2\t$rtd, $rsd", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}

// X4ENERGY16: sum-of-squares of 4 packed 16-bit lanes (low 32b = sum, high 32b = 0).
def X4ENERGY16_M0S1 : FmtM0_S1_MAC_1SR<0x66,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4energy16\t$rtd, $rsd", []> {
  let DecoderNamespace = "HaydnM0S1MAC";
  let isPseudo = 0;
}
