//===-- HaydnGenPerOpResources.inc - per-op resource import -*- C++ -*-===//
//
// DO NOT EDIT.
// Generator: llvm/lib/Target/Haydn/FormatE/generate_sched_records.py
// Schema: format-e-records/1
// Family: e96 (id=0)
// Check: python3 llvm/lib/Target/Haydn/FormatE/generate_sched_records.py --check --family e96
// Umbrella: llvm/lib/Target/Haydn/FormatE/check_generated.sh
// Overlay: authored_catalog_overlay.json
// Overlay-SHA256: 276dea569f8761e81bb3d17e041a4f74911d34ba6f30711939a293519ac531fc
//
// M18 import from golden instruction_type_index.json: one
// HaydnAdmittedPerOpResourceRecord per compiler-reachable logical
// with a golden row. UnitMask bits are the seven shared units
// (LOADSTORE0, LOAD1, ALU0, ALU1, ALU2, MAC0, MAC1); port counts
// are per-bank golden port lists; DataLatency is golden
// Pipeline_Info (0 = golden silent:
// stores/branches/hints publish no latency); conservative bound
// (uimm4+2 dest) rides OperandCycles like the published SinCosLat
// class. PipelineOccupancy=1 is the same product InstrStage<1,...>
// law the itineraries publish. CompleteModel stays 0: the census
// below is the explicit uncovered set (GE96-04).
//
//===----------------------------------------------------------------------===//

#ifdef GET_HAYDN_PER_OP_RESOURCES
#undef GET_HAYDN_PER_OP_RESOURCES

static_assert(HAYDN_ADMITTED_OPERAND_CYCLE_ROOM ==
                  8u,
              "generator/C++ OperandCycles room drift");

static constexpr HaydnAdmittedPerOpResourceRecord Table[] = {
    { // ABS32 (R) units=0x1C
        Haydn::ABS32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ABS32S (R) units=0x1C
        Haydn::ABS32S, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ABS64 (R) units=0x1C
        Haydn::ABS64, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ABS64S (R) units=0x1C
        Haydn::ABS64S, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ADD32 (RR) units=0x1C
        Haydn::ADD32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ADD32S (RR) units=0x1C
        Haydn::ADD32S, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ADD64 (RR) units=0x1C
        Haydn::ADD64, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ADD64S (RR) units=0x1C
        Haydn::ADD64S, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ADD64S_H (RR) units=0x1C
        Haydn::ADD64S_H, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ADD64S_L (RR) units=0x1C
        Haydn::ADD64S_L, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ADD64_H (RR) units=0x1C
        Haydn::ADD64_H, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ADD64_L (RR) units=0x1C
        Haydn::ADD64_L, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ADDI32 (RI20) units=0x1C
        Haydn::ADDI32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ADDI32S (RI20) units=0x1C
        Haydn::ADDI32S, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ADDI32_W (RI20) units=0x1C
        Haydn::ADDI32_W, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // AND32 (RR) units=0x1C
        Haydn::AND32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // AND64 (RR) units=0x1C
        Haydn::AND64, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ANDI32 (RI20) units=0x1C
        Haydn::ANDI32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ARCTAN (RI4) units=0x18
        Haydn::ARCTAN, 0, 1, 1, 0, 0, 0, 0, 0, 0x18u,
        0u, 1u, {17},
        1u, 0u,
    },
    { // ASR32 (RR) units=0x1C
        Haydn::ASR32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // BEQ (RI12) units=0x04
        Haydn::BEQ, 2, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BEQZ (I12) units=0x04
        Haydn::BEQZ, 1, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BEQZ_W (I12) units=0x04
        Haydn::BEQZ_W, 1, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BEQ_W (RI12) units=0x04
        Haydn::BEQ_W, 2, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BGE (RI12) units=0x04
        Haydn::BGE, 2, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BGEU (RI12) units=0x04
        Haydn::BGEU, 2, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BGEU_W (RI12) units=0x04
        Haydn::BGEU_W, 2, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BGEZ (I12) units=0x04
        Haydn::BGEZ, 1, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BGEZ_W (I12) units=0x04
        Haydn::BGEZ_W, 1, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BGE_W (RI12) units=0x04
        Haydn::BGE_W, 2, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BLT (RI12) units=0x04
        Haydn::BLT, 2, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BLTU (RI12) units=0x04
        Haydn::BLTU, 2, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BLTU_W (RI12) units=0x04
        Haydn::BLTU_W, 2, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BLTZ (I12) units=0x04
        Haydn::BLTZ, 1, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BLTZ_W (I12) units=0x04
        Haydn::BLTZ_W, 1, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BLT_W (RI12) units=0x04
        Haydn::BLT_W, 2, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BNE (RI12) units=0x04
        Haydn::BNE, 2, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BNEZ (I12) units=0x04
        Haydn::BNEZ, 1, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BNEZ_W (I12) units=0x04
        Haydn::BNEZ_W, 1, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BNE_W (RI12) units=0x04
        Haydn::BNE_W, 2, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // BREV32 (RR) units=0x1C
        Haydn::BREV32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // CSRR (I8) units=0x1C
        Haydn::CSRR, 0, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        2u, 1u, {2},
        1u, 0u,
    },
    { // CSRW (I8) units=0x1C
        Haydn::CSRW, 1, 0, 0, 0, 0, 0, 0, 0, 0x1Cu,
        2u, 1u, {2},
        1u, 0u,
    },
    { // CSRW_W (I8) units=0x1C
        Haydn::CSRW_W, 1, 0, 0, 0, 0, 0, 0, 0, 0x1Cu,
        2u, 1u, {2},
        1u, 0u,
    },
    { // D_LDW_BREV_IMM (RI6) units=0x03
        Haydn::D_LDW_BREV_IMM, 1, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 8u,
    },
    { // D_LDW_BREV_REG (RR) units=0x03
        Haydn::D_LDW_BREV_REG, 2, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 8u,
    },
    { // D_LDW_CB_IMM (CBRI) units=0x01
        Haydn::D_LDW_CB_IMM, 1, 1, 0, 1, 0, 0, 0, 0, 0x01u,
        2u, 1u, {2},
        1u, 8u,
    },
    { // D_LDW_CB_REG (CBRR) units=0x01
        Haydn::D_LDW_CB_REG, 2, 1, 0, 1, 0, 0, 0, 0, 0x01u,
        2u, 1u, {2},
        1u, 8u,
    },
    { // D_LDW_POST_IMM (RI6) units=0x03
        Haydn::D_LDW_POST_IMM, 1, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 8u,
    },
    { // D_LDW_POST_REG (RR) units=0x03
        Haydn::D_LDW_POST_REG, 2, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 8u,
    },
    { // D_LDW_PRE_IMM (RI6) units=0x03
        Haydn::D_LDW_PRE_IMM, 1, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 8u,
    },
    { // D_LDW_PRE_REG (RR) units=0x03
        Haydn::D_LDW_PRE_REG, 2, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 8u,
    },
    { // D_LDW_WITH_IMM (RI6) units=0x03
        Haydn::D_LDW_WITH_IMM, 1, 0, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 8u,
    },
    { // D_LDW_WITH_REG (RR) units=0x03
        Haydn::D_LDW_WITH_REG, 2, 0, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 8u,
    },
    { // D_LHW_POST_IMM (RI6) units=0x03
        Haydn::D_LHW_POST_IMM, 1, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // D_LHW_POST_REG (RR) units=0x03
        Haydn::D_LHW_POST_REG, 2, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // D_LHW_PRE_IMM (RI6) units=0x03
        Haydn::D_LHW_PRE_IMM, 1, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // D_LHW_PRE_REG (RR) units=0x03
        Haydn::D_LHW_PRE_REG, 2, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // D_LHW_WITH_IMM (RI6) units=0x03
        Haydn::D_LHW_WITH_IMM, 1, 0, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // D_LHW_WITH_REG (RR) units=0x03
        Haydn::D_LHW_WITH_REG, 2, 0, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // D_LQHWUA_CB_POST (AR_CBR) units=0x01
        Haydn::D_LQHWUA_CB_POST, 1, 1, 0, 1, 1, 1, 0, 0, 0x01u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // D_LQHWUA_POST (AR) units=0x03
        Haydn::D_LQHWUA_POST, 1, 1, 0, 1, 1, 1, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // D_LTWUA_CB_POST (AR_CBR) units=0x01
        Haydn::D_LTWUA_CB_POST, 1, 1, 0, 1, 1, 1, 0, 0, 0x01u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // D_LTWUA_POST (AR) units=0x03
        Haydn::D_LTWUA_POST, 1, 1, 0, 1, 1, 1, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // D_LW_POST_IMM (RI6) units=0x03
        Haydn::D_LW_POST_IMM, 1, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // D_LW_POST_REG (RR) units=0x03
        Haydn::D_LW_POST_REG, 2, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // D_LW_PRE_IMM (RI6) units=0x03
        Haydn::D_LW_PRE_IMM, 1, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // D_LW_PRE_REG (RR) units=0x03
        Haydn::D_LW_PRE_REG, 2, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // D_LW_WITH_IMM (RI6) units=0x03
        Haydn::D_LW_WITH_IMM, 1, 0, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // D_LW_WITH_REG (RR) units=0x03
        Haydn::D_LW_WITH_REG, 2, 0, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // D_SDW_BREV_IMM (RI6) units=0x01
        Haydn::D_SDW_BREV_IMM, 1, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 8u,
    },
    { // D_SDW_BREV_REG (RR) units=0x01
        Haydn::D_SDW_BREV_REG, 2, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 8u,
    },
    { // D_SDW_CB_IMM (CBRI) units=0x01
        Haydn::D_SDW_CB_IMM, 1, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 8u,
    },
    { // D_SDW_CB_REG (CBRR) units=0x01
        Haydn::D_SDW_CB_REG, 2, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 8u,
    },
    { // D_SDW_POST_IMM (RI6) units=0x01
        Haydn::D_SDW_POST_IMM, 1, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 8u,
    },
    { // D_SDW_POST_REG (RR) units=0x01
        Haydn::D_SDW_POST_REG, 2, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 8u,
    },
    { // D_SDW_PRE_IMM (RI6) units=0x01
        Haydn::D_SDW_PRE_IMM, 1, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 8u,
    },
    { // D_SDW_PRE_REG (RR) units=0x01
        Haydn::D_SDW_PRE_REG, 2, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 8u,
    },
    { // D_SDW_WITH_IMM (RI6) units=0x01
        Haydn::D_SDW_WITH_IMM, 1, 0, 1, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 8u,
    },
    { // D_SDW_WITH_REG (RR) units=0x01
        Haydn::D_SDW_WITH_REG, 2, 0, 1, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 8u,
    },
    { // D_SHW_POST_IMM (RI6) units=0x01
        Haydn::D_SHW_POST_IMM, 1, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 2u,
    },
    { // D_SHW_POST_REG (RR) units=0x01
        Haydn::D_SHW_POST_REG, 2, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 2u,
    },
    { // D_SHW_PRE_IMM (RI6) units=0x01
        Haydn::D_SHW_PRE_IMM, 1, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 2u,
    },
    { // D_SHW_PRE_REG (RR) units=0x01
        Haydn::D_SHW_PRE_REG, 2, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 2u,
    },
    { // D_SHW_WITH_IMM (RI6) units=0x01
        Haydn::D_SHW_WITH_IMM, 1, 0, 1, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 2u,
    },
    { // D_SHW_WITH_REG (RR) units=0x01
        Haydn::D_SHW_WITH_REG, 2, 0, 1, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 2u,
    },
    { // D_SQHWUA_CB_POST (AR_CBR) units=0x01
        Haydn::D_SQHWUA_CB_POST, 1, 1, 1, 0, 1, 1, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 2u,
    },
    { // D_SQHWUA_POST (AR) units=0x01
        Haydn::D_SQHWUA_POST, 1, 1, 1, 0, 1, 1, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 2u,
    },
    { // D_STWUA_CB_POST (AR_CBR) units=0x01
        Haydn::D_STWUA_CB_POST, 1, 1, 1, 0, 1, 1, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // D_STWUA_POST (AR) units=0x01
        Haydn::D_STWUA_POST, 1, 1, 1, 0, 1, 1, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // D_SW_F64RS_POST_IMM (RI6) units=0x01
        Haydn::D_SW_F64RS_POST_IMM, 1, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // D_SW_F64RS_POST_REG (RR) units=0x01
        Haydn::D_SW_F64RS_POST_REG, 2, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // D_SW_F64RS_WITH_IMM (RI6) units=0x01
        Haydn::D_SW_F64RS_WITH_IMM, 1, 0, 1, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 4u,
    },
    { // D_SW_F64RS_WITH_REG (RR) units=0x01
        Haydn::D_SW_F64RS_WITH_REG, 2, 0, 1, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 4u,
    },
    { // D_SW_H_POST_IMM (RI6) units=0x01
        Haydn::D_SW_H_POST_IMM, 1, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // D_SW_H_POST_REG (RR) units=0x01
        Haydn::D_SW_H_POST_REG, 2, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // D_SW_H_PRE_IMM (RI6) units=0x01
        Haydn::D_SW_H_PRE_IMM, 1, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // D_SW_H_PRE_REG (RR) units=0x01
        Haydn::D_SW_H_PRE_REG, 2, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // D_SW_H_WITH_IMM (RI6) units=0x01
        Haydn::D_SW_H_WITH_IMM, 1, 0, 1, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 4u,
    },
    { // D_SW_H_WITH_REG (RR) units=0x01
        Haydn::D_SW_H_WITH_REG, 2, 0, 1, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 4u,
    },
    { // D_SW_L_POST_IMM (RI6) units=0x01
        Haydn::D_SW_L_POST_IMM, 1, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // D_SW_L_POST_REG (RR) units=0x01
        Haydn::D_SW_L_POST_REG, 2, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // D_SW_L_PRE_IMM (RI6) units=0x01
        Haydn::D_SW_L_PRE_IMM, 1, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // D_SW_L_PRE_REG (RR) units=0x01
        Haydn::D_SW_L_PRE_REG, 2, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // D_SW_L_WITH_IMM (RI6) units=0x01
        Haydn::D_SW_L_WITH_IMM, 1, 0, 1, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 4u,
    },
    { // D_SW_L_WITH_REG (RR) units=0x01
        Haydn::D_SW_L_WITH_REG, 2, 0, 1, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 4u,
    },
    { // EXP2 (R) units=0x18
        Haydn::EXP2, 1, 1, 0, 0, 0, 0, 0, 0, 0x18u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULAA32RS_HHLL (RR) units=0x60
        Haydn::F2MULAA32RS_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULAA32RS_HLLH (RR) units=0x60
        Haydn::F2MULAA32RS_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULAA32R_HHLL (RR) units=0x60
        Haydn::F2MULAA32R_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULAA32R_HLLH (RR) units=0x60
        Haydn::F2MULAA32R_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULAS32RS_HHLL (RR) units=0x60
        Haydn::F2MULAS32RS_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULAS32RS_HLLH (RR) units=0x60
        Haydn::F2MULAS32RS_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULAS32R_HHLL (RR) units=0x60
        Haydn::F2MULAS32R_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULAS32R_HLLH (RR) units=0x60
        Haydn::F2MULAS32R_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULSA32RS_HHLL (RR) units=0x60
        Haydn::F2MULSA32RS_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULSA32RS_HLLH (RR) units=0x60
        Haydn::F2MULSA32RS_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULSA32R_HHLL (RR) units=0x60
        Haydn::F2MULSA32R_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULSA32R_HLLH (RR) units=0x60
        Haydn::F2MULSA32R_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULSS32RS_HHLL (RR) units=0x60
        Haydn::F2MULSS32RS_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULSS32RS_HLLH (RR) units=0x60
        Haydn::F2MULSS32RS_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULSS32R_HHLL (RR) units=0x60
        Haydn::F2MULSS32R_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULSS32R_HLLH (RR) units=0x60
        Haydn::F2MULSS32R_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZAA32RS_HHLL (RR) units=0x60
        Haydn::F2MULZAA32RS_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZAA32RS_HLLH (RR) units=0x60
        Haydn::F2MULZAA32RS_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZAA32R_HHLL (RR) units=0x60
        Haydn::F2MULZAA32R_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZAA32R_HLLH (RR) units=0x60
        Haydn::F2MULZAA32R_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZAS32RS_HHLL (RR) units=0x60
        Haydn::F2MULZAS32RS_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZAS32RS_HLLH (RR) units=0x60
        Haydn::F2MULZAS32RS_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZAS32R_HHLL (RR) units=0x60
        Haydn::F2MULZAS32R_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZAS32R_HLLH (RR) units=0x60
        Haydn::F2MULZAS32R_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZSA32RS_HHLL (RR) units=0x60
        Haydn::F2MULZSA32RS_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZSA32RS_HLLH (RR) units=0x60
        Haydn::F2MULZSA32RS_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZSA32R_HHLL (RR) units=0x60
        Haydn::F2MULZSA32R_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZSA32R_HLLH (RR) units=0x60
        Haydn::F2MULZSA32R_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZSS32RS_HHLL (RR) units=0x60
        Haydn::F2MULZSS32RS_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZSS32RS_HLLH (RR) units=0x60
        Haydn::F2MULZSS32RS_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZSS32R_HHLL (RR) units=0x60
        Haydn::F2MULZSS32R_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // F2MULZSS32R_HLLH (RR) units=0x60
        Haydn::F2MULZSS32R_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MUL32RS_HH (RR) units=0x60
        Haydn::FF2MUL32RS_HH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MUL32RS_LH (RR) units=0x60
        Haydn::FF2MUL32RS_LH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MUL32RS_LL (RR) units=0x60
        Haydn::FF2MUL32RS_LL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MUL32R_HH (RR) units=0x60
        Haydn::FF2MUL32R_HH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MUL32R_LH (RR) units=0x60
        Haydn::FF2MUL32R_LH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MUL32R_LL (RR) units=0x60
        Haydn::FF2MUL32R_LL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MULA32RS_HH (RR) units=0x60
        Haydn::FF2MULA32RS_HH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MULA32RS_LH (RR) units=0x60
        Haydn::FF2MULA32RS_LH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MULA32RS_LL (RR) units=0x60
        Haydn::FF2MULA32RS_LL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MULA32R_HH (RR) units=0x60
        Haydn::FF2MULA32R_HH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MULA32R_LH (RR) units=0x60
        Haydn::FF2MULA32R_LH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MULA32R_LL (RR) units=0x60
        Haydn::FF2MULA32R_LL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MULS32RS_HH (RR) units=0x60
        Haydn::FF2MULS32RS_HH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MULS32RS_LH (RR) units=0x60
        Haydn::FF2MULS32RS_LH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MULS32RS_LL (RR) units=0x60
        Haydn::FF2MULS32RS_LL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MULS32R_HH (RR) units=0x60
        Haydn::FF2MULS32R_HH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MULS32R_LH (RR) units=0x60
        Haydn::FF2MULS32R_LH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FF2MULS32R_LL (RR) units=0x60
        Haydn::FF2MULS32R_LL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FLAR (AR) units=0x03
        Haydn::FLAR, 0, 0, 0, 0, 0, 1, 0, 0, 0x03u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_HS00 (RR) units=0x60
        Haydn::FMUL16_HS00, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_HS01 (RR) units=0x60
        Haydn::FMUL16_HS01, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_HS02 (RR) units=0x60
        Haydn::FMUL16_HS02, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_HS03 (RR) units=0x60
        Haydn::FMUL16_HS03, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_HS11 (RR) units=0x60
        Haydn::FMUL16_HS11, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_HS12 (RR) units=0x60
        Haydn::FMUL16_HS12, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_HS13 (RR) units=0x60
        Haydn::FMUL16_HS13, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_HS22 (RR) units=0x60
        Haydn::FMUL16_HS22, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_HS23 (RR) units=0x60
        Haydn::FMUL16_HS23, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_HS33 (RR) units=0x60
        Haydn::FMUL16_HS33, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_LS00 (RR) units=0x60
        Haydn::FMUL16_LS00, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_LS01 (RR) units=0x60
        Haydn::FMUL16_LS01, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_LS02 (RR) units=0x60
        Haydn::FMUL16_LS02, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_LS03 (RR) units=0x60
        Haydn::FMUL16_LS03, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_LS11 (RR) units=0x60
        Haydn::FMUL16_LS11, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_LS12 (RR) units=0x60
        Haydn::FMUL16_LS12, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_LS13 (RR) units=0x60
        Haydn::FMUL16_LS13, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_LS22 (RR) units=0x60
        Haydn::FMUL16_LS22, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_LS23 (RR) units=0x60
        Haydn::FMUL16_LS23, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL16_LS33 (RR) units=0x60
        Haydn::FMUL16_LS33, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL32S_HH (RR) units=0x60
        Haydn::FMUL32S_HH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMUL32S_LH (RR) units=0x60
        Haydn::FMUL32S_LH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMUL32S_LL (RR) units=0x60
        Haydn::FMUL32S_LL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMUL32X16_H0 (RR) units=0x60
        Haydn::FMUL32X16_H0, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL32X16_H1 (RR) units=0x60
        Haydn::FMUL32X16_H1, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL32X16_H2 (RR) units=0x60
        Haydn::FMUL32X16_H2, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL32X16_H3 (RR) units=0x60
        Haydn::FMUL32X16_H3, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL32X16_L0 (RR) units=0x60
        Haydn::FMUL32X16_L0, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL32X16_L1 (RR) units=0x60
        Haydn::FMUL32X16_L1, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL32X16_L2 (RR) units=0x60
        Haydn::FMUL32X16_L2, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMUL32X16_L3 (RR) units=0x60
        Haydn::FMUL32X16_L3, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // FMULA16_HS00 (RR) units=0x60
        Haydn::FMULA16_HS00, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_HS01 (RR) units=0x60
        Haydn::FMULA16_HS01, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_HS02 (RR) units=0x60
        Haydn::FMULA16_HS02, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_HS03 (RR) units=0x60
        Haydn::FMULA16_HS03, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_HS11 (RR) units=0x60
        Haydn::FMULA16_HS11, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_HS12 (RR) units=0x60
        Haydn::FMULA16_HS12, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_HS13 (RR) units=0x60
        Haydn::FMULA16_HS13, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_HS22 (RR) units=0x60
        Haydn::FMULA16_HS22, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_HS23 (RR) units=0x60
        Haydn::FMULA16_HS23, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_HS33 (RR) units=0x60
        Haydn::FMULA16_HS33, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_LS00 (RR) units=0x60
        Haydn::FMULA16_LS00, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_LS01 (RR) units=0x60
        Haydn::FMULA16_LS01, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_LS02 (RR) units=0x60
        Haydn::FMULA16_LS02, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_LS03 (RR) units=0x60
        Haydn::FMULA16_LS03, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_LS11 (RR) units=0x60
        Haydn::FMULA16_LS11, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_LS12 (RR) units=0x60
        Haydn::FMULA16_LS12, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_LS13 (RR) units=0x60
        Haydn::FMULA16_LS13, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_LS22 (RR) units=0x60
        Haydn::FMULA16_LS22, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_LS23 (RR) units=0x60
        Haydn::FMULA16_LS23, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA16_LS33 (RR) units=0x60
        Haydn::FMULA16_LS33, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA32S_HH (RR) units=0x60
        Haydn::FMULA32S_HH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA32S_LH (RR) units=0x60
        Haydn::FMULA32S_LH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA32S_LL (RR) units=0x60
        Haydn::FMULA32S_LL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA32X16_H0 (RR) units=0x60
        Haydn::FMULA32X16_H0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA32X16_H1 (RR) units=0x60
        Haydn::FMULA32X16_H1, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA32X16_H2 (RR) units=0x60
        Haydn::FMULA32X16_H2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA32X16_H3 (RR) units=0x60
        Haydn::FMULA32X16_H3, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA32X16_L0 (RR) units=0x60
        Haydn::FMULA32X16_L0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA32X16_L1 (RR) units=0x60
        Haydn::FMULA32X16_L1, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA32X16_L2 (RR) units=0x60
        Haydn::FMULA32X16_L2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULA32X16_L3 (RR) units=0x60
        Haydn::FMULA32X16_L3, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAA16_HS_11_00 (RR) units=0x60
        Haydn::FMULAA16_HS_11_00, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAA16_HS_13_02 (RR) units=0x60
        Haydn::FMULAA16_HS_13_02, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAA16_HS_33_22 (RR) units=0x60
        Haydn::FMULAA16_HS_33_22, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAA16_LS_11_00 (RR) units=0x60
        Haydn::FMULAA16_LS_11_00, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAA16_LS_13_02 (RR) units=0x60
        Haydn::FMULAA16_LS_13_02, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAA16_LS_33_22 (RR) units=0x60
        Haydn::FMULAA16_LS_33_22, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAA32S_HHLL (RR) units=0x60
        Haydn::FMULAA32S_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAA32S_HLLH (RR) units=0x60
        Haydn::FMULAA32S_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAA32X16_H0_L1 (RR) units=0x60
        Haydn::FMULAA32X16_H0_L1, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAA32X16_H1_L0 (RR) units=0x60
        Haydn::FMULAA32X16_H1_L0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAA32X16_H2_L3 (RR) units=0x60
        Haydn::FMULAA32X16_H2_L3, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAA32X16_H3_L2 (RR) units=0x60
        Haydn::FMULAA32X16_H3_L2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAS32S_HHLL (RR) units=0x60
        Haydn::FMULAS32S_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAS32S_HLLH (RR) units=0x60
        Haydn::FMULAS32S_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAS32X16_H1_L0 (RR) units=0x60
        Haydn::FMULAS32X16_H1_L0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULAS32X16_H3_L2 (RR) units=0x60
        Haydn::FMULAS32X16_H3_L2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_HS00 (RR) units=0x60
        Haydn::FMULS16_HS00, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_HS01 (RR) units=0x60
        Haydn::FMULS16_HS01, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_HS02 (RR) units=0x60
        Haydn::FMULS16_HS02, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_HS03 (RR) units=0x60
        Haydn::FMULS16_HS03, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_HS11 (RR) units=0x60
        Haydn::FMULS16_HS11, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_HS12 (RR) units=0x60
        Haydn::FMULS16_HS12, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_HS13 (RR) units=0x60
        Haydn::FMULS16_HS13, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_HS22 (RR) units=0x60
        Haydn::FMULS16_HS22, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_HS23 (RR) units=0x60
        Haydn::FMULS16_HS23, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_HS33 (RR) units=0x60
        Haydn::FMULS16_HS33, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_LS00 (RR) units=0x60
        Haydn::FMULS16_LS00, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_LS01 (RR) units=0x60
        Haydn::FMULS16_LS01, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_LS02 (RR) units=0x60
        Haydn::FMULS16_LS02, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_LS03 (RR) units=0x60
        Haydn::FMULS16_LS03, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_LS11 (RR) units=0x60
        Haydn::FMULS16_LS11, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_LS12 (RR) units=0x60
        Haydn::FMULS16_LS12, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_LS13 (RR) units=0x60
        Haydn::FMULS16_LS13, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_LS22 (RR) units=0x60
        Haydn::FMULS16_LS22, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_LS23 (RR) units=0x60
        Haydn::FMULS16_LS23, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS16_LS33 (RR) units=0x60
        Haydn::FMULS16_LS33, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS32S_HH (RR) units=0x60
        Haydn::FMULS32S_HH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS32S_LH (RR) units=0x60
        Haydn::FMULS32S_LH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS32S_LL (RR) units=0x60
        Haydn::FMULS32S_LL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS32X16_H0 (RR) units=0x60
        Haydn::FMULS32X16_H0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS32X16_H1 (RR) units=0x60
        Haydn::FMULS32X16_H1, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS32X16_H2 (RR) units=0x60
        Haydn::FMULS32X16_H2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS32X16_H3 (RR) units=0x60
        Haydn::FMULS32X16_H3, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS32X16_L0 (RR) units=0x60
        Haydn::FMULS32X16_L0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS32X16_L1 (RR) units=0x60
        Haydn::FMULS32X16_L1, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS32X16_L2 (RR) units=0x60
        Haydn::FMULS32X16_L2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULS32X16_L3 (RR) units=0x60
        Haydn::FMULS32X16_L3, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULSA32S_HHLL (RR) units=0x60
        Haydn::FMULSA32S_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULSA32S_HLLH (RR) units=0x60
        Haydn::FMULSA32S_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULSA32X16_H1_L0 (RR) units=0x60
        Haydn::FMULSA32X16_H1_L0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULSA32X16_H3_L2 (RR) units=0x60
        Haydn::FMULSA32X16_H3_L2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULSS16_HS_11_00 (RR) units=0x60
        Haydn::FMULSS16_HS_11_00, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULSS16_HS_13_02 (RR) units=0x60
        Haydn::FMULSS16_HS_13_02, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULSS16_HS_33_22 (RR) units=0x60
        Haydn::FMULSS16_HS_33_22, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULSS16_LS_11_00 (RR) units=0x60
        Haydn::FMULSS16_LS_11_00, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULSS16_LS_13_02 (RR) units=0x60
        Haydn::FMULSS16_LS_13_02, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULSS16_LS_33_22 (RR) units=0x60
        Haydn::FMULSS16_LS_33_22, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULSS32S_HHLL (RR) units=0x60
        Haydn::FMULSS32S_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULSS32S_HLLH (RR) units=0x60
        Haydn::FMULSS32S_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULSS32X16_H1_L0 (RR) units=0x60
        Haydn::FMULSS32X16_H1_L0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULSS32X16_H3_L2 (RR) units=0x60
        Haydn::FMULSS32X16_H3_L2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAA16_HS_11_00 (RR) units=0x60
        Haydn::FMULZAA16_HS_11_00, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAA16_HS_13_02 (RR) units=0x60
        Haydn::FMULZAA16_HS_13_02, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAA16_HS_33_22 (RR) units=0x60
        Haydn::FMULZAA16_HS_33_22, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAA16_LS_11_00 (RR) units=0x60
        Haydn::FMULZAA16_LS_11_00, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAA16_LS_13_02 (RR) units=0x60
        Haydn::FMULZAA16_LS_13_02, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAA16_LS_33_22 (RR) units=0x60
        Haydn::FMULZAA16_LS_33_22, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAA32S_HHLL (RR) units=0x60
        Haydn::FMULZAA32S_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAA32S_HLLH (RR) units=0x60
        Haydn::FMULZAA32S_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAA32X16_H0_L1 (RR) units=0x60
        Haydn::FMULZAA32X16_H0_L1, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAA32X16_H1_L0 (RR) units=0x60
        Haydn::FMULZAA32X16_H1_L0, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAA32X16_H2_L3 (RR) units=0x60
        Haydn::FMULZAA32X16_H2_L3, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAA32X16_H3_L2 (RR) units=0x60
        Haydn::FMULZAA32X16_H3_L2, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAS32S_HHLL (RR) units=0x60
        Haydn::FMULZAS32S_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAS32S_HLLH (RR) units=0x60
        Haydn::FMULZAS32S_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAS32X16_H1_L0 (RR) units=0x60
        Haydn::FMULZAS32X16_H1_L0, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZAS32X16_H3_L2 (RR) units=0x60
        Haydn::FMULZAS32X16_H3_L2, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZSA32S_HHLL (RR) units=0x60
        Haydn::FMULZSA32S_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZSA32S_HLLH (RR) units=0x60
        Haydn::FMULZSA32S_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZSA32X16_H1_L0 (RR) units=0x60
        Haydn::FMULZSA32X16_H1_L0, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZSA32X16_H3_L2 (RR) units=0x60
        Haydn::FMULZSA32X16_H3_L2, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZSS16_HS_11_00 (RR) units=0x60
        Haydn::FMULZSS16_HS_11_00, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZSS16_HS_13_02 (RR) units=0x60
        Haydn::FMULZSS16_HS_13_02, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZSS16_HS_33_22 (RR) units=0x60
        Haydn::FMULZSS16_HS_33_22, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZSS16_LS_11_00 (RR) units=0x60
        Haydn::FMULZSS16_LS_11_00, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZSS16_LS_13_02 (RR) units=0x60
        Haydn::FMULZSS16_LS_13_02, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZSS16_LS_33_22 (RR) units=0x60
        Haydn::FMULZSS16_LS_33_22, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZSS32S_HHLL (RR) units=0x60
        Haydn::FMULZSS32S_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZSS32S_HLLH (RR) units=0x60
        Haydn::FMULZSS32S_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZSS32X16_H1_L0 (RR) units=0x60
        Haydn::FMULZSS32X16_H1_L0, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // FMULZSS32X16_H3_L2 (RR) units=0x60
        Haydn::FMULZSS32X16_H3_L2, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // JAL (I20) units=0x04
        Haydn::JAL, 0, 1, 0, 0, 0, 0, 0, 0, 0x04u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // JALR (RI12) units=0x04
        Haydn::JALR, 1, 1, 0, 0, 0, 0, 0, 0, 0x04u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // JALR_W (RI12) units=0x04
        Haydn::JALR_W, 1, 1, 0, 0, 0, 0, 0, 0, 0x04u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // JAL_W (I20) units=0x04
        Haydn::JAL_W, 0, 1, 0, 0, 0, 0, 0, 0, 0x04u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // LD16 (RI6) units=0x03
        Haydn::LD16, 1, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // LD32 (RI6) units=0x03
        Haydn::LD32, 1, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // LD32_POST (RI6) units=0x03
        Haydn::LD32_POST, 1, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // LD32_REG_M0S0LS (RR) units=0x03
        Haydn::LD32_REG_M0S0LS, 2, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // LD64 (RI6) units=0x03
        Haydn::LD64, 1, 0, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 8u,
    },
    { // LD64_POST (RI6) units=0x03
        Haydn::LD64_POST, 1, 1, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 8u,
    },
    { // LD64_REG_M0S0LS (RR) units=0x03
        Haydn::LD64_REG_M0S0LS, 2, 0, 0, 1, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 8u,
    },
    { // LD8 (RI6) units=0x03
        Haydn::LD8, 1, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 1u,
    },
    { // LDU16 (RI6) units=0x03
        Haydn::LDU16, 1, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // LDU8 (RI6) units=0x03
        Haydn::LDU8, 1, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 1u,
    },
    { // LOG2 (R) units=0x18
        Haydn::LOG2, 1, 1, 0, 0, 0, 0, 0, 0, 0x18u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // LSR32 (RR) units=0x1C
        Haydn::LSR32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // LUI (I12) units=0x1C
        Haydn::LUI, 0, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MAX32 (RR) units=0x1C
        Haydn::MAX32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MAX64 (RR) units=0x1C
        Haydn::MAX64, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MAXU32 (RR) units=0x1C
        Haydn::MAXU32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MIN32 (RR) units=0x1C
        Haydn::MIN32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MIN64 (RR) units=0x1C
        Haydn::MIN64, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MINU32 (RR) units=0x1C
        Haydn::MINU32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MOVE32 (R) units=0x1C
        Haydn::MOVE32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MOVE32_DR_H (R) units=0x1C
        Haydn::MOVE32_DR_H, 0, 1, 1, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MOVE32_DR_L (R) units=0x1C
        Haydn::MOVE32_DR_L, 0, 1, 1, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MOVE64 (R) units=0x1C
        Haydn::MOVE64, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MOVEGPR2SFR (SFR) units=0x1C
        Haydn::MOVEGPR2SFR, 1, 0, 0, 0, 0, 0, 0, 1, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MOVEI_H (I32) units=0x1C
        Haydn::MOVEI_H, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MOVEI_L (I32) units=0x1C
        Haydn::MOVEI_L, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MOVESFR2GPR (SFR) units=0x1C
        Haydn::MOVESFR2GPR, 0, 1, 0, 0, 0, 0, 1, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MOVF32 (RR) units=0x1C
        Haydn::MOVF32, 3, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MOVF64 (R) units=0x1C
        Haydn::MOVF64, 0, 0, 2, 1, 0, 0, 1, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MOVT32 (RR) units=0x1C
        Haydn::MOVT32, 3, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MOVT64 (R) units=0x1C
        Haydn::MOVT64, 0, 0, 2, 1, 0, 0, 1, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL16AQ (RR) units=0x60
        Haydn::MUL16AQ, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MUL16ZAQ (RR) units=0x60
        Haydn::MUL16ZAQ, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MUL32X16_H0 (RR) units=0x60
        Haydn::MUL32X16_H0, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL32X16_H1 (RR) units=0x60
        Haydn::MUL32X16_H1, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL32X16_H2 (RR) units=0x60
        Haydn::MUL32X16_H2, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL32X16_H3 (RR) units=0x60
        Haydn::MUL32X16_H3, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL32X16_L0 (RR) units=0x60
        Haydn::MUL32X16_L0, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL32X16_L1 (RR) units=0x60
        Haydn::MUL32X16_L1, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL32X16_L2 (RR) units=0x60
        Haydn::MUL32X16_L2, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL32X16_L3 (RR) units=0x60
        Haydn::MUL32X16_L3, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_HH (RR) units=0x60
        Haydn::MUL64_HH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_HL (RR) units=0x60
        Haydn::MUL64_HL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_HUH (RR) units=0x60
        Haydn::MUL64_HUH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_HUL (RR) units=0x60
        Haydn::MUL64_HUL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_LH (RR) units=0x60
        Haydn::MUL64_LH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_LL (RR) units=0x60
        Haydn::MUL64_LL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_LUH (RR) units=0x60
        Haydn::MUL64_LUH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_LUL (RR) units=0x60
        Haydn::MUL64_LUL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_UHH (RR) units=0x60
        Haydn::MUL64_UHH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_UHL (RR) units=0x60
        Haydn::MUL64_UHL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_UHUH (RR) units=0x60
        Haydn::MUL64_UHUH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_UHUL (RR) units=0x60
        Haydn::MUL64_UHUL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_ULH (RR) units=0x60
        Haydn::MUL64_ULH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_ULL (RR) units=0x60
        Haydn::MUL64_ULL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_ULUH (RR) units=0x60
        Haydn::MUL64_ULUH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MUL64_ULUL (RR) units=0x60
        Haydn::MUL64_ULUL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MULA32X16_H0 (RR) units=0x60
        Haydn::MULA32X16_H0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA32X16_H1 (RR) units=0x60
        Haydn::MULA32X16_H1, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA32X16_H2 (RR) units=0x60
        Haydn::MULA32X16_H2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA32X16_H3 (RR) units=0x60
        Haydn::MULA32X16_H3, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA32X16_L0 (RR) units=0x60
        Haydn::MULA32X16_L0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA32X16_L1 (RR) units=0x60
        Haydn::MULA32X16_L1, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA32X16_L2 (RR) units=0x60
        Haydn::MULA32X16_L2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA32X16_L3 (RR) units=0x60
        Haydn::MULA32X16_L3, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_HH (RR) units=0x60
        Haydn::MULA64_HH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_HL (RR) units=0x60
        Haydn::MULA64_HL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_HUH (RR) units=0x60
        Haydn::MULA64_HUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_HUL (RR) units=0x60
        Haydn::MULA64_HUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_LH (RR) units=0x60
        Haydn::MULA64_LH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_LL (RR) units=0x60
        Haydn::MULA64_LL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_LUH (RR) units=0x60
        Haydn::MULA64_LUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_LUL (RR) units=0x60
        Haydn::MULA64_LUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_UHH (RR) units=0x60
        Haydn::MULA64_UHH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_UHL (RR) units=0x60
        Haydn::MULA64_UHL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_UHUH (RR) units=0x60
        Haydn::MULA64_UHUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_UHUL (RR) units=0x60
        Haydn::MULA64_UHUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_ULH (RR) units=0x60
        Haydn::MULA64_ULH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_ULL (RR) units=0x60
        Haydn::MULA64_ULL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_ULUH (RR) units=0x60
        Haydn::MULA64_ULUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULA64_ULUL (RR) units=0x60
        Haydn::MULA64_ULUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAA32X16_H0_L1 (RR) units=0x60
        Haydn::MULAA32X16_H0_L1, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAA32X16_H1_L0 (RR) units=0x60
        Haydn::MULAA32X16_H1_L0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAA32X16_H2_L3 (RR) units=0x60
        Haydn::MULAA32X16_H2_L3, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAA32X16_H3_L2 (RR) units=0x60
        Haydn::MULAA32X16_H3_L2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAA32_HHLL (RR) units=0x60
        Haydn::MULAA32_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAA32_HLLH (RR) units=0x60
        Haydn::MULAA32_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS32X16_H1_L0 (RR) units=0x60
        Haydn::MULAS32X16_H1_L0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS32X16_H3_L2 (RR) units=0x60
        Haydn::MULAS32X16_H3_L2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS32_HHLL (RR) units=0x60
        Haydn::MULAS32_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS32_HLLH (RR) units=0x60
        Haydn::MULAS32_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_HH (RR) units=0x60
        Haydn::MULAS64_HH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_HL (RR) units=0x60
        Haydn::MULAS64_HL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_HUH (RR) units=0x60
        Haydn::MULAS64_HUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_HUL (RR) units=0x60
        Haydn::MULAS64_HUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_LH (RR) units=0x60
        Haydn::MULAS64_LH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_LL (RR) units=0x60
        Haydn::MULAS64_LL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_LUH (RR) units=0x60
        Haydn::MULAS64_LUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_LUL (RR) units=0x60
        Haydn::MULAS64_LUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_UHH (RR) units=0x60
        Haydn::MULAS64_UHH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_UHL (RR) units=0x60
        Haydn::MULAS64_UHL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_UHUH (RR) units=0x60
        Haydn::MULAS64_UHUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_UHUL (RR) units=0x60
        Haydn::MULAS64_UHUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_ULH (RR) units=0x60
        Haydn::MULAS64_ULH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_ULL (RR) units=0x60
        Haydn::MULAS64_ULL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_ULUH (RR) units=0x60
        Haydn::MULAS64_ULUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULAS64_ULUL (RR) units=0x60
        Haydn::MULAS64_ULUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULL (RR) units=0x60
        Haydn::MULL, 2, 1, 0, 0, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MULS32X16_H0 (RR) units=0x60
        Haydn::MULS32X16_H0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS32X16_H1 (RR) units=0x60
        Haydn::MULS32X16_H1, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS32X16_H2 (RR) units=0x60
        Haydn::MULS32X16_H2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS32X16_H3 (RR) units=0x60
        Haydn::MULS32X16_H3, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS32X16_L0 (RR) units=0x60
        Haydn::MULS32X16_L0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS32X16_L1 (RR) units=0x60
        Haydn::MULS32X16_L1, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS32X16_L2 (RR) units=0x60
        Haydn::MULS32X16_L2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS32X16_L3 (RR) units=0x60
        Haydn::MULS32X16_L3, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_HH (RR) units=0x60
        Haydn::MULS64_HH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_HL (RR) units=0x60
        Haydn::MULS64_HL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_HUH (RR) units=0x60
        Haydn::MULS64_HUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_HUL (RR) units=0x60
        Haydn::MULS64_HUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_LH (RR) units=0x60
        Haydn::MULS64_LH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_LL (RR) units=0x60
        Haydn::MULS64_LL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_LUH (RR) units=0x60
        Haydn::MULS64_LUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_LUL (RR) units=0x60
        Haydn::MULS64_LUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_UHH (RR) units=0x60
        Haydn::MULS64_UHH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_UHL (RR) units=0x60
        Haydn::MULS64_UHL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_UHUH (RR) units=0x60
        Haydn::MULS64_UHUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_UHUL (RR) units=0x60
        Haydn::MULS64_UHUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_ULH (RR) units=0x60
        Haydn::MULS64_ULH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_ULL (RR) units=0x60
        Haydn::MULS64_ULL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_ULUH (RR) units=0x60
        Haydn::MULS64_ULUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULS64_ULUL (RR) units=0x60
        Haydn::MULS64_ULUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSA32X16_H1_L0 (RR) units=0x60
        Haydn::MULSA32X16_H1_L0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSA32X16_H3_L2 (RR) units=0x60
        Haydn::MULSA32X16_H3_L2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSA32_HHLL (RR) units=0x60
        Haydn::MULSA32_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSA32_HLLH (RR) units=0x60
        Haydn::MULSA32_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS32X16_H1_L0 (RR) units=0x60
        Haydn::MULSS32X16_H1_L0, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS32X16_H3_L2 (RR) units=0x60
        Haydn::MULSS32X16_H3_L2, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS32_HHLL (RR) units=0x60
        Haydn::MULSS32_HHLL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS32_HLLH (RR) units=0x60
        Haydn::MULSS32_HLLH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_HH (RR) units=0x60
        Haydn::MULSS64_HH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_HL (RR) units=0x60
        Haydn::MULSS64_HL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_HUH (RR) units=0x60
        Haydn::MULSS64_HUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_HUL (RR) units=0x60
        Haydn::MULSS64_HUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_LH (RR) units=0x60
        Haydn::MULSS64_LH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_LL (RR) units=0x60
        Haydn::MULSS64_LL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_LUH (RR) units=0x60
        Haydn::MULSS64_LUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_LUL (RR) units=0x60
        Haydn::MULSS64_LUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_UHH (RR) units=0x60
        Haydn::MULSS64_UHH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_UHL (RR) units=0x60
        Haydn::MULSS64_UHL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_UHUH (RR) units=0x60
        Haydn::MULSS64_UHUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_UHUL (RR) units=0x60
        Haydn::MULSS64_UHUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_ULH (RR) units=0x60
        Haydn::MULSS64_ULH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_ULL (RR) units=0x60
        Haydn::MULSS64_ULL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_ULUH (RR) units=0x60
        Haydn::MULSS64_ULUH, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSS64_ULUL (RR) units=0x60
        Haydn::MULSS64_ULUL, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULSSH (RR) units=0x60
        Haydn::MULSSH, 2, 1, 0, 0, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MULSUH (RR) units=0x60
        Haydn::MULSUH, 2, 1, 0, 0, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MULUUH (RR) units=0x60
        Haydn::MULUUH, 2, 1, 0, 0, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // MULZAA32X16_H0_L1 (RR) units=0x60
        Haydn::MULZAA32X16_H0_L1, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZAA32X16_H1_L0 (RR) units=0x60
        Haydn::MULZAA32X16_H1_L0, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZAA32X16_H2_L3 (RR) units=0x60
        Haydn::MULZAA32X16_H2_L3, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZAA32X16_H3_L2 (RR) units=0x60
        Haydn::MULZAA32X16_H3_L2, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZAA32_HHLL (RR) units=0x60
        Haydn::MULZAA32_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZAA32_HLLH (RR) units=0x60
        Haydn::MULZAA32_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZAS32X16_H1_L0 (RR) units=0x60
        Haydn::MULZAS32X16_H1_L0, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZAS32X16_H3_L2 (RR) units=0x60
        Haydn::MULZAS32X16_H3_L2, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZAS32_HHLL (RR) units=0x60
        Haydn::MULZAS32_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZAS32_HLLH (RR) units=0x60
        Haydn::MULZAS32_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZSA32X16_H1_L0 (RR) units=0x60
        Haydn::MULZSA32X16_H1_L0, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZSA32X16_H3_L2 (RR) units=0x60
        Haydn::MULZSA32X16_H3_L2, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZSA32_HHLL (RR) units=0x60
        Haydn::MULZSA32_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZSA32_HLLH (RR) units=0x60
        Haydn::MULZSA32_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZSS32X16_H1_L0 (RR) units=0x60
        Haydn::MULZSS32X16_H1_L0, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZSS32X16_H3_L2 (RR) units=0x60
        Haydn::MULZSS32X16_H3_L2, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZSS32_HHLL (RR) units=0x60
        Haydn::MULZSS32_HHLL, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // MULZSS32_HLLH (RR) units=0x60
        Haydn::MULZSS32_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // NEG32 (R) units=0x1C
        Haydn::NEG32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // NEG32S (R) units=0x1C
        Haydn::NEG32S, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // NEG64 (R) units=0x1C
        Haydn::NEG64, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // NEG64S (R) units=0x1C
        Haydn::NEG64S, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // NOT32 (R) units=0x1C
        Haydn::NOT32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // NOT64 (R) units=0x1C
        Haydn::NOT64, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // NSA16_L (R) units=0x1C
        Haydn::NSA16_L, 0, 1, 1, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // NSA32 (R) units=0x1C
        Haydn::NSA32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // NSA32_L (R) units=0x1C
        Haydn::NSA32_L, 0, 1, 1, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // NSA64 (R) units=0x1C
        Haydn::NSA64, 0, 1, 1, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // NSAU32 (R) units=0x1C
        Haydn::NSAU32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // NSAZ16_L (R) units=0x1C
        Haydn::NSAZ16_L, 0, 1, 1, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // NSAZ32_L (R) units=0x1C
        Haydn::NSAZ32_L, 0, 1, 1, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // NSAZ64 (R) units=0x1C
        Haydn::NSAZ64, 0, 1, 1, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // OR32 (RR) units=0x1C
        Haydn::OR32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // OR64 (RR) units=0x1C
        Haydn::OR64, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ORI32 (RI20) units=0x1C
        Haydn::ORI32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ORI32_W (RI20) units=0x1C
        Haydn::ORI32_W, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // PLDWWUA (AR) units=0x03
        Haydn::PLDWWUA, 1, 1, 0, 0, 0, 1, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // PLDWWUA_POST (AR) units=0x03
        Haydn::PLDWWUA_POST, 1, 1, 0, 0, 0, 1, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // PLQHWUA_CB_POST (AR_CBR) units=0x01
        Haydn::PLQHWUA_CB_POST, 1, 1, 0, 0, 0, 1, 0, 0, 0x01u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // PLTWWUA_CB_POST (AR_CBR) units=0x01
        Haydn::PLTWWUA_CB_POST, 1, 1, 0, 0, 0, 1, 0, 0, 0x01u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // POPCOUNT32 (R) units=0x1C
        Haydn::POPCOUNT32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // POPCOUNT64 (R) units=0x1C
        Haydn::POPCOUNT64, 0, 1, 1, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // RECIP (R) units=0x18
        Haydn::RECIP, 1, 1, 0, 0, 0, 0, 0, 0, 0x18u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SEQ32 (RR) units=0x1C
        Haydn::SEQ32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SEQ64 (R) units=0x1C
        Haydn::SEQ64, 0, 0, 2, 0, 0, 0, 0, 1, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SET_HWLOOP (HWLRIII) units=0x04
        Haydn::SET_HWLOOP, 0, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // SET_HWLOOP_F2 (HWLRIIR) units=0x04
        Haydn::SET_HWLOOP_F2, 1, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // SET_HWLOOP_F2_W (HWLRIIR) units=0x04
        Haydn::SET_HWLOOP_F2_W, 1, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // SET_HWLOOP_REG (HWLRRRR) units=0x04
        Haydn::SET_HWLOOP_REG, 3, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // SET_HWLOOP_REG_W (HWLRRRR) units=0x04
        Haydn::SET_HWLOOP_REG_W, 3, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // SET_HWLOOP_W (HWLRIII) units=0x04
        Haydn::SET_HWLOOP_W, 0, 0, 0, 0, 0, 0, 0, 0, 0x04u,
        0u, 0u, {},
        1u, 0u,
    },
    { // SEXT32T64 (R) units=0x1C
        Haydn::SEXT32T64, 1, 0, 0, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SEXT_GPR32_TO_DR64 (R) units=0x1C
        Haydn::SEXT_GPR32_TO_DR64, 1, 0, 0, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SHL32 (RR) units=0x1C
        Haydn::SHL32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SIN_COS (RI4) units=0x18
        Haydn::SIN_COS, 1, 0, 0, 1, 0, 0, 0, 0, 0x18u,
        0u, 1u, {17},
        1u, 0u,
    },
    { // SLE32 (RR) units=0x1C
        Haydn::SLE32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SLE64 (R) units=0x1C
        Haydn::SLE64, 0, 0, 2, 0, 0, 0, 0, 1, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SLL32 (RR) units=0x1C
        Haydn::SLL32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SLL64 (RR) units=0x1C
        Haydn::SLL64, 1, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SLLI32 (RI5) units=0x1C
        Haydn::SLLI32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SLLI64 (RI6) units=0x1C
        Haydn::SLLI64, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SLT32 (RR) units=0x1C
        Haydn::SLT32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SLT64 (R) units=0x1C
        Haydn::SLT64, 0, 0, 2, 0, 0, 0, 0, 1, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SLTU32 (RR) units=0x1C
        Haydn::SLTU32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SMULA16S_00 (RR) units=0x60
        Haydn::SMULA16S_00, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16S_10 (RR) units=0x60
        Haydn::SMULA16S_10, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16S_11 (RR) units=0x60
        Haydn::SMULA16S_11, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16S_20 (RR) units=0x60
        Haydn::SMULA16S_20, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16S_21 (RR) units=0x60
        Haydn::SMULA16S_21, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16S_22 (RR) units=0x60
        Haydn::SMULA16S_22, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16S_30 (RR) units=0x60
        Haydn::SMULA16S_30, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16S_31 (RR) units=0x60
        Haydn::SMULA16S_31, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16S_32 (RR) units=0x60
        Haydn::SMULA16S_32, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16S_33 (RR) units=0x60
        Haydn::SMULA16S_33, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16_00 (RR) units=0x60
        Haydn::SMULA16_00, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16_10 (RR) units=0x60
        Haydn::SMULA16_10, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16_11 (RR) units=0x60
        Haydn::SMULA16_11, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16_20 (RR) units=0x60
        Haydn::SMULA16_20, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16_21 (RR) units=0x60
        Haydn::SMULA16_21, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16_22 (RR) units=0x60
        Haydn::SMULA16_22, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16_30 (RR) units=0x60
        Haydn::SMULA16_30, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16_31 (RR) units=0x60
        Haydn::SMULA16_31, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16_32 (RR) units=0x60
        Haydn::SMULA16_32, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULA16_33 (RR) units=0x60
        Haydn::SMULA16_33, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16S_00 (RR) units=0x60
        Haydn::SMULS16S_00, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16S_10 (RR) units=0x60
        Haydn::SMULS16S_10, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16S_11 (RR) units=0x60
        Haydn::SMULS16S_11, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16S_20 (RR) units=0x60
        Haydn::SMULS16S_20, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16S_21 (RR) units=0x60
        Haydn::SMULS16S_21, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16S_22 (RR) units=0x60
        Haydn::SMULS16S_22, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16S_30 (RR) units=0x60
        Haydn::SMULS16S_30, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16S_31 (RR) units=0x60
        Haydn::SMULS16S_31, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16S_32 (RR) units=0x60
        Haydn::SMULS16S_32, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16S_33 (RR) units=0x60
        Haydn::SMULS16S_33, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16_00 (RR) units=0x60
        Haydn::SMULS16_00, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16_10 (RR) units=0x60
        Haydn::SMULS16_10, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16_11 (RR) units=0x60
        Haydn::SMULS16_11, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16_20 (RR) units=0x60
        Haydn::SMULS16_20, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16_21 (RR) units=0x60
        Haydn::SMULS16_21, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16_22 (RR) units=0x60
        Haydn::SMULS16_22, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16_30 (RR) units=0x60
        Haydn::SMULS16_30, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16_31 (RR) units=0x60
        Haydn::SMULS16_31, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16_32 (RR) units=0x60
        Haydn::SMULS16_32, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SMULS16_33 (RR) units=0x60
        Haydn::SMULS16_33, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SQRT (R) units=0x18
        Haydn::SQRT, 1, 1, 0, 0, 0, 0, 0, 0, 0x18u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // SRA32 (RR) units=0x1C
        Haydn::SRA32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SRA32R (RR) units=0x1C
        Haydn::SRA32R, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SRA64 (RR) units=0x1C
        Haydn::SRA64, 1, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SRA64R (RR) units=0x1C
        Haydn::SRA64R, 1, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SRAI32 (RI5) units=0x1C
        Haydn::SRAI32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SRAI32R (RI5) units=0x1C
        Haydn::SRAI32R, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SRAI64 (RI6) units=0x1C
        Haydn::SRAI64, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SRAI64R (RI6) units=0x1C
        Haydn::SRAI64R, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SRL32 (RR) units=0x1C
        Haydn::SRL32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SRL64 (RR) units=0x1C
        Haydn::SRL64, 1, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SRLI32 (RI5) units=0x1C
        Haydn::SRLI32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SRLI64 (RI6) units=0x1C
        Haydn::SRLI64, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ST16 (RI6) units=0x01
        Haydn::ST16, 2, 0, 0, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 2u,
    },
    { // ST32 (RI6) units=0x01
        Haydn::ST32, 2, 0, 0, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 4u,
    },
    { // ST32_POST (RI6) units=0x01
        Haydn::ST32_POST, 2, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // ST32_REG_M0S0LS (RR) units=0x01
        Haydn::ST32_REG_M0S0LS, 3, 0, 0, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 4u,
    },
    { // ST64 (RI6) units=0x01
        Haydn::ST64, 1, 0, 1, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 8u,
    },
    { // ST64_POST (RI6) units=0x01
        Haydn::ST64_POST, 1, 1, 1, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 8u,
    },
    { // ST64_REG_M0S0LS (RR) units=0x01
        Haydn::ST64_REG_M0S0LS, 2, 0, 1, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 8u,
    },
    { // ST8 (RI6) units=0x01
        Haydn::ST8, 2, 0, 0, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 1u,
    },
    { // SUB32 (RR) units=0x1C
        Haydn::SUB32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SUB32S (RR) units=0x1C
        Haydn::SUB32S, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SUB64 (RR) units=0x1C
        Haydn::SUB64, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SUB64S (RR) units=0x1C
        Haydn::SUB64S, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SUB64S_H (RR) units=0x1C
        Haydn::SUB64S_H, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SUB64S_L (RR) units=0x1C
        Haydn::SUB64S_L, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SUB64_H (RR) units=0x1C
        Haydn::SUB64_H, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SUB64_L (RR) units=0x1C
        Haydn::SUB64_L, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SUBI32 (RI20) units=0x1C
        Haydn::SUBI32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // SUBI32S (RI20) units=0x1C
        Haydn::SUBI32S, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // S_LBS_POST_IMM (RI6) units=0x03
        Haydn::S_LBS_POST_IMM, 1, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 1u,
    },
    { // S_LBS_POST_REG (RR) units=0x03
        Haydn::S_LBS_POST_REG, 2, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 1u,
    },
    { // S_LBS_PRE_IMM (RI6) units=0x03
        Haydn::S_LBS_PRE_IMM, 1, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 1u,
    },
    { // S_LBS_PRE_REG (RR) units=0x03
        Haydn::S_LBS_PRE_REG, 2, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 1u,
    },
    { // S_LBS_WITH_IMM (RI6) units=0x03
        Haydn::S_LBS_WITH_IMM, 1, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 1u,
    },
    { // S_LBS_WITH_REG (RR) units=0x03
        Haydn::S_LBS_WITH_REG, 2, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 1u,
    },
    { // S_LBU_POST_IMM (RI6) units=0x03
        Haydn::S_LBU_POST_IMM, 1, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 1u,
    },
    { // S_LBU_POST_REG (RR) units=0x03
        Haydn::S_LBU_POST_REG, 2, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 1u,
    },
    { // S_LBU_PRE_IMM (RI6) units=0x03
        Haydn::S_LBU_PRE_IMM, 1, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 1u,
    },
    { // S_LBU_PRE_REG (RR) units=0x03
        Haydn::S_LBU_PRE_REG, 2, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 1u,
    },
    { // S_LBU_WITH_IMM (RI6) units=0x03
        Haydn::S_LBU_WITH_IMM, 1, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 1u,
    },
    { // S_LBU_WITH_REG (RR) units=0x03
        Haydn::S_LBU_WITH_REG, 2, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 1u,
    },
    { // S_LHWS_POST_IMM (RI6) units=0x03
        Haydn::S_LHWS_POST_IMM, 1, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // S_LHWS_POST_REG (RR) units=0x03
        Haydn::S_LHWS_POST_REG, 2, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // S_LHWS_PRE_IMM (RI6) units=0x03
        Haydn::S_LHWS_PRE_IMM, 1, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // S_LHWS_PRE_REG (RR) units=0x03
        Haydn::S_LHWS_PRE_REG, 2, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // S_LHWS_WITH_IMM (RI6) units=0x03
        Haydn::S_LHWS_WITH_IMM, 1, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // S_LHWS_WITH_REG (RR) units=0x03
        Haydn::S_LHWS_WITH_REG, 2, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // S_LHWU_POST_IMM (RI6) units=0x03
        Haydn::S_LHWU_POST_IMM, 1, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // S_LHWU_POST_REG (RR) units=0x03
        Haydn::S_LHWU_POST_REG, 2, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // S_LHWU_PRE_IMM (RI6) units=0x03
        Haydn::S_LHWU_PRE_IMM, 1, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // S_LHWU_PRE_REG (RR) units=0x03
        Haydn::S_LHWU_PRE_REG, 2, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // S_LHWU_WITH_IMM (RI6) units=0x03
        Haydn::S_LHWU_WITH_IMM, 1, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // S_LHWU_WITH_REG (RR) units=0x03
        Haydn::S_LHWU_WITH_REG, 2, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 2u,
    },
    { // S_LW_BREV_IMM (RI6) units=0x03
        Haydn::S_LW_BREV_IMM, 1, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // S_LW_BREV_REG (RR) units=0x03
        Haydn::S_LW_BREV_REG, 2, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // S_LW_POST_IMM (RI6) units=0x03
        Haydn::S_LW_POST_IMM, 1, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // S_LW_POST_REG (RR) units=0x03
        Haydn::S_LW_POST_REG, 2, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // S_LW_PRE_IMM (RI6) units=0x03
        Haydn::S_LW_PRE_IMM, 1, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // S_LW_PRE_REG (RR) units=0x03
        Haydn::S_LW_PRE_REG, 2, 2, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // S_LW_WITH_IMM (RI6) units=0x03
        Haydn::S_LW_WITH_IMM, 1, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // S_LW_WITH_REG (RR) units=0x03
        Haydn::S_LW_WITH_REG, 2, 1, 0, 0, 0, 0, 0, 0, 0x03u,
        2u, 1u, {2},
        1u, 4u,
    },
    { // S_SB_POST_IMM (RI6) units=0x01
        Haydn::S_SB_POST_IMM, 2, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 1u,
    },
    { // S_SB_POST_REG (RR) units=0x01
        Haydn::S_SB_POST_REG, 3, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 1u,
    },
    { // S_SB_PRE_IMM (RI6) units=0x01
        Haydn::S_SB_PRE_IMM, 2, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 1u,
    },
    { // S_SB_PRE_REG (RR) units=0x01
        Haydn::S_SB_PRE_REG, 3, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 1u,
    },
    { // S_SB_WITH_IMM (RI6) units=0x01
        Haydn::S_SB_WITH_IMM, 2, 0, 0, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 1u,
    },
    { // S_SB_WITH_REG (RR) units=0x01
        Haydn::S_SB_WITH_REG, 3, 0, 0, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 1u,
    },
    { // S_SHW_POST_IMM (RI6) units=0x01
        Haydn::S_SHW_POST_IMM, 2, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 2u,
    },
    { // S_SHW_POST_REG (RR) units=0x01
        Haydn::S_SHW_POST_REG, 3, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 2u,
    },
    { // S_SHW_PRE_IMM (RI6) units=0x01
        Haydn::S_SHW_PRE_IMM, 2, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 2u,
    },
    { // S_SHW_PRE_REG (RR) units=0x01
        Haydn::S_SHW_PRE_REG, 3, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 2u,
    },
    { // S_SHW_WITH_IMM (RI6) units=0x01
        Haydn::S_SHW_WITH_IMM, 2, 0, 0, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 2u,
    },
    { // S_SHW_WITH_REG (RR) units=0x01
        Haydn::S_SHW_WITH_REG, 3, 0, 0, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 2u,
    },
    { // S_SW_BREV_IMM (RI6) units=0x01
        Haydn::S_SW_BREV_IMM, 2, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // S_SW_BREV_REG (RR) units=0x01
        Haydn::S_SW_BREV_REG, 3, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // S_SW_POST_IMM (RI6) units=0x01
        Haydn::S_SW_POST_IMM, 2, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // S_SW_POST_REG (RR) units=0x01
        Haydn::S_SW_POST_REG, 3, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // S_SW_PRE_IMM (RI6) units=0x01
        Haydn::S_SW_PRE_IMM, 2, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // S_SW_PRE_REG (RR) units=0x01
        Haydn::S_SW_PRE_REG, 3, 1, 0, 0, 0, 0, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 4u,
    },
    { // S_SW_WITH_IMM (RI6) units=0x01
        Haydn::S_SW_WITH_IMM, 2, 0, 0, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 4u,
    },
    { // S_SW_WITH_REG (RR) units=0x01
        Haydn::S_SW_WITH_REG, 3, 0, 0, 0, 0, 0, 0, 0, 0x01u,
        0u, 0u, {},
        1u, 4u,
    },
    { // TRANSF64 (R) units=0x1C
        Haydn::TRANSF64, 1, 0, 0, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // TRANSF64F2 (R) units=0x1C
        Haydn::TRANSF64F2, 1, 0, 0, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // TRANSF64_H (R) units=0x1C
        Haydn::TRANSF64_H, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // TRANSF64_L (R) units=0x1C
        Haydn::TRANSF64_L, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // WBARWUA (AR) units=0x01
        Haydn::WBARWUA, 1, 0, 0, 0, 1, 1, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 2u,
    },
    { // WBARWUA_CB (AR_CBR) units=0x01
        Haydn::WBARWUA_CB, 1, 0, 0, 0, 1, 1, 0, 0, 0x01u,
        1u, 1u, {1},
        1u, 2u,
    },
    { // X2ABS32 (R) units=0x1C
        Haydn::X2ABS32, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2ABS32S (R) units=0x1C
        Haydn::X2ABS32S, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2ADD32 (RR) units=0x1C
        Haydn::X2ADD32, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2ADD32S (RR) units=0x1C
        Haydn::X2ADD32S, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2ADD32S_HLLH (RR) units=0x1C
        Haydn::X2ADD32S_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2ADD32_HLLH (RR) units=0x1C
        Haydn::X2ADD32_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2ADDSUB32 (RR) units=0x1C
        Haydn::X2ADDSUB32, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2ADDSUB32S (RR) units=0x1C
        Haydn::X2ADDSUB32S, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2ADDSUB32S_HLLH (RR) units=0x1C
        Haydn::X2ADDSUB32S_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2ADDSUB32_HLLH (RR) units=0x1C
        Haydn::X2ADDSUB32_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2CLAMP32 (RR) units=0x1C
        Haydn::X2CLAMP32, 0, 0, 3, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2CMUL32 (RRR) units=0x60
        Haydn::X2CMUL32, 0, 0, 2, 2, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2CMUL32S (RRR) units=0x60
        Haydn::X2CMUL32S, 0, 0, 2, 2, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2CMUL32S_F2 (RRR) units=0x60
        Haydn::X2CMUL32S_F2, 0, 0, 2, 2, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2CMUL32X16_H (RR) units=0x60
        Haydn::X2CMUL32X16_H, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2CMUL32X16_L (RR) units=0x60
        Haydn::X2CMUL32X16_L, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2CMUL32_F2 (RRR) units=0x60
        Haydn::X2CMUL32_F2, 0, 0, 2, 2, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2CMULA32X16_H (RR) units=0x60
        Haydn::X2CMULA32X16_H, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2CMULA32X16_L (RR) units=0x60
        Haydn::X2CMULA32X16_L, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2DOT32 (RR) units=0x60
        Haydn::X2DOT32, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FCMUL32RS (RR) units=0x60
        Haydn::X2FCMUL32RS, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FCMUL32RSS (RR) units=0x60
        Haydn::X2FCMUL32RSS, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FCMUL32X16RS_H (RR) units=0x60
        Haydn::X2FCMUL32X16RS_H, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FCMUL32X16RS_L (RR) units=0x60
        Haydn::X2FCMUL32X16RS_L, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FCMULA32RS (RR) units=0x60
        Haydn::X2FCMULA32RS, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FCMULA32RSS (RR) units=0x60
        Haydn::X2FCMULA32RSS, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FCMULA32X16RS_H (RR) units=0x60
        Haydn::X2FCMULA32X16RS_H, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FCMULA32X16RS_L (RR) units=0x60
        Haydn::X2FCMULA32X16RS_L, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FF2RSST32 (RR) units=0x1C
        Haydn::X2FF2RSST32, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2FF2RST32 (RR) units=0x1C
        Haydn::X2FF2RST32, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2FMUL32RS (RR) units=0x60
        Haydn::X2FMUL32RS, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMUL32RSS (RR) units=0x60
        Haydn::X2FMUL32RSS, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMUL32TS (RR) units=0x60
        Haydn::X2FMUL32TS, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMUL32X16RSS_H (RR) units=0x60
        Haydn::X2FMUL32X16RSS_H, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2FMUL32X16RSS_L (RR) units=0x60
        Haydn::X2FMUL32X16RSS_L, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2FMUL32X16RS_H (RR) units=0x60
        Haydn::X2FMUL32X16RS_H, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2FMUL32X16RS_L (RR) units=0x60
        Haydn::X2FMUL32X16RS_L, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2FMUL32X16TS_H (RR) units=0x60
        Haydn::X2FMUL32X16TS_H, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2FMUL32X16TS_L (RR) units=0x60
        Haydn::X2FMUL32X16TS_L, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2FMULA32RS (RR) units=0x60
        Haydn::X2FMULA32RS, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULA32RSS (RR) units=0x60
        Haydn::X2FMULA32RSS, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULA32TS (RR) units=0x60
        Haydn::X2FMULA32TS, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULA32X16RSS_H (RR) units=0x60
        Haydn::X2FMULA32X16RSS_H, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULA32X16RSS_L (RR) units=0x60
        Haydn::X2FMULA32X16RSS_L, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULA32X16RS_H (RR) units=0x60
        Haydn::X2FMULA32X16RS_H, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULA32X16RS_L (RR) units=0x60
        Haydn::X2FMULA32X16RS_L, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULA32X16TS_H (RR) units=0x60
        Haydn::X2FMULA32X16TS_H, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULA32X16TS_L (RR) units=0x60
        Haydn::X2FMULA32X16TS_L, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULS32RS (RR) units=0x60
        Haydn::X2FMULS32RS, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULS32RSS (RR) units=0x60
        Haydn::X2FMULS32RSS, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULS32TS (RR) units=0x60
        Haydn::X2FMULS32TS, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULS32X16RSS_H (RR) units=0x60
        Haydn::X2FMULS32X16RSS_H, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULS32X16RSS_L (RR) units=0x60
        Haydn::X2FMULS32X16RSS_L, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULS32X16RS_H (RR) units=0x60
        Haydn::X2FMULS32X16RS_H, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULS32X16RS_L (RR) units=0x60
        Haydn::X2FMULS32X16RS_L, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULS32X16TS_H (RR) units=0x60
        Haydn::X2FMULS32X16TS_H, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FMULS32X16TS_L (RR) units=0x60
        Haydn::X2FMULS32X16TS_L, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2FRSST32 (RR) units=0x1C
        Haydn::X2FRSST32, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2FRST32 (RR) units=0x1C
        Haydn::X2FRST32, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2HADD32S_H (R) units=0x1C
        Haydn::X2HADD32S_H, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2HADD32S_L (R) units=0x1C
        Haydn::X2HADD32S_L, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2HADD32_H (R) units=0x1C
        Haydn::X2HADD32_H, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2HADD32_L (R) units=0x1C
        Haydn::X2HADD32_L, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2HMAX32 (R) units=0x1C
        Haydn::X2HMAX32, 0, 1, 1, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2HMIN32 (R) units=0x1C
        Haydn::X2HMIN32, 0, 1, 1, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2MAX32 (RR) units=0x1C
        Haydn::X2MAX32, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2MIN32 (RR) units=0x1C
        Haydn::X2MIN32, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2MJSWAP32 (R) units=0x1C
        Haydn::X2MJSWAP32, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2MJSWAP32S (R) units=0x1C
        Haydn::X2MJSWAP32S, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2MOVF32 (R) units=0x1C
        Haydn::X2MOVF32, 0, 0, 2, 1, 0, 0, 1, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2MOVT32 (R) units=0x1C
        Haydn::X2MOVT32, 0, 0, 2, 1, 0, 0, 1, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2MUL32 (RRR) units=0x60
        Haydn::X2MUL32, 0, 0, 2, 2, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2MUL32X16_H (RR) units=0x60
        Haydn::X2MUL32X16_H, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2MUL32X16_L (RR) units=0x60
        Haydn::X2MUL32X16_L, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2MULA32 (RRR) units=0x60
        Haydn::X2MULA32, 0, 0, 4, 2, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2MULA32X16_H (RR) units=0x60
        Haydn::X2MULA32X16_H, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2MULA32X16_L (RR) units=0x60
        Haydn::X2MULA32X16_L, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2MULAPH32 (RR) units=0x60
        Haydn::X2MULAPH32, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2MULAPL32 (RR) units=0x60
        Haydn::X2MULAPL32, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2MULPH32 (RR) units=0x60
        Haydn::X2MULPH32, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2MULPL32 (RR) units=0x60
        Haydn::X2MULPL32, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2MULS32 (RRR) units=0x60
        Haydn::X2MULS32, 0, 0, 4, 2, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2MULS32X16_H (RR) units=0x60
        Haydn::X2MULS32X16_H, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2MULS32X16_L (RR) units=0x60
        Haydn::X2MULS32X16_L, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2MULSPH32 (RR) units=0x60
        Haydn::X2MULSPH32, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2MULSPL32 (RR) units=0x60
        Haydn::X2MULSPL32, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X2NEG32 (R) units=0x1C
        Haydn::X2NEG32, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2NEG32S (R) units=0x1C
        Haydn::X2NEG32S, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2NEG32S_L (R) units=0x1C
        Haydn::X2NEG32S_L, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2NEG32_L (R) units=0x1C
        Haydn::X2NEG32_L, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SEL32_HH (RR) units=0x1C
        Haydn::X2SEL32_HH, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SEL32_HL (RR) units=0x1C
        Haydn::X2SEL32_HL, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SEL32_LH (RR) units=0x1C
        Haydn::X2SEL32_LH, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SEL32_LL (RR) units=0x1C
        Haydn::X2SEL32_LL, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SEQ32 (R) units=0x1C
        Haydn::X2SEQ32, 0, 0, 2, 0, 0, 0, 0, 1, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SLE32 (R) units=0x1C
        Haydn::X2SLE32, 0, 0, 2, 0, 0, 0, 0, 1, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SLL32 (RR) units=0x1C
        Haydn::X2SLL32, 1, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SLLI32 (RI5) units=0x1C
        Haydn::X2SLLI32, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SLT32 (R) units=0x1C
        Haydn::X2SLT32, 0, 0, 2, 0, 0, 0, 0, 1, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SRA32 (RR) units=0x1C
        Haydn::X2SRA32, 1, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SRA32R (RR) units=0x1C
        Haydn::X2SRA32R, 1, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SRAI32 (RI5) units=0x1C
        Haydn::X2SRAI32, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SRAI32R (RI5) units=0x1C
        Haydn::X2SRAI32R, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SRL32 (RR) units=0x1C
        Haydn::X2SRL32, 1, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SRLI32 (RI5) units=0x1C
        Haydn::X2SRLI32, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SUB32 (RR) units=0x1C
        Haydn::X2SUB32, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SUB32S (RR) units=0x1C
        Haydn::X2SUB32S, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SUB32S_HLLH (RR) units=0x1C
        Haydn::X2SUB32S_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SUB32_HLLH (RR) units=0x1C
        Haydn::X2SUB32_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SUBADD32 (RR) units=0x1C
        Haydn::X2SUBADD32, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SUBADD32S (RR) units=0x1C
        Haydn::X2SUBADD32S, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SUBADD32S_HLLH (RR) units=0x1C
        Haydn::X2SUBADD32S_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SUBADD32_HLLH (RR) units=0x1C
        Haydn::X2SUBADD32_HLLH, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X2SWAP32 (R) units=0x1C
        Haydn::X2SWAP32, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4ABS16 (R) units=0x1C
        Haydn::X4ABS16, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4ABS16S (R) units=0x1C
        Haydn::X4ABS16S, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4ADD16 (RR) units=0x1C
        Haydn::X4ADD16, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4ADD16S (RR) units=0x1C
        Haydn::X4ADD16S, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4CJMUL16S_H (RR) units=0x60
        Haydn::X4CJMUL16S_H, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4CJMUL16S_L (RR) units=0x60
        Haydn::X4CJMUL16S_L, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4CJMULA16S_H (RR) units=0x60
        Haydn::X4CJMULA16S_H, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4CJMULA16S_L (RR) units=0x60
        Haydn::X4CJMULA16S_L, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4CLAMP16 (RR) units=0x1C
        Haydn::X4CLAMP16, 0, 0, 3, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4CMUL16 (R) units=0x60
        Haydn::X4CMUL16, 0, 0, 1, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4CMUL16S (R) units=0x60
        Haydn::X4CMUL16S, 0, 0, 1, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4CMUL16S_F2 (R) units=0x60
        Haydn::X4CMUL16S_F2, 0, 0, 1, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4CMUL16S_H (RR) units=0x60
        Haydn::X4CMUL16S_H, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4CMUL16S_L (RR) units=0x60
        Haydn::X4CMUL16S_L, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4CMUL16_F2 (R) units=0x60
        Haydn::X4CMUL16_F2, 0, 0, 1, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4CMULA16S_H (RR) units=0x60
        Haydn::X4CMULA16S_H, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4CMULA16S_L (RR) units=0x60
        Haydn::X4CMULA16S_L, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4CONJ16 (R) units=0x1C
        Haydn::X4CONJ16, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4CONJ16S (R) units=0x1C
        Haydn::X4CONJ16S, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4DOT16 (RR) units=0x60
        Haydn::X4DOT16, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4ENERGY16 (R) units=0x60
        Haydn::X4ENERGY16, 0, 0, 1, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4FCMUL16RS (RR) units=0x60
        Haydn::X4FCMUL16RS, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4FCMUL16RSS (RR) units=0x60
        Haydn::X4FCMUL16RSS, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4FCMULA16RS (RR) units=0x60
        Haydn::X4FCMULA16RS, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4FCMULA16RSS (RR) units=0x60
        Haydn::X4FCMULA16RSS, 0, 0, 3, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4FF2MUL16S (RRR) units=0x60
        Haydn::X4FF2MUL16S, 0, 0, 2, 2, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4FF2MULA16S (RRR) units=0x60
        Haydn::X4FF2MULA16S, 0, 0, 4, 2, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4FF2MULS16S (RRR) units=0x60
        Haydn::X4FF2MULS16S, 0, 0, 4, 2, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4FMUL16RS (RR) units=0x60
        Haydn::X4FMUL16RS, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4FMUL16RSS (RR) units=0x60
        Haydn::X4FMUL16RSS, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4FMUL16TS (RR) units=0x60
        Haydn::X4FMUL16TS, 0, 0, 2, 1, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4FRSST16 (RR) units=0x1C
        Haydn::X4FRSST16, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4FRST16 (RR) units=0x1C
        Haydn::X4FRST16, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4HADD16_H (R) units=0x1C
        Haydn::X4HADD16_H, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4HADD16_L (R) units=0x1C
        Haydn::X4HADD16_L, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4HMAX16 (R) units=0x1C
        Haydn::X4HMAX16, 0, 1, 1, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4HMIN16 (R) units=0x1C
        Haydn::X4HMIN16, 0, 1, 1, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4MAX16 (RR) units=0x1C
        Haydn::X4MAX16, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4MIN16 (RR) units=0x1C
        Haydn::X4MIN16, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4MJSWAP16 (R) units=0x1C
        Haydn::X4MJSWAP16, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4MJSWAP16S (R) units=0x1C
        Haydn::X4MJSWAP16S, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4MOVF16 (R) units=0x1C
        Haydn::X4MOVF16, 0, 0, 2, 1, 0, 0, 1, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4MOVT16 (R) units=0x1C
        Haydn::X4MOVT16, 0, 0, 2, 1, 0, 0, 1, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4MUL16 (RRR) units=0x60
        Haydn::X4MUL16, 0, 0, 2, 2, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4MULA16 (RRR) units=0x60
        Haydn::X4MULA16, 0, 0, 4, 2, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4MULA16S (RRR) units=0x60
        Haydn::X4MULA16S, 0, 0, 4, 2, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4MULS16 (RRR) units=0x60
        Haydn::X4MULS16, 0, 0, 4, 2, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4MULS16S (RRR) units=0x60
        Haydn::X4MULS16S, 0, 0, 4, 2, 0, 0, 0, 0, 0x60u,
        2u, 1u, {2},
        1u, 0u,
    },
    { // X4NEG16 (R) units=0x1C
        Haydn::X4NEG16, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4NEG16S (R) units=0x1C
        Haydn::X4NEG16S, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SAT32T16 (RR) units=0x1C
        Haydn::X4SAT32T16, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SEL16 (RRR) units=0x1C
        Haydn::X4SEL16, 1, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SELI16 (RRI4) units=0x1C
        Haydn::X4SELI16, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SEQ16 (R) units=0x1C
        Haydn::X4SEQ16, 0, 0, 2, 0, 0, 0, 0, 1, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SLE16 (R) units=0x1C
        Haydn::X4SLE16, 0, 0, 2, 0, 0, 0, 0, 1, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SLL16 (RR) units=0x1C
        Haydn::X4SLL16, 1, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SLLI16 (RI4) units=0x1C
        Haydn::X4SLLI16, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SLT16 (R) units=0x1C
        Haydn::X4SLT16, 0, 0, 2, 0, 0, 0, 0, 1, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SRA16 (RR) units=0x1C
        Haydn::X4SRA16, 1, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SRA16R (RR) units=0x1C
        Haydn::X4SRA16R, 1, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SRAI16 (RI4) units=0x1C
        Haydn::X4SRAI16, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SRAI16R (RI4) units=0x1C
        Haydn::X4SRAI16R, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SRL16 (RR) units=0x1C
        Haydn::X4SRL16, 1, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SRLI16 (RI4) units=0x1C
        Haydn::X4SRLI16, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SUB16 (RR) units=0x1C
        Haydn::X4SUB16, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SUB16S (RR) units=0x1C
        Haydn::X4SUB16S, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // X4SWAP16 (R) units=0x1C
        Haydn::X4SWAP16, 0, 0, 1, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // XOR32 (RR) units=0x1C
        Haydn::XOR32, 2, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // XOR64 (RR) units=0x1C
        Haydn::XOR64, 0, 0, 2, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // XORI32 (RI20) units=0x1C
        Haydn::XORI32, 1, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ZERO_DR (I8) units=0x1C
        Haydn::ZERO_DR, 0, 0, 0, 1, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ZERO_GPR (I8) units=0x1C
        Haydn::ZERO_GPR, 0, 1, 0, 0, 0, 0, 0, 0, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
    { // ZERO_SFR (I8) units=0x1C
        Haydn::ZERO_SFR, 0, 0, 0, 0, 0, 0, 0, 1, 0x1Cu,
        1u, 1u, {1},
        1u, 0u,
    },
};

static constexpr unsigned HaydnAdmittedPerOpRecordCount =
    sizeof(Table) / sizeof(Table[0]);
static_assert(HaydnAdmittedPerOpRecordCount > 0u,
              "empty per-op import is not an import");

inline const HaydnAdmittedPerOpResourceRecord *
haydnGetAdmittedPerOpResourceRecord(unsigned Opcode) {
  switch (Opcode) {
  default:
    return nullptr;
  case Haydn::ABS32:
    return &Table[0];
  case Haydn::ABS32S:
    return &Table[1];
  case Haydn::ABS64:
    return &Table[2];
  case Haydn::ABS64S:
    return &Table[3];
  case Haydn::ADD32:
    return &Table[4];
  case Haydn::ADD32S:
    return &Table[5];
  case Haydn::ADD64:
    return &Table[6];
  case Haydn::ADD64S:
    return &Table[7];
  case Haydn::ADD64S_H:
    return &Table[8];
  case Haydn::ADD64S_L:
    return &Table[9];
  case Haydn::ADD64_H:
    return &Table[10];
  case Haydn::ADD64_L:
    return &Table[11];
  case Haydn::ADDI32:
    return &Table[12];
  case Haydn::ADDI32S:
    return &Table[13];
  case Haydn::ADDI32_W:
    return &Table[14];
  case Haydn::AND32:
    return &Table[15];
  case Haydn::AND64:
    return &Table[16];
  case Haydn::ANDI32:
    return &Table[17];
  case Haydn::ARCTAN:
    return &Table[18];
  case Haydn::ASR32:
    return &Table[19];
  case Haydn::BEQ:
    return &Table[20];
  case Haydn::BEQZ:
    return &Table[21];
  case Haydn::BEQZ_W:
    return &Table[22];
  case Haydn::BEQ_W:
    return &Table[23];
  case Haydn::BGE:
    return &Table[24];
  case Haydn::BGEU:
    return &Table[25];
  case Haydn::BGEU_W:
    return &Table[26];
  case Haydn::BGEZ:
    return &Table[27];
  case Haydn::BGEZ_W:
    return &Table[28];
  case Haydn::BGE_W:
    return &Table[29];
  case Haydn::BLT:
    return &Table[30];
  case Haydn::BLTU:
    return &Table[31];
  case Haydn::BLTU_W:
    return &Table[32];
  case Haydn::BLTZ:
    return &Table[33];
  case Haydn::BLTZ_W:
    return &Table[34];
  case Haydn::BLT_W:
    return &Table[35];
  case Haydn::BNE:
    return &Table[36];
  case Haydn::BNEZ:
    return &Table[37];
  case Haydn::BNEZ_W:
    return &Table[38];
  case Haydn::BNE_W:
    return &Table[39];
  case Haydn::BREV32:
    return &Table[40];
  case Haydn::CSRR:
    return &Table[41];
  case Haydn::CSRW:
    return &Table[42];
  case Haydn::CSRW_W:
    return &Table[43];
  case Haydn::D_LDW_BREV_IMM:
    return &Table[44];
  case Haydn::D_LDW_BREV_REG:
    return &Table[45];
  case Haydn::D_LDW_CB_IMM:
    return &Table[46];
  case Haydn::D_LDW_CB_REG:
    return &Table[47];
  case Haydn::D_LDW_POST_IMM:
    return &Table[48];
  case Haydn::D_LDW_POST_REG:
    return &Table[49];
  case Haydn::D_LDW_PRE_IMM:
    return &Table[50];
  case Haydn::D_LDW_PRE_REG:
    return &Table[51];
  case Haydn::D_LDW_WITH_IMM:
    return &Table[52];
  case Haydn::D_LDW_WITH_REG:
    return &Table[53];
  case Haydn::D_LHW_POST_IMM:
    return &Table[54];
  case Haydn::D_LHW_POST_REG:
    return &Table[55];
  case Haydn::D_LHW_PRE_IMM:
    return &Table[56];
  case Haydn::D_LHW_PRE_REG:
    return &Table[57];
  case Haydn::D_LHW_WITH_IMM:
    return &Table[58];
  case Haydn::D_LHW_WITH_REG:
    return &Table[59];
  case Haydn::D_LQHWUA_CB_POST:
    return &Table[60];
  case Haydn::D_LQHWUA_POST:
    return &Table[61];
  case Haydn::D_LTWUA_CB_POST:
    return &Table[62];
  case Haydn::D_LTWUA_POST:
    return &Table[63];
  case Haydn::D_LW_POST_IMM:
    return &Table[64];
  case Haydn::D_LW_POST_REG:
    return &Table[65];
  case Haydn::D_LW_PRE_IMM:
    return &Table[66];
  case Haydn::D_LW_PRE_REG:
    return &Table[67];
  case Haydn::D_LW_WITH_IMM:
    return &Table[68];
  case Haydn::D_LW_WITH_REG:
    return &Table[69];
  case Haydn::D_SDW_BREV_IMM:
    return &Table[70];
  case Haydn::D_SDW_BREV_REG:
    return &Table[71];
  case Haydn::D_SDW_CB_IMM:
    return &Table[72];
  case Haydn::D_SDW_CB_REG:
    return &Table[73];
  case Haydn::D_SDW_POST_IMM:
    return &Table[74];
  case Haydn::D_SDW_POST_REG:
    return &Table[75];
  case Haydn::D_SDW_PRE_IMM:
    return &Table[76];
  case Haydn::D_SDW_PRE_REG:
    return &Table[77];
  case Haydn::D_SDW_WITH_IMM:
    return &Table[78];
  case Haydn::D_SDW_WITH_REG:
    return &Table[79];
  case Haydn::D_SHW_POST_IMM:
    return &Table[80];
  case Haydn::D_SHW_POST_REG:
    return &Table[81];
  case Haydn::D_SHW_PRE_IMM:
    return &Table[82];
  case Haydn::D_SHW_PRE_REG:
    return &Table[83];
  case Haydn::D_SHW_WITH_IMM:
    return &Table[84];
  case Haydn::D_SHW_WITH_REG:
    return &Table[85];
  case Haydn::D_SQHWUA_CB_POST:
    return &Table[86];
  case Haydn::D_SQHWUA_POST:
    return &Table[87];
  case Haydn::D_STWUA_CB_POST:
    return &Table[88];
  case Haydn::D_STWUA_POST:
    return &Table[89];
  case Haydn::D_SW_F64RS_POST_IMM:
    return &Table[90];
  case Haydn::D_SW_F64RS_POST_REG:
    return &Table[91];
  case Haydn::D_SW_F64RS_WITH_IMM:
    return &Table[92];
  case Haydn::D_SW_F64RS_WITH_REG:
    return &Table[93];
  case Haydn::D_SW_H_POST_IMM:
    return &Table[94];
  case Haydn::D_SW_H_POST_REG:
    return &Table[95];
  case Haydn::D_SW_H_PRE_IMM:
    return &Table[96];
  case Haydn::D_SW_H_PRE_REG:
    return &Table[97];
  case Haydn::D_SW_H_WITH_IMM:
    return &Table[98];
  case Haydn::D_SW_H_WITH_REG:
    return &Table[99];
  case Haydn::D_SW_L_POST_IMM:
    return &Table[100];
  case Haydn::D_SW_L_POST_REG:
    return &Table[101];
  case Haydn::D_SW_L_PRE_IMM:
    return &Table[102];
  case Haydn::D_SW_L_PRE_REG:
    return &Table[103];
  case Haydn::D_SW_L_WITH_IMM:
    return &Table[104];
  case Haydn::D_SW_L_WITH_REG:
    return &Table[105];
  case Haydn::EXP2:
    return &Table[106];
  case Haydn::F2MULAA32RS_HHLL:
    return &Table[107];
  case Haydn::F2MULAA32RS_HLLH:
    return &Table[108];
  case Haydn::F2MULAA32R_HHLL:
    return &Table[109];
  case Haydn::F2MULAA32R_HLLH:
    return &Table[110];
  case Haydn::F2MULAS32RS_HHLL:
    return &Table[111];
  case Haydn::F2MULAS32RS_HLLH:
    return &Table[112];
  case Haydn::F2MULAS32R_HHLL:
    return &Table[113];
  case Haydn::F2MULAS32R_HLLH:
    return &Table[114];
  case Haydn::F2MULSA32RS_HHLL:
    return &Table[115];
  case Haydn::F2MULSA32RS_HLLH:
    return &Table[116];
  case Haydn::F2MULSA32R_HHLL:
    return &Table[117];
  case Haydn::F2MULSA32R_HLLH:
    return &Table[118];
  case Haydn::F2MULSS32RS_HHLL:
    return &Table[119];
  case Haydn::F2MULSS32RS_HLLH:
    return &Table[120];
  case Haydn::F2MULSS32R_HHLL:
    return &Table[121];
  case Haydn::F2MULSS32R_HLLH:
    return &Table[122];
  case Haydn::F2MULZAA32RS_HHLL:
    return &Table[123];
  case Haydn::F2MULZAA32RS_HLLH:
    return &Table[124];
  case Haydn::F2MULZAA32R_HHLL:
    return &Table[125];
  case Haydn::F2MULZAA32R_HLLH:
    return &Table[126];
  case Haydn::F2MULZAS32RS_HHLL:
    return &Table[127];
  case Haydn::F2MULZAS32RS_HLLH:
    return &Table[128];
  case Haydn::F2MULZAS32R_HHLL:
    return &Table[129];
  case Haydn::F2MULZAS32R_HLLH:
    return &Table[130];
  case Haydn::F2MULZSA32RS_HHLL:
    return &Table[131];
  case Haydn::F2MULZSA32RS_HLLH:
    return &Table[132];
  case Haydn::F2MULZSA32R_HHLL:
    return &Table[133];
  case Haydn::F2MULZSA32R_HLLH:
    return &Table[134];
  case Haydn::F2MULZSS32RS_HHLL:
    return &Table[135];
  case Haydn::F2MULZSS32RS_HLLH:
    return &Table[136];
  case Haydn::F2MULZSS32R_HHLL:
    return &Table[137];
  case Haydn::F2MULZSS32R_HLLH:
    return &Table[138];
  case Haydn::FF2MUL32RS_HH:
    return &Table[139];
  case Haydn::FF2MUL32RS_LH:
    return &Table[140];
  case Haydn::FF2MUL32RS_LL:
    return &Table[141];
  case Haydn::FF2MUL32R_HH:
    return &Table[142];
  case Haydn::FF2MUL32R_LH:
    return &Table[143];
  case Haydn::FF2MUL32R_LL:
    return &Table[144];
  case Haydn::FF2MULA32RS_HH:
    return &Table[145];
  case Haydn::FF2MULA32RS_LH:
    return &Table[146];
  case Haydn::FF2MULA32RS_LL:
    return &Table[147];
  case Haydn::FF2MULA32R_HH:
    return &Table[148];
  case Haydn::FF2MULA32R_LH:
    return &Table[149];
  case Haydn::FF2MULA32R_LL:
    return &Table[150];
  case Haydn::FF2MULS32RS_HH:
    return &Table[151];
  case Haydn::FF2MULS32RS_LH:
    return &Table[152];
  case Haydn::FF2MULS32RS_LL:
    return &Table[153];
  case Haydn::FF2MULS32R_HH:
    return &Table[154];
  case Haydn::FF2MULS32R_LH:
    return &Table[155];
  case Haydn::FF2MULS32R_LL:
    return &Table[156];
  case Haydn::FLAR:
    return &Table[157];
  case Haydn::FMUL16_HS00:
    return &Table[158];
  case Haydn::FMUL16_HS01:
    return &Table[159];
  case Haydn::FMUL16_HS02:
    return &Table[160];
  case Haydn::FMUL16_HS03:
    return &Table[161];
  case Haydn::FMUL16_HS11:
    return &Table[162];
  case Haydn::FMUL16_HS12:
    return &Table[163];
  case Haydn::FMUL16_HS13:
    return &Table[164];
  case Haydn::FMUL16_HS22:
    return &Table[165];
  case Haydn::FMUL16_HS23:
    return &Table[166];
  case Haydn::FMUL16_HS33:
    return &Table[167];
  case Haydn::FMUL16_LS00:
    return &Table[168];
  case Haydn::FMUL16_LS01:
    return &Table[169];
  case Haydn::FMUL16_LS02:
    return &Table[170];
  case Haydn::FMUL16_LS03:
    return &Table[171];
  case Haydn::FMUL16_LS11:
    return &Table[172];
  case Haydn::FMUL16_LS12:
    return &Table[173];
  case Haydn::FMUL16_LS13:
    return &Table[174];
  case Haydn::FMUL16_LS22:
    return &Table[175];
  case Haydn::FMUL16_LS23:
    return &Table[176];
  case Haydn::FMUL16_LS33:
    return &Table[177];
  case Haydn::FMUL32S_HH:
    return &Table[178];
  case Haydn::FMUL32S_LH:
    return &Table[179];
  case Haydn::FMUL32S_LL:
    return &Table[180];
  case Haydn::FMUL32X16_H0:
    return &Table[181];
  case Haydn::FMUL32X16_H1:
    return &Table[182];
  case Haydn::FMUL32X16_H2:
    return &Table[183];
  case Haydn::FMUL32X16_H3:
    return &Table[184];
  case Haydn::FMUL32X16_L0:
    return &Table[185];
  case Haydn::FMUL32X16_L1:
    return &Table[186];
  case Haydn::FMUL32X16_L2:
    return &Table[187];
  case Haydn::FMUL32X16_L3:
    return &Table[188];
  case Haydn::FMULA16_HS00:
    return &Table[189];
  case Haydn::FMULA16_HS01:
    return &Table[190];
  case Haydn::FMULA16_HS02:
    return &Table[191];
  case Haydn::FMULA16_HS03:
    return &Table[192];
  case Haydn::FMULA16_HS11:
    return &Table[193];
  case Haydn::FMULA16_HS12:
    return &Table[194];
  case Haydn::FMULA16_HS13:
    return &Table[195];
  case Haydn::FMULA16_HS22:
    return &Table[196];
  case Haydn::FMULA16_HS23:
    return &Table[197];
  case Haydn::FMULA16_HS33:
    return &Table[198];
  case Haydn::FMULA16_LS00:
    return &Table[199];
  case Haydn::FMULA16_LS01:
    return &Table[200];
  case Haydn::FMULA16_LS02:
    return &Table[201];
  case Haydn::FMULA16_LS03:
    return &Table[202];
  case Haydn::FMULA16_LS11:
    return &Table[203];
  case Haydn::FMULA16_LS12:
    return &Table[204];
  case Haydn::FMULA16_LS13:
    return &Table[205];
  case Haydn::FMULA16_LS22:
    return &Table[206];
  case Haydn::FMULA16_LS23:
    return &Table[207];
  case Haydn::FMULA16_LS33:
    return &Table[208];
  case Haydn::FMULA32S_HH:
    return &Table[209];
  case Haydn::FMULA32S_LH:
    return &Table[210];
  case Haydn::FMULA32S_LL:
    return &Table[211];
  case Haydn::FMULA32X16_H0:
    return &Table[212];
  case Haydn::FMULA32X16_H1:
    return &Table[213];
  case Haydn::FMULA32X16_H2:
    return &Table[214];
  case Haydn::FMULA32X16_H3:
    return &Table[215];
  case Haydn::FMULA32X16_L0:
    return &Table[216];
  case Haydn::FMULA32X16_L1:
    return &Table[217];
  case Haydn::FMULA32X16_L2:
    return &Table[218];
  case Haydn::FMULA32X16_L3:
    return &Table[219];
  case Haydn::FMULAA16_HS_11_00:
    return &Table[220];
  case Haydn::FMULAA16_HS_13_02:
    return &Table[221];
  case Haydn::FMULAA16_HS_33_22:
    return &Table[222];
  case Haydn::FMULAA16_LS_11_00:
    return &Table[223];
  case Haydn::FMULAA16_LS_13_02:
    return &Table[224];
  case Haydn::FMULAA16_LS_33_22:
    return &Table[225];
  case Haydn::FMULAA32S_HHLL:
    return &Table[226];
  case Haydn::FMULAA32S_HLLH:
    return &Table[227];
  case Haydn::FMULAA32X16_H0_L1:
    return &Table[228];
  case Haydn::FMULAA32X16_H1_L0:
    return &Table[229];
  case Haydn::FMULAA32X16_H2_L3:
    return &Table[230];
  case Haydn::FMULAA32X16_H3_L2:
    return &Table[231];
  case Haydn::FMULAS32S_HHLL:
    return &Table[232];
  case Haydn::FMULAS32S_HLLH:
    return &Table[233];
  case Haydn::FMULAS32X16_H1_L0:
    return &Table[234];
  case Haydn::FMULAS32X16_H3_L2:
    return &Table[235];
  case Haydn::FMULS16_HS00:
    return &Table[236];
  case Haydn::FMULS16_HS01:
    return &Table[237];
  case Haydn::FMULS16_HS02:
    return &Table[238];
  case Haydn::FMULS16_HS03:
    return &Table[239];
  case Haydn::FMULS16_HS11:
    return &Table[240];
  case Haydn::FMULS16_HS12:
    return &Table[241];
  case Haydn::FMULS16_HS13:
    return &Table[242];
  case Haydn::FMULS16_HS22:
    return &Table[243];
  case Haydn::FMULS16_HS23:
    return &Table[244];
  case Haydn::FMULS16_HS33:
    return &Table[245];
  case Haydn::FMULS16_LS00:
    return &Table[246];
  case Haydn::FMULS16_LS01:
    return &Table[247];
  case Haydn::FMULS16_LS02:
    return &Table[248];
  case Haydn::FMULS16_LS03:
    return &Table[249];
  case Haydn::FMULS16_LS11:
    return &Table[250];
  case Haydn::FMULS16_LS12:
    return &Table[251];
  case Haydn::FMULS16_LS13:
    return &Table[252];
  case Haydn::FMULS16_LS22:
    return &Table[253];
  case Haydn::FMULS16_LS23:
    return &Table[254];
  case Haydn::FMULS16_LS33:
    return &Table[255];
  case Haydn::FMULS32S_HH:
    return &Table[256];
  case Haydn::FMULS32S_LH:
    return &Table[257];
  case Haydn::FMULS32S_LL:
    return &Table[258];
  case Haydn::FMULS32X16_H0:
    return &Table[259];
  case Haydn::FMULS32X16_H1:
    return &Table[260];
  case Haydn::FMULS32X16_H2:
    return &Table[261];
  case Haydn::FMULS32X16_H3:
    return &Table[262];
  case Haydn::FMULS32X16_L0:
    return &Table[263];
  case Haydn::FMULS32X16_L1:
    return &Table[264];
  case Haydn::FMULS32X16_L2:
    return &Table[265];
  case Haydn::FMULS32X16_L3:
    return &Table[266];
  case Haydn::FMULSA32S_HHLL:
    return &Table[267];
  case Haydn::FMULSA32S_HLLH:
    return &Table[268];
  case Haydn::FMULSA32X16_H1_L0:
    return &Table[269];
  case Haydn::FMULSA32X16_H3_L2:
    return &Table[270];
  case Haydn::FMULSS16_HS_11_00:
    return &Table[271];
  case Haydn::FMULSS16_HS_13_02:
    return &Table[272];
  case Haydn::FMULSS16_HS_33_22:
    return &Table[273];
  case Haydn::FMULSS16_LS_11_00:
    return &Table[274];
  case Haydn::FMULSS16_LS_13_02:
    return &Table[275];
  case Haydn::FMULSS16_LS_33_22:
    return &Table[276];
  case Haydn::FMULSS32S_HHLL:
    return &Table[277];
  case Haydn::FMULSS32S_HLLH:
    return &Table[278];
  case Haydn::FMULSS32X16_H1_L0:
    return &Table[279];
  case Haydn::FMULSS32X16_H3_L2:
    return &Table[280];
  case Haydn::FMULZAA16_HS_11_00:
    return &Table[281];
  case Haydn::FMULZAA16_HS_13_02:
    return &Table[282];
  case Haydn::FMULZAA16_HS_33_22:
    return &Table[283];
  case Haydn::FMULZAA16_LS_11_00:
    return &Table[284];
  case Haydn::FMULZAA16_LS_13_02:
    return &Table[285];
  case Haydn::FMULZAA16_LS_33_22:
    return &Table[286];
  case Haydn::FMULZAA32S_HHLL:
    return &Table[287];
  case Haydn::FMULZAA32S_HLLH:
    return &Table[288];
  case Haydn::FMULZAA32X16_H0_L1:
    return &Table[289];
  case Haydn::FMULZAA32X16_H1_L0:
    return &Table[290];
  case Haydn::FMULZAA32X16_H2_L3:
    return &Table[291];
  case Haydn::FMULZAA32X16_H3_L2:
    return &Table[292];
  case Haydn::FMULZAS32S_HHLL:
    return &Table[293];
  case Haydn::FMULZAS32S_HLLH:
    return &Table[294];
  case Haydn::FMULZAS32X16_H1_L0:
    return &Table[295];
  case Haydn::FMULZAS32X16_H3_L2:
    return &Table[296];
  case Haydn::FMULZSA32S_HHLL:
    return &Table[297];
  case Haydn::FMULZSA32S_HLLH:
    return &Table[298];
  case Haydn::FMULZSA32X16_H1_L0:
    return &Table[299];
  case Haydn::FMULZSA32X16_H3_L2:
    return &Table[300];
  case Haydn::FMULZSS16_HS_11_00:
    return &Table[301];
  case Haydn::FMULZSS16_HS_13_02:
    return &Table[302];
  case Haydn::FMULZSS16_HS_33_22:
    return &Table[303];
  case Haydn::FMULZSS16_LS_11_00:
    return &Table[304];
  case Haydn::FMULZSS16_LS_13_02:
    return &Table[305];
  case Haydn::FMULZSS16_LS_33_22:
    return &Table[306];
  case Haydn::FMULZSS32S_HHLL:
    return &Table[307];
  case Haydn::FMULZSS32S_HLLH:
    return &Table[308];
  case Haydn::FMULZSS32X16_H1_L0:
    return &Table[309];
  case Haydn::FMULZSS32X16_H3_L2:
    return &Table[310];
  case Haydn::JAL:
    return &Table[311];
  case Haydn::JALR:
    return &Table[312];
  case Haydn::JALR_W:
    return &Table[313];
  case Haydn::JAL_W:
    return &Table[314];
  case Haydn::LD16:
    return &Table[315];
  case Haydn::LD32:
    return &Table[316];
  case Haydn::LD32_POST:
    return &Table[317];
  case Haydn::LD32_REG_M0S0LS:
    return &Table[318];
  case Haydn::LD64:
    return &Table[319];
  case Haydn::LD64_POST:
    return &Table[320];
  case Haydn::LD64_REG_M0S0LS:
    return &Table[321];
  case Haydn::LD8:
    return &Table[322];
  case Haydn::LDU16:
    return &Table[323];
  case Haydn::LDU8:
    return &Table[324];
  case Haydn::LOG2:
    return &Table[325];
  case Haydn::LSR32:
    return &Table[326];
  case Haydn::LUI:
    return &Table[327];
  case Haydn::MAX32:
    return &Table[328];
  case Haydn::MAX64:
    return &Table[329];
  case Haydn::MAXU32:
    return &Table[330];
  case Haydn::MIN32:
    return &Table[331];
  case Haydn::MIN64:
    return &Table[332];
  case Haydn::MINU32:
    return &Table[333];
  case Haydn::MOVE32:
    return &Table[334];
  case Haydn::MOVE32_DR_H:
    return &Table[335];
  case Haydn::MOVE32_DR_L:
    return &Table[336];
  case Haydn::MOVE64:
    return &Table[337];
  case Haydn::MOVEGPR2SFR:
    return &Table[338];
  case Haydn::MOVEI_H:
    return &Table[339];
  case Haydn::MOVEI_L:
    return &Table[340];
  case Haydn::MOVESFR2GPR:
    return &Table[341];
  case Haydn::MOVF32:
    return &Table[342];
  case Haydn::MOVF64:
    return &Table[343];
  case Haydn::MOVT32:
    return &Table[344];
  case Haydn::MOVT64:
    return &Table[345];
  case Haydn::MUL16AQ:
    return &Table[346];
  case Haydn::MUL16ZAQ:
    return &Table[347];
  case Haydn::MUL32X16_H0:
    return &Table[348];
  case Haydn::MUL32X16_H1:
    return &Table[349];
  case Haydn::MUL32X16_H2:
    return &Table[350];
  case Haydn::MUL32X16_H3:
    return &Table[351];
  case Haydn::MUL32X16_L0:
    return &Table[352];
  case Haydn::MUL32X16_L1:
    return &Table[353];
  case Haydn::MUL32X16_L2:
    return &Table[354];
  case Haydn::MUL32X16_L3:
    return &Table[355];
  case Haydn::MUL64_HH:
    return &Table[356];
  case Haydn::MUL64_HL:
    return &Table[357];
  case Haydn::MUL64_HUH:
    return &Table[358];
  case Haydn::MUL64_HUL:
    return &Table[359];
  case Haydn::MUL64_LH:
    return &Table[360];
  case Haydn::MUL64_LL:
    return &Table[361];
  case Haydn::MUL64_LUH:
    return &Table[362];
  case Haydn::MUL64_LUL:
    return &Table[363];
  case Haydn::MUL64_UHH:
    return &Table[364];
  case Haydn::MUL64_UHL:
    return &Table[365];
  case Haydn::MUL64_UHUH:
    return &Table[366];
  case Haydn::MUL64_UHUL:
    return &Table[367];
  case Haydn::MUL64_ULH:
    return &Table[368];
  case Haydn::MUL64_ULL:
    return &Table[369];
  case Haydn::MUL64_ULUH:
    return &Table[370];
  case Haydn::MUL64_ULUL:
    return &Table[371];
  case Haydn::MULA32X16_H0:
    return &Table[372];
  case Haydn::MULA32X16_H1:
    return &Table[373];
  case Haydn::MULA32X16_H2:
    return &Table[374];
  case Haydn::MULA32X16_H3:
    return &Table[375];
  case Haydn::MULA32X16_L0:
    return &Table[376];
  case Haydn::MULA32X16_L1:
    return &Table[377];
  case Haydn::MULA32X16_L2:
    return &Table[378];
  case Haydn::MULA32X16_L3:
    return &Table[379];
  case Haydn::MULA64_HH:
    return &Table[380];
  case Haydn::MULA64_HL:
    return &Table[381];
  case Haydn::MULA64_HUH:
    return &Table[382];
  case Haydn::MULA64_HUL:
    return &Table[383];
  case Haydn::MULA64_LH:
    return &Table[384];
  case Haydn::MULA64_LL:
    return &Table[385];
  case Haydn::MULA64_LUH:
    return &Table[386];
  case Haydn::MULA64_LUL:
    return &Table[387];
  case Haydn::MULA64_UHH:
    return &Table[388];
  case Haydn::MULA64_UHL:
    return &Table[389];
  case Haydn::MULA64_UHUH:
    return &Table[390];
  case Haydn::MULA64_UHUL:
    return &Table[391];
  case Haydn::MULA64_ULH:
    return &Table[392];
  case Haydn::MULA64_ULL:
    return &Table[393];
  case Haydn::MULA64_ULUH:
    return &Table[394];
  case Haydn::MULA64_ULUL:
    return &Table[395];
  case Haydn::MULAA32X16_H0_L1:
    return &Table[396];
  case Haydn::MULAA32X16_H1_L0:
    return &Table[397];
  case Haydn::MULAA32X16_H2_L3:
    return &Table[398];
  case Haydn::MULAA32X16_H3_L2:
    return &Table[399];
  case Haydn::MULAA32_HHLL:
    return &Table[400];
  case Haydn::MULAA32_HLLH:
    return &Table[401];
  case Haydn::MULAS32X16_H1_L0:
    return &Table[402];
  case Haydn::MULAS32X16_H3_L2:
    return &Table[403];
  case Haydn::MULAS32_HHLL:
    return &Table[404];
  case Haydn::MULAS32_HLLH:
    return &Table[405];
  case Haydn::MULAS64_HH:
    return &Table[406];
  case Haydn::MULAS64_HL:
    return &Table[407];
  case Haydn::MULAS64_HUH:
    return &Table[408];
  case Haydn::MULAS64_HUL:
    return &Table[409];
  case Haydn::MULAS64_LH:
    return &Table[410];
  case Haydn::MULAS64_LL:
    return &Table[411];
  case Haydn::MULAS64_LUH:
    return &Table[412];
  case Haydn::MULAS64_LUL:
    return &Table[413];
  case Haydn::MULAS64_UHH:
    return &Table[414];
  case Haydn::MULAS64_UHL:
    return &Table[415];
  case Haydn::MULAS64_UHUH:
    return &Table[416];
  case Haydn::MULAS64_UHUL:
    return &Table[417];
  case Haydn::MULAS64_ULH:
    return &Table[418];
  case Haydn::MULAS64_ULL:
    return &Table[419];
  case Haydn::MULAS64_ULUH:
    return &Table[420];
  case Haydn::MULAS64_ULUL:
    return &Table[421];
  case Haydn::MULL:
    return &Table[422];
  case Haydn::MULS32X16_H0:
    return &Table[423];
  case Haydn::MULS32X16_H1:
    return &Table[424];
  case Haydn::MULS32X16_H2:
    return &Table[425];
  case Haydn::MULS32X16_H3:
    return &Table[426];
  case Haydn::MULS32X16_L0:
    return &Table[427];
  case Haydn::MULS32X16_L1:
    return &Table[428];
  case Haydn::MULS32X16_L2:
    return &Table[429];
  case Haydn::MULS32X16_L3:
    return &Table[430];
  case Haydn::MULS64_HH:
    return &Table[431];
  case Haydn::MULS64_HL:
    return &Table[432];
  case Haydn::MULS64_HUH:
    return &Table[433];
  case Haydn::MULS64_HUL:
    return &Table[434];
  case Haydn::MULS64_LH:
    return &Table[435];
  case Haydn::MULS64_LL:
    return &Table[436];
  case Haydn::MULS64_LUH:
    return &Table[437];
  case Haydn::MULS64_LUL:
    return &Table[438];
  case Haydn::MULS64_UHH:
    return &Table[439];
  case Haydn::MULS64_UHL:
    return &Table[440];
  case Haydn::MULS64_UHUH:
    return &Table[441];
  case Haydn::MULS64_UHUL:
    return &Table[442];
  case Haydn::MULS64_ULH:
    return &Table[443];
  case Haydn::MULS64_ULL:
    return &Table[444];
  case Haydn::MULS64_ULUH:
    return &Table[445];
  case Haydn::MULS64_ULUL:
    return &Table[446];
  case Haydn::MULSA32X16_H1_L0:
    return &Table[447];
  case Haydn::MULSA32X16_H3_L2:
    return &Table[448];
  case Haydn::MULSA32_HHLL:
    return &Table[449];
  case Haydn::MULSA32_HLLH:
    return &Table[450];
  case Haydn::MULSS32X16_H1_L0:
    return &Table[451];
  case Haydn::MULSS32X16_H3_L2:
    return &Table[452];
  case Haydn::MULSS32_HHLL:
    return &Table[453];
  case Haydn::MULSS32_HLLH:
    return &Table[454];
  case Haydn::MULSS64_HH:
    return &Table[455];
  case Haydn::MULSS64_HL:
    return &Table[456];
  case Haydn::MULSS64_HUH:
    return &Table[457];
  case Haydn::MULSS64_HUL:
    return &Table[458];
  case Haydn::MULSS64_LH:
    return &Table[459];
  case Haydn::MULSS64_LL:
    return &Table[460];
  case Haydn::MULSS64_LUH:
    return &Table[461];
  case Haydn::MULSS64_LUL:
    return &Table[462];
  case Haydn::MULSS64_UHH:
    return &Table[463];
  case Haydn::MULSS64_UHL:
    return &Table[464];
  case Haydn::MULSS64_UHUH:
    return &Table[465];
  case Haydn::MULSS64_UHUL:
    return &Table[466];
  case Haydn::MULSS64_ULH:
    return &Table[467];
  case Haydn::MULSS64_ULL:
    return &Table[468];
  case Haydn::MULSS64_ULUH:
    return &Table[469];
  case Haydn::MULSS64_ULUL:
    return &Table[470];
  case Haydn::MULSSH:
    return &Table[471];
  case Haydn::MULSUH:
    return &Table[472];
  case Haydn::MULUUH:
    return &Table[473];
  case Haydn::MULZAA32X16_H0_L1:
    return &Table[474];
  case Haydn::MULZAA32X16_H1_L0:
    return &Table[475];
  case Haydn::MULZAA32X16_H2_L3:
    return &Table[476];
  case Haydn::MULZAA32X16_H3_L2:
    return &Table[477];
  case Haydn::MULZAA32_HHLL:
    return &Table[478];
  case Haydn::MULZAA32_HLLH:
    return &Table[479];
  case Haydn::MULZAS32X16_H1_L0:
    return &Table[480];
  case Haydn::MULZAS32X16_H3_L2:
    return &Table[481];
  case Haydn::MULZAS32_HHLL:
    return &Table[482];
  case Haydn::MULZAS32_HLLH:
    return &Table[483];
  case Haydn::MULZSA32X16_H1_L0:
    return &Table[484];
  case Haydn::MULZSA32X16_H3_L2:
    return &Table[485];
  case Haydn::MULZSA32_HHLL:
    return &Table[486];
  case Haydn::MULZSA32_HLLH:
    return &Table[487];
  case Haydn::MULZSS32X16_H1_L0:
    return &Table[488];
  case Haydn::MULZSS32X16_H3_L2:
    return &Table[489];
  case Haydn::MULZSS32_HHLL:
    return &Table[490];
  case Haydn::MULZSS32_HLLH:
    return &Table[491];
  case Haydn::NEG32:
    return &Table[492];
  case Haydn::NEG32S:
    return &Table[493];
  case Haydn::NEG64:
    return &Table[494];
  case Haydn::NEG64S:
    return &Table[495];
  case Haydn::NOT32:
    return &Table[496];
  case Haydn::NOT64:
    return &Table[497];
  case Haydn::NSA16_L:
    return &Table[498];
  case Haydn::NSA32:
    return &Table[499];
  case Haydn::NSA32_L:
    return &Table[500];
  case Haydn::NSA64:
    return &Table[501];
  case Haydn::NSAU32:
    return &Table[502];
  case Haydn::NSAZ16_L:
    return &Table[503];
  case Haydn::NSAZ32_L:
    return &Table[504];
  case Haydn::NSAZ64:
    return &Table[505];
  case Haydn::OR32:
    return &Table[506];
  case Haydn::OR64:
    return &Table[507];
  case Haydn::ORI32:
    return &Table[508];
  case Haydn::ORI32_W:
    return &Table[509];
  case Haydn::PLDWWUA:
    return &Table[510];
  case Haydn::PLDWWUA_POST:
    return &Table[511];
  case Haydn::PLQHWUA_CB_POST:
    return &Table[512];
  case Haydn::PLTWWUA_CB_POST:
    return &Table[513];
  case Haydn::POPCOUNT32:
    return &Table[514];
  case Haydn::POPCOUNT64:
    return &Table[515];
  case Haydn::RECIP:
    return &Table[516];
  case Haydn::SEQ32:
    return &Table[517];
  case Haydn::SEQ64:
    return &Table[518];
  case Haydn::SET_HWLOOP:
    return &Table[519];
  case Haydn::SET_HWLOOP_F2:
    return &Table[520];
  case Haydn::SET_HWLOOP_F2_W:
    return &Table[521];
  case Haydn::SET_HWLOOP_REG:
    return &Table[522];
  case Haydn::SET_HWLOOP_REG_W:
    return &Table[523];
  case Haydn::SET_HWLOOP_W:
    return &Table[524];
  case Haydn::SEXT32T64:
    return &Table[525];
  case Haydn::SEXT_GPR32_TO_DR64:
    return &Table[526];
  case Haydn::SHL32:
    return &Table[527];
  case Haydn::SIN_COS:
    return &Table[528];
  case Haydn::SLE32:
    return &Table[529];
  case Haydn::SLE64:
    return &Table[530];
  case Haydn::SLL32:
    return &Table[531];
  case Haydn::SLL64:
    return &Table[532];
  case Haydn::SLLI32:
    return &Table[533];
  case Haydn::SLLI64:
    return &Table[534];
  case Haydn::SLT32:
    return &Table[535];
  case Haydn::SLT64:
    return &Table[536];
  case Haydn::SLTU32:
    return &Table[537];
  case Haydn::SMULA16S_00:
    return &Table[538];
  case Haydn::SMULA16S_10:
    return &Table[539];
  case Haydn::SMULA16S_11:
    return &Table[540];
  case Haydn::SMULA16S_20:
    return &Table[541];
  case Haydn::SMULA16S_21:
    return &Table[542];
  case Haydn::SMULA16S_22:
    return &Table[543];
  case Haydn::SMULA16S_30:
    return &Table[544];
  case Haydn::SMULA16S_31:
    return &Table[545];
  case Haydn::SMULA16S_32:
    return &Table[546];
  case Haydn::SMULA16S_33:
    return &Table[547];
  case Haydn::SMULA16_00:
    return &Table[548];
  case Haydn::SMULA16_10:
    return &Table[549];
  case Haydn::SMULA16_11:
    return &Table[550];
  case Haydn::SMULA16_20:
    return &Table[551];
  case Haydn::SMULA16_21:
    return &Table[552];
  case Haydn::SMULA16_22:
    return &Table[553];
  case Haydn::SMULA16_30:
    return &Table[554];
  case Haydn::SMULA16_31:
    return &Table[555];
  case Haydn::SMULA16_32:
    return &Table[556];
  case Haydn::SMULA16_33:
    return &Table[557];
  case Haydn::SMULS16S_00:
    return &Table[558];
  case Haydn::SMULS16S_10:
    return &Table[559];
  case Haydn::SMULS16S_11:
    return &Table[560];
  case Haydn::SMULS16S_20:
    return &Table[561];
  case Haydn::SMULS16S_21:
    return &Table[562];
  case Haydn::SMULS16S_22:
    return &Table[563];
  case Haydn::SMULS16S_30:
    return &Table[564];
  case Haydn::SMULS16S_31:
    return &Table[565];
  case Haydn::SMULS16S_32:
    return &Table[566];
  case Haydn::SMULS16S_33:
    return &Table[567];
  case Haydn::SMULS16_00:
    return &Table[568];
  case Haydn::SMULS16_10:
    return &Table[569];
  case Haydn::SMULS16_11:
    return &Table[570];
  case Haydn::SMULS16_20:
    return &Table[571];
  case Haydn::SMULS16_21:
    return &Table[572];
  case Haydn::SMULS16_22:
    return &Table[573];
  case Haydn::SMULS16_30:
    return &Table[574];
  case Haydn::SMULS16_31:
    return &Table[575];
  case Haydn::SMULS16_32:
    return &Table[576];
  case Haydn::SMULS16_33:
    return &Table[577];
  case Haydn::SQRT:
    return &Table[578];
  case Haydn::SRA32:
    return &Table[579];
  case Haydn::SRA32R:
    return &Table[580];
  case Haydn::SRA64:
    return &Table[581];
  case Haydn::SRA64R:
    return &Table[582];
  case Haydn::SRAI32:
    return &Table[583];
  case Haydn::SRAI32R:
    return &Table[584];
  case Haydn::SRAI64:
    return &Table[585];
  case Haydn::SRAI64R:
    return &Table[586];
  case Haydn::SRL32:
    return &Table[587];
  case Haydn::SRL64:
    return &Table[588];
  case Haydn::SRLI32:
    return &Table[589];
  case Haydn::SRLI64:
    return &Table[590];
  case Haydn::ST16:
    return &Table[591];
  case Haydn::ST32:
    return &Table[592];
  case Haydn::ST32_POST:
    return &Table[593];
  case Haydn::ST32_REG_M0S0LS:
    return &Table[594];
  case Haydn::ST64:
    return &Table[595];
  case Haydn::ST64_POST:
    return &Table[596];
  case Haydn::ST64_REG_M0S0LS:
    return &Table[597];
  case Haydn::ST8:
    return &Table[598];
  case Haydn::SUB32:
    return &Table[599];
  case Haydn::SUB32S:
    return &Table[600];
  case Haydn::SUB64:
    return &Table[601];
  case Haydn::SUB64S:
    return &Table[602];
  case Haydn::SUB64S_H:
    return &Table[603];
  case Haydn::SUB64S_L:
    return &Table[604];
  case Haydn::SUB64_H:
    return &Table[605];
  case Haydn::SUB64_L:
    return &Table[606];
  case Haydn::SUBI32:
    return &Table[607];
  case Haydn::SUBI32S:
    return &Table[608];
  case Haydn::S_LBS_POST_IMM:
    return &Table[609];
  case Haydn::S_LBS_POST_REG:
    return &Table[610];
  case Haydn::S_LBS_PRE_IMM:
    return &Table[611];
  case Haydn::S_LBS_PRE_REG:
    return &Table[612];
  case Haydn::S_LBS_WITH_IMM:
    return &Table[613];
  case Haydn::S_LBS_WITH_REG:
    return &Table[614];
  case Haydn::S_LBU_POST_IMM:
    return &Table[615];
  case Haydn::S_LBU_POST_REG:
    return &Table[616];
  case Haydn::S_LBU_PRE_IMM:
    return &Table[617];
  case Haydn::S_LBU_PRE_REG:
    return &Table[618];
  case Haydn::S_LBU_WITH_IMM:
    return &Table[619];
  case Haydn::S_LBU_WITH_REG:
    return &Table[620];
  case Haydn::S_LHWS_POST_IMM:
    return &Table[621];
  case Haydn::S_LHWS_POST_REG:
    return &Table[622];
  case Haydn::S_LHWS_PRE_IMM:
    return &Table[623];
  case Haydn::S_LHWS_PRE_REG:
    return &Table[624];
  case Haydn::S_LHWS_WITH_IMM:
    return &Table[625];
  case Haydn::S_LHWS_WITH_REG:
    return &Table[626];
  case Haydn::S_LHWU_POST_IMM:
    return &Table[627];
  case Haydn::S_LHWU_POST_REG:
    return &Table[628];
  case Haydn::S_LHWU_PRE_IMM:
    return &Table[629];
  case Haydn::S_LHWU_PRE_REG:
    return &Table[630];
  case Haydn::S_LHWU_WITH_IMM:
    return &Table[631];
  case Haydn::S_LHWU_WITH_REG:
    return &Table[632];
  case Haydn::S_LW_BREV_IMM:
    return &Table[633];
  case Haydn::S_LW_BREV_REG:
    return &Table[634];
  case Haydn::S_LW_POST_IMM:
    return &Table[635];
  case Haydn::S_LW_POST_REG:
    return &Table[636];
  case Haydn::S_LW_PRE_IMM:
    return &Table[637];
  case Haydn::S_LW_PRE_REG:
    return &Table[638];
  case Haydn::S_LW_WITH_IMM:
    return &Table[639];
  case Haydn::S_LW_WITH_REG:
    return &Table[640];
  case Haydn::S_SB_POST_IMM:
    return &Table[641];
  case Haydn::S_SB_POST_REG:
    return &Table[642];
  case Haydn::S_SB_PRE_IMM:
    return &Table[643];
  case Haydn::S_SB_PRE_REG:
    return &Table[644];
  case Haydn::S_SB_WITH_IMM:
    return &Table[645];
  case Haydn::S_SB_WITH_REG:
    return &Table[646];
  case Haydn::S_SHW_POST_IMM:
    return &Table[647];
  case Haydn::S_SHW_POST_REG:
    return &Table[648];
  case Haydn::S_SHW_PRE_IMM:
    return &Table[649];
  case Haydn::S_SHW_PRE_REG:
    return &Table[650];
  case Haydn::S_SHW_WITH_IMM:
    return &Table[651];
  case Haydn::S_SHW_WITH_REG:
    return &Table[652];
  case Haydn::S_SW_BREV_IMM:
    return &Table[653];
  case Haydn::S_SW_BREV_REG:
    return &Table[654];
  case Haydn::S_SW_POST_IMM:
    return &Table[655];
  case Haydn::S_SW_POST_REG:
    return &Table[656];
  case Haydn::S_SW_PRE_IMM:
    return &Table[657];
  case Haydn::S_SW_PRE_REG:
    return &Table[658];
  case Haydn::S_SW_WITH_IMM:
    return &Table[659];
  case Haydn::S_SW_WITH_REG:
    return &Table[660];
  case Haydn::TRANSF64:
    return &Table[661];
  case Haydn::TRANSF64F2:
    return &Table[662];
  case Haydn::TRANSF64_H:
    return &Table[663];
  case Haydn::TRANSF64_L:
    return &Table[664];
  case Haydn::WBARWUA:
    return &Table[665];
  case Haydn::WBARWUA_CB:
    return &Table[666];
  case Haydn::X2ABS32:
    return &Table[667];
  case Haydn::X2ABS32S:
    return &Table[668];
  case Haydn::X2ADD32:
    return &Table[669];
  case Haydn::X2ADD32S:
    return &Table[670];
  case Haydn::X2ADD32S_HLLH:
    return &Table[671];
  case Haydn::X2ADD32_HLLH:
    return &Table[672];
  case Haydn::X2ADDSUB32:
    return &Table[673];
  case Haydn::X2ADDSUB32S:
    return &Table[674];
  case Haydn::X2ADDSUB32S_HLLH:
    return &Table[675];
  case Haydn::X2ADDSUB32_HLLH:
    return &Table[676];
  case Haydn::X2CLAMP32:
    return &Table[677];
  case Haydn::X2CMUL32:
    return &Table[678];
  case Haydn::X2CMUL32S:
    return &Table[679];
  case Haydn::X2CMUL32S_F2:
    return &Table[680];
  case Haydn::X2CMUL32X16_H:
    return &Table[681];
  case Haydn::X2CMUL32X16_L:
    return &Table[682];
  case Haydn::X2CMUL32_F2:
    return &Table[683];
  case Haydn::X2CMULA32X16_H:
    return &Table[684];
  case Haydn::X2CMULA32X16_L:
    return &Table[685];
  case Haydn::X2DOT32:
    return &Table[686];
  case Haydn::X2FCMUL32RS:
    return &Table[687];
  case Haydn::X2FCMUL32RSS:
    return &Table[688];
  case Haydn::X2FCMUL32X16RS_H:
    return &Table[689];
  case Haydn::X2FCMUL32X16RS_L:
    return &Table[690];
  case Haydn::X2FCMULA32RS:
    return &Table[691];
  case Haydn::X2FCMULA32RSS:
    return &Table[692];
  case Haydn::X2FCMULA32X16RS_H:
    return &Table[693];
  case Haydn::X2FCMULA32X16RS_L:
    return &Table[694];
  case Haydn::X2FF2RSST32:
    return &Table[695];
  case Haydn::X2FF2RST32:
    return &Table[696];
  case Haydn::X2FMUL32RS:
    return &Table[697];
  case Haydn::X2FMUL32RSS:
    return &Table[698];
  case Haydn::X2FMUL32TS:
    return &Table[699];
  case Haydn::X2FMUL32X16RSS_H:
    return &Table[700];
  case Haydn::X2FMUL32X16RSS_L:
    return &Table[701];
  case Haydn::X2FMUL32X16RS_H:
    return &Table[702];
  case Haydn::X2FMUL32X16RS_L:
    return &Table[703];
  case Haydn::X2FMUL32X16TS_H:
    return &Table[704];
  case Haydn::X2FMUL32X16TS_L:
    return &Table[705];
  case Haydn::X2FMULA32RS:
    return &Table[706];
  case Haydn::X2FMULA32RSS:
    return &Table[707];
  case Haydn::X2FMULA32TS:
    return &Table[708];
  case Haydn::X2FMULA32X16RSS_H:
    return &Table[709];
  case Haydn::X2FMULA32X16RSS_L:
    return &Table[710];
  case Haydn::X2FMULA32X16RS_H:
    return &Table[711];
  case Haydn::X2FMULA32X16RS_L:
    return &Table[712];
  case Haydn::X2FMULA32X16TS_H:
    return &Table[713];
  case Haydn::X2FMULA32X16TS_L:
    return &Table[714];
  case Haydn::X2FMULS32RS:
    return &Table[715];
  case Haydn::X2FMULS32RSS:
    return &Table[716];
  case Haydn::X2FMULS32TS:
    return &Table[717];
  case Haydn::X2FMULS32X16RSS_H:
    return &Table[718];
  case Haydn::X2FMULS32X16RSS_L:
    return &Table[719];
  case Haydn::X2FMULS32X16RS_H:
    return &Table[720];
  case Haydn::X2FMULS32X16RS_L:
    return &Table[721];
  case Haydn::X2FMULS32X16TS_H:
    return &Table[722];
  case Haydn::X2FMULS32X16TS_L:
    return &Table[723];
  case Haydn::X2FRSST32:
    return &Table[724];
  case Haydn::X2FRST32:
    return &Table[725];
  case Haydn::X2HADD32S_H:
    return &Table[726];
  case Haydn::X2HADD32S_L:
    return &Table[727];
  case Haydn::X2HADD32_H:
    return &Table[728];
  case Haydn::X2HADD32_L:
    return &Table[729];
  case Haydn::X2HMAX32:
    return &Table[730];
  case Haydn::X2HMIN32:
    return &Table[731];
  case Haydn::X2MAX32:
    return &Table[732];
  case Haydn::X2MIN32:
    return &Table[733];
  case Haydn::X2MJSWAP32:
    return &Table[734];
  case Haydn::X2MJSWAP32S:
    return &Table[735];
  case Haydn::X2MOVF32:
    return &Table[736];
  case Haydn::X2MOVT32:
    return &Table[737];
  case Haydn::X2MUL32:
    return &Table[738];
  case Haydn::X2MUL32X16_H:
    return &Table[739];
  case Haydn::X2MUL32X16_L:
    return &Table[740];
  case Haydn::X2MULA32:
    return &Table[741];
  case Haydn::X2MULA32X16_H:
    return &Table[742];
  case Haydn::X2MULA32X16_L:
    return &Table[743];
  case Haydn::X2MULAPH32:
    return &Table[744];
  case Haydn::X2MULAPL32:
    return &Table[745];
  case Haydn::X2MULPH32:
    return &Table[746];
  case Haydn::X2MULPL32:
    return &Table[747];
  case Haydn::X2MULS32:
    return &Table[748];
  case Haydn::X2MULS32X16_H:
    return &Table[749];
  case Haydn::X2MULS32X16_L:
    return &Table[750];
  case Haydn::X2MULSPH32:
    return &Table[751];
  case Haydn::X2MULSPL32:
    return &Table[752];
  case Haydn::X2NEG32:
    return &Table[753];
  case Haydn::X2NEG32S:
    return &Table[754];
  case Haydn::X2NEG32S_L:
    return &Table[755];
  case Haydn::X2NEG32_L:
    return &Table[756];
  case Haydn::X2SEL32_HH:
    return &Table[757];
  case Haydn::X2SEL32_HL:
    return &Table[758];
  case Haydn::X2SEL32_LH:
    return &Table[759];
  case Haydn::X2SEL32_LL:
    return &Table[760];
  case Haydn::X2SEQ32:
    return &Table[761];
  case Haydn::X2SLE32:
    return &Table[762];
  case Haydn::X2SLL32:
    return &Table[763];
  case Haydn::X2SLLI32:
    return &Table[764];
  case Haydn::X2SLT32:
    return &Table[765];
  case Haydn::X2SRA32:
    return &Table[766];
  case Haydn::X2SRA32R:
    return &Table[767];
  case Haydn::X2SRAI32:
    return &Table[768];
  case Haydn::X2SRAI32R:
    return &Table[769];
  case Haydn::X2SRL32:
    return &Table[770];
  case Haydn::X2SRLI32:
    return &Table[771];
  case Haydn::X2SUB32:
    return &Table[772];
  case Haydn::X2SUB32S:
    return &Table[773];
  case Haydn::X2SUB32S_HLLH:
    return &Table[774];
  case Haydn::X2SUB32_HLLH:
    return &Table[775];
  case Haydn::X2SUBADD32:
    return &Table[776];
  case Haydn::X2SUBADD32S:
    return &Table[777];
  case Haydn::X2SUBADD32S_HLLH:
    return &Table[778];
  case Haydn::X2SUBADD32_HLLH:
    return &Table[779];
  case Haydn::X2SWAP32:
    return &Table[780];
  case Haydn::X4ABS16:
    return &Table[781];
  case Haydn::X4ABS16S:
    return &Table[782];
  case Haydn::X4ADD16:
    return &Table[783];
  case Haydn::X4ADD16S:
    return &Table[784];
  case Haydn::X4CJMUL16S_H:
    return &Table[785];
  case Haydn::X4CJMUL16S_L:
    return &Table[786];
  case Haydn::X4CJMULA16S_H:
    return &Table[787];
  case Haydn::X4CJMULA16S_L:
    return &Table[788];
  case Haydn::X4CLAMP16:
    return &Table[789];
  case Haydn::X4CMUL16:
    return &Table[790];
  case Haydn::X4CMUL16S:
    return &Table[791];
  case Haydn::X4CMUL16S_F2:
    return &Table[792];
  case Haydn::X4CMUL16S_H:
    return &Table[793];
  case Haydn::X4CMUL16S_L:
    return &Table[794];
  case Haydn::X4CMUL16_F2:
    return &Table[795];
  case Haydn::X4CMULA16S_H:
    return &Table[796];
  case Haydn::X4CMULA16S_L:
    return &Table[797];
  case Haydn::X4CONJ16:
    return &Table[798];
  case Haydn::X4CONJ16S:
    return &Table[799];
  case Haydn::X4DOT16:
    return &Table[800];
  case Haydn::X4ENERGY16:
    return &Table[801];
  case Haydn::X4FCMUL16RS:
    return &Table[802];
  case Haydn::X4FCMUL16RSS:
    return &Table[803];
  case Haydn::X4FCMULA16RS:
    return &Table[804];
  case Haydn::X4FCMULA16RSS:
    return &Table[805];
  case Haydn::X4FF2MUL16S:
    return &Table[806];
  case Haydn::X4FF2MULA16S:
    return &Table[807];
  case Haydn::X4FF2MULS16S:
    return &Table[808];
  case Haydn::X4FMUL16RS:
    return &Table[809];
  case Haydn::X4FMUL16RSS:
    return &Table[810];
  case Haydn::X4FMUL16TS:
    return &Table[811];
  case Haydn::X4FRSST16:
    return &Table[812];
  case Haydn::X4FRST16:
    return &Table[813];
  case Haydn::X4HADD16_H:
    return &Table[814];
  case Haydn::X4HADD16_L:
    return &Table[815];
  case Haydn::X4HMAX16:
    return &Table[816];
  case Haydn::X4HMIN16:
    return &Table[817];
  case Haydn::X4MAX16:
    return &Table[818];
  case Haydn::X4MIN16:
    return &Table[819];
  case Haydn::X4MJSWAP16:
    return &Table[820];
  case Haydn::X4MJSWAP16S:
    return &Table[821];
  case Haydn::X4MOVF16:
    return &Table[822];
  case Haydn::X4MOVT16:
    return &Table[823];
  case Haydn::X4MUL16:
    return &Table[824];
  case Haydn::X4MULA16:
    return &Table[825];
  case Haydn::X4MULA16S:
    return &Table[826];
  case Haydn::X4MULS16:
    return &Table[827];
  case Haydn::X4MULS16S:
    return &Table[828];
  case Haydn::X4NEG16:
    return &Table[829];
  case Haydn::X4NEG16S:
    return &Table[830];
  case Haydn::X4SAT32T16:
    return &Table[831];
  case Haydn::X4SEL16:
    return &Table[832];
  case Haydn::X4SELI16:
    return &Table[833];
  case Haydn::X4SEQ16:
    return &Table[834];
  case Haydn::X4SLE16:
    return &Table[835];
  case Haydn::X4SLL16:
    return &Table[836];
  case Haydn::X4SLLI16:
    return &Table[837];
  case Haydn::X4SLT16:
    return &Table[838];
  case Haydn::X4SRA16:
    return &Table[839];
  case Haydn::X4SRA16R:
    return &Table[840];
  case Haydn::X4SRAI16:
    return &Table[841];
  case Haydn::X4SRAI16R:
    return &Table[842];
  case Haydn::X4SRL16:
    return &Table[843];
  case Haydn::X4SRLI16:
    return &Table[844];
  case Haydn::X4SUB16:
    return &Table[845];
  case Haydn::X4SUB16S:
    return &Table[846];
  case Haydn::X4SWAP16:
    return &Table[847];
  case Haydn::XOR32:
    return &Table[848];
  case Haydn::XOR64:
    return &Table[849];
  case Haydn::XORI32:
    return &Table[850];
  case Haydn::ZERO_DR:
    return &Table[851];
  case Haydn::ZERO_GPR:
    return &Table[852];
  case Haydn::ZERO_SFR:
    return &Table[853];
  }
}

// Compiler-reachable logicals WITHOUT a golden row (fail-closed
// census; never synthesized). CompleteModel=0 evidence.
static constexpr const char *HaydnPerOpUncoveredNames[] = {
    "NOP",
    "WFI",
    "WFITBDTBDTBD",
};
static constexpr unsigned HaydnPerOpUncoveredCount =
    sizeof(HaydnPerOpUncoveredNames) /
    sizeof(HaydnPerOpUncoveredNames[0]);

#endif // GET_HAYDN_PER_OP_RESOURCES
