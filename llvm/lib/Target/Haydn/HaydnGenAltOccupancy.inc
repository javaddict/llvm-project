//===-- HaydnGenAltOccupancy.inc -*- C++ -*-===//
//
// DO NOT EDIT.
// Generator: llvm/lib/Target/Haydn/FormatE/generate_alt_occupancy.py
// Schema: format-e-records/1
// Family: e96 (id=0)
// Check: python3 llvm/lib/Target/Haydn/FormatE/generate_alt_occupancy.py --check --family e96
// Umbrella: llvm/lib/Target/Haydn/FormatE/check_generated.sh
// Overlay: authored_catalog_overlay.json
// Overlay-SHA256: 276dea569f8761e81bb3d17e041a4f74911d34ba6f30711939a293519ac531fc
// Index-SHA256: 3f306463d108c8240b6f8afa876e3fa4ca06b91ee7f38efe1fb4eba631c3433b
//
// Residual SLOT0/1/2 occupancy for compiler-reachable logicals.
// Mask bit i is residual occupancy class i. Fallback is 0;
// Format E members fill residual indices.
// Do not derive Mask from Format E EntryIdx.
// X2*/X4* ALU masks follow golden Available ALU0|ALU1|ALU2 (0x7);
// MAC-family X2*/X4* stay MAC0|MAC1 (0x6).
// SEXT32T64 residual is 0x7 (golden ALU0|ALU1|ALU2); other *64
// Slot012_ALU shells keep residual S1|S2 (0x6).
//
//===----------------------------------------------------------------------===//

#ifdef GET_HAYDN_ALT_OCCUPANCY
#undef GET_HAYDN_ALT_OCCUPANCY

struct HaydnAltOccupancyRow {
  unsigned LogicalOpc;
  uint8_t Mask;
  unsigned Fallback[3];
};

static const HaydnAltOccupancyRow HaydnAltOccupancy[] = {
    { Haydn::ADD32_MSP, 0x7, { 0, 0, 0 } },
    { Haydn::ABS32, 0x7, { 0, 0, 0 } },
    { Haydn::ABS32S, 0x7, { 0, 0, 0 } },
    { Haydn::ABS64, 0x6, { 0, 0, 0 } },
    { Haydn::ABS64S, 0x6, { 0, 0, 0 } },
    { Haydn::ADD32, 0x7, { 0, 0, 0 } },
    { Haydn::ADD32S, 0x7, { 0, 0, 0 } },
    { Haydn::ADD64, 0x6, { 0, 0, 0 } },
    { Haydn::ADD64S, 0x6, { 0, 0, 0 } },
    { Haydn::ADD64S_H, 0x6, { 0, 0, 0 } },
    { Haydn::ADD64S_L, 0x6, { 0, 0, 0 } },
    { Haydn::ADD64_H, 0x6, { 0, 0, 0 } },
    { Haydn::ADD64_L, 0x6, { 0, 0, 0 } },
    { Haydn::ADDI32, 0x7, { 0, 0, 0 } },
    { Haydn::ADDI32S, 0x7, { 0, 0, 0 } },
    { Haydn::ADDI32_W, 0x1, { 0, 0, 0 } },
    { Haydn::AND32, 0x7, { 0, 0, 0 } },
    { Haydn::AND64, 0x6, { 0, 0, 0 } },
    { Haydn::ANDI32, 0x7, { 0, 0, 0 } },
    { Haydn::ARCTAN, 0x6, { 0, 0, 0 } },
    { Haydn::ASR32, 0x7, { 0, 0, 0 } },
    { Haydn::BEQ, 0x1, { 0, 0, 0 } },
    { Haydn::BEQZ, 0x1, { 0, 0, 0 } },
    { Haydn::BEQZ_W, 0x1, { 0, 0, 0 } },
    { Haydn::BEQ_W, 0x1, { 0, 0, 0 } },
    { Haydn::BGE, 0x1, { 0, 0, 0 } },
    { Haydn::BGEU, 0x1, { 0, 0, 0 } },
    { Haydn::BGEU_W, 0x1, { 0, 0, 0 } },
    { Haydn::BGEZ, 0x1, { 0, 0, 0 } },
    { Haydn::BGEZ_W, 0x1, { 0, 0, 0 } },
    { Haydn::BGE_W, 0x1, { 0, 0, 0 } },
    { Haydn::BLT, 0x1, { 0, 0, 0 } },
    { Haydn::BLTU, 0x1, { 0, 0, 0 } },
    { Haydn::BLTU_W, 0x1, { 0, 0, 0 } },
    { Haydn::BLTZ, 0x1, { 0, 0, 0 } },
    { Haydn::BLTZ_W, 0x1, { 0, 0, 0 } },
    { Haydn::BLT_W, 0x1, { 0, 0, 0 } },
    { Haydn::BNE, 0x1, { 0, 0, 0 } },
    { Haydn::BNEZ, 0x1, { 0, 0, 0 } },
    { Haydn::BNEZ_W, 0x1, { 0, 0, 0 } },
    { Haydn::BNE_W, 0x1, { 0, 0, 0 } },
    { Haydn::BREV32, 0x7, { 0, 0, 0 } },
    { Haydn::CSRR, 0x1, { 0, 0, 0 } },
    { Haydn::CSRW, 0x1, { 0, 0, 0 } },
    { Haydn::CSRW_W, 0x1, { 0, 0, 0 } },
    { Haydn::D_LDW_BREV_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_LDW_BREV_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_LDW_CB_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_LDW_CB_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_LDW_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_LDW_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_LDW_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_LDW_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_LDW_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_LDW_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_LHW_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_LHW_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_LHW_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_LHW_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_LHW_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_LHW_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_LQHWUA_CB_POST, 0x7, { 0, 0, 0 } },
    { Haydn::D_LQHWUA_POST, 0x7, { 0, 0, 0 } },
    { Haydn::D_LTWUA_CB_POST, 0x7, { 0, 0, 0 } },
    { Haydn::D_LTWUA_POST, 0x7, { 0, 0, 0 } },
    { Haydn::D_LW_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_LW_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_LW_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_LW_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_LW_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_LW_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SDW_BREV_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SDW_BREV_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SDW_CB_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SDW_CB_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SDW_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SDW_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SDW_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SDW_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SDW_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SDW_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SHW_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SHW_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SHW_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SHW_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SHW_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SHW_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SQHWUA_CB_POST, 0x7, { 0, 0, 0 } },
    { Haydn::D_SQHWUA_POST, 0x7, { 0, 0, 0 } },
    { Haydn::D_STWUA_CB_POST, 0x7, { 0, 0, 0 } },
    { Haydn::D_STWUA_POST, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_F64RS_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_F64RS_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_F64RS_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_F64RS_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_H_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_H_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_H_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_H_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_H_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_H_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_L_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_L_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_L_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_L_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_L_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::D_SW_L_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::EXP2, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULAA32RS_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULAA32RS_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULAA32R_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULAA32R_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULAS32RS_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULAS32RS_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULAS32R_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULAS32R_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULSA32RS_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULSA32RS_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULSA32R_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULSA32R_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULSS32RS_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULSS32RS_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULSS32R_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULSS32R_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZAA32RS_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZAA32RS_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZAA32R_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZAA32R_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZAS32RS_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZAS32RS_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZAS32R_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZAS32R_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZSA32RS_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZSA32RS_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZSA32R_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZSA32R_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZSS32RS_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZSS32RS_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZSS32R_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::F2MULZSS32R_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MUL32RS_HH, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MUL32RS_LH, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MUL32RS_LL, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MUL32R_HH, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MUL32R_LH, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MUL32R_LL, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MULA32RS_HH, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MULA32RS_LH, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MULA32RS_LL, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MULA32R_HH, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MULA32R_LH, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MULA32R_LL, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MULS32RS_HH, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MULS32RS_LH, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MULS32RS_LL, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MULS32R_HH, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MULS32R_LH, 0x6, { 0, 0, 0 } },
    { Haydn::FF2MULS32R_LL, 0x6, { 0, 0, 0 } },
    { Haydn::FLAR, 0x7, { 0, 0, 0 } },
    { Haydn::FMUL16_HS00, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_HS01, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_HS02, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_HS03, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_HS11, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_HS12, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_HS13, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_HS22, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_HS23, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_HS33, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_LS00, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_LS01, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_LS02, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_LS03, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_LS11, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_LS12, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_LS13, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_LS22, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_LS23, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL16_LS33, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL32S_HH, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL32S_LH, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL32S_LL, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL32X16_H0, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL32X16_H1, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL32X16_H2, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL32X16_H3, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL32X16_L0, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL32X16_L1, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL32X16_L2, 0x6, { 0, 0, 0 } },
    { Haydn::FMUL32X16_L3, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_HS00, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_HS01, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_HS02, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_HS03, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_HS11, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_HS12, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_HS13, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_HS22, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_HS23, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_HS33, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_LS00, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_LS01, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_LS02, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_LS03, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_LS11, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_LS12, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_LS13, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_LS22, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_LS23, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA16_LS33, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA32S_HH, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA32S_LH, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA32S_LL, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA32X16_H0, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA32X16_H1, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA32X16_H2, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA32X16_H3, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA32X16_L0, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA32X16_L1, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA32X16_L2, 0x6, { 0, 0, 0 } },
    { Haydn::FMULA32X16_L3, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAA16_HS_11_00, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAA16_HS_13_02, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAA16_HS_33_22, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAA16_LS_11_00, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAA16_LS_13_02, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAA16_LS_33_22, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAA32S_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAA32S_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAA32X16_H0_L1, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAA32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAA32X16_H2_L3, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAA32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAS32S_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAS32S_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAS32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::FMULAS32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_HS00, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_HS01, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_HS02, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_HS03, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_HS11, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_HS12, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_HS13, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_HS22, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_HS23, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_HS33, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_LS00, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_LS01, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_LS02, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_LS03, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_LS11, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_LS12, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_LS13, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_LS22, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_LS23, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS16_LS33, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS32S_HH, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS32S_LH, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS32S_LL, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS32X16_H0, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS32X16_H1, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS32X16_H2, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS32X16_H3, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS32X16_L0, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS32X16_L1, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS32X16_L2, 0x6, { 0, 0, 0 } },
    { Haydn::FMULS32X16_L3, 0x6, { 0, 0, 0 } },
    { Haydn::FMULSA32S_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::FMULSA32S_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::FMULSA32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::FMULSA32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::FMULSS16_HS_11_00, 0x6, { 0, 0, 0 } },
    { Haydn::FMULSS16_HS_13_02, 0x6, { 0, 0, 0 } },
    { Haydn::FMULSS16_HS_33_22, 0x6, { 0, 0, 0 } },
    { Haydn::FMULSS16_LS_11_00, 0x6, { 0, 0, 0 } },
    { Haydn::FMULSS16_LS_13_02, 0x6, { 0, 0, 0 } },
    { Haydn::FMULSS16_LS_33_22, 0x6, { 0, 0, 0 } },
    { Haydn::FMULSS32S_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::FMULSS32S_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::FMULSS32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::FMULSS32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAA16_HS_11_00, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAA16_HS_13_02, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAA16_HS_33_22, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAA16_LS_11_00, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAA16_LS_13_02, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAA16_LS_33_22, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAA32S_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAA32S_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAA32X16_H0_L1, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAA32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAA32X16_H2_L3, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAA32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAS32S_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAS32S_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAS32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZAS32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZSA32S_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZSA32S_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZSA32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZSA32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZSS16_HS_11_00, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZSS16_HS_13_02, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZSS16_HS_33_22, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZSS16_LS_11_00, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZSS16_LS_13_02, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZSS16_LS_33_22, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZSS32S_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZSS32S_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZSS32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::FMULZSS32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::JAL, 0x1, { 0, 0, 0 } },
    { Haydn::JALR, 0x1, { 0, 0, 0 } },
    { Haydn::JALR_W, 0x1, { 0, 0, 0 } },
    { Haydn::JAL_W, 0x1, { 0, 0, 0 } },
    { Haydn::LD16, 0x1, { 0, 0, 0 } },
    { Haydn::LD32, 0x3, { 0, 0, 0 } },
    { Haydn::LD32_POST, 0x2, { 0, 0, 0 } },
    { Haydn::LD32_REG_M0S0LS, 0x1, { 0, 0, 0 } },
    { Haydn::LD64, 0x3, { 0, 0, 0 } },
    { Haydn::LD64_POST, 0x2, { 0, 0, 0 } },
    { Haydn::LD64_REG_M0S0LS, 0x1, { 0, 0, 0 } },
    { Haydn::LD8, 0x1, { 0, 0, 0 } },
    { Haydn::LDU16, 0x1, { 0, 0, 0 } },
    { Haydn::LDU8, 0x1, { 0, 0, 0 } },
    { Haydn::LOG2, 0x6, { 0, 0, 0 } },
    { Haydn::LSR32, 0x7, { 0, 0, 0 } },
    { Haydn::LUI, 0x1, { 0, 0, 0 } },
    { Haydn::MAX32, 0x7, { 0, 0, 0 } },
    { Haydn::MAX64, 0x6, { 0, 0, 0 } },
    { Haydn::MAXU32, 0x7, { 0, 0, 0 } },
    { Haydn::MIN32, 0x7, { 0, 0, 0 } },
    { Haydn::MIN64, 0x6, { 0, 0, 0 } },
    { Haydn::MINU32, 0x7, { 0, 0, 0 } },
    { Haydn::MOVE32, 0x7, { 0, 0, 0 } },
    { Haydn::MOVE32_DR_H, 0x7, { 0, 0, 0 } },
    { Haydn::MOVE32_DR_L, 0x7, { 0, 0, 0 } },
    { Haydn::MOVE64, 0x6, { 0, 0, 0 } },
    { Haydn::MOVEGPR2SFR, 0x6, { 0, 0, 0 } },
    { Haydn::MOVEI_H, 0x1, { 0, 0, 0 } },
    { Haydn::MOVEI_L, 0x1, { 0, 0, 0 } },
    { Haydn::MOVESFR2GPR, 0x6, { 0, 0, 0 } },
    { Haydn::MOVF32, 0x7, { 0, 0, 0 } },
    { Haydn::MOVF64, 0x6, { 0, 0, 0 } },
    { Haydn::MOVT32, 0x7, { 0, 0, 0 } },
    { Haydn::MOVT64, 0x6, { 0, 0, 0 } },
    { Haydn::MUL16AQ, 0x6, { 0, 0, 0 } },
    { Haydn::MUL16ZAQ, 0x6, { 0, 0, 0 } },
    { Haydn::MUL32X16_H0, 0x6, { 0, 0, 0 } },
    { Haydn::MUL32X16_H1, 0x6, { 0, 0, 0 } },
    { Haydn::MUL32X16_H2, 0x6, { 0, 0, 0 } },
    { Haydn::MUL32X16_H3, 0x6, { 0, 0, 0 } },
    { Haydn::MUL32X16_L0, 0x6, { 0, 0, 0 } },
    { Haydn::MUL32X16_L1, 0x6, { 0, 0, 0 } },
    { Haydn::MUL32X16_L2, 0x6, { 0, 0, 0 } },
    { Haydn::MUL32X16_L3, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_HH, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_HL, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_HUH, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_HUL, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_LH, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_LL, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_LUH, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_LUL, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_UHH, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_UHL, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_UHUH, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_UHUL, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_ULH, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_ULL, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_ULUH, 0x6, { 0, 0, 0 } },
    { Haydn::MUL64_ULUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULA32X16_H0, 0x6, { 0, 0, 0 } },
    { Haydn::MULA32X16_H1, 0x6, { 0, 0, 0 } },
    { Haydn::MULA32X16_H2, 0x6, { 0, 0, 0 } },
    { Haydn::MULA32X16_H3, 0x6, { 0, 0, 0 } },
    { Haydn::MULA32X16_L0, 0x6, { 0, 0, 0 } },
    { Haydn::MULA32X16_L1, 0x6, { 0, 0, 0 } },
    { Haydn::MULA32X16_L2, 0x6, { 0, 0, 0 } },
    { Haydn::MULA32X16_L3, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_HH, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_HL, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_HUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_HUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_LH, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_LL, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_LUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_LUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_UHH, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_UHL, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_UHUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_UHUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_ULH, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_ULL, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_ULUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULA64_ULUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULAA32X16_H0_L1, 0x6, { 0, 0, 0 } },
    { Haydn::MULAA32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::MULAA32X16_H2_L3, 0x6, { 0, 0, 0 } },
    { Haydn::MULAA32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::MULAA32_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::MULAA32_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS32_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS32_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_HH, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_HL, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_HUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_HUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_LH, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_LL, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_LUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_LUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_UHH, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_UHL, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_UHUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_UHUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_ULH, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_ULL, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_ULUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULAS64_ULUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULL, 0x6, { 0, 0, 0 } },
    { Haydn::MULS32X16_H0, 0x6, { 0, 0, 0 } },
    { Haydn::MULS32X16_H1, 0x6, { 0, 0, 0 } },
    { Haydn::MULS32X16_H2, 0x6, { 0, 0, 0 } },
    { Haydn::MULS32X16_H3, 0x6, { 0, 0, 0 } },
    { Haydn::MULS32X16_L0, 0x6, { 0, 0, 0 } },
    { Haydn::MULS32X16_L1, 0x6, { 0, 0, 0 } },
    { Haydn::MULS32X16_L2, 0x6, { 0, 0, 0 } },
    { Haydn::MULS32X16_L3, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_HH, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_HL, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_HUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_HUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_LH, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_LL, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_LUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_LUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_UHH, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_UHL, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_UHUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_UHUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_ULH, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_ULL, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_ULUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULS64_ULUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULSA32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::MULSA32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::MULSA32_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::MULSA32_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS32_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS32_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_HH, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_HL, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_HUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_HUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_LH, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_LL, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_LUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_LUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_UHH, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_UHL, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_UHUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_UHUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_ULH, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_ULL, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_ULUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULSS64_ULUL, 0x6, { 0, 0, 0 } },
    { Haydn::MULSSH, 0x6, { 0, 0, 0 } },
    { Haydn::MULSUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULUUH, 0x6, { 0, 0, 0 } },
    { Haydn::MULZAA32X16_H0_L1, 0x6, { 0, 0, 0 } },
    { Haydn::MULZAA32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::MULZAA32X16_H2_L3, 0x6, { 0, 0, 0 } },
    { Haydn::MULZAA32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::MULZAA32_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::MULZAA32_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::MULZAS32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::MULZAS32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::MULZAS32_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::MULZAS32_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::MULZSA32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::MULZSA32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::MULZSA32_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::MULZSA32_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::MULZSS32X16_H1_L0, 0x6, { 0, 0, 0 } },
    { Haydn::MULZSS32X16_H3_L2, 0x6, { 0, 0, 0 } },
    { Haydn::MULZSS32_HHLL, 0x6, { 0, 0, 0 } },
    { Haydn::MULZSS32_HLLH, 0x6, { 0, 0, 0 } },
    { Haydn::NEG32, 0x7, { 0, 0, 0 } },
    { Haydn::NEG32S, 0x7, { 0, 0, 0 } },
    { Haydn::NEG64, 0x6, { 0, 0, 0 } },
    { Haydn::NEG64S, 0x6, { 0, 0, 0 } },
    { Haydn::NOP, 0x1, { 0, 0, 0 } },
    { Haydn::NOT32, 0x7, { 0, 0, 0 } },
    { Haydn::NOT64, 0x6, { 0, 0, 0 } },
    { Haydn::NSA16_L, 0x6, { 0, 0, 0 } },
    { Haydn::NSA32, 0x7, { 0, 0, 0 } },
    { Haydn::NSA32_L, 0x6, { 0, 0, 0 } },
    { Haydn::NSA64, 0x6, { 0, 0, 0 } },
    { Haydn::NSAU32, 0x7, { 0, 0, 0 } },
    { Haydn::NSAZ16_L, 0x6, { 0, 0, 0 } },
    { Haydn::NSAZ32_L, 0x6, { 0, 0, 0 } },
    { Haydn::NSAZ64, 0x6, { 0, 0, 0 } },
    { Haydn::OR32, 0x7, { 0, 0, 0 } },
    { Haydn::OR64, 0x6, { 0, 0, 0 } },
    { Haydn::ORI32, 0x7, { 0, 0, 0 } },
    { Haydn::ORI32_W, 0x1, { 0, 0, 0 } },
    { Haydn::PLDWWUA, 0x7, { 0, 0, 0 } },
    { Haydn::PLDWWUA_POST, 0x7, { 0, 0, 0 } },
    { Haydn::PLQHWUA_CB_POST, 0x7, { 0, 0, 0 } },
    { Haydn::PLTWWUA_CB_POST, 0x7, { 0, 0, 0 } },
    { Haydn::POPCOUNT32, 0x7, { 0, 0, 0 } },
    { Haydn::POPCOUNT64, 0x6, { 0, 0, 0 } },
    { Haydn::RECIP, 0x6, { 0, 0, 0 } },
    { Haydn::SEQ32, 0x7, { 0, 0, 0 } },
    { Haydn::SEQ64, 0x6, { 0, 0, 0 } },
    { Haydn::SET_HWLOOP_F2, 0x1, { 0, 0, 0 } },
    { Haydn::SET_HWLOOP_F2_W, 0x1, { 0, 0, 0 } },
    { Haydn::SET_HWLOOP_REG_W, 0x1, { 0, 0, 0 } },
    { Haydn::SET_HWLOOP_W, 0x1, { 0, 0, 0 } },
    { Haydn::SEXT32T64, 0x7, { 0, 0, 0 } },
    { Haydn::SEXT_GPR32_TO_DR64, 0x2, { 0, 0, 0 } },
    { Haydn::SHL32, 0x7, { 0, 0, 0 } },
    { Haydn::SIN_COS, 0x6, { 0, 0, 0 } },
    { Haydn::SLE32, 0x7, { 0, 0, 0 } },
    { Haydn::SLE64, 0x6, { 0, 0, 0 } },
    { Haydn::SLL32, 0x7, { 0, 0, 0 } },
    { Haydn::SLL64, 0x6, { 0, 0, 0 } },
    { Haydn::SLLI32, 0x7, { 0, 0, 0 } },
    { Haydn::SLLI64, 0x6, { 0, 0, 0 } },
    { Haydn::SLT32, 0x7, { 0, 0, 0 } },
    { Haydn::SLT64, 0x6, { 0, 0, 0 } },
    { Haydn::SLTU32, 0x7, { 0, 0, 0 } },
    { Haydn::SMULA16S_00, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16S_10, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16S_11, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16S_20, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16S_21, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16S_22, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16S_30, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16S_31, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16S_32, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16S_33, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16_00, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16_10, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16_11, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16_20, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16_21, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16_22, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16_30, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16_31, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16_32, 0x6, { 0, 0, 0 } },
    { Haydn::SMULA16_33, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16S_00, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16S_10, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16S_11, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16S_20, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16S_21, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16S_22, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16S_30, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16S_31, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16S_32, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16S_33, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16_00, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16_10, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16_11, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16_20, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16_21, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16_22, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16_30, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16_31, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16_32, 0x6, { 0, 0, 0 } },
    { Haydn::SMULS16_33, 0x6, { 0, 0, 0 } },
    { Haydn::SQRT, 0x6, { 0, 0, 0 } },
    { Haydn::SRA32, 0x7, { 0, 0, 0 } },
    { Haydn::SRA32R, 0x7, { 0, 0, 0 } },
    { Haydn::SRA64, 0x6, { 0, 0, 0 } },
    { Haydn::SRA64R, 0x6, { 0, 0, 0 } },
    { Haydn::SRAI32, 0x7, { 0, 0, 0 } },
    { Haydn::SRAI32R, 0x7, { 0, 0, 0 } },
    { Haydn::SRAI64, 0x6, { 0, 0, 0 } },
    { Haydn::SRAI64R, 0x6, { 0, 0, 0 } },
    { Haydn::SRL32, 0x7, { 0, 0, 0 } },
    { Haydn::SRL64, 0x6, { 0, 0, 0 } },
    { Haydn::SRLI32, 0x7, { 0, 0, 0 } },
    { Haydn::SRLI64, 0x6, { 0, 0, 0 } },
    { Haydn::ST16, 0x1, { 0, 0, 0 } },
    { Haydn::ST32, 0x1, { 0, 0, 0 } },
    { Haydn::ST32_POST, 0x2, { 0, 0, 0 } },
    { Haydn::ST32_REG_M0S0LS, 0x1, { 0, 0, 0 } },
    { Haydn::ST64, 0x1, { 0, 0, 0 } },
    { Haydn::ST64_POST, 0x2, { 0, 0, 0 } },
    { Haydn::ST64_REG_M0S0LS, 0x1, { 0, 0, 0 } },
    { Haydn::ST8, 0x1, { 0, 0, 0 } },
    { Haydn::SUB32, 0x7, { 0, 0, 0 } },
    { Haydn::SUB32S, 0x7, { 0, 0, 0 } },
    { Haydn::SUB64, 0x6, { 0, 0, 0 } },
    { Haydn::SUB64S, 0x6, { 0, 0, 0 } },
    { Haydn::SUB64S_H, 0x6, { 0, 0, 0 } },
    { Haydn::SUB64S_L, 0x6, { 0, 0, 0 } },
    { Haydn::SUB64_H, 0x6, { 0, 0, 0 } },
    { Haydn::SUB64_L, 0x6, { 0, 0, 0 } },
    { Haydn::SUBI32, 0x7, { 0, 0, 0 } },
    { Haydn::SUBI32S, 0x7, { 0, 0, 0 } },
    { Haydn::S_LBS_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LBS_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LBS_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LBS_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LBS_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LBS_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LBU_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LBU_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LBU_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LBU_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LBU_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LBU_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LHWS_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LHWS_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LHWS_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LHWS_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LHWS_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LHWS_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LHWU_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LHWU_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LHWU_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LHWU_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LHWU_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LHWU_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LW_BREV_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LW_BREV_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LW_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LW_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LW_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LW_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_LW_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_LW_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_SB_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_SB_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_SB_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_SB_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_SB_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_SB_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_SHW_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_SHW_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_SHW_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_SHW_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_SHW_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_SHW_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_SW_BREV_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_SW_BREV_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_SW_POST_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_SW_POST_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_SW_PRE_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_SW_PRE_REG, 0x7, { 0, 0, 0 } },
    { Haydn::S_SW_WITH_IMM, 0x7, { 0, 0, 0 } },
    { Haydn::S_SW_WITH_REG, 0x7, { 0, 0, 0 } },
    { Haydn::TRANSF64, 0x6, { 0, 0, 0 } },
    { Haydn::TRANSF64F2, 0x6, { 0, 0, 0 } },
    { Haydn::TRANSF64_H, 0x6, { 0, 0, 0 } },
    { Haydn::TRANSF64_L, 0x6, { 0, 0, 0 } },
    { Haydn::WBARWUA, 0x7, { 0, 0, 0 } },
    { Haydn::WBARWUA_CB, 0x7, { 0, 0, 0 } },
    { Haydn::WFI, 0x1, { 0, 0, 0 } },
    { Haydn::X2ABS32, 0x7, { 0, 0, 0 } },
    { Haydn::X2ABS32S, 0x7, { 0, 0, 0 } },
    { Haydn::X2ADD32, 0x7, { 0, 0, 0 } },
    { Haydn::X2ADD32S, 0x7, { 0, 0, 0 } },
    { Haydn::X2ADD32S_HLLH, 0x7, { 0, 0, 0 } },
    { Haydn::X2ADD32_HLLH, 0x7, { 0, 0, 0 } },
    { Haydn::X2ADDSUB32, 0x7, { 0, 0, 0 } },
    { Haydn::X2ADDSUB32S, 0x7, { 0, 0, 0 } },
    { Haydn::X2ADDSUB32S_HLLH, 0x7, { 0, 0, 0 } },
    { Haydn::X2ADDSUB32_HLLH, 0x7, { 0, 0, 0 } },
    { Haydn::X2CLAMP32, 0x7, { 0, 0, 0 } },
    { Haydn::X2CMUL32, 0x6, { 0, 0, 0 } },
    { Haydn::X2CMUL32S, 0x6, { 0, 0, 0 } },
    { Haydn::X2CMUL32S_F2, 0x6, { 0, 0, 0 } },
    { Haydn::X2CMUL32X16_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2CMUL32X16_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2CMUL32_F2, 0x6, { 0, 0, 0 } },
    { Haydn::X2CMULA32X16_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2CMULA32X16_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2DOT32, 0x6, { 0, 0, 0 } },
    { Haydn::X2FCMUL32RS, 0x6, { 0, 0, 0 } },
    { Haydn::X2FCMUL32RSS, 0x6, { 0, 0, 0 } },
    { Haydn::X2FCMUL32X16RS_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2FCMUL32X16RS_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2FCMULA32RS, 0x6, { 0, 0, 0 } },
    { Haydn::X2FCMULA32RSS, 0x6, { 0, 0, 0 } },
    { Haydn::X2FCMULA32X16RS_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2FCMULA32X16RS_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2FF2RSST32, 0x7, { 0, 0, 0 } },
    { Haydn::X2FF2RST32, 0x7, { 0, 0, 0 } },
    { Haydn::X2FMUL32RS, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMUL32RSS, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMUL32TS, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMUL32X16RSS_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMUL32X16RSS_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMUL32X16RS_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMUL32X16RS_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMUL32X16TS_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMUL32X16TS_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULA32RS, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULA32RSS, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULA32TS, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULA32X16RSS_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULA32X16RSS_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULA32X16RS_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULA32X16RS_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULA32X16TS_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULA32X16TS_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULS32RS, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULS32RSS, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULS32TS, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULS32X16RSS_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULS32X16RSS_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULS32X16RS_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULS32X16RS_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULS32X16TS_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2FMULS32X16TS_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2FRSST32, 0x7, { 0, 0, 0 } },
    { Haydn::X2FRST32, 0x7, { 0, 0, 0 } },
    { Haydn::X2HADD32S_H, 0x7, { 0, 0, 0 } },
    { Haydn::X2HADD32S_L, 0x7, { 0, 0, 0 } },
    { Haydn::X2HADD32_H, 0x7, { 0, 0, 0 } },
    { Haydn::X2HADD32_L, 0x7, { 0, 0, 0 } },
    { Haydn::X2HMAX32, 0x7, { 0, 0, 0 } },
    { Haydn::X2HMIN32, 0x7, { 0, 0, 0 } },
    { Haydn::X2MAX32, 0x7, { 0, 0, 0 } },
    { Haydn::X2MIN32, 0x7, { 0, 0, 0 } },
    { Haydn::X2MJSWAP32, 0x7, { 0, 0, 0 } },
    { Haydn::X2MJSWAP32S, 0x7, { 0, 0, 0 } },
    { Haydn::X2MOVF32, 0x7, { 0, 0, 0 } },
    { Haydn::X2MOVT32, 0x7, { 0, 0, 0 } },
    { Haydn::X2MUL32, 0x6, { 0, 0, 0 } },
    { Haydn::X2MUL32X16_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2MUL32X16_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2MULA32, 0x6, { 0, 0, 0 } },
    { Haydn::X2MULA32X16_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2MULA32X16_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2MULAPH32, 0x6, { 0, 0, 0 } },
    { Haydn::X2MULAPL32, 0x6, { 0, 0, 0 } },
    { Haydn::X2MULPH32, 0x6, { 0, 0, 0 } },
    { Haydn::X2MULPL32, 0x6, { 0, 0, 0 } },
    { Haydn::X2MULS32, 0x6, { 0, 0, 0 } },
    { Haydn::X2MULS32X16_H, 0x6, { 0, 0, 0 } },
    { Haydn::X2MULS32X16_L, 0x6, { 0, 0, 0 } },
    { Haydn::X2MULSPH32, 0x6, { 0, 0, 0 } },
    { Haydn::X2MULSPL32, 0x6, { 0, 0, 0 } },
    { Haydn::X2NEG32, 0x7, { 0, 0, 0 } },
    { Haydn::X2NEG32S, 0x7, { 0, 0, 0 } },
    { Haydn::X2NEG32S_L, 0x7, { 0, 0, 0 } },
    { Haydn::X2NEG32_L, 0x7, { 0, 0, 0 } },
    { Haydn::X2SEL32_HH, 0x7, { 0, 0, 0 } },
    { Haydn::X2SEL32_HL, 0x7, { 0, 0, 0 } },
    { Haydn::X2SEL32_LH, 0x7, { 0, 0, 0 } },
    { Haydn::X2SEL32_LL, 0x7, { 0, 0, 0 } },
    { Haydn::X2SEQ32, 0x7, { 0, 0, 0 } },
    { Haydn::X2SLE32, 0x7, { 0, 0, 0 } },
    { Haydn::X2SLL32, 0x7, { 0, 0, 0 } },
    { Haydn::X2SLLI32, 0x7, { 0, 0, 0 } },
    { Haydn::X2SLT32, 0x7, { 0, 0, 0 } },
    { Haydn::X2SRA32, 0x7, { 0, 0, 0 } },
    { Haydn::X2SRA32R, 0x7, { 0, 0, 0 } },
    { Haydn::X2SRAI32, 0x7, { 0, 0, 0 } },
    { Haydn::X2SRAI32R, 0x7, { 0, 0, 0 } },
    { Haydn::X2SRL32, 0x7, { 0, 0, 0 } },
    { Haydn::X2SRLI32, 0x7, { 0, 0, 0 } },
    { Haydn::X2SUB32, 0x7, { 0, 0, 0 } },
    { Haydn::X2SUB32S, 0x7, { 0, 0, 0 } },
    { Haydn::X2SUB32S_HLLH, 0x7, { 0, 0, 0 } },
    { Haydn::X2SUB32_HLLH, 0x7, { 0, 0, 0 } },
    { Haydn::X2SUBADD32, 0x7, { 0, 0, 0 } },
    { Haydn::X2SUBADD32S, 0x7, { 0, 0, 0 } },
    { Haydn::X2SUBADD32S_HLLH, 0x7, { 0, 0, 0 } },
    { Haydn::X2SUBADD32_HLLH, 0x7, { 0, 0, 0 } },
    { Haydn::X2SWAP32, 0x7, { 0, 0, 0 } },
    { Haydn::X4ABS16, 0x7, { 0, 0, 0 } },
    { Haydn::X4ABS16S, 0x7, { 0, 0, 0 } },
    { Haydn::X4ADD16, 0x7, { 0, 0, 0 } },
    { Haydn::X4ADD16S, 0x7, { 0, 0, 0 } },
    { Haydn::X4CJMUL16S_H, 0x6, { 0, 0, 0 } },
    { Haydn::X4CJMUL16S_L, 0x6, { 0, 0, 0 } },
    { Haydn::X4CJMULA16S_H, 0x6, { 0, 0, 0 } },
    { Haydn::X4CJMULA16S_L, 0x6, { 0, 0, 0 } },
    { Haydn::X4CLAMP16, 0x7, { 0, 0, 0 } },
    { Haydn::X4CMUL16, 0x6, { 0, 0, 0 } },
    { Haydn::X4CMUL16S, 0x6, { 0, 0, 0 } },
    { Haydn::X4CMUL16S_F2, 0x6, { 0, 0, 0 } },
    { Haydn::X4CMUL16S_H, 0x6, { 0, 0, 0 } },
    { Haydn::X4CMUL16S_L, 0x6, { 0, 0, 0 } },
    { Haydn::X4CMUL16_F2, 0x6, { 0, 0, 0 } },
    { Haydn::X4CMULA16S_H, 0x6, { 0, 0, 0 } },
    { Haydn::X4CMULA16S_L, 0x6, { 0, 0, 0 } },
    { Haydn::X4CONJ16, 0x7, { 0, 0, 0 } },
    { Haydn::X4CONJ16S, 0x7, { 0, 0, 0 } },
    { Haydn::X4DOT16, 0x6, { 0, 0, 0 } },
    { Haydn::X4ENERGY16, 0x6, { 0, 0, 0 } },
    { Haydn::X4FCMUL16RS, 0x6, { 0, 0, 0 } },
    { Haydn::X4FCMUL16RSS, 0x6, { 0, 0, 0 } },
    { Haydn::X4FCMULA16RS, 0x6, { 0, 0, 0 } },
    { Haydn::X4FCMULA16RSS, 0x6, { 0, 0, 0 } },
    { Haydn::X4FF2MUL16S, 0x6, { 0, 0, 0 } },
    { Haydn::X4FF2MULA16S, 0x6, { 0, 0, 0 } },
    { Haydn::X4FF2MULS16S, 0x6, { 0, 0, 0 } },
    { Haydn::X4FMUL16RS, 0x6, { 0, 0, 0 } },
    { Haydn::X4FMUL16RSS, 0x6, { 0, 0, 0 } },
    { Haydn::X4FMUL16TS, 0x6, { 0, 0, 0 } },
    { Haydn::X4FRSST16, 0x7, { 0, 0, 0 } },
    { Haydn::X4FRST16, 0x7, { 0, 0, 0 } },
    { Haydn::X4HADD16_H, 0x7, { 0, 0, 0 } },
    { Haydn::X4HADD16_L, 0x7, { 0, 0, 0 } },
    { Haydn::X4HMAX16, 0x7, { 0, 0, 0 } },
    { Haydn::X4HMIN16, 0x7, { 0, 0, 0 } },
    { Haydn::X4MAX16, 0x7, { 0, 0, 0 } },
    { Haydn::X4MIN16, 0x7, { 0, 0, 0 } },
    { Haydn::X4MJSWAP16, 0x7, { 0, 0, 0 } },
    { Haydn::X4MJSWAP16S, 0x7, { 0, 0, 0 } },
    { Haydn::X4MOVF16, 0x7, { 0, 0, 0 } },
    { Haydn::X4MOVT16, 0x7, { 0, 0, 0 } },
    { Haydn::X4MUL16, 0x6, { 0, 0, 0 } },
    { Haydn::X4MULA16, 0x6, { 0, 0, 0 } },
    { Haydn::X4MULA16S, 0x6, { 0, 0, 0 } },
    { Haydn::X4MULS16, 0x6, { 0, 0, 0 } },
    { Haydn::X4MULS16S, 0x6, { 0, 0, 0 } },
    { Haydn::X4NEG16, 0x7, { 0, 0, 0 } },
    { Haydn::X4NEG16S, 0x7, { 0, 0, 0 } },
    { Haydn::X4SAT32T16, 0x7, { 0, 0, 0 } },
    { Haydn::X4SEL16, 0x7, { 0, 0, 0 } },
    { Haydn::X4SELI16, 0x7, { 0, 0, 0 } },
    { Haydn::X4SEQ16, 0x7, { 0, 0, 0 } },
    { Haydn::X4SLE16, 0x7, { 0, 0, 0 } },
    { Haydn::X4SLL16, 0x7, { 0, 0, 0 } },
    { Haydn::X4SLLI16, 0x7, { 0, 0, 0 } },
    { Haydn::X4SLT16, 0x7, { 0, 0, 0 } },
    { Haydn::X4SRA16, 0x7, { 0, 0, 0 } },
    { Haydn::X4SRA16R, 0x7, { 0, 0, 0 } },
    { Haydn::X4SRAI16, 0x7, { 0, 0, 0 } },
    { Haydn::X4SRAI16R, 0x7, { 0, 0, 0 } },
    { Haydn::X4SRL16, 0x7, { 0, 0, 0 } },
    { Haydn::X4SRLI16, 0x7, { 0, 0, 0 } },
    { Haydn::X4SUB16, 0x7, { 0, 0, 0 } },
    { Haydn::X4SUB16S, 0x7, { 0, 0, 0 } },
    { Haydn::X4SWAP16, 0x7, { 0, 0, 0 } },
    { Haydn::XOR32, 0x7, { 0, 0, 0 } },
    { Haydn::XOR64, 0x6, { 0, 0, 0 } },
    { Haydn::XORI32, 0x7, { 0, 0, 0 } },
    { Haydn::ZERO_DR, 0x6, { 0, 0, 0 } },
    { Haydn::ZERO_GPR, 0x7, { 0, 0, 0 } },
    { Haydn::ZERO_SFR, 0x6, { 0, 0, 0 } },
};

static int haydnAltOccupancyIndex(unsigned Opcode) {
  switch (Opcode) {
  default:
    return -1;
  case Haydn::ADD32_MSP:
    return 0;
  case Haydn::ABS32:
    return 1;
  case Haydn::ABS32S:
    return 2;
  case Haydn::ABS64:
    return 3;
  case Haydn::ABS64S:
    return 4;
  case Haydn::ADD32:
    return 5;
  case Haydn::ADD32S:
    return 6;
  case Haydn::ADD64:
    return 7;
  case Haydn::ADD64S:
    return 8;
  case Haydn::ADD64S_H:
    return 9;
  case Haydn::ADD64S_L:
    return 10;
  case Haydn::ADD64_H:
    return 11;
  case Haydn::ADD64_L:
    return 12;
  case Haydn::ADDI32:
    return 13;
  case Haydn::ADDI32S:
    return 14;
  case Haydn::ADDI32_W:
    return 15;
  case Haydn::AND32:
    return 16;
  case Haydn::AND64:
    return 17;
  case Haydn::ANDI32:
    return 18;
  case Haydn::ARCTAN:
    return 19;
  case Haydn::ASR32:
    return 20;
  case Haydn::BEQ:
    return 21;
  case Haydn::BEQZ:
    return 22;
  case Haydn::BEQZ_W:
    return 23;
  case Haydn::BEQ_W:
    return 24;
  case Haydn::BGE:
    return 25;
  case Haydn::BGEU:
    return 26;
  case Haydn::BGEU_W:
    return 27;
  case Haydn::BGEZ:
    return 28;
  case Haydn::BGEZ_W:
    return 29;
  case Haydn::BGE_W:
    return 30;
  case Haydn::BLT:
    return 31;
  case Haydn::BLTU:
    return 32;
  case Haydn::BLTU_W:
    return 33;
  case Haydn::BLTZ:
    return 34;
  case Haydn::BLTZ_W:
    return 35;
  case Haydn::BLT_W:
    return 36;
  case Haydn::BNE:
    return 37;
  case Haydn::BNEZ:
    return 38;
  case Haydn::BNEZ_W:
    return 39;
  case Haydn::BNE_W:
    return 40;
  case Haydn::BREV32:
    return 41;
  case Haydn::CSRR:
    return 42;
  case Haydn::CSRW:
    return 43;
  case Haydn::CSRW_W:
    return 44;
  case Haydn::D_LDW_BREV_IMM:
    return 45;
  case Haydn::D_LDW_BREV_REG:
    return 46;
  case Haydn::D_LDW_CB_IMM:
    return 47;
  case Haydn::D_LDW_CB_REG:
    return 48;
  case Haydn::D_LDW_POST_IMM:
    return 49;
  case Haydn::D_LDW_POST_REG:
    return 50;
  case Haydn::D_LDW_PRE_IMM:
    return 51;
  case Haydn::D_LDW_PRE_REG:
    return 52;
  case Haydn::D_LDW_WITH_IMM:
    return 53;
  case Haydn::D_LDW_WITH_REG:
    return 54;
  case Haydn::D_LHW_POST_IMM:
    return 55;
  case Haydn::D_LHW_POST_REG:
    return 56;
  case Haydn::D_LHW_PRE_IMM:
    return 57;
  case Haydn::D_LHW_PRE_REG:
    return 58;
  case Haydn::D_LHW_WITH_IMM:
    return 59;
  case Haydn::D_LHW_WITH_REG:
    return 60;
  case Haydn::D_LQHWUA_CB_POST:
    return 61;
  case Haydn::D_LQHWUA_POST:
    return 62;
  case Haydn::D_LTWUA_CB_POST:
    return 63;
  case Haydn::D_LTWUA_POST:
    return 64;
  case Haydn::D_LW_POST_IMM:
    return 65;
  case Haydn::D_LW_POST_REG:
    return 66;
  case Haydn::D_LW_PRE_IMM:
    return 67;
  case Haydn::D_LW_PRE_REG:
    return 68;
  case Haydn::D_LW_WITH_IMM:
    return 69;
  case Haydn::D_LW_WITH_REG:
    return 70;
  case Haydn::D_SDW_BREV_IMM:
    return 71;
  case Haydn::D_SDW_BREV_REG:
    return 72;
  case Haydn::D_SDW_CB_IMM:
    return 73;
  case Haydn::D_SDW_CB_REG:
    return 74;
  case Haydn::D_SDW_POST_IMM:
    return 75;
  case Haydn::D_SDW_POST_REG:
    return 76;
  case Haydn::D_SDW_PRE_IMM:
    return 77;
  case Haydn::D_SDW_PRE_REG:
    return 78;
  case Haydn::D_SDW_WITH_IMM:
    return 79;
  case Haydn::D_SDW_WITH_REG:
    return 80;
  case Haydn::D_SHW_POST_IMM:
    return 81;
  case Haydn::D_SHW_POST_REG:
    return 82;
  case Haydn::D_SHW_PRE_IMM:
    return 83;
  case Haydn::D_SHW_PRE_REG:
    return 84;
  case Haydn::D_SHW_WITH_IMM:
    return 85;
  case Haydn::D_SHW_WITH_REG:
    return 86;
  case Haydn::D_SQHWUA_CB_POST:
    return 87;
  case Haydn::D_SQHWUA_POST:
    return 88;
  case Haydn::D_STWUA_CB_POST:
    return 89;
  case Haydn::D_STWUA_POST:
    return 90;
  case Haydn::D_SW_F64RS_POST_IMM:
    return 91;
  case Haydn::D_SW_F64RS_POST_REG:
    return 92;
  case Haydn::D_SW_F64RS_WITH_IMM:
    return 93;
  case Haydn::D_SW_F64RS_WITH_REG:
    return 94;
  case Haydn::D_SW_H_POST_IMM:
    return 95;
  case Haydn::D_SW_H_POST_REG:
    return 96;
  case Haydn::D_SW_H_PRE_IMM:
    return 97;
  case Haydn::D_SW_H_PRE_REG:
    return 98;
  case Haydn::D_SW_H_WITH_IMM:
    return 99;
  case Haydn::D_SW_H_WITH_REG:
    return 100;
  case Haydn::D_SW_L_POST_IMM:
    return 101;
  case Haydn::D_SW_L_POST_REG:
    return 102;
  case Haydn::D_SW_L_PRE_IMM:
    return 103;
  case Haydn::D_SW_L_PRE_REG:
    return 104;
  case Haydn::D_SW_L_WITH_IMM:
    return 105;
  case Haydn::D_SW_L_WITH_REG:
    return 106;
  case Haydn::EXP2:
    return 107;
  case Haydn::F2MULAA32RS_HHLL:
    return 108;
  case Haydn::F2MULAA32RS_HLLH:
    return 109;
  case Haydn::F2MULAA32R_HHLL:
    return 110;
  case Haydn::F2MULAA32R_HLLH:
    return 111;
  case Haydn::F2MULAS32RS_HHLL:
    return 112;
  case Haydn::F2MULAS32RS_HLLH:
    return 113;
  case Haydn::F2MULAS32R_HHLL:
    return 114;
  case Haydn::F2MULAS32R_HLLH:
    return 115;
  case Haydn::F2MULSA32RS_HHLL:
    return 116;
  case Haydn::F2MULSA32RS_HLLH:
    return 117;
  case Haydn::F2MULSA32R_HHLL:
    return 118;
  case Haydn::F2MULSA32R_HLLH:
    return 119;
  case Haydn::F2MULSS32RS_HHLL:
    return 120;
  case Haydn::F2MULSS32RS_HLLH:
    return 121;
  case Haydn::F2MULSS32R_HHLL:
    return 122;
  case Haydn::F2MULSS32R_HLLH:
    return 123;
  case Haydn::F2MULZAA32RS_HHLL:
    return 124;
  case Haydn::F2MULZAA32RS_HLLH:
    return 125;
  case Haydn::F2MULZAA32R_HHLL:
    return 126;
  case Haydn::F2MULZAA32R_HLLH:
    return 127;
  case Haydn::F2MULZAS32RS_HHLL:
    return 128;
  case Haydn::F2MULZAS32RS_HLLH:
    return 129;
  case Haydn::F2MULZAS32R_HHLL:
    return 130;
  case Haydn::F2MULZAS32R_HLLH:
    return 131;
  case Haydn::F2MULZSA32RS_HHLL:
    return 132;
  case Haydn::F2MULZSA32RS_HLLH:
    return 133;
  case Haydn::F2MULZSA32R_HHLL:
    return 134;
  case Haydn::F2MULZSA32R_HLLH:
    return 135;
  case Haydn::F2MULZSS32RS_HHLL:
    return 136;
  case Haydn::F2MULZSS32RS_HLLH:
    return 137;
  case Haydn::F2MULZSS32R_HHLL:
    return 138;
  case Haydn::F2MULZSS32R_HLLH:
    return 139;
  case Haydn::FF2MUL32RS_HH:
    return 140;
  case Haydn::FF2MUL32RS_LH:
    return 141;
  case Haydn::FF2MUL32RS_LL:
    return 142;
  case Haydn::FF2MUL32R_HH:
    return 143;
  case Haydn::FF2MUL32R_LH:
    return 144;
  case Haydn::FF2MUL32R_LL:
    return 145;
  case Haydn::FF2MULA32RS_HH:
    return 146;
  case Haydn::FF2MULA32RS_LH:
    return 147;
  case Haydn::FF2MULA32RS_LL:
    return 148;
  case Haydn::FF2MULA32R_HH:
    return 149;
  case Haydn::FF2MULA32R_LH:
    return 150;
  case Haydn::FF2MULA32R_LL:
    return 151;
  case Haydn::FF2MULS32RS_HH:
    return 152;
  case Haydn::FF2MULS32RS_LH:
    return 153;
  case Haydn::FF2MULS32RS_LL:
    return 154;
  case Haydn::FF2MULS32R_HH:
    return 155;
  case Haydn::FF2MULS32R_LH:
    return 156;
  case Haydn::FF2MULS32R_LL:
    return 157;
  case Haydn::FLAR:
    return 158;
  case Haydn::FMUL16_HS00:
    return 159;
  case Haydn::FMUL16_HS01:
    return 160;
  case Haydn::FMUL16_HS02:
    return 161;
  case Haydn::FMUL16_HS03:
    return 162;
  case Haydn::FMUL16_HS11:
    return 163;
  case Haydn::FMUL16_HS12:
    return 164;
  case Haydn::FMUL16_HS13:
    return 165;
  case Haydn::FMUL16_HS22:
    return 166;
  case Haydn::FMUL16_HS23:
    return 167;
  case Haydn::FMUL16_HS33:
    return 168;
  case Haydn::FMUL16_LS00:
    return 169;
  case Haydn::FMUL16_LS01:
    return 170;
  case Haydn::FMUL16_LS02:
    return 171;
  case Haydn::FMUL16_LS03:
    return 172;
  case Haydn::FMUL16_LS11:
    return 173;
  case Haydn::FMUL16_LS12:
    return 174;
  case Haydn::FMUL16_LS13:
    return 175;
  case Haydn::FMUL16_LS22:
    return 176;
  case Haydn::FMUL16_LS23:
    return 177;
  case Haydn::FMUL16_LS33:
    return 178;
  case Haydn::FMUL32S_HH:
    return 179;
  case Haydn::FMUL32S_LH:
    return 180;
  case Haydn::FMUL32S_LL:
    return 181;
  case Haydn::FMUL32X16_H0:
    return 182;
  case Haydn::FMUL32X16_H1:
    return 183;
  case Haydn::FMUL32X16_H2:
    return 184;
  case Haydn::FMUL32X16_H3:
    return 185;
  case Haydn::FMUL32X16_L0:
    return 186;
  case Haydn::FMUL32X16_L1:
    return 187;
  case Haydn::FMUL32X16_L2:
    return 188;
  case Haydn::FMUL32X16_L3:
    return 189;
  case Haydn::FMULA16_HS00:
    return 190;
  case Haydn::FMULA16_HS01:
    return 191;
  case Haydn::FMULA16_HS02:
    return 192;
  case Haydn::FMULA16_HS03:
    return 193;
  case Haydn::FMULA16_HS11:
    return 194;
  case Haydn::FMULA16_HS12:
    return 195;
  case Haydn::FMULA16_HS13:
    return 196;
  case Haydn::FMULA16_HS22:
    return 197;
  case Haydn::FMULA16_HS23:
    return 198;
  case Haydn::FMULA16_HS33:
    return 199;
  case Haydn::FMULA16_LS00:
    return 200;
  case Haydn::FMULA16_LS01:
    return 201;
  case Haydn::FMULA16_LS02:
    return 202;
  case Haydn::FMULA16_LS03:
    return 203;
  case Haydn::FMULA16_LS11:
    return 204;
  case Haydn::FMULA16_LS12:
    return 205;
  case Haydn::FMULA16_LS13:
    return 206;
  case Haydn::FMULA16_LS22:
    return 207;
  case Haydn::FMULA16_LS23:
    return 208;
  case Haydn::FMULA16_LS33:
    return 209;
  case Haydn::FMULA32S_HH:
    return 210;
  case Haydn::FMULA32S_LH:
    return 211;
  case Haydn::FMULA32S_LL:
    return 212;
  case Haydn::FMULA32X16_H0:
    return 213;
  case Haydn::FMULA32X16_H1:
    return 214;
  case Haydn::FMULA32X16_H2:
    return 215;
  case Haydn::FMULA32X16_H3:
    return 216;
  case Haydn::FMULA32X16_L0:
    return 217;
  case Haydn::FMULA32X16_L1:
    return 218;
  case Haydn::FMULA32X16_L2:
    return 219;
  case Haydn::FMULA32X16_L3:
    return 220;
  case Haydn::FMULAA16_HS_11_00:
    return 221;
  case Haydn::FMULAA16_HS_13_02:
    return 222;
  case Haydn::FMULAA16_HS_33_22:
    return 223;
  case Haydn::FMULAA16_LS_11_00:
    return 224;
  case Haydn::FMULAA16_LS_13_02:
    return 225;
  case Haydn::FMULAA16_LS_33_22:
    return 226;
  case Haydn::FMULAA32S_HHLL:
    return 227;
  case Haydn::FMULAA32S_HLLH:
    return 228;
  case Haydn::FMULAA32X16_H0_L1:
    return 229;
  case Haydn::FMULAA32X16_H1_L0:
    return 230;
  case Haydn::FMULAA32X16_H2_L3:
    return 231;
  case Haydn::FMULAA32X16_H3_L2:
    return 232;
  case Haydn::FMULAS32S_HHLL:
    return 233;
  case Haydn::FMULAS32S_HLLH:
    return 234;
  case Haydn::FMULAS32X16_H1_L0:
    return 235;
  case Haydn::FMULAS32X16_H3_L2:
    return 236;
  case Haydn::FMULS16_HS00:
    return 237;
  case Haydn::FMULS16_HS01:
    return 238;
  case Haydn::FMULS16_HS02:
    return 239;
  case Haydn::FMULS16_HS03:
    return 240;
  case Haydn::FMULS16_HS11:
    return 241;
  case Haydn::FMULS16_HS12:
    return 242;
  case Haydn::FMULS16_HS13:
    return 243;
  case Haydn::FMULS16_HS22:
    return 244;
  case Haydn::FMULS16_HS23:
    return 245;
  case Haydn::FMULS16_HS33:
    return 246;
  case Haydn::FMULS16_LS00:
    return 247;
  case Haydn::FMULS16_LS01:
    return 248;
  case Haydn::FMULS16_LS02:
    return 249;
  case Haydn::FMULS16_LS03:
    return 250;
  case Haydn::FMULS16_LS11:
    return 251;
  case Haydn::FMULS16_LS12:
    return 252;
  case Haydn::FMULS16_LS13:
    return 253;
  case Haydn::FMULS16_LS22:
    return 254;
  case Haydn::FMULS16_LS23:
    return 255;
  case Haydn::FMULS16_LS33:
    return 256;
  case Haydn::FMULS32S_HH:
    return 257;
  case Haydn::FMULS32S_LH:
    return 258;
  case Haydn::FMULS32S_LL:
    return 259;
  case Haydn::FMULS32X16_H0:
    return 260;
  case Haydn::FMULS32X16_H1:
    return 261;
  case Haydn::FMULS32X16_H2:
    return 262;
  case Haydn::FMULS32X16_H3:
    return 263;
  case Haydn::FMULS32X16_L0:
    return 264;
  case Haydn::FMULS32X16_L1:
    return 265;
  case Haydn::FMULS32X16_L2:
    return 266;
  case Haydn::FMULS32X16_L3:
    return 267;
  case Haydn::FMULSA32S_HHLL:
    return 268;
  case Haydn::FMULSA32S_HLLH:
    return 269;
  case Haydn::FMULSA32X16_H1_L0:
    return 270;
  case Haydn::FMULSA32X16_H3_L2:
    return 271;
  case Haydn::FMULSS16_HS_11_00:
    return 272;
  case Haydn::FMULSS16_HS_13_02:
    return 273;
  case Haydn::FMULSS16_HS_33_22:
    return 274;
  case Haydn::FMULSS16_LS_11_00:
    return 275;
  case Haydn::FMULSS16_LS_13_02:
    return 276;
  case Haydn::FMULSS16_LS_33_22:
    return 277;
  case Haydn::FMULSS32S_HHLL:
    return 278;
  case Haydn::FMULSS32S_HLLH:
    return 279;
  case Haydn::FMULSS32X16_H1_L0:
    return 280;
  case Haydn::FMULSS32X16_H3_L2:
    return 281;
  case Haydn::FMULZAA16_HS_11_00:
    return 282;
  case Haydn::FMULZAA16_HS_13_02:
    return 283;
  case Haydn::FMULZAA16_HS_33_22:
    return 284;
  case Haydn::FMULZAA16_LS_11_00:
    return 285;
  case Haydn::FMULZAA16_LS_13_02:
    return 286;
  case Haydn::FMULZAA16_LS_33_22:
    return 287;
  case Haydn::FMULZAA32S_HHLL:
    return 288;
  case Haydn::FMULZAA32S_HLLH:
    return 289;
  case Haydn::FMULZAA32X16_H0_L1:
    return 290;
  case Haydn::FMULZAA32X16_H1_L0:
    return 291;
  case Haydn::FMULZAA32X16_H2_L3:
    return 292;
  case Haydn::FMULZAA32X16_H3_L2:
    return 293;
  case Haydn::FMULZAS32S_HHLL:
    return 294;
  case Haydn::FMULZAS32S_HLLH:
    return 295;
  case Haydn::FMULZAS32X16_H1_L0:
    return 296;
  case Haydn::FMULZAS32X16_H3_L2:
    return 297;
  case Haydn::FMULZSA32S_HHLL:
    return 298;
  case Haydn::FMULZSA32S_HLLH:
    return 299;
  case Haydn::FMULZSA32X16_H1_L0:
    return 300;
  case Haydn::FMULZSA32X16_H3_L2:
    return 301;
  case Haydn::FMULZSS16_HS_11_00:
    return 302;
  case Haydn::FMULZSS16_HS_13_02:
    return 303;
  case Haydn::FMULZSS16_HS_33_22:
    return 304;
  case Haydn::FMULZSS16_LS_11_00:
    return 305;
  case Haydn::FMULZSS16_LS_13_02:
    return 306;
  case Haydn::FMULZSS16_LS_33_22:
    return 307;
  case Haydn::FMULZSS32S_HHLL:
    return 308;
  case Haydn::FMULZSS32S_HLLH:
    return 309;
  case Haydn::FMULZSS32X16_H1_L0:
    return 310;
  case Haydn::FMULZSS32X16_H3_L2:
    return 311;
  case Haydn::JAL:
    return 312;
  case Haydn::JALR:
    return 313;
  case Haydn::JALR_W:
    return 314;
  case Haydn::JAL_W:
    return 315;
  case Haydn::LD16:
    return 316;
  case Haydn::LD32:
    return 317;
  case Haydn::LD32_POST:
    return 318;
  case Haydn::LD32_REG_M0S0LS:
    return 319;
  case Haydn::LD64:
    return 320;
  case Haydn::LD64_POST:
    return 321;
  case Haydn::LD64_REG_M0S0LS:
    return 322;
  case Haydn::LD8:
    return 323;
  case Haydn::LDU16:
    return 324;
  case Haydn::LDU8:
    return 325;
  case Haydn::LOG2:
    return 326;
  case Haydn::LSR32:
    return 327;
  case Haydn::LUI:
    return 328;
  case Haydn::MAX32:
    return 329;
  case Haydn::MAX64:
    return 330;
  case Haydn::MAXU32:
    return 331;
  case Haydn::MIN32:
    return 332;
  case Haydn::MIN64:
    return 333;
  case Haydn::MINU32:
    return 334;
  case Haydn::MOVE32:
    return 335;
  case Haydn::MOVE32_DR_H:
    return 336;
  case Haydn::MOVE32_DR_L:
    return 337;
  case Haydn::MOVE64:
    return 338;
  case Haydn::MOVEGPR2SFR:
    return 339;
  case Haydn::MOVEI_H:
    return 340;
  case Haydn::MOVEI_L:
    return 341;
  case Haydn::MOVESFR2GPR:
    return 342;
  case Haydn::MOVF32:
    return 343;
  case Haydn::MOVF64:
    return 344;
  case Haydn::MOVT32:
    return 345;
  case Haydn::MOVT64:
    return 346;
  case Haydn::MUL16AQ:
    return 347;
  case Haydn::MUL16ZAQ:
    return 348;
  case Haydn::MUL32X16_H0:
    return 349;
  case Haydn::MUL32X16_H1:
    return 350;
  case Haydn::MUL32X16_H2:
    return 351;
  case Haydn::MUL32X16_H3:
    return 352;
  case Haydn::MUL32X16_L0:
    return 353;
  case Haydn::MUL32X16_L1:
    return 354;
  case Haydn::MUL32X16_L2:
    return 355;
  case Haydn::MUL32X16_L3:
    return 356;
  case Haydn::MUL64_HH:
    return 357;
  case Haydn::MUL64_HL:
    return 358;
  case Haydn::MUL64_HUH:
    return 359;
  case Haydn::MUL64_HUL:
    return 360;
  case Haydn::MUL64_LH:
    return 361;
  case Haydn::MUL64_LL:
    return 362;
  case Haydn::MUL64_LUH:
    return 363;
  case Haydn::MUL64_LUL:
    return 364;
  case Haydn::MUL64_UHH:
    return 365;
  case Haydn::MUL64_UHL:
    return 366;
  case Haydn::MUL64_UHUH:
    return 367;
  case Haydn::MUL64_UHUL:
    return 368;
  case Haydn::MUL64_ULH:
    return 369;
  case Haydn::MUL64_ULL:
    return 370;
  case Haydn::MUL64_ULUH:
    return 371;
  case Haydn::MUL64_ULUL:
    return 372;
  case Haydn::MULA32X16_H0:
    return 373;
  case Haydn::MULA32X16_H1:
    return 374;
  case Haydn::MULA32X16_H2:
    return 375;
  case Haydn::MULA32X16_H3:
    return 376;
  case Haydn::MULA32X16_L0:
    return 377;
  case Haydn::MULA32X16_L1:
    return 378;
  case Haydn::MULA32X16_L2:
    return 379;
  case Haydn::MULA32X16_L3:
    return 380;
  case Haydn::MULA64_HH:
    return 381;
  case Haydn::MULA64_HL:
    return 382;
  case Haydn::MULA64_HUH:
    return 383;
  case Haydn::MULA64_HUL:
    return 384;
  case Haydn::MULA64_LH:
    return 385;
  case Haydn::MULA64_LL:
    return 386;
  case Haydn::MULA64_LUH:
    return 387;
  case Haydn::MULA64_LUL:
    return 388;
  case Haydn::MULA64_UHH:
    return 389;
  case Haydn::MULA64_UHL:
    return 390;
  case Haydn::MULA64_UHUH:
    return 391;
  case Haydn::MULA64_UHUL:
    return 392;
  case Haydn::MULA64_ULH:
    return 393;
  case Haydn::MULA64_ULL:
    return 394;
  case Haydn::MULA64_ULUH:
    return 395;
  case Haydn::MULA64_ULUL:
    return 396;
  case Haydn::MULAA32X16_H0_L1:
    return 397;
  case Haydn::MULAA32X16_H1_L0:
    return 398;
  case Haydn::MULAA32X16_H2_L3:
    return 399;
  case Haydn::MULAA32X16_H3_L2:
    return 400;
  case Haydn::MULAA32_HHLL:
    return 401;
  case Haydn::MULAA32_HLLH:
    return 402;
  case Haydn::MULAS32X16_H1_L0:
    return 403;
  case Haydn::MULAS32X16_H3_L2:
    return 404;
  case Haydn::MULAS32_HHLL:
    return 405;
  case Haydn::MULAS32_HLLH:
    return 406;
  case Haydn::MULAS64_HH:
    return 407;
  case Haydn::MULAS64_HL:
    return 408;
  case Haydn::MULAS64_HUH:
    return 409;
  case Haydn::MULAS64_HUL:
    return 410;
  case Haydn::MULAS64_LH:
    return 411;
  case Haydn::MULAS64_LL:
    return 412;
  case Haydn::MULAS64_LUH:
    return 413;
  case Haydn::MULAS64_LUL:
    return 414;
  case Haydn::MULAS64_UHH:
    return 415;
  case Haydn::MULAS64_UHL:
    return 416;
  case Haydn::MULAS64_UHUH:
    return 417;
  case Haydn::MULAS64_UHUL:
    return 418;
  case Haydn::MULAS64_ULH:
    return 419;
  case Haydn::MULAS64_ULL:
    return 420;
  case Haydn::MULAS64_ULUH:
    return 421;
  case Haydn::MULAS64_ULUL:
    return 422;
  case Haydn::MULL:
    return 423;
  case Haydn::MULS32X16_H0:
    return 424;
  case Haydn::MULS32X16_H1:
    return 425;
  case Haydn::MULS32X16_H2:
    return 426;
  case Haydn::MULS32X16_H3:
    return 427;
  case Haydn::MULS32X16_L0:
    return 428;
  case Haydn::MULS32X16_L1:
    return 429;
  case Haydn::MULS32X16_L2:
    return 430;
  case Haydn::MULS32X16_L3:
    return 431;
  case Haydn::MULS64_HH:
    return 432;
  case Haydn::MULS64_HL:
    return 433;
  case Haydn::MULS64_HUH:
    return 434;
  case Haydn::MULS64_HUL:
    return 435;
  case Haydn::MULS64_LH:
    return 436;
  case Haydn::MULS64_LL:
    return 437;
  case Haydn::MULS64_LUH:
    return 438;
  case Haydn::MULS64_LUL:
    return 439;
  case Haydn::MULS64_UHH:
    return 440;
  case Haydn::MULS64_UHL:
    return 441;
  case Haydn::MULS64_UHUH:
    return 442;
  case Haydn::MULS64_UHUL:
    return 443;
  case Haydn::MULS64_ULH:
    return 444;
  case Haydn::MULS64_ULL:
    return 445;
  case Haydn::MULS64_ULUH:
    return 446;
  case Haydn::MULS64_ULUL:
    return 447;
  case Haydn::MULSA32X16_H1_L0:
    return 448;
  case Haydn::MULSA32X16_H3_L2:
    return 449;
  case Haydn::MULSA32_HHLL:
    return 450;
  case Haydn::MULSA32_HLLH:
    return 451;
  case Haydn::MULSS32X16_H1_L0:
    return 452;
  case Haydn::MULSS32X16_H3_L2:
    return 453;
  case Haydn::MULSS32_HHLL:
    return 454;
  case Haydn::MULSS32_HLLH:
    return 455;
  case Haydn::MULSS64_HH:
    return 456;
  case Haydn::MULSS64_HL:
    return 457;
  case Haydn::MULSS64_HUH:
    return 458;
  case Haydn::MULSS64_HUL:
    return 459;
  case Haydn::MULSS64_LH:
    return 460;
  case Haydn::MULSS64_LL:
    return 461;
  case Haydn::MULSS64_LUH:
    return 462;
  case Haydn::MULSS64_LUL:
    return 463;
  case Haydn::MULSS64_UHH:
    return 464;
  case Haydn::MULSS64_UHL:
    return 465;
  case Haydn::MULSS64_UHUH:
    return 466;
  case Haydn::MULSS64_UHUL:
    return 467;
  case Haydn::MULSS64_ULH:
    return 468;
  case Haydn::MULSS64_ULL:
    return 469;
  case Haydn::MULSS64_ULUH:
    return 470;
  case Haydn::MULSS64_ULUL:
    return 471;
  case Haydn::MULSSH:
    return 472;
  case Haydn::MULSUH:
    return 473;
  case Haydn::MULUUH:
    return 474;
  case Haydn::MULZAA32X16_H0_L1:
    return 475;
  case Haydn::MULZAA32X16_H1_L0:
    return 476;
  case Haydn::MULZAA32X16_H2_L3:
    return 477;
  case Haydn::MULZAA32X16_H3_L2:
    return 478;
  case Haydn::MULZAA32_HHLL:
    return 479;
  case Haydn::MULZAA32_HLLH:
    return 480;
  case Haydn::MULZAS32X16_H1_L0:
    return 481;
  case Haydn::MULZAS32X16_H3_L2:
    return 482;
  case Haydn::MULZAS32_HHLL:
    return 483;
  case Haydn::MULZAS32_HLLH:
    return 484;
  case Haydn::MULZSA32X16_H1_L0:
    return 485;
  case Haydn::MULZSA32X16_H3_L2:
    return 486;
  case Haydn::MULZSA32_HHLL:
    return 487;
  case Haydn::MULZSA32_HLLH:
    return 488;
  case Haydn::MULZSS32X16_H1_L0:
    return 489;
  case Haydn::MULZSS32X16_H3_L2:
    return 490;
  case Haydn::MULZSS32_HHLL:
    return 491;
  case Haydn::MULZSS32_HLLH:
    return 492;
  case Haydn::NEG32:
    return 493;
  case Haydn::NEG32S:
    return 494;
  case Haydn::NEG64:
    return 495;
  case Haydn::NEG64S:
    return 496;
  case Haydn::NOP:
    return 497;
  case Haydn::NOT32:
    return 498;
  case Haydn::NOT64:
    return 499;
  case Haydn::NSA16_L:
    return 500;
  case Haydn::NSA32:
    return 501;
  case Haydn::NSA32_L:
    return 502;
  case Haydn::NSA64:
    return 503;
  case Haydn::NSAU32:
    return 504;
  case Haydn::NSAZ16_L:
    return 505;
  case Haydn::NSAZ32_L:
    return 506;
  case Haydn::NSAZ64:
    return 507;
  case Haydn::OR32:
    return 508;
  case Haydn::OR64:
    return 509;
  case Haydn::ORI32:
    return 510;
  case Haydn::ORI32_W:
    return 511;
  case Haydn::PLDWWUA:
    return 512;
  case Haydn::PLDWWUA_POST:
    return 513;
  case Haydn::PLQHWUA_CB_POST:
    return 514;
  case Haydn::PLTWWUA_CB_POST:
    return 515;
  case Haydn::POPCOUNT32:
    return 516;
  case Haydn::POPCOUNT64:
    return 517;
  case Haydn::RECIP:
    return 518;
  case Haydn::SEQ32:
    return 519;
  case Haydn::SEQ64:
    return 520;
  case Haydn::SET_HWLOOP_F2:
    return 521;
  case Haydn::SET_HWLOOP_F2_W:
    return 522;
  case Haydn::SET_HWLOOP_REG_W:
    return 523;
  case Haydn::SET_HWLOOP_W:
    return 524;
  case Haydn::SEXT32T64:
    return 525;
  case Haydn::SEXT_GPR32_TO_DR64:
    return 526;
  case Haydn::SHL32:
    return 527;
  case Haydn::SIN_COS:
    return 528;
  case Haydn::SLE32:
    return 529;
  case Haydn::SLE64:
    return 530;
  case Haydn::SLL32:
    return 531;
  case Haydn::SLL64:
    return 532;
  case Haydn::SLLI32:
    return 533;
  case Haydn::SLLI64:
    return 534;
  case Haydn::SLT32:
    return 535;
  case Haydn::SLT64:
    return 536;
  case Haydn::SLTU32:
    return 537;
  case Haydn::SMULA16S_00:
    return 538;
  case Haydn::SMULA16S_10:
    return 539;
  case Haydn::SMULA16S_11:
    return 540;
  case Haydn::SMULA16S_20:
    return 541;
  case Haydn::SMULA16S_21:
    return 542;
  case Haydn::SMULA16S_22:
    return 543;
  case Haydn::SMULA16S_30:
    return 544;
  case Haydn::SMULA16S_31:
    return 545;
  case Haydn::SMULA16S_32:
    return 546;
  case Haydn::SMULA16S_33:
    return 547;
  case Haydn::SMULA16_00:
    return 548;
  case Haydn::SMULA16_10:
    return 549;
  case Haydn::SMULA16_11:
    return 550;
  case Haydn::SMULA16_20:
    return 551;
  case Haydn::SMULA16_21:
    return 552;
  case Haydn::SMULA16_22:
    return 553;
  case Haydn::SMULA16_30:
    return 554;
  case Haydn::SMULA16_31:
    return 555;
  case Haydn::SMULA16_32:
    return 556;
  case Haydn::SMULA16_33:
    return 557;
  case Haydn::SMULS16S_00:
    return 558;
  case Haydn::SMULS16S_10:
    return 559;
  case Haydn::SMULS16S_11:
    return 560;
  case Haydn::SMULS16S_20:
    return 561;
  case Haydn::SMULS16S_21:
    return 562;
  case Haydn::SMULS16S_22:
    return 563;
  case Haydn::SMULS16S_30:
    return 564;
  case Haydn::SMULS16S_31:
    return 565;
  case Haydn::SMULS16S_32:
    return 566;
  case Haydn::SMULS16S_33:
    return 567;
  case Haydn::SMULS16_00:
    return 568;
  case Haydn::SMULS16_10:
    return 569;
  case Haydn::SMULS16_11:
    return 570;
  case Haydn::SMULS16_20:
    return 571;
  case Haydn::SMULS16_21:
    return 572;
  case Haydn::SMULS16_22:
    return 573;
  case Haydn::SMULS16_30:
    return 574;
  case Haydn::SMULS16_31:
    return 575;
  case Haydn::SMULS16_32:
    return 576;
  case Haydn::SMULS16_33:
    return 577;
  case Haydn::SQRT:
    return 578;
  case Haydn::SRA32:
    return 579;
  case Haydn::SRA32R:
    return 580;
  case Haydn::SRA64:
    return 581;
  case Haydn::SRA64R:
    return 582;
  case Haydn::SRAI32:
    return 583;
  case Haydn::SRAI32R:
    return 584;
  case Haydn::SRAI64:
    return 585;
  case Haydn::SRAI64R:
    return 586;
  case Haydn::SRL32:
    return 587;
  case Haydn::SRL64:
    return 588;
  case Haydn::SRLI32:
    return 589;
  case Haydn::SRLI64:
    return 590;
  case Haydn::ST16:
    return 591;
  case Haydn::ST32:
    return 592;
  case Haydn::ST32_POST:
    return 593;
  case Haydn::ST32_REG_M0S0LS:
    return 594;
  case Haydn::ST64:
    return 595;
  case Haydn::ST64_POST:
    return 596;
  case Haydn::ST64_REG_M0S0LS:
    return 597;
  case Haydn::ST8:
    return 598;
  case Haydn::SUB32:
    return 599;
  case Haydn::SUB32S:
    return 600;
  case Haydn::SUB64:
    return 601;
  case Haydn::SUB64S:
    return 602;
  case Haydn::SUB64S_H:
    return 603;
  case Haydn::SUB64S_L:
    return 604;
  case Haydn::SUB64_H:
    return 605;
  case Haydn::SUB64_L:
    return 606;
  case Haydn::SUBI32:
    return 607;
  case Haydn::SUBI32S:
    return 608;
  case Haydn::S_LBS_POST_IMM:
    return 609;
  case Haydn::S_LBS_POST_REG:
    return 610;
  case Haydn::S_LBS_PRE_IMM:
    return 611;
  case Haydn::S_LBS_PRE_REG:
    return 612;
  case Haydn::S_LBS_WITH_IMM:
    return 613;
  case Haydn::S_LBS_WITH_REG:
    return 614;
  case Haydn::S_LBU_POST_IMM:
    return 615;
  case Haydn::S_LBU_POST_REG:
    return 616;
  case Haydn::S_LBU_PRE_IMM:
    return 617;
  case Haydn::S_LBU_PRE_REG:
    return 618;
  case Haydn::S_LBU_WITH_IMM:
    return 619;
  case Haydn::S_LBU_WITH_REG:
    return 620;
  case Haydn::S_LHWS_POST_IMM:
    return 621;
  case Haydn::S_LHWS_POST_REG:
    return 622;
  case Haydn::S_LHWS_PRE_IMM:
    return 623;
  case Haydn::S_LHWS_PRE_REG:
    return 624;
  case Haydn::S_LHWS_WITH_IMM:
    return 625;
  case Haydn::S_LHWS_WITH_REG:
    return 626;
  case Haydn::S_LHWU_POST_IMM:
    return 627;
  case Haydn::S_LHWU_POST_REG:
    return 628;
  case Haydn::S_LHWU_PRE_IMM:
    return 629;
  case Haydn::S_LHWU_PRE_REG:
    return 630;
  case Haydn::S_LHWU_WITH_IMM:
    return 631;
  case Haydn::S_LHWU_WITH_REG:
    return 632;
  case Haydn::S_LW_BREV_IMM:
    return 633;
  case Haydn::S_LW_BREV_REG:
    return 634;
  case Haydn::S_LW_POST_IMM:
    return 635;
  case Haydn::S_LW_POST_REG:
    return 636;
  case Haydn::S_LW_PRE_IMM:
    return 637;
  case Haydn::S_LW_PRE_REG:
    return 638;
  case Haydn::S_LW_WITH_IMM:
    return 639;
  case Haydn::S_LW_WITH_REG:
    return 640;
  case Haydn::S_SB_POST_IMM:
    return 641;
  case Haydn::S_SB_POST_REG:
    return 642;
  case Haydn::S_SB_PRE_IMM:
    return 643;
  case Haydn::S_SB_PRE_REG:
    return 644;
  case Haydn::S_SB_WITH_IMM:
    return 645;
  case Haydn::S_SB_WITH_REG:
    return 646;
  case Haydn::S_SHW_POST_IMM:
    return 647;
  case Haydn::S_SHW_POST_REG:
    return 648;
  case Haydn::S_SHW_PRE_IMM:
    return 649;
  case Haydn::S_SHW_PRE_REG:
    return 650;
  case Haydn::S_SHW_WITH_IMM:
    return 651;
  case Haydn::S_SHW_WITH_REG:
    return 652;
  case Haydn::S_SW_BREV_IMM:
    return 653;
  case Haydn::S_SW_BREV_REG:
    return 654;
  case Haydn::S_SW_POST_IMM:
    return 655;
  case Haydn::S_SW_POST_REG:
    return 656;
  case Haydn::S_SW_PRE_IMM:
    return 657;
  case Haydn::S_SW_PRE_REG:
    return 658;
  case Haydn::S_SW_WITH_IMM:
    return 659;
  case Haydn::S_SW_WITH_REG:
    return 660;
  case Haydn::TRANSF64:
    return 661;
  case Haydn::TRANSF64F2:
    return 662;
  case Haydn::TRANSF64_H:
    return 663;
  case Haydn::TRANSF64_L:
    return 664;
  case Haydn::WBARWUA:
    return 665;
  case Haydn::WBARWUA_CB:
    return 666;
  case Haydn::WFI:
    return 667;
  case Haydn::X2ABS32:
    return 668;
  case Haydn::X2ABS32S:
    return 669;
  case Haydn::X2ADD32:
    return 670;
  case Haydn::X2ADD32S:
    return 671;
  case Haydn::X2ADD32S_HLLH:
    return 672;
  case Haydn::X2ADD32_HLLH:
    return 673;
  case Haydn::X2ADDSUB32:
    return 674;
  case Haydn::X2ADDSUB32S:
    return 675;
  case Haydn::X2ADDSUB32S_HLLH:
    return 676;
  case Haydn::X2ADDSUB32_HLLH:
    return 677;
  case Haydn::X2CLAMP32:
    return 678;
  case Haydn::X2CMUL32:
    return 679;
  case Haydn::X2CMUL32S:
    return 680;
  case Haydn::X2CMUL32S_F2:
    return 681;
  case Haydn::X2CMUL32X16_H:
    return 682;
  case Haydn::X2CMUL32X16_L:
    return 683;
  case Haydn::X2CMUL32_F2:
    return 684;
  case Haydn::X2CMULA32X16_H:
    return 685;
  case Haydn::X2CMULA32X16_L:
    return 686;
  case Haydn::X2DOT32:
    return 687;
  case Haydn::X2FCMUL32RS:
    return 688;
  case Haydn::X2FCMUL32RSS:
    return 689;
  case Haydn::X2FCMUL32X16RS_H:
    return 690;
  case Haydn::X2FCMUL32X16RS_L:
    return 691;
  case Haydn::X2FCMULA32RS:
    return 692;
  case Haydn::X2FCMULA32RSS:
    return 693;
  case Haydn::X2FCMULA32X16RS_H:
    return 694;
  case Haydn::X2FCMULA32X16RS_L:
    return 695;
  case Haydn::X2FF2RSST32:
    return 696;
  case Haydn::X2FF2RST32:
    return 697;
  case Haydn::X2FMUL32RS:
    return 698;
  case Haydn::X2FMUL32RSS:
    return 699;
  case Haydn::X2FMUL32TS:
    return 700;
  case Haydn::X2FMUL32X16RSS_H:
    return 701;
  case Haydn::X2FMUL32X16RSS_L:
    return 702;
  case Haydn::X2FMUL32X16RS_H:
    return 703;
  case Haydn::X2FMUL32X16RS_L:
    return 704;
  case Haydn::X2FMUL32X16TS_H:
    return 705;
  case Haydn::X2FMUL32X16TS_L:
    return 706;
  case Haydn::X2FMULA32RS:
    return 707;
  case Haydn::X2FMULA32RSS:
    return 708;
  case Haydn::X2FMULA32TS:
    return 709;
  case Haydn::X2FMULA32X16RSS_H:
    return 710;
  case Haydn::X2FMULA32X16RSS_L:
    return 711;
  case Haydn::X2FMULA32X16RS_H:
    return 712;
  case Haydn::X2FMULA32X16RS_L:
    return 713;
  case Haydn::X2FMULA32X16TS_H:
    return 714;
  case Haydn::X2FMULA32X16TS_L:
    return 715;
  case Haydn::X2FMULS32RS:
    return 716;
  case Haydn::X2FMULS32RSS:
    return 717;
  case Haydn::X2FMULS32TS:
    return 718;
  case Haydn::X2FMULS32X16RSS_H:
    return 719;
  case Haydn::X2FMULS32X16RSS_L:
    return 720;
  case Haydn::X2FMULS32X16RS_H:
    return 721;
  case Haydn::X2FMULS32X16RS_L:
    return 722;
  case Haydn::X2FMULS32X16TS_H:
    return 723;
  case Haydn::X2FMULS32X16TS_L:
    return 724;
  case Haydn::X2FRSST32:
    return 725;
  case Haydn::X2FRST32:
    return 726;
  case Haydn::X2HADD32S_H:
    return 727;
  case Haydn::X2HADD32S_L:
    return 728;
  case Haydn::X2HADD32_H:
    return 729;
  case Haydn::X2HADD32_L:
    return 730;
  case Haydn::X2HMAX32:
    return 731;
  case Haydn::X2HMIN32:
    return 732;
  case Haydn::X2MAX32:
    return 733;
  case Haydn::X2MIN32:
    return 734;
  case Haydn::X2MJSWAP32:
    return 735;
  case Haydn::X2MJSWAP32S:
    return 736;
  case Haydn::X2MOVF32:
    return 737;
  case Haydn::X2MOVT32:
    return 738;
  case Haydn::X2MUL32:
    return 739;
  case Haydn::X2MUL32X16_H:
    return 740;
  case Haydn::X2MUL32X16_L:
    return 741;
  case Haydn::X2MULA32:
    return 742;
  case Haydn::X2MULA32X16_H:
    return 743;
  case Haydn::X2MULA32X16_L:
    return 744;
  case Haydn::X2MULAPH32:
    return 745;
  case Haydn::X2MULAPL32:
    return 746;
  case Haydn::X2MULPH32:
    return 747;
  case Haydn::X2MULPL32:
    return 748;
  case Haydn::X2MULS32:
    return 749;
  case Haydn::X2MULS32X16_H:
    return 750;
  case Haydn::X2MULS32X16_L:
    return 751;
  case Haydn::X2MULSPH32:
    return 752;
  case Haydn::X2MULSPL32:
    return 753;
  case Haydn::X2NEG32:
    return 754;
  case Haydn::X2NEG32S:
    return 755;
  case Haydn::X2NEG32S_L:
    return 756;
  case Haydn::X2NEG32_L:
    return 757;
  case Haydn::X2SEL32_HH:
    return 758;
  case Haydn::X2SEL32_HL:
    return 759;
  case Haydn::X2SEL32_LH:
    return 760;
  case Haydn::X2SEL32_LL:
    return 761;
  case Haydn::X2SEQ32:
    return 762;
  case Haydn::X2SLE32:
    return 763;
  case Haydn::X2SLL32:
    return 764;
  case Haydn::X2SLLI32:
    return 765;
  case Haydn::X2SLT32:
    return 766;
  case Haydn::X2SRA32:
    return 767;
  case Haydn::X2SRA32R:
    return 768;
  case Haydn::X2SRAI32:
    return 769;
  case Haydn::X2SRAI32R:
    return 770;
  case Haydn::X2SRL32:
    return 771;
  case Haydn::X2SRLI32:
    return 772;
  case Haydn::X2SUB32:
    return 773;
  case Haydn::X2SUB32S:
    return 774;
  case Haydn::X2SUB32S_HLLH:
    return 775;
  case Haydn::X2SUB32_HLLH:
    return 776;
  case Haydn::X2SUBADD32:
    return 777;
  case Haydn::X2SUBADD32S:
    return 778;
  case Haydn::X2SUBADD32S_HLLH:
    return 779;
  case Haydn::X2SUBADD32_HLLH:
    return 780;
  case Haydn::X2SWAP32:
    return 781;
  case Haydn::X4ABS16:
    return 782;
  case Haydn::X4ABS16S:
    return 783;
  case Haydn::X4ADD16:
    return 784;
  case Haydn::X4ADD16S:
    return 785;
  case Haydn::X4CJMUL16S_H:
    return 786;
  case Haydn::X4CJMUL16S_L:
    return 787;
  case Haydn::X4CJMULA16S_H:
    return 788;
  case Haydn::X4CJMULA16S_L:
    return 789;
  case Haydn::X4CLAMP16:
    return 790;
  case Haydn::X4CMUL16:
    return 791;
  case Haydn::X4CMUL16S:
    return 792;
  case Haydn::X4CMUL16S_F2:
    return 793;
  case Haydn::X4CMUL16S_H:
    return 794;
  case Haydn::X4CMUL16S_L:
    return 795;
  case Haydn::X4CMUL16_F2:
    return 796;
  case Haydn::X4CMULA16S_H:
    return 797;
  case Haydn::X4CMULA16S_L:
    return 798;
  case Haydn::X4CONJ16:
    return 799;
  case Haydn::X4CONJ16S:
    return 800;
  case Haydn::X4DOT16:
    return 801;
  case Haydn::X4ENERGY16:
    return 802;
  case Haydn::X4FCMUL16RS:
    return 803;
  case Haydn::X4FCMUL16RSS:
    return 804;
  case Haydn::X4FCMULA16RS:
    return 805;
  case Haydn::X4FCMULA16RSS:
    return 806;
  case Haydn::X4FF2MUL16S:
    return 807;
  case Haydn::X4FF2MULA16S:
    return 808;
  case Haydn::X4FF2MULS16S:
    return 809;
  case Haydn::X4FMUL16RS:
    return 810;
  case Haydn::X4FMUL16RSS:
    return 811;
  case Haydn::X4FMUL16TS:
    return 812;
  case Haydn::X4FRSST16:
    return 813;
  case Haydn::X4FRST16:
    return 814;
  case Haydn::X4HADD16_H:
    return 815;
  case Haydn::X4HADD16_L:
    return 816;
  case Haydn::X4HMAX16:
    return 817;
  case Haydn::X4HMIN16:
    return 818;
  case Haydn::X4MAX16:
    return 819;
  case Haydn::X4MIN16:
    return 820;
  case Haydn::X4MJSWAP16:
    return 821;
  case Haydn::X4MJSWAP16S:
    return 822;
  case Haydn::X4MOVF16:
    return 823;
  case Haydn::X4MOVT16:
    return 824;
  case Haydn::X4MUL16:
    return 825;
  case Haydn::X4MULA16:
    return 826;
  case Haydn::X4MULA16S:
    return 827;
  case Haydn::X4MULS16:
    return 828;
  case Haydn::X4MULS16S:
    return 829;
  case Haydn::X4NEG16:
    return 830;
  case Haydn::X4NEG16S:
    return 831;
  case Haydn::X4SAT32T16:
    return 832;
  case Haydn::X4SEL16:
    return 833;
  case Haydn::X4SELI16:
    return 834;
  case Haydn::X4SEQ16:
    return 835;
  case Haydn::X4SLE16:
    return 836;
  case Haydn::X4SLL16:
    return 837;
  case Haydn::X4SLLI16:
    return 838;
  case Haydn::X4SLT16:
    return 839;
  case Haydn::X4SRA16:
    return 840;
  case Haydn::X4SRA16R:
    return 841;
  case Haydn::X4SRAI16:
    return 842;
  case Haydn::X4SRAI16R:
    return 843;
  case Haydn::X4SRL16:
    return 844;
  case Haydn::X4SRLI16:
    return 845;
  case Haydn::X4SUB16:
    return 846;
  case Haydn::X4SUB16S:
    return 847;
  case Haydn::X4SWAP16:
    return 848;
  case Haydn::XOR32:
    return 849;
  case Haydn::XOR64:
    return 850;
  case Haydn::XORI32:
    return 851;
  case Haydn::ZERO_DR:
    return 852;
  case Haydn::ZERO_GPR:
    return 853;
  case Haydn::ZERO_SFR:
    return 854;
  }
}
#endif // GET_HAYDN_ALT_OCCUPANCY
