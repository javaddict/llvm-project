//===-- HaydnPostLegalizerCombiner.cpp -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// Post-legalization combines on generic MachineInstrs for the Haydn target.
// The combines here must preserve instruction legality and operate on generic
// (pre-RegBankSelect) MIR. MAC fusion is NOT performed here because MAC32
// requires GPR32-constrained registers — it's done in PostSelectOptimize
// instead. This combiner is reserved for generic-MIR optimizations.
// TableGen-generated rules (from HaydnCombine.td) are dispatched via
// tryCombineAllImpl. Additional C++ rules that don't map to TableGen
// patterns are in tryCombineAll after the TableGen dispatch.
//===----------------------------------------------------------------------===//

#include "HaydnPostLegalizerCombiner.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
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
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#define GET_GICOMBINER_DEPS
#include "HaydnGenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_DEPS

#define DEBUG_TYPE "haydn-postlegalizer-combiner"

using namespace llvm;
using namespace MIPatternMatch;

// Product AIE-style form (GISel post-legalizer): G_LOAD/STORE + G_PTR_ADD
// → G_HAYDN_*INC_* → AGU PRE/POST at InstructionSelect.
// Modeled on llvm-aie AIECombinerHelper::findPostIncMatch /
// checkRegUsesDominate (aie-postinc-combine default ON;
// aie-greedy-address-combines default OFF).
// All of PRE/POST/IMM/REG default ON. Sole product AGU fuse path (FormUpdateAddr retired).
static cl::opt<bool> EnableHaydnGISelUpdateAddr(
    "haydn-enable-gisel-update-addr", cl::init(true), cl::Hidden,
    cl::desc("Enable GISel AGU update-addr form (PRE+POST, IMM+REG). "
             "Default ON. Disable: -haydn-enable-gisel-update-addr=0."));
static cl::opt<bool> EnableHaydnGISelPostInc(
    "haydn-enable-gisel-post-inc", cl::init(true), cl::Hidden,
    cl::desc("GISel form POST-inc/dec. Default ON (AIE aie-postinc-combine)."));
static cl::opt<bool> EnableHaydnGISelPreInc(
    "haydn-enable-gisel-pre-inc", cl::init(true), cl::Hidden,
    cl::desc("GISel form PRE-inc/dec. Default ON."));
static cl::opt<bool> EnableHaydnGISelRegStride(
    "haydn-enable-gisel-reg-stride", cl::init(true), cl::Hidden,
    cl::desc("GISel form non-const (REG) stride. Default ON."));
// AIE aie-greedy-address-combines: default OFF. When false, require every
// use of the pre-update base to dominate the insertion point (ignore the
// folded G_PTR_ADD). When true, fuse even if base is used later (unsafe).
static cl::opt<bool> EnableHaydnGISelGreedyAddr(
    "haydn-enable-gisel-greedy-addr", cl::init(false), cl::Hidden,
    cl::desc("GISel form: allow fuse when base is used after insert "
             "(AIE greedy; default OFF)."));

namespace {

#define GET_GICOMBINER_TYPES
#include "HaydnGenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_TYPES

//===----------------------------------------------------------------------===//
// C++ combine match/apply helpers for patterns not expressible in TableGen.
//===----------------------------------------------------------------------===//

// Match G_TRUNC(G_SEXT/G_ZEXT x) where the trunc output type matches the
// extension input type. This is identity: sext/trunc cancels out.
// Example: G_TRUNC(s32) of G_SEXT(s16->s64) where result is s16 -> COPY src.
bool matchTruncOfExtToIdentity(MachineInstr &MI, MachineRegisterInfo &MRI,
                               Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_TRUNC);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  LLT DstTy = MRI.getType(Dst);

  Register InnerSrc;
  // G_TRUNC(G_SEXT x) where output type == sext input type
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

// Apply the trunc-of-ext identity: replace G_TRUNC with COPY.
void applyTruncOfExtToIdentity(MachineInstr &MI, MachineRegisterInfo &MRI,
                               MachineIRBuilder &Builder,
                               GISelChangeObserver &Observer,
                               Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_TRUNC(G_ANYEXT x) where the trunc output type matches the anyext
// input type. This is a no-op identity that can be replaced with COPY.
bool matchTruncOfAnyExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                        Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_TRUNC);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  LLT DstTy = MRI.getType(Dst);

  Register AnyExtSrc;
  if (mi_match(Src, MRI, m_GAnyExt(m_Reg(AnyExtSrc)))) {
    if (DstTy == MRI.getType(AnyExtSrc)) {
      MatchInfo = AnyExtSrc;
      return true;
    }
  }
  return false;
}

// Apply the trunc-of-anyext combine: replace G_TRUNC with COPY.
void applyTruncOfAnyExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                        MachineIRBuilder &Builder,
                        GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match redundant G_SEXT/G_ZEXT followed by G_TRUNC back to the original
// type. Pattern: dst = G_TRUNC(G_SEXT/G_ZEXT src) where dst type == src type.
// This is identity extension-truncation.
bool matchExtTruncIdentity(MachineInstr &MI, MachineRegisterInfo &MRI,
                           Register &MatchInfo) {
  unsigned Opc = MI.getOpcode();
  assert(Opc == TargetOpcode::G_SEXT || Opc == TargetOpcode::G_ZEXT);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  LLT SrcTy = MRI.getType(Src);

  // Only match if the sole use is a trunc back to the source type.
  if (!MRI.hasOneNonDBGUse(Dst))
    return false;

  MachineInstr &UseMI = *MRI.use_instr_nodbg_begin(Dst);
  if (UseMI.getOpcode() != TargetOpcode::G_TRUNC)
    return false;

  Register TruncDst = UseMI.getOperand(0).getReg();
  if (MRI.getType(TruncDst) == SrcTy) {
    MatchInfo = TruncDst;
    return true;
  }
  return false;
}

// Apply ext-trunc identity: replace the trunc with COPY of the original src.
void applyExtTruncIdentity(MachineInstr &MI, MachineRegisterInfo &MRI,
                           MachineIRBuilder &Builder,
                           GISelChangeObserver &Observer, Register &MatchInfo) {
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();

  // The MatchInfo is the trunc's destination register.
  // We replace the trunc with COPY src, and then the ext becomes dead.
  MachineInstr &TruncMI = *MRI.use_instr_nodbg_begin(Dst);
  Builder.setInstrAndDebugLoc(TruncMI);
  Observer.changingInstr(TruncMI);
  TruncMI.setDesc(
      TruncMI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (TruncMI.getNumOperands() > 2)
    TruncMI.removeOperand(TruncMI.getNumOperands() - 1);
  TruncMI.getOperand(1).setReg(Src);
  Observer.changedInstr(TruncMI);
}

// Match G_COPY of the same register: G_COPY x, x -> eliminate.
bool matchRedundantCopy(MachineInstr &MI, MachineRegisterInfo &MRI) {
  assert(MI.getOpcode() == TargetOpcode::COPY);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  return Dst == Src;
}

// Apply redundant copy elimination: erase the instruction.
void applyRedundantCopy(MachineInstr &MI, MachineRegisterInfo &MRI,
                        MachineIRBuilder &Builder,
                        GISelChangeObserver &Observer) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.erasingInstr(MI);
  MI.eraseFromParent();
}

// Match G_ICMP with both operands being known constants.
// Replaces the compare with a constant 0 or 1 result.
bool matchConstantFoldICmp(MachineInstr &MI, MachineRegisterInfo &MRI,
                           int64_t &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_ICMP);
  auto Pred =
      static_cast<CmpInst::Predicate>(MI.getOperand(1).getPredicate());
  Register LHS = MI.getOperand(2).getReg();
  Register RHS = MI.getOperand(3).getReg();

  // Only fold scalar integer types.
  LLT LTy = MRI.getType(LHS);
  if (!LTy.isScalar())
    return false;

  auto LHSV = getIConstantVRegValWithLookThrough(LHS, MRI);
  auto RHSV = getIConstantVRegValWithLookThrough(RHS, MRI);
  if (!LHSV || !RHSV)
    return false;

  // residual: look-through can surface constants of unequal bit widths
  // (e.g. trunc/zext chains at -O0). APInt::operator== / ICmpInst::compare
  // assert BitWidth equality — normalize to the icmp operand type first.
  unsigned BW = LTy.getSizeInBits();
  APInt LHSVal = LHSV->Value.sextOrTrunc(BW);
  APInt RHSVal = RHSV->Value.sextOrTrunc(BW);
  bool Result = ICmpInst::compare(LHSVal, RHSVal, Pred);
  MatchInfo = Result ? 1 : 0;
  return true;
}

// Apply constant-folded ICMP: replace with constant.
void applyConstantFoldICmp(MachineInstr &MI, MachineRegisterInfo &MRI,
                           MachineIRBuilder &Builder,
                           GISelChangeObserver &Observer, int64_t &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  Builder.buildConstant(Dst, MatchInfo);
  Observer.erasingInstr(MI);
  MI.eraseFromParent();
}

// Match G_AND with all-ones or all-zeros constant operand.
// G_AND x, 0 -> 0
// G_AND 0, x -> 0
bool matchAndZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                  APInt &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_AND);
  Register Dst = MI.getOperand(0).getReg();
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;
  unsigned BW = Ty.getSizeInBits();

  // Check G_AND x, 0 or G_AND 0, x — store zero at *destination* width so
  // applyAndZero → buildConstant never sees a mismatched APInt.
  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (V2 && V2->Value.isZero()) {
    MatchInfo = APInt::getZero(BW);
    return true;
  }
  auto V1 = getIConstantVRegValWithLookThrough(Op1, MRI);
  if (V1 && V1->Value.isZero()) {
    MatchInfo = APInt::getZero(BW);
    return true;
  }
  return false;
}

// Apply G_AND with zero: replace with constant 0.
void applyAndZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                  MachineIRBuilder &Builder, GISelChangeObserver &Observer,
                  APInt &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  Builder.buildConstant(Dst, MatchInfo);
  Observer.erasingInstr(MI);
  MI.eraseFromParent();
}

// Match G_OR with all-ones constant operand.
// G_OR x, -1 -> -1
// G_OR -1, x -> -1
bool matchOrAllOnes(MachineInstr &MI, MachineRegisterInfo &MRI,
                    APInt &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_OR);
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(MI.getOperand(0).getReg());
  if (!Ty.isScalar())
    return false;
  unsigned BitWidth = Ty.getSizeInBits();
  APInt AllOnes = APInt::getAllOnes(BitWidth);

  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (V2 && V2->Value.sextOrTrunc(BitWidth) == AllOnes) {
    MatchInfo = AllOnes;
    return true;
  }
  auto V1 = getIConstantVRegValWithLookThrough(Op1, MRI);
  if (V1 && V1->Value.sextOrTrunc(BitWidth) == AllOnes) {
    MatchInfo = AllOnes;
    return true;
  }
  return false;
}

// Apply G_OR with all-ones: replace with constant -1.
void applyOrAllOnes(MachineInstr &MI, MachineRegisterInfo &MRI,
                    MachineIRBuilder &Builder, GISelChangeObserver &Observer,
                    APInt &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  Builder.buildConstant(Dst, MatchInfo);
  Observer.erasingInstr(MI);
  MI.eraseFromParent();
}

// Match G_BSWAP(G_BSWAP x) -> x. Double bswap is identity.
bool matchBswapOfBswap(MachineInstr &MI, MachineRegisterInfo &MRI,
                       Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_BSWAP);
  Register Src = MI.getOperand(1).getReg();

  auto *InnerBSwap = getOpcodeDef(TargetOpcode::G_BSWAP, Src, MRI);
  if (!InnerBSwap)
    return false;

  MatchInfo = InnerBSwap->getOperand(1).getReg();
  return true;
}

// Apply bswap-of-bswap: replace with COPY of the original source.
void applyBswapOfBswap(MachineInstr &MI, MachineRegisterInfo &MRI,
                       MachineIRBuilder &Builder,
                       GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_SEXT(G_SEXT x) where the outer extension source is already
// sign-extended (i.e., the inner sext already covered the bits).
// This is redundant when the inner sext destination type equals the
// outer sext destination type, which means the outer is a no-op.
bool matchRedundantSExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                        Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_SEXT);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  LLT DstTy = MRI.getType(Dst);

  Register InnerSrc;
  if (mi_match(Src, MRI, m_GSExt(m_Reg(InnerSrc)))) {
    // G_SEXT(G_SEXT inner) -> identity if outer dest type == inner dest type
    // (the inner sext already produced the right type).
    if (DstTy == MRI.getType(Src)) {
      MatchInfo = Src;
      return true;
    }
  }
  return false;
}

// Apply redundant sext: replace with COPY.
void applyRedundantSExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                        MachineIRBuilder &Builder,
                        GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_ZEXT(G_ZEXT x) where the outer extension is a no-op
// (outer dest type == inner dest type).
bool matchRedundantZExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                        Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_ZEXT);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  LLT DstTy = MRI.getType(Dst);

  Register InnerSrc;
  if (mi_match(Src, MRI, m_GZExt(m_Reg(InnerSrc)))) {
    if (DstTy == MRI.getType(Src)) {
      MatchInfo = Src;
      return true;
    }
  }
  return false;
}

// Apply redundant zext: replace with COPY.
void applyRedundantZExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                        MachineIRBuilder &Builder,
                        GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

//===----------------------------------------------------------------------===//
// Redundant extension elimination combines.
//
// These patterns eliminate redundant cascading extension sequences:
// double-sext, double-zext, sext-of-zext, zext-of-sext, trunc-zext identity
// and zext-trunc identity round-trips.
//===----------------------------------------------------------------------===//

// Match G_ZEXT(G_SEXT x) where the outer zero-extension is a no-op because
// the destination type equals the inner SEXT's destination type.
// Pattern: dst = G_ZEXT(G_SEXT x) where DstTy == SextOutTy -> COPY sext_result.
bool matchZExtOfSExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                     Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_ZEXT);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  LLT DstTy = MRI.getType(Dst);

  Register InnerSrc;
  if (mi_match(Src, MRI, m_GSExt(m_Reg(InnerSrc)))) {
    // G_ZEXT(G_SEXT inner) where outer dest == inner sext dest is a no-op.
    if (DstTy == MRI.getType(Src)) {
      MatchInfo = Src;
      return true;
    }
  }
  return false;
}

// Apply zext-of-sext: replace with COPY of the inner sext result.
void applyZExtOfSExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                     MachineIRBuilder &Builder,
                     GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_SEXT(G_ZEXT x) where the outer sign-extension of a zero-extended
// value is equivalent to zero-extension. Since G_ZEXT zeros all high bits
// the sign bit of the zext result is always 0, so G_SEXT of that result also
// fills high bits with 0 — identical to G_ZEXT.
// Pattern: dst = G_SEXT(G_ZEXT x) -> replace with G_ZEXT from x to dst type.
// Valid for ANY wider destination type (the outer sext is always redundant
// because the inner zext guarantees a non-negative value).
bool matchSExtOfZExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                     Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_SEXT);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  LLT DstTy = MRI.getType(Dst);

  Register InnerSrc;
  if (mi_match(Src, MRI, m_GZExt(m_Reg(InnerSrc)))) {
    // If the outer dest type == inner zext dest type, it's a simple no-op
    // (handled by matchRedundantSExt for same-type).
    if (DstTy == MRI.getType(Src))
      return false;

    // G_SEXT(G_ZEXT x) from TyA -> TyB -> TyC where TyC > TyB.
    // Inner zext zeros bits [TyA..TyB). Outer sext zeros bits [TyB..TyC)
    // because sign bit at position TyB-1 is 0 (zext cleared it).
    // Equivalent to: G_ZEXT x from TyA -> TyC directly.
    // Only profitable if the inner zext has a single use.
    if (!MRI.hasOneNonDBGUse(Src))
      return false;

    MatchInfo = InnerSrc;
    return true;
  }
  return false;
}

// Apply sext-of-zext: replace G_SEXT(G_ZEXT x) with G_ZEXT x to the outer
// destination type.
void applySExtOfZExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                     MachineIRBuilder &Builder,
                     GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  Observer.erasingInstr(MI);
  Builder.buildZExt(Dst, MatchInfo);
  MI.eraseFromParent();
}

// Match G_ZEXT(G_ZEXT x) where cascading zero-extensions can be collapsed
// into a single zero-extension from the original source to the final type.
// Pattern: dst = G_ZEXT(G_ZEXT x) from TyA->TyB->TyC -> G_ZEXT x TyA->TyC.
// Only profitable if the inner zext has a single use.
bool matchFoldDoubleZExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                         Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_ZEXT);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  LLT DstTy = MRI.getType(Dst);

  Register InnerSrc;
  if (mi_match(Src, MRI, m_GZExt(m_Reg(InnerSrc)))) {
    // If dest type == source type, it's a no-op handled by matchRedundantZExt.
    if (DstTy == MRI.getType(Src))
      return false;

    // Collapse: G_ZEXT(G_ZEXT inner) -> G_ZEXT inner from original type to
    // final type.
    if (!MRI.hasOneNonDBGUse(Src))
      return false;

    MatchInfo = InnerSrc;
    return true;
  }
  return false;
}

// Apply double-zext fold: replace with single G_ZEXT from original source.
void applyFoldDoubleZExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                         MachineIRBuilder &Builder,
                         GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  Observer.erasingInstr(MI);
  Builder.buildZExt(Dst, MatchInfo);
  MI.eraseFromParent();
}

// Match G_SEXT(G_SEXT x) where cascading sign-extensions can be collapsed
// into a single sign-extension from the original source to the final type.
// Pattern: dst = G_SEXT(G_SEXT x) from TyA->TyB->TyC -> G_SEXT x TyA->TyC.
// The inner sext sign-extends from TyA. The outer sext preserves the
// sign-extension from TyB to TyC. Combined: sext TyA->TyC.
// Only profitable if the inner sext has a single use.
bool matchFoldDoubleSExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                         Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_SEXT);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  LLT DstTy = MRI.getType(Dst);

  Register InnerSrc;
  if (mi_match(Src, MRI, m_GSExt(m_Reg(InnerSrc)))) {
    // If dest type == source type, it's a no-op handled by matchRedundantSExt.
    if (DstTy == MRI.getType(Src))
      return false;

    // Collapse: G_SEXT(G_SEXT inner) -> G_SEXT inner from original type to
    // final type.
    if (!MRI.hasOneNonDBGUse(Src))
      return false;

    MatchInfo = InnerSrc;
    return true;
  }
  return false;
}

// Apply double-sext fold: replace with single G_SEXT from original source.
void applyFoldDoubleSExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                         MachineIRBuilder &Builder,
                         GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  Observer.erasingInstr(MI);
  Builder.buildSExt(Dst, MatchInfo);
  MI.eraseFromParent();
}

// Match G_ADD x, G_SUB(0, y) -> G_SUB x, y.
// Replaces addition of a negated value with subtraction.
bool matchAddOfNeg(MachineInstr &MI, MachineRegisterInfo &MRI,
                   std::pair<Register, Register> &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_ADD);
  Register Dst = MI.getOperand(0).getReg();
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  // Check if Op2 = G_SUB(0, y)
  auto *SubMI = getOpcodeDef(TargetOpcode::G_SUB, Op2, MRI);
  if (!SubMI)
    return false;
  auto SubOp1V = getIConstantVRegValWithLookThrough(
      SubMI->getOperand(1).getReg(), MRI);
  if (!SubOp1V || !SubOp1V->Value.isZero())
    return false;

  // Only profitable if the G_SUB has a single use (otherwise we'd duplicate it).
  if (!MRI.hasOneNonDBGUse(Op2))
    return false;

  MatchInfo = {Op1, SubMI->getOperand(2).getReg()};
  return true;
}

// Apply add-of-neg: replace G_ADD x, G_SUB(0, y) with G_SUB x, y.
void applyAddOfNeg(MachineInstr &MI, MachineRegisterInfo &MRI,
                   MachineIRBuilder &Builder,
                   GISelChangeObserver &Observer,
                   std::pair<Register, Register> &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::G_SUB));
  MI.getOperand(1).setReg(MatchInfo.first);
  MI.getOperand(2).setReg(MatchInfo.second);
  Observer.changedInstr(MI);
}

// Match G_SUB x, G_SUB(0, y) -> G_ADD x, y.
// Replaces subtraction of a negated value with addition.
bool matchSubOfNeg(MachineInstr &MI, MachineRegisterInfo &MRI,
                   std::pair<Register, Register> &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_SUB);
  Register Dst = MI.getOperand(0).getReg();
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  auto *SubMI = getOpcodeDef(TargetOpcode::G_SUB, Op2, MRI);
  if (!SubMI)
    return false;
  auto SubOp1V = getIConstantVRegValWithLookThrough(
      SubMI->getOperand(1).getReg(), MRI);
  if (!SubOp1V || !SubOp1V->Value.isZero())
    return false;

  if (!MRI.hasOneNonDBGUse(Op2))
    return false;

  MatchInfo = {Op1, SubMI->getOperand(2).getReg()};
  return true;
}

// Apply sub-of-neg: replace G_SUB x, G_SUB(0, y) with G_ADD x, y.
void applySubOfNeg(MachineInstr &MI, MachineRegisterInfo &MRI,
                   MachineIRBuilder &Builder,
                   GISelChangeObserver &Observer,
                   std::pair<Register, Register> &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::G_ADD));
  MI.getOperand(1).setReg(MatchInfo.first);
  MI.getOperand(2).setReg(MatchInfo.second);
  Observer.changedInstr(MI);
}

// Match G_MUL x, 1 -> x. Multiplication by 1 is identity.
bool matchMulByOne(MachineInstr &MI, MachineRegisterInfo &MRI,
                   Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_MUL);
  Register Dst = MI.getOperand(0).getReg();
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  unsigned BW = Ty.getSizeInBits();
  // Check either operand for constant 1.
  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (V2 && V2->Value.zextOrTrunc(BW).isOne()) {
    MatchInfo = Op1;
    return true;
  }
  auto V1 = getIConstantVRegValWithLookThrough(Op1, MRI);
  if (V1 && V1->Value.zextOrTrunc(BW).isOne()) {
    MatchInfo = Op2;
    return true;
  }
  return false;
}

// Apply mul-by-one: replace G_MUL x, 1 with COPY of x.
void applyMulByOne(MachineInstr &MI, MachineRegisterInfo &MRI,
                   MachineIRBuilder &Builder,
                   GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_MUL x, -1 -> G_SUB 0, x.
// Canonicalizes multiplication by -1 to negation.
bool matchMulByNegOne(MachineInstr &MI, MachineRegisterInfo &MRI,
                      Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_MUL);
  Register Dst = MI.getOperand(0).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (!V2)
    return false;

  unsigned BitWidth = Ty.getSizeInBits();
  APInt NegOne = APInt::getAllOnes(BitWidth);
  if (V2->Value.sextOrTrunc(BitWidth) != NegOne)
    return false;

  MatchInfo = MI.getOperand(1).getReg();
  return true;
}

// Apply mul-by-neg-one: replace G_MUL x, -1 with G_SUB 0, x.
void applyMulByNegOne(MachineInstr &MI, MachineRegisterInfo &MRI,
                      MachineIRBuilder &Builder,
                      GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);
  Register Zero = Builder.buildConstant(Ty, 0).getReg(0);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::G_SUB));
  MI.getOperand(1).setReg(Zero);
  MI.getOperand(2).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_ADD(G_ADD x, C1), C2 -> G_ADD x, (C1+C2).
// Folds a chain of constant additions into a single add with the combined
// constant. Only applies when the inner G_ADD has a single use (otherwise
// we'd duplicate the inner add).
bool matchAddConstChain(MachineInstr &MI, MachineRegisterInfo &MRI,
                        std::pair<Register, APInt> &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_ADD);
  Register Dst = MI.getOperand(0).getReg();
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  // Outer operand must be constant.
  auto OuterC = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (!OuterC)
    return false;

  // Inner must be G_ADD with a constant.
  auto *InnerAdd = getOpcodeDef(TargetOpcode::G_ADD, Op1, MRI);
  if (!InnerAdd)
    return false;

  // Inner G_ADD must have a single use (otherwise we'd duplicate it).
  if (!MRI.hasOneNonDBGUse(Op1))
    return false;

  Register InnerOp2 = InnerAdd->getOperand(2).getReg();
  auto InnerC = getIConstantVRegValWithLookThrough(InnerOp2, MRI);
  if (!InnerC)
    return false;

  // Fold: C1 + C2 (normalize widths — look-through can surface unequal APInts).
  unsigned BW = Ty.getSizeInBits();
  APInt FoldedC =
      InnerC->Value.sextOrTrunc(BW) + OuterC->Value.sextOrTrunc(BW);
  Register Base = InnerAdd->getOperand(1).getReg();
  MatchInfo = {Base, FoldedC};
  return true;
}

// Apply add-constant-chain: replace G_ADD(G_ADD x, C1), C2 with
// G_ADD x, (C1+C2). If the folded constant is zero, replace with COPY.
void applyAddConstChain(MachineInstr &MI, MachineRegisterInfo &MRI,
                        MachineIRBuilder &Builder,
                        GISelChangeObserver &Observer,
                        std::pair<Register, APInt> &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);

  if (MatchInfo.second.isZero()) {
    // x + 0 == x -> COPY
    Observer.changingInstr(MI);
    MI.setDesc(
        MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
    while (MI.getNumOperands() > 2)
      MI.removeOperand(MI.getNumOperands() - 1);
    MI.getOperand(1).setReg(MatchInfo.first);
    Observer.changedInstr(MI);
  } else {
    // Rebuild with folded constant.
    Register FoldedConst =
        Builder.buildConstant(Ty, MatchInfo.second).getReg(0);
    Observer.erasingInstr(MI);
    Builder.buildAdd(Dst, MatchInfo.first, FoldedConst);
    MI.eraseFromParent();
  }
}

// Match redundant G_SEXT_INREG when the value is already sign-extended
// by a G_SEXT from the same or smaller type.
// Pattern: G_SEXT_INREG(G_SEXT x, K) where the sext already guarantees
// the high bits.
bool matchSExtInRegOfSExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                          Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_SEXT_INREG);
  Register Src = MI.getOperand(1).getReg();
  int64_t Imm = MI.getOperand(2).getImm();

  Register InnerSrc;
  if (mi_match(Src, MRI, m_GSExt(m_Reg(InnerSrc)))) {
    LLT InnerSrcTy = MRI.getType(InnerSrc);
    // If the inner sext already extended from a type >= the sext_inreg width
    // the sext_inreg is redundant.
    if (InnerSrcTy.getSizeInBits() >= static_cast<unsigned>(Imm)) {
      MatchInfo = Src;
      return true;
    }
  }
  return false;
}

// Apply sext_inreg of sext: replace with COPY.
void applySExtInRegOfSExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                          MachineIRBuilder &Builder,
                          GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

//===----------------------------------------------------------------------===//
// Bit manipulation simplification combines.
//
// These patterns target common DSP and CoreMark idioms: bit-field extraction
// sign/zero extension simplification, and constant folding of bitwise ops.
//===----------------------------------------------------------------------===//

// Match XOR cancellation: (A ^ C1) ^ C2 -> A ^ (C1^C2).
// Folds two XOR-of-constant sequences into a single XOR with a folded
// constant. If the folded constant is zero, the XOR is eliminated entirely.
bool matchXorXorConstantFold(MachineInstr &MI, MachineRegisterInfo &MRI,
                             std::pair<Register, APInt> &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_XOR);
  Register Dst = MI.getOperand(0).getReg();
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  // Look for (A ^ C1) ^ C2 where C1 and C2 are constants.
  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (!V2)
    return false;

  auto *InnerXor = getOpcodeDef(TargetOpcode::G_XOR, Op1, MRI);
  if (!InnerXor)
    return false;

  Register InnerOp1 = InnerXor->getOperand(1).getReg();
  Register InnerOp2 = InnerXor->getOperand(2).getReg();

  // Inner XOR must have one constant operand.
  auto InnerV2 = getIConstantVRegValWithLookThrough(InnerOp2, MRI);
  if (!InnerV2)
    return false;

  // Fold the two constants: C1 ^ C2.
  unsigned BW = Ty.getSizeInBits();
  APInt FoldedC =
      InnerV2->Value.zextOrTrunc(BW) ^ V2->Value.zextOrTrunc(BW);
  Register InnerVar = InnerOp1;
  MatchInfo = {InnerVar, FoldedC};
  return true;
}

// Apply XOR cancellation: replace (A ^ C1) ^ C2 with A ^ (C1^C2).
// If the folded constant is zero, replace with COPY.
void applyXorXorConstantFold(MachineInstr &MI, MachineRegisterInfo &MRI,
                             MachineIRBuilder &Builder,
                             GISelChangeObserver &Observer,
                             std::pair<Register, APInt> &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);

  if (MatchInfo.second.isZero()) {
    // A ^ 0 == A -> COPY
    Observer.changingInstr(MI);
    MI.setDesc(
        MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
    while (MI.getNumOperands() > 2)
      MI.removeOperand(MI.getNumOperands() - 1);
    MI.getOperand(1).setReg(MatchInfo.first);
    Observer.changedInstr(MI);
  } else {
    // A ^ C -> rebuild with folded constant.
    Observer.erasingInstr(MI);
    Register FoldedConst = Builder.buildConstant(Ty, MatchInfo.second).getReg(0);
    Builder.buildXor(Dst, MatchInfo.first, FoldedConst);
    MI.eraseFromParent();
  }
}

// Match double NOT: XOR(XOR(x, -1), -1) -> x.
// Two consecutive NOT operations cancel out.
bool matchDoubleNot(MachineInstr &MI, MachineRegisterInfo &MRI,
                    Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_XOR);
  Register Dst = MI.getOperand(0).getReg();
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  unsigned BitWidth = Ty.getSizeInBits();
  APInt AllOnes = APInt::getAllOnes(BitWidth);

  // Outer XOR must be with -1.
  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (!V2 || V2->Value.sextOrTrunc(BitWidth) != AllOnes)
    return false;

  // Inner must be XOR x, -1.
  auto *InnerXor = getOpcodeDef(TargetOpcode::G_XOR, Op1, MRI);
  if (!InnerXor)
    return false;

  Register InnerOp2 = InnerXor->getOperand(2).getReg();
  auto InnerV2 = getIConstantVRegValWithLookThrough(InnerOp2, MRI);
  if (!InnerV2 || InnerV2->Value.sextOrTrunc(BitWidth) != AllOnes)
    return false;

  MatchInfo = InnerXor->getOperand(1).getReg();
  return true;
}

// Apply double NOT: replace with COPY of the original value.
void applyDoubleNot(MachineInstr &MI, MachineRegisterInfo &MRI,
                    MachineIRBuilder &Builder,
                    GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match AND-OR canonicalization: (A & MaskC) | SetC where MaskC and SetC
// don't overlap (i.e., MaskC & SetC == 0). In this case the OR just sets
// bits that are already guaranteed to be zero by the AND, so we can replace
// with A | SetC (which is equivalent but potentially simpler) or fold into
// (A | SetC) & MaskC (if MaskC is a superset).
// Simplification: when MaskC | SetC == MaskC|SetC (no overlap), result is
// (A & MaskC) | SetC -> (A | SetC) & (MaskC | SetC).
// But the simplest useful form: if MaskC | SetC is all-ones, this simplifies
// to (A | SetC). If MaskC has no overlap with SetC, the result is correct.
bool matchAndOrDisjoint(MachineInstr &MI, MachineRegisterInfo &MRI,
                        std::tuple<Register, APInt, APInt> &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_OR);
  Register Dst = MI.getOperand(0).getReg();
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  // Op2 must be a constant.
  auto CV2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (!CV2)
    return false;

  // Op1 must be G_AND(x, mask).
  auto *AndMI = getOpcodeDef(TargetOpcode::G_AND, Op1, MRI);
  if (!AndMI)
    return false;

  Register AndOp2 = AndMI->getOperand(2).getReg();
  auto CVAnd = getIConstantVRegValWithLookThrough(AndOp2, MRI);
  if (!CVAnd)
    return false;

  unsigned BW = Ty.getSizeInBits();
  APInt MaskC = CVAnd->Value.zextOrTrunc(BW);
  APInt SetC = CV2->Value.zextOrTrunc(BW);

  // Check that the mask and set constants are disjoint (no overlapping bits).
  if ((MaskC & SetC) != 0)
    return false;

  // Only simplify if SetC is non-zero (otherwise it's just the AND).
  if (SetC.isZero())
    return false;

  // Check that (MaskC | SetC) is all-ones, meaning (A & MaskC) | SetC covers
  // all bits. In this case, the result simplifies to (A | SetC).
  APInt AllOnes = APInt::getAllOnes(BW);
  if ((MaskC | SetC) != AllOnes)
    return false;

  // Only profitable if the AND has a single use.
  if (!MRI.hasOneNonDBGUse(Op1))
    return false;

  Register A = AndMI->getOperand(1).getReg();
  MatchInfo = {A, MaskC, SetC};
  return true;
}

// Apply AND-OR disjoint simplification: (A & MaskC) | SetC -> A | SetC.
void applyAndOrDisjoint(MachineInstr &MI, MachineRegisterInfo &MRI,
                        MachineIRBuilder &Builder,
                        GISelChangeObserver &Observer,
                        std::tuple<Register, APInt, APInt> &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);

  // Build: SetC constant, then OR.
  Register SetCReg = Builder.buildConstant(Ty, std::get<2>(MatchInfo)).getReg(0);
  Observer.erasingInstr(MI);
  Builder.buildOr(Dst, std::get<0>(MatchInfo), SetCReg);
  MI.eraseFromParent();
}

// Match shift-mask simplification: (x >> C) & mask where the mask covers
// exactly the bits that could be nonzero after the shift.
// For logical right shift: (x >> C) & ((1 << C2) - 1) where C2 == bitwidth-C.
// If the mask is all-ones (covers full width), the AND is redundant.
bool matchShiftMaskRedundant(MachineInstr &MI, MachineRegisterInfo &MRI,
                             Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_AND);
  Register Dst = MI.getOperand(0).getReg();
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;
  unsigned BitWidth = Ty.getSizeInBits();

  // Mask must be a constant.
  auto CV = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (!CV)
    return false;

  APInt Mask = CV->Value.zextOrTrunc(BitWidth);

  // Check if the LHS is G_LSHR(x, C).
  // Note: ASHR is excluded because it sign-extends — the AND with low-bit
  // mask is not redundant when high bits are sign bits, not zero.
  auto *ShiftMI = getOpcodeDef(TargetOpcode::G_LSHR, Op1, MRI);
  if (!ShiftMI)
    return false;

  // Shift amount must be a constant.
  auto ShiftAmtV = getIConstantVRegValWithLookThrough(
      ShiftMI->getOperand(2).getReg(), MRI);
  if (!ShiftAmtV)
    return false;

  uint64_t ShiftAmt = ShiftAmtV->Value.getZExtValue();
  if (ShiftAmt == 0 || ShiftAmt >= BitWidth)
    return false;

  // Compute the expected mask after the shift: (1 << (BitWidth - ShiftAmt)) - 1
  // For LSHR, this is the bits that remain. For ASHR, high bits are sign bits.
  // The mask is redundant if it covers all the bits that the shift could produce.
  APInt ExpectedMask = APInt::getLowBitsSet(BitWidth, BitWidth - ShiftAmt);

  if (Mask != ExpectedMask)
    return false;

  // Only simplify if the shift has a single use.
  if (!MRI.hasOneNonDBGUse(Op1))
    return false;

  MatchInfo = Op1;
  return true;
}

// Apply shift-mask simplification: replace G_AND(shift, mask) with the shift.
void applyShiftMaskRedundant(MachineInstr &MI, MachineRegisterInfo &MRI,
                             MachineIRBuilder &Builder,
                             GISelChangeObserver &Observer,
                             Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match XOR-of-constant with zero constant (covers XOR x, 0 after folding).
// This is a general constant-fold for G_XOR with a single constant operand
// where the result is the non-constant operand (i.e., the constant is zero).
// Pattern: G_XOR x, 0 -> x (identity).
bool matchXorZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                  Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_XOR);
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();

  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (V2 && V2->Value.isZero()) {
    MatchInfo = Op1;
    return true;
  }
  auto V1 = getIConstantVRegValWithLookThrough(Op1, MRI);
  if (V1 && V1->Value.isZero()) {
    MatchInfo = Op2;
    return true;
  }
  return false;
}

// Apply XOR with zero: replace with COPY.
void applyXorZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                  MachineIRBuilder &Builder,
                  GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match `G_XOR %bool, G_CONSTANT -1` (bitwise NOT) where `%bool` is a known
// 0-or-1 value (a widened i1). The Legalizer widens `xor i1 %x, true` to
// `G_XOR (anyext %x), G_CONSTANT i32 -1`, which is a 32-bit bitwise NOT:
// it maps 0 -> -1 and 1 -> -2. Neither result is zero, so a later `BNEZ`
// (the Haydn G_BRCOND lowering, which tests the full s32 register non-zero)
// always branches — regardless of the i1 value. This is the root cause of
// the BST insert loop's `placed` flag never tests as zero, so the
// loop spins forever.
// When the non-constant operand is known-boolean, rewrite the all-ones
// constant to `1`, turning the operation into a logical NOT (0 -> 1, 1 -> 0)
// that is correct under both BNEZ (non-zero) and bit-0 tests. This preserves
// the i1 XOR-with-true semantics that the IR intended.
namespace {
// Walk the def chain to determine if `R` is guaranteed to hold a 0-or-1 value
// (a widened i1 boolean). Recognizes the common boolean producers that the
// Legalizer emits when widening s1 operations to s32:
// G_ANYEXT / G_ZEXT of an s1 (upper bits are 0, so value is 0 or 1)
// G_TRUNC of a larger type that is itself boolean (recursing once)
// G_SELECT %cond, %a, %b where both %a and %b are boolean
// G_ICMP / G_FCMP (always 0-or-1)
// G_CONSTANT 0 or 1.
// (G_SEXT of an s1 is NOT boolean: sext of i1 1 = -1, which has upper bits set.)
bool isBooleanRegister(Register R, MachineRegisterInfo &MRI, unsigned Depth = 0) {
  if (!R.isValid() || !R.isVirtual())
    return false;
  if (Depth > 4)
    return false;
  MachineInstr *Def = MRI.getVRegDef(R);
  if (!Def)
    return false;
  switch (Def->getOpcode()) {
  case TargetOpcode::G_ICMP:
  case TargetOpcode::G_FCMP:
    return true;
  case TargetOpcode::G_CONSTANT: {
    auto V = getIConstantVRegValWithLookThrough(R, MRI);
    return V && (V->Value.isZero() || V->Value.isOne());
  }
  case TargetOpcode::G_ANYEXT:
  case TargetOpcode::G_ZEXT: {
    Register Src = Def->getOperand(1).getReg();
    LLT SrcTy = MRI.getType(Src);
    // anyext/zext of an s1 fills upper bits with 0 -> value is 0 or 1.
    if (SrcTy.isScalar() && SrcTy.getSizeInBits() == 1)
      return true;
    // anyext/zext of a larger boolean-typed value: recurse.
    return isBooleanRegister(Src, MRI, Depth + 1);
  }
  case TargetOpcode::G_TRUNC: {
    Register Src = Def->getOperand(1).getReg();
    return isBooleanRegister(Src, MRI, Depth + 1);
  }
  case TargetOpcode::G_SELECT: {
    Register OpA = Def->getOperand(2).getReg();
    Register OpB = Def->getOperand(3).getReg();
    return isBooleanRegister(OpA, MRI, Depth + 1) &&
           isBooleanRegister(OpB, MRI, Depth + 1);
  }
  default:
    return false;
  }
}
} // namespace

bool matchXorAllOnesBoolean(MachineInstr &MI, MachineRegisterInfo &MRI,
                            bool &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_XOR);
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  unsigned BitWidth = Ty.getSizeInBits();
  APInt AllOnes = APInt::getAllOnes(BitWidth);

  // Find the all-ones constant operand; the other operand is the candidate.
  Register Other = Register();
  for (unsigned I = 1; I <= 2; ++I) {
    Register Op = MI.getOperand(I).getReg();
    auto V = getIConstantVRegValWithLookThrough(Op, MRI);
    if (V && V->Value.sextOrTrunc(BitWidth) == AllOnes) {
      Other = MI.getOperand(I == 1 ? 2 : 1).getReg();
      break;
    }
  }
  if (!Other.isValid())
    return false;

  if (!isBooleanRegister(Other, MRI))
    return false;

  // the rewrite `xor X, -1` (bitwise NOT) -> `xor (and X, 1), 1`
  // (logical NOT) is sound ONLY when X has UNDEF upper bits — i.e. X is
  // G_ANYEXT of an i1, the legalizer's widening of an i1 `xor %x, true`
  // (: the BST-insert loop-exit `while (placed == 0)`; also `zext i1
  // (xor i1 %c, true)` value uses, where the i1 xor widens via anyext). For
  // anyext X, `xor X, -1` has undef upper bits and (under the Haydn non-zero
  // branch test) never reads as false, so masking to a clean logical NOT is
  // required.
  //
  // For a CLEAN boolean X — G_ZEXT/G_SEXT of an i1, or a setcc (known 0/1
  // with clean upper bits) — `xor X, -1` is a WELL-DEFINED bitwise NOT
  // (-1 / -2) that VALUE uses require. Canonical repro:
  // int f(int a,int b){ return ~(a>b); } / C `~` on the int (a>b): zext icmp
  // must yield -2 (254 as unsigned char) for a>b; the rewrite to logical NOT
  // (0/1) mis-compiles it (: host 254 -> sim 0). The earlier "branch-only
  // Dst users" gate was WRONG: it skipped legitimate anyext value uses
  // (xor-i1-bool-loop-exit.ll's `zext(xor i1 %c, true) -> ret`), regressing
  // The correct discriminator is the SOURCE: fire iff X is G_ANYEXT.
  MachineInstr *OtherDef = MRI.getVRegDef(Other);
  if (!OtherDef || OtherDef->getOpcode() != TargetOpcode::G_ANYEXT)
    return false;

  MatchInfo = true;
  return true;
}

// Apply: rewrite the all-ones constant operand of the XOR to 1, turning the
// bitwise NOT into a logical NOT on the boolean operand.
void applyXorAllOnesBoolean(MachineInstr &MI, MachineRegisterInfo &MRI,
                            MachineIRBuilder &Builder,
                            GISelChangeObserver &Observer, bool &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);

  // Locate the all-ones constant operand and the other (boolean) operand.
  unsigned AllOnesIdx = 0;
  for (unsigned I = 1; I <= 2; ++I) {
    auto V = getIConstantVRegValWithLookThrough(MI.getOperand(I).getReg(), MRI);
    if (V && V->Value.isAllOnes()) {
      AllOnesIdx = I;
      break;
    }
  }
  if (!AllOnesIdx)
    return;
  unsigned OtherIdx = (AllOnesIdx == 1) ? 2 : 1;
  Register Other = MI.getOperand(OtherIdx).getReg();

  // Turn `xor X, -1` into `xor (and X, 1), 1`. Masking X to a clean 0/1 makes
  // the XOR-with-1 a sound logical NOT under the non-zero branch test even when
  // X arrived via G_ANYEXT of an s1 (whose upper bits are undef — without the
  // mask the result's upper bits stay undef and the branch test is wrong). For
  // already-clean booleans (icmp/zext/select/0-or-1 const) the AND is
  // redundant but harmless.
  Register OneC =
      Builder.buildConstant(Ty, APInt(Ty.getSizeInBits(), 1)).getReg(0);
  Register Masked = Builder.buildAnd(Ty, Other, OneC).getReg(0);

  Observer.changingInstr(MI);
  MI.getOperand(OtherIdx).setReg(Masked);
  MI.getOperand(AllOnesIdx).setReg(OneC);
  Observer.changedInstr(MI);
}

// Match G_MUL x, C where C is a positive power of 2.
// Replaces multiplication with left shift: G_SHL x, log2(C).
// Strength reduction: shift is cheaper than multiply on most targets
// and Haydn has no hardware multiply in the base ALU slot.
bool matchMulToShift(MachineInstr &MI, MachineRegisterInfo &MRI,
                     std::pair<Register, uint64_t> &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_MUL);
  Register Dst = MI.getOperand(0).getReg();
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar() || Ty.getSizeInBits() != 32)
    return false;

  // Check either operand for a power-of-2 constant.
  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (V2 && V2->Value.isPowerOf2()) {
    // G_MUL x, pow2 -> G_SHL x, log2(pow2)
    MatchInfo = {Op1, V2->Value.exactLogBase2()};
    return true;
  }

  auto V1 = getIConstantVRegValWithLookThrough(Op1, MRI);
  if (V1 && V1->Value.isPowerOf2()) {
    // G_MUL pow2, x -> G_SHL x, log2(pow2) (commutative)
    MatchInfo = {Op2, V1->Value.exactLogBase2()};
    return true;
  }

  return false;
}

// Apply mul-to-shift: replace G_MUL x, C with G_SHL x, log2(C).
void applyMulToShift(MachineInstr &MI, MachineRegisterInfo &MRI,
                     MachineIRBuilder &Builder,
                     GISelChangeObserver &Observer,
                     std::pair<Register, uint64_t> &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);

  Register ShiftAmt = Builder.buildConstant(Ty, MatchInfo.second).getReg(0);
  Observer.erasingInstr(MI);
  Builder.buildShl(Dst, MatchInfo.first, ShiftAmt);
  MI.eraseFromParent();
}

// Match G_MUL x, C where C is (pow2 + 1) for s32 types.
// Replaces multiplication with shift-add: G_ADD (G_SHL x, log2(pow2)), x.
// Example: x * 9 = (x << 3) + x.
// Only applies to s32 since s64 mul is already lowered to a libcall.
bool matchMulToShiftAdd(MachineInstr &MI, MachineRegisterInfo &MRI,
                        std::pair<Register, uint64_t> &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_MUL);
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar() || Ty.getSizeInBits() != 32)
    return false;

  // Check either operand for a (pow2 + 1) constant.
  for (unsigned Idx = 1; Idx <= 2; ++Idx) {
    Register ConstOp = MI.getOperand(Idx).getReg();
    Register VarOp = MI.getOperand(3 - Idx).getReg();
    auto CV = getIConstantVRegValWithLookThrough(ConstOp, MRI);
    if (!CV)
      continue;

    // Normalize to s32 result width (look-through can surface narrower APInts).
    APInt C = CV->Value.zextOrTrunc(32);

    // C must be > 2 (1 is identity, 2 is pow2 handled by mul_to_shift).
    // Use ugt to avoid signed interpretation issues with negative constants.
    if (!C.ugt(2))
      continue;

    // Check if C is (pow2 + 1): C - 1 must be a power of 2.
    APInt Dec = C - 1;
    if (!Dec.isPowerOf2())
      continue;

    unsigned Log2 = Dec.exactLogBase2();
    MatchInfo = {VarOp, Log2};
    return true;
  }
  return false;
}

// Apply mul-to-shift-add: replace G_MUL x, (pow2+1) with
// G_ADD (G_SHL x, log2(pow2)), x.
void applyMulToShiftAdd(MachineInstr &MI, MachineRegisterInfo &MRI,
                        MachineIRBuilder &Builder,
                        GISelChangeObserver &Observer,
                        std::pair<Register, uint64_t> &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);

  Register ShiftAmt = Builder.buildConstant(Ty, MatchInfo.second).getReg(0);
  Register Shifted = Builder.buildShl(Ty, MatchInfo.first, ShiftAmt).getReg(0);
  Observer.erasingInstr(MI);
  Builder.buildAdd(Dst, Shifted, MatchInfo.first);
  MI.eraseFromParent();
}

// Match G_MUL x, C where C is (pow2 - 1) for s32 types.
// Replaces multiplication with shift-sub: G_SUB (G_SHL x, log2(pow2)), x.
// Example: x * 7 = (x << 3) - x.
// Only applies to s32 since s64 mul is already lowered to a libcall.
bool matchMulToShiftSub(MachineInstr &MI, MachineRegisterInfo &MRI,
                        std::pair<Register, uint64_t> &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_MUL);
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar() || Ty.getSizeInBits() != 32)
    return false;

  // Check either operand for a (pow2 - 1) constant.
  for (unsigned Idx = 1; Idx <= 2; ++Idx) {
    Register ConstOp = MI.getOperand(Idx).getReg();
    Register VarOp = MI.getOperand(3 - Idx).getReg();
    auto CV = getIConstantVRegValWithLookThrough(ConstOp, MRI);
    if (!CV)
      continue;

    // Normalize to s32 (look-through can surface narrower APInts —).
    APInt C = CV->Value.zextOrTrunc(32);
    // C must be > 1 (1 is identity). Use ugt to handle all values correctly.
    if (!C.ugt(1))
      continue;

    // Check if C is (pow2 - 1): C + 1 must be a power of 2.
    // For C = 0xFFFFFFFF (i.e., -1), C+1 wraps to 0, which is NOT power of 2.
    APInt Inc = C + 1;
    if (!Inc.isPowerOf2())
      continue;

    unsigned Log2 = Inc.exactLogBase2();
    MatchInfo = {VarOp, Log2};
    return true;
  }
  return false;
}

// Apply mul-to-shift-sub: replace G_MUL x, (pow2-1) with
// G_SUB (G_SHL x, log2(pow2)), x.
void applyMulToShiftSub(MachineInstr &MI, MachineRegisterInfo &MRI,
                        MachineIRBuilder &Builder,
                        GISelChangeObserver &Observer,
                        std::pair<Register, uint64_t> &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);

  Register ShiftAmt = Builder.buildConstant(Ty, MatchInfo.second).getReg(0);
  Register Shifted = Builder.buildShl(Ty, MatchInfo.first, ShiftAmt).getReg(0);
  Observer.erasingInstr(MI);
  Builder.buildSub(Dst, Shifted, MatchInfo.first);
  MI.eraseFromParent();
}

// Match redundant sign-extend via shifts: if x was obtained by arithmetic
// right shift of C bits and we sext_inreg to (BitWidth-C) bits, the
// sext_inreg is redundant because ASHR already sign-extended.
// Pattern: G_SEXT_INREG(G_ASHR x, C), BitWidth-C where the sext width
// equals BitWidth-C.
bool matchSExtInRegOfAShr(MachineInstr &MI, MachineRegisterInfo &MRI,
                          Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_SEXT_INREG);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  int64_t Imm = MI.getOperand(2).getImm();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;
  unsigned BitWidth = Ty.getSizeInBits();

  // Check if Src is G_ASHR(x, C).
  auto *AshrMI = getOpcodeDef(TargetOpcode::G_ASHR, Src, MRI);
  if (!AshrMI)
    return false;

  auto ShiftAmtV = getIConstantVRegValWithLookThrough(
      AshrMI->getOperand(2).getReg(), MRI);
  if (!ShiftAmtV)
    return false;

  uint64_t ShiftAmt = ShiftAmtV->Value.getZExtValue();
  // sext_inreg width == BitWidth - ShiftAmt means the ASHR already produced
  // a sign-extended value of exactly that width.
  if (static_cast<unsigned>(Imm) != BitWidth - ShiftAmt)
    return false;

  // Only profitable if the ashr has a single use.
  if (!MRI.hasOneNonDBGUse(Src))
    return false;

  MatchInfo = Src;
  return true;
}

// Apply sext_inreg of ashr: replace with COPY.
void applySExtInRegOfAShr(MachineInstr &MI, MachineRegisterInfo &MRI,
                          MachineIRBuilder &Builder,
                          GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_AND x, -1 -> x (AND with all-ones is identity).
bool matchAndAllOnes(MachineInstr &MI, MachineRegisterInfo &MRI,
                     Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_AND);
  Register Dst = MI.getOperand(0).getReg();
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;
  unsigned BitWidth = Ty.getSizeInBits();
  APInt AllOnes = APInt::getAllOnes(BitWidth);

  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (V2 && V2->Value.sextOrTrunc(BitWidth) == AllOnes) {
    MatchInfo = Op1;
    return true;
  }
  auto V1 = getIConstantVRegValWithLookThrough(Op1, MRI);
  if (V1 && V1->Value.sextOrTrunc(BitWidth) == AllOnes) {
    MatchInfo = Op2;
    return true;
  }
  return false;
}

// Apply G_AND x, -1: replace with COPY of the non-constant operand.
void applyAndAllOnes(MachineInstr &MI, MachineRegisterInfo &MRI,
                     MachineIRBuilder &Builder,
                     GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_OR x, 0 -> x (OR with zero is identity).
bool matchOrZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                 Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_OR);
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();

  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (V2 && V2->Value.isZero()) {
    MatchInfo = Op1;
    return true;
  }
  auto V1 = getIConstantVRegValWithLookThrough(Op1, MRI);
  if (V1 && V1->Value.isZero()) {
    MatchInfo = Op2;
    return true;
  }
  return false;
}

// Apply G_OR x, 0: replace with COPY of the non-constant operand.
void applyOrZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                 MachineIRBuilder &Builder,
                 GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_ADD x, 0 -> x (ADD with zero is identity).
bool matchAddZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                  Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_ADD);
  Register Dst = MI.getOperand(0).getReg();
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (V2 && V2->Value.isZero()) {
    MatchInfo = Op1;
    return true;
  }
  auto V1 = getIConstantVRegValWithLookThrough(Op1, MRI);
  if (V1 && V1->Value.isZero()) {
    MatchInfo = Op2;
    return true;
  }
  return false;
}

// Apply G_ADD x, 0: replace with COPY of the non-constant operand.
void applyAddZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                  MachineIRBuilder &Builder,
                  GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_SUB x, 0 -> x (SUB with zero RHS is identity).
bool matchSubZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                  Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_SUB);
  Register Dst = MI.getOperand(0).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (V2 && V2->Value.isZero()) {
    MatchInfo = MI.getOperand(1).getReg();
    return true;
  }
  return false;
}

// Apply G_SUB x, 0: replace with COPY of the LHS operand.
void applySubZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                  MachineIRBuilder &Builder,
                  GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_SHL x, 0 -> x (shift by zero is identity).
bool matchShlZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                  Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_SHL);
  Register Dst = MI.getOperand(0).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (V2 && V2->Value.isZero()) {
    MatchInfo = MI.getOperand(1).getReg();
    return true;
  }
  return false;
}

// Apply G_SHL x, 0: replace with COPY of the value operand.
void applyShlZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                  MachineIRBuilder &Builder,
                  GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_LSHR x, 0 -> x (logical shift right by zero is identity).
bool matchLshrZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                   Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_LSHR);
  Register Dst = MI.getOperand(0).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (V2 && V2->Value.isZero()) {
    MatchInfo = MI.getOperand(1).getReg();
    return true;
  }
  return false;
}

// Apply G_LSHR x, 0: replace with COPY of the value operand.
void applyLshrZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                   MachineIRBuilder &Builder,
                   GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_ASHR x, 0 -> x (arithmetic shift right by zero is identity).
bool matchAshrZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                   Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_ASHR);
  Register Dst = MI.getOperand(0).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (V2 && V2->Value.isZero()) {
    MatchInfo = MI.getOperand(1).getReg();
    return true;
  }
  return false;
}

// Apply G_ASHR x, 0: replace with COPY of the value operand.
void applyAshrZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                   MachineIRBuilder &Builder,
                   GISelChangeObserver &Observer, Register &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Observer.changingInstr(MI);
  MI.setDesc(
      MI.getMF()->getSubtarget().getInstrInfo()->get(TargetOpcode::COPY));
  while (MI.getNumOperands() > 2)
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.getOperand(1).setReg(MatchInfo);
  Observer.changedInstr(MI);
}

// Match G_MUL x, 0 -> 0 (multiply by zero is always zero).
bool matchMulZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                  APInt &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_MUL);
  Register Dst = MI.getOperand(0).getReg();
  Register Op1 = MI.getOperand(1).getReg();
  Register Op2 = MI.getOperand(2).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;
  unsigned BW = Ty.getSizeInBits();

  auto V2 = getIConstantVRegValWithLookThrough(Op2, MRI);
  if (V2 && V2->Value.isZero()) {
    MatchInfo = APInt::getZero(BW);
    return true;
  }
  auto V1 = getIConstantVRegValWithLookThrough(Op1, MRI);
  if (V1 && V1->Value.isZero()) {
    MatchInfo = APInt::getZero(BW);
    return true;
  }
  return false;
}

// Apply G_MUL x, 0: replace with constant 0.
void applyMulZero(MachineInstr &MI, MachineRegisterInfo &MRI,
                  MachineIRBuilder &Builder,
                  GISelChangeObserver &Observer, APInt &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  Builder.buildConstant(Dst, MatchInfo);
  Observer.erasingInstr(MI);
  MI.eraseFromParent();
}

// Select-to-minmax combine result: holds the target generic opcode to replace
// with, plus the two source operands for the min/max.
struct SelectMinMaxMatchInfo {
  unsigned MinMaxOpcode; // G_SMAX, G_SMIN, G_UMAX, or G_UMIN
  Register OpA;         // First min/max operand
  Register OpB;         // Second min/max operand
};

// Match G_SELECT(G_ICMP(pred, a, b), x, y) where the select is equivalent
// to a min or max operation.
// Patterns detected:
// select (a > b), a, b -> smax(a, b)
// select (a > b), b, a -> smin(a, b)
// select (a < b), a, b -> smin(a, b)
// select (a < b), b, a -> smax(a, b)
// Same for unsigned variants with ugt/ult -> umax/umin.
// Only matches when the G_ICMP feeds directly into the G_SELECT (one use)
// and the comparison operands match the select true/false operands.
bool matchSelectToMinMax(MachineInstr &MI, MachineRegisterInfo &MRI,
                         SelectMinMaxMatchInfo &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_SELECT);
  Register Dst = MI.getOperand(0).getReg();
  Register Cond = MI.getOperand(1).getReg();
  Register TrueVal = MI.getOperand(2).getReg();
  Register FalseVal = MI.getOperand(3).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar())
    return false;

  // Only combine s32 selects — G_SMAX/G_SMIN/G_UMAX/G_UMIN are legal for s32
  // but s64 is lowered. Producing them for s64 would fail the legality check.
  if (Ty.getSizeInBits() != 32)
    return false;

  // The condition must be a G_ICMP.
  auto *CmpMI = getOpcodeDef(TargetOpcode::G_ICMP, Cond, MRI);
  if (!CmpMI)
    return false;

  // Only fold if the ICMP has a single non-debug use (the SELECT).
  if (!MRI.hasOneNonDBGUse(Cond))
    return false;

  auto Pred =
      static_cast<CmpInst::Predicate>(CmpMI->getOperand(1).getPredicate());
  Register CmpA = CmpMI->getOperand(2).getReg();
  Register CmpB = CmpMI->getOperand(3).getReg();

  // Determine the min/max opcode and operand order based on the predicate
  // and which comparison operand maps to the true/false values of the select.
  //
  // For "a > b" (SGT):
  // select(a > b, a, b) = smax(a, b) [true val is the larger]
  // select(a > b, b, a) = smin(a, b) [true val is the smaller]
  //
  // For "a < b" (SLT):
  // select(a < b, a, b) = smin(a, b) [true val is the smaller]
  // select(a < b, b, a) = smax(a, b) [true val is the larger]

  switch (Pred) {
  default:
    return false;
  case CmpInst::ICMP_SGT:
    // a > b: true=a,false=b -> smax(a,b); true=b,false=a -> smin(a,b)
    if (TrueVal == CmpA && FalseVal == CmpB) {
      MatchInfo = {TargetOpcode::G_SMAX, CmpA, CmpB};
      return true;
    }
    if (TrueVal == CmpB && FalseVal == CmpA) {
      MatchInfo = {TargetOpcode::G_SMIN, CmpB, CmpA};
      return true;
    }
    return false;
  case CmpInst::ICMP_SLT:
    // a < b: true=a,false=b -> smin(a,b); true=b,false=a -> smax(a,b)
    if (TrueVal == CmpA && FalseVal == CmpB) {
      MatchInfo = {TargetOpcode::G_SMIN, CmpA, CmpB};
      return true;
    }
    if (TrueVal == CmpB && FalseVal == CmpA) {
      MatchInfo = {TargetOpcode::G_SMAX, CmpB, CmpA};
      return true;
    }
    return false;
  case CmpInst::ICMP_UGT:
    // a >u b: true=a,false=b -> umax(a,b); true=b,false=a -> umin(a,b)
    if (TrueVal == CmpA && FalseVal == CmpB) {
      MatchInfo = {TargetOpcode::G_UMAX, CmpA, CmpB};
      return true;
    }
    if (TrueVal == CmpB && FalseVal == CmpA) {
      MatchInfo = {TargetOpcode::G_UMIN, CmpB, CmpA};
      return true;
    }
    return false;
  case CmpInst::ICMP_ULT:
    // a <u b: true=a,false=b -> umin(a,b); true=b,false=a -> umax(a,b)
    if (TrueVal == CmpA && FalseVal == CmpB) {
      MatchInfo = {TargetOpcode::G_UMIN, CmpA, CmpB};
      return true;
    }
    if (TrueVal == CmpB && FalseVal == CmpA) {
      MatchInfo = {TargetOpcode::G_UMAX, CmpB, CmpA};
      return true;
    }
    return false;
  }
}

// Apply select-to-minmax: replace G_SELECT(G_ICMP) with G_SMAX/G_SMIN/G_UMAX/G_UMIN.
void applySelectToMinMax(MachineInstr &MI, MachineRegisterInfo &MRI,
                         MachineIRBuilder &Builder,
                         GISelChangeObserver &Observer,
                         SelectMinMaxMatchInfo &MatchInfo) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();

  // The ICMP that fed the SELECT is now dead (it had one use).
  // Erase the SELECT and build the min/max instruction.
  Observer.erasingInstr(MI);
  Builder.buildInstr(MatchInfo.MinMaxOpcode, {Dst},
                     {MatchInfo.OpA, MatchInfo.OpB});
  MI.eraseFromParent();
}

//===----------------------------------------------------------------------===//
// AIE-style pre/post-inc/dec: G_LOAD/STORE + G_PTR_ADD → G_HAYDN_*INC_*
//===----------------------------------------------------------------------===//

struct HaydnIncMemInfo {
  MachineInstr *MemI = nullptr;
  MachineInstr *PtrAddI = nullptr;
  Register Data;     // load dest or store src
  Register Base;     // pointer before update
  Register NewPtr;   // G_PTR_ADD def (= writeback)
  Register OffsetReg;
  int64_t OffsetBytes = 0;
  unsigned ScaleShift = 0; // 0=byte,1=half,2=word,3=dword
  unsigned MemBytes = 0;   // access size 1/2/4/8
  bool IsSExtLoad = false; // i8/i16 signed load (LBS/LHWS)
  bool IsPost = true;
  bool IsLoad = true;
  bool IsRegStride = false; // offset is non-const reg
};

// Const byte offset from G_PTR_ADD's offset operand (G_CONSTANT / G_CONSTANT
// through copies). Returns false if not a compile-time constant.
static bool getPtrAddConstBytes(const MachineInstr &PtrAdd,
                                MachineRegisterInfo &MRI, int64_t &Bytes) {
  if (PtrAdd.getOpcode() != TargetOpcode::G_PTR_ADD)
    return false;
  Register Off = PtrAdd.getOperand(2).getReg();
  auto C = getIConstantVRegValWithLookThrough(Off, MRI);
  if (!C)
    return false;
  Bytes = C->Value.getSExtValue();
  return true;
}

static bool strideFitsScaledImm6(int64_t Bytes, unsigned ScaleShift) {
  int64_t Step = int64_t(1) << ScaleShift;
  if (Bytes % Step != 0)
    return false;
  return isInt<6>(Bytes >> ScaleShift);
}

// Access size + scale from MMO (preferred) or data LLT. Covers i8/i16/i32/i64.
static bool memAccessInfo(const MachineInstr &MemI, MachineRegisterInfo &MRI,
                          unsigned &MemBytes, unsigned &ScaleShift,
                          bool &IsSExtLoad) {
  IsSExtLoad = MemI.getOpcode() == TargetOpcode::G_SEXTLOAD;
  uint64_t Sz = 0;
  if (!MemI.memoperands_empty()) {
    auto SzOpt = (*MemI.memoperands_begin())->getSize();
    if (SzOpt.hasValue())
      Sz = SzOpt.getValue();
  }
  if (Sz == 0) {
    LLT Ty = MRI.getType(MemI.getOperand(0).getReg());
    if (!Ty.isValid() || Ty.isPointer() || Ty.isVector())
      return false;
    Sz = Ty.getSizeInBits() / 8;
  }
  switch (Sz) {
  case 1:
    MemBytes = 1;
    ScaleShift = 0;
    return true;
  case 2:
    MemBytes = 2;
    ScaleShift = 1;
    return true;
  case 4:
    MemBytes = 4;
    ScaleShift = 2;
    return true;
  case 8:
    MemBytes = 8;
    ScaleShift = 3;
    return true;
  default:
    return false;
  }
}

// True if no instruction strictly between A and B (same MBB) is a mem op or
// defs NewPtr (other than PtrAdd).
static bool gapSafeForInc(MachineInstr &A, MachineInstr &B, Register NewPtr,
                          MachineRegisterInfo &MRI) {
  if (A.getParent() != B.getParent())
    return false;
  MachineBasicBlock::iterator Begin = A.getIterator();
  MachineBasicBlock::iterator End = B.getIterator();
  if (std::distance(A.getParent()->begin(), Begin) >
      std::distance(A.getParent()->begin(), End))
    std::swap(Begin, End);
  for (auto I = std::next(Begin); I != End; ++I) {
    if (I->isDebugInstr())
      continue;
    if (I->mayLoadOrStore())
      return false;
    if (I->definesRegister(NewPtr, /*TRI=*/nullptr))
      return false;
  }
  return true;
}

// AIE CombinerHelper::checkRegUsesDominate — every non-dbg use of Reg must
// dominate Instr, except IgnoreUser (the folded G_PTR_ADD). Ensures the
// original pointer is not needed after the fused insert point.
static bool checkRegUsesDominate(Register Reg, MachineInstr &Instr,
                                 MachineInstr &IgnoreUser,
                                 MachineRegisterInfo &MRI,
                                 const CombinerHelper &Helper) {
  for (MachineInstr &Use : MRI.use_nodbg_instructions(Reg)) {
    if (&Use == &IgnoreUser)
      continue;
    if (!Helper.dominates(Use, Instr))
      return false;
  }
  return true;
}

static bool matchPostIncMem(MachineInstr &MemI, MachineRegisterInfo &MRI,
                            const CombinerHelper &Helper,
                            HaydnIncMemInfo &Info) {
  const bool IsLoad = MemI.getOpcode() == TargetOpcode::G_LOAD ||
                      MemI.getOpcode() == TargetOpcode::G_ZEXTLOAD;
  // G_SEXTLOAD: not fused (no signedness on G_HAYDN_*INC_*; would select LBU/LHWU).
  if (!IsLoad && MemI.getOpcode() != TargetOpcode::G_STORE)
    return false;

  unsigned MemBytes = 0, Scale = 0;
  bool IsSExt = false;
  if (!memAccessInfo(MemI, MRI, MemBytes, Scale, IsSExt))
    return false;
  // G_LOAD/G_ZEXTLOAD → unsigned byte/half forms at select.
  IsSExt = false;

  Register Data = MemI.getOperand(0).getReg();
  Register Base = MemI.getOperand(1).getReg();
  if (!Base.isVirtual())
    return false;
  if (IsLoad && Data == Base)
    return false;

  // Find G_PTR_ADD Base, Off that is dominated by Mem (post) and uses Base.
  for (MachineInstr &U : MRI.use_nodbg_instructions(Base)) {
    if (U.getOpcode() != TargetOpcode::G_PTR_ADD)
      continue;
    if (U.getOperand(1).getReg() != Base)
      continue;
    if (U.getParent() != MemI.getParent())
      continue;
    // Post: Mem dominates PtrAdd (Mem before PtrAdd).
    if (!Helper.dominates(MemI, U))
      continue;

    Register OffReg = U.getOperand(2).getReg();
    int64_t Bytes = 0;
    bool IsReg = !getPtrAddConstBytes(U, MRI, Bytes);
    if (IsReg && !EnableHaydnGISelRegStride)
      continue;
    if (!IsReg && !strideFitsScaledImm6(Bytes, Scale))
      continue;

    Register NewPtr = U.getOperand(0).getReg();
    if (IsLoad && Data == NewPtr)
      continue;
    if (!IsLoad && Data == NewPtr)
      continue;
    if (!gapSafeForInc(MemI, U, NewPtr, MRI))
      continue;

    // Between Mem and PtrAdd, Base should not be redefined.
    bool BaseClobbered = false;
    for (auto I = std::next(MemI.getIterator()); I != U.getIterator(); ++I) {
      if (I->modifiesRegister(Base, /*TRI=*/nullptr)) {
        BaseClobbered = true;
        break;
      }
    }
    if (BaseClobbered)
      continue;

    // REG stride must dominate Mem (we insert fused op at Mem).
    if (IsReg) {
      MachineInstr *OffDef = MRI.getVRegDef(OffReg);
      if (!OffDef || !Helper.dominates(*OffDef, MemI))
        continue;
    }

    // AIE: only combine if original pointer is not used after insert point
    // (all uses dominate insert; ignore the folded ptradd). Greedy opt-in.
    if (!EnableHaydnGISelGreedyAddr &&
        !checkRegUsesDominate(Base, MemI, /*IgnoreUser=*/U, MRI, Helper))
      continue;

    // Updated pointer must be used (otherwise fold is dead).
    if (MRI.use_nodbg_empty(NewPtr))
      continue;

    Info.MemI = &MemI;
    Info.PtrAddI = &U;
    Info.Data = Data;
    Info.Base = Base;
    Info.NewPtr = NewPtr;
    Info.OffsetReg = OffReg;
    Info.OffsetBytes = Bytes;
    Info.ScaleShift = Scale;
    Info.MemBytes = MemBytes;
    Info.IsSExtLoad = IsSExt;
    Info.IsPost = true;
    Info.IsLoad = IsLoad;
    Info.IsRegStride = IsReg;
    return true;
  }
  return false;
}

static bool matchPreIncMem(MachineInstr &MemI, MachineRegisterInfo &MRI,
                           const CombinerHelper &Helper, HaydnIncMemInfo &Info) {
  const bool IsLoad = MemI.getOpcode() == TargetOpcode::G_LOAD ||
                      MemI.getOpcode() == TargetOpcode::G_ZEXTLOAD;
  if (!IsLoad && MemI.getOpcode() != TargetOpcode::G_STORE)
    return false;

  unsigned MemBytes = 0, Scale = 0;
  bool IsSExt = false;
  if (!memAccessInfo(MemI, MRI, MemBytes, Scale, IsSExt))
    return false;
  IsSExt = false;

  Register Data = MemI.getOperand(0).getReg();
  Register MemBase = MemI.getOperand(1).getReg();
  if (!MemBase.isVirtual())
    return false;

  MachineInstr *PtrAdd = MRI.getVRegDef(MemBase);
  if (!PtrAdd || PtrAdd->getOpcode() != TargetOpcode::G_PTR_ADD)
    return false;
  if (PtrAdd->getParent() != MemI.getParent())
    return false;
  // Pre: PtrAdd dominates Mem.
  if (!Helper.dominates(*PtrAdd, MemI))
    return false;

  Register Base = PtrAdd->getOperand(1).getReg();
  Register NewPtr = PtrAdd->getOperand(0).getReg();
  if (NewPtr != MemBase)
    return false;

  Register OffReg = PtrAdd->getOperand(2).getReg();
  int64_t Bytes = 0;
  bool IsReg = !getPtrAddConstBytes(*PtrAdd, MRI, Bytes);
  if (IsReg && !EnableHaydnGISelRegStride)
    return false;
  if (!IsReg && !strideFitsScaledImm6(Bytes, Scale))
    return false;
  if (!gapSafeForInc(*PtrAdd, MemI, NewPtr, MRI))
    return false;

  // No use of NewPtr strictly between PtrAdd and Mem (PRE inserts at Mem;
  // NewPtr's def moves from PtrAdd to the fused op at Mem).
  for (auto I = std::next(PtrAdd->getIterator()); I != MemI.getIterator();
       ++I) {
    if (I->readsRegister(NewPtr, /*TRI=*/nullptr))
      return false;
  }

  // Load dest must not alias base/writeback (tied AGU forms).
  if (IsLoad && (Data == Base || Data == NewPtr))
    return false;
  // Store data must not be the writeback vreg (would be a self-ref on PRE).
  if (!IsLoad && Data == NewPtr)
    return false;

  // PRE is emitted at Mem (see applyIncMem). Store data / load is already
  // at Mem; Base and OffReg are used by PtrAdd which dominates Mem — OK.
  // Reject if Base is redefined between PtrAdd and Mem.
  for (auto I = std::next(PtrAdd->getIterator()); I != MemI.getIterator();
       ++I) {
    if (I->modifiesRegister(Base, /*TRI=*/nullptr))
      return false;
  }

  // AIE-style base liveness at insert point (Mem). Ignore folded PtrAdd.
  if (!EnableHaydnGISelGreedyAddr &&
      !checkRegUsesDominate(Base, MemI, /*IgnoreUser=*/*PtrAdd, MRI, Helper))
    return false;

  Info.MemI = &MemI;
  Info.PtrAddI = PtrAdd;
  Info.Data = Data;
  Info.Base = Base;
  Info.NewPtr = NewPtr;
  Info.OffsetReg = OffReg;
  Info.OffsetBytes = Bytes;
  Info.ScaleShift = Scale;
  Info.MemBytes = MemBytes;
  Info.IsSExtLoad = IsSExt;
  Info.IsPost = false;
  Info.IsLoad = IsLoad;
  Info.IsRegStride = IsReg;
  return true;
}

static void applyIncMem(MachineInstr &MemI, MachineRegisterInfo &MRI,
                        MachineIRBuilder &B, GISelChangeObserver &Observer,
                        HaydnIncMemInfo &Info) {
  // Always insert at the memory op. PRE used to insert at G_PTR_ADD, which
  // hoisted store-data uses above their defs when %data was defined between
  // ptradd and store (coremark core_main: S_SW_PRE_IMM LiveIntervals
  // "Use not jointly dominated by defs"). Same-MBB matcher already forbids
  // NewPtr uses between PtrAdd and Mem, so moving the NewPtr def down to Mem
  // is SSA-safe.
  B.setInstrAndDebugLoc(MemI);

  // IMM strides: always emit a fresh G_CONSTANT at the insert point so the
  // offset use is dominated (CSE may leave the original constant *after* the
  // load). REG strides keep OffsetReg (must already dominate PtrAdd/Mem).
  Register OffsetUse = Info.OffsetReg;
  if (!Info.IsRegStride) {
    auto Cst = B.buildConstant(LLT::scalar(32), Info.OffsetBytes);
    OffsetUse = Cst.getReg(0);
  }

  unsigned Opc;
  if (Info.IsLoad)
    Opc = Info.IsPost ? Haydn::G_HAYDN_POSTINC_LOAD : Haydn::G_HAYDN_PREINC_LOAD;
  else
    Opc =
        Info.IsPost ? Haydn::G_HAYDN_POSTINC_STORE : Haydn::G_HAYDN_PREINC_STORE;

  MachineInstrBuilder MIB = B.buildInstr(Opc);
  if (Info.IsLoad) {
    MIB.addDef(Info.Data);
    MIB.addDef(Info.NewPtr);
    MIB.addUse(Info.Base);
    MIB.addUse(OffsetUse);
  } else {
    MIB.addDef(Info.NewPtr);
    MIB.addUse(Info.Data);
    MIB.addUse(Info.Base);
    MIB.addUse(OffsetUse);
  }
  for (auto *MMO : MemI.memoperands())
    MIB.addMemOperand(MMO);

  LLVM_DEBUG(dbgs() << "Haydn postleg combine " << (Info.IsPost ? "POST" : "PRE")
                    << (Info.IsLoad ? "INC_LOAD" : "INC_STORE")
                    << " stride=" << Info.OffsetBytes
                    << (Info.IsRegStride ? " REG" : " IMM") << "\n  -> "
                    << *MIB);

  Observer.erasingInstr(*Info.PtrAddI);
  Info.PtrAddI->eraseFromParent();
  Observer.erasingInstr(MemI);
  MemI.eraseFromParent();
}

//===----------------------------------------------------------------------===//
// HaydnPostLegalizerCombinerImpl
//===----------------------------------------------------------------------===//

class HaydnPostLegalizerCombinerImpl : public Combiner {
protected:
  const CombinerHelper Helper;
  const HaydnPostLegalizerCombinerImplRuleConfig &RuleConfig;
  const HaydnSubtarget &STI;

public:
  HaydnPostLegalizerCombinerImpl(
      MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
      GISelValueTracking &VT, GISelCSEInfo *CSEInfo,
      const HaydnPostLegalizerCombinerImplRuleConfig &RuleConfig,
      const HaydnSubtarget &STI, MachineDominatorTree *MDT,
      const LegalizerInfo *LI);

  static const char *getName() { return "HaydnPostLegalizerCombiner"; }

  bool tryCombineAll(MachineInstr &I) const override;
  bool tryCombineAllImpl(MachineInstr &I) const;

private:
#define GET_GICOMBINER_CLASS_MEMBERS
#include "HaydnGenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CLASS_MEMBERS
};

#define GET_GICOMBINER_IMPL
#include "HaydnGenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_IMPL

HaydnPostLegalizerCombinerImpl::HaydnPostLegalizerCombinerImpl(
    MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
    GISelValueTracking &VT, GISelCSEInfo *CSEInfo,
    const HaydnPostLegalizerCombinerImplRuleConfig &RuleConfig,
    const HaydnSubtarget &STI, MachineDominatorTree *MDT,
    const LegalizerInfo *LI)
    : Combiner(MF, CInfo, TPC, &VT, CSEInfo),
      Helper(Observer, B, /*IsPreLegalize*/ false, &VT, MDT, LI),
      RuleConfig(RuleConfig), STI(STI),
#define GET_GICOMBINER_CONSTRUCTOR_INITS
#include "HaydnGenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CONSTRUCTOR_INITS
{
}

bool HaydnPostLegalizerCombinerImpl::tryCombineAll(MachineInstr &MI) const {
  // First try the TableGen-generated rules.
  if (tryCombineAllImpl(MI))
    return true;

  // Then try target-specific C++ rules.
  unsigned Opc = MI.getOpcode();
  MachineRegisterInfo &MRI = *B.getMRI();

  switch (Opc) {
  default:
    break;
  case TargetOpcode::G_LOAD:
  case TargetOpcode::G_ZEXTLOAD:
  case TargetOpcode::G_STORE: {
    // AIE-style: fuse G_LOAD/STORE + G_PTR_ADD into G_HAYDN_*INC_* so
    // InstructionSelect emits fused AGU writeback. Default ON
    // (-haydn-enable-gisel-update-addr). G_SEXTLOAD intentionally omitted:
    // selector always emits LBU/LHWU for 1/2-byte fused loads (no signedness
    // on G_HAYDN_*INC_* yet).
    if (EnableHaydnGISelUpdateAddr) {
      HaydnIncMemInfo Info;
      bool Matched = false;
      if (EnableHaydnGISelPostInc && matchPostIncMem(MI, MRI, Helper, Info))
        Matched = true;
      else if (EnableHaydnGISelPreInc &&
               matchPreIncMem(MI, MRI, Helper, Info))
        Matched = true;
      if (Matched) {
        applyIncMem(MI, MRI, B, Observer, Info);
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_TRUNC: {
    // G_TRUNC(G_ANYEXT x) -> COPY x
    // Eliminates trivial trunc-of-anyext identity patterns.
    {
      Register MatchInfo;
      if (matchTruncOfAnyExt(MI, MRI, MatchInfo)) {
        applyTruncOfAnyExt(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_TRUNC(G_SEXT/G_ZEXT x) where output == input type -> COPY x
    // Identity extension-truncation cancels out.
    {
      Register MatchInfo;
      if (matchTruncOfExtToIdentity(MI, MRI, MatchInfo)) {
        applyTruncOfExtToIdentity(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_SEXT:
  case TargetOpcode::G_ZEXT: {
    // G_SEXT/G_ZEXT x followed by G_TRUNC back to src type -> COPY x
    // Eliminates extension-truncation round-trips.
    {
      Register MatchInfo;
      if (matchExtTruncIdentity(MI, MRI, MatchInfo)) {
        applyExtTruncIdentity(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_SEXT(G_SEXT x) where outer is no-op (same type) -> COPY
    if (Opc == TargetOpcode::G_SEXT) {
      Register MatchInfo;
      if (matchRedundantSExt(MI, MRI, MatchInfo)) {
        applyRedundantSExt(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_ZEXT(G_ZEXT x) where outer is no-op (same type) -> COPY
    if (Opc == TargetOpcode::G_ZEXT) {
      Register MatchInfo;
      if (matchRedundantZExt(MI, MRI, MatchInfo)) {
        applyRedundantZExt(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_ZEXT(G_SEXT x) where outer dest == inner sext dest -> COPY
    // Outer zero-extension is a no-op when the types are already equal.
    if (Opc == TargetOpcode::G_ZEXT) {
      Register MatchInfo;
      if (matchZExtOfSExt(MI, MRI, MatchInfo)) {
        applyZExtOfSExt(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_SEXT(G_ZEXT x) -> G_ZEXT x (to wider type)
    // Sign-extending a zero-extended value is equivalent to zero-extension
    // because the inner zext guarantees a non-negative (sign bit = 0) value.
    if (Opc == TargetOpcode::G_SEXT) {
      Register MatchInfo;
      if (matchSExtOfZExt(MI, MRI, MatchInfo)) {
        applySExtOfZExt(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_ZEXT(G_ZEXT x) -> G_ZEXT x (collapse to single wider zext)
    if (Opc == TargetOpcode::G_ZEXT) {
      Register MatchInfo;
      if (matchFoldDoubleZExt(MI, MRI, MatchInfo)) {
        applyFoldDoubleZExt(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_SEXT(G_SEXT x) -> G_SEXT x (collapse to single wider sext)
    if (Opc == TargetOpcode::G_SEXT) {
      Register MatchInfo;
      if (matchFoldDoubleSExt(MI, MRI, MatchInfo)) {
        applyFoldDoubleSExt(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // NOTE: G_ZEXT(G_TRUNC x) where result type == trunc source type is NOT
    // identity. trunc(s32->s16) drops upper 16 bits, then zext(s16->s32)
    // zero-extends, producing x & 0xFFFF, not x. Do NOT fold this pattern.
    break;
  }
  case TargetOpcode::COPY: {
    // G_COPY x, x -> eliminate (redundant self-copy).
    if (matchRedundantCopy(MI, MRI)) {
      applyRedundantCopy(MI, MRI, B, Observer);
      return true;
    }
    break;
  }
  case TargetOpcode::G_ICMP: {
    // G_ICMP const, const -> const 0 or 1
    // Constant-fold integer comparisons with known constant operands.
    {
      int64_t MatchInfo;
      if (matchConstantFoldICmp(MI, MRI, MatchInfo)) {
        applyConstantFoldICmp(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_ICMP with known bits -> constant true/false.
    {
      int64_t MatchInfo;
      if (Helper.matchICmpToTrueFalseKnownBits(MI, MatchInfo)) {
        B.setInstrAndDebugLoc(MI);
        Register Dst = MI.getOperand(0).getReg();
        B.buildConstant(Dst, MatchInfo);
        Observer.erasingInstr(MI);
        MI.eraseFromParent();
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_SELECT: {
    // G_SELECT(G_ICMP(pred, a, b), x, y) -> G_SMAX/G_SMIN/G_UMAX/G_UMIN
    // when the select is equivalent to a min or max operation.
    {
      SelectMinMaxMatchInfo MatchInfo;
      if (matchSelectToMinMax(MI, MRI, MatchInfo)) {
        applySelectToMinMax(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_AND: {
    // G_AND x, 0 -> 0 (AND with zero is always zero)
    {
      APInt MatchInfo;
      if (matchAndZero(MI, MRI, MatchInfo)) {
        applyAndZero(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_AND x, -1 -> x (AND with all-ones is identity)
    {
      Register MatchInfo;
      if (matchAndAllOnes(MI, MRI, MatchInfo)) {
        applyAndAllOnes(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // (x >> C) & mask where mask == exact remaining bits -> x >> C
    // Eliminates redundant mask after shift (bit-field extraction pattern).
    {
      Register MatchInfo;
      if (matchShiftMaskRedundant(MI, MRI, MatchInfo)) {
        applyShiftMaskRedundant(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_OR: {
    // G_OR x, -1 -> -1 (OR with all-ones is always all-ones)
    {
      APInt MatchInfo;
      if (matchOrAllOnes(MI, MRI, MatchInfo)) {
        applyOrAllOnes(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_OR x, 0 -> x (OR with zero is identity)
    {
      Register MatchInfo;
      if (matchOrZero(MI, MRI, MatchInfo)) {
        applyOrZero(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // (A & MaskC) | SetC where MaskC and SetC are disjoint and
    // MaskC|SetC covers all bits -> A | SetC (AND-OR canonicalization).
    {
      std::tuple<Register, APInt, APInt> MatchInfo;
      if (matchAndOrDisjoint(MI, MRI, MatchInfo)) {
        applyAndOrDisjoint(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_XOR: {
    // G_XOR x, 0 -> x (XOR with zero is identity).
    {
      Register MatchInfo;
      if (matchXorZero(MI, MRI, MatchInfo)) {
        applyXorZero(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // (A ^ C1) ^ C2 -> A ^ (C1^C2) (XOR constant cancellation).
    {
      std::pair<Register, APInt> MatchInfo;
      if (matchXorXorConstantFold(MI, MRI, MatchInfo)) {
        applyXorXorConstantFold(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // XOR(NOT(x)) -> x (double NOT cancellation).
    {
      Register MatchInfo;
      if (matchDoubleNot(MI, MRI, MatchInfo)) {
        applyDoubleNot(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // XOR %bool, -1 -> XOR (and %bool,1), 1 when %bool is a widened i1 AND the
    // XOR result is consumed only by a branch (BNEZ loop). gate:
    // the s32 bitwise NOT (0/-1, 1/-2) is only equivalent to logical NOT
    // (0/1, 1/0) under the BNEZ polarity test; for value uses (return/store
    // arith/PHI) they differ and the combine would mis-compile, so it is
    // restricted to branch-only Dst users.
    {
      bool MatchInfo;
      if (matchXorAllOnesBoolean(MI, MRI, MatchInfo)) {
        applyXorAllOnesBoolean(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_BSWAP: {
    // G_BSWAP(G_BSWAP x) -> COPY x (double bswap is identity)
    {
      Register MatchInfo;
      if (matchBswapOfBswap(MI, MRI, MatchInfo)) {
        applyBswapOfBswap(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_ADD: {
    // G_ADD x, 0 -> x (ADD with zero is identity).
    {
      Register MatchInfo;
      if (matchAddZero(MI, MRI, MatchInfo)) {
        applyAddZero(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_ADD(G_ADD x, C1), C2 -> G_ADD x, (C1+C2)
    {
      std::pair<Register, APInt> MatchInfo;
      if (matchAddConstChain(MI, MRI, MatchInfo)) {
        applyAddConstChain(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_ADD x, G_SUB(0, y) -> G_SUB x, y
    {
      std::pair<Register, Register> MatchInfo;
      if (matchAddOfNeg(MI, MRI, MatchInfo)) {
        applyAddOfNeg(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_SUB: {
    // G_SUB x, 0 -> x (SUB with zero RHS is identity).
    {
      Register MatchInfo;
      if (matchSubZero(MI, MRI, MatchInfo)) {
        applySubZero(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_SUB x, G_SUB(0, y) -> G_ADD x, y
    {
      std::pair<Register, Register> MatchInfo;
      if (matchSubOfNeg(MI, MRI, MatchInfo)) {
        applySubOfNeg(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_MUL: {
    // G_MUL x, 0 -> 0 (multiply by zero is always zero)
    {
      APInt MatchInfo;
      if (matchMulZero(MI, MRI, MatchInfo)) {
        applyMulZero(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_MUL x, C where C is (pow2 - 1) -> G_SUB (G_SHL x, log2(pow2)), x
    // Shift-sub: x * 7 = (x << 3) - x. Must check before pow2 match.
    {
      std::pair<Register, uint64_t> MatchInfo;
      if (matchMulToShiftSub(MI, MRI, MatchInfo)) {
        applyMulToShiftSub(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_MUL x, C where C is (pow2 + 1) -> G_ADD (G_SHL x, log2(pow2)), x
    // Shift-add: x * 9 = (x << 3) + x. Must check before pow2 match.
    {
      std::pair<Register, uint64_t> MatchInfo;
      if (matchMulToShiftAdd(MI, MRI, MatchInfo)) {
        applyMulToShiftAdd(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_MUL x, C where C is a power of 2 -> G_SHL x, log2(C)
    // Strength reduction: shift is cheaper than multiply.
    {
      std::pair<Register, uint64_t> MatchInfo;
      if (matchMulToShift(MI, MRI, MatchInfo)) {
        applyMulToShift(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_MUL x, 1 -> x (multiplication by 1 is identity).
    {
      Register MatchInfo;
      if (matchMulByOne(MI, MRI, MatchInfo)) {
        applyMulByOne(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_MUL x, -1 -> G_SUB 0, x (canonicalize negation)
    {
      Register MatchInfo;
      if (matchMulByNegOne(MI, MRI, MatchInfo)) {
        applyMulByNegOne(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_SHL: {
    // G_SHL x, 0 -> x (shift by zero is identity).
    {
      Register MatchInfo;
      if (matchShlZero(MI, MRI, MatchInfo)) {
        applyShlZero(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_LSHR: {
    // G_LSHR x, 0 -> x (logical shift right by zero is identity).
    {
      Register MatchInfo;
      if (matchLshrZero(MI, MRI, MatchInfo)) {
        applyLshrZero(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_ASHR: {
    // G_ASHR x, 0 -> x (arithmetic shift right by zero is identity).
    {
      Register MatchInfo;
      if (matchAshrZero(MI, MRI, MatchInfo)) {
        applyAshrZero(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    break;
  }
  case TargetOpcode::G_SEXT_INREG: {
    // G_SEXT_INREG(G_SEXT x, K) where sext already covers K bits -> COPY
    {
      Register MatchInfo;
      if (matchSExtInRegOfSExt(MI, MRI, MatchInfo)) {
        applySExtInRegOfSExt(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    // G_SEXT_INREG(G_ASHR x, C) where sext width == BitWidth-C -> COPY
    // Redundant sign-extension after arithmetic right shift.
    {
      Register MatchInfo;
      if (matchSExtInRegOfAShr(MI, MRI, MatchInfo)) {
        applySExtInRegOfAShr(MI, MRI, B, Observer, MatchInfo);
        return true;
      }
    }
    break;
  }
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Pass boilerplate
//===----------------------------------------------------------------------===//

class HaydnPostLegalizerCombiner : public MachineFunctionPass {
public:
  static char ID;

  HaydnPostLegalizerCombiner();

  StringRef getPassName() const override {
    return "HaydnPostLegalizerCombiner";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  HaydnPostLegalizerCombinerImplRuleConfig RuleConfig;
};
} // end anonymous namespace

void HaydnPostLegalizerCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
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

HaydnPostLegalizerCombiner::HaydnPostLegalizerCombiner()
    : MachineFunctionPass(ID) {
  if (!RuleConfig.parseCommandLineOption())
    report_fatal_error("Invalid rule identifier");
}

// residual: sanitize type-mismatched COPY and invalid G_TRUNC before
// TableGen/C++ combines run.
// Root cause of the remaining full-seed crash: identity folds (and earlier
// pipeline stages) leave cross-type COPY such as `s64 = COPY s1`. Upstream
// getIConstantVRegValWithLookThrough follows COPY without adjusting the
// constant APInt width; a later G_TRUNC then does Val.trunc(dstW) and
// asserts "Invalid APInt Truncate request" from isOperandImmEqual.
// Fix (Haydn-side, no further upstream edits):
// COPY with DstW > SrcW -> G_ANYEXT
// COPY with DstW < SrcW -> G_TRUNC
// G_TRUNC with DstW > SrcW -> G_ANYEXT
// G_TRUNC with DstW == SrcW -> COPY
static bool sanitizeCastCopies(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  bool Changed = false;
  unsigned FixedTrunc = 0;
  unsigned FixedCopy = 0;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      unsigned Opc = MI.getOpcode();
      if (Opc != TargetOpcode::G_TRUNC && Opc != TargetOpcode::COPY)
        continue;
      if (!MI.getOperand(0).isReg() || !MI.getOperand(1).isReg())
        continue;
      Register Dst = MI.getOperand(0).getReg();
      Register Src = MI.getOperand(1).getReg();
      if (!Dst.isVirtual() || !Src.isVirtual())
        continue;
      LLT DstTy = MRI.getType(Dst);
      LLT SrcTy = MRI.getType(Src);
      if (!DstTy.isValid() || !SrcTy.isValid())
        continue;
      unsigned DstW = DstTy.getSizeInBits();
      unsigned SrcW = SrcTy.getSizeInBits();

      if (Opc == TargetOpcode::COPY) {
        // Cross-type COPY breaks getIConstantVRegValWithLookThrough: it
        // follows COPY without adjusting APInt width, then a later G_TRUNC
        // applies trunc(dstW) on the narrow constant and asserts.
        if (DstW == SrcW)
          continue;
        if (DstW > SrcW)
          MI.setDesc(TII.get(TargetOpcode::G_ANYEXT));
        else
          MI.setDesc(TII.get(TargetOpcode::G_TRUNC));
        ++FixedCopy;
        Changed = true;
        continue;
      }

      // G_TRUNC
      if (DstW < SrcW)
        continue; // Proper narrowing.
      if (DstW == SrcW)
        MI.setDesc(TII.get(TargetOpcode::COPY));
      else
        MI.setDesc(TII.get(TargetOpcode::G_ANYEXT));
      ++FixedTrunc;
      Changed = true;
    }
  }
  LLVM_DEBUG(if (FixedTrunc || FixedCopy) {
    dbgs() << " sanitize: trunc=" << FixedTrunc << " copy=" << FixedCopy
           << " in " << MF.getName() << '\n';
  });
  return Changed;
}

bool HaydnPostLegalizerCombiner::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasFailedISel())
    return false;
  assert(MF.getProperties().hasLegalized() && "Expected a legalized function?");
  auto *TPC = &getAnalysis<TargetPassConfig>();
  const Function &F = MF.getFunction();
  bool EnableOpt =
      MF.getTarget().getOptLevel() != CodeGenOptLevel::None && !skipFunction(F);

  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const auto *LI = ST.getLegalizerInfo();

  GISelValueTracking *VT =
      &getAnalysis<GISelValueTrackingAnalysisLegacy>().get(MF);
  MachineDominatorTree *MDT =
      &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  // residual: fix type-mismatched COPY / invalid G_TRUNC *before*
  // CSE is built. setDesc without observer would leave stale CSE
  // entries if sanitize ran after Wrapper.get.
  bool Changed = sanitizeCastCopies(MF);

  GISelCSEAnalysisWrapper &Wrapper =
      getAnalysis<GISelCSEAnalysisWrapperPass>().getCSEWrapper();
  auto *CSEInfo = &Wrapper.get(TPC->getCSEConfig());

  CombinerInfo CInfo(/*AllowIllegalOps*/ true, /*ShouldLegalizeIllegal*/ false,
                     /*LegalizerInfo*/ nullptr, EnableOpt, F.hasOptSize(),
                     F.hasMinSize());
  // Disable fixed-point iteration to reduce compile-time
  CInfo.MaxIterations = 1;
  CInfo.ObserverLvl = CombinerInfo::ObserverLevel::SinglePass;
  HaydnPostLegalizerCombinerImpl Impl(MF, CInfo, TPC, *VT, CSEInfo,
                                      RuleConfig, ST, MDT, LI);
  Changed |= Impl.combineMachineInstrs();
  return Changed;
}

char HaydnPostLegalizerCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(HaydnPostLegalizerCombiner, DEBUG_TYPE,
                      "Combine Haydn MachineInstrs after legalization", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelValueTrackingAnalysisLegacy)
INITIALIZE_PASS_END(HaydnPostLegalizerCombiner, DEBUG_TYPE,
                    "Combine Haydn MachineInstrs after legalization", false,
                    false)

FunctionPass *llvm::createHaydnPostLegalizerCombiner() {
  return new HaydnPostLegalizerCombiner();
}
