//===----------------------------------------------------------------------===//
// Haydn Instruction Definitions - Auto-generated from spec JSON
// Binary encodings are hypothesized (per CLAUDE.md section 2.9 / M0).
//
// FmtALU64: 391 instructions, opcode=0x67, funct=0x000-0x186
// FmtMAC: 14 instructions, opcode=0x68, funct=0x00-0x0D
// FmtALU32: 23 instructions, opcode=0x80+
// Pseudo: 161 instructions (no matching format)
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// FmtALU64 — Binary register (rd, rs1, rs2) — slots 1+2 (0b110)
// 391 instructions
//===----------------------------------------------------------------------===//

def ADD64S : FmtALU64<0x67, 0x000,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "add64s	$rd, $rs1, $rs2", []>;

def ADD64S_H : FmtALU64<0x67, 0x001,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "add64s_h	$rd, $rs1, $rs2", []>;

def ADD64S_L : FmtALU64<0x67, 0x002,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "add64s_l	$rd, $rs1, $rs2", []>;

def ADD64_H : FmtALU64<0x67, 0x003,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "add64_h	$rd, $rs1, $rs2", []>;

def ADD64_L : FmtALU64<0x67, 0x004,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "add64_l	$rd, $rs1, $rs2", []>;

def F2MULAA32RS_HHLL : FmtALU64Acc<0x67, 0x005,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulaa32rs_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULAA32RS_HLLH : FmtALU64Acc<0x67, 0x006,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulaa32rs_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULAA32R_HHLL : FmtALU64Acc<0x67, 0x007,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulaa32r_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULAA32R_HLLH : FmtALU64Acc<0x67, 0x008,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulaa32r_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULAS32RS_HHLL : FmtALU64Acc<0x67, 0x009,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulas32rs_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULAS32RS_HLLH : FmtALU64Acc<0x67, 0x00A,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulas32rs_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULAS32R_HHLL : FmtALU64Acc<0x67, 0x00B,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulas32r_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULAS32R_HLLH : FmtALU64Acc<0x67, 0x00C,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulas32r_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULSA32RS_HHLL : FmtALU64Acc<0x67, 0x00D,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulsa32rs_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULSA32RS_HLLH : FmtALU64Acc<0x67, 0x00E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulsa32rs_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULSA32R_HHLL : FmtALU64Acc<0x67, 0x00F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulsa32r_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULSA32R_HLLH : FmtALU64Acc<0x67, 0x010,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulsa32r_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULSS32RS_HHLL : FmtALU64Acc<0x67, 0x011,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulss32rs_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULSS32RS_HLLH : FmtALU64Acc<0x67, 0x012,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulss32rs_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULSS32R_HHLL : FmtALU64Acc<0x67, 0x013,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulss32r_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULSS32R_HLLH : FmtALU64Acc<0x67, 0x014,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "f2mulss32r_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def F2MULZAA32RS_HHLL : FmtALU64<0x67, 0x015,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzaa32rs_hhll	$rd, $rs1, $rs2", []>;

def F2MULZAA32RS_HLLH : FmtALU64<0x67, 0x016,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzaa32rs_hllh	$rd, $rs1, $rs2", []>;

def F2MULZAA32R_HHLL : FmtALU64<0x67, 0x017,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzaa32r_hhll	$rd, $rs1, $rs2", []>;

def F2MULZAA32R_HLLH : FmtALU64<0x67, 0x018,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzaa32r_hllh	$rd, $rs1, $rs2", []>;

def F2MULZAS32RS_HHLL : FmtALU64<0x67, 0x019,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzas32rs_hhll	$rd, $rs1, $rs2", []>;

def F2MULZAS32RS_HLLH : FmtALU64<0x67, 0x01A,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzas32rs_hllh	$rd, $rs1, $rs2", []>;

def F2MULZAS32R_HHLL : FmtALU64<0x67, 0x01B,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzas32r_hhll	$rd, $rs1, $rs2", []>;

def F2MULZAS32R_HLLH : FmtALU64<0x67, 0x01C,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzas32r_hllh	$rd, $rs1, $rs2", []>;

def F2MULZSA32RS_HHLL : FmtALU64<0x67, 0x01D,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzsa32rs_hhll	$rd, $rs1, $rs2", []>;

def F2MULZSA32RS_HLLH : FmtALU64<0x67, 0x01E,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzsa32rs_hllh	$rd, $rs1, $rs2", []>;

def F2MULZSA32R_HHLL : FmtALU64<0x67, 0x01F,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzsa32r_hhll	$rd, $rs1, $rs2", []>;

def F2MULZSA32R_HLLH : FmtALU64<0x67, 0x020,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzsa32r_hllh	$rd, $rs1, $rs2", []>;

def F2MULZSS32RS_HHLL : FmtALU64<0x67, 0x021,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzss32rs_hhll	$rd, $rs1, $rs2", []>;

def F2MULZSS32RS_HLLH : FmtALU64<0x67, 0x022,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzss32rs_hllh	$rd, $rs1, $rs2", []>;

def F2MULZSS32R_HHLL : FmtALU64<0x67, 0x023,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzss32r_hhll	$rd, $rs1, $rs2", []>;

def F2MULZSS32R_HLLH : FmtALU64<0x67, 0x024,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "f2mulzss32r_hllh	$rd, $rs1, $rs2", []>;

def FF2MUL32RS_HH : FmtALU64<0x67, 0x025,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "ff2mul32rs_hh	$rd, $rs1, $rs2", []>;

def FF2MUL32RS_LH : FmtALU64<0x67, 0x026,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "ff2mul32rs_lh	$rd, $rs1, $rs2", []>;

def FF2MUL32RS_LL : FmtALU64<0x67, 0x027,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "ff2mul32rs_ll	$rd, $rs1, $rs2", []>;

def FF2MUL32R_HH : FmtALU64<0x67, 0x028,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "ff2mul32r_hh	$rd, $rs1, $rs2", []>;

def FF2MUL32R_LH : FmtALU64<0x67, 0x029,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "ff2mul32r_lh	$rd, $rs1, $rs2", []>;

def FF2MUL32R_LL : FmtALU64<0x67, 0x02A,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "ff2mul32r_ll	$rd, $rs1, $rs2", []>;

def FF2MULA32RS_HH : FmtALU64Acc<0x67, 0x02B,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2mula32rs_hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FF2MULA32RS_LH : FmtALU64Acc<0x67, 0x02C,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2mula32rs_lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FF2MULA32RS_LL : FmtALU64Acc<0x67, 0x02D,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2mula32rs_ll	$rd, $rs1, $rs2", []> {
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
def FF2MULA32R_HH : FmtALU64Acc<0x67, 0x02E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2mula32r_hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FF2MULA32R_LH : FmtALU64Acc<0x67, 0x02F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2mula32r_lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FF2MULA32R_LL : FmtALU64Acc<0x67, 0x030,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2mula32r_ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FF2MULS32RS_HH : FmtALU64Acc<0x67, 0x031,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2muls32rs_hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FF2MULS32RS_LH : FmtALU64Acc<0x67, 0x032,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2muls32rs_lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FF2MULS32RS_LL : FmtALU64Acc<0x67, 0x033,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2muls32rs_ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FF2MULS32R_HH : FmtALU64Acc<0x67, 0x034,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2muls32r_hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FF2MULS32R_LH : FmtALU64Acc<0x67, 0x035,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2muls32r_lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FF2MULS32R_LL : FmtALU64Acc<0x67, 0x036,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "ff2muls32r_ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMUL16_HS00 : FmtALU64<0x67, 0x037,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_hs00	$rd, $rs1, $rs2", []>;

def FMUL16_HS01 : FmtALU64<0x67, 0x038,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_hs01	$rd, $rs1, $rs2", []>;

def FMUL16_HS02 : FmtALU64<0x67, 0x039,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_hs02	$rd, $rs1, $rs2", []>;

def FMUL16_HS03 : FmtALU64<0x67, 0x03A,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_hs03	$rd, $rs1, $rs2", []>;

def FMUL16_HS11 : FmtALU64<0x67, 0x03B,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_hs11	$rd, $rs1, $rs2", []>;

def FMUL16_HS12 : FmtALU64<0x67, 0x03C,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_hs12	$rd, $rs1, $rs2", []>;

def FMUL16_HS13 : FmtALU64<0x67, 0x03D,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_hs13	$rd, $rs1, $rs2", []>;

def FMUL16_HS22 : FmtALU64<0x67, 0x03E,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_hs22	$rd, $rs1, $rs2", []>;

def FMUL16_HS23 : FmtALU64<0x67, 0x03F,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_hs23	$rd, $rs1, $rs2", []>;

def FMUL16_HS33 : FmtALU64<0x67, 0x040,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_hs33	$rd, $rs1, $rs2", []>;

def FMUL16_LS00 : FmtALU64<0x67, 0x041,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_ls00	$rd, $rs1, $rs2", []>;

def FMUL16_LS01 : FmtALU64<0x67, 0x042,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_ls01	$rd, $rs1, $rs2", []>;

def FMUL16_LS02 : FmtALU64<0x67, 0x043,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_ls02	$rd, $rs1, $rs2", []>;

def FMUL16_LS03 : FmtALU64<0x67, 0x044,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_ls03	$rd, $rs1, $rs2", []>;

def FMUL16_LS11 : FmtALU64<0x67, 0x045,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_ls11	$rd, $rs1, $rs2", []>;

def FMUL16_LS12 : FmtALU64<0x67, 0x046,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_ls12	$rd, $rs1, $rs2", []>;

def FMUL16_LS13 : FmtALU64<0x67, 0x047,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_ls13	$rd, $rs1, $rs2", []>;

def FMUL16_LS22 : FmtALU64<0x67, 0x048,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_ls22	$rd, $rs1, $rs2", []>;

def FMUL16_LS23 : FmtALU64<0x67, 0x049,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_ls23	$rd, $rs1, $rs2", []>;

def FMUL16_LS33 : FmtALU64<0x67, 0x04A,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul16_ls33	$rd, $rs1, $rs2", []>;

def FMUL32S_HH : FmtALU64<0x67, 0x04B,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul32s_hh	$rd, $rs1, $rs2", []>;

def FMUL32S_LH : FmtALU64<0x67, 0x04C,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul32s_lh	$rd, $rs1, $rs2", []>;

def FMUL32S_LL : FmtALU64<0x67, 0x04D,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmul32s_ll	$rd, $rs1, $rs2", []>;

def FMULA16_HS00 : FmtALU64Acc<0x67, 0x04E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_hs00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_HS01 : FmtALU64Acc<0x67, 0x04F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_hs01	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_HS02 : FmtALU64Acc<0x67, 0x050,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_hs02	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_HS03 : FmtALU64Acc<0x67, 0x051,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_hs03	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_HS11 : FmtALU64Acc<0x67, 0x052,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_hs11	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_HS12 : FmtALU64Acc<0x67, 0x053,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_hs12	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_HS13 : FmtALU64Acc<0x67, 0x054,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_hs13	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_HS22 : FmtALU64Acc<0x67, 0x055,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_hs22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_HS23 : FmtALU64Acc<0x67, 0x056,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_hs23	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_HS33 : FmtALU64Acc<0x67, 0x057,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_hs33	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_LS00 : FmtALU64Acc<0x67, 0x058,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_ls00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_LS01 : FmtALU64Acc<0x67, 0x059,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_ls01	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_LS02 : FmtALU64Acc<0x67, 0x05A,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_ls02	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_LS03 : FmtALU64Acc<0x67, 0x05B,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_ls03	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_LS11 : FmtALU64Acc<0x67, 0x05C,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_ls11	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_LS12 : FmtALU64Acc<0x67, 0x05D,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_ls12	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_LS13 : FmtALU64Acc<0x67, 0x05E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_ls13	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_LS22 : FmtALU64Acc<0x67, 0x05F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_ls22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_LS23 : FmtALU64Acc<0x67, 0x060,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_ls23	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA16_LS33 : FmtALU64Acc<0x67, 0x061,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula16_ls33	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA32S_HH : FmtALU64Acc<0x67, 0x062,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula32s_hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA32S_LH : FmtALU64Acc<0x67, 0x063,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula32s_lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULA32S_LL : FmtALU64Acc<0x67, 0x064,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmula32s_ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULAA16_HS_11_00 : FmtALU64Acc<0x67, 0x065,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulaa16_hs_11_00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULAA16_HS_13_02 : FmtALU64Acc<0x67, 0x066,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulaa16_hs_13_02	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

// Spec/DB: rtd[63:32] = SATQ1.31(rtd[63:32] + p3 + p2); DR_Read_Port
// includes rtd. Encoding is byte-identical to prior FmtALU64 form
// (FmtALU64Acc only adds a tied $rd_in MIR operand; wire layout unchanged).
// Migrated so GISel selectAccMAC / FIR_1 can preserve the accumulator.
def FMULAA16_HS_33_22 : FmtALU64Acc<0x67, 0x067,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulaa16_hs_33_22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULAA16_LS_11_00 : FmtALU64Acc<0x67, 0x068,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulaa16_ls_11_00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULAA16_LS_13_02 : FmtALU64Acc<0x67, 0x069,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulaa16_ls_13_02	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULAA16_LS_33_22 : FmtALU64Acc<0x67, 0x06A,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulaa16_ls_33_22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

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
def FMULAA32S_HHLL : FmtALU64Acc<0x67, 0x06B,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulaa32s_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULAA32S_HLLH : FmtALU64Acc<0x67, 0x06C,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulaa32s_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULAS32S_HHLL : FmtALU64Acc<0x67, 0x06D,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulas32s_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULAS32S_HLLH : FmtALU64Acc<0x67, 0x06E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulas32s_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_HS00 : FmtALU64Acc<0x67, 0x06F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_hs00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_HS01 : FmtALU64Acc<0x67, 0x070,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_hs01	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_HS02 : FmtALU64Acc<0x67, 0x071,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_hs02	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_HS03 : FmtALU64Acc<0x67, 0x072,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_hs03	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_HS11 : FmtALU64Acc<0x67, 0x073,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_hs11	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_HS12 : FmtALU64Acc<0x67, 0x074,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_hs12	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_HS13 : FmtALU64Acc<0x67, 0x075,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_hs13	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_HS22 : FmtALU64Acc<0x67, 0x076,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_hs22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_HS23 : FmtALU64Acc<0x67, 0x077,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_hs23	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_HS33 : FmtALU64Acc<0x67, 0x078,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_hs33	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_LS00 : FmtALU64Acc<0x67, 0x079,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_ls00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_LS01 : FmtALU64Acc<0x67, 0x07A,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_ls01	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_LS02 : FmtALU64Acc<0x67, 0x07B,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_ls02	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_LS03 : FmtALU64Acc<0x67, 0x07C,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_ls03	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_LS11 : FmtALU64Acc<0x67, 0x07D,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_ls11	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_LS12 : FmtALU64Acc<0x67, 0x07E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_ls12	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_LS13 : FmtALU64Acc<0x67, 0x07F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_ls13	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_LS22 : FmtALU64Acc<0x67, 0x080,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_ls22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_LS23 : FmtALU64Acc<0x67, 0x081,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_ls23	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS16_LS33 : FmtALU64Acc<0x67, 0x082,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls16_ls33	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS32S_HH : FmtALU64Acc<0x67, 0x083,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls32s_hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS32S_LH : FmtALU64Acc<0x67, 0x084,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls32s_lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULS32S_LL : FmtALU64Acc<0x67, 0x085,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmuls32s_ll	$rd, $rs1, $rs2", []> {
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
def FMULSA32S_HHLL : FmtALU64Acc<0x67, 0x086,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulsa32s_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULSA32S_HLLH : FmtALU64Acc<0x67, 0x087,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulsa32s_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULSS16_HS_11_00 : FmtALU64Acc<0x67, 0x088,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulss16_hs_11_00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULSS16_HS_13_02 : FmtALU64Acc<0x67, 0x089,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulss16_hs_13_02	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULSS16_HS_33_22 : FmtALU64Acc<0x67, 0x08A,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulss16_hs_33_22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULSS16_LS_11_00 : FmtALU64Acc<0x67, 0x08B,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulss16_ls_11_00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULSS16_LS_13_02 : FmtALU64Acc<0x67, 0x08C,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulss16_ls_13_02	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULSS16_LS_33_22 : FmtALU64Acc<0x67, 0x08D,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulss16_ls_33_22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

// FMULSS32S_HHLL/HLLH — spec correction (Rev 2): the *SS*
// (sub-sub → combine '+') inner-product sign was inverted in the old manual.
// Correct behavior:
// FMULSS32S_HHLL: rtd = SATQ1.63(rtd - (HH·HH) - (LL·LL)) [inner '-', both '-']
// FMULSS32S_HLLH: rtd = SATQ1.63(rtd - (HH·LL) - (LL·HH))
// (old DB combined the two products with '+'). The accumulator-form question
// (DB reads rtd) is a SEPARATE pre-existing gap — these stay FmtALU64 here
// for encoding parity with the sibling FMULZ*32S zero-acc ops; the spec
// fix is documentation-only.
def FMULSS32S_HHLL : FmtALU64Acc<0x67, 0x08E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulss32s_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULSS32S_HLLH : FmtALU64Acc<0x67, 0x08F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "fmulss32s_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def FMULZAA16_HS_11_00 : FmtALU64<0x67, 0x090,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzaa16_hs_11_00	$rd, $rs1, $rs2", []>;

def FMULZAA16_HS_13_02 : FmtALU64<0x67, 0x091,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzaa16_hs_13_02	$rd, $rs1, $rs2", []>;

def FMULZAA16_HS_33_22 : FmtALU64<0x67, 0x092,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzaa16_hs_33_22	$rd, $rs1, $rs2", []>;

def FMULZAA16_LS_11_00 : FmtALU64<0x67, 0x093,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzaa16_ls_11_00	$rd, $rs1, $rs2", []>;

def FMULZAA16_LS_13_02 : FmtALU64<0x67, 0x094,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzaa16_ls_13_02	$rd, $rs1, $rs2", []>;

def FMULZAA16_LS_33_22 : FmtALU64<0x67, 0x095,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzaa16_ls_33_22	$rd, $rs1, $rs2", []>;

def FMULZAA32S_HHLL : FmtALU64<0x67, 0x096,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzaa32s_hhll	$rd, $rs1, $rs2", []>;

def FMULZAA32S_HLLH : FmtALU64<0x67, 0x097,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzaa32s_hllh	$rd, $rs1, $rs2", []>;

def FMULZAS32S_HHLL : FmtALU64<0x67, 0x098,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzas32s_hhll	$rd, $rs1, $rs2", []>;

def FMULZAS32S_HLLH : FmtALU64<0x67, 0x099,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzas32s_hllh	$rd, $rs1, $rs2", []>;

def FMULZSA32S_HHLL : FmtALU64<0x67, 0x09A,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzsa32s_hhll	$rd, $rs1, $rs2", []>;

def FMULZSA32S_HLLH : FmtALU64<0x67, 0x09B,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzsa32s_hllh	$rd, $rs1, $rs2", []>;

def FMULZSS16_HS_11_00 : FmtALU64<0x67, 0x09C,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzss16_hs_11_00	$rd, $rs1, $rs2", []>;

def FMULZSS16_HS_13_02 : FmtALU64<0x67, 0x09D,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzss16_hs_13_02	$rd, $rs1, $rs2", []>;

def FMULZSS16_HS_33_22 : FmtALU64<0x67, 0x09E,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzss16_hs_33_22	$rd, $rs1, $rs2", []>;

def FMULZSS16_LS_11_00 : FmtALU64<0x67, 0x09F,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzss16_ls_11_00	$rd, $rs1, $rs2", []>;

def FMULZSS16_LS_13_02 : FmtALU64<0x67, 0x0A0,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzss16_ls_13_02	$rd, $rs1, $rs2", []>;

def FMULZSS16_LS_33_22 : FmtALU64<0x67, 0x0A1,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzss16_ls_33_22	$rd, $rs1, $rs2", []>;

def FMULZSS32S_HHLL : FmtALU64<0x67, 0x0A2,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzss32s_hhll	$rd, $rs1, $rs2", []>;

def FMULZSS32S_HLLH : FmtALU64<0x67, 0x0A3,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "fmulzss32s_hllh	$rd, $rs1, $rs2", []>;

def MAX64 : FmtALU64<0x67, 0x0A4,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "max64	$rd, $rs1, $rs2", []>;

def MIN64 : FmtALU64<0x67, 0x0A5,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "min64	$rd, $rs1, $rs2", []>;

def MUL16AQ : FmtALU64Acc<0x67, 0x0A6,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mul16aq	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MUL16ZAQ : FmtALU64<0x67, 0x0A7,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul16zaq	$rd, $rs1, $rs2", []>;

def MUL64_HH : FmtALU64<0x67, 0x0A8,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.hh	$rd, $rs1, $rs2", []>;

def MUL64_HL : FmtALU64<0x67, 0x0A9,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.hl	$rd, $rs1, $rs2", []>;

def MUL64_HUH : FmtALU64<0x67, 0x0AA,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.huh	$rd, $rs1, $rs2", []>;

def MUL64_HUL : FmtALU64<0x67, 0x0AB,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.hul	$rd, $rs1, $rs2", []>;

def MUL64_LH : FmtALU64<0x67, 0x0AC,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.lh	$rd, $rs1, $rs2", []>;

def MUL64_LL : FmtALU64<0x67, 0x0AD,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.ll	$rd, $rs1, $rs2", []>;

def MUL64_LUH : FmtALU64<0x67, 0x0AE,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.luh	$rd, $rs1, $rs2", []>;

def MUL64_LUL : FmtALU64<0x67, 0x0AF,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.lul	$rd, $rs1, $rs2", []>;

def MUL64_UHH : FmtALU64<0x67, 0x0B0,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.uhh	$rd, $rs1, $rs2", []>;

def MUL64_UHL : FmtALU64<0x67, 0x0B1,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.uhl	$rd, $rs1, $rs2", []>;

def MUL64_UHUH : FmtALU64<0x67, 0x0B2,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.uhuh	$rd, $rs1, $rs2", []>;

def MUL64_UHUL : FmtALU64<0x67, 0x0B3,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.uhul	$rd, $rs1, $rs2", []>;

def MUL64_ULH : FmtALU64<0x67, 0x0B4,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.ulh	$rd, $rs1, $rs2", []>;

def MUL64_ULL : FmtALU64<0x67, 0x0B5,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.ull	$rd, $rs1, $rs2", []>;

def MUL64_ULUH : FmtALU64<0x67, 0x0B6,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.uluh	$rd, $rs1, $rs2", []>;

def MUL64_ULUL : FmtALU64<0x67, 0x0B7,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mul64.ulul	$rd, $rs1, $rs2", []>;

def MULA64_HH : FmtALU64Acc<0x67, 0x0B8,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_HL : FmtALU64Acc<0x67, 0x0B9,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.hl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_HUH : FmtALU64Acc<0x67, 0x0BA,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.huh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_HUL : FmtALU64Acc<0x67, 0x0BB,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.hul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_LH : FmtALU64Acc<0x67, 0x0BC,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_LL : FmtALU64Acc<0x67, 0x0BD,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_LUH : FmtALU64Acc<0x67, 0x0BE,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.luh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_LUL : FmtALU64Acc<0x67, 0x0BF,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.lul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_UHH : FmtALU64Acc<0x67, 0x0C0,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.uhh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_UHL : FmtALU64Acc<0x67, 0x0C1,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.uhl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_UHUH : FmtALU64Acc<0x67, 0x0C2,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.uhuh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_UHUL : FmtALU64Acc<0x67, 0x0C3,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.uhul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_ULH : FmtALU64Acc<0x67, 0x0C4,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.ulh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_ULL : FmtALU64Acc<0x67, 0x0C5,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.ull	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_ULUH : FmtALU64Acc<0x67, 0x0C6,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.uluh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULA64_ULUL : FmtALU64Acc<0x67, 0x0C7,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mula64.ulul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAA32_HHLL : FmtALU64Acc<0x67, 0x0C8,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulaa32_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAA32_HLLH : FmtALU64Acc<0x67, 0x0C9,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulaa32_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS32_HHLL : FmtALU64Acc<0x67, 0x0CA,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas32_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS32_HLLH : FmtALU64Acc<0x67, 0x0CB,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas32_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_HH : FmtALU64Acc<0x67, 0x0CC,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_HL : FmtALU64Acc<0x67, 0x0CD,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_hl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_HUH : FmtALU64Acc<0x67, 0x0CE,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_huh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_HUL : FmtALU64Acc<0x67, 0x0CF,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_hul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_LH : FmtALU64Acc<0x67, 0x0D0,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_LL : FmtALU64Acc<0x67, 0x0D1,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_LUH : FmtALU64Acc<0x67, 0x0D2,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_luh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_LUL : FmtALU64Acc<0x67, 0x0D3,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_lul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_UHH : FmtALU64Acc<0x67, 0x0D4,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_uhh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_UHL : FmtALU64Acc<0x67, 0x0D5,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_uhl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_UHUH : FmtALU64Acc<0x67, 0x0D6,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_uhuh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_UHUL : FmtALU64Acc<0x67, 0x0D7,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_uhul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_ULH : FmtALU64Acc<0x67, 0x0D8,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_ulh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_ULL : FmtALU64Acc<0x67, 0x0D9,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_ull	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_ULUH : FmtALU64Acc<0x67, 0x0DA,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_uluh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULAS64_ULUL : FmtALU64Acc<0x67, 0x0DB,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulas64_ulul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

// MULL/MULSSH/MULSUH/MULUUH — slot-0 GPR32 multiply (DB pipeline=MAC
// slots=[0], latency 2; golden slot0_alu indices 65-68). migrated these
// off the legacy FmtALU64<0x67> 4-byte parcel onto the Mode-0 §6.5 s0 ALU32
// layout: opcode 0x3F (extended-ALU32 marker) + ext[4:2] sub-variant. The
// wire form is destructive 2-operand (spec §6.5: rt = rs*rt — only rs1 + rt
// on the wire); the architectural/asm view stays 3-operand (DB: rt = rs1*rs2).
// The tied constraint "$rd = $rs2" bridges the two: when the selector builds
// MULL with dst != src2, LLVM's TwoAddress pass inserts a MOV (copy rs2->rd)
// before MULL, so the encoded rt field always holds rs2 at silicon time. The
// encoder (HaydnMCCodeEmitter PS_S0) packs rt=$rd, rs1=$rs1, ext=getMullVariant
// (drops $rs2 — it is tied to $rd); the decoder reconstructs rs2 = rt.
//
// The funct template param writes the variant STATICALLY into Inst bits[8:4]
// (ext[4:2]=variant, ext[1:0]=0) so tablegen's decoder table can distinguish
// the 4 variants (FmtALU32 leaves ext unset → all collide → "Decoding
// conflict"). FmtALU32Ext provides EW_32Bit + Slot012_ALU itinerary + GPR32
// operand model; the Mode-0 encoder uses getEncOpcodeMap (not these Inst
// bits). See.
def MULL : FmtALU32Ext<0x3F, 0b00000,
    (outs GPR32:$rd),
    (ins GPR32:$rs1, GPR32:$rs2),
    "mull	$rd, $rs1, $rs2", []> {
  // NOT tied, despite an earlier note claiming a "destructive wire form".
  // The database reads rs1 and rs2 and writes rt, three distinct registers,
  // and both FmtALU32Ext and the format E members carry three register
  // fields. The tie only made TwoAddress insert a copy the hardware never
  // needed. FORMAT-E-SWITCH-PLAN.md 5.11.
}

def MULS64_HH : FmtALU64Acc<0x67, 0x0DD,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_HL : FmtALU64Acc<0x67, 0x0DE,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.hl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_HUH : FmtALU64Acc<0x67, 0x0DF,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.huh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_HUL : FmtALU64Acc<0x67, 0x0E0,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.hul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_LH : FmtALU64Acc<0x67, 0x0E1,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_LL : FmtALU64Acc<0x67, 0x0E2,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_LUH : FmtALU64Acc<0x67, 0x0E3,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.luh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_LUL : FmtALU64Acc<0x67, 0x0E4,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.lul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_UHH : FmtALU64Acc<0x67, 0x0E5,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.uhh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_UHL : FmtALU64Acc<0x67, 0x0E6,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.uhl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_UHUH : FmtALU64Acc<0x67, 0x0E7,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.uhuh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_UHUL : FmtALU64Acc<0x67, 0x0E8,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.uhul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_ULH : FmtALU64Acc<0x67, 0x0E9,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.ulh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_ULL : FmtALU64Acc<0x67, 0x0EA,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.ull	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_ULUH : FmtALU64Acc<0x67, 0x0EB,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.uluh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULS64_ULUL : FmtALU64Acc<0x67, 0x0EC,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "muls64.ulul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSA32_HHLL : FmtALU64Acc<0x67, 0x0ED,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulsa32_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSA32_HLLH : FmtALU64Acc<0x67, 0x0EE,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulsa32_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS32_HHLL : FmtALU64Acc<0x67, 0x0EF,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss32_hhll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS32_HLLH : FmtALU64Acc<0x67, 0x0F0,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss32_hllh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_HH : FmtALU64Acc<0x67, 0x0F1,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_hh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_HL : FmtALU64Acc<0x67, 0x0F2,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_hl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_HUH : FmtALU64Acc<0x67, 0x0F3,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_huh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_HUL : FmtALU64Acc<0x67, 0x0F4,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_hul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_LH : FmtALU64Acc<0x67, 0x0F5,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_lh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_LL : FmtALU64Acc<0x67, 0x0F6,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_ll	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_LUH : FmtALU64Acc<0x67, 0x0F7,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_luh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_LUL : FmtALU64Acc<0x67, 0x0F8,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_lul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_UHH : FmtALU64Acc<0x67, 0x0F9,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_uhh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_UHL : FmtALU64Acc<0x67, 0x0FA,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_uhl	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_UHUH : FmtALU64Acc<0x67, 0x0FB,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_uhuh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_UHUL : FmtALU64Acc<0x67, 0x0FC,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_uhul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_ULH : FmtALU64Acc<0x67, 0x0FD,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_ulh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_ULL : FmtALU64Acc<0x67, 0x0FE,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_ull	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_ULUH : FmtALU64Acc<0x67, 0x0FF,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_uluh	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def MULSS64_ULUL : FmtALU64Acc<0x67, 0x100,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "mulss64_ulul	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

// MULSSH/MULSUH/MULUUH — slot-0 GPR32 multiply (see MULL comment above;
// Mode-0 §6.5 migration). Same destructive wire form: opcode 0x3F, ext[4:2]
// variant 1/2/3 (written statically via FmtALU32Ext funct at bits[8:4]).
// B3 (golden-authoritative): NON-destructive — rt, rs1, rs2 distinct
// (golden: GPR_Write_Port=[rt], GPR_Read_Port=[rs1,rs2], no tie). REMOVE the
// spurious `let Constraints = "$rd = $rs2"` (it was the B3 verifier-reject
// cause — golden says rt is distinct from rs1/rs2).
def MULSSH : FmtALU32Ext<0x3F, 0b00100,
    (outs GPR32:$rd),
    (ins GPR32:$rs1, GPR32:$rs2),
    "mulssh	$rd, $rs1, $rs2", []>;

def MULSUH : FmtALU32Ext<0x3F, 0b01000,
    (outs GPR32:$rd),
    (ins GPR32:$rs1, GPR32:$rs2),
    "mulsuh	$rd, $rs1, $rs2", []>;

def MULUUH : FmtALU32Ext<0x3F, 0b01100,
    (outs GPR32:$rd),
    (ins GPR32:$rs1, GPR32:$rs2),
    "muluuh	$rd, $rs1, $rs2", []>;

def MULZAA32_HHLL : FmtALU64<0x67, 0x104,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mulzaa32_hhll	$rd, $rs1, $rs2", []>;

def MULZAA32_HLLH : FmtALU64<0x67, 0x105,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mulzaa32_hllh	$rd, $rs1, $rs2", []>;

def MULZAS32_HHLL : FmtALU64<0x67, 0x106,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mulzas32_hhll	$rd, $rs1, $rs2", []>;

def MULZAS32_HLLH : FmtALU64<0x67, 0x107,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mulzas32_hllh	$rd, $rs1, $rs2", []>;

def MULZSA32_HHLL : FmtALU64<0x67, 0x108,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mulzsa32_hhll	$rd, $rs1, $rs2", []>;

def MULZSA32_HLLH : FmtALU64<0x67, 0x109,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mulzsa32_hllh	$rd, $rs1, $rs2", []>;

def MULZSS32_HHLL : FmtALU64<0x67, 0x10A,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mulzss32_hhll	$rd, $rs1, $rs2", []>;

def MULZSS32_HLLH : FmtALU64<0x67, 0x10B,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "mulzss32_hllh	$rd, $rs1, $rs2", []>;

def SLL64 : FmtALU64<0x67, 0x10C,
    (outs DR64:$rd),
    (ins DR64:$rs1, GPR32:$rs2),
    "sll64	$rd, $rs1, $rs2", []>;

def SMULA16S_00 : FmtALU64Acc<0x67, 0x10D,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16s_00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16S_10 : FmtALU64Acc<0x67, 0x10E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16s_10	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16S_11 : FmtALU64Acc<0x67, 0x10F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16s_11	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16S_20 : FmtALU64Acc<0x67, 0x110,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16s_20	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16S_21 : FmtALU64Acc<0x67, 0x111,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16s_21	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16S_22 : FmtALU64Acc<0x67, 0x112,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16s_22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16S_30 : FmtALU64Acc<0x67, 0x113,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16s_30	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16S_31 : FmtALU64Acc<0x67, 0x114,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16s_31	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16S_32 : FmtALU64Acc<0x67, 0x115,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16s_32	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16S_33 : FmtALU64Acc<0x67, 0x116,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16s_33	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16_00 : FmtALU64Acc<0x67, 0x117,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16_00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16_10 : FmtALU64Acc<0x67, 0x118,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16_10	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16_11 : FmtALU64Acc<0x67, 0x119,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16_11	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16_20 : FmtALU64Acc<0x67, 0x11A,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16_20	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16_21 : FmtALU64Acc<0x67, 0x11B,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16_21	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16_22 : FmtALU64Acc<0x67, 0x11C,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16_22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16_30 : FmtALU64Acc<0x67, 0x11D,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16_30	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16_31 : FmtALU64Acc<0x67, 0x11E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16_31	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16_32 : FmtALU64Acc<0x67, 0x11F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16_32	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULA16_33 : FmtALU64Acc<0x67, 0x120,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smula16_33	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16S_00 : FmtALU64Acc<0x67, 0x121,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16s_00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16S_10 : FmtALU64Acc<0x67, 0x122,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16s_10	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16S_11 : FmtALU64Acc<0x67, 0x123,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16s_11	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16S_20 : FmtALU64Acc<0x67, 0x124,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16s_20	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16S_21 : FmtALU64Acc<0x67, 0x125,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16s_21	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16S_22 : FmtALU64Acc<0x67, 0x126,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16s_22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16S_30 : FmtALU64Acc<0x67, 0x127,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16s_30	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16S_31 : FmtALU64Acc<0x67, 0x128,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16s_31	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16S_32 : FmtALU64Acc<0x67, 0x129,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16s_32	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16S_33 : FmtALU64Acc<0x67, 0x12A,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16s_33	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16_00 : FmtALU64Acc<0x67, 0x12B,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16_00	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16_10 : FmtALU64Acc<0x67, 0x12C,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16_10	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16_11 : FmtALU64Acc<0x67, 0x12D,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16_11	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16_20 : FmtALU64Acc<0x67, 0x12E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16_20	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16_21 : FmtALU64Acc<0x67, 0x12F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16_21	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16_22 : FmtALU64Acc<0x67, 0x130,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16_22	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16_30 : FmtALU64Acc<0x67, 0x131,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16_30	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16_31 : FmtALU64Acc<0x67, 0x132,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16_31	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16_32 : FmtALU64Acc<0x67, 0x133,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16_32	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def SMULS16_33 : FmtALU64Acc<0x67, 0x134,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "smuls16_33	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

// SRA64/SRA64R/SRL64: 64-bit shift producing DR64 result.
// ISA DB: rtd=DR64(result), rsd=DR64(data), rs=GPR32(shift amount)
def SRA64 : FmtALU64<0x67, 0x135,
    (outs DR64:$rd),
    (ins DR64:$rs1, GPR32:$rs2),
    "sra64	$rd, $rs1, $rs2", []>;

def SRA64R : FmtALU64<0x67, 0x136,
    (outs DR64:$rd),
    (ins DR64:$rs1, GPR32:$rs2),
    "sra64r	$rd, $rs1, $rs2", []>;

def SRL64 : FmtALU64<0x67, 0x137,
    (outs DR64:$rd),
    (ins DR64:$rs1, GPR32:$rs2),
    "srl64	$rd, $rs1, $rs2", []>;

def SUB64S : FmtALU64<0x67, 0x138,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "sub64s	$rd, $rs1, $rs2", []>;

def SUB64S_H : FmtALU64<0x67, 0x139,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "sub64s_h	$rd, $rs1, $rs2", []>;

def SUB64S_L : FmtALU64<0x67, 0x13A,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "sub64s_l	$rd, $rs1, $rs2", []>;

def SUB64_H : FmtALU64<0x67, 0x13B,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "sub64_h	$rd, $rs1, $rs2", []>;

def SUB64_L : FmtALU64<0x67, 0x13C,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "sub64_l	$rd, $rs1, $rs2", []>;

def X2ADD32S : FmtALU64<0x67, 0x13D,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2add32s	$rd, $rs1, $rs2", []>;

def X2ADD32S_HLLH : FmtALU64<0x67, 0x13E,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2add32s_hllh	$rd, $rs1, $rs2", []>;

def X2ADD32_HLLH : FmtALU64<0x67, 0x13F,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2add32_hllh	$rd, $rs1, $rs2", []>;

def X2ADDSUB32 : FmtALU64<0x67, 0x140,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2addsub32	$rd, $rs1, $rs2", []>;

def X2ADDSUB32S : FmtALU64<0x67, 0x141,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2addsub32s	$rd, $rs1, $rs2", []>;

def X2ADDSUB32S_HLLH : FmtALU64<0x67, 0x142,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2addsub32s_hllh	$rd, $rs1, $rs2", []>;

def X2ADDSUB32_HLLH : FmtALU64<0x67, 0x143,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2addsub32_hllh	$rd, $rs1, $rs2", []>;

def X2CLAMP32 : FmtALU64Acc<0x67, 0x144,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2clamp32	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X2DOT32 : FmtALU64<0x67, 0x145,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2dot32	$rd, $rs1, $rs2", []>;

def X2FCMUL32RS : FmtALU64<0x67, 0x146,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2fcmul32rs	$rd, $rs1, $rs2", []>;

def X2FCMUL32RSS : FmtALU64<0x67, 0x147,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2fcmul32rss	$rd, $rs1, $rs2", []>;

def X2FCMULA32RS : FmtALU64Acc<0x67, 0x148,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2fcmula32rs	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X2FCMULA32RSS : FmtALU64Acc<0x67, 0x149,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2fcmula32rss	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X2FF2RSST32 : FmtALU64<0x67, 0x14A,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2ff2rsst32	$rd, $rs1, $rs2", []>;

def X2FF2RST32 : FmtALU64<0x67, 0x14B,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2ff2rst32	$rd, $rs1, $rs2", []>;

def X2FMUL32RS : FmtALU64<0x67, 0x14C,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2fmul32rs	$rd, $rs1, $rs2", []>;

def X2FMUL32RSS : FmtALU64<0x67, 0x14D,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2fmul32rss	$rd, $rs1, $rs2", []>;

def X2FMUL32TS : FmtALU64<0x67, 0x14E,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2fmul32ts	$rd, $rs1, $rs2", []>;

def X2FMULA32RS : FmtALU64Acc<0x67, 0x14F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2fmula32rs	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X2FMULA32RSS : FmtALU64Acc<0x67, 0x150,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2fmula32rss	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X2FMULA32TS : FmtALU64Acc<0x67, 0x151,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2fmula32ts	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X2FMULS32RS : FmtALU64Acc<0x67, 0x152,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2fmuls32rs	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X2FMULS32RSS : FmtALU64Acc<0x67, 0x153,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2fmuls32rss	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X2FMULS32TS : FmtALU64Acc<0x67, 0x154,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2fmuls32ts	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X2FRSST32 : FmtALU64<0x67, 0x155,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2frsst32	$rd, $rs1, $rs2", []>;

def X2FRST32 : FmtALU64<0x67, 0x156,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2frst32	$rd, $rs1, $rs2", []>;

def X2MAX32 : FmtALU64<0x67, 0x157,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2max32	$rd, $rs1, $rs2", []>;

def X2MIN32 : FmtALU64<0x67, 0x158,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2min32	$rd, $rs1, $rs2", []>;

def X2MULAPH32 : FmtALU64Acc<0x67, 0x159,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2mulaph32	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X2MULAPL32 : FmtALU64Acc<0x67, 0x15A,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2mulapl32	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X2MULPH32 : FmtALU64<0x67, 0x15B,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2mulph32	$rd, $rs1, $rs2", []>;

def X2MULPL32 : FmtALU64<0x67, 0x15C,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2mulpl32	$rd, $rs1, $rs2", []>;

def X2MULSPH32 : FmtALU64Acc<0x67, 0x15D,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2mulsph32	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X2MULSPL32 : FmtALU64Acc<0x67, 0x15E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x2mulspl32	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X2SEL32_HH : FmtALU64<0x67, 0x15F,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2sel32_hh	$rd, $rs1, $rs2", []>;

def X2SEL32_HL : FmtALU64<0x67, 0x160,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2sel32_hl	$rd, $rs1, $rs2", []>;

def X2SEL32_LH : FmtALU64<0x67, 0x161,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2sel32_lh	$rd, $rs1, $rs2", []>;

def X2SEL32_LL : FmtALU64<0x67, 0x162,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2sel32_ll	$rd, $rs1, $rs2", []>;

// X2SRA32R: per spec, rtd=DR64, rsd=DR64, rs=GPR32 (shift amount).
def X2SRA32R : FmtALU64<0x67, 0x163,
    (outs DR64:$rd),
    (ins DR64:$rs1, GPR32:$rs2),
    "x2sra32r	$rd, $rs1, $rs2", []>;

def X2SUB32S : FmtALU64<0x67, 0x164,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2sub32s	$rd, $rs1, $rs2", []>;

def X2SUB32S_HLLH : FmtALU64<0x67, 0x165,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2sub32s_hllh	$rd, $rs1, $rs2", []>;

def X2SUB32_HLLH : FmtALU64<0x67, 0x166,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2sub32_hllh	$rd, $rs1, $rs2", []>;

def X2SUBADD32 : FmtALU64<0x67, 0x167,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2subadd32	$rd, $rs1, $rs2", []>;

def X2SUBADD32S : FmtALU64<0x67, 0x168,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2subadd32s	$rd, $rs1, $rs2", []>;

def X2SUBADD32S_HLLH : FmtALU64<0x67, 0x169,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2subadd32s_hllh	$rd, $rs1, $rs2", []>;

def X2SUBADD32_HLLH : FmtALU64<0x67, 0x16A,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x2subadd32_hllh	$rd, $rs1, $rs2", []>;

def X4ADD16S : FmtALU64<0x67, 0x16B,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4add16s	$rd, $rs1, $rs2", []>;

def X4CJMUL16S_H : FmtALU64<0x67, 0x16C,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4cjmul16s_h	$rd, $rs1, $rs2", []>;

def X4CJMUL16S_L : FmtALU64<0x67, 0x16D,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4cjmul16s_l	$rd, $rs1, $rs2", []>;

def X4CJMULA16S_H : FmtALU64Acc<0x67, 0x16E,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x4cjmula16s_h	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X4CJMULA16S_L : FmtALU64Acc<0x67, 0x16F,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x4cjmula16s_l	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X4CLAMP16 : FmtALU64Acc<0x67, 0x170,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x4clamp16	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X4CMUL16S_H : FmtALU64<0x67, 0x171,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4cmul16s_h	$rd, $rs1, $rs2", []>;

def X4CMUL16S_L : FmtALU64<0x67, 0x172,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4cmul16s_l	$rd, $rs1, $rs2", []>;

def X4CMULA16S_H : FmtALU64Acc<0x67, 0x173,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x4cmula16s_h	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X4CMULA16S_L : FmtALU64Acc<0x67, 0x174,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x4cmula16s_l	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X4DOT16 : FmtALU64<0x67, 0x175,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4dot16	$rd, $rs1, $rs2", []>;

def X4FCMUL16RS : FmtALU64<0x67, 0x176,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4fcmul16rs	$rd, $rs1, $rs2", []>;

def X4FCMUL16RSS : FmtALU64<0x67, 0x177,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4fcmul16rss	$rd, $rs1, $rs2", []>;

// X4FCMULA16RS / RSS — complex MAC accumulate forms. DB behavior reads rtd
// as accumulator (rtd[lane] = SAT(rtd[lane] +...)), so these are tied-def
// FmtALU64Acc (was mis-modeled as FmtALU64 non-accumulating —).
def X4FCMULA16RS : FmtALU64Acc<0x67, 0x178,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x4fcmula16rs	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X4FCMULA16RSS : FmtALU64Acc<0x67, 0x179,
    (outs DR64:$rd),
    (ins DR64:$rd_in, DR64:$rs1, DR64:$rs2),
    "x4fcmula16rss	$rd, $rs1, $rs2", []> {
    let Constraints = "$rd = $rd_in";
  }

def X4FMUL16RS : FmtALU64<0x67, 0x17A,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4fmul16rs	$rd, $rs1, $rs2", []>;

def X4FMUL16RSS : FmtALU64<0x67, 0x17B,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4fmul16rss	$rd, $rs1, $rs2", []>;

def X4FMUL16TS : FmtALU64<0x67, 0x17C,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4fmul16ts	$rd, $rs1, $rs2", []>;

def X4FRSST16 : FmtALU64<0x67, 0x17D,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4frsst16	$rd, $rs1, $rs2", []>;

def X4FRST16 : FmtALU64<0x67, 0x17E,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4frst16	$rd, $rs1, $rs2", []>;

def X4MAX16 : FmtALU64<0x67, 0x17F,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4max16	$rd, $rs1, $rs2", []>;

def X4MIN16 : FmtALU64<0x67, 0x180,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4min16	$rd, $rs1, $rs2", []>;

def X4SAT32T16 : FmtALU64<0x67, 0x181,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4sat32t16	$rd, $rs1, $rs2", []>;

// X4 SIMD shifts: per spec, rtd=DR64, rsd=DR64, rs=GPR32 (shift amount).
// The shift amount comes from a GPR32 register (signed 5-bit value in rs[4:0]).
def X4SLL16 : FmtALU64<0x67, 0x182,
    (outs DR64:$rd),
    (ins DR64:$rs1, GPR32:$rs2),
    "x4sll16	$rd, $rs1, $rs2", []>;

def X4SRA16 : FmtALU64<0x67, 0x183,
    (outs DR64:$rd),
    (ins DR64:$rs1, GPR32:$rs2),
    "x4sra16	$rd, $rs1, $rs2", []>;

def X4SRA16R : FmtALU64<0x67, 0x184,
    (outs DR64:$rd),
    (ins DR64:$rs1, GPR32:$rs2),
    "x4sra16r	$rd, $rs1, $rs2", []>;

def X4SRL16 : FmtALU64<0x67, 0x185,
    (outs DR64:$rd),
    (ins DR64:$rs1, GPR32:$rs2),
    "x4srl16	$rd, $rs1, $rs2", []>;

def X4SUB16S : FmtALU64<0x67, 0x186,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2),
    "x4sub16s	$rd, $rs1, $rs2", []>;

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

def X2CMUL32 : FmtMAC2Dest<0x68, 0x00,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x2cmul32	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Unit_MAC0MAC1_L2;
  let TSFlags{2-0} = 0b010;
}

def X2CMUL32S : FmtMAC2Dest<0x68, 0x01,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x2cmul32s	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Unit_MAC0MAC1_L2;
  let TSFlags{2-0} = 0b010;
}

def X2CMUL32S_F2 : FmtMAC2Dest<0x68, 0x02,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x2cmul32s_f2	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Unit_MAC0MAC1_L2;
  let TSFlags{2-0} = 0b010;
}

def X2CMUL32_F2 : FmtMAC2Dest<0x68, 0x03,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x2cmul32_f2	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Unit_MAC0MAC1_L2;
  let TSFlags{2-0} = 0b010;
}

// (Path B): TRUE 2-output accumulate (golden DR_Write_Port=[rtd1,rtd2]
// DR_Read_Port=[rtd1,rtd2,rsd1,rsd2]). The legacy dag binds 2 dests + 2 acc
// inputs + 2 srcs (6 operands) with tied-def Constraints so MI.setDesc(legacy
// > _S<k>) preserves the operand count (6) and def count (2). The 2nd
// dest ($rtd2) and 2nd accum ($ra2) are unencoded passthroughs here (FmtMAC
// has no rtd2/ra2 fields) — the slot encoder handles the real encoding after
// setDesc. The Constraints force regalloc to coalesce acc1->rtd1, acc2->rtd2.
let Constraints = "$rd = $ra, $rtd2 = $ra2" in {
def X2MULA32 : FmtMAC2Dest<0x68, 0x04,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x2mula32	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Unit_MAC0MAC1_L2;
  let TSFlags{2-0} = 0b010;
}

def X2MULS32 : FmtMAC2Dest<0x68, 0x05,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x2muls32	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Unit_MAC0MAC1_L2;
  let TSFlags{2-0} = 0b010;
}
}  // Constraints = "$rd = $ra, $rtd2 = $ra2"

// X4FF2MUL16S — (Path B): TRUE 2-output NON-accum (golden DR_Write_Port
// =[rtd1,rtd2], DR_Read_Port=[rsd1,rsd2]). 2 dests + 2 srcs (4 operands).
def X4FF2MUL16S : FmtMAC2Dest<0x68, 0x06,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$rs1, DR64:$rs2),
    "x4ff2mul16s	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Unit_MAC0MAC1_L2;
  let TSFlags{2-0} = 0b010;
}

let Constraints = "$rd = $ra, $rtd2 = $ra2" in {
def X4FF2MULA16S : FmtMAC2Dest<0x68, 0x07,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4ff2mula16s	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Unit_MAC0MAC1_L2;
  let TSFlags{2-0} = 0b010;
}

def X4FF2MULS16S : FmtMAC2Dest<0x68, 0x08,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4ff2muls16s	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Unit_MAC0MAC1_L2;
  let TSFlags{2-0} = 0b010;
}

def X4MULA16 : FmtMAC2Dest<0x68, 0x09,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4mula16	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Unit_MAC0MAC1_L2;
  let TSFlags{2-0} = 0b010;
}

def X4MULA16S : FmtMAC2Dest<0x68, 0x0A,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4mula16s	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Unit_MAC0MAC1_L2;
  let TSFlags{2-0} = 0b010;
}

def X4MULS16 : FmtMAC2Dest<0x68, 0x0B,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4muls16	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Unit_MAC0MAC1_L2;
  let TSFlags{2-0} = 0b010;
}

def X4MULS16S : FmtMAC2Dest<0x68, 0x0C,
    (outs DR64:$rd, DR64:$rtd2),
    (ins DR64:$ra, DR64:$ra2, DR64:$rs1, DR64:$rs2),
    "x4muls16s	$rd, $rtd2, $rs1, $rs2", []> {
  let Itinerary = Unit_MAC0MAC1_L2;
  let TSFlags{2-0} = 0b010;
}
}  // Constraints = "$rd = $ra, $rtd2 = $ra2"

// X4SEL16 — DB syntax `X4SEL16 rtd, rsd1, rsd2, rs` (DR64 dest + 2 DR64
// sources + GPR32 lane selector; DB slot 1 only). Previously stubbed with
// all-GPR32 operands (capstone drift cat 1/2). Fixed to DR64 dest/sources;
// the GPR32 selector `ra` (DB `rs`) stays GPR32. Inert: no selector case
// no intrinsic Pat. FmtMAC encoding uses DR64 register fields, matching.
// Slot-1-only per capstone-schedule-port-audit §(b)-1.
def X4SEL16 : FmtMAC<0x68, 0x0D,
    (outs DR64:$rd),
    (ins DR64:$rs1, DR64:$rs2, GPR32:$ra),
    "x4sel16	$rd, $rs1, $rs2, $ra", []> {
  let Itinerary = Unit_ALU0ALU1ALU2_L1;
  let TSFlags{2-0} = 0b010;
}

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

//===---------------------------------------------------------------===//
// Store _REG family (Shape A). The prior stubs were on FmtALU32 with
// a bogus (outs GPR32:$rd), NO store-data operand, no mayStore, and the
// Slot012_ALU itinerary — wrong on every axis (review §3b/§4b). Stores invert
// the load model: the store DATA is an INPUT (DR64:$rtd for D_S*, GPR32:$rt
// for S_S*), the only OUTPUT is the optional tied base writeback ($rs_wb for
// POST/PRE; empty for WITH). asm order per DB: base, data, stride-reg.
//
// These keep their 0x80-0x96 opcodes (LS opcode space) but use HaydnInst<4>
// with explicit Inst bits + Slot0_LS itinerary (mirroring the gold-standard
// D_SDW_CB_* defs), because FmtALU32 hardcodes the ALU slot/itinerary and the
// rd/rs1/rs2 field shape that does not fit a store (no data operand, no result).
//===---------------------------------------------------------------===//

// BREV stores — DB: mem64[REVERSE32(rs1)] = rtd; rs1 += rs2. DR64 data, WB.
let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def D_SDW_BREV_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2),
    "d_sdw_brev_reg	$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;   // SLOT0 — DB slot 0 only for stores
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def S_SW_BREV_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2),
    "s_sw_brev_reg	$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

// POST_REG — DR64 data. DB: mem64[rs1] = rtd; rs1 += rs2.
let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def D_SDW_POST_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2),
    "d_sdw_post_reg	$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def D_SHW_POST_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2),
    "d_shw_post_reg	$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def D_SW_H_POST_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2),
    "d_sw_h_post_reg	$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def D_SW_L_POST_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2),
    "d_sw_l_post_reg	$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

// POST_REG — GPR32 data.
let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def S_SB_POST_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2),
    "s_sb_post_reg	$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def S_SHW_POST_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2),
    "s_shw_post_reg	$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def S_SW_POST_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2),
    "s_sw_post_reg	$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

// PRE_REG — update base, then store. Same two-def shape.
let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def D_SDW_PRE_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2),
    "d_sdw_pre_reg	$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def D_SHW_PRE_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2),
    "d_shw_pre_reg	$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def D_SW_H_PRE_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2),
    "d_sw_h_pre_reg	$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def D_SW_L_PRE_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2),
    "d_sw_l_pre_reg	$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def S_SB_PRE_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2),
    "s_sb_pre_reg	$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def S_SHW_PRE_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2),
    "s_shw_pre_reg	$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1, Constraints = "$rs1 = $rs1_wb" in
def S_SW_PRE_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2),
    "s_sw_pre_reg	$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

// WITH_REG — offset addressing, NO base writeback. (outs) empty.
let mayStore = 1 in
def D_SDW_WITH_REG : HaydnInst<4, (outs),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2),
    "d_sdw_with_reg	$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1 in
def D_SHW_WITH_REG : HaydnInst<4, (outs),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2),
    "d_shw_with_reg	$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1 in
def D_SW_H_WITH_REG : HaydnInst<4, (outs),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2),
    "d_sw_h_with_reg	$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1 in
def D_SW_L_WITH_REG : HaydnInst<4, (outs),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2),
    "d_sw_l_with_reg	$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1 in
def S_SB_WITH_REG : HaydnInst<4, (outs),
    (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2),
    "s_sb_with_reg	$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1 in
def S_SHW_WITH_REG : HaydnInst<4, (outs),
    (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2),
    "s_shw_with_reg	$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let mayStore = 1 in
def S_SW_WITH_REG : HaydnInst<4, (outs),
    (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2),
    "s_sw_with_reg	$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

} // end isAsmParserOnly = 1

//===----------------------------------------------------------------------===//
// Real ISA instructions — not yet migrated to encoding format classes
//
// These are genuine hardware instructions that use HaydnInst (no format class).
// Previously wrapped in a blanket isPseudo=1 scope that suppressed emission.
//
// TSFlags encodes slot mask [2:0] and encoded width [4:3]:
// TSFlags=9 = SLOT0 (0b001), EW_32Bit (0b01) — store variants
// TSFlags=11 = SLOT0|SLOT1 (0b011), EW_32Bit (0b01) — load variants
// TSFlags=14 = SLOT1|SLOT2 (0b110), EW_32Bit (0b01) — ALU64/SIMD/MAC
//
// Encoding migration (task #52) will assign real opcode bits per
// encoding_manual.md. Until then, instructions have no Inst encoding bits
// set. Without isPseudo=1, multiple instructions decode to 0x00000000
// causing "Decoding conflict encountered" build errors.
//
// These remain isPseudo=1 until encodings are assigned. GISel selector
// can still match them via Pat patterns in HaydnIntrinsics.td; the
// AsmPrinter simply skips emission until encoding is real.
//===----------------------------------------------------------------------===//

let isPseudo = 1 in {

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
let TSFlags = 14, isCodeGenOnly = 1 in
// isPseudo = 0: this def has real format E members (<NAME>_P<form><pos>_<UNIT>), so it is a real instruction and the
// enclosing `let isPseudo = 1 in {` at line 2925 -- Bundle128's "no encoding for this yet" -- no longer describes it. Leaving it
// pseudo makes HaydnAsmPrinter's `else if (I->isPseudo()) continue;`
// DROP the instruction from its bundle without a word.
let isPseudo = 0 in
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
// the surrounding `let isPseudo = 1 in { … }` scope (HaydnInstrInfoAuto.td:2205).
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
let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1,
    Constraints = "$rs = $rs_wb" in
def D_LDW_BREV_IMM : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "d_ldw_brev_imm\t$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;   // SLOT0|SLOT1 — DB slots 0 and 1
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1,
    Constraints = "$rs = $rs_wb" in
def D_LDW_BREV_REG : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "d_ldw_brev_reg\t$rtd, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

// POST_IMM forms — DR64 dest, tied base writeback, imm6 element index.
// D_LDW_POST_IMM reuses slot 0x47 (LD64_POST); D_LW_POST_IMM reuses 0x48.
// FmtLSPostInc defaults to Slot1_LD; override to Slot0_LS to match the DB
// (slots 0 and 1, pipeline LS) per review §4.2. The outs data operand is named
// `$rt` (not `$rtd`) to bind to the FmtLSPostInc `bits<4> rt` encoding field
// the asm still renders `d_ldw_post_imm $rt,...`. Mirrors LD64_POST exactly.
let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1,
    Constraints = "$rs = $rs_wb" in
def D_LDW_POST_IMM : FmtLSPostInc<0x47, (outs DR64:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "d_ldw_post_imm\t$rt, $rs, $scaled_imm", []> {
  // Multi-slot like LD64: alts S0/S1 dual post-inc streams (P3).
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isCodeGenOnly = 1, isAsmParserOnly = 1, mayLoad = 1,
    Constraints = "$rs = $rs_wb" in
let isPseudo = 0 in
def D_LW_POST_IMM : FmtLSPostInc<0x48, (outs DR64:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "d_lw_post_imm\t$rt, $rs, $scaled_imm", []> {
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isAsmParserOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
let isPseudo = 0 in
def D_LHW_POST_IMM : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "d_lhw_post_imm\t$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

// POST_REG forms — DR64 dest, register stride. Base is `$rs` (review §8.1.2).
// D_LDW_POST_REG now carries real tablegen `Inst` bits via
// FmtLSPostIncReg so the standalone 32-bit path (getBinaryCodeForInstr)
// emits a real word, AND the bundle FU_LD path packs the s1 register-offset
// layout per encoding_manual.md:412-494 (worked example d_ldw_post_reg d3
// r5, r7 -> s1 window 0x157103). No `isCodeGenOnly` (unlike D_LDW_POST_IMM
// which keeps it to avoid a decoder-table collision with its LD64_POST
// alias): D_LDW_POST_REG has no parallel alias name, so it stays in the
// asm parser matcher AND the auto-decoder table. The LoadStoreOptimizer
// still builds it by opcode (HaydnLoadStoreOptimizer.cpp:245) via the
// Pattern= (no selection). The outs data operand is `$rt` (not `$rtd`)
// to bind to the FmtLSPostIncReg `bits<4> rt` encoding field — mirrors
// D_LDW_POST_IMM at line 2791.
let isPseudo = 0, mayLoad = 1,
    Constraints = "$rs = $rs_wb" in
def D_LDW_POST_REG : FmtLSPostIncReg<0x4F, 0x0, (outs DR64:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "d_ldw_post_reg\t$rt, $rs, $rs2", []> {
  let Itinerary = Unit_LS0LOAD1_L2;
}

// W and HW POST_REG forms — same encoding shape as D_LDW_POST_REG
// (FmtLSPostIncReg), differing only in the type field (0x1=W, 0x2=HW) and
// the standalone opcode (0x4E, 0x4D). All three now have real Inst bits
// and round-trip through both standalone and bundle paths.
let isPseudo = 0, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def D_LW_POST_REG : FmtLSPostIncReg<0x4E, 0x1, (outs DR64:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "d_lw_post_reg\t$rt, $rs, $rs2", []> {
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def D_LHW_POST_REG : FmtLSPostIncReg<0x4D, 0x2, (outs DR64:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "d_lhw_post_reg\t$rt, $rs, $rs2", []> {
  let Itinerary = Unit_LS0LOAD1_L2;
}

// PRE_IMM / PRE_REG forms — update base, then load. Same two-def shape.
// isCodeGenOnly: GISel / ExpandPostIncEarly form these so SMS
// sees a live base writeback before MachinePipeliner.
let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def D_LDW_PRE_IMM : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "d_ldw_pre_imm\t$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isAsmParserOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
let isPseudo = 0 in
def D_LW_PRE_IMM : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "d_lw_pre_imm\t$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isAsmParserOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
let isPseudo = 0 in
def D_LHW_PRE_IMM : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "d_lhw_pre_imm\t$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def D_LDW_PRE_REG : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "d_ldw_pre_reg\t$rtd, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isAsmParserOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
let isPseudo = 0 in
def D_LW_PRE_REG : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "d_lw_pre_reg\t$rtd, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isAsmParserOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
let isPseudo = 0 in
def D_LHW_PRE_REG : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "d_lhw_pre_reg\t$rtd, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

// WITH_IMM / WITH_REG forms — offset addressing, NO base writeback.
// Single def ($rtd), no tied operand.
let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def D_LDW_WITH_IMM : HaydnInst<4, (outs DR64:$rtd),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "d_ldw_with_imm\t$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def D_LW_WITH_IMM : HaydnInst<4, (outs DR64:$rtd),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "d_lw_with_imm\t$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def D_LHW_WITH_IMM : HaydnInst<4, (outs DR64:$rtd),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "d_lhw_with_imm\t$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def D_LDW_WITH_REG : HaydnInst<4, (outs DR64:$rtd),
    (ins GPR32:$rs1, GPR32:$rs2),
    "d_ldw_with_reg\t$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def D_LW_WITH_REG : HaydnInst<4, (outs DR64:$rtd),
    (ins GPR32:$rs1, GPR32:$rs2),
    "d_lw_with_reg\t$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def D_LHW_WITH_REG : HaydnInst<4, (outs DR64:$rtd),
    (ins GPR32:$rs1, GPR32:$rs2),
    "d_lhw_with_reg\t$rtd, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

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
// 2467 which correctly carries the override. The slot map
// {DC_D1, Row 2, S0, opcode 1} in HaydnDClassOpcodes.cpp already covers this
// opcode, so emission is real once non-pseudo.
let isPseudo = 0, mayStore = 1,
    Constraints = "$rs = $rs_wb" in
def D_SDW_BREV_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_sdw_brev_imm	$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

// POST_IMM — DR64 data. DB: mem64[rs] = rtd; rs += imm6<<3.
// isPseudo=0 required: inside surrounding isPseudo=1 scope (C0.2 / C0.1
// WITH_* precedent) so AsmPrinter does not drop MCInst as Pseudo.
let isPseudo = 0, isCodeGenOnly = 1, mayStore = 1, Constraints = "$rs = $rs_wb" in
def D_SDW_POST_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_sdw_post_imm	$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let isPseudo = 0, isAsmParserOnly = 1, mayStore = 1, Constraints = "$rs = $rs_wb" in
def D_SHW_POST_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_shw_post_imm	$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let isPseudo = 0, isAsmParserOnly = 1, mayStore = 1, Constraints = "$rs = $rs_wb" in
def D_SW_H_POST_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_sw_h_post_imm	$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let isPseudo = 0, isAsmParserOnly = 1, mayStore = 1, Constraints = "$rs = $rs_wb" in
def D_SW_L_POST_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_sw_l_post_imm	$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

// PRE_IMM — update base, then store. DR64 data.
// isCodeGenOnly: selected by GISel update-addr fuse (pre-RA).
let isPseudo = 0, isCodeGenOnly = 1, mayStore = 1, Constraints = "$rs = $rs_wb" in
def D_SDW_PRE_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_sdw_pre_imm	$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

// isPseudo=0: inside isPseudo=1 scope — C0.2 emit path for public PRE stores.
let isPseudo = 0, isAsmParserOnly = 1, mayStore = 1, Constraints = "$rs = $rs_wb" in
def D_SHW_PRE_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_shw_pre_imm	$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let isPseudo = 0, isAsmParserOnly = 1, mayStore = 1, Constraints = "$rs = $rs_wb" in
def D_SW_H_PRE_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_sw_h_pre_imm	$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let isPseudo = 0, isAsmParserOnly = 1, mayStore = 1, Constraints = "$rs = $rs_wb" in
def D_SW_L_PRE_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_sw_l_pre_imm	$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

// WITH_IMM — offset addressing, NO base writeback. (outs) empty.
// D_SW_L/H_WITH_IMM are now defined in HaydnInstrInfo.td via
// FmtLaneStore (with real encoding bits) so codegen can emit them.
let isPseudo = 0, isAsmParserOnly = 1, mayStore = 1 in
def D_SDW_WITH_IMM : HaydnInst<4, (outs),
    (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_sdw_with_imm	$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let isPseudo = 0, isAsmParserOnly = 1, mayStore = 1 in
def D_SHW_WITH_IMM : HaydnInst<4, (outs),
    (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm),
    "d_shw_with_imm	$rtd, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def EXP2 : HaydnInst<4, (outs GPR32:$rd),
    (ins GPR32:$rs),
    "exp2	$rd, $rs", []>;

let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def LOG2 : HaydnInst<4, (outs GPR32:$rd),
    (ins GPR32:$rs),
    "log2	$rd, $rs", []>;

let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def MOVE64 : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "move64	$rd, $rs", []>;

// MOVEI_H and MOVEI_L (48-bit S-format, §4) RETIRED
// zero live C++ consumers subsequent (the 48-bit encoder/decoder paths were
// gutted; no Haydn::MOVEI_H/MOVEI_L enum ref survived in any.cpp/.h).
// Removed along with the Fmt48_MOVEI class (HaydnInstrFormatsC.td). The
// surrounding `let isPseudo = 1 in {... }` scope (opened at line 2715) is
// unaffected. The prior `let isPseudo = 0 in {... }` escape block is also
// removed (it existed only to host these two defs).


// MOVF64 / MOVT64 moved to HaydnInstrInfo.td (— real FmtALU64Unary
// encodings under opcode 0x67 funct 0x18A/0x18B). The HaydnInst<4> defs
// here were blanket pseudos with no encoding; AsmPrinter dropped them.

// NEG64 / NEG64S moved to HaydnInstrInfo.td (real FmtALU64Unary 0x67/0x1A9
// 0x1AA). Same EMPTY_SEL → emit fix as ABS64/ABS64S.

// NOT64 moved to HaydnInstrInfo.td (real FmtALU64Unary — was MCID::Pseudo
// HaydnInst shell that AsmPrinter dropped; slot members NOT64_S1/S2 live).

// R_GD family (FormatsALU64): outs GPR32:$rt, ins DR64:$rsd.
// Logical operand dag MUST match members so leaveRegion setDesc is legal.
// Prior stubs modeled GPR32 source (capstone drift); product members are
// DR64 source. Same shape as POPCOUNT64.
let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def NSA16_L : HaydnInst<4, (outs GPR32:$rd),
    (ins DR64:$rs),
    "nsa16_l	$rd, $rs", []>;

let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def NSA32_L : HaydnInst<4, (outs GPR32:$rd),
    (ins DR64:$rs),
    "nsa32_l	$rd, $rs", []>;

let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def NSA64 : HaydnInst<4, (outs GPR32:$rd),
    (ins DR64:$rs),
    "nsa64	$rd, $rs", []>;

let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def NSAZ16_L : HaydnInst<4, (outs GPR32:$rd),
    (ins DR64:$rs),
    "nsaz16_l	$rd, $rs", []>;

let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def NSAZ32_L : HaydnInst<4, (outs GPR32:$rd),
    (ins DR64:$rs),
    "nsaz32_l	$rd, $rs", []>;

let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def NSAZ64 : HaydnInst<4, (outs GPR32:$rd),
    (ins DR64:$rs),
    "nsaz64	$rd, $rs", []>;

// POPCOUNT64 — DB syntax `POPCOUNT64 rt, rsd` (GPR32 dest + DR64 source
// DB slots DR_Read=['rsd'] GPR_Write=['rt']). Stub previously modeled source
// as GPR32 (capstone drift cat 1). Fixed to DR64 source (R_GD).
let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def POPCOUNT64 : HaydnInst<4, (outs GPR32:$rd),
    (ins DR64:$rs),
    "popcount64	$rd, $rs", []>;

let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def RECIP : HaydnInst<4, (outs GPR32:$rd),
    (ins GPR32:$rs),
    "recip	$rd, $rs", []>;

// SEQ64 / SLE64 / SLT64 moved to HaydnInstrInfo.td (— real
// FmtALU64Unary encodings under opcode 0x67 funct 0x187/0x188/0x189).

// Auto-gen stub for the SEXT32T64 mnemonic — KEEPS isCodeGenOnly=1.
// The REAL instruction is SEXT_GPR32_TO_DR64 in HaydnInstrInfo.td :
// FmtALU64Unary with the correct `(outs DR64:$rd), (ins GPR32:$rs1)`
// operand types and a real encoding (opcode 0x67 funct 0x197), and it is
// NOT isCodeGenOnly so the AsmParser matcher + disassembler both see it.
//
// This stub's operand-type typo (GPR32:$rd instead of DR64:$rd) and absent
// encoding mean it MUST stay out of both the AsmParser matcher and the
// decoder. isPseudo=1 alone was NOT enough : it excludes the *decoder*
// but NOT the AsmParser's MatchInstructionImpl table. The first fix attempt
// put isCodeGenOnly=1 on BOTH this stub AND the real instruction — but that
// excluded the real instruction too, so `llvm-mc` had nothing to match
// `sext32t64 d0, r8` against and still failed. The correct split:
// real instruction NOT isCodeGenOnly (parseable + encodable), this stub
// isCodeGenOnly (excluded from matcher). AsmPrinter never emits this opcode
// (it emits SEXT_GPR32_TO_DR64), so MC encoding of the stub is irrelevant.
// Shape corrected to match the database and this def's OWN generated members
// (`SEXT32T64_P20_ALU0` is `(outs DR64:$rtd), (ins GPR32:$rs)`): the database
// says `rtd = SEXT32->64(rs)`, so the result is 64-bit. It was declared
// `(outs GPR32:$rd)` while it was an unreachable stub, which was harmless
// only because CodeGen emitted SEXT_GPR32_TO_DR64 instead. CodeGen now emits
// THIS, because it is the name the database gave members to.
let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def SEXT32T64 : HaydnInst<4, (outs DR64:$rtd),
    (ins GPR32:$rs),
    "sext32t64	$rtd, $rs", []>;

// SIN_COS — DB syntax `SIN_COS rtd, rs, uimm4` (DR64 dest + GPR32 src
// DB slots DR_Write=['rtd'] GPR_Read=['rs']). Stub previously modeled dest
// as GPR32 (capstone drift cat 2). Fixed to DR64 dest. Inert: no selector
// case, no intrinsic Pat. Stays isCodeGenOnly until a real encoding (M5).
let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def SIN_COS : HaydnInst<4, (outs DR64:$rd),
    (ins GPR32:$rs, simm16:$imm),
    "sin_cos	$rd, $rs, $imm", []>;



let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def SQRT : HaydnInst<4, (outs GPR32:$rd),
    (ins GPR32:$rs),
    "sqrt	$rd, $rs", []>;

let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def SRAI64R : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs, simm16:$imm),
    "srai64r	$rd, $rs, $imm", []>;

//===---------------------------------------------------------------===//
// Scalar GPR-data load family. DB dest is genuinely GPR32 ($rt), so
// the data out stays GPR32 — but the stubs were missing mayLoad, itinerary
// and (for POST/PRE) the tied base-writeback two-def. Rewritten to the DB
// contract: mayLoad, Slot0_LS itinerary, POST/PRE carry
// Constraints = "$rs = $rs_wb" with (outs GPR32:$rt, GPR32:$rs_wb). _REG
// POST/PRE forms name the base `$rs` inside the Constraints block (review
// §8.1.2). WITH forms have a single GPR32 def, no writeback.
//===---------------------------------------------------------------===//

// S_LBS (sign-extended byte). DB: rt = sext(mem8[rs]); POST: rs += imm6.
let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LBS_POST_IMM : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lbs_post_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LBS_POST_REG : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "s_lbs_post_reg\t$rt, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LBS_PRE_IMM : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lbs_pre_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LBS_PRE_REG : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "s_lbs_pre_reg\t$rt, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def S_LBS_WITH_IMM : HaydnInst<4, (outs GPR32:$rt),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lbs_with_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def S_LBS_WITH_REG : HaydnInst<4, (outs GPR32:$rt),
    (ins GPR32:$rs1, GPR32:$rs2),
    "s_lbs_with_reg\t$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

// S_LBU (zero-extended byte).
let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LBU_POST_IMM : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lbu_post_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LBU_POST_REG : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "s_lbu_post_reg\t$rt, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LBU_PRE_IMM : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lbu_pre_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LBU_PRE_REG : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "s_lbu_pre_reg\t$rt, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def S_LBU_WITH_IMM : HaydnInst<4, (outs GPR32:$rt),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lbu_with_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def S_LBU_WITH_REG : HaydnInst<4, (outs GPR32:$rt),
    (ins GPR32:$rs1, GPR32:$rs2),
    "s_lbu_with_reg\t$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

// S_LHWS (sign-extended halfword).
let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LHWS_POST_IMM : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lhws_post_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LHWS_POST_REG : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "s_lhws_post_reg\t$rt, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LHWS_PRE_IMM : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lhws_pre_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LHWS_PRE_REG : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "s_lhws_pre_reg\t$rt, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def S_LHWS_WITH_IMM : HaydnInst<4, (outs GPR32:$rt),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lhws_with_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def S_LHWS_WITH_REG : HaydnInst<4, (outs GPR32:$rt),
    (ins GPR32:$rs1, GPR32:$rs2),
    "s_lhws_with_reg\t$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

// S_LHWU (zero-extended halfword).
let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LHWU_POST_IMM : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lhwu_post_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LHWU_POST_REG : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "s_lhwu_post_reg\t$rt, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LHWU_PRE_IMM : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lhwu_pre_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LHWU_PRE_REG : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "s_lhwu_pre_reg\t$rt, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def S_LHWU_WITH_IMM : HaydnInst<4, (outs GPR32:$rt),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lhwu_with_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def S_LHWU_WITH_REG : HaydnInst<4, (outs GPR32:$rt),
    (ins GPR32:$rs1, GPR32:$rs2),
    "s_lhwu_with_reg\t$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

// S_LW_BREV — DB: rt = mem32[REVERSE32(rs)]; rs += imm6<<2. GPR32 dest + WB.
// (followup, SILENT WRONG-CODE class): `let isPseudo = 0` is
// REQUIRED because these defs sit inside the surrounding `let isPseudo = 1
// in {... }` scope opened at line 2413. Without it, AsmPrinter drops the
// MCInst (MCID::Pseudo) and the load has NO effect — the exact defect
// fixed for D_LDW_POST_IMM / S_LW_POST_IMM. Mirrors D_LDW_BREV_IMM/REG at
// lines 2467/2477 which correctly carry the override. The S_LW_BREV_IMM/REG
// forms have no dedicated slot-map entry in HaydnDClassOpcodes.cpp (unlike
// the D_LDW_BREV_* forms), so the encoder takes the legacy-flat path for
// single-child bundles and emits the TableGen encoding bits (0, like the
// working D_SDW_BREV_REG / S_SW_BREV_REG); the hardware opcode assignment
// for these LS bit-reversed forms is pending the encoding migration (M5).
let isPseudo = 0, mayLoad = 1,
    Constraints = "$rs = $rs_wb" in
def S_LW_BREV_IMM : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lw_brev_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, mayLoad = 1,
    Constraints = "$rs = $rs_wb" in
def S_LW_BREV_REG : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "s_lw_brev_reg\t$rt, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

// S_LW (word). DB: rt = mem32[rs]; POST: rs += imm6<<2.
// This reuses the existing LD32_POST/FmtLSPostInc encoding slot. It must be a
// real codegen-only instruction, not an asm-parser-only pseudo: AsmPrinter
// bundle emission skips pseudos, and TableGen does not emit an MC encoder case
// for asm-parser-only records.
let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1,
    Constraints = "$rs = $rs_wb" in
def S_LW_POST_IMM : FmtLSPostInc<0x48, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lw_post_imm\t$rt, $rs, $scaled_imm", []> {
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LW_POST_REG : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "s_lw_post_reg\t$rt, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

// isCodeGenOnly: GISel forms pre-inc/dec word loads.
let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LW_PRE_IMM : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lw_pre_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isCodeGenOnly = 1, mayLoad = 1, Constraints = "$rs = $rs_wb" in
def S_LW_PRE_REG : HaydnInst<4, (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, GPR32:$rs2),
    "s_lw_pre_reg\t$rt, $rs, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def S_LW_WITH_IMM : HaydnInst<4, (outs GPR32:$rt),
    (ins GPR32:$rs, simm6:$scaled_imm),
    "s_lw_with_imm\t$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

let isPseudo = 0, isAsmParserOnly = 1, mayLoad = 1 in
def S_LW_WITH_REG : HaydnInst<4, (outs GPR32:$rt),
    (ins GPR32:$rs1, GPR32:$rs2),
    "s_lw_with_reg\t$rt, $rs1, $rs2", []> {
  let TSFlags{2-0} = 0b011;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0LOAD1_L2;
}

//===---------------------------------------------------------------===//
// Scalar GPR-data store _IMM family (Shape B). GPR32 store data ($rt).
// DB contract: data is an INPUT (GPR32:$rt), only OUTPUT is the optional tied
// base writeback $rs_wb (POST/PRE) or empty (outs) (WITH), mayStore, Slot0_LS.
// The canonical S_SW_POST_IMM is the selector / HaydnExpandPostIncEarly target
// for fused GPR post-inc stores.
//===---------------------------------------------------------------===//

// POST_IMM — GPR32 data. DB: mem8[rs] = rt[7:0]; rs += imm6 (SB, no shift).
let isPseudo = 0, isCodeGenOnly = 1, mayStore = 1, Constraints = "$rs = $rs_wb" in
def S_SB_POST_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins GPR32:$rt, GPR32:$rs, simm6:$scaled_imm),
    "s_sb_post_imm	$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let isPseudo = 0, isCodeGenOnly = 1, mayStore = 1, Constraints = "$rs = $rs_wb" in
def S_SHW_POST_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins GPR32:$rt, GPR32:$rs, simm6:$scaled_imm),
    "s_shw_post_imm	$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

// DB: mem32[rs] = rt; rs += imm6<<2.
// isPseudo=0: inside isPseudo=1 scope — C0.2 emit path (S_SB/S_SHW peers).
let isPseudo = 0, isCodeGenOnly = 1, mayStore = 1, Constraints = "$rs = $rs_wb" in
def S_SW_POST_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins GPR32:$rt, GPR32:$rs, simm6:$scaled_imm),
    "s_sw_post_imm	$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

// PRE_IMM — update base, then store. GPR32 data.
let isPseudo = 0, isCodeGenOnly = 1, mayStore = 1, Constraints = "$rs = $rs_wb" in
def S_SB_PRE_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins GPR32:$rt, GPR32:$rs, simm6:$scaled_imm),
    "s_sb_pre_imm	$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let isPseudo = 0, isCodeGenOnly = 1, mayStore = 1, Constraints = "$rs = $rs_wb" in
def S_SHW_PRE_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins GPR32:$rt, GPR32:$rs, simm6:$scaled_imm),
    "s_shw_pre_imm	$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

// isCodeGenOnly: GISel forms pre-inc/dec word stores.
let isPseudo = 0, isCodeGenOnly = 1, mayStore = 1, Constraints = "$rs = $rs_wb" in
def S_SW_PRE_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins GPR32:$rt, GPR32:$rs, simm6:$scaled_imm),
    "s_sw_pre_imm	$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

// WITH_IMM — offset addressing, NO base writeback. (outs) empty.
let isPseudo = 0, isAsmParserOnly = 1, mayStore = 1 in
def S_SB_WITH_IMM : HaydnInst<4, (outs),
    (ins GPR32:$rt, GPR32:$rs, simm6:$scaled_imm),
    "s_sb_with_imm	$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let isPseudo = 0, isAsmParserOnly = 1, mayStore = 1 in
def S_SHW_WITH_IMM : HaydnInst<4, (outs),
    (ins GPR32:$rt, GPR32:$rs, simm6:$scaled_imm),
    "s_shw_with_imm	$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

let isPseudo = 0, isAsmParserOnly = 1, mayStore = 1 in
def S_SW_WITH_IMM : HaydnInst<4, (outs),
    (ins GPR32:$rt, GPR32:$rs, simm6:$scaled_imm),
    "s_sw_with_imm	$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

// BREV store — DB: mem32[REVERSE32(rs)] = rt; rs += imm6<<2. GPR32 data, WB.
// (followup, SILENT WRONG-CODE class): `let isPseudo = 0` is
// REQUIRED because this def sits inside the surrounding `let isPseudo = 1
// in {... }` scope opened at line 2413. Without it, AsmPrinter drops the
// MCInst (MCID::Pseudo) and the store has NO effect — the exact defect
// fixed for D_LDW_POST_IMM / S_LW_POST_IMM. Mirrors D_LDW_BREV_IMM at line
// 2467 which correctly carries the override. S_SW_BREV_IMM has no dedicated
// slot-map entry in HaydnDClassOpcodes.cpp (same as the working
// S_SW_BREV_REG), so the encoder takes the legacy-flat path for single-child
// bundles; the hardware opcode assignment is pending the M5 encoding migration.
let isPseudo = 0, mayStore = 1,
    Constraints = "$rs = $rs_wb" in
def S_SW_BREV_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins GPR32:$rt, GPR32:$rs, simm6:$scaled_imm),
    "s_sw_brev_imm	$rt, $rs, $scaled_imm", []> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
}

// TRANSF64 / TRANSF64F2 — DB syntax `TRANSF64 rtd, rs` (DR64 dest + GPR32
// src, per DB slots DR_Write=['rtd'] GPR_Read=['rs']). The stub below
// previously modeled both operands as GPR32 (capstone drift cat 1/2). Fixed
// to match the DB. Inert placeholder: no selector (HaydnInstructionSelector
// has no case), no intrinsic Pat in HaydnIntrinsics.td. Stays isCodeGenOnly
// until a real encoding lands (M5).
let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def TRANSF64 : HaydnInst<4, (outs DR64:$rd),
    (ins GPR32:$rs),
    "transf64	$rd, $rs", []>;

let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def TRANSF64F2 : HaydnInst<4, (outs DR64:$rd),
    (ins GPR32:$rs),
    "transf64f2	$rd, $rs", []>;

let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def TRANSF64_H : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "transf64_h	$rd, $rs", []>;

let TSFlags = 14, isCodeGenOnly = 1 in
let isPseudo = 0 in
def TRANSF64_L : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "transf64_l	$rd, $rs", []>;

// X2ABS32 / X2ABS32S / X2NEG32 / X2NEG32S / X2NEG32_L / X2NEG32S_L moved to
// HaydnInstrInfo.td (FmtALU64Unary 0x67/0x18C + 0x1A2..0x1A6). Flex _S1/_S2
// forms remain in HaydnFormatsALU64.td (golden Mode-0 opcodes 0x51..0x56).

let TSFlags = 14 in
let isPseudo = 0 in
def X2HADD32S_H : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x2hadd32s_h	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X2HADD32S_L : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x2hadd32s_l	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X2HADD32_H : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x2hadd32_h	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X2HADD32_L : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x2hadd32_l	$rd, $rs", []>;

// X2HMAX/X2HMIN — R_GD members (FormatsALU64): outs GPR32 / ins DR64.
// Prior logical used DR64 dest (R_DD drift); setDesc then aborted
// '$d0 is not a GPR32'. Match members so alts leaveRegion is legal.
let TSFlags = 14 in
let isPseudo = 0 in
def X2HMAX32 : HaydnInst<4, (outs GPR32:$rd),
    (ins DR64:$rs),
    "x2hmax32	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X2HMIN32 : HaydnInst<4, (outs GPR32:$rd),
    (ins DR64:$rs),
    "x2hmin32	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X2MJSWAP32 : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x2mjswap32	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X2MJSWAP32S : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x2mjswap32s	$rd, $rs", []>;

// X2MOVF32 / X2MOVT32 moved to HaydnInstrInfo.td (— FmtALU64Binary
// 0x67/0x190, 0x67/0x191).

// X2NEG* base stubs removed — see note above (real FmtALU64Unary in
// HaydnInstrInfo.td).

// X2SEQ32 / X2SLE32 / X2SLT32 moved to HaydnInstrInfo.td.
// X2SLLI32 / X2SRAI32 / X2SRAI32R / X2SRLI32 moved to HaydnInstrInfo.td
// (— FmtALU64ShiftImm encoding, retiring the AsmPrinter hack).

let TSFlags = 14 in
let isPseudo = 0 in
def X2SWAP32 : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x2swap32	$rd, $rs", []>;

// X4ABS16 / X4ABS16S promoted to real FmtALU64Unary in HaydnInstrInfo.td
// (same EMPTY_SEL / AsmPrinter MCID::Pseudo drop as ABS64/X2ABS32S).
// Slot forms remain X4ABS16{,S}_S1/_S2 in HaydnFormatsALU64.td.

// X4CMUL16 / X4CMUL16S / X4CMUL16_F2 / X4CMUL16S_F2 promoted to real
// FmtALU64Unary in HaydnInstrInfo.td (same EMPTY_SEL / AsmPrinter MCID::Pseudo
// drop as X4ABS16 / ABS64 / X2ABS32S). Slot forms remain X4CMUL16*_S1/_S2 in
// HaydnFormatsMAC.td.

let TSFlags = 14 in
let isPseudo = 0 in
def X4CONJ16 : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x4conj16	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X4CONJ16S : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x4conj16s	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X4ENERGY16 : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x4energy16	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X4HADD16_H : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x4hadd16_h	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X4HADD16_L : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x4hadd16_l	$rd, $rs", []>;

let TSFlags = 14 in
// X4HMAX/X4HMIN — same R_GD shape as X2HMAX (GPR32 dest, DR64 src).
let isPseudo = 0 in
def X4HMAX16 : HaydnInst<4, (outs GPR32:$rd),
    (ins DR64:$rs),
    "x4hmax16	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X4HMIN16 : HaydnInst<4, (outs GPR32:$rd),
    (ins DR64:$rs),
    "x4hmin16	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X4MJSWAP16 : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x4mjswap16	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X4MJSWAP16S : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x4mjswap16s	$rd, $rs", []>;

// X4MOVF16 / X4MOVT16 / X4SELI16 / X4SEQ16 / X4SLE16 / X4SLT16 moved to
// HaydnInstrInfo.td (— FmtALU64Binary / FmtALU64SelImm encodings).
// X4SLLI16 / X4SRAI16 / X4SRAI16R / X4SRLI16 moved to HaydnInstrInfo.td
// (— FmtALU64ShiftImm encoding, retiring the AsmPrinter hack).

let TSFlags = 14 in
let isPseudo = 0 in
def X4NEG16 : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x4neg16	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X4NEG16S : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x4neg16s	$rd, $rs", []>;

let TSFlags = 14 in
let isPseudo = 0 in
def X4SWAP16 : HaydnInst<4, (outs DR64:$rd),
    (ins DR64:$rs),
    "x4swap16	$rd, $rs", []>;

let TSFlags = 14 in
// `ZERO_DR rtd` writes zero and takes no input; the immediate was spurious
// and gave CodeGen an operand the member does not have (§ 5.11).
let isPseudo = 0 in
def ZERO_DR : HaydnInst<4, (outs DR64:$rd), (ins),
    "zero_dr	$rd", []>;

// MOVESFR2GPR / MOVEGPR2SFR / ZERO_SFR moved to HaydnInstrInfo.td (
// real 32-bit R-type encodings under opcode 0x67 funct 0x198/0x199/0x19A
// with explicit Inst{...}=... assignments for the unusual operand shapes).

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
def D_LDW_CB_IMM : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs_wb),
    (ins GPR32:$rs, i32imm:$cbr_sel, simm16:$imm),
    "d_ldw_cb_imm\t$rtd, $cbr_sel, $rs, $imm", []> {
  let Constraints = "$rs = $rs_wb";
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L2;
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
def D_LDW_CB_REG : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs1_wb),
    (ins GPR32:$rs1, i32imm:$cbr_sel, GPR32:$rs2),
    "d_ldw_cb_reg\t$rtd, $cbr_sel, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs1_wb";
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L2;
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
// Operands follow the database Syntax — `D_SDW_CB_IMM cbr_sel, ...` — so the
// selector immediate leads. It used to sit third, which put an
// immediate where the member has a register (§ 5.11 `kinds`).
def D_SDW_CB_IMM : HaydnInst<4, (outs GPR32:$rs_wb),
    (ins i32imm:$cbr_sel, DR64:$rtd, GPR32:$rs, simm16:$imm),
    "d_sdw_cb_imm\t$cbr_sel, $rtd, $rs, $imm", []> {
  let Constraints = "$rs = $rs_wb";
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
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
// Operands follow the database Syntax — `D_SDW_CB_REG cbr_sel, ...` — so the
// selector immediate leads. It used to sit third, which put an
// immediate where the member has a register (§ 5.11 `kinds`).
def D_SDW_CB_REG : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins i32imm:$cbr_sel, DR64:$rtd, GPR32:$rs1, GPR32:$rs2),
    "d_sdw_cb_reg\t$cbr_sel, $rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs1_wb";
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Unit_LS0_L1;
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