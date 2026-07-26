//===-- HaydnPreLegalizerCombiner.cpp ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// Pre-legalization combines on generic MachineInstrs for the Haydn target.
// This combiner runs before the Legalizer, operating on fully generic G_*
// instructions.
//
// TableGen (HaydnCombine.td) owns the rule registry:
//   haydn_pre_generic_combines  — shared generics + intdiv/intrem
//   haydn_pre_target_combines   — form_mula64 (G_MULA64 / G_MULA64U)
// Dispatched exclusively via tryCombineAllImpl. Pass shell has no free-form
// opcode switch.
//
// C++ residual is match/apply helpers for TD-registered target fuses only:
//   - matchCombineMULA64 / applyCombineMULA64 (ss/uu low-lane widening MAC)
// Cast identities (trunc-of-ext, ext-of-ext) live in cast_combines — no dual
// home. Product MAC = intrinsic + G_MULA64 (this combiner).
//===----------------------------------------------------------------------===//

#include "HaydnPreLegalizerCombiner.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h" // GET_INSTRINFO_ENUM -> G_MULA64{,U}
#include "HaydnSubtarget.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/CodeGen/GlobalISel/CombinerHelper.h"
#include "llvm/CodeGen/GlobalISel/CombinerInfo.h"
#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutorImpl.h"
#include "llvm/CodeGen/GlobalISel/GISelValueTracking.h"
#include "llvm/CodeGen/GlobalISel/MIPatternMatch.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetPassConfig.h"

#define GET_GICOMBINER_DEPS
#include "HaydnGenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_DEPS

#define DEBUG_TYPE "haydn-prelegalizer-combiner"

using namespace llvm;
using namespace MIPatternMatch;

namespace {

#define GET_GICOMBINER_TYPES
#include "HaydnGenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_TYPES

// Match info for early widening MAC fusion (TD form_mula64).
struct MULA64CombineInfo {
  Register Acc;  // s64 accumulator.
  Register MulA; // s64 multiply source 1 (G_SEXT/G_ZEXT of s32).
  Register MulB; // s64 multiply source 2 (G_SEXT/G_ZEXT of s32).
  MachineInstr *MulMI = nullptr; // The G_MUL being folded (to erase).
  // Single-use COPYs between the G_MUL def and the G_ADD use (to erase).
  SmallVector<MachineInstr *, 4> DeadCopies;
  // G_MULA64 (ss → MULA64_LL) or G_MULA64U (uu → MULA64_ULUL).
  unsigned TargetOpc = Haydn::G_MULA64;
};

// Match G_ADD<s64>(acc, G_MUL<s64>(ext s32, ext s32)) -> G_MULA64{,U}.
//
// Supported (low-lane only; high-lane / mixed-sign stay intrinsic-only):
//   G_SEXT  x G_SEXT  -> G_MULA64  -> MULA64_LL   (signed x signed)
//   G_ZEXT  x G_ZEXT  -> G_MULA64U -> MULA64_ULUL (unsigned x unsigned)
//   G_ANYEXT x G_ANYEXT (or mixed with ZEXT) -> G_MULA64U (same as legalizer)
//
// NOT fused: mixed SEXT×ZEXT, high-lane LH/HL/HH, subtract forms (MULS64_*).
// Both G_ADD operand orders accepted. G_MUL result and any intermediate COPY
// chain from mul to add must be single-use.
// COPY chains on the mul-def and accumulator are peeled; dead COPYs erased.
bool matchCombineMULA64(MachineInstr &MI, MachineRegisterInfo &MRI,
                        MULA64CombineInfo &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_ADD);
  Register Dst = MI.getOperand(0).getReg();
  if (MRI.getType(Dst) != LLT::scalar(64))
    return false;
  Register Src0 = MI.getOperand(1).getReg();
  Register Src1 = MI.getOperand(2).getReg();
  if (!Src0.isVirtual() || !Src1.isVirtual())
    return false;

  // Peel a single-use COPY chain from \p Start back to a G_MUL. Each
  // intermediate COPY must have exactly one non-dbg use so apply can erase
  // the chain without leaving the mul live. Caps depth at 6.
  auto peelMulThroughCopies =
      [&](Register Start,
          SmallVectorImpl<MachineInstr *> &Copies) -> MachineInstr * {
    Copies.clear();
    Register Cur = Start;
    for (unsigned Depth = 0; Depth < 6; ++Depth) {
      if (!Cur.isVirtual())
        return nullptr;
      MachineInstr *Def = MRI.getVRegDef(Cur);
      if (!Def)
        return nullptr;
      if (Def->isCopy()) {
        Register CopyDst = Def->getOperand(0).getReg();
        Register CopySrc = Def->getOperand(1).getReg();
        if (!CopyDst.isVirtual() || !CopySrc.isVirtual())
          return nullptr;
        // The first link (Start) is used by the G_ADD; subsequent links are
        // used only by the previous COPY. All must be single-use.
        if (!MRI.hasOneNonDBGUse(CopyDst))
          return nullptr;
        Copies.push_back(Def);
        Cur = CopySrc;
        continue;
      }
      if (Def->getOpcode() == TargetOpcode::G_MUL)
        return Def;
      return nullptr;
    }
    return nullptr;
  };

  // Match a single-use G_MUL<s64> whose BOTH operands are same-kind
  // extensions of s32. MULA64_* take DR64 s64 operands; the lane is
  // implicitly the low lane (ext places the s32 in bits [31:0]).
  auto tryOperand = [&](Register MaybeMulReg, Register AccReg) -> bool {
    SmallVector<MachineInstr *, 4> CopyChain;
    MachineInstr *MulDef = peelMulThroughCopies(MaybeMulReg, CopyChain);
    if (!MulDef)
      return false;
    Register MulDefReg = MulDef->getOperand(0).getReg();
    if (MRI.getType(MulDefReg) != LLT::scalar(64))
      return false;
    // Mul result must be single-use: either the G_ADD directly, or the first
    // COPY in the chain that leads to the G_ADD.
    if (!MRI.hasOneNonDBGUse(MulDefReg))
      return false;

    Register A = MulDef->getOperand(1).getReg();
    Register B = MulDef->getOperand(2).getReg();
    // Classify each mul source as SEXT-of-s32, unsigned-ext-of-s32, or fail.
    // Returns: 1 = SEXT, 2 = ZEXT/ANYEXT, 0 = not a widening ext of s32.
    auto extKindOfS32 = [&](Register R, Register &Wide) -> int {
      if (!R.isVirtual())
        return 0;
      MachineInstr *Def = MRI.getVRegDef(R);
      if (!Def)
        return 0;
      unsigned Op = Def->getOpcode();
      if (Op != TargetOpcode::G_SEXT && Op != TargetOpcode::G_ZEXT &&
          Op != TargetOpcode::G_ANYEXT)
        return 0;
      Register Narrow = Def->getOperand(1).getReg();
      if (!Narrow.isVirtual() || MRI.getType(Narrow) != LLT::scalar(32))
        return 0;
      Wide = R; // the s64 extension result
      return Op == TargetOpcode::G_SEXT ? 1 : 2;
    };
    Register WideA, WideB;
    int KindA = extKindOfS32(A, WideA);
    int KindB = extKindOfS32(B, WideB);
    if (!KindA || !KindB)
      return false;
    // Homogeneous signedness only. Mixed SEXT×ZEXT needs ordered LUL/ULL
    // and is left for the legalizer schoolbook + separate mul/add.
    if (KindA != KindB)
      return false;
    unsigned TargetOpc = KindA == 1 ? Haydn::G_MULA64 : Haydn::G_MULA64U;

    // Peel single-use COPYs on the accumulator (do not erase — still live).
    Register PeeledAcc = AccReg;
    for (unsigned Depth = 0; Depth < 6; ++Depth) {
      if (!PeeledAcc.isVirtual())
        break;
      MachineInstr *AccDef = MRI.getVRegDef(PeeledAcc);
      if (!AccDef || !AccDef->isCopy())
        break;
      Register AccSrc = AccDef->getOperand(1).getReg();
      if (!AccSrc.isVirtual())
        break;
      PeeledAcc = AccSrc;
    }

    MatchInfo.Acc = PeeledAcc;
    MatchInfo.MulA = WideA;
    MatchInfo.MulB = WideB;
    MatchInfo.MulMI = MulDef;
    MatchInfo.DeadCopies = std::move(CopyChain);
    MatchInfo.TargetOpc = TargetOpc;
    return true;
  };

  if (tryOperand(Src0, Src1))
    return true;
  return tryOperand(Src1, Src0);
}

// Rewrite G_ADD in place into G_MULA64{,U} rd, ra, rs1, rs2; erase dead
// G_MUL and any intermediate single-use COPYs that linked mul to add.
void applyCombineMULA64(MachineInstr &MI, MachineRegisterInfo &MRI,
                        MachineIRBuilder &Builder, GISelChangeObserver &Observer,
                        MULA64CombineInfo &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_ADD);
  assert(MatchInfo.TargetOpc == Haydn::G_MULA64 ||
         MatchInfo.TargetOpc == Haydn::G_MULA64U);
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  // Rewrite the G_ADD in place into G_MULA64 / G_MULA64U rd, ra, rs1, rs2.
  MI.setDesc(Builder.getMF().getSubtarget().getInstrInfo()->get(
      MatchInfo.TargetOpc));
  MI.removeOperand(2); // drop old src1
  MI.removeOperand(1); // drop old src0
  MI.addOperand(MachineOperand::CreateReg(MatchInfo.Acc, false));  // ra
  MI.addOperand(MachineOperand::CreateReg(MatchInfo.MulA, false)); // rs1
  MI.addOperand(MachineOperand::CreateReg(MatchInfo.MulB, false)); // rs2
  Observer.changedInstr(MI);

  // Erase intermediate COPYs first (they held the mul live), then the mul.
  for (MachineInstr *CopyMI : MatchInfo.DeadCopies) {
    assert(MRI.use_empty(CopyMI->getOperand(0).getReg()) &&
           "G_MULA64 fusion left a COPY with live uses");
    Observer.erasingInstr(*CopyMI);
    CopyMI->eraseFromParent();
  }
  if (MatchInfo.MulMI) {
    assert(MRI.use_empty(MatchInfo.MulMI->getOperand(0).getReg()) &&
           "G_MULA64 fusion left the G_MUL with live uses");
    Observer.erasingInstr(*MatchInfo.MulMI);
    MatchInfo.MulMI->eraseFromParent();
  }
}

class HaydnPreLegalizerCombinerImpl : public Combiner {
protected:
  const CombinerHelper Helper;
  const HaydnPreLegalizerCombinerImplRuleConfig &RuleConfig;
  const HaydnSubtarget &STI;

public:
  HaydnPreLegalizerCombinerImpl(
      MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
      GISelValueTracking &VT, GISelCSEInfo *CSEInfo,
      const HaydnPreLegalizerCombinerImplRuleConfig &RuleConfig,
      const HaydnSubtarget &STI, MachineDominatorTree *MDT,
      const LegalizerInfo *LI);

  static const char *getName() { return "HaydnPreLegalizerCombiner"; }

  bool tryCombineAll(MachineInstr &I) const override;
  bool tryCombineAllImpl(MachineInstr &I) const;

private:
#define GET_GICOMBINER_CLASS_MEMBERS
#include "HaydnGenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CLASS_MEMBERS
};

#define GET_GICOMBINER_IMPL
#include "HaydnGenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_IMPL

HaydnPreLegalizerCombinerImpl::HaydnPreLegalizerCombinerImpl(
    MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
    GISelValueTracking &VT, GISelCSEInfo *CSEInfo,
    const HaydnPreLegalizerCombinerImplRuleConfig &RuleConfig,
    const HaydnSubtarget &STI, MachineDominatorTree *MDT,
    const LegalizerInfo *LI)
    : Combiner(MF, CInfo, TPC, &VT, CSEInfo),
      Helper(Observer, B, /*IsPreLegalize*/ true, &VT, MDT, LI),
      RuleConfig(RuleConfig), STI(STI),
#define GET_GICOMBINER_CONSTRUCTOR_INITS
#include "HaydnGenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CONSTRUCTOR_INITS
{
}

bool HaydnPreLegalizerCombinerImpl::tryCombineAll(MachineInstr &MI) const {
  // TD registry only: haydn_pre_generic_combines + form_mula64.
  // No free-form C++ opcode switch.
  return tryCombineAllImpl(MI);
}

// Pass boilerplate
//================//

class HaydnPreLegalizerCombiner : public MachineFunctionPass {
public:
  static char ID;

  HaydnPreLegalizerCombiner();

  StringRef getPassName() const override {
    return "HaydnPreLegalizerCombiner";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  HaydnPreLegalizerCombinerImplRuleConfig RuleConfig;
};
} // end anonymous namespace

void HaydnPreLegalizerCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.setPreservesCFG();
  getSelectionDAGFallbackAnalysisUsage(AU);
  AU.addRequired<GISelValueTrackingAnalysisLegacy>();
  AU.addPreserved<GISelValueTrackingAnalysisLegacy>();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  AU.addPreserved<MachineDominatorTreeWrapperPass>();
  AU.addRequired<GISelCSEAnalysisWrapperPass>();
  AU.addPreserved<GISelCSEAnalysisWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

HaydnPreLegalizerCombiner::HaydnPreLegalizerCombiner()
    : MachineFunctionPass(ID) {
  if (!RuleConfig.parseCommandLineOption())
    report_fatal_error("Invalid rule identifier");
}

bool HaydnPreLegalizerCombiner::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasFailedISel())
    return false;
  auto &TPC = getAnalysis<TargetPassConfig>();

  // Enable CSE.
  GISelCSEAnalysisWrapper &Wrapper =
      getAnalysis<GISelCSEAnalysisWrapperPass>().getCSEWrapper();
  auto *CSEInfo = &Wrapper.get(TPC.getCSEConfig());

  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const auto *LI = ST.getLegalizerInfo();

  const Function &F = MF.getFunction();
  bool EnableOpt =
      MF.getTarget().getOptLevel() != CodeGenOptLevel::None && !skipFunction(F);
  GISelValueTracking *VT =
      &getAnalysis<GISelValueTrackingAnalysisLegacy>().get(MF);
  MachineDominatorTree *MDT =
      &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  CombinerInfo CInfo(/*AllowIllegalOps*/ true, /*ShouldLegalizeIllegal*/ false,
                     /*LegalizerInfo*/ nullptr, EnableOpt, F.hasOptSize(),
                     F.hasMinSize());
  // Disable fixed-point iteration to reduce compile-time
  CInfo.MaxIterations = 1;
  CInfo.ObserverLvl = CombinerInfo::ObserverLevel::SinglePass;
  // This is the first combiner after IRTranslator, so the input IR might
  // contain dead instructions.
  CInfo.EnableFullDCE = true;
  HaydnPreLegalizerCombinerImpl Impl(MF, CInfo, &TPC, *VT, CSEInfo,
                                     RuleConfig, ST, MDT, LI);
  return Impl.combineMachineInstrs();
}

char HaydnPreLegalizerCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(HaydnPreLegalizerCombiner, DEBUG_TYPE,
                      "Combine Haydn MachineInstrs before legalization", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelValueTrackingAnalysisLegacy)
INITIALIZE_PASS_END(HaydnPreLegalizerCombiner, DEBUG_TYPE,
                    "Combine Haydn MachineInstrs before legalization", false,
                    false)

FunctionPass *llvm::createHaydnPreLegalizerCombiner() {
  return new HaydnPreLegalizerCombiner();
}
