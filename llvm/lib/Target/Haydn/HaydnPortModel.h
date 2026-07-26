//===-- HaydnPortModel.h - Haydn GPR port accounting ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Shared register-file port accounting for the Haydn VLIW architecture.
//
// Two register files, each with its own per-cycle port budget shared across
// all three slots (CLAUDE.md "Slot architecture"; spec port-budget table):
// * GPR32 — 4 read / 2 write (compiler view; 5R3W physical with AGU bypass).
// Caps scalar ALU parallelism at dual-issue (2 × 2R1W = 4R2W).
// * DR64 — 7 read / 3 write per cycle. This is the spec's hard cap — an
// RTL SVA assertion (`bundle_dr_read_o <= 7`, `bundle_dr_write_o <= 3`
// formal_verification_decoder_sva.json), called there "the single most
// impactful physical constraint on the DR array". The 3W ceiling binds
// first once fused-MAC / dual-write DR ops land (the spec's fused-MAC
// family is under-described today — see).
//
// Under the CURRENT slot model DR64 ops are slot-restricted to slots 1/2, so
// at most 2 land in one bundle and the max legal DR demand is 6R/2W < 7R/3W
// (§Finding 1 correction). Modeling DR ports is therefore forward-compat
// spec-alignment today, but it is correct-by-construction the moment a
// fused-MAC op (4+ DR reads) or a dual-write DR op arrives — and it matches
// the RTL contract regardless.
//
// G-MC-6: single port authority for post-RA packing. HaydnHazardRecognizer
// builds HaydnFuncUnitWrapper port counts from these helpers. AIE-style
// exclusive named-port FuncUnits in the itinerary are the wrong model for
// Haydn's shared 4R2W/7R3W pools (see HaydnSchedule.td). Packetizer retired.
// Extracted from HaydnVLIWPacketizer.cpp in.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPORTMODEL_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPORTMODEL_H

#include "HaydnRegisterInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/MC/MCRegister.h"

namespace llvm {

// GPR port budget constants (CLAUDE.md "Slot architecture"; compiler view
// 5R3W physical incl. AGU bypass).
inline constexpr unsigned HAYDN_GPR_READ_PORTS = 4;
inline constexpr unsigned HAYDN_GPR_WRITE_PORTS = 2;

// DR64 port budget constants — spec hard cap, enforced as an RTL SVA
// assertion (formal_verification_decoder_sva.json a_dr_read_budget<=7
// a_dr_write_budget<=3; port_budget_analysis.py:119-122). Shared across all
// three slots. The 3W ceiling is the binding constraint once fused-MAC
// dual-write DR ops arrive.
inline constexpr unsigned HAYDN_DR_READ_PORTS = 7;
inline constexpr unsigned HAYDN_DR_WRITE_PORTS = 3;

// AR (aligned-register) port budget constants — spec hard cap, 2R2W shared
// across all three slots (VLIW_Engine_Compiler_Constraints.md §Registers).
// Forward-compat: no Haydn instruction currently lists an AR operand
// (circular-buffer ops take an immediate cbr_sel + a GPR base pointer), so AR
// port demand is always 0 today. The cap is correct-by-construction the moment
// an AR-operand op lands, and it matches the RTL contract regardless.
inline constexpr unsigned HAYDN_AR_READ_PORTS = 2;
inline constexpr unsigned HAYDN_AR_WRITE_PORTS = 2;

// Count GPR32 read and write port usage for an instruction.
// DR64 accesses use a separate register file with own ports — not counted.
// Per-instruction accounting rules:
// 1. **Reads and writes are counted independently.** A single operand that is
// both a use and a def (read-write, e.g. ADD32 rd, rd, rs) consumes one
// read port AND one write port — not one or the other. The previous
// `else if` form undercounted these by treating them as reads-only.
// 2. **All physical writes count, including dead defs.** A dead def still
// occupies a write port for the cycle (the register file port is reserved
// before liveness is considered). The previous `!MO.isDead` filter
// undercounted writes.
// 3. **Duplicate source registers on one instruction count once.** MOVE32 is
// modeled in the.td with two source operands (`$rs1`, `$rs2`) for the
// R-type encoding, but `copyPhysReg` passes the same SrcReg twice and the
// hardware move reads a single register (1R/1W, RI-like — see).
// Without dedup the packetizer would reject a 2-issue packet of two moves
// as needing 4 read ports when only 2 are physically consumed.
// 4. **No R0 exemption.** R0 is soft-zero, not hardwired (HaydnRegisterInfo):
// silicon does not force R0==0 and does not discard R0 traffic. A MatInt
// ADDI rd,R0,imm still reads the R0 file port; XOR32 R0,R0,R0 restore and
// R0-borrow loads occupy real write ports. Dual R0 defs in one bundle are
// WRITE_CONFLICT (HaydnHazardRecognizer WAW). Counting R0 is required for
// correct 4R2W budgeting — the old "R0 is free" assumption was wrong.
// \returns {Reads, Writes} GPR32 port demand for the instruction.
inline std::pair<unsigned, unsigned> countGPRPorts(const MachineInstr &MI) {
  unsigned Reads = 0, Writes = 0;
  // Track physical GPR32 registers seen as reads/writes on this instruction
  // so duplicate operands (e.g. MOVE32 rd, rs, rs) count once per port type.
  SmallSet<unsigned, 6> SeenReads;
  SmallSet<unsigned, 6> SeenWrites;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;
    // DR64 uses separate register file ports — counted by countDRPorts.
    if (Haydn::DR64RegClass.contains(Reg))
      continue;
    // Only count GPR32 registers (includes soft-zero R0).
    if (!Haydn::GPR32RegClass.contains(Reg))
      continue;

    const bool IsUse = MO.isUse() && !MO.isUndef();
    const bool IsDef = MO.isDef();
    // Count reads and writes independently — a read-write operand consumes
    // one port of each type (not one or the other). SmallSet::insert returns
    // pair<iterator, bool>; the bool is true iff the element was newly added.
    if (IsUse && SeenReads.insert(Reg).second)
      ++Reads;
    if (IsDef && SeenWrites.insert(Reg).second)
      ++Writes;
  }
  return {Reads, Writes};
}

// Count DR64 read and write port usage for an instruction. DR64 is a separate
// register file from GPR32 with its own 7R3W port budget (see file header).
// Accounting mirrors countGPRPorts (read/write counted independently, dead
// defs still occupy a write port, duplicate sources count once per port
// type) with two differences:
// * Only DR64 registers are counted (GPR32 — including soft-zero R0 — is
// handled by countGPRPorts; neither file exempts a "zero" reg from ports).
// * D0 is a normal allocatable register; materialize zero with xor64 d,d,d.
// Every DR64 operand consumes a real port — nothing to skip.
// Tied accumulator operands (FmtALU64Acc `$rd = $rd_in`, e.g. MULA64) appear
// as one def + one use of the same physical register; with the read/write
// independent rule this correctly charges one read port (the accumulator
// input) and one write port (the result) — matching how the hardware reads
// the accumulator and writes the new value.
// \returns {Reads, Writes} DR64 port demand for the instruction.
inline std::pair<unsigned, unsigned> countDRPorts(const MachineInstr &MI) {
  unsigned Reads = 0, Writes = 0;
  // Track physical DR64 registers seen as reads/writes on this instruction
  // so duplicate operands count once per port type (mirrors countGPRPorts).
  SmallSet<unsigned, 6> SeenReads;
  SmallSet<unsigned, 6> SeenWrites;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;
    // Only count DR64 registers — GPR32 handled by countGPRPorts.
    if (!Haydn::DR64RegClass.contains(Reg))
      continue;

    const bool IsUse = MO.isUse() && !MO.isUndef();
    const bool IsDef = MO.isDef();
    if (IsUse && SeenReads.insert(Reg).second)
      ++Reads;
    if (IsDef && SeenWrites.insert(Reg).second)
      ++Writes;
  }
  return {Reads, Writes};
}

// Count AR (aligned-register) read and write port usage for an instruction.
// Spec budget: 2R2W, shared across all three slots. Accounting mirrors
// countDRPorts (read/write counted independently, dead defs still occupy a
// write port, duplicate sources count once per port type). No Haydn
// instruction currently lists an AR operand, so this returns {0,0} today
// kept for forward-compat / spec-alignment (see HAYDN_AR_*_PORTS doc).
// \returns {Reads, Writes} AR port demand for the instruction.
inline std::pair<unsigned, unsigned> countARPorts(const MachineInstr &MI) {
  unsigned Reads = 0, Writes = 0;
  SmallSet<unsigned, 6> SeenReads;
  SmallSet<unsigned, 6> SeenWrites;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;
    if (!Haydn::ARRegClass.contains(Reg))
      continue;

    const bool IsUse = MO.isUse() && !MO.isUndef();
    const bool IsDef = MO.isDef();
    if (IsUse && SeenReads.insert(Reg).second)
      ++Reads;
    if (IsDef && SeenWrites.insert(Reg).second)
      ++Writes;
  }
  return {Reads, Writes};
}

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPORTMODEL_H
