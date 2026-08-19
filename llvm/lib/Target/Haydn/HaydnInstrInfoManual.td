//===----------------------------------------------------------------------===//
// Haydn Instruction Definitions — hand-maintained hypothesized encodings
//
// generate_format_e_records.py does not emit this file. The filename is
// historical; treat contents as hand-maintained, not generated output.
//
// Hypothesized Inst{} encodings (FmtALU64 / FmtMAC / FmtALU32Ext and hand
// Inst{} on HaydnInst stubs). Quarantined isCodeGenOnly=1 so they are
// un-encodable at MC: AsmMatcher and the decoder skip isCodeGenOnly records.
// CodeGen still selects these logicals; post-RA setDesc commits slot /
// Format E members. Do not invent replacement encodings. Do not activate
// Format E members from this file.
//
// R13: Format E catalog logicals in this file override to isCodeGenOnly=0
// with unused DecoderNamespace HaydnAutoNoDecode (FormatsLS LD32_S0 idiom:
// matcher yes, live decoder trie no). Non-catalog hypothesized names stay
// under the outer isCodeGenOnly=1 quarantine.
//
// PORT_FIRST: AIE MultiSlot_Pseudo is isPseudo + isCodeGenOnly without a
// live Inst{} that MC would emit as zeros (AIE2InstrFormats.td:26-29);
// mapping-only MOV_OR is isCodeGenOnly (AIE2GenInstrInfo.td:97). Hexagon
// mapping-only insns use isCodeGenOnly (HexagonPseudo.td:500-508).
//
// FmtALU64: 391 instructions, opcode=0x67, funct=0x000-0x186
// FmtMAC: 14 instructions, opcode=0x68, funct=0x00-0x0D
// FmtALU32: 23 instructions, opcode=0x80+
// Pseudo: 161 instructions (no matching format)
//===----------------------------------------------------------------------===//

let isCodeGenOnly = 1 in {
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULAA32RS_HHLL : FmtALU64Acc<0x67, 0x005,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulaa32rs.hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULAA32RS_HLLH : FmtALU64Acc<0x67, 0x006,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulaa32rs.hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULAA32R_HHLL : FmtALU64Acc<0x67, 0x007,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulaa32r.hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULAA32R_HLLH : FmtALU64Acc<0x67, 0x008,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulaa32r.hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULAS32RS_HHLL : FmtALU64<0x67, 0x009,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulas32rs.hhll	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULAS32RS_HLLH : FmtALU64<0x67, 0x00A,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulas32rs.hllh	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULAS32R_HHLL : FmtALU64<0x67, 0x00B,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulas32r.hhll	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULAS32R_HLLH : FmtALU64<0x67, 0x00C,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulas32r.hllh	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULSA32RS_HHLL : FmtALU64<0x67, 0x00D,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulsa32rs.hhll	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULSA32RS_HLLH : FmtALU64<0x67, 0x00E,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulsa32rs.hllh	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULSA32R_HHLL : FmtALU64<0x67, 0x00F,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulsa32r.hhll	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULSA32R_HLLH : FmtALU64<0x67, 0x010,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulsa32r.hllh	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULSS32RS_HHLL : FmtALU64Acc<0x67, 0x011,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulss32rs.hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULSS32RS_HLLH : FmtALU64Acc<0x67, 0x012,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulss32rs.hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULSS32R_HHLL : FmtALU64Acc<0x67, 0x013,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulss32r.hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def F2MULSS32R_HLLH : FmtALU64Acc<0x67, 0x014,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulss32r.hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FF2MULA32RS_HH : FmtALU64Acc<0x67, 0x02B,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2mula32rs.hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FF2MULA32RS_LH : FmtALU64Acc<0x67, 0x02C,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2mula32rs.lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FF2MULA32RS_LL : FmtALU64Acc<0x67, 0x02D,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2mula32rs.ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

// FF2MULA32R/FF2MULS32R/FF2MULS32RS — DB behavior for all nine is a
// read-modify-write of the accumulator: `rtd = rtd +/- rnd(product)`
// (slot-1 DR_Read_Port = [rsd1, rsd2, rtd]). They are tied-def accumulator
// MACs, modeled as FmtALU64Acc mirroring the saturating sibling
// FF2MULA32RS_* above. Previously (interim) these were FmtALU64
// (2-source) with a selectBinary path that dropped the accumulate operand
// a latent miscompute; -followup (this wave) reconciles the arity
// end-to-end: intrinsic->ternary, selector->selectAccMAC, Pat->tied-def.
// Encoding is byte-identical to the prior FmtALU64 form (FmtALU64Acc
// writes rd_in into the same Inst{21-18} field as rd; the tied constraint
// guarantees rd_in == rd). See.
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FF2MULA32R_HH : FmtALU64Acc<0x67, 0x02E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2mula32r.hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FF2MULA32R_LH : FmtALU64Acc<0x67, 0x02F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2mula32r.lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FF2MULA32R_LL : FmtALU64Acc<0x67, 0x030,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2mula32r.ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FF2MULS32RS_HH : FmtALU64Acc<0x67, 0x031,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2muls32rs.hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FF2MULS32RS_LH : FmtALU64Acc<0x67, 0x032,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2muls32rs.lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FF2MULS32RS_LL : FmtALU64Acc<0x67, 0x033,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2muls32rs.ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FF2MULS32R_HH : FmtALU64Acc<0x67, 0x034,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2muls32r.hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FF2MULS32R_LH : FmtALU64Acc<0x67, 0x035,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2muls32r.lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FF2MULS32R_LL : FmtALU64Acc<0x67, 0x036,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2muls32r.ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_HS00 : FmtALU64Acc<0x67, 0x04E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.hs00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_HS01 : FmtALU64Acc<0x67, 0x04F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.hs01	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_HS02 : FmtALU64Acc<0x67, 0x050,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.hs02	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_HS03 : FmtALU64Acc<0x67, 0x051,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.hs03	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_HS11 : FmtALU64Acc<0x67, 0x052,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.hs11	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_HS12 : FmtALU64Acc<0x67, 0x053,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.hs12	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_HS13 : FmtALU64Acc<0x67, 0x054,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.hs13	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_HS22 : FmtALU64Acc<0x67, 0x055,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.hs22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_HS23 : FmtALU64Acc<0x67, 0x056,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.hs23	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_HS33 : FmtALU64Acc<0x67, 0x057,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.hs33	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_LS00 : FmtALU64Acc<0x67, 0x058,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.ls00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_LS01 : FmtALU64Acc<0x67, 0x059,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.ls01	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_LS02 : FmtALU64Acc<0x67, 0x05A,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.ls02	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_LS03 : FmtALU64Acc<0x67, 0x05B,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.ls03	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_LS11 : FmtALU64Acc<0x67, 0x05C,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.ls11	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_LS12 : FmtALU64Acc<0x67, 0x05D,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.ls12	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_LS13 : FmtALU64Acc<0x67, 0x05E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.ls13	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_LS22 : FmtALU64Acc<0x67, 0x05F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.ls22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_LS23 : FmtALU64Acc<0x67, 0x060,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.ls23	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA16_LS33 : FmtALU64Acc<0x67, 0x061,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16.ls33	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA32S_HH : FmtALU64Acc<0x67, 0x062,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula32s.hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA32S_LH : FmtALU64Acc<0x67, 0x063,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula32s.lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULA32S_LL : FmtALU64Acc<0x67, 0x064,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula32s.ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULAA16_HS_11_00 : FmtALU64Acc<0x67, 0x065,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulaa16.hs.11.00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULAA16_HS_13_02 : FmtALU64<0x67, 0x066,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulaa16.hs.13.02	$rd, $rs1, $rs2", []>;

// Spec/DB: rtd[63:32] = SATQ1.31(rtd[63:32] + p3 + p2); DR_Read_Port
// includes rtd. Encoding is byte-identical to prior FmtALU64 form
// (FmtALU64Acc only adds a tied $rd_in MIR operand; wire layout unchanged).
// Migrated so GISel selectAccMAC / FIR_1 can preserve the accumulator.
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULAA16_HS_33_22 : FmtALU64Acc<0x67, 0x067,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulaa16.hs.33.22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULAA16_LS_11_00 : FmtALU64<0x67, 0x068,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulaa16.ls.11.00	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULAA16_LS_13_02 : FmtALU64<0x67, 0x069,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulaa16.ls.13.02	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULAA16_LS_33_22 : FmtALU64<0x67, 0x06A,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulaa16.ls.33.22	$rd, $rs1, $rs2", []>;

// FMULAA32S_HHLL/HLLH and FMULAS32S_HHLL/HLLH are accumulator-form dual-MACs:
// the DB (haydn_instruction_db.json:7272/7314/7356/7397) shows slot-1 reads
// rtd as an accumulator input (DR_Read_Port = [rsd1, rsd2, rtd], behavior
// strings all start with `rtd = SATQ1.63(rtd +...)` or `rtd -...`). They
// were originally declared FmtALU64 (2 src, no tied accumulator) which
// mismatches silicon — without the tied-def the regalloc is free to assign
// the accumulator and destination to different physical registers and silicon
// reads the wrong rtd (the silent-miscompute class). Migrated to
// FmtALU64Acc mirroring (FMULS32S_LH) and / (the tied-def
// contract every accumulator-form MAC needs for silicon-correct rtd reads).
// FMULZAA32S_*/FMULZAS32S_*/FMULZSA32S_*/FMULZSS32S_* (zero-acc, DB reads NO
// rtd: behavior `rtd = SATQ1.63(0 +...)`) correctly stay FmtALU64 below.
// Encoding-safe: FmtALU64Acc uses the identical bit layout as FmtALU64
// (rd_in duplicates rd in Inst{21-18}, no new encoding bits — see). The
// intrinsic/selector/Pat consumers are reconciled in lockstep (A1 full).
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULAA32S_HHLL : FmtALU64Acc<0x67, 0x06B,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulaa32s.hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULAA32S_HLLH : FmtALU64Acc<0x67, 0x06C,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulaa32s.hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULAS32S_HHLL : FmtALU64Acc<0x67, 0x06D,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulas32s.hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULAS32S_HLLH : FmtALU64Acc<0x67, 0x06E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulas32s.hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_HS00 : FmtALU64<0x67, 0x06F,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.hs00	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_HS01 : FmtALU64<0x67, 0x070,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.hs01	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_HS02 : FmtALU64<0x67, 0x071,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.hs02	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_HS03 : FmtALU64<0x67, 0x072,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.hs03	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_HS11 : FmtALU64<0x67, 0x073,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.hs11	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_HS12 : FmtALU64<0x67, 0x074,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.hs12	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_HS13 : FmtALU64<0x67, 0x075,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.hs13	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_HS22 : FmtALU64<0x67, 0x076,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.hs22	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_HS23 : FmtALU64<0x67, 0x077,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.hs23	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_HS33 : FmtALU64<0x67, 0x078,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.hs33	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_LS00 : FmtALU64<0x67, 0x079,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.ls00	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_LS01 : FmtALU64<0x67, 0x07A,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.ls01	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_LS02 : FmtALU64<0x67, 0x07B,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.ls02	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_LS03 : FmtALU64<0x67, 0x07C,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.ls03	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_LS11 : FmtALU64<0x67, 0x07D,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.ls11	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_LS12 : FmtALU64<0x67, 0x07E,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.ls12	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_LS13 : FmtALU64<0x67, 0x07F,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.ls13	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_LS22 : FmtALU64<0x67, 0x080,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.ls22	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_LS23 : FmtALU64<0x67, 0x081,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.ls23	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS16_LS33 : FmtALU64<0x67, 0x082,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmuls16.ls33	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS32S_HH : FmtALU64Acc<0x67, 0x083,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls32s.hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS32S_LH : FmtALU64Acc<0x67, 0x084,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls32s.lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULS32S_LL : FmtALU64Acc<0x67, 0x085,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls32s.ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

// FMULSA32S_HHLL/HLLH: accumulator-form (DB :8375/:8416 behavior
// `rtd = SATQ1.63(rtd -...)` reads rtd). Migrated FmtALU64 -> FmtALU64Acc
// alongside FMULAS32S_* above (A1 full, precedent). Consumers
// reconciled in IntrinsicsHaydn.td/selector/Pats in lockstep.
//
// spec correction (Rev 2): the inner-product sign convention for
// the *SA* (sub-add) family was inverted in the old manual. Correct behavior
// FMULSA32S_HHLL: rtd = SATQ1.63(rtd - (HH·HH) + (LL·LL)) [inner '-', then '+']
// FMULSA32S_HLLH: rtd = SATQ1.63(rtd - (HH·LL) + (LL·HH))
// (old DB had the two products with opposite signs). The encoding/operand
// shape is unchanged; only the documented behavior string was wrong. The
// *SS* family below was corrected the other way (inner '-', products '+').
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULSA32S_HHLL : FmtALU64Acc<0x67, 0x086,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulsa32s.hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULSA32S_HLLH : FmtALU64Acc<0x67, 0x087,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulsa32s.hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULSS16_HS_11_00 : FmtALU64Acc<0x67, 0x088,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulss16.hs.11.00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULSS16_HS_13_02 : FmtALU64<0x67, 0x089,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulss16.hs.13.02	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULSS16_HS_33_22 : FmtALU64<0x67, 0x08A,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulss16.hs.33.22	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULSS16_LS_11_00 : FmtALU64<0x67, 0x08B,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulss16.ls.11.00	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULSS16_LS_13_02 : FmtALU64<0x67, 0x08C,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulss16.ls.13.02	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULSS16_LS_33_22 : FmtALU64<0x67, 0x08D,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulss16.ls.33.22	$rd, $rs1, $rs2", []>;

// FMULSS32S_HHLL/HLLH — spec correction (Rev 2): the *SS*
// (sub-sub → combine '+') inner-product sign was inverted in the old manual.
// Correct behavior:
// FMULSS32S_HHLL: rtd = SATQ1.63(rtd - (HH·HH) - (LL·LL)) [inner '-', both '-']
// FMULSS32S_HLLH: rtd = SATQ1.63(rtd - (HH·LL) - (LL·HH))
// (old DB combined the two products with '+'). The accumulator-form question
// (DB reads rtd) is a SEPARATE pre-existing gap — these stay FmtALU64 here
// for encoding parity with the sibling FMULZ*32S zero-acc ops; the spec
// fix is documentation-only.
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULSS32S_HHLL : FmtALU64Acc<0x67, 0x08E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulss32s.hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def FMULSS32S_HLLH : FmtALU64Acc<0x67, 0x08F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulss32s.hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MUL16AQ : FmtALU64Acc<0x67, 0x0A6,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mul16aq	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_HH : FmtALU64Acc<0x67, 0x0B8,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_HL : FmtALU64Acc<0x67, 0x0B9,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.hl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_HUH : FmtALU64Acc<0x67, 0x0BA,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.huh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_HUL : FmtALU64Acc<0x67, 0x0BB,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.hul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_LH : FmtALU64Acc<0x67, 0x0BC,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_LL : FmtALU64Acc<0x67, 0x0BD,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_LUH : FmtALU64Acc<0x67, 0x0BE,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.luh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_LUL : FmtALU64Acc<0x67, 0x0BF,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.lul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_UHH : FmtALU64Acc<0x67, 0x0C0,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.uhh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_UHL : FmtALU64Acc<0x67, 0x0C1,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.uhl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_UHUH : FmtALU64Acc<0x67, 0x0C2,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.uhuh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_UHUL : FmtALU64Acc<0x67, 0x0C3,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.uhul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_ULH : FmtALU64Acc<0x67, 0x0C4,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.ulh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_ULL : FmtALU64Acc<0x67, 0x0C5,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.ull	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_ULUH : FmtALU64Acc<0x67, 0x0C6,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.uluh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULA64_ULUL : FmtALU64Acc<0x67, 0x0C7,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.ulul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAA32_HHLL : FmtALU64Acc<0x67, 0x0C8,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulaa32.hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAA32_HLLH : FmtALU64Acc<0x67, 0x0C9,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulaa32.hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS32_HHLL : FmtALU64Acc<0x67, 0x0CA,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas32.hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS32_HLLH : FmtALU64Acc<0x67, 0x0CB,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas32.hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_HH : FmtALU64Acc<0x67, 0x0CC,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_HL : FmtALU64Acc<0x67, 0x0CD,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.hl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_HUH : FmtALU64Acc<0x67, 0x0CE,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.huh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_HUL : FmtALU64Acc<0x67, 0x0CF,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.hul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_LH : FmtALU64Acc<0x67, 0x0D0,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_LL : FmtALU64Acc<0x67, 0x0D1,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_LUH : FmtALU64Acc<0x67, 0x0D2,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.luh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_LUL : FmtALU64Acc<0x67, 0x0D3,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.lul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_UHH : FmtALU64Acc<0x67, 0x0D4,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.uhh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_UHL : FmtALU64Acc<0x67, 0x0D5,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.uhl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_UHUH : FmtALU64Acc<0x67, 0x0D6,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.uhuh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_UHUL : FmtALU64Acc<0x67, 0x0D7,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.uhul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_ULH : FmtALU64Acc<0x67, 0x0D8,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.ulh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_ULL : FmtALU64Acc<0x67, 0x0D9,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.ull	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_ULUH : FmtALU64Acc<0x67, 0x0DA,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.uluh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULAS64_ULUL : FmtALU64Acc<0x67, 0x0DB,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64.ulul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_HH : FmtALU64Acc<0x67, 0x0DD,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_HL : FmtALU64Acc<0x67, 0x0DE,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.hl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_HUH : FmtALU64Acc<0x67, 0x0DF,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.huh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_HUL : FmtALU64Acc<0x67, 0x0E0,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.hul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_LH : FmtALU64Acc<0x67, 0x0E1,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_LL : FmtALU64Acc<0x67, 0x0E2,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_LUH : FmtALU64Acc<0x67, 0x0E3,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.luh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_LUL : FmtALU64Acc<0x67, 0x0E4,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.lul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_UHH : FmtALU64Acc<0x67, 0x0E5,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.uhh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_UHL : FmtALU64Acc<0x67, 0x0E6,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.uhl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_UHUH : FmtALU64Acc<0x67, 0x0E7,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.uhuh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_UHUL : FmtALU64Acc<0x67, 0x0E8,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.uhul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_ULH : FmtALU64Acc<0x67, 0x0E9,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.ulh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_ULL : FmtALU64Acc<0x67, 0x0EA,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.ull	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_ULUH : FmtALU64Acc<0x67, 0x0EB,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.uluh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULS64_ULUL : FmtALU64Acc<0x67, 0x0EC,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.ulul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSA32_HHLL : FmtALU64<0x67, 0x0ED,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mulsa32.hhll	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSA32_HLLH : FmtALU64<0x67, 0x0EE,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mulsa32.hllh	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS32_HHLL : FmtALU64<0x67, 0x0EF,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mulss32.hhll	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS32_HLLH : FmtALU64<0x67, 0x0F0,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mulss32.hllh	$rd, $rs1, $rs2", []>;

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_HH : FmtALU64Acc<0x67, 0x0F1,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_HL : FmtALU64Acc<0x67, 0x0F2,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.hl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_HUH : FmtALU64Acc<0x67, 0x0F3,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.huh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_HUL : FmtALU64Acc<0x67, 0x0F4,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.hul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_LH : FmtALU64Acc<0x67, 0x0F5,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_LL : FmtALU64Acc<0x67, 0x0F6,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_LUH : FmtALU64Acc<0x67, 0x0F7,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.luh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_LUL : FmtALU64Acc<0x67, 0x0F8,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.lul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_UHH : FmtALU64Acc<0x67, 0x0F9,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.uhh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_UHL : FmtALU64Acc<0x67, 0x0FA,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.uhl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_UHUH : FmtALU64Acc<0x67, 0x0FB,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.uhuh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_UHUL : FmtALU64Acc<0x67, 0x0FC,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.uhul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_ULH : FmtALU64Acc<0x67, 0x0FD,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.ulh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_ULL : FmtALU64Acc<0x67, 0x0FE,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.ull	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_ULUH : FmtALU64Acc<0x67, 0x0FF,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.uluh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MULSS64_ULUL : FmtALU64Acc<0x67, 0x100,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64.ulul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2FMULA32RS : FmtALU64Acc<0x67, 0x14F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2fmula32rs	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2FMULA32RSS : FmtALU64Acc<0x67, 0x150,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2fmula32rss	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2FMULA32TS : FmtALU64Acc<0x67, 0x151,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2fmula32ts	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2FMULS32RS : FmtALU64Acc<0x67, 0x152,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2fmuls32rs	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2FMULS32RSS : FmtALU64Acc<0x67, 0x153,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2fmuls32rss	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2FMULS32TS : FmtALU64Acc<0x67, 0x154,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2fmuls32ts	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2MULAPH32 : FmtALU64Acc<0x67, 0x159,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2mulaph32	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2MULAPL32 : FmtALU64Acc<0x67, 0x15A,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2mulapl32	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2MULSPH32 : FmtALU64Acc<0x67, 0x15D,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2mulsph32	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2MULSPL32 : FmtALU64Acc<0x67, 0x15E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2mulspl32	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X4CJMULA16S_H : FmtALU64Acc<0x67, 0x16E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x4cjmula16s.h	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X4CJMULA16S_L : FmtALU64Acc<0x67, 0x16F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x4cjmula16s.l	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }


let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X4CMULA16S_L : FmtALU64Acc<0x67, 0x174,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x4cmula16s.l	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X4FCMULA16RS : FmtALU64Acc<0x67, 0x178,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x4fcmula16rs	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X4FCMULA16RSS : FmtALU64Acc<0x67, 0x179,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x4fcmula16rss	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }
let Constraints = "$rd = $ra, $rtd2 = $ra2" in {
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2MULA32 : FmtMAC2Dest<0x68, 0x04,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x2mula32	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2MULS32 : FmtMAC2Dest<0x68, 0x05,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x2muls32	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}
}  // Constraints = "$rd = $ra, $rtd2 = $ra2"
let Constraints = "$rd = $ra, $rtd2 = $ra2" in {
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X4FF2MULA16S : FmtMAC2Dest<0x68, 0x07,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4ff2mula16s	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X4FF2MULS16S : FmtMAC2Dest<0x68, 0x08,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4ff2muls16s	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X4MULA16 : FmtMAC2Dest<0x68, 0x09,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4mula16	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X4MULA16S : FmtMAC2Dest<0x68, 0x0A,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4mula16s	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X4MULS16 : FmtMAC2Dest<0x68, 0x0B,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4muls16	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X4MULS16S : FmtMAC2Dest<0x68, 0x0C,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4muls16s	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}
}  // Constraints = "$rd = $ra, $rtd2 = $ra2"
//===----------------------------------------------------------------------===//
// FmtALU32 — Binary GPR32 (rd, rs1, rs2) — slot 0 (0b001)
// 23 instructions
//
// These DSP instructions share the Inst[31:24] byte space with FmtI
// instructions (e.g., ADDI32 opcode 0x20 and D_SDW_BREV_REG opcode 0x80
// both produce top byte 0x60). The disassembler cannot reliably distinguish
// them because FmtI uses a 6-bit opcode in [29:24] while FmtALU32 uses an
// 8-bit opcode in [29:22]. When the FmtI rd field has bits [3:2]=0b11
// (rd >= 12, e.g., SP/FP/LR), the bit pattern matches a FmtALU32 opcode
// with [1:0]=0b11, causing mis-decodes. Exclude from disassembler until
// the encoding space is properly partitioned (see ISA-02).
//===----------------------------------------------------------------------===//

let isAsmParserOnly = 1 in {






} // end isAsmParserOnly = 1

//===----------------------------------------------------------------------===//
// Real ISA instructions — not yet migrated to encoding format classes
//
// These are genuine hardware instructions that use HaydnInst (no format class).
// Previously wrapped in a blanket isPseudo=1 scope that suppressed emission.
//
// TSFlags encodes slot mask [2:0] and historical width bits [4:3]:
// TSFlags=9 = SLOT0 (0b001), width bits 0b01 — store variants
// TSFlags=11 = SLOT0|SLOT1 (0b011), width bits 0b01 — load variants
// TSFlags=14 = SLOT1|SLOT2 (0b110), width bits 0b01 — ALU64/SIMD/MAC
//
// Encoding migration assigns real opcode bits per Format E golden.
// Until then, instructions have no Inst encoding bits
// set. Without isPseudo=1, multiple instructions decode to 0x00000000
// causing "Decoding conflict encountered" build errors.
//
// These remain isPseudo=1 until encodings are assigned. GISel selector
// can still match them via Pat patterns in HaydnIntrinsics.td; the
// AsmPrinter simply skips emission until encoding is real.
//===----------------------------------------------------------------------===//

let isPseudo = 1 in {
//===---------------------------------------------------------------===//
// Fused post-increment / pre-increment / offset DR64 loads.
// DB (haydn_instruction_db.json) is authoritative: every D_L*_POST/PRE/WITH
// form has a DR64 data destination ($rtd), a base writeback for POST/PRE
// ($rs/$rs1 += stride via the AGU bypass), mayLoad, and the Slot0_LS
// Slot1 LD itinerary. The prior `let TSFlags = 11 in def... : HaydnInst<4
// (outs GPR32:$rd)>` stubs had the WRONG destination (GPR32 vs DB DR64), no
// mayLoad, no itinerary, and no two-def base writeback, so the selector could
// never produce them and the ISA-21/ISA-22 census saw zero `*_POST_*`
// mnemonics. See reviews/ldw-postinc-stub-vs-spec-divergence-.md
// and decisions/-ldw-postinc-fix-and-isa-retraction.md.
//
// The POST/PRE forms use the tied two-def idiom (Constraints = "$rs = $rs_wb")
// mirroring the gold-standard LD64_POST at HaydnInstrInfo.td:1167. The _REG
// POST/PRE forms name the base `$rs` (NOT `$rs1`) inside the Constraints
// block — review §8.1.2: an earlier attempt declared base as `$rs1` inside
// `Constraints = "$rs = $rs_wb"` and tablegen errored "does not have an
// operand named '$rs'". WITH_REG correctly keeps $rs1/$rs2 (no writeback).
//
// The canonical D_LDW_POST_IMM / D_LW_POST_IMM reuse the encoding slots
// 0x47 / 0x48 (LD64_POST / LD32_POST) so no existing object code breaks;
// they are isCodeGenOnly=1 (the selector / HaydnExpandPostIncEarly build them
// by opcode) and excluded from the decoder table to avoid an opcode conflict
// with the parallel LD64_POST/LD32_POST names — the disassembler's FU_LD
// special case (HaydnDisassembler.cpp) decodes 0x47/0x48 to these DB names.
//===---------------------------------------------------------------===//

// D_LDW_BREV_IMM/REG — DB: rtd = mem64[REVERSE32(rs)]; rs += imm6<<3.
// Same defect class (review §8.1.3): DR64 dest + tied base writeback.




// POST_IMM forms — DR64 dest, tied base writeback, imm6 element index.
// D_LDW_POST_IMM reuses slot 0x47 (LD64_POST); D_LW_POST_IMM reuses 0x48.
// FmtLSPostInc defaults to Slot1_LD; override to Slot0_LS to match the DB
// (slots 0 and 1, pipeline LS) per review §4.2. The outs data operand is named
// `$rt` (not `$rtd`) to bind to the FmtLSPostInc `bits<4> rt` encoding field
// the asm still renders `d_ldw_post_imm $rt,...`. Mirrors LD64_POST exactly.



// D_LDW_POST_REG carries residual tablegen `Inst` bits via FmtLSPostIncReg.
// Product encode is Format E LD register-offset members. No `isCodeGenOnly`
// (unlike D_LDW_POST_IMM which keeps it to avoid a decoder-table collision
// with its LD64_POST alias): D_LDW_POST_REG has no parallel alias name.
// The outs data operand is `$rt` (not `$rtd`)
// to bind to the FmtLSPostIncReg `bits<4> rt` encoding field — mirrors
// D_LDW_POST_IMM at line 2791.




//===---------------------------------------------------------------===//
// Store _IMM family (Shape B). The prior `let TSFlags = 9 in def...
// HaydnInst<4, (outs GPR32:$rd)>` stubs had the bogus (outs GPR32:$rd)
// NO store-data operand, no mayStore, and no itinerary (review §3b/§4b).
// Rewritten to the DB contract: store data is an INPUT (DR64:$rtd for D_S*)
// the only OUTPUT is the optional tied base writeback $rs_wb (POST/PRE) or
// empty (outs) (WITH), mayStore = 1, Slot0_LS, imm6 element-index stride.
// asm order per DB: base, data, imm. The canonical D_SDW_POST_IMM is the
// selector / HaydnExpandPostIncEarly target for fused DR64 post-inc stores.
//===---------------------------------------------------------------===//

// BREV store — DB: mem64[REVERSE32(rs)] = rtd; rs += imm6<<3.
// (followup, SILENT WRONG-CODE class): `let isPseudo = 0` is
// REQUIRED because this def sits inside the surrounding `let isPseudo = 1 in
// {... }` scope opened at line 2413. Without it, AsmPrinter drops the
// MCInst (MCID::Pseudo) and the store has NO effect — the exact defect
// fixed for D_LDW_POST_IMM / S_LW_POST_IMM. Mirrors D_LDW_BREV_IMM at line
// 2467 which correctly carries the override. Emission is real once
// non-pseudo.
// MOVEI_H / MOVEI_L — logical bases for Format E I32 members
// (MOVEI_*_E2_E0_ALU0_I32). Operand dag must match members
// (DR64 def + simm32_movei). FieldSlots retired (R13).
let TSFlags = 14, isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MOVEI_H : HaydnInst<4, (outs DR64:$rd),
    (ins simm32_movei:$imm32),
    "movei_h\t$rd, $imm32", []>;

let TSFlags = 14, isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def MOVEI_L : HaydnInst<4, (outs DR64:$rd),
    (ins simm32_movei:$imm32),
    "movei_l\t$rd, $imm32", []>;
//===----------------------------------------------------------------------===//
// Circular Buffer Load/Store (CBR — Circular Buffer Registers)
//
// These instructions use cbr_sel to select which CBR set controls the
// circular wrapping. There are 2 sets (cbr_sel = 0 or 1); the old
// CBR_SIZE register is removed (Rev 2) — the buffer size is
// derived as CBR_END[cbr_sel] - CBR_BEGIN[cbr_sel] + 1. The base pointer
// (GPR32) is updated in-place with wrap semantics.
//
// D_LDW_CB: 64-bit load with circular buffer wrapping
// D_SDW_CB: 64-bit store with circular buffer wrapping
//
// Pre-conditions (DB note fields + ISA ref §"Circular Buffer Load/Store"):
// 1. pointer must already lie in [CBR_BEGIN, CBR_END] before the access
// 2. |stride| <= buffer size (multiple wrapping is not handled)
// 3. CBR_BEGIN <= CBR_END
// 4. the selected CBR set must be configured first via CSRW to CSR
// 0x2C/0x2D (set 0 begin/end) or 0x2E/0x2F (set 1 begin/end) — see
// the SETCBR_BEGIN / SETCBR_END pseudos in HaydnInstrInfo.td.
//
// Note: The hardware uses GPR32 for the base pointer even though the data
// is DR64. The D_ prefix refers to the DR64 data operand.
// Selection is handled by custom C++ in HaydnInstructionSelector because
// these instructions have tied operands (base ptr read+write) and a cbr_sel
// immediate that cannot be expressed via simple Pat rules.
//===----------------------------------------------------------------------===//

} // end isPseudo = 1

//===----------------------------------------------------------------------===//
// Circular-buffer load/store encodings (4-operand CB ops extracted
// from the isPseudo block above). No existing FmtX class matches the 4
// operand shape (DR64:$rtd, GPR32:$rs, i32imm:$cbr_sel, simm16:$imm), so
// we use HaydnInst<4> with explicit Inst bits under isAsmParserOnly=1.
// Layout mirrors FmtLS but adds cbr_sel in Inst[3:2]:
// [31:30]=01 | [29:22] opcode(8) | [21:18] rtd | [17:14] rs
// [13:4] imm10 (low bits of stride) | [3:2] cbr_sel(2) | [1:0]=0b00
// Stride is simm16 but only low 10 bits fit; large strides get truncated
// at emission. The selector passes stride as a constant (imm8 per spec).
//
// Cursor model (supersedes): the CB instruction DOES compute the
// next pointer in hardware (rs = rs + stride, wrapped at CBR bounds) — but
// the compiler does NOT model that update. The stride is a tracked value
// (index-derived, supplied per call); the pointer is hardware-managed and
// persists across iterations of a hardware loop (the body is opaque to the
// optimizer, so the untracked update is correct). CBR is a boundary guard
// not a value the program reads back. Hence: data-only intrinsic, no tied
// def, no updated-ptr output. The only path to *convert* plain-C to CB is
// the Phase-C detection pass (modular arithmetic on an index-derived ptr).
//===----------------------------------------------------------------------===//

// Slot01_LD (not Slot0_LS): PlacementAlternative alts place D_LDW_CB_IMM in
// S0/S1/S2 and the post-RA HR already dual-issues independent CB loads.
// Slot0_LS made SMS DFA ResMII treat every CB load as exclusive S0 — bkfir
// ResMII 10 vs HiFi 8. Issue-width + dual MAC still bound the floor; this
// only fixes the DFA fallback / itinerary view to match alts-derived slots.
// AGU writeback of base (tied) — matches BundleSim and FormatsLS Flex peers.
// Prevents PostRA multi-issue of same-base CB ops (B6 WRITE_CONFLICT).
let isAsmParserOnly = 1, isPseudo = 0, mayLoad = 1 in
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def D_LDW_CB_IMM : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs_wb),
    (ins GPR32:$rs, i32imm:$cbr_sel, simm16:$imm),
    // Print order must match golden catalog / BundleSim frontend:
    // cbr_sel (IMM), rtd (DR), rs (GPR), imm (IMM). Wrong order makes
    // objdump emit "d_ldw_cb_immd0, 0, r1, 0" → catalog kind mismatch.
    "d_ldw_cb_imm\t$cbr_sel, $rtd, $rs, $imm", []> {
  let Constraints = "$rs = $rs_wb";
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot01_LD;
  bits<4> rtd;
  bits<4> rs;
  bits<16> imm;
  bits<32> cbr_sel;
  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = 0x99;
  let Inst{21 - 18} = rtd;
  let Inst{17 - 14} = rs;
  let Inst{13 - 4} = imm{9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
  let Inst{3, 2} = cbr_sel{1, 0};
  let Inst{1, 0} = 0;
}

// D_LDW_CB_REG — circular-buffer load with register stride. Same shape
// as D_LDW_CB_IMM but stride is GPR32 instead of imm. Layout mirrors
// FmtALU32 (R-type 3-operand) with cbr_sel encoded in Inst[3:2]:
// [31:30]=01 | [29:22] opcode(8)=0x9A | [21:18] rtd | [17:14] rs1
// [13:4] rs2(zero-extended in [13:10], zero in [9:4]) | [3:2] cbr_sel
// [1:0]=0b00
// Mirror D_LDW_CB_IMM: multi-slot load itinerary for dual CB-load ResMII (P3).
let isAsmParserOnly = 1, isPseudo = 0, mayLoad = 1 in
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def D_LDW_CB_REG : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs1_wb),
    (ins GPR32:$rs1, i32imm:$cbr_sel, GPR32:$rs2),
    "d_ldw_cb_reg\t$cbr_sel, $rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs1_wb";
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot01_LD;
  bits<4> rtd;
  bits<4> rs1;
  bits<4> rs2;
  bits<32> cbr_sel;
  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = 0x9A;
  let Inst{21 - 18} = rtd;
  let Inst{17 - 14} = rs1;
  let Inst{13 - 10} = rs2;
  let Inst{9 - 4} = 0;
  let Inst{3, 2} = cbr_sel{1, 0};
  let Inst{1, 0} = 0;
}

//===----------------------------------------------------------------------===//
// Circular-buffer STORE encodings (the SDW_CB counterparts of the
// LDW_CB encodings). The selector already builds D_SDW_CB_IMM/REG from
// the haydn_sdw_cb_imm/reg intrinsics (HaydnInstructionSelector.cpp:4375
// 4405), but the instructions were still pseudos inside the isPseudo block
// above (which closes at "} / end isPseudo = 1"). AsmPrinter::
// emitInstruction (HaydnAsmPrinter.cpp:125-126) silently drops MCID::Pseudo
// operands inside bundles, so the d_sdw_cb_imm/d_sdw_cb_reg mnemonics never
// reached assembly and `ar-linear-circular-combo.ll` was XFAIL'd.
//
// The store layout mirrors the load layout exactly (same opcode-space
// reservations, same Inst field positions, same cbr_sel encoding), with
// two differences:
// 1. mayStore = 1 (instead of mayLoad = 1).
// 2. rtd is a SOURCE operand (store data) — moved from (outs) to (ins).
// Operand order (must match FormatsLS Flex peers — setOpcode-only encode):
//   LDW: rtd, rs_wb, rs, cbr_sel, imm/rs2
//   SDW: rs_wb, rtd, rs, cbr_sel, imm/rs2
// Selector builds tied base writeback (B6).
//
// Opcodes 0x9B/0x9C are the next sequential allocation after 's
// 0x99/0x9A. The FmtALU32/FmtI opcode-space collision is accepted under
// isAsmParserOnly=1 (per the wave-2 ISA-02 note).
//===----------------------------------------------------------------------===//

let isAsmParserOnly = 1, isPseudo = 0, mayStore = 1 in
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def D_SDW_CB_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins DR64:$rtd, GPR32:$rs, i32imm:$cbr_sel, simm16:$imm),
    "d_sdw_cb_imm\t$cbr_sel, $rtd, $rs, $imm", []> {
  let Constraints = "$rs = $rs_wb";
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot0_LS;
  bits<4> rtd;
  bits<4> rs;
  bits<16> imm;
  bits<32> cbr_sel;
  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = 0x9B;
  let Inst{21 - 18} = rtd;
  let Inst{17 - 14} = rs;
  let Inst{13 - 4} = imm{9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
  let Inst{3, 2} = cbr_sel{1, 0};
  let Inst{1, 0} = 0;
}

// D_SDW_CB_REG — circular-buffer store with register stride. Same shape as
// D_SDW_CB_IMM but stride is GPR32. Layout mirrors D_LDW_CB_REG with the
// data operand flipped to a source.
let isAsmParserOnly = 1, isPseudo = 0, mayStore = 1 in
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def D_SDW_CB_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, i32imm:$cbr_sel, GPR32:$rs2),
    "d_sdw_cb_reg\t$cbr_sel, $rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs1_wb";
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot0_LS;
  bits<4> rtd;
  bits<4> rs1;
  bits<4> rs2;
  bits<32> cbr_sel;
  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = 0x9C;
  let Inst{21 - 18} = rtd;
  let Inst{17 - 14} = rs1;
  let Inst{13 - 10} = rs2;
  let Inst{9 - 4} = 0;
  let Inst{3, 2} = cbr_sel{1, 0};
  let Inst{1, 0} = 0;
}


// Total: 593 auto-instructions
// FmtALU64: 391
// FmtMAC: 14
// FmtALU32: 23
// HaydnInst (no format class): 167
} // isCodeGenOnly: hypothesized Auto encodings quarantined from MC

//===----------------------------------------------------------------------===//
// Multi-dest (2×DR64) logicals — hand-owned until the golden-logicals
// emitter learns dual accumulator ties (same class as X2MULA32).
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// FmtMAC — Ternary register (rd, rs1, rs2, ra). DB restricts this whole
// SIMD-MAC family to SLOT 1 ONLY (capstone-schedule-port-audit §(b)-1
// 15 instrs over-promised slot 2 under FmtMAC's default
// Slot12_ALU + slot-1/2 bits). Each def overrides Itinerary + TSFlags{2-0} =
// 0b010 so two of these cannot pack into one bundle.
//
// Itinerary split (D2XX): ACCUMULATOR-form SIMD-MACs (X*MULA*/X*MULS*) use
// Slot1_MAC -> OperandCycles [2,1,1,2] so the 1-cycle acc feedback path
// and 2-cycle regfile writeback are both modeled. MULTIPLY-form SIMD-MACs
// (X*CMUL*, X4FF2MUL16S) keep Slot1_ALU (latency 1, no acc read). X4SEL16
// is a lane permutation, not a MAC -> Slot1_ALU.
//===----------------------------------------------------------------------===//

let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2CMUL32 : FmtMAC2Dest<0x68, 0x00,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x2cmul32	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}




let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2CMUL32S : FmtMAC2Dest<0x68, 0x01,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x2cmul32s	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}




let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2CMUL32S_F2 : FmtMAC2Dest<0x68, 0x02,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x2cmul32s.f2	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}




let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X2CMUL32_F2 : FmtMAC2Dest<0x68, 0x03,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x2cmul32.f2	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot1_ALU;
  let TSFlags{2-0} = 0b010;
}

// (Path B): TRUE 2-output accumulate (golden DR_Write_Port=[rtd1,rtd2]
// DR_Read_Port=[rtd1,rtd2,rsd1,rsd2]). The legacy dag binds 2 dests + 2 acc
// inputs + 2 srcs (6 operands) with tied-def Constraints so MI.setDesc(legacy
// > _S<k>) preserves the operand count (6) and def count (2). The 2nd
// dest ($rtd2) and 2nd accum ($ra2) are unencoded passthroughs here (FmtMAC
// has no rtd2/ra2 fields) — the slot encoder handles the real encoding after
// setDesc. The Constraints force regalloc to coalesce acc1->rtd1, acc2->rtd2.



// X4FF2MUL16S — (Path B): TRUE 2-output NON-accum (golden DR_Write_Port
// =[rtd1,rtd2], DR_Read_Port=[rsd1,rsd2]). 2 dests + 2 srcs (4 operands).
let isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def X4FF2MUL16S : FmtMAC2Dest<0x68, 0x06,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x4ff2mul16s	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Slot12_MAC;
  let TSFlags{2-0} = 0b010;
}

// ZERO_DR — hand-owned: golden I8 member has no dest field; the value-dest
// law cannot see the implicit DR64 def the GISel ZERO_DR pseudo expects.

// ZERO_DR — isCodeGenOnly logical base for S1/S2 I8_ZERODR members.
// Operand dag must match members (DR64 def, no imm) so LogicalMaterialize
// can setDesc. Golden ZERO_DR has no immediate; the simm16 stub was a
// hypothesized Auto encoding and blocked ISel from using the logical.
let TSFlags = 14, isPseudo = 0, isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode" in
def ZERO_DR : HaydnInst<4, (outs DR64:$rd),
    (ins),
    "zero_dr\t$rd", []>;

// MOVESFR2GPR / MOVEGPR2SFR / ZERO_SFR moved to HaydnInstrInfo.td (
// real 32-bit R-type encodings under opcode 0x67 funct 0x198/0x199/0x19A
// with explicit Inst{...}=... assignments for the unusual operand shapes).

// SIN_COS / ARCTAN — hand-owned: Slot12_ALU_SinCosLat is a conservative
// dest Data_Latency-17 law (golden uimm4+2 max, T-SM2) the port-derived
// itinerary cannot see.


// SIN_COS — DB syntax `SIN_COS rtd, rs, uimm4` (DR64 dest + GPR32 src
// DB slots DR_Write=['rtd'] GPR_Read=['rs']). Stub previously modeled dest
// as GPR32 (capstone drift cat 2). Fixed to DR64 dest. Inert: no selector
// case, no intrinsic Pat. Stays isCodeGenOnly until a real encoding (M5).
// T-SM2: conservative dest Data_Latency 17 (golden uimm4+2 max) so pre-RA
// and LatencyStalls see the read window; members cover the same OperandCycles.
let TSFlags = 14, isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode", Itinerary = Slot12_ALU_SinCosLat in
def SIN_COS : HaydnInst<4, (outs DR64:$rd),
    (ins GPR32:$rs, simm16:$imm),
    "sin_cos	$rd, $rs, $imm", []>;





// MC crash-shield (capstone-mc-encoding-audit H1): isPseudo=1
// alone does NOT exclude these from the asm matcher, so `llvm-mc` accepted
// them and the emitter then crashed (LLVM ERROR: Unsupported instruction).
// isCodeGenOnly=1 excludes them from the matcher (and decoder) while keeping
// them selectable via Pat patterns. Drop isCodeGenOnly once real encoding
// bits land (M5). Same pattern as SEXT32T64 (line ~2885) /.
// ABS64 / ABS64S moved to HaydnInstrInfo.td (real FmtALU64Unary 0x67/0x1A7
// 0x1A8). The HaydnInst<4> isPseudo stubs here were dropped by AsmPrinter
// (EMPTY_SEL: intrin_sat64). Post-RA leaveRegion setDesc commits base →
// ABS64*_S1/_S2 members; encode prints Desc as-is.

// ARCTAN — DB syntax `ARCTAN rt, rsd, uimm4` (GPR32 dest + DR64 source
// DB slots DR_Read=['rsd'] GPR_Write=['rt']). Stub previously modeled source
// as GPR32 (capstone drift cat 1). Fixed to DR64 source. Inert: no selector
// case, no intrinsic Pat. Stays isCodeGenOnly until a real encoding (M5).
// T-SM2: conservative dest Data_Latency 17 (golden uimm4+2 max) so pre-RA
// and LatencyStalls see the read window; members cover the same OperandCycles.
let TSFlags = 14, isCodeGenOnly = 0, DecoderNamespace = "HaydnAutoNoDecode", Itinerary = Slot12_ALU_SinCosLat in
def ARCTAN : HaydnInst<4, (outs GPR32:$rd),
    (ins DR64:$rs, simm16:$imm),
    "arctan	$rd, $rs, $imm", []>;

// BREV/CB load + BREV imm-store encodings (extracted from the
// blanket isPseudo=1 block above). Encoded as FmtALU32 (RR-form, slot
// 012) and FmtI (RI-form) under isAsmParserOnly=1, mirroring wave 2's
// SDW_BREV_REG/S_SW_BREV_REG treatment. The disassembler conflict with
// FmtI (ISA-02 note above) is accepted: assembly + AsmPrinter emission
// work, disassembly is excluded until the encoding space is partitioned.
// `let isPseudo = 0` is required because these defs are still inside
// the surrounding `let isPseudo = 1 in { … }` scope (HaydnInstrInfoManual.td:2205).

