//===----------------------------------------------------------------------===//
//
// Mode-3 Slot 2 MAC accumulate decode-included defs (spike).
//
// Mode 3 (bits[3:0]=1111, "wide-s2") is the bundle format that lets an
// accumulating MAC execute in slot 2. Mode 0 s2 is only 18b destructive
// (2 reg fields) and CANNOT represent the accumulator form (rtd += rsd1*rsd2);
// Mode 3 widens s2 to 22b with the full RRR layout (rsd1, rsd2, rtd).
//
// User correction (NON-NEGOTIABLE, §1): "accumulate MAC is s1|s2".
// getLegalSlots already returns LSB_S1|LSB_S2 for FU_MAC; the encoding-side
// blocker (Mode 0 s2 has no room for 3 DR fields) is what Mode 3 fixes.
// This slice is DECODE ONLY — no emit (Slice B), no packetizer change (Slice C).
//
// Mechanism (mirrors HaydnInstrInfoMACM0.td _M0S1 spike):
// FmtM3_S2_MAC0 carries the Inst s2 MAC0 layout for Mode 3 (FU=01
// rsd1@61:58, rsd2@57:54, rtd@53:50, opcode@49:42, pattern 1111).
// isCodeGenOnly is 0 (DEFAULT — do NOT set =1, or the decoder table
// won't generate); Pattern =.
// DecoderNamespace = "HaydnM3S2MAC" isolates these defs from the DEFAULT
// asm-match namespace (the generic X2MULA32 still parses `x2mula32`);
// the _M3S2 defs only DECODE the encoded Mode 3 s2 MAC window.
// The def NAME carries the _M3S2 suffix to keep the enum entry distinct.
// The asm string uses the BARE generic mnemonic so objdump output matches
// the generic FmtMAC instruction (asm-string-neutral, decode-included).
//
// ACC accumulate operand model (validated for ALU64Acc by prior revision
// mirror of FMULA16_LS00_M0S1 + the X2MULA32_M0S1 spike):
// (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in)
// (ins order MUST be rsd1, rsd2, rd_in — NOT rd_in first — so blind setDesc
// from generic [rd,rs1,rs2,ra] lands rsd1<-rs1, rsd2<-rs2, rd_in<-ra.)
// bits<4> rd_in; declared, NOT assigned (tied to rtd).
// let Constraints = "$rtd = $rd_in";
// asm: "x2mula32\t$rtd, $rsd1, $rsd2" (rd_in not printed — equals rtd).
//
// s2 MAC opcode space (open assumption, coordinator-verifiable):
// encoding_manual.md §6.6 defines s1 MAC as 8-bit class-partitioned
// (357 ops). §6.7 defines Mode 0 s2 MAC as 9-bit destructive. §8.1 defines
// Mode 3 s2 MAC as 8-bit opcode (FU=01 MAC0 / FU=10 MAC1) but does NOT
// publish a separate Mode 3 s2 MAC opcode table. Most reasonable reading:
// Mode 3 s2 MAC REUSES the s1 MAC 8-bit opcode values (same 357 ops, same
// encodings) — Mode 3 was designed to give s2 full RRR for the SAME MAC
// instructions s1 already executes. The X2MULA32_M3S2 opcode below
// (0x21) is byte-equal to X2MULA32_M0S1 / the s1 entry in
// HaydnDClassOpcodes.cpp:276. If the spec later defines a distinct s2 MAC
// opcode table, only the opcode values in this file change.
//
// Spike scope: 2 ops only (X2MULA32, X2MULS32). Bulk MAC + MAC1 (FU=10)
// migration follows the same bulk pattern once the spike is green.
//===----------------------------------------------------------------------===//

def X2MULA32_M3S2 : FmtM3_S2_MAC0<0x21,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x2mula32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;   // declared, not assigned (tied to rtd)
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2MULS32_M3S2 : FmtM3_S2_MAC0<0x22,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x2muls32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

//===----------------------------------------------------------------------===//
// BULK extension (bulk, mirroring bulk): 6 ACC X4MULA16-family
// ops extended to Mode 3 s2. Same shape as the X2MULA32_M3S2 / X2MULS32_M3S2
// spike above — FmtM3_S2_MAC0, opcode byte-equal to the _M0S1 versions
// (HaydnInstrInfoMACM0.td L103), ACC model verbatim
// DecoderNamespace "HaydnM3S2MAC" (auto-included by the existing
// DecoderTableHaydnM3S2MAC64 from Slice A). Decode/emit/packetizer wiring
// unchanged — only new defs + the getM3S2Variant map (HaydnMCCodeEmitter.cpp).
//
// The 6 BLOCKED ops (X2CMUL32 family 2-dest + X4SEL16, ISA-65 gap) are
// INTENTIONALLY NOT generated here — they cannot encode in FmtM3_S2_MAC0
// (single-ACC-dest model).
//===----------------------------------------------------------------------===//

// X4MULA16: rtd += rsd1[63:48]*rsd2[63:48] (4-lane Q1.15 accumulate).
def X4MULA16_M3S2 : FmtM3_S2_MAC0<0x61,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4mula16\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

// X4MULS16: rtd -= rsd1[63:48]*rsd2[63:48] (4-lane Q1.15 subtract-accumulate).
def X4MULS16_M3S2 : FmtM3_S2_MAC0<0x62,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4muls16\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

// X4MULA16S: saturating variant of X4MULA16.
def X4MULA16S_M3S2 : FmtM3_S2_MAC0<0x63,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4mula16s\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

// X4MULS16S: saturating variant of X4MULS16.
def X4MULS16S_M3S2 : FmtM3_S2_MAC0<0x64,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4muls16s\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

// X4FF2MULA16S: fixed-point 4-lane Q1.15 accumulate with saturation.
def X4FF2MULA16S_M3S2 : FmtM3_S2_MAC0<0x6B,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4ff2mula16s\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

// X4FF2MULS16S: fixed-point 4-lane Q1.15 subtract-accumulate with saturation.
def X4FF2MULS16S_M3S2 : FmtM3_S2_MAC0<0x6C,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4ff2muls16s\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

//===----------------------------------------------------------------------===//
// PAIR-WRITE (Mode 3 s2): 5 two-destination MAC ops mirrored to s2.
//
// ISA-65 Resolution: pair-write rtd2=rtd+1 (NO encoding change). 3-op variant;
// pair-write dest rtd+1 implicit per ISA-65 (not modeled in the MC encode/decode
// layer — FmtM3_S2_MAC0 has only rsd1@61:58, rsd2@57:54, rtd@53:50). The
// generic X2CMUL32 keeps its 4-op model (ra=rtd2 for CodeGen/regalloc); these
// _M3S2 VARIANTS are encode/decode-only and use the 3 encoded fields
// (rtd, rsd1, rsd2). The finalizer's blind setDesc from the generic 4-op MI →
// this 3-op variant is safe: getBinaryCodeForInstr reads op0/op1/op2 per the
// variant's fields and ignores the extra generic op3.
// NO Constraints tie (NON-accum — these are product ops, not rd += product).
// Opcodes byte-equal to the _M0S1 versions (HaydnDClassOpcodes.cpp).
//===----------------------------------------------------------------------===//

// X2CMUL32: complex multiply, pair-write (rtd=Re, rtd+1=Im).
def X2CMUL32_M3S2 : FmtM3_S2_MAC0<0x40,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

// X2CMUL32_F2: complex multiply (fractional variant 2), pair-write.
def X2CMUL32_F2_M3S2 : FmtM3_S2_MAC0<0x41,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32_f2\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

// X2CMUL32S: saturating complex multiply, pair-write.
def X2CMUL32S_M3S2 : FmtM3_S2_MAC0<0x42,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32s\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

// X2CMUL32S_F2: saturating complex multiply (fractional variant 2), pair-write.
def X2CMUL32S_F2_M3S2 : FmtM3_S2_MAC0<0x43,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32s_f2\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

// X4FF2MUL16S: fixed-point 4-lane Q1.15 multiply, pair-write (2 dests).
def X4FF2MUL16S_M3S2 : FmtM3_S2_MAC0<0x6A,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4ff2mul16s\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

//===----------------------------------------------------------------------===//
// BULK: remaining FU_MAC ops mirrored to Mode 3 s2.
//
// Same shape + opcodes as the _M0S1 defs (HaydnInstrInfoMACM0.td).
// Opcodes byte-equal to the s1 entries (encoding_manual.md §8.1 — Mode 3 s2
// MAC reuses the s1 8-bit opcode space; open assumption, still holds).
// Three operand-model classes (setDesc safety preserved per-class — see the
// _M0S1 file's comment for the full rationale):
// (P) PAIR-WRITE 3-op — non-accum, rtd+1 implicit (ISA-65).
// (A) ACC rd_in-FIRST 4-op — FmtALU64Acc generic order (rd, rd_in, rs1, rs2);
// variant ins (rd_in, rsd1, rsd2) preserves order; Constraint tie.
// (R) RRR 3-op — FmtALU64 generic (rd, rs1, rs2); variant ins (rsd1, rsd2).
// Same 6 DEFERRED ops as (X4SEL16 GPR32 selector + 5 two-input MAC +
// X4ENERGY16) — not migrated here.
//===----------------------------------------------------------------------===//

//=== (P) PAIR-WRITE 3-op =====================================================//

def X2MUL32_M3S2 : FmtM3_S2_MAC0<0x20,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mul32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

//=== (A) ACC rd_in-FIRST 4-op ================================================//

def X2MULAPL32_M3S2 : FmtM3_S2_MAC0<0x2E,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2mulapl32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2MULSPL32_M3S2 : FmtM3_S2_MAC0<0x2F,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2mulspl32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2MULAPH32_M3S2 : FmtM3_S2_MAC0<0x31,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2mulaph32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2MULSPH32_M3S2 : FmtM3_S2_MAC0<0x32,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2mulsph32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2FMULA32RS_M3S2 : FmtM3_S2_MAC0<0x26,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fmula32rs\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2FMULA32RSS_M3S2 : FmtM3_S2_MAC0<0x27,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fmula32rss\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2FMULA32TS_M3S2 : FmtM3_S2_MAC0<0x2B,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fmula32ts\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2FCMULA32RS_M3S2 : FmtM3_S2_MAC0<0x46,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fcmula32rs\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2FCMULA32RSS_M3S2 : FmtM3_S2_MAC0<0x47,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fcmula32rss\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4FCMULA16RS_M3S2 : FmtM3_S2_MAC0<0x4E,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4fcmula16rs\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4FCMULA16RSS_M3S2 : FmtM3_S2_MAC0<0x4F,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4fcmula16rss\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4CMULA16S_H_M3S2 : FmtM3_S2_MAC0<0x52,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4cmula16s_h\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4CMULA16S_L_M3S2 : FmtM3_S2_MAC0<0x53,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4cmula16s_l\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4CJMULA16S_H_M3S2 : FmtM3_S2_MAC0<0x56,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4cjmula16s_h\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4CJMULA16S_L_M3S2 : FmtM3_S2_MAC0<0x57,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4cjmula16s_l\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

//=== (R) RRR 3-op ===========================================================//

def X4ADD16_M3S2 : FmtM3_S2_MAC0<0x00,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4add16\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4SUB16_M3S2 : FmtM3_S2_MAC0<0x01,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4sub16\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2DOT32_M3S2 : FmtM3_S2_MAC0<0x23,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2dot32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2MULPL32_M3S2 : FmtM3_S2_MAC0<0x2D,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulpl32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2MULPH32_M3S2 : FmtM3_S2_MAC0<0x30,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulph32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2FMUL32RS_M3S2 : FmtM3_S2_MAC0<0x24,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2FMUL32RSS_M3S2 : FmtM3_S2_MAC0<0x25,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2FMUL32TS_M3S2 : FmtM3_S2_MAC0<0x2A,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32ts\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2FMULS32RS_M3S2 : FmtM3_S2_MAC0<0x28,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2FMULS32RSS_M3S2 : FmtM3_S2_MAC0<0x29,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2FMULS32TS_M3S2 : FmtM3_S2_MAC0<0x2C,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32ts\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2FCMUL32RS_M3S2 : FmtM3_S2_MAC0<0x44,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fcmul32rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X2FCMUL32RSS_M3S2 : FmtM3_S2_MAC0<0x45,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fcmul32rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4CMUL16S_H_M3S2 : FmtM3_S2_MAC0<0x50,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmul16s_h\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4CMUL16S_L_M3S2 : FmtM3_S2_MAC0<0x51,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmul16s_l\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4CJMUL16S_H_M3S2 : FmtM3_S2_MAC0<0x54,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cjmul16s_h\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4CJMUL16S_L_M3S2 : FmtM3_S2_MAC0<0x55,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cjmul16s_l\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4FCMUL16RS_M3S2 : FmtM3_S2_MAC0<0x4C,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fcmul16rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4FCMUL16RSS_M3S2 : FmtM3_S2_MAC0<0x4D,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fcmul16rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4FMUL16TS_M3S2 : FmtM3_S2_MAC0<0x67,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16ts\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4FMUL16RS_M3S2 : FmtM3_S2_MAC0<0x68,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4FMUL16RSS_M3S2 : FmtM3_S2_MAC0<0x69,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4MUL16_M3S2 : FmtM3_S2_MAC0<0x60,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4mul16\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

def X4DOT16_M3S2 : FmtM3_S2_MAC0<0x65,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4dot16\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

//===----------------------------------------------------------------------===//
// TWO-INPUT MAC (Mode 3 s2 mirror): 5 single-source ops via dual-field-bind.
//
// Mirror of the _M0S1 defs (HaydnInstrInfoMACM0.td). FmtM3_S2_MAC0_1SR
// binds one `bits<4> rsd` operand to BOTH Inst{61-58} (rsd1) and Inst{57-54}
// (rsd2). Opcodes byte-equal to the s1 versions (encoding_manual.md §8.1
// Mode 3 s2 MAC reuses the s1 8-bit opcode space; open assumption, still
// holds). Finalizer rewrite is blind setDesc (2-op = 2-op).
//
// X4SEL16 stays deferred (+ ISA-66 — ALU64 op, GPR32 selector gap).
//===----------------------------------------------------------------------===//

// X4CMUL16: complex 16x16 mul, single 64-bit source.
def X4CMUL16_M3S2 : FmtM3_S2_MAC0_1SR<0x48,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16\t$rtd, $rsd", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

// X4CMUL16S: complex 16x16 mul, saturating.
def X4CMUL16S_M3S2 : FmtM3_S2_MAC0_1SR<0x49,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16s\t$rtd, $rsd", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

// X4CMUL16_F2: complex 16x16 mul, Format-2 result layout.
def X4CMUL16_F2_M3S2 : FmtM3_S2_MAC0_1SR<0x4A,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16_f2\t$rtd, $rsd", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

// X4CMUL16S_F2: complex 16x16 mul, Format-2 + saturating.
def X4CMUL16S_F2_M3S2 : FmtM3_S2_MAC0_1SR<0x4B,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16s_f2\t$rtd, $rsd", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}

// X4ENERGY16: sum-of-squares of 4 packed 16-bit lanes.
def X4ENERGY16_M3S2 : FmtM3_S2_MAC0_1SR<0x66,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4energy16\t$rtd, $rsd", []> {
  let DecoderNamespace = "HaydnM3S2MAC";
  let isPseudo = 0;
}
