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
// Pooled per-cycle budgets across the seven Format E execution units
// (constraints §Registers / port table):
// * GPR32 — 4 read / 2 write (compiler view; 5R3W physical with AGU bypass).
// * DR64 — 7 read / 3 write.
// * AR — 2 read / 2 write.
// * SFR — 2 read / 1 write (and at most one SFR writer per bundle).
//
// Single port authority for post-RA packing AND pre-RA HR.
// HaydnHazardRecognizer builds HaydnFuncUnitWrapper port counts from these
// helpers. AIE-style exclusive named-port FuncUnits in the itinerary are the
// wrong model for Haydn's shared pools (see HaydnSchedule.td).
//
// Pre-RA (vreg) path: physreg class membership alone treats every virtual
// register as port-free. Classification uses MachineRegisterInfo regclass
// facts (BankRC.hasSubClassEq(RC)) so GPR32/DR64/AR vregs charge the same
// pooled demand as equivalent physregs. Repeated operands and tied use/defs
// dedupe by Register identity under the same architectural rule. Unknown
// regs (no class yet) stay existential / uncounted.
//
// SMS-RESMII: the pure *format* ResMII oracle lives in
// HaydnBundleFormatSolver (computeExhaustiveProductResMII) and is exposed on
// HaydnPreRASchedStrategy. MI-aware DFA ResMII additionally charges these
// pooled ports via count*Ports / ResourceCycle (sibling SMS track). Pre-RA HR
// uses the same PortModel so vreg demand never undercounts vs SMS MI packing.
//
// Port lower-bound ResMII (haydnPortLowerBoundResMII): ceil of total bank
// demand over per-cycle budgets — independent of format packing. Three
// independent GPR writes force ≥2 cycles under HAYDN_GPR_WRITE_PORTS=2 even
// when Full has three slots (format-only ResMII can still report 1).
// Soft-exit QoR (VF3): PreRASchedStrategy::productSoftExitIIFloor takes
// max(format exhaustive ResMII, this port floor) so II floors cannot drop
// below either bound. RecMII is not computed here (DDG/SMS sibling).
//
// SMS-HANDOFF / packability: PortModel is metrics and feasibility only on the
// pre-RA path. It never invents hard BUNDLE membership. Qualification kernels
// that stay under bank budgets remain candidates for post-RA exact no-split
// pack; the durable clone→BUNDLE handoff is a separate SMS-gate (sibling).
//
// Ports are issue-cycle capacity (pooled demand per issue). They do not model
// multi-cycle FU occupancy wrapping under SMS II. Anonymous cross-cycle
// capacity / II-wrap false-accept is SMS-HOOK catalog + PreRASchedStrategy
// fail-closed polarity (product stages cycles==1); PortModel stays issue-only.
//
// Format-acceptance differential (plan §8.4 #7) is orthogonal to ports:
// descriptor-derived slot legality is pure exactTryAddProduct (opcode-keyed),
// shared by ResourceCycle and post-RA / pre-RA HR. MI-versus-descriptor
// *port* demand (MOVE32 repeated-src 1R1W vs desc 2R1W) must not be read as a
// format accept/reject gap — format polarity agrees for equal opcodes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPORTMODEL_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPORTMODEL_H

#include "HaydnRegisterInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCRegister.h"
#include <algorithm>
#include <cassert>

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

// AR (aligned-register) port budget constants — 2R2W shared across units
// (VLIW_Engine_Compiler_Constraints.md §Registers).
inline constexpr unsigned HAYDN_AR_READ_PORTS = 2;
inline constexpr unsigned HAYDN_AR_WRITE_PORTS = 2;

// SFR port budget constants — 2R1W shared across units. Constraints also
// require at most one SFR writer per bundle (write budget 1 encodes that).
inline constexpr unsigned HAYDN_SFR_READ_PORTS = 2;
inline constexpr unsigned HAYDN_SFR_WRITE_PORTS = 1;

/// Ceiling division (N/D), 0 when N==0. D must be > 0.
inline unsigned haydnCeilDivPorts(unsigned N, unsigned D) {
  assert(D > 0 && "port budget divisor");
  if (N == 0)
    return 0;
  return (N + D - 1) / D;
}

/// Pure port-pressure lower bound on issue cycles for a loop body (or any
/// multiset of operand traffic). Max of ceil(total_bank_demand / budget) over
/// GPR/DR/AR/SFR read and write pools. Zero demand → 0. Nonzero demand → ≥ 1.
///
/// This is *not* format packing: unit geometry may place three ops in one
/// cycle, but three independent GPR writes still need ≥2 cycles under 2W.
/// Pre-RA HR and SMS MI ResMII must both respect this floor (same PortModel).
inline unsigned haydnPortLowerBoundResMII(unsigned GPRReads, unsigned GPRWrites,
                                         unsigned DRReads = 0,
                                         unsigned DRWrites = 0,
                                         unsigned ARReads = 0,
                                         unsigned ARWrites = 0,
                                         unsigned SFRReads = 0,
                                         unsigned SFRWrites = 0) {
  if (GPRReads == 0 && GPRWrites == 0 && DRReads == 0 && DRWrites == 0 &&
      ARReads == 0 && ARWrites == 0 && SFRReads == 0 && SFRWrites == 0)
    return 0;
  unsigned Cycles = 1;
  Cycles = std::max(Cycles, haydnCeilDivPorts(GPRReads, HAYDN_GPR_READ_PORTS));
  Cycles = std::max(Cycles, haydnCeilDivPorts(GPRWrites, HAYDN_GPR_WRITE_PORTS));
  Cycles = std::max(Cycles, haydnCeilDivPorts(DRReads, HAYDN_DR_READ_PORTS));
  Cycles = std::max(Cycles, haydnCeilDivPorts(DRWrites, HAYDN_DR_WRITE_PORTS));
  Cycles = std::max(Cycles, haydnCeilDivPorts(ARReads, HAYDN_AR_READ_PORTS));
  Cycles = std::max(Cycles, haydnCeilDivPorts(ARWrites, HAYDN_AR_WRITE_PORTS));
  Cycles = std::max(Cycles, haydnCeilDivPorts(SFRReads, HAYDN_SFR_READ_PORTS));
  Cycles = std::max(Cycles, haydnCeilDivPorts(SFRWrites, HAYDN_SFR_WRITE_PORTS));
  return Cycles;
}

/// True iff \p Reg is classified into port bank \p BankRC.
/// * Physical: BankRC.contains(Reg).
/// * Virtual: MRI regclass is BankRC or a subclass (e.g. GPR32Lo ⊂ GPR32).
/// * No MRI / no class: false (existential — do not invent demand).
inline bool isHaydnPortBankReg(Register Reg, const TargetRegisterClass &BankRC,
                               const MachineRegisterInfo *MRI) {
  if (!Reg)
    return false;
  if (Reg.isPhysical())
    return BankRC.contains(Reg);
  if (!MRI)
    return false;
  const TargetRegisterClass *RC = MRI->getRegClassOrNull(Reg);
  if (!RC)
    return false;
  // hasSubClassEq: RC is BankRC or a subclass (GPR32Lo / GPR32NoSPNoLR).
  return BankRC.hasSubClassEq(RC);
}

inline bool isHaydnGPRPortReg(Register Reg, const MachineRegisterInfo *MRI) {
  // DR64 is a separate file; never charge it as GPR even if misclassified.
  if (isHaydnPortBankReg(Reg, Haydn::DR64RegClass, MRI))
    return false;
  if (isHaydnPortBankReg(Reg, Haydn::ARRegClass, MRI))
    return false;
  return isHaydnPortBankReg(Reg, Haydn::GPR32RegClass, MRI);
}

inline bool isHaydnDRPortReg(Register Reg, const MachineRegisterInfo *MRI) {
  return isHaydnPortBankReg(Reg, Haydn::DR64RegClass, MRI);
}

inline bool isHaydnARPortReg(Register Reg, const MachineRegisterInfo *MRI) {
  return isHaydnPortBankReg(Reg, Haydn::ARRegClass, MRI);
}

inline bool isHaydnSFRPortReg(Register Reg) {
  return Reg.isPhysical() && Reg == Haydn::SFR;
}

inline const MachineRegisterInfo *
haydnPortMRI(const MachineInstr &MI, const MachineRegisterInfo *MRI) {
  if (MRI)
    return MRI;
  if (const MachineFunction *MF = MI.getMF())
    return &MF->getRegInfo();
  return nullptr;
}

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
// modeled in the .td with two source operands (`$rs1`, `$rs2`) for the
// R-type encoding, but `copyPhysReg` passes the same SrcReg twice and the
// hardware move reads a single register (1R/1W, RI-like).
// Without dedup the packetizer would reject a 2-issue packet of two moves
// as needing 4 read ports when only 2 are physically consumed. Dedup key is
// Register identity (works for both physregs and vregs).
//
// MI-versus-descriptor differential (MOVE32-class): this MI path is exact —
// `MOVE32 rd, rs, rs` → 1R1W after same-reg dedup. Descriptor-only estimates
// (SMS placement via MCInstrDesc, no operand identity) always see the 1-def +
// 2-use shape → 2R1W and overcount. Pre-RA HR / list-sched use only this MI
// PortModel path (CreateTargetMIHazardRecognizer IsPreRA). The overcount is
// intentional conservative placement on the SMS MID path, not a PortModel bug
// and not an operand-dependent format predicate.
// 4. **No R0 exemption.** R0 is soft-zero, not hardwired (HaydnRegisterInfo):
// silicon does not force R0==0 and does not discard R0 traffic. A MatInt
// ADDI rd,R0,imm still reads the R0 file port; XOR32 R0,R0,R0 restore and
// R0-borrow loads occupy real write ports. Dual R0 defs in one bundle are
// WRITE_CONFLICT (HaydnHazardRecognizer WAW). Counting R0 is required for
// correct 4R2W budgeting — the old "R0 is free" assumption was wrong.
// 5. **Vregs:** classified via MRI regclass → GPR bank (see
// isHaydnGPRPortReg). Same 4R2W budget as physregs; no undercount.
// \p MRI optional; defaults to MI's MachineFunction MRI when present.
// \returns {Reads, Writes} GPR32 port demand for the instruction.
inline std::pair<unsigned, unsigned>
countGPRPorts(const MachineInstr &MI,
              const MachineRegisterInfo *MRI = nullptr) {
  MRI = haydnPortMRI(MI, MRI);
  unsigned Reads = 0, Writes = 0;
  // Track registers seen as reads/writes on this instruction so duplicate
  // operands (e.g. MOVE32 rd, rs, rs) count once per port type. Key is the
  // Register unit (phys or virt) — tied use/def of the same reg still charge
  // independently because they go to separate Seen sets.
  //
  // Skip implicit operands: call ABI clobbers (JAL_W/JALR_W regmask +
  // implicit-def of every CSR) are not same-cycle RF port traffic. Counting
  // them as writes made a lone JAL_W exceed 2W and assert in
  // ResourceManager::calculateResMIIDFA (pr28982a/b SMS ResMII). Hardware
  // ports only see explicit data-path operands; tied use/def are explicit.
  SmallSet<unsigned, 6> SeenReads;
  SmallSet<unsigned, 6> SeenWrites;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || MO.isImplicit())
      continue;
    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;
    if (!isHaydnGPRPortReg(Reg, MRI))
      continue;

    const bool IsUse = MO.isUse() && !MO.isUndef();
    const bool IsDef = MO.isDef();
    // Count reads and writes independently — a read-write operand consumes
    // one port of each type (not one or the other). SmallSet::insert returns
    // pair<iterator, bool>; the bool is true iff the element was newly added.
    if (IsUse && SeenReads.insert(Reg.id()).second)
      ++Reads;
    if (IsDef && SeenWrites.insert(Reg.id()).second)
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
// Vregs classified via MRI regclass → DR64 bank.
// \returns {Reads, Writes} DR64 port demand for the instruction.
inline std::pair<unsigned, unsigned>
countDRPorts(const MachineInstr &MI,
             const MachineRegisterInfo *MRI = nullptr) {
  MRI = haydnPortMRI(MI, MRI);
  unsigned Reads = 0, Writes = 0;
  SmallSet<unsigned, 6> SeenReads;
  SmallSet<unsigned, 6> SeenWrites;
  for (const MachineOperand &MO : MI.operands()) {
    // Same as countGPRPorts: skip ABI/implicit clobbers (not RF port traffic).
    if (!MO.isReg() || MO.isImplicit())
      continue;
    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;
    if (!isHaydnDRPortReg(Reg, MRI))
      continue;

    const bool IsUse = MO.isUse() && !MO.isUndef();
    const bool IsDef = MO.isDef();
    if (IsUse && SeenReads.insert(Reg.id()).second)
      ++Reads;
    if (IsDef && SeenWrites.insert(Reg.id()).second)
      ++Writes;
  }
  return {Reads, Writes};
}

// Count AR (aligned-register) read and write port usage for an instruction.
// Spec budget: 2R2W, shared across units. Accounting mirrors countDRPorts
// (read/write counted independently, dead defs still occupy a write port,
// duplicate sources count once per port type). Vregs via MRI → AR bank.
// \returns {Reads, Writes} AR port demand for the instruction.
inline std::pair<unsigned, unsigned>
countARPorts(const MachineInstr &MI,
             const MachineRegisterInfo *MRI = nullptr) {
  MRI = haydnPortMRI(MI, MRI);
  unsigned Reads = 0, Writes = 0;
  SmallSet<unsigned, 6> SeenReads;
  SmallSet<unsigned, 6> SeenWrites;
  for (const MachineOperand &MO : MI.operands()) {
    // Same as countGPRPorts: skip ABI/implicit clobbers (not RF port traffic).
    if (!MO.isReg() || MO.isImplicit())
      continue;
    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;
    if (!isHaydnARPortReg(Reg, MRI))
      continue;

    const bool IsUse = MO.isUse() && !MO.isUndef();
    const bool IsDef = MO.isDef();
    if (IsUse && SeenReads.insert(Reg.id()).second)
      ++Reads;
    if (IsDef && SeenWrites.insert(Reg.id()).second)
      ++Writes;
  }
  return {Reads, Writes};
}

// Count SFR read and write port usage for an instruction.
// Spec budget vocabulary is 2R1W, but product packing law treats dual *dead*
// implicit-def $sfr as legal (slot-ordered flag side-effects on ALU ops).
// Charge only live SFR uses/defs so ordinary flag-writing ALUs still co-issue.
// Live SFR traffic (true flag consumers/writers) remains port-visible.
// \returns {Reads, Writes}.
inline std::pair<unsigned, unsigned>
countSFRPorts(const MachineInstr &MI,
              const MachineRegisterInfo *MRI = nullptr) {
  (void)MRI;
  unsigned Reads = 0, Writes = 0;
  SmallSet<unsigned, 2> SeenReads;
  SmallSet<unsigned, 2> SeenWrites;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (!isHaydnSFRPortReg(Reg))
      continue;
    // Dead flag side-effects do not reserve the exclusive SFR write port
    // under current product law (see HaydnPackLegality rule 3).
    if (MO.isDead())
      continue;
    const bool IsUse = MO.isUse() && !MO.isUndef();
    const bool IsDef = MO.isDef();
    if (IsUse && SeenReads.insert(Reg.id()).second)
      ++Reads;
    if (IsDef && SeenWrites.insert(Reg.id()).second)
      ++Writes;
  }
  return {Reads, Writes};
}

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPORTMODEL_H
