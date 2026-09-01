//===-- HaydnGenRelocFieldLsb.inc - reloc FieldLsb sites -*- C++ -*-===//
//
// DO NOT EDIT.
// Generator: llvm/lib/Target/Haydn/FormatE/generate_format_e_records.py
// Schema: format-e-records/1
// Family: e96 (id=0)
// Check: python3 llvm/lib/Target/Haydn/FormatE/generate_format_e_records.py --check --family e96
// Umbrella: llvm/lib/Target/Haydn/FormatE/check_generated.sh
// JSON-SHA256: c436793cc8d3295088dda2271e68e5b53074eeb4bce3341d443ebac3e828dcba
// XLSX-SHA256: 2a2b43cb394a16cf89173538f2a673235fdb6e4ed04cb6e4e75e72520cf7ffdb
// Overlay: authored_catalog_overlay.json
// Overlay-SHA256: 276dea569f8761e81bb3d17e041a4f74911d34ba6f30711939a293519ac531fc
//
// Parcel-absolute FieldLsb from generated Format E members.
// RelocKind / scale / ELF rows stay in HaydnRelocLayout.cpp.
//===----------------------------------------------------------------------===//

#ifdef GET_HAYDN_RELOC_FIELD_LSB
#undef GET_HAYDN_RELOC_FIELD_LSB
constexpr FieldLsbSite FieldLsbSites[] = {
    // HI12 / LUI I12
    {RelocKind::HI12, 0, 0, kAnyUnit, 32},
    {RelocKind::HI12, 1, 0, kALU2, 21},
    {RelocKind::HI12, 1, 0, kALU0, 23},
    {RelocKind::HI12, 1, 1, kAnyUnit, 54},
    {RelocKind::HI12, 1, 2, kALU2, 83},
    {RelocKind::HI12, 1, 2, kALU0, 81},
    // LO20 / PC_LO20 — RI20 is E2-only
    {RelocKind::LO20, 0, 0, kAnyUnit, 31},
    {RelocKind::LO20, 0, 1, kAnyUnit, 65},
    {RelocKind::PC_LO20, 0, 0, kAnyUnit, 31},
    {RelocKind::PC_LO20, 0, 1, kAnyUnit, 65},
    // LS_IMM RI6
    {RelocKind::LS_IMM, 0, 0, kAnyUnit, 28},
    {RelocKind::LS_IMM, 0, 1, kAnyUnit, 72},
    {RelocKind::LS_IMM, 1, 0, kAnyUnit, 25},
    {RelocKind::LS_IMM, 1, 1, kAnyUnit, 54},
    {RelocKind::LS_IMM, 1, 2, kAnyUnit, 85},
    // CSR I8
    {RelocKind::CSR_UImm8, 0, 0, kAnyUnit, 32},
    {RelocKind::CSR_UImm8, 1, 0, kALU2, 27},
    {RelocKind::CSR_UImm8, 1, 0, kALU0, 23},
    {RelocKind::CSR_UImm8, 1, 1, kAnyUnit, 54},
    {RelocKind::CSR_UImm8, 1, 2, kAnyUnit, 85},
    // JALR RI12
    {RelocKind::JALRSImm12, 0, 0, kAnyUnit, 32},
    {RelocKind::JALRSImm12, 1, 0, kAnyUnit, 23},
    {RelocKind::JALRSImm12, 1, 1, kAnyUnit, 54},
    // I12 / RI12 cond-branch
    {RelocKind::WIDE_BranchSImm12, 0, 0, kAnyUnit, 32},
    {RelocKind::WIDE_BranchSImm12, 1, 0, kAnyUnit, 23},
    {RelocKind::WIDE_BranchSImm12, 1, 1, kAnyUnit, 54},
    {RelocKind::WIDE_BranchSImm12, 1, 2, kAnyUnit, 81},
    {RelocKind::WIDE_BranchSImm12_RI, 0, 0, kAnyUnit, 32},
    {RelocKind::WIDE_BranchSImm12_RI, 1, 0, kAnyUnit, 23},
    {RelocKind::WIDE_BranchSImm12_RI, 1, 1, kAnyUnit, 54},
    // JAL I20
    {RelocKind::WIDE_CallSImm20, 0, 0, kAnyUnit, 31},
    {RelocKind::WIDE_CallSImm20, 1, 0, kAnyUnit, 17},
    {RelocKind::WIDE_CallSImm20, 1, 1, kAnyUnit, 48},
    // SET_HWLOOP F2 (HWLRIIR); HWLRIII extras below
    {RelocKind::HWLoopOff1, 0, 0, kAnyUnit, 32},
    {RelocKind::HWLoopOff1, 1, 0, kAnyUnit, 18},
    {RelocKind::HWLoopOff1, 1, 1, kAnyUnit, 49},
    {RelocKind::HWLoopOff2, 0, 0, kAnyUnit, 38},
    {RelocKind::HWLoopOff2, 1, 0, kAnyUnit, 24},
    {RelocKind::HWLoopOff2, 1, 1, kAnyUnit, 55},
};

constexpr ExtraLsb ExtraPublishedLsb[] = {
    {RelocKind::HWLoopOff1, 13},
    {RelocKind::HWLoopOff2, 36},
};

// D1.42 hwloop Loc-sniff geometry (generated golden authority).
// Map/type windows are parcel-absolute BitRanges; Off1/Off2 LSBs are
// law-checked against FieldLsbSites (HWLRIIR) / ExtraPublishedLsb
// (E2 e0 HWLRIII) at generation — not a second LSB authority.
constexpr HwLoopSniffSite HwLoopSniffSites[] = {
    {0, 0, 6, 2, 0, 8, 5, 16, 13, 36}, // HWLRIII
    {0, 0, 6, 2, 0, 8, 5, 12, 32, 38}, // HWLRIIR
    {1, 0, 6, 2, 2, 8, 4, 12, 18, 24}, // HWLRIIR
    {1, 1, 37, 2, 2, 39, 4, 12, 49, 55}, // HWLRIIR
};
#endif // GET_HAYDN_RELOC_FIELD_LSB

