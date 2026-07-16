//===-- HaydnPostSelectOptimize.cpp ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// Post-select MI peeps for Haydn (O1+).
//
// **Live product rule:** elideCrossBankRoundTrips only (ST64/LD64 +
// MOV_GPR↔DR64 pack/unpack → paired ST32/LD32; sext-shape fold).
//
// **Disabled / residual:** formMACs / optimizeSIMD (Track A — phantom MAC /
// incomplete defs). Keep source until registered MIR matrix exists; do not
// call from the driver. Slot commit is post-RA FlexMap only.
//===----------------------------------------------------------------------===//

#include "HaydnPostSelectOptimize.h"
#include "Haydn.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/Support/Debug.h"
#include "llvm/InitializePasses.h"

#define DEBUG_TYPE "haydn-postselect-opt"

using namespace llvm;

STATISTIC(NumMACsFormed, "Number of MAC instructions formed");
STATISTIC(NumSIMDMACsFormed, "Number of SIMD MAC instructions formed");
STATISTIC(NumSIMDMSubsFormed, "Number of SIMD multiply-subtract instructions formed");
// NumFracMACsFormed removed : tryFormFracMACQ31 deleted; MACQ31 phantom.
STATISTIC(NumCopiesBypassed, "Number of COPY chains bypassed in MAC fusion");
STATISTIC(NumSIMDHAddsFormed, "Number of SIMD horizontal add instructions formed"); // NOLINT
STATISTIC(NumSIMDDotProductsFormed, "Number of SIMD dot product instructions formed"); // NOLINT
STATISTIC(NumScalarPairsPromoted, "Number of scalar pairs promoted to SIMD");
STATISTIC(NumCrossBankSt64Folds,
          "Number of MOV_GPR_TO_DR64 + ST64 folded to 2xST32");
STATISTIC(NumCrossBankLd64Folds,
          "Number of LD64 + MOV_DR64_TO_GPR folded to 2xLD32"); // NOLINT
STATISTIC(NumSextMovFolds,
          "Number of MOV_GPR_TO_DR64 sext-shape folded to SEXT_GPR32_TO_DR64");
STATISTIC(NumShiftAddMACsFormed, "Number of shift+add to MAC patterns formed"); // NOLINT
STATISTIC(NumLaneStoreFolds,
          "Number of MOVE32_DR + ST32 folded to D_SW_L/H_WITH_IMM"); // NOLINT
STATISTIC(NumDR64ConstCSE,
          "Number of duplicate MOV_GPR_TO_DR64 constants CSE'd"); // NOLINT

char HaydnPostSelectOptimize::ID = 0;

INITIALIZE_PASS(HaydnPostSelectOptimize, DEBUG_TYPE,
                "Haydn Post-Select Peepholes (cross-bank elide)", false, false)

HaydnPostSelectOptimize::HaydnPostSelectOptimize()
    : MachineFunctionPass(ID) {}

// Peel through COPY chains to find the real defining instruction.
// Returns the defining MI after peeling, or nullptr if we hit a physical
// register or an opaque source.
MachineInstr *HaydnPostSelectOptimize::peekThroughCopies(
    Register Reg, MachineRegisterInfo &MRI) const {
  // Safety limit to avoid infinite loops in pathological cases.
  unsigned Depth = 0;
  const unsigned MaxDepth = 6;

  while (Reg.isVirtual() && Depth < MaxDepth) {
    MachineInstr *DefMI = MRI.getVRegDef(Reg);
    if (!DefMI || !DefMI->isCopy())
      return DefMI;

    Register SrcReg = DefMI->getOperand(1).getReg();
    if (!SrcReg.isVirtual()) {
      // Physical register source — we cannot peel further.
      return DefMI;
    }
    Reg = SrcReg;
    ++Depth;
  }
  return Reg.isVirtual() ? MRI.getVRegDef(Reg) : nullptr;
}

// Generic helper: try to fuse an ADD + MUL pattern into a single MAC.
// Looks for: %add = AddOpc %x, %y where one of %x, %y is defined by
// %mul = MulOpc %a, %b
// Replaces with:
// %mac = MacOpc %acc, %a, %b [, %ra] (if HasAccumulator)
// When HasAccumulator is false (e.g. MAC32 has 4 operands including ra)
// we always provide the accumulator operand. When the source MUL already
// has an accumulator operand (like MULQ31), we extract only the multiply
// sources from it.
bool HaydnPostSelectOptimize::tryFuseAddMulToMAC(MachineInstr &AddInst,
                                                  MachineRegisterInfo &MRI,
                                                  const HaydnInstrInfo &TII,
                                                  unsigned AddOpc,
                                                  unsigned MulOpc,
                                                  unsigned MacOpc,
                                                  bool HasAccumulator) {
  if (AddInst.getOpcode() != AddOpc)
    return false;

  Register AddDst = AddInst.getOperand(0).getReg();
  Register AddSrc0 = AddInst.getOperand(1).getReg();
  Register AddSrc1 = AddInst.getOperand(2).getReg();

  // Try each direction: src0 or src1 could be the multiply result.
  MachineInstr *MulMI = nullptr;
  Register AccReg;

  auto tryOperand = [&](Register SrcReg, // NOLINT
                        Register OtherReg) -> MachineInstr * {
    if (!SrcReg.isVirtual())
      return nullptr;
    MachineInstr *DefMI = peekThroughCopies(SrcReg, MRI);
    if (!DefMI || DefMI->getOpcode() != MulOpc)
      return nullptr;
    // Only fuse if the multiply result has a single use (or the COPY
    // chain leading to it has a single use).
    Register MulDefReg = DefMI->getOperand(0).getReg();
    if (!MRI.hasOneNonDBGUse(MulDefReg))
      return nullptr;
    return DefMI;
  };

  MulMI = tryOperand(AddSrc0, AddSrc1);
  if (MulMI) {
    AccReg = AddSrc1;
  } else {
    MulMI = tryOperand(AddSrc1, AddSrc0);
    if (MulMI)
      AccReg = AddSrc0;
  }

  if (!MulMI || !AccReg.isVirtual())
    return false;

  // Bypass COPY on the accumulator input as well.
  MachineInstr *AccDef = peekThroughCopies(AccReg, MRI);
  if (AccDef && AccDef->isCopy()) {
    Register AccSrc = AccDef->getOperand(1).getReg();
    if (AccSrc.isVirtual()) {
      AccReg = AccSrc;
      ++NumCopiesBypassed;
    }
  }

  // Extract multiply source operands.
  // (Path B): X2MUL32 is now a TRUE 2-output op — operand layout is
  // [dst0, dst1, src0, src1] (2 defs). The fold consumes dst0 (the high half
  // that fed the ADD); dst1 (low half) is dropped. Sources are at operands
  // 2 and 3 (shifted by the 2nd def).
  Register MulSrc0 = MulMI->getOperand(2).getReg();
  Register MulSrc1 = MulMI->getOperand(3).getReg();

  // Build the MAC instruction (X2MULA32 — TRUE 2-output accumulate).
  // (Path B): X2MULA32 layout is [dst0, dst1, acc0, acc1, src0, src1]
  // (2 defs + 4 uses, acc0/acc1 tied to dst0/dst1). The ADD provides one
  // accumulator (AccReg -> acc0 -> dst0 = AddDst). The 2nd accum/dest needs a
  // fresh DR64 vreg (the low-half accumulator — the fold has no info on it, so
  // create a new vreg; it is live-but-unused unless a follow-up consumes it).
  Register Dst1 = MRI.createVirtualRegister(&Haydn::DR64RegClass);
  Register Acc1 = MRI.createVirtualRegister(&Haydn::DR64RegClass);
  MachineInstr *MAC =
      BuildMI(*AddInst.getParent(), AddInst, AddInst.getDebugLoc(),
              TII.get(MacOpc), AddDst)
          .addDef(Dst1)                           // dst1 (2nd dest)
          .addReg(AccReg)                         // acc0 (tied to dst0)
          .addReg(Acc1)                           // acc1 (tied to dst1)
          .addReg(MulSrc0)                        // src0
          .addReg(MulSrc1);                       // src1

  LLVM_DEBUG(dbgs() << "Formed MAC: " << *MAC << "\n");
  LLVM_DEBUG(dbgs() << "  Replacing ADD: " << AddInst << "\n");
  LLVM_DEBUG(dbgs() << "  And MUL: " << *MulMI << "\n");

  // Erase old instructions. MUL first since ADD may reference its result.
  MulMI->eraseFromParent();
  AddInst.eraseFromParent();

  ++NumMACsFormed;
  return true;
}

// Try to form a scalar MAC32 from ADD32 + MUL32.
bool HaydnPostSelectOptimize::tryFormMAC(MachineInstr &AddInst,
                                          MachineRegisterInfo &MRI,
                                          const HaydnInstrInfo &TII) {
  return false; // MAC32 not in ISA DB
}

// Try to form a SIMD X2MULA32 from X2ADD32 + X2MUL32 (v2i32).
bool HaydnPostSelectOptimize::tryFormSIMDMAC(MachineInstr &AddInst,
                                              MachineRegisterInfo &MRI,
                                              const HaydnInstrInfo &TII) {
  bool Changed = tryFuseAddMulToMAC(AddInst, MRI, TII,
                                     /*AddOpc=*/Haydn::X2ADD32,
                                     /*MulOpc=*/Haydn::X2MUL32,
                                     /*MacOpc=*/Haydn::X2MULA32,
                                     /*HasAccumulator=*/true);
  if (Changed)
    ++NumSIMDMACsFormed;
  return Changed;
}

// Try to form a SIMD X2MULS32 from X2SUB32 + X2MUL32 (v2i32).
// Pattern: %mul = X2MUL32 %a, %b
// %sub = X2SUB32 %acc, %mul
// => %mac = X2MULS32 %acc, %a, %b
// Note: X2MULS32 has format $rd, $rs1, $rs2, $ra where $ra is the
// accumulator: rtd = rtd - (rs1 * rs2). We only fuse when the SUB's
// subtrahend (src1) is the MUL result and the minuend (src0) is the
// accumulator.
bool HaydnPostSelectOptimize::tryFormSIMDMSub(MachineInstr &SubInst,
                                               MachineRegisterInfo &MRI,
                                               const HaydnInstrInfo &TII) {
  if (SubInst.getOpcode() != Haydn::X2SUB32)
    return false;

  Register SubDst = SubInst.getOperand(0).getReg();
  Register SubSrc0 = SubInst.getOperand(1).getReg();
  Register SubSrc1 = SubInst.getOperand(2).getReg();

  // For SUB: src0 is the minuend (accumulator), src1 is the subtrahend.
  // We need src1 to be the MUL result: acc - (a*b).
  MachineInstr *MulMI = nullptr;
  Register AccReg;

  if (SubSrc1.isVirtual() && SubSrc0.isVirtual()) {
    MachineInstr *DefMI = peekThroughCopies(SubSrc1, MRI);
    if (DefMI && DefMI->getOpcode() == Haydn::X2MUL32) {
      Register MulDefReg = DefMI->getOperand(0).getReg();
      if (MRI.hasOneNonDBGUse(MulDefReg)) {
        MulMI = DefMI;
        AccReg = SubSrc0;
      }
    }
  }

  if (!MulMI)
    return false;

  // Bypass COPY on the accumulator.
  MachineInstr *AccDef = peekThroughCopies(AccReg, MRI);
  if (AccDef && AccDef->isCopy()) {
    Register AccSrc = AccDef->getOperand(1).getReg();
    if (AccSrc.isVirtual()) {
      AccReg = AccSrc;
      ++NumCopiesBypassed;
    }
  }

  // Extract multiply sources from X2MUL32.
  // (Path B): X2MUL32 is 2-def — operands [dst0, dst1, src0, src1].
  Register MulSrc0 = MulMI->getOperand(2).getReg();
  Register MulSrc1 = MulMI->getOperand(3).getReg();

  // Build X2MULS32 (TRUE 2-output accumulate). (Path B): layout is
  // [dst0, dst1, acc0, acc1, src0, src1]. The SUB provides one accumulator
  // (AccReg -> acc0 -> dst0 = SubDst); the 2nd accum/dest needs a fresh vreg.
  Register Dst1 = MRI.createVirtualRegister(&Haydn::DR64RegClass);
  Register Acc1 = MRI.createVirtualRegister(&Haydn::DR64RegClass);
  MachineInstr *MS =
      BuildMI(*SubInst.getParent(), SubInst, SubInst.getDebugLoc(),
              TII.get(Haydn::X2MULS32), SubDst)
          .addDef(Dst1)        // dst1 (2nd dest)
          .addReg(AccReg)      // acc0 (tied to dst0)
          .addReg(Acc1)        // acc1 (tied to dst1)
          .addReg(MulSrc0)     // src0
          .addReg(MulSrc1);    // src1

  LLVM_DEBUG(dbgs() << "Formed SIMD MSUB: " << *MS << "\n");
  LLVM_DEBUG(dbgs() << "  Replacing SUB: " << SubInst << "\n");
  LLVM_DEBUG(dbgs() << "  And MUL: " << *MulMI << "\n");

  MulMI->eraseFromParent();
  SubInst.eraseFromParent();

  ++NumSIMDMSubsFormed;
  return true;
}

// tryFormFracMACQ31 REMOVED — MACQ31 was a phantom instruction (not in
// the ISA DB; the real Q-format MAC family is FF2MULA32RS_*). The combine that
// formed MACQ31 from ADD32 + MULQ31 is gone. haydn_macq31 is lowered in the
// selector to ADD32(acc, MULSSH(a,b)). The method declaration is retained in
// the header but never called.

// Try to extract a constant value from a virtual register.
// Handles both G_CONSTANT (pre-selection) and target-specific constant
// materialization patterns like ADDI32 $r0, imm (post-selection).
static std::optional<int64_t> getTargetConstant(Register Reg,
                                                 MachineRegisterInfo &MRI) {
  if (!Reg.isVirtual())
    return std::nullopt;

  // Try the generic helper first (handles G_CONSTANT, COPY chains, etc.).
  auto GenericC = getIConstantVRegValWithLookThrough(Reg, MRI);
  if (GenericC)
    return GenericC->Value.getSExtValue();

  // Fall back to target-specific patterns.
  MachineInstr *DefMI = MRI.getVRegDef(Reg);
  if (!DefMI)
    return std::nullopt;

  // ADDI32 $rd, $r0, imm — constant materialization with soft-zero base.
  if (DefMI->getOpcode() == Haydn::ADDI32 && DefMI->getOperand(1).isReg() &&
      DefMI->getOperand(1).getReg() == Haydn::R0 &&
      DefMI->getOperand(2).isImm()) {
    return DefMI->getOperand(2).getImm();
  }

  return std::nullopt;
}

// Try to extract a constant shift amount from a SLLI32 or SLL32 instruction.
// Returns the shift amount if it can be determined, or std::nullopt otherwise.
static std::optional<uint64_t> getShiftAmount(const MachineInstr &MI,
                                               MachineRegisterInfo &MRI) {
  if (MI.getOpcode() == Haydn::SLLI32) {
    // SLLI32: rd, rs, uimm5. The shift immediate may be stored as a CImm or a
    // plain Imm depending on how the instr was selected — getCImm asserts on a
    // non-CImm operand, so guard it and fall back to getImm.
    const MachineOperand &Amt = MI.getOperand(2);
    if (Amt.isCImm())
      return Amt.getCImm()->getZExtValue();
    if (Amt.isImm())
      return Amt.getImm();
    return std::nullopt;
  }
  if (MI.getOpcode() == Haydn::SHL32 || MI.getOpcode() == Haydn::SLL32) {
    // SLL32/SHL32: rd, rs1, rs2 — try to get a constant from rs2.
    Register AmtReg = MI.getOperand(2).getReg();
    auto C = getTargetConstant(AmtReg, MRI);
    if (C && *C >= 0 && *C < 32)
      return static_cast<uint64_t>(*C);
    return std::nullopt;
  }
  return std::nullopt;
}

// Try to recognize shift+add patterns that compute a * C (where C is not
// a power of 2) and fuse them with an accumulator add into MAC32.
// This handles two common patterns produced by LLVM's multiply expansion
// or by explicit shift+add code:
// Pattern A (shift + same-value add, then accumulate):
// %shl = SLLI32 %a, k; a << k
// %inner = ADD32 %shl, %a; a*(2^k + 1)
// %result = ADD32 %inner, %acc
// => materialize C = 2^k + 1
// %mac = MAC32 %acc, %a, C
// Pattern B (two shifts of same value, then accumulate):
// %shl0 = SLLI32 %a, k0
// %shl1 = SLLI32 %a, k1
// %inner = ADD32 %shl0, %shl1; a*(2^k0 + 2^k1)
// %result = ADD32 %inner, %acc
// => materialize C = 2^k0 + 2^k1
// %mac = MAC32 %acc, %a, C
// The optimization is profitable because MAC32 is a single instruction that
// replaces the entire shift+add chain. For Pattern A the saving is:
// Before: SLLI32 + ADD32 + ADD32 = 3 instructions
// After: ADDI32 (materialize C) + MAC32 = 2 instructions
// For Pattern B with non-adjacent shift amounts the saving is even larger.
bool HaydnPostSelectOptimize::tryFormShiftAddMAC(MachineInstr &OuterAdd,
                                                  MachineRegisterInfo &MRI,
                                                  const HaydnInstrInfo &TII) {
  return false; // MAC32 not in ISA DB (MULA* targets DR64, not GPR32)
}
// Scan the function for MAC formation opportunities.
bool HaydnPostSelectOptimize::formMACs(MachineFunction &MF) {
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const HaydnInstrInfo &TII = *ST.getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    // Collect candidate instructions first — we modify the block while
    // iterating, so snapshot the initial set.
    SmallVector<MachineInstr *, 16> Candidates;

    for (MachineInstr &MI : MBB) {
      unsigned Opc = MI.getOpcode();
      if (Opc == Haydn::ADD32 || Opc == Haydn::X2ADD32 ||
          Opc == Haydn::X2SUB32)
        Candidates.push_back(&MI);
    }

    for (MachineInstr *MI : Candidates) {
      if (!MI->getParent()) // Already erased by a prior fusion.
        continue;

      // Try each pattern in priority order. First match wins.
      // 1. Scalar MAC32: ADD32 + MUL32
      if (tryFormMAC(*MI, MRI, TII)) {
        Changed = true;
        continue;
      }

      // 2. SIMD MAC: X2ADD32 + X2MUL32
      if (tryFormSIMDMAC(*MI, MRI, TII)) {
        Changed = true;
        continue;
      }

      // 3. SIMD MSUB: X2SUB32 + X2MUL32
      if (tryFormSIMDMSub(*MI, MRI, TII)) {
        Changed = true;
        continue;
      }

      // Fractional Q1.31 MAC combine REMOVED — MACQ31 was a phantom
      // instruction (not in the ISA DB). haydn_macq31 is lowered in the
      // selector to ADD32(acc, MULSSH(a,b)); no post-select combine needed.

      // 5. Shift+add to MAC: ADD32(ADD32(SLL*, a), acc) -> MAC32
      if (tryFormShiftAddMAC(*MI, MRI, TII)) {
        Changed = true;
        continue;
      }
    }
  }

  return Changed;
}

bool HaydnPostSelectOptimize::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  // formMACs + optimizeSIMD disabled (Track A / Codex residual): SIMD
  // MAC/MSUB fabricates undef Acc1 and drops second X2MUL def; keep off until
  // every def/use/lane is proven with registered -run-pass tests.
  // Changed |= formMACs(MF);
  // Changed |= optimizeSIMD(MF);
  Changed |= elideCrossBankRoundTrips(MF);
  // slot commitment moved OUT of pre-RA. materializeFlexVariants
  // previously rewrote every legacy opcode to `_S0`/`_S1` via
  // FU-derivation here (post-select, pre-RA), which BAKED the slot before the
  // VLIW scheduler could distribute ALU32 ops across S0/S1/S2 — the Bug1 root
  // cause (every ALU32 locked to S0, the post-RA HR auction saw committed flex
  // opcodes with no remaining family, two S0 ops packed -> encoder decline).
  // Single-authority fix: opcodes stay LEGACY through RA + the pre-RA scheduler
  // + the post-RA scheduler; the post-RA HR auction (HaydnHazardRecognizer
  // FlexMap authority) assigns a distinct slot per cycle and records it in
  // AltDescs; materializeMultiOpcodeInstrs (HaydnPostRASchedStrategy::leaveRegion)
  // commits the legacy opcode to its auction-chosen `_S<k>` variant. The
  // slot is decided ONCE, at the VLIW scheduler, by the single FlexMap
  // authority. materializeFlexVariants + resolveFlexSlot below are now dead
  // (retained for reference until Cycle B deletes them).
  return Changed;
}

//===----------------------------------------------------------------------===//
// SIMD Optimization Passes
//===----------------------------------------------------------------------===//

// Try to recognize a scalarized horizontal add and replace with X2HADD32_L.
// The legalizer expands G_VECREDUCE_ADD on v2i32 into:
// %lo, %hi = MOV_DR64_TO_GPR %vec
// %sum = ADD32 %lo, %hi
// We recognize this pattern and replace with:
// %result_dr = X2HADD32_L %vec
// %sum = MOV_DR64_TO_GPR_lo %result_dr
// X2HADD32_L computes: rtd[31:0] = rsd[63:32] + rsd[31:0]
// The result is in the lower 32 bits of the DR64 output.
bool HaydnPostSelectOptimize::tryFormSIMDHAdd(MachineInstr &AddInst,
                                               MachineRegisterInfo &MRI,
                                               const HaydnInstrInfo &TII) {
  if (AddInst.getOpcode() != Haydn::ADD32)
    return false;

  Register AddDst = AddInst.getOperand(0).getReg();
  Register Src0 = AddInst.getOperand(1).getReg();
  Register Src1 = AddInst.getOperand(2).getReg();

  if (!Src0.isVirtual() || !Src1.isVirtual())
    return false;

  // Both sources must be defined by MOV_DR64_TO_GPR.
  MachineInstr *Def0 = peekThroughCopies(Src0, MRI);
  MachineInstr *Def1 = peekThroughCopies(Src1, MRI);
  if (!Def0 || !Def1 || Def0->getOpcode() != Haydn::MOV_DR64_TO_GPR ||
      Def1->getOpcode() != Haydn::MOV_DR64_TO_GPR)
    return false;

  // They must come from the same source DR64 register.
  Register VecReg0 = Def0->getOperand(2).getReg();
  Register VecReg1 = Def1->getOperand(2).getReg();
  if (VecReg0 != VecReg1)
    return false;

  // Src0 must be the lower half (def 0) and Src1 the upper half (def 1)
  // or vice versa — X2HADD32_L sums both halves regardless of order.
  // Verify that Src0 and Src1 correspond to the two different defs.
  Register LoDef = Def0->getOperand(0).getReg();
  Register HiDef = Def0->getOperand(1).getReg();
  bool Src0IsLo = (Src0 == LoDef);
  bool Src0IsHi = (Src0 == HiDef);
  if (!Src0IsLo && !Src0IsHi)
    return false;

  // Check that the MOV_DR64_TO_GPR results are only used here.
  // The other half (not used by ADD) must have no other uses.
  // Allow the unused half to be dead or only used in debug info.
  if (MRI.hasOneNonDBGUse(LoDef) && MRI.hasOneNonDBGUse(HiDef)) {
    // Good — both halves are only used by this ADD.
  } else {
    // At minimum, the two halves used by ADD must be the only non-debug uses.
    if (!MRI.hasOneNonDBGUse(Src0) || !MRI.hasOneNonDBGUse(Src1))
      return false;
  }

  // Build X2HADD32_L: result is in lower 32 bits of DR64.
  const TargetRegisterClass *DR64RC = &Haydn::DR64RegClass;
  Register HAddDR = MRI.createVirtualRegister(DR64RC);

  MachineInstr *HAdd =
      BuildMI(*AddInst.getParent(), AddInst, AddInst.getDebugLoc(),
              TII.get(Haydn::X2HADD32_L), HAddDR)
          .addReg(VecReg0);

  // Now extract the lower 32 bits of the result.
  // Create a new MOV_DR64_TO_GPR to extract the result.
  Register ResultLo = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
  Register ResultHi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);

  BuildMI(*AddInst.getParent(), HAdd, AddInst.getDebugLoc(),
          TII.get(Haydn::MOV_DR64_TO_GPR))
      .addDef(ResultLo)
      .addDef(ResultHi)
      .addReg(HAddDR);

  // Replace all uses of the ADD result with the extracted low half.
  MRI.replaceRegWith(AddDst, ResultLo);

  LLVM_DEBUG(dbgs() << "Formed SIMD horizontal add: " << *HAdd << "\n");
  LLVM_DEBUG(dbgs() << "  Replacing ADD: " << AddInst << "\n");

  // Erase the original ADD and the now-dead MOV_DR64_TO_GPR.
  AddInst.eraseFromParent();
  // The old MOV_DR64_TO_GPR may now be dead — let DCE clean it up.
  // But if it's truly dead (both halves only used by the erased ADD)
  // erase it now to avoid leaving dangling instructions.
  if (MRI.use_nodbg_empty(LoDef) && MRI.use_nodbg_empty(HiDef))
    Def0->eraseFromParent();

  ++NumSIMDHAddsFormed;
  return true;
}

// Try to recognize X2MUL32 followed by X2HADD32_L and replace with X2DOT32.
// Pattern:
// %mul = X2MUL32 %a, %b; element-wise multiply: {a0*b0, a1*b1}
// %hadd = X2HADD32_L %mul; horizontal add: a0*b0 + a1*b1
// => %dot = X2DOT32 %a, %b; dot product: a0*b0 + a1*b1
// X2DOT32 computes the sum of two element-wise products in one instruction.
bool HaydnPostSelectOptimize::tryFormSIMDDotProduct(MachineInstr &HAddInst,
                                                     MachineRegisterInfo &MRI,
                                                     const HaydnInstrInfo &TII) {
  if (HAddInst.getOpcode() != Haydn::X2HADD32_L)
    return false;

  Register HAddDst = HAddInst.getOperand(0).getReg();
  Register HAddSrc = HAddInst.getOperand(1).getReg();

  if (!HAddSrc.isVirtual())
    return false;

  MachineInstr *MulMI = peekThroughCopies(HAddSrc, MRI);
  if (!MulMI || MulMI->getOpcode() != Haydn::X2MUL32)
    return false;

  // The MUL must have a single use (this HADD).
  Register MulDef = MulMI->getOperand(0).getReg();
  if (!MRI.hasOneNonDBGUse(MulDef))
    return false;

  // Extract MUL sources.
  // (Path B): X2MUL32 is 2-def — operands [dst0, dst1, src0, src1].
  Register MulSrc0 = MulMI->getOperand(2).getReg();
  Register MulSrc1 = MulMI->getOperand(3).getReg();

  // Build X2DOT32: result is DR64 (lower 32 bits = dot product).
  BuildMI(*HAddInst.getParent(), HAddInst, HAddInst.getDebugLoc(),
              TII.get(Haydn::X2DOT32), HAddDst)
          .addReg(MulSrc0)
          .addReg(MulSrc1);

  LLVM_DEBUG(dbgs() << "Formed SIMD dot product\n");
  LLVM_DEBUG(dbgs() << "  Replacing HADD: " << HAddInst << "\n");
  LLVM_DEBUG(dbgs() << "  And MUL: " << *MulMI << "\n");

  MulMI->eraseFromParent();
  HAddInst.eraseFromParent();

  ++NumSIMDDotProductsFormed;
  return true;
}

// Try to promote two independent scalar i32 operations whose inputs come
// from the same DR64 register into a single SIMD X2 operation.
// Pattern:
// %lo0, %hi0 = MOV_DR64_TO_GPR %vec_a
// %lo1, %hi1 = MOV_DR64_TO_GPR %vec_b
// %r0 = ADD32 %lo0, %lo1
// %r1 = ADD32 %hi0, %hi1
// %result = MOV_GPR_TO_DR64 %r0, %r1
// => %result = X2ADD32 %vec_a, %vec_b
// Works for ADD32->X2ADD32, SUB32->X2SUB32, MUL32->X2MUL32.
bool HaydnPostSelectOptimize::tryPromoteScalarPairToSIMD(
    MachineInstr &MI, MachineRegisterInfo &MRI, const HaydnInstrInfo &TII) {
  // We scan for MOV_GPR_TO_DR64 that merges two scalar results.
  // Each scalar result should come from the same operation on matching
  // halves of the same two source DR64 registers.
  if (MI.getOpcode() != Haydn::MOV_GPR_TO_DR64)
    return false;

  Register DstDR = MI.getOperand(0).getReg();
  Register LoSrc = MI.getOperand(1).getReg();
  Register HiSrc = MI.getOperand(2).getReg();

  if (!LoSrc.isVirtual() || !HiSrc.isVirtual())
    return false;

  MachineInstr *LoMI = peekThroughCopies(LoSrc, MRI);
  MachineInstr *HiMI = peekThroughCopies(HiSrc, MRI);
  if (!LoMI || !HiMI)
    return false;

  // Both must be the same scalar opcode.
  unsigned ScalarOpc = LoMI->getOpcode();
  if (ScalarOpc != HiMI->getOpcode())
    return false;

  // Determine the SIMD opcode.
  unsigned SimdOpc;
  switch (ScalarOpc) {
  case Haydn::ADD32:
    SimdOpc = Haydn::X2ADD32;
    break;
  case Haydn::SUB32:
    SimdOpc = Haydn::X2SUB32;
    break;
  default:
    return false;
  }

  // Each scalar op should have 3 operands: dst, src0, src1.
  if (LoMI->getNumOperands() < 3 || HiMI->getNumOperands() < 3)
    return false;

  Register LoSrc0 = LoMI->getOperand(1).getReg();
  Register LoSrc1 = LoMI->getOperand(2).getReg();
  Register HiSrc0 = HiMI->getOperand(1).getReg();
  Register HiSrc1 = HiMI->getOperand(2).getReg();

  // Both sources of each scalar op should come from MOV_DR64_TO_GPR.
  auto findSplitSource = [&](Register R0, Register R1,
                             Register &OutVecReg,
                             unsigned &OutLoIdx,
                             unsigned &OutHiIdx) -> bool {
    if (!R0.isVirtual() || !R1.isVirtual())
      return false;
    MachineInstr *D0 = peekThroughCopies(R0, MRI);
    MachineInstr *D1 = peekThroughCopies(R1, MRI);
    if (!D0 || !D1)
      return false;
    if (D0->getOpcode() != Haydn::MOV_DR64_TO_GPR ||
        D1->getOpcode() != Haydn::MOV_DR64_TO_GPR)
      return false;
    // Both must come from the same DR64.
    Register V0 = D0->getOperand(2).getReg();
    Register V1 = D1->getOperand(2).getReg();
    if (V0 != V1)
      return false;
    OutVecReg = V0;
    // Determine which def index each source uses.
    // D0 defines {Lo0, Hi0}; if R0 == Lo0 then OutLoIdx=0, if R0 == Hi0
    // then OutLoIdx=1.
    Register D0Lo = D0->getOperand(0).getReg();
    Register D0Hi = D0->getOperand(1).getReg();
    Register D1Lo = D1->getOperand(0).getReg();
    Register D1Hi = D1->getOperand(1).getReg();
    if (R0 == D0Lo)
      OutLoIdx = 0;
    else if (R0 == D0Hi)
      OutLoIdx = 1;
    else
      return false;
    if (R1 == D1Lo)
      OutHiIdx = 0;
    else if (R1 == D1Hi)
      OutHiIdx = 1;
    else
      return false;
    return true;
  };

  Register VecA, VecB;
  unsigned LoAIdx, LoBIdx, HiAIdx, HiBIdx;

  // LoMI uses (src0_a, src1_a); HiMI uses (src0_b, src1_b).
  // We need src0_a and src0_b to come from the same DR64, and
  // src1_a and src1_b to come from the same DR64.
  // The halves must match: LoMI uses lower halves and HiMI uses upper halves
  // or vice versa — but the critical check is that corresponding lanes match.

  bool Matched = false;

  // Try: LoMI's op0 and HiMI's op0 from same DR64, LoMI's op1 and HiMI's
  // op1 from same DR64.
  if (findSplitSource(LoSrc0, HiSrc0, VecA, LoAIdx, HiAIdx) &&
      findSplitSource(LoSrc1, HiSrc1, VecB, LoBIdx, HiBIdx)) {
    // Both must use matching halves: Lo uses lower of each, Hi uses upper.
    if (LoAIdx == LoBIdx && HiAIdx == HiBIdx && LoAIdx != HiAIdx)
      Matched = true;
  }

  if (!Matched)
    return false;

  // Verify the scalar ops have single uses (so we can erase them).
  Register LoDef = LoMI->getOperand(0).getReg();
  Register HiDef = HiMI->getOperand(0).getReg();
  if (!MRI.hasOneNonDBGUse(LoDef) || !MRI.hasOneNonDBGUse(HiDef))
    return false;

  // Build the SIMD instruction.
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII.get(SimdOpc), DstDR)
          .addReg(VecA)
          .addReg(VecB);

  LLVM_DEBUG(dbgs() << "Promoted scalar pair to SIMD\n");
  LLVM_DEBUG(dbgs() << "  Replacing merge: " << MI << "\n");

  // Erase the old instructions in reverse order.
  MI.eraseFromParent();
  HiMI->eraseFromParent();
  LoMI->eraseFromParent();

  ++NumScalarPairsPromoted;
  return true;
}

// Scan the function for SIMD optimization opportunities.
bool HaydnPostSelectOptimize::optimizeSIMD(MachineFunction &MF) {
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const HaydnInstrInfo &TII = *ST.getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    // Collect candidate instructions.
    SmallVector<MachineInstr *, 16> AddCandidates;
    SmallVector<MachineInstr *, 16> HAddCandidates;
    SmallVector<MachineInstr *, 16> MergeCandidates;

    for (MachineInstr &MI : MBB) {
      unsigned Opc = MI.getOpcode();
      if (Opc == Haydn::ADD32)
        AddCandidates.push_back(&MI);
      if (Opc == Haydn::X2HADD32_L)
        HAddCandidates.push_back(&MI);
      if (Opc == Haydn::MOV_GPR_TO_DR64)
        MergeCandidates.push_back(&MI);
    }

    // Pass 1: Try to form SIMD horizontal add from scalarized reductions.
    for (MachineInstr *MI : AddCandidates) {
      if (!MI->getParent())
        continue;
      if (tryFormSIMDHAdd(*MI, MRI, TII)) {
        Changed = true;
        // The ADD32 was replaced; HAddCandidates don't need updating here
        // because we process them next.
      }
    }

    // Pass 2: Try to form SIMD dot product from X2MUL32 + X2HADD32_L.
    for (MachineInstr *MI : HAddCandidates) {
      if (!MI->getParent())
        continue;
      if (tryFormSIMDDotProduct(*MI, MRI, TII))
        Changed = true;
    }

    // Pass 3: Try to promote scalar pairs to SIMD.
    for (MachineInstr *MI : MergeCandidates) {
      if (!MI->getParent())
        continue;
      if (tryPromoteScalarPairToSIMD(*MI, MRI, TII))
        Changed = true;
    }
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// Cross-bank pack/unpack elision
//===----------------------------------------------------------------------===//
//
// The MOV_GPR_TO_DR64 / MOV_DR64_TO_GPR pseudos expand (post-RA) to a 5-cycle
// serial Slot-0 stack-staged chain (SUBI32 SP; ST32/ST64; LD32/LD64_S1; ADDI32
// SP). When such a pseudo directly feeds (or is fed by) a 64-bit memory access
// the round-trip through DR64 is pure waste: the memory holds the value in the
// same 2xGPR32 layout that the pack/unpack produces.
//
// Two peephole patterns collapse the round-trip:
//
// (A) ST64 fold:
// %dr = MOV_GPR_TO_DR64 %lo, %hi
// ST64 %dr, %base, %off
// => ST32 %lo, %base, %off
// ST32 %hi, %base, %off+4
//
// (B) LD64 fold:
// %dr = LD64_S1 %base, %off
// %lo, %hi = MOV_DR64_TO_GPR %dr
// => LD32 %lo, %base, %off
// LD32 %hi, %base, %off+4
//
// Each fold replaces the 5-instruction pack chain + the 64-bit memory op with a
// pair of 32-bit memory ops, recovering ~4 instructions per call site. In DSP
// kernels (IIR/FIR) that round-trip every sample through DR64, this typically
// removes ~10 instructions/iteration and unblocks VLIW packetization into
// 2-3 slot bundles (the pack chain's serial Slot-0 traffic was saturating the
// slot and preventing independent work from co-issuing).
//
// Safety:
// The MOV pseudo's DR64 result must have exactly one non-debug use (the
// ST64/LD64). Other uses keep the round-trip.
// The base+offset of the ST64/LD64 must allow off+4 within simm16 range.
// The MOV pseudo must be in the same block as the ST64/LD64.
// Alignment: ST64/LD64 may require 8-byte alignment on some hardware; the
// 2xST32/2xLD32 replacement only requires 4-byte alignment. This is safe
// because we only fold when the source/destination is a register pair that
// was about to be packed/unpacked anyway — the underlying memory access
// pattern is identical (8 contiguous bytes), only the access width changes.

bool HaydnPostSelectOptimize::tryFoldMovToSt64(MachineInstr &MovInst,
                                                MachineRegisterInfo &MRI,
                                                const HaydnInstrInfo &TII) {
  // MovInst: %dr = MOV_GPR_TO_DR64 %lo, %hi
  if (MovInst.getOpcode() != Haydn::MOV_GPR_TO_DR64)
    return false;

  Register DrDst = MovInst.getOperand(0).getReg();
  if (!DrDst.isVirtual())
    return false;

  // The DR64 result must have exactly one non-debug use, and that use must be
  // an ST64.
  if (!MRI.hasOneNonDBGUse(DrDst))
    return false;

  MachineInstr *St64 = nullptr;
  for (MachineInstr &UseMI : MRI.use_nodbg_instructions(DrDst)) {
    St64 = &UseMI;
    break;
  }
  if (!St64 || St64->getOpcode() != Haydn::ST64)
    return false;

  // Both must be in the same block.
  if (St64->getParent() != MovInst.getParent())
    return false;

  // ST64 operands: $rt(DR64), $rs(GPR32 base), $offset(simm16).
  Register Lo = MovInst.getOperand(1).getReg();
  Register Hi = MovInst.getOperand(2).getReg();
  Register Base = St64->getOperand(1).getReg();
  int64_t Off = St64->getOperand(2).getImm();

  // Check off+4 stays within simm16 range.
  // simm16 range: -32768..32767.
  if (Off > 32767 - 4 || Off < -32768)
    return false;

  bool LoKill = MovInst.getOperand(1).isKill();
  bool HiKill = MovInst.getOperand(2).isKill();

  // The post-inc offset bail-out that used to live here is no longer
  // needed: ST32_POST_INC / LD32_POST_INC now carry an explicit offset
  // operand, so HaydnLoadStoreOptimizer's post-inc formation on the
  // offset-4 ST32 preserves the displacement through expansion. The fold
  // fires unconditionally — store-fold now works inside loops.

  // Emit two ST32 in St64's position (before it), then erase St64 and MovInst.
  // When Lo == Hi (same physical GPR32 used for both halves), only mark kill
  // on the second store to avoid killing the register twice.
  if (Lo == Hi) {
    BuildMI(*St64->getParent(), St64, St64->getDebugLoc(),
            TII.get(Haydn::ST32))
        .addReg(Lo, getKillRegState(false))
        .addReg(Base)
        .addImm(Off);
    BuildMI(*St64->getParent(), St64, St64->getDebugLoc(),
            TII.get(Haydn::ST32))
        .addReg(Hi, getKillRegState(LoKill || HiKill))
        .addReg(Base)
        .addImm(Off + 4);
  } else {
    BuildMI(*St64->getParent(), St64, St64->getDebugLoc(),
            TII.get(Haydn::ST32))
        .addReg(Lo, getKillRegState(LoKill))
        .addReg(Base)
        .addImm(Off);
    BuildMI(*St64->getParent(), St64, St64->getDebugLoc(),
            TII.get(Haydn::ST32))
        .addReg(Hi, getKillRegState(HiKill))
        .addReg(Base)
        .addImm(Off + 4);
  }

  LLVM_DEBUG(dbgs() << "Folded MOV_GPR_TO_DR64 + ST64 into 2xST32: "
                    << MovInst << "\n");

  ++NumCrossBankSt64Folds;
  St64->eraseFromParent();
  MovInst.eraseFromParent();
  return true;
}

bool HaydnPostSelectOptimize::tryFoldLd64ToMovFrom(MachineInstr &MovInst,
                                                    MachineRegisterInfo &MRI,
                                                    const HaydnInstrInfo &TII) {
  // MovInst: %lo, %hi = MOV_DR64_TO_GPR %dr
  if (MovInst.getOpcode() != Haydn::MOV_DR64_TO_GPR)
    return false;

  Register DrSrc = MovInst.getOperand(2).getReg();
  if (!DrSrc.isVirtual())
    return false;

  // The DR64 source must be defined by logical LD64 (or post-inc).
  MachineInstr *Def = MRI.getVRegDef(DrSrc);
  if (!Def || (Def->getOpcode() != Haydn::LD64 &&
               Def->getOpcode() != Haydn::LD64_POST_INC))
    return false;

  // The DR64 source must have no other non-debug users (we're replacing the
  // load entirely).
  if (!MRI.hasOneNonDBGUse(DrSrc))
    return false;

  // Both must be in the same block.
  if (Def->getParent() != MovInst.getParent())
    return false;

  // For LD64_POST_INC (the post-increment addressing form), the load+update
  // pattern is more complex; skip it for now to keep the fold safe.
  if (Def->getOpcode() == Haydn::LD64_POST_INC)
    return false;

  // LD64 operands: $rt(DR64 def), $rs(GPR32 base), $offset(simm16).
  Register Lo = MovInst.getOperand(0).getReg();
  Register Hi = MovInst.getOperand(1).getReg();
  Register Base = Def->getOperand(1).getReg();
  int64_t Off = Def->getOperand(2).getImm();

  // Check off+4 stays within simm16 range.
  if (Off > 32767 - 4 || Off < -32768)
    return false;

  // The post-inc offset bail-out that used to live here is no longer
  // needed: LD32_POST_INC now carries an explicit offset operand, so
  // post-inc formation on the offset-4 LD32 preserves the displacement.

  bool SrcKill = MovInst.getOperand(2).isKill();

  // Emit two LD32 in MovInst's position (before it).
  BuildMI(*MovInst.getParent(), MovInst, MovInst.getDebugLoc(),
          TII.get(Haydn::LD32))
      .addDef(Lo)
      .addReg(Base)
      .addImm(Off);
  BuildMI(*MovInst.getParent(), MovInst, MovInst.getDebugLoc(),
          TII.get(Haydn::LD32))
      .addDef(Hi)
      .addReg(Base)
      .addImm(Off + 4);

  LLVM_DEBUG(dbgs() << "Folded LD64 + MOV_DR64_TO_GPR into 2xLD32: "
                    << MovInst << "\n");

  ++NumCrossBankLd64Folds;
  // Drop the operand kill flag on the load before erasing, to keep verifier
  // happy if SrcKill was set.
  (void)SrcKill;
  MovInst.eraseFromParent();
  Def->eraseFromParent();
  return true;
}

bool HaydnPostSelectOptimize::tryFoldSextMovToDirect(MachineInstr &MovInst,
                                                     MachineRegisterInfo &MRI,
                                                     const HaydnInstrInfo &TII) {
  // Historically this folded `MOV_GPR_TO_DR64 %dr, %x, %x` → SEXT_GPR32_TO_DR64.
  // That is wrong as a general rewrite:
  // * MOV_GPR_TO_DR64 lo,hi means bit-pack: rd = (hi<<32)|lo (dual-lane
  // G_BUILD_VECTOR splat uses x,x to put the same word in both lanes).
  // * SEXT_GPR32_TO_DR64 means sign-extend: hi = all-ones if x<0.
  // For x = INT_MIN (0x80000000), pack is 0x8000000080000000 but sext is
  // 0xFFFFFFFF80000000 — x2abs32s then fails the INT_MIN sat check
  // (BundleSim intrin_x2simd exit 6).
  // G_SEXT i32→i64 already selects SEXT_GPR32_TO_DR64 directly. Leave
  // MOV_GPR_TO_DR64(x,x) for true dual-lane replicate / pack.
  (void)MovInst;
  (void)MRI;
  (void)TII;
  return false;
}

bool HaydnPostSelectOptimize::tryFoldMove32DrToSw(MachineInstr &MovInst,
                                                   MachineRegisterInfo &MRI,
                                                   const HaydnInstrInfo &TII) {
  // MovInst: %gpr = MOVE32_DR_L/H %dr
  unsigned MovOpc = MovInst.getOpcode();
  if (MovOpc != Haydn::MOVE32_DR_L && MovOpc != Haydn::MOVE32_DR_H)
    return false;

  Register GprDst = MovInst.getOperand(0).getReg();
  if (!GprDst.isVirtual())
    return false;

  // The GPR32 result must have exactly one non-debug use, and that use must
  // be an ST32 (the extracted lane goes straight to memory).
  if (!MRI.hasOneNonDBGUse(GprDst))
    return false;

  MachineInstr *St32 = nullptr;
  for (MachineInstr &UseMI : MRI.use_nodbg_instructions(GprDst)) {
    St32 = &UseMI;
    break;
  }
  if (!St32 || St32->getOpcode() != Haydn::ST32)
    return false;

  // Both must be in the same block.
  if (St32->getParent() != MovInst.getParent())
    return false;

  // ST32 operands: $rt(GPR32 data), $rs(GPR32 base), $offset(simm16).
  Register DrSrc = MovInst.getOperand(1).getReg();
  Register Base = St32->getOperand(1).getReg();
  int64_t Off = St32->getOperand(2).getImm();

  // D_SW_L/H_WITH_IMM offset is imm6<<2 (word-scaled, signed). The byte
  // offset must be a multiple of 4 and the word-index must fit in signed
  // 6-bit range (-32..+31, byte range -128..+124).
  if ((Off % 4) != 0)
    return false;
  int64_t ScaledImm = Off >> 2;
  if (!isInt<6>(ScaledImm))
    return false;

  unsigned SwOpc = (MovOpc == Haydn::MOVE32_DR_L) ? Haydn::D_SW_L_WITH_IMM
                                                   : Haydn::D_SW_H_WITH_IMM;

  // reject volatile/atomic; preserve ST32 MMOs on the fused lane-store.
  for (MachineMemOperand *MMO : St32->memoperands()) {
    if (MMO->isVolatile() || MMO->isAtomic())
      return false;
  }

  // Build the lane-store in ST32's position: the DR64 data goes directly to
  // memory, skipping the GPR32 extract entirely.
  // D_SW_L/H_WITH_IMM $rtd(DR64), $rs(GPR32 base), $scaled_imm(imm6)
  bool DrKill = MovInst.getOperand(1).isKill();
  MachineInstrBuilder MIB =
      BuildMI(*St32->getParent(), St32, St32->getDebugLoc(), TII.get(SwOpc))
          .addReg(DrSrc, getKillRegState(DrKill))
          .addReg(Base)
          .addImm(ScaledImm);
  MIB.cloneMemRefs(*St32);

  LLVM_DEBUG(dbgs() << "Folded MOVE32_DR + ST32 into D_SW_L/H_WITH_IMM: "
                    << MovInst << "\n");

  ++NumLaneStoreFolds;
  St32->eraseFromParent();
  MovInst.eraseFromParent();
  return true;
}


// Same-block CSE of MOV_GPR_TO_DR64 with constant GPR32 sources.
// AIE PostSelect only cleans physreg identity COPYs (in-block tracker)
// it does not invent function-wide virt-const CSE. We keep a *local* CSE
// because MOV_GPR_TO_DR64 expands to a costly stack round-trip; reusing an
// earlier same-BB def is dominance-safe (defs earlier in the block dominate
// later uses in SSA layout order within one MBB) and matches AIE's local
// tracker spirit. Cross-block reuse is left to generic MachineCSE once
// materialization is a real selected MOV rather than a pseudo expand.
bool HaydnPostSelectOptimize::tryCSEConstantDR64(MachineInstr &MovInst,
                                                   MachineRegisterInfo &MRI,
                                                   const HaydnInstrInfo &TII) {
  // MovInst: %dr = MOV_GPR_TO_DR64 %lo, %hi
  if (MovInst.getOpcode() != Haydn::MOV_GPR_TO_DR64)
    return false;

  Register DrDst = MovInst.getOperand(0).getReg();
  if (!DrDst.isVirtual())
    return false;

  Register LoReg = MovInst.getOperand(1).getReg();
  Register HiReg = MovInst.getOperand(2).getReg();

  // Look through the GPR32 defs to find constant values.
  // Supported constant forms: ADDI32 r0, C and LOADI32 C.
  auto getConstVal = [&](Register R, int64_t &Val) -> bool {
    if (!R.isVirtual())
      return false;
    MachineInstr *Def = MRI.getVRegDef(R);
    if (!Def)
      return false;
    if (Def->getOpcode() == Haydn::ADDI32 && Def->getOperand(1).getReg() == Haydn::R0) {
      Val = Def->getOperand(2).getImm();
      return true;
    }
    if (Def->getOpcode() == Haydn::LOADI32) {
      Val = Def->getOperand(1).getImm();
      return true;
    }
    return false;
  };

  int64_t LoVal, HiVal;
  if (!getConstVal(LoReg, LoVal) || !getConstVal(HiReg, HiVal))
    return false;

  // Same basic block only: walk predecessors of MovInst in layout order.
  MachineBasicBlock *MBB = MovInst.getParent();
  for (MachineInstr &MI : *MBB) {
    if (&MI == &MovInst)
      break;
    if (MI.getOpcode() != Haydn::MOV_GPR_TO_DR64)
      continue;
    Register OtherLo = MI.getOperand(1).getReg();
    Register OtherHi = MI.getOperand(2).getReg();
    int64_t OtherLoVal, OtherHiVal;
    if (!getConstVal(OtherLo, OtherLoVal) || !getConstVal(OtherHi, OtherHiVal))
      continue;
    if (OtherLoVal == LoVal && OtherHiVal == HiVal) {
      Register ExistingDr = MI.getOperand(0).getReg();
      LLVM_DEBUG(dbgs() << "Same-BB CSE MOV_GPR_TO_DR64 constant (" << LoVal
                        << ", " << HiVal << "): " << MovInst << " -> from "
                        << MI << "\n");
      BuildMI(*MBB, MovInst, MovInst.getDebugLoc(), TII.get(Haydn::OR64),
              DrDst)
          .addReg(ExistingDr)
          .addReg(ExistingDr);
      ++NumDR64ConstCSE;
      MovInst.eraseFromParent();
      return true;
    }
  }

  return false;
}

bool HaydnPostSelectOptimize::elideCrossBankRoundTrips(MachineFunction &MF) {
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const HaydnInstrInfo &TII = *ST.getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;

  // Iterate to a fixed point: each fold removes two instructions and may
  // expose a new fold opportunity (rare, but cheap to handle).
  bool LocalChanged = true;
  while (LocalChanged) {
    LocalChanged = false;
    for (MachineBasicBlock &MBB : MF) {
      SmallVector<MachineInstr *, 16> MovCandidates;
      for (MachineInstr &MI : MBB) {
        unsigned Opc = MI.getOpcode();
        if (Opc == Haydn::MOV_GPR_TO_DR64 || Opc == Haydn::MOV_DR64_TO_GPR ||
            Opc == Haydn::MOVE32_DR_L || Opc == Haydn::MOVE32_DR_H)
          MovCandidates.push_back(&MI);
      }
      for (MachineInstr *MI : MovCandidates) {
        if (!MI->getParent())
          continue;
        unsigned Opc = MI->getOpcode();
        if (Opc == Haydn::MOV_GPR_TO_DR64) {
          // CSE + sext; ST64 split fold still disabled (MMO drop).
          if (tryCSEConstantDR64(*MI, MRI, TII) ||
              tryFoldSextMovToDirect(*MI, MRI, TII)) {
            Changed = true;
            LocalChanged = true;
          }
        } else if (Opc == Haydn::MOVE32_DR_L || Opc == Haydn::MOVE32_DR_H) {
          // Lane-store fusion re-enabled with MMO clone + vol/atomic reject.
          if (tryFoldMove32DrToSw(*MI, MRI, TII)) {
            Changed = true;
            LocalChanged = true;
          }
        }
        // tryFoldLd64ToMovFrom / tryFoldMovToSt64: still disabled.
      }
    }
  }

  return Changed;
}

FunctionPass *llvm::createHaydnPostSelectOptimizePass() {
  return new HaydnPostSelectOptimize();
}
