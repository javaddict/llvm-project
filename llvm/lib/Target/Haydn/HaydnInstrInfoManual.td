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
// Cursor model (supersedes; corrected 2026-08-19): the CB instruction DOES
// compute the next pointer in hardware (rs = rs + stride, wrapped at CBR
// bounds) and the MI-level defs below DO model that update as a tied
// writeback ($rs = $rs_wb — AGU base update, same law as POST/PRE/BREV
// peers and the generated CB defs in HaydnInstrInfoGolden.td.inc). The
// *intrinsic-level* API remains data-only: no updated-ptr output is exposed
// to C, the stride is a tracked value (index-derived, supplied per call),
// and the pointer persists across hardware-loop iterations (body opaque to
// the optimizer, so the unexposed update is correct at the source level).
// CBR is a boundary guard, not a value the program reads back. The only
// path to *convert* plain-C to CB is
// the Phase-C detection pass (modular arithmetic on an index-derived ptr).
//===----------------------------------------------------------------------===//

// NOTE (2026-08-19 audit): golden v2_1 places D_LDW_CB_{IMM,REG} on
// LOADSTORE0 ONLY (layout entry0, both modes), so the generated peer
// D_LDW_CB_REG correctly carries Slot0_LS. This hand D_LDW_CB_IMM keeps
// Slot01_LD as a measured SMS exception: Slot0_LS inflated the DFA ResMII
// for CB-load loops (bkfir ResMII 10 vs HiFi 8) because the ResMII model
// read the itinerary as exclusive S0 while the PlacementAlternative path
// allows broader issue. Resolving the discrepancy belongs to the scheduling
// model (GOALS M18/GE96-04: admitted per-op records consumed by ALL
// scheduling paths), not to widening golden placements. If the unified
// scheduler model proves Slot0_LS-equivalent ResMII, this hand def and its
// Slot01_LD exception should be deleted in favor of the generated def.
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


// Total after S2b wave-2 migration: 19 hand-kept defs.
} // isCodeGenOnly: hypothesized Auto encodings quarantined from MC
