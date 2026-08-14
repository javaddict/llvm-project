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
// (pre-RegBankSelect) MIR. MAC fusion is NOT performed here
// (product MAC = intrinsic + PreLegalizer G_MULA64).
//
// TableGen owns the full rule registry (HaydnCombine.td):
//   haydn_post_generic_combines  — legal-preserving shared generics (no intdiv)
//   haydn_post_residual_combines — Haydn algebraic residuals
//   haydn_post_target_combines   — form_agu_inc_mem (G_HAYDN_*INC_*)
// Dispatched exclusively via tryCombineAllImpl. Pass shell has no free-form
// opcode switch and no post-legal cast sanitizer.
//
// C++ residual is match/apply helpers for TD-registered rules only.
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

// Product AIE-style form (GISel post-legalizer): G_LOAD/ZEXTLOAD/SEXTLOAD/STORE
// + G_PTR_ADD → G_HAYDN_*INC_* → AGU PRE/POST at InstructionSelect.
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
// Thin match/apply for haydn_post_residual_combines (HaydnCombine.td).
// Called only from TableGen-generated tryCombineAllImpl — no dual-home switch.
//===----------------------------------------------------------------------===//

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

  // Anti-ping-pong with generic xor_of_and_with_same_reg:
  //   xor_allones_boolean:  xor X,-1  ->  xor (and X,1), 1
  //   xor_of_and_with_same_reg: xor (and X,1), 1  ->  and (xor X,-1), 1
  //   xor_allones_boolean on the fresh inner xor X,-1  -> infinite loop
  // If this XOR's only use is `and %xor, 1` (commuted ok), we are already the
  // inner not of the stable masked form — leave it alone.
  if (MRI.hasOneNonDBGUse(Dst)) {
    MachineInstr &UseMI = *MRI.use_instr_nodbg_begin(Dst);
    if (UseMI.getOpcode() == TargetOpcode::G_AND) {
      Register OtherAndOp = UseMI.getOperand(1).getReg() == Dst
                                ? UseMI.getOperand(2).getReg()
                                : UseMI.getOperand(1).getReg();
      auto AV = getIConstantVRegValWithLookThrough(OtherAndOp, MRI);
      if (AV && AV->Value.sextOrTrunc(BitWidth).isOne())
        return false;
    }
  }

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

// Haydn PRE/POST LS forms are hardware-tied `$rs = $rs_wb` (destructive AGU
// update). If Base has any other use besides the folded G_PTR_ADD (and the
// mem op for POST), regalloc assigns one physreg for the tied pair while
// sibling `ST base, immN` ops still need the *pre-update* value. Post-RA
// scheduling can then place a sibling store after the PRE/POST and apply
// its imm to the advanced base — e.g. varargs DR spill `d_sdw_pre d3, r, 3`
// then `d_sdw d2, r, 2` lands at base+40 instead of base+16 and stomps the
// GPR save area / incoming stack args (va-arg-2 f1/f7 ABORT).
// Only fuse when Base is exclusive to this mem+ptradd pair.
static bool baseExclusiveForTiedInc(Register Base, MachineInstr &MemI,
                                    MachineInstr &PtrAdd,
                                    MachineRegisterInfo &MRI, bool IsPost) {
  for (MachineInstr &Use : MRI.use_nodbg_instructions(Base)) {
    if (&Use == &PtrAdd)
      continue;
    if (IsPost && &Use == &MemI)
      continue;
    return false;
  }
  return true;
}

static bool matchPostIncMem(MachineInstr &MemI, MachineRegisterInfo &MRI,
                            const CombinerHelper &Helper,
                            HaydnIncMemInfo &Info) {
  // G_SEXTLOAD fuses too: is_sext imm on G_HAYDN_*INC_LOAD selects S_LBS/S_LHWS.
  const bool IsLoad = MemI.getOpcode() == TargetOpcode::G_LOAD ||
                      MemI.getOpcode() == TargetOpcode::G_ZEXTLOAD ||
                      MemI.getOpcode() == TargetOpcode::G_SEXTLOAD;
  if (!IsLoad && MemI.getOpcode() != TargetOpcode::G_STORE)
    return false;

  unsigned MemBytes = 0, Scale = 0;
  bool IsSExt = false;
  if (!memAccessInfo(MemI, MRI, MemBytes, Scale, IsSExt))
    return false;

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

    // Tied rs=rs_wb: refuse if Base is shared with other addressing.
    if (!baseExclusiveForTiedInc(Base, MemI, U, MRI, /*IsPost=*/true))
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
                      MemI.getOpcode() == TargetOpcode::G_ZEXTLOAD ||
                      MemI.getOpcode() == TargetOpcode::G_SEXTLOAD;
  if (!IsLoad && MemI.getOpcode() != TargetOpcode::G_STORE)
    return false;

  unsigned MemBytes = 0, Scale = 0;
  bool IsSExt = false;
  if (!memAccessInfo(MemI, MRI, MemBytes, Scale, IsSExt))
    return false;

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

  // Tied rs=rs_wb: refuse if Base is shared with other addressing.
  if (!baseExclusiveForTiedInc(Base, MemI, *PtrAdd, MRI, /*IsPost=*/false))
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

// TD form_agu_inc_mem entry: fuse G_LOAD/ZEXTLOAD/SEXTLOAD/STORE + G_PTR_ADD
// into G_HAYDN_*INC_*. Requires Subtarget hasAGU() (baseline on generic and
// haydn CPUs; disable with -mattr=-agu) and -haydn-enable-gisel-update-addr.
bool matchCombineAGUIncMem(MachineInstr &MI, MachineRegisterInfo &MRI,
                           const CombinerHelper &Helper,
                           HaydnIncMemInfo &Info) {
  const HaydnSubtarget &ST = MI.getMF()->getSubtarget<HaydnSubtarget>();
  if (!ST.hasAGU())
    return false;
  if (!EnableHaydnGISelUpdateAddr)
    return false;

  unsigned Opc = MI.getOpcode();
  if (Opc != TargetOpcode::G_LOAD && Opc != TargetOpcode::G_ZEXTLOAD &&
      Opc != TargetOpcode::G_SEXTLOAD && Opc != TargetOpcode::G_STORE)
    return false;

  if (EnableHaydnGISelPostInc && matchPostIncMem(MI, MRI, Helper, Info))
    return true;
  if (EnableHaydnGISelPreInc && matchPreIncMem(MI, MRI, Helper, Info))
    return true;
  return false;
}

void applyIncMem(MachineInstr &MemI, MachineRegisterInfo &MRI,
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
    // is_sext: 1 → S_LBS/S_LHWS at select; 0 → S_LBU/S_LHWU (or full-width).
    MIB.addImm(Info.IsSExtLoad ? 1 : 0);
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
  // TD registry only: post generics + residual + form_agu_inc_mem.
  // No free-form C++ opcode switch / no sanitizeCastCopies.
  return tryCombineAllImpl(MI);
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

  // No sanitizeCastCopies: PreLegalizer cast_combines + fixed ext collapse
  // keep the legalization boundary type-clean. Post must not create
  // unchecked G_ANYEXT/G_TRUNC while ShouldLegalizeIllegal=false.

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
  return Impl.combineMachineInstrs();
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
