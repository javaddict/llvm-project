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
// instructions. It performs target-specific simplifications that reduce IR
// complexity before type/action legalization, including:
// Trunc elimination: G_TRUNC(G_ANYEXT x) -> COPY x
// Extension simplification: G_SEXT(G_SEXT x) -> G_SEXT x, etc.
// Redundant AND/OR with all-ones/all-zeros
// Constant folding for binary ops
// Shift by zero elimination
// Address arithmetic: chained G_PTR_ADD constant folding
// These combines leverage LLVM's generic CombinerHelper where possible and
// add Haydn-specific patterns on top.
//===----------------------------------------------------------------------===//

#include "HaydnPreLegalizerCombiner.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h" // GET_INSTRINFO_ENUM -> Haydn::G_MULA64
#include "HaydnSubtarget.h"
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

// Match G_TRUNC(G_SEXT/G_ZEXT x) where the trunc output type equals the
// extension's input type. A truncate cancels a sign/zero extension when the
// narrowed result width matches the value's pre-extension width, so the pair
// is identity and can be replaced with a COPY of the inner source.
// Canonicalizing this HERE (in the pre-legalizer) prevents the upstream
// Legalizer's artifact-combiner `trunc(trunc)` fold from observing a chain
// whose inner source has already been narrowed to a mismatched width, which
// otherwise trips `MachineIRBuilder::validateTruncExt` ("invalid widening
// trunc") on yarpgen-style nested cast chains such as
// `(long long)X >> (long long)(int)(bool)Y`. Folding the identity before
// the Legalizer sees it removes the too-narrow inner source entirely.
bool matchTruncOfExtToIdentity(MachineInstr &MI, MachineRegisterInfo &MRI,
                               Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_TRUNC);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  LLT DstTy = MRI.getType(Dst);

  // G_TRUNC(G_SEXT x) where output type == sext input type
  Register InnerSrc;
  if (mi_match(Src, MRI, m_GSExt(m_Reg(InnerSrc)))) {
    if (DstTy == MRI.getType(InnerSrc)) {
      MatchInfo = InnerSrc;
      return true;
    }
  }
  // G_TRUNC(G_ZEXT x) where output type == zext input type
  if (mi_match(Src, MRI, m_GZExt(m_Reg(InnerSrc)))) {
    if (DstTy == MRI.getType(InnerSrc)) {
      MatchInfo = InnerSrc;
      return true;
    }
  }
  return false;
}

// Match G_TRUNC(G_ANYEXT x) where the trunc output type matches the
// anyext input type. This is a no-op identity that can be replaced
// with a COPY.
bool matchTruncOfAnyExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                        Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_TRUNC);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  LLT DstTy = MRI.getType(Dst);

  // Look through G_ANYEXT: if the source is G_ANYEXT and the trunc output
  // type matches the anyext input type, this is a no-op.
  Register AnyExtSrc;
  if (mi_match(Src, MRI, m_GAnyExt(m_Reg(AnyExtSrc)))) {
    LLT InnerTy = MRI.getType(AnyExtSrc);
    if (DstTy == InnerTy) {
      MatchInfo = AnyExtSrc;
      return true;
    }
  }

  return false;
}

// Apply the trunc-of-ext combine: replace with COPY. Shared apply helper
// for matchTruncOfExtToIdentity (sext/zext) and matchTruncOfAnyExt (anyext)
// all three ext flavors collapse to the same COPY-of-inner-source shape.
void applyTruncOfExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                     MachineIRBuilder &Builder,
                     GISelChangeObserver &Observer,
                     Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_TRUNC);
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  // Replace G_TRUNC with COPY
  MI.setDesc(MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_SEXT(G_SEXT x) or G_ZEXT(G_ZEXT x) — collapse cascading same-kind
// extensions into a single extend from the original source.
// the old apply rewrote the outer ext to `COPY InnerSrc`, which is
// only valid when DstTy == type(InnerSrc). For `s64 = G_SEXT(s32 =
// G_SEXT(s1))` that produced `s64 = COPY s1` (type-mismatched COPY).
// Upstream getIConstantVRegValWithLookThrough follows COPY without
// adjusting APInt width; a later G_TRUNC then asserts in APInt::trunc.
bool matchRedundantExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                       Register &MatchInfo) {
  unsigned Opc = MI.getOpcode();
  assert(Opc == TargetOpcode::G_SEXT || Opc == TargetOpcode::G_ZEXT);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  LLT DstTy = MRI.getType(Dst);

  Register InnerSrc;
  if ((Opc == TargetOpcode::G_SEXT &&
       mi_match(Src, MRI, m_GSExt(m_Reg(InnerSrc)))) ||
      (Opc == TargetOpcode::G_ZEXT &&
       mi_match(Src, MRI, m_GZExt(m_Reg(InnerSrc))))) {
    // Only fold when the inner ext is single-use (else we'd duplicate it)
    // and the original source is strictly narrower than the outer dest.
    if (!MRI.hasOneNonDBGUse(Src))
      return false;
    LLT InnerTy = MRI.getType(InnerSrc);
    if (!InnerTy.isValid() || !DstTy.isValid() ||
        InnerTy.getSizeInBits() >= DstTy.getSizeInBits())
      return false;
    MatchInfo = InnerSrc;
    return true;
  }

  return false;
}

// Apply double-ext collapse: replace outer G_SEXT/G_ZEXT with a single
// extend from InnerSrc to the outer destination type. When types already
// match (true no-op), use COPY.
void applyRedundantExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                       MachineIRBuilder &Builder,
                       GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  LLT DstTy = MRI.getType(Dst);
  LLT SrcTy = MRI.getType(MatchInfo);
  Observer.erasingInstr(MI);
  if (DstTy == SrcTy) {
    Builder.buildCopy(Dst, MatchInfo);
  } else if (MI.getOpcode() == TargetOpcode::G_SEXT) {
    Builder.buildSExt(Dst, MatchInfo);
  } else {
    Builder.buildZExt(Dst, MatchInfo);
  }
  MI.eraseFromParent();
}

// Peel through COPY chains to find the real defining instruction of \p Reg.
// Depth-capped (mirrors HaydnPostSelectOptimize::peekThroughCopies).
static MachineInstr *peekThroughCopies(Register Reg, MachineRegisterInfo &MRI) {
  unsigned Depth = 0;
  while (Reg.isVirtual() && Depth < 6) {
    MachineInstr *DefMI = MRI.getVRegDef(Reg);
    if (!DefMI || !DefMI->isCopy())
      return DefMI;
    Register SrcReg = DefMI->getOperand(1).getReg();
    if (!SrcReg.isVirtual())
      return DefMI;
    Reg = SrcReg;
    ++Depth;
  }
  return Reg.isVirtual() ? MRI.getVRegDef(Reg) : nullptr;
}

// Match info for early widening MAC fusion.
struct MULA64CombineInfo {
  Register Acc;   //< s64 accumulator.
  Register MulA;  //< s64 multiply source 1 (a G_SEXT of s32).
  Register MulB;  //< s64 multiply source 2 (a G_SEXT of s32).
  MachineInstr *MulMI = nullptr; //< The G_MUL being folded (to erase).
};

// Match G_ADD<s64>(acc, G_MUL<s64>(G_SEXT s32, G_SEXT s32)) -> G_MULA64.
// Signed-signed only: both G_MUL operands must be G_SEXT of s32 (the `_ss_`
// accumulator variant -- unsigned/mixed `_su_`/`_uu_` have no accumulator form
// and are not fused). Both G_ADD operand orders accepted. The G_MUL result
// must be single-use. COPY chains on the mul-def and accumulator are peeled.
static bool matchCombineMULA64(MachineInstr &MI, MachineRegisterInfo &MRI,
                               MULA64CombineInfo &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_ADD);
  Register Dst = MI.getOperand(0).getReg();
  if (MRI.getType(Dst) != LLT::scalar(64))
    return false;
  Register Src0 = MI.getOperand(1).getReg();
  Register Src1 = MI.getOperand(2).getReg();
  if (!Src0.isVirtual() || !Src1.isVirtual())
    return false;

  // Match a single-use G_MUL<s64> whose BOTH operands are G_SEXT of s32.
  // Returns the G_MUL MI and records the s64 G_SEXT results (not the s32
  // MULA64_LL takes DR64 s64 operands; the lane is implicitly the low lane).
  auto tryOperand = [&](Register MaybeMulReg,
                        Register AccReg) -> MachineInstr * {
    MachineInstr *MulDef = peekThroughCopies(MaybeMulReg, MRI);
    if (!MulDef || MulDef->getOpcode() != TargetOpcode::G_MUL)
      return nullptr;
    Register MulDefReg = MulDef->getOperand(0).getReg();
    if (MRI.getType(MulDefReg) != LLT::scalar(64))
      return nullptr;
    if (!MRI.hasOneNonDBGUse(MulDefReg))
      return nullptr;
    Register A = MulDef->getOperand(1).getReg();
    Register B = MulDef->getOperand(2).getReg();
    // Both multiply sources must be G_SEXT of s32 (signed-signed, `_ss_`).
    auto isSExtS32 = [&](Register R, Register &Wide) -> bool {
      if (!R.isVirtual())
        return false;
      MachineInstr *Def = MRI.getVRegDef(R);
      if (!Def || Def->getOpcode() != TargetOpcode::G_SEXT)
        return false;
      Register Narrow = Def->getOperand(1).getReg();
      if (MRI.getType(Narrow) != LLT::scalar(32))
        return false;
      Wide = R; // the s64 G_SEXT result
      return true;
    };
    Register WideA, WideB;
    if (!isSExtS32(A, WideA) || !isSExtS32(B, WideB))
      return nullptr;
    // Bypass a COPY on the accumulator input.
    MachineInstr *AccDef = peekThroughCopies(AccReg, MRI);
    if (AccDef && AccDef->isCopy()) {
      Register AccSrc = AccDef->getOperand(1).getReg();
      if (AccSrc.isVirtual())
        AccReg = AccSrc;
    }
    MatchInfo.Acc = AccReg;
    MatchInfo.MulA = WideA;
    MatchInfo.MulB = WideB;
    MatchInfo.MulMI = MulDef;
    return MulDef;
  };

  if (tryOperand(Src0, Src1))
    return true;
  return tryOperand(Src1, Src0) != nullptr;
}

// Rewrite G_ADD in place into G_MULA64 rd, ra, rs1, rs2; erase the dead G_MUL.
// The s64 G_SEXT results become the multiply sources (rs1, rs2); the low-lane
// selection of MULA64_LL is implicit (G_SEXT places the s32 in the low lane).
static void applyCombineMULA64(MachineInstr &MI, MachineRegisterInfo &MRI,
                               MachineIRBuilder &Builder,
                               GISelChangeObserver &Observer,
                               MULA64CombineInfo &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_ADD);
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  // Rewrite the G_ADD in place into G_MULA64 rd, ra, rs1, rs2.
  MI.setDesc(Builder.getMF().getSubtarget().getInstrInfo()->get(
      Haydn::G_MULA64));
  MI.removeOperand(2);                 // drop old src1
  MI.removeOperand(1);                 // drop old src0
  MI.addOperand(MachineOperand::CreateReg(MatchInfo.Acc, false));  // ra
  MI.addOperand(MachineOperand::CreateReg(MatchInfo.MulA, false)); // rs1
  MI.addOperand(MachineOperand::CreateReg(MatchInfo.MulB, false)); // rs2
  Observer.changedInstr(MI);
  // Erase the now-dead G_MUL. Its single use (the old G_ADD) is gone.
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
  if (tryCombineAllImpl(MI))
    return true;

  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  default:
    break;
  case TargetOpcode::G_TRUNC: {
    // G_TRUNC(G_SEXT/G_ZEXT x) -> COPY x (identity when dest == ext input)
    // Run before the anyext fold: sext/zext carry semantic payload the
    // generic anyext rule does not peek through, and collapsing the
    // identity here starves the Legalizer artifact sweep of the
    // trunc(trunc) shape that would otherwise crash validateTruncExt.
    Register MatchInfo;
    if (matchTruncOfExtToIdentity(MI, *B.getMRI(), MatchInfo)) {
      applyTruncOfExt(const_cast<MachineInstr &>(MI), *B.getMRI(), B,
                      Observer, MatchInfo);
      return true;
    }
    // G_TRUNC(G_ANYEXT x) -> COPY x
    // Eliminates trivial trunc-of-anyext patterns that the IRTranslator
    // can produce for certain integer width conversions.
    if (matchTruncOfAnyExt(MI, *B.getMRI(), MatchInfo)) {
      applyTruncOfExt(const_cast<MachineInstr &>(MI), *B.getMRI(), B,
                      Observer, MatchInfo);
      return true;
    }
    break;
  }
  case TargetOpcode::G_SEXT:
  case TargetOpcode::G_ZEXT: {
    // G_SEXT(G_SEXT x) -> COPY x
    // G_ZEXT(G_ZEXT x) -> COPY x
    // Redundant same-kind extension is a no-op.
    Register MatchInfo;
    if (matchRedundantExt(MI, *B.getMRI(), MatchInfo)) {
      applyRedundantExt(const_cast<MachineInstr &>(MI), *B.getMRI(), B,
                        Observer, MatchInfo);
      return true;
    }
    break;
  }
  case TargetOpcode::G_AND: {
    // G_AND x, -1 -> COPY x (AND with all-ones is identity)
    // G_AND -1, x -> COPY x
    Register Replacement;
    if (Helper.matchRedundantAnd(MI, Replacement)) {
      Helper.replaceSingleDefInstWithReg(MI, Replacement);
      return true;
    }
    break;
  }
  case TargetOpcode::G_OR: {
    // G_OR x, 0 -> COPY x (OR with all-zeros is identity)
    // G_OR 0, x -> COPY x
    Register Replacement;
    if (Helper.matchRedundantOr(MI, Replacement)) {
      Helper.replaceSingleDefInstWithReg(MI, Replacement);
      return true;
    }
    break;
  }
  case TargetOpcode::G_SHL:
  case TargetOpcode::G_LSHR:
  case TargetOpcode::G_ASHR: {
    // G_SHL x, 0 -> COPY x
    // G_LSHR x, 0 -> COPY x
    // G_ASHR x, 0 -> COPY x
    // Shift by zero is identity.
    if (Helper.matchOperandIsZero(MI, 2)) {
      Helper.replaceSingleDefInstWithReg(MI, MI.getOperand(1).getReg());
      return true;
    }
    break;
  }
  case TargetOpcode::G_PTR_ADD: {
    // G_PTR_ADD(G_PTR_ADD base, C1), C2 -> G_PTR_ADD base, (C1+C2)
    // Folds chained constant offsets in pointer arithmetic.
    PtrAddChain MatchInfo;
    if (Helper.matchPtrAddImmedChain(MI, MatchInfo)) {
      Helper.applyPtrAddImmedChain(MI, MatchInfo);
      return true;
    }
    break;
  }
  case TargetOpcode::G_ADD:
  case TargetOpcode::G_SUB:
  case TargetOpcode::G_MUL: {
    // Constant fold binary operations with two constant operands.
    // G_ADD const, const -> const
    // G_SUB const, const -> const
    // G_MUL const, const -> const
    APInt MatchInfo;
    if (Helper.matchConstantFoldBinOp(MI, MatchInfo)) {
      Helper.replaceInstWithConstant(MI, MatchInfo);
      return true;
    }
    // Early widening MAC fusion, G_ADD only: G_ADD<s64>(acc
    // G_MUL<s64>(G_SEXT s32, G_SEXT s32)) -> G_MULA64. Runs AFTER
    // constant-fold. Signed-signed only; both operand orders; COPY-peels.
    if (Opc == TargetOpcode::G_ADD) {
      MULA64CombineInfo MULAInfo;
      if (matchCombineMULA64(MI, *B.getMRI(), MULAInfo)) {
        applyCombineMULA64(MI, *B.getMRI(), B, Observer, MULAInfo);
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_SDIV:
  case TargetOpcode::G_UDIV:
  case TargetOpcode::G_SREM:
  case TargetOpcode::G_UREM: {
    // Division/remainder strength reduction.
    // Haydn has no native divide instruction — without these combines
    // G_SDIV/G_UDIV/G_SREM/G_UREM by constants lower to __divsi3/__divdi3
    // libcalls (170 sites across 30+ NatureDSP FFT/DCT kernels, e.g.
    // fft_cplx16x16.c:701 `N/4`, bit-reversal `i/8`, polyphase `M/3`).
    // The combines run BEFORE legalization so the division is replaced
    // with shifts + arithmetic that Haydn can execute natively:
    // Power-of-2 divisor: sdiv X, 4 → ashr X, 2; udiv X, 4 → lshr X, 2
    // Non-power-of-2 constant: sdiv X, 3 → magic-number multiply
    // (mulhi + adjustment, same as SelectionDAG's DAGCombiner).
    // Uses CombinerHelper's existing division combines (no new algorithm):
    // matchDivByPow2 + applySDivByPow2/applyUDivByPow2
    // matchSDivOrSRemByConst + applySDivOrSRemByConst
    // matchUDivOrURemByConst + applyUDivOrURemByConst
    // Fixed (Tier 1 optimization wave).

    // Try power-of-2 first (shift — cheapest). matchDivByPow2 only supports
    // G_SDIV/G_UDIV (it asserts on G_SREM/G_UREM), so gate the opcode.
    bool IsSigned = (Opc == TargetOpcode::G_SDIV || Opc == TargetOpcode::G_SREM);
    if ((Opc == TargetOpcode::G_SDIV || Opc == TargetOpcode::G_UDIV) &&
        Helper.matchDivByPow2(MI, IsSigned)) {
      if (IsSigned)
        Helper.applySDivByPow2(MI);
      else
        Helper.applyUDivByPow2(MI);
      return true;
    }
    // Fall back to magic-number multiply for non-power-of-2 constants.
    if (Opc == TargetOpcode::G_SDIV || Opc == TargetOpcode::G_SREM) {
      if (Helper.matchSDivOrSRemByConst(MI)) {
        Helper.applySDivOrSRemByConst(MI);
        return true;
      }
    } else {
      if (Helper.matchUDivOrURemByConst(MI)) {
        Helper.applyUDivOrURemByConst(MI);
        return true;
      }
    }
    break;
  }
  }

  return false;
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
