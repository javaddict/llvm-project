//===-- HaydnInstructionSelector.cpp -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// This file implements the targeting of the InstructionSelector class for
// Haydn.
// Selector emits logical opcodes only (hard constraint #8). Every 32-bit
// ALU32 op is constrained to the full GPR32RegClass (r0-r15); there is no
// compact-register size routing on the live path. Post-RA HR setDesc picks
// generated member `_S*` / AltDesc forms. Encode is Desc-as-is (Format E).
//===----------------------------------------------------------------------===//

#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnRegisterBankInfo.h"
#include "HaydnRegisterInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMatInt.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutorImpl.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsHaydn.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-isel"

#define GET_GLOBALISEL_PREDICATE_BITSET
#include "HaydnGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATE_BITSET

namespace {

// Logical-not of a 0/1 GPR predicate: `rd = rs ^ 1`.
// Single authority for s32 and s64 icmp invert (NE/GE/LE and NeedInvert).
// Must NOT use bitwise NOT32 (~0 = -1, ~1 = -2 — both nonzero → BNEZ always).
// ISA: XORI32 is ZEXT imm20; emit logical XORI32; post-RA setDesc → XORI32_S*.
static Register emitInvert01(MachineIRBuilder &MIB, Register Pred01,
                             const TargetInstrInfo &TII,
                             const TargetRegisterInfo &TRI,
                             const RegisterBankInfo &RBI,
                             MachineRegisterInfo &MRI) {
  Register Out = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
  if (Pred01.isVirtual())
    RBI.constrainGenericRegister(Pred01, Haydn::GPR32RegClass, MRI);
  MachineInstr *MI = MIB.buildInstr(Haydn::XORI32)
                         .addDef(Out)
                         .addReg(Pred01)
                         .addImm(1);
  if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
    return Register();
  return Out;
}

// `rd = rs OP imm` for an RI20/RI5 immediate ALU form — ANDI32 (uimm20),
// SLLI32/SRLI32/SRAI32 (uimm5). Companion to emitInvert01 for the ext/trunc
// idioms this selector builds by hand: they never enter selectImpl, so the
// HaydnGISel.td immediate Pats cannot reach them and the constant would be
// materialized with LOADI32 and then consumed by the RR form (TODO.md T1b).
// The caller owns the range check; post-RA setDesc picks the _S* slot member.
static bool emitALUImm(MachineIRBuilder &MIB, unsigned Opc, Register Dst,
                       Register Src, int64_t Imm, const TargetInstrInfo &TII,
                       const TargetRegisterInfo &TRI,
                       const RegisterBankInfo &RBI,
                       MachineRegisterInfo &MRI) {
  if (Src.isVirtual())
    RBI.constrainGenericRegister(Src, Haydn::GPR32RegClass, MRI);
  MachineInstr *MI = MIB.buildInstr(Opc).addDef(Dst).addReg(Src).addImm(Imm);
  return constrainSelectedInstRegOperands(*MI, TII, TRI, RBI);
}

// Memory access size in bytes from MMO (peer: AIEBaseInstructionSelector
// getMemSizeInBits). Prefer MMO over SSA width so anyext/trunc loads after
// legalizer splits of unaligned/i24 mem select LDU8/LDU16/ST8/ST16, never
// LD32/ST32 on a narrow access.
static unsigned getMemAccessBytes(const MachineInstr &I,
                                  unsigned FallbackBytes) {
  if (!I.memoperands_empty()) {
    LocationSize LS = (*I.memoperands_begin())->getSize();
    if (LS.hasValue())
      return static_cast<unsigned>(LS.getValue());
  }
  return FallbackBytes;
}

// – / : IRTranslator attaches MMOs via getTgtMemIntrinsic
// on G_INTRINSIC_W_SIDE_EFFECTS (ordinary MOLoad/MOStore vs stateful
// MOVolatile). Clone them onto the selected target MI so scheduler/AA see
// object, size, alignment, and flags. Without this, selected CB/BREV/Golden
// mem ops print without `:: (load/store …)`.
static void cloneMemOperands(MachineInstr &Dst, const MachineInstr &Src) {
  MachineFunction *MF = Dst.getMF();
  for (MachineMemOperand *MMO : Src.memoperands())
    Dst.addMemOperand(*MF, MMO);
}

// Per-half MMO for an 8-byte access split into two 4-byte ops at offsets 0/4.
// Peer: AArch64InstructionSelector.cpp:1996-1998 / 2031-2033 (getWithOffset +
// Size); MachineFunction::getMachineMemOperand(MMO, Offset, Size) as in
// RISCVExpandPseudoInsts.cpp:496-497 and AArch64ISelLowering.cpp:24874.
static void addSplitHalfMMO(MachineInstr *MI, MachineFunction &MF,
                            const MachineInstr &Src, int64_t ByteOffset) {
  if (Src.memoperands_empty())
    return;
  MI->addMemOperand(MF, MF.getMachineMemOperand(*Src.memoperands_begin(),
                                                ByteOffset, /*Size=*/4));
}

// Constrain selected regs and preserve any target-mem-intrinsic MMOs.
static bool constrainSelectedMemInst(MachineInstr *MI, MachineInstr &I,
                                     const TargetInstrInfo &TII,
                                     const TargetRegisterInfo &TRI,
                                     const RegisterBankInfo &RBI) {
  cloneMemOperands(*MI, I);
  return constrainSelectedInstRegOperands(*MI, TII, TRI, RBI);
}

// CB load/store consumes the CBR set programmed by SETCBR→CSRW_W
// (implicit-def of CBR0/CBR1). Without this use, PostRA free-reorders CB
// mem before the boundary CSRW ( early expand exposed the missing edge).
// CBR is sticky HW state — also mark entry live-in so MachineVerifier accepts
// CB ops in functions that omit an in-function SETCBR (setup may be external).
static void addCircularBufferUse(MachineInstr *MI, uint64_t CbrSel) {
  assert((CbrSel == 0 || CbrSel == 1) && "cbr_sel must be 0 or 1");
  Register CbrReg = (CbrSel == 0) ? Haydn::CBR0 : Haydn::CBR1;
  MachineFunction &MF = *MI->getParent()->getParent();
  MachineBasicBlock &Entry = MF.front();
  if (!Entry.isLiveIn(CbrReg))
    Entry.addLiveIn(CbrReg);
  MI->addOperand(
      MachineOperand::CreateReg(CbrReg, /*isDef=*/false, /*isImp=*/true));
}

// Scalar mem ops land in GPR32 except full 8-byte LD64/ST64 → DR64.
static const TargetRegisterClass *memResultRC(unsigned MemBytes) {
  return MemBytes == 8 ? &Haydn::DR64RegClass : &Haydn::GPR32RegClass;
}

// Golden short-form imm is scaled simm6: EA = base + (simm6 << log2(scale)).
static bool isLegalScaledSimm6(int64_t ByteOff, unsigned Scale) {
  return Scale != 0 && (ByteOff % static_cast<int64_t>(Scale)) == 0 &&
         isInt<6>(ByteOff / static_cast<int64_t>(Scale));
}

// Fail-closed ImmArg range check. Out-of-range values must not be emitted:
// MC N-bit-truncates them into silent wrong code (PA2-B7 / W52: slli32 33
// encodes as shift-by-1). Widths are the product field, not the loose
// logical-stub operand type (SIN_COS/ARCTAN td simm16 vs golden uimm4;
// CB logical simm16 vs member simm8; LS logical simm16 vs member simm6).
static bool expectUImm(int64_t Val, unsigned Bits, const char *What) {
  if (Val >= 0 && isUIntN(Bits, static_cast<uint64_t>(Val)))
    return true;
  LLVM_DEBUG(dbgs() << What << ": immediate " << Val << " is not uimm" << Bits
                    << "\n");
  return false;
}

static bool expectSImm(int64_t Val, unsigned Bits, const char *What) {
  if (isIntN(Bits, Val))
    return true;
  LLVM_DEBUG(dbgs() << What << ": immediate " << Val << " is not simm" << Bits
                    << "\n");
  return false;
}

// Register class for a vreg operand, consulting the operand's existing
// constraint before the size-derived default (one rule for every selector
// constrain site; GOALS W45):
//   1. an already-set register class wins (constraining is then a no-op);
//   2. a 64-bit operand constrained to the AR bank constrains to ARRegClass
//      — never the blanket DR64 size default (ARRegBank does not cover
//      DR64RegClass, so the old blanket DR64 silently failed and left the
//      vreg bank-only; the COPY path had the same mis-bank — W36 residual);
//   3. otherwise the size default: 64-bit → DR64, else GPR32. A bank that
//      does not cover its size class (DR64 bank on ≤32-bit, GPR32 bank on
//      64-bit) cannot be produced by the pipeline (RBI maps by type); the
//      constrain then fails loudly instead of the old silent mis-bank.
// The AR row must be entered via MRI::setRegClassOrRegBank by the caller:
// ARRegClass is isAllocatable=0 and RBI::constrainGenericRegister →
// MRI::setRegClass asserts on it (W36 precedent — the one legal entry).
static const TargetRegisterClass *
bankAwareRegClass(Register VReg, const MachineRegisterInfo &MRI) {
  if (const TargetRegisterClass *RC = MRI.getRegClassOrNull(VReg))
    return RC;
  const RegisterBank *Bank = MRI.getRegBankOrNull(VReg);
  const bool IsARConstrained =
      Bank && Bank->getID() == Haydn::ARRegBankID;
  LLT Ty = MRI.getType(VReg);
  if (Ty.isValid() && Ty.getSizeInBits() == 64)
    return IsARConstrained ? &Haydn::ARRegClass : &Haydn::DR64RegClass;
  return &Haydn::GPR32RegClass;
}

class HaydnInstructionSelector : public InstructionSelector {
public:
  HaydnInstructionSelector(const HaydnSubtarget &ST,
                           const HaydnRegisterBankInfo &RBI);

  bool select(MachineInstr &I) override;
  static const char *getName() { return "HaydnGISel"; }

  /// Setup per-MF executor state, then run the one-time pre-constrain of
  /// bank-only vregs before any select() call (AArch64/RISCV peer shape).
  void setupMF(MachineFunction &MF, GISelValueTracking *VT,
               CodeGenCoverage *CoverageInfo, ProfileSummaryInfo *PSI,
               BlockFrequencyInfo *BFI) override {
    InstructionSelector::setupMF(MF, VT, CoverageInfo, PSI, BFI);
    preConstrainBankOnlyVRegs(MF);
  }

private:
  // tblgen-erated 'select' implementation
  bool selectImpl(MachineInstr &I, CodeGenCoverage &CoverageInfo) const;

  // Once-per-MF pre-constrain of bank-only vregs (see setupMF).
  void preConstrainBankOnlyVRegs(MachineFunction &MF);

  // Constrain VReg to its bank-aware register class (bankAwareRegClass),
  // entering through the one constraint entry that also accepts the
  // non-allocatable ARRegClass. False only on a real constrain failure.
  bool constrainBankAware(Register VReg, MachineRegisterInfo &MRI);

  // Select Haydn DSP intrinsics (G_INTRINSIC) to target instructions.
  bool selectIntrinsic(MachineInstr &I);

  // Lower scalar s32 wrap-mul to golden MULL (MAC GRR: low 32 of product).
  // Family: MULL / MULSSH / MULSUH / MULUUH. Used by G_MUL <s32>, haydn_mac32.
  // MULL Constraints "$rd = $rs2"; TwoAddress inserts a MOV when needed.
  bool emitScalarMul32(MachineInstr &I, Register Dst, Register Src0,
                       Register Src1, MachineIRBuilder &MIB,
                       MachineRegisterInfo &MRI);

  // Lane-0 of a DR64 vector shift-amount as GPR32 (selected ops only).
  // v2i32: low 32 bits; v4i16: low 16 of that half (shift units use low bits).
  // Avoids G_EXTRACT_VECTOR_ELT (dest elt type must match vector elt — the old
  // path used s32 for v4i16 and asserted in MachineIRBuilder).
  Register extractVecLane0AsGPR32(MachineIRBuilder &MIB, Register AmtVec,
                                  MachineRegisterInfo &MRI);

  const HaydnInstrInfo &TII;
  const HaydnRegisterInfo &TRI;
  const HaydnSubtarget &STI;
  const HaydnRegisterBankInfo &RBI;

#define GET_GLOBALISEL_PREDICATES_DECL
#include "HaydnGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATES_DECL

#define GET_GLOBALISEL_TEMPORARIES_DECL
#include "HaydnGenGlobalISel.inc"
#undef GET_GLOBALISEL_TEMPORARIES_DECL
};


} // end anonymous namespace

#define GET_GLOBALISEL_IMPL
#include "HaydnGenGlobalISel.inc"
#undef GET_GLOBALISEL_IMPL

HaydnInstructionSelector::HaydnInstructionSelector(
    const HaydnSubtarget &ST, const HaydnRegisterBankInfo &RBI)
    : TII(*ST.getInstrInfo()), TRI(*ST.getRegisterInfo()), STI(ST), RBI(RBI),
#define GET_GLOBALISEL_PREDICATES_INIT
#include "HaydnGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATES_INIT
#define GET_GLOBALISEL_TEMPORARIES_INIT
#include "HaydnGenGlobalISel.inc"
#undef GET_GLOBALISEL_TEMPORARIES_INIT
{
}

// Once-per-MF pre-constrain (setupMF) of bank-only vregs: give every vreg
// left unconstrained by earlier passes (e.g., G_PHI defs) a register class
// before any select() call and before the post-selection COPY optimization
// in InstructionSelect.cpp calls MRI.getRegClass. The previous per-MI
// invocation made selection O(N_vregs × N_instrs) (GOALS W45).
//
// The class choice is bank-aware (bankAwareRegClass): a 64-bit AR-banked
// vreg constrains to ARRegClass, not the blanket DR64 size default — the
// old blanket DR64 silently failed (ARRegBank does not cover DR64RegClass)
// and left the vreg bank-only. Vregs the selector itself creates during
// selection all carry an explicit class (createVirtualRegister), so the
// once-per-MF walk covers every bank-only vreg that can exist here.
void HaydnInstructionSelector::preConstrainBankOnlyVRegs(
    MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    Register VReg = Register::index2VirtReg(I);
    if (MRI.getRegClassOrNull(VReg))
      continue;
    if (MRI.use_empty(VReg) && MRI.def_empty(VReg))
      continue;
    if (!MRI.getRegBankOrNull(VReg))
      continue;
    constrainBankAware(VReg, MRI);
  }
}

// Constrain through bankAwareRegClass. For the non-allocatable ARRegClass,
// RBI::constrainGenericRegister → MRI::setRegClass would assert
// (isAllocatable=0), so enter via setRegClassOrRegBank — the one constraint
// entry with no allocatability check (W36 precedent).
bool HaydnInstructionSelector::constrainBankAware(Register VReg,
                                                  MachineRegisterInfo &MRI) {
  const TargetRegisterClass *RC = bankAwareRegClass(VReg, MRI);
  if (RC->isAllocatable())
    return RBI.constrainGenericRegister(VReg, *RC, MRI) != nullptr;
  MRI.setRegClassOrRegBank(VReg, RC);
  return true;
}

bool HaydnInstructionSelector::emitScalarMul32(MachineInstr &I, Register Dst,
                                               Register Src0, Register Src1,
                                               MachineIRBuilder &MIB,
                                               MachineRegisterInfo &MRI) {
  if (Dst.isVirtual())
    RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
  if (Src0.isVirtual())
    RBI.constrainGenericRegister(Src0, Haydn::GPR32RegClass, MRI);
  if (Src1.isVirtual())
    RBI.constrainGenericRegister(Src1, Haydn::GPR32RegClass, MRI);

  // MULL: rd = low32(rs1 * rs2). Matches C/LLVM mul i32 wrap semantics.
  MachineInstr *MI =
      MIB.buildInstr(Haydn::MULL).addDef(Dst).addReg(Src0).addReg(Src1);
  return constrainSelectedInstRegOperands(*MI, TII, TRI, RBI);
}

Register HaydnInstructionSelector::extractVecLane0AsGPR32(
    MachineIRBuilder &MIB, Register AmtVec, MachineRegisterInfo &MRI) {
  if (AmtVec.isVirtual())
    RBI.constrainGenericRegister(AmtVec, Haydn::DR64RegClass, MRI);
  Register Lo = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
  Register Hi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
  MachineInstr *Unmerge = MIB.buildInstr(Haydn::MOV_DR64_TO_GPR)
                              .addDef(Lo)
                              .addDef(Hi)
                              .addReg(AmtVec);
  if (!constrainSelectedInstRegOperands(*Unmerge, TII, TRI, RBI))
    return Register();
  return Lo;
}

bool HaydnInstructionSelector::select(MachineInstr &I) {
  MachineFunction &MF = *I.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  // Bank-only vregs were pre-constrained once per MF in setupMF (W45);
  // no per-MI walk here.

  // Handle COPY instructions explicitly to ensure virtual register operands
  // are constrained to the proper register class.
  // This handles cases like %0:gpr32regbank(s32) = COPY $r0 where the virtual
  // register has a regbank but no regclass.
  if (I.isCopy()) {
    // Handle COPY from $noreg (undef) — just erase it.
    if (I.getOperand(1).getReg() == MCRegister::NoRegister) {
      I.eraseFromParent();
      return true;
    }

    for (unsigned OpIdx = 0; OpIdx < I.getNumOperands(); ++OpIdx) {
      MachineOperand &Op = I.getOperand(OpIdx);
      if (Op.isReg() && Op.getReg().isVirtual()) {
        // One bank-aware rule (bankAwareRegClass via constrainBankAware):
        // existing class wins; 64-bit AR-banked → ARRegClass (never the
        // blanket DR64 size default — the old COPY inference checked
        // DR64/size BEFORE the AR bank, so a 64-bit AR-banked operand
        // mis-banked to DR64, the W36 residual); else the size default.
        // Haydn does not override getConstrainedRegClassForOperand, so the
        // TRI call here always returned nullptr and is dropped.
        if (!constrainBankAware(Op.getReg(), MRI)) {
          LLVM_DEBUG(
              dbgs() << "Failed to constrain COPY operand to register class\n");
          return false;
        }
      }
    }
    return true;
  }

  // Target-specific instructions (non-G_* opcodes) are already selected.
  // Constrain their register operands to the proper register class.
  //
  // Gate on the MCID::PreISelOpcode FLAG (MachineInstr::isPreISelOpcode), not
  // the isPreISelGenericOpcode opcode-RANGE test. The range test only covers
  // the shared TargetOpcode namespace (G_ADD..G_UBFX); target-namespaced
  // generic ops like Haydn::G_MAC32 (enum 362) are ABOVE that range and so
  // would be misclassified as "already selected" and silently skipped here
  // leaking the unselected generic op into machine code. The flag test
  // returns true for BOTH standard and target generic ops (all set
  // MCID::PreIselOpcode via GenericInstruction), so !flag is true only for
  // genuinely-already-selected target instrs (MAC32, ADD32,...).
  // Do not call selectImpl here: TableGen patterns match generic ops, not
  // already-selected target MIs (T-GS5; RISCV/AArch64 constrain-only tail).
  if (!I.isPreISelOpcode()) {
    if (I.isPseudo())
      return true;
    return constrainSelectedInstRegOperands(I, TII, TRI, RBI);
  }

  // Try TableGen-generated patterns first (HaydnGISel.td Pats).
  if (selectImpl(I, *CoverageInfo))
    return true;

  // C++ residual allowlist. Classes:
  //   Pat-covered (return false here): s32/s64/v2i32/v4i16 binops, s32/s64
  //     minmax (signed), s32 shifts, s64 shifts, s32 mul, v2i32 mul, mulh,
  //     abs s32/s64, s32 select → MOVT32
  //   Permanent C++ (this switch):
  //     G_ICMP (s32/s64 multi-instr), G_SELECT s64 (dual MOVT),
  //     G_SHL/LSHR/ASHR SIMD only,
  //     G_LOAD/STORE/Z/SEXTLOAD (MMO), G_HAYDN_* AGU, G_MULA64*,
  //     G_HAYDN_MUL64_WIDEN{,U},
  //     G_Z/S/ANYEXT G_TRUNC multi-width, MERGE/UNMERGE, VAARG/VASTART,
  //     G_INTRINSIC*, BRJT/dyn stack/trap peeps, BUILD_VECTOR, constants
  //   Delete candidates: none left for pure 1:1 after Pats (s32 G_SELECT
  //     dual home removed). G_MUL has no C++ arm: s32/v2i32 are Pats;
  //     leftover G_MUL fails closed (legalizer owns s64 widen/schoolbook).
  LLVM_DEBUG(dbgs() << "Falling through to custom selection for: " << I << "\n");
  unsigned Opcode = I.getOpcode();

  switch (Opcode) {
  // CB-127: Haydn has no prefetch ISA; treat as compile-time nop.
  case TargetOpcode::G_PREFETCH:
    I.eraseFromParent();
    return true;

  case TargetOpcode::G_CONSTANT: {
    // Materialize constant for s32, s64, and pointer types.
    // Uses HaydnMatInt to compute the optimal instruction sequence.
    Register Dst = I.getOperand(0).getReg();
    const ConstantInt *C = I.getOperand(1).getCImm();
    int64_t Imm = C->getSExtValue();
    LLT DstTy = MRI.getType(Dst);

    MachineIRBuilder MIB(I);

    if (DstTy.getSizeInBits() <= 32) {
      // Constrain the destination register before building instructions.
      // constrainSelectedInstRegOperands needs the def register to have a class.
      RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);

      // s32/p0 constant — emit a single rematerializable LOADI32 (expanded
      // post-RA via HaydnMatInt). Do NOT emit multi-instr LUI+ADDI sequences
      // here: they are not a remat unit, so RA may spill one piece across a
      // diamond and reload an uninitialized stack slot on the other arm
      // (same failure class as non-remat LOAD_ADDR / seed 3148).
      MachineInstr *LI32 =
          MIB.buildInstr(Haydn::LOADI32).addDef(Dst).addImm(Imm);
      if (!constrainSelectedInstRegOperands(*LI32, TII, TRI, RBI))
        return false;
    } else {
      // s64 constant: emit a single rematerializable LOADI64 pseudo. Expanded
      // post-RA (HaydnInstrInfo::expandPostRAPseudo) into lo32/hi32 HaydnMatInt
      // sequences materialised into the reserved R12 scratch + a transient
      // 8-byte SP slot + LD64. As a single immediate-operand pseudo it is
      // trivially rematerialisable, so RA re-emits it at each use instead of
      // cross-block-copying the DR64 via OR64 — which fails dominance for i64
      // constants used in non-dominating join blocks. (Sign-extension
      // and other hi-half optimisations move into the expander with this change;
      // the MatInt cost is unchanged in the common case.)
      RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
      MachineInstr *LI64 = MIB.buildInstr(Haydn::LOADI64)
                               .addDef(Dst)
                               .addImm(Imm);
      if (!constrainSelectedInstRegOperands(*LI64, TII, TRI, RBI))
        return false;
    }

    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_FRAME_INDEX: {
    // Frame index → materialize the stack address.
    // Modify in-place to ADDI32, preserving the frame index operand so that
    // eliminateFrameIndex (HaydnRegisterInfo) can replace it with the actual
    // SP-relative offset during prologue/epilogue.
    //
    // Before: %dst = G_FRAME_INDEX %fi
    // After: %dst = ADDI32 %fi, 0
    // eliminateFrameIndex will later change %fi to SP/FP and add the offset.
    I.setDesc(TII.get(Haydn::ADDI32_W));
    I.addOperand(MachineOperand::CreateImm(0));
    return constrainSelectedInstRegOperands(I, TII, TRI, RBI);
  }

  case TargetOpcode::G_GLOBAL_VALUE: {
    // Global address → materialize using LUI + ADDI32_W with HI12/LO20 fixups
    // Use LOAD_ADDR pseudo which expands to the proper instruction sequence.
    Register Dst = I.getOperand(0).getReg();
    const GlobalValue *GV = I.getOperand(1).getGlobal();

    // Baremetal Haydn has no TLS model — a thread_local global has no
    // defined address equation. Selecting it as an ordinary global would
    // silently materialize the TLS *template* address (wrong code for every
    // thread at runtime), so fail closed with a named diagnostic instead of
    // falling through to LOAD_ADDR (RISCV rejects the same way).
    if (GV->isThreadLocal()) {
      reportGISelFailure(*I.getMF(), *MORE, "HaydnInstructionSelector",
                         "TLS global address: baremetal Haydn has no TLS "
                         "model (T-ABI10)",
                         I);
      return false;
    }

    MachineIRBuilder MIB(I);
    MachineInstr *LoadMI = MIB.buildInstr(Haydn::LOAD_ADDR)
                                .addDef(Dst)
                                .addGlobalAddress(GV);
    if (!constrainSelectedInstRegOperands(*LoadMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_BLOCK_ADDR: {
    // Block address (computed goto / label pointer) → materialize address.
    // Use LOAD_ADDR pseudo with the block address operand.
    // The AsmPrinter expands this to LUI + ADDI32_W with HI12/LO20 fixups.
    Register Dst = I.getOperand(0).getReg();
    const BlockAddress *BA = I.getOperand(1).getBlockAddress();

    MachineIRBuilder MIB(I);
    MachineInstr *LoadMI = MIB.buildInstr(Haydn::LOAD_ADDR)
                                .addDef(Dst)
                                .addBlockAddress(BA);
    if (!constrainSelectedInstRegOperands(*LoadMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_BR: {
    // Unconditional branch → logical BEQZ_W R0, target (R0 is always zero,
    // always branches). Format E encode after post-RA placement.
    MachineBasicBlock *TargetBB = I.getOperand(0).getMBB();

    MachineIRBuilder MIB(I);
    MachineInstrBuilder Br = MIB.buildInstr(Haydn::BEQZ_W)
                                   .addReg(Haydn::R0)
                                   .addMBB(TargetBB);
    if (!constrainSelectedInstRegOperands(*Br, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_BRCOND: {
    // Conditional branch → logical BNEZ_W (branch if not zero). Format E
    // encode after post-RA placement.
    Register Cond = I.getOperand(0).getReg();
    MachineBasicBlock *TrueBB = I.getOperand(1).getMBB();

    // Check if this is a hardware-loop latch branch. The upstream
    // HardwareLoops pass produces two forms:
    // ZOL: G_BRCOND(llvm.loop.decrement, TrueBB)
    // JNZD: G_BRCOND(G_ICMP ne, llvm.loop.decrement.reg, 0, TrueBB)
    // For ZOL we emit PseudoLoopEnd (the SMS-safe meta-branch terminator).
    // For JNZD we emit LoopDec + LoopJNZ. Mirrors AIE's
    // selectBrCondLoopDecrement (AIEBaseInstructionSelector.cpp:144-173)
    // and selectBrCondLoopDecrementReg (:176-257).
    // T-GS4: erase only when each folded def has one non-dbg use (AIE2
    // hasOneUse before killing a folded producer,
    // AIE2InstructionSelector.cpp:726). Direct def of Cond — do not fold
    // through copies (erasing the producer would undef the copy).
    MachineInstr *CondDef = MRI.getVRegDef(Cond);
    const bool CondOneUse = MRI.hasOneNonDBGUse(Cond);

    // ZOL: condition is directly llvm.loop.decrement
    if (CondOneUse && CondDef &&
        CondDef->getOpcode() == TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS) {
      auto *GI = dyn_cast<GIntrinsic>(CondDef);
      if (GI && GI->getIntrinsicID() == Intrinsic::loop_decrement) {
        MachineIRBuilder MIB(I);
        MachineInstr *PLE = MIB.buildInstr(Haydn::PseudoLoopEnd)
                                .addMBB(TrueBB);
        if (!constrainSelectedInstRegOperands(*PLE, TII, TRI, RBI))
          return false;
        CondDef->eraseFromParent();
        I.eraseFromParent();
        return true;
      }
    }

    // JNZD: condition is G_ICMP ne, llvm.loop.decrement.reg, 0
    if (CondOneUse && CondDef && CondDef->getOpcode() == TargetOpcode::G_ICMP) {
      const auto Pred = static_cast<CmpInst::Predicate>(
          CondDef->getOperand(1).getPredicate());
      if (Pred == CmpInst::ICMP_NE) {
        auto CmpRHS = getIConstantVRegValWithLookThrough(
            CondDef->getOperand(3).getReg(), MRI);
        if (CmpRHS && CmpRHS->Value == 0) {
          Register CmpLHSReg = CondDef->getOperand(2).getReg();
          auto *CmpLHS = MRI.getVRegDef(CmpLHSReg);
          if (CmpLHS &&
              MRI.hasOneNonDBGUse(CmpLHSReg) &&
              CmpLHS->getOpcode() ==
                  TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS) {
            auto *GI = dyn_cast<GIntrinsic>(CmpLHS);
            if (GI && GI->getIntrinsicID() == Intrinsic::loop_decrement_reg &&
                MRI.hasOneNonDBGUse(CmpLHS->getOperand(0).getReg())) {
              Register NewLC = CmpLHS->getOperand(0).getReg();
              Register PrevLC = CmpLHS->getOperand(2).getReg();
              MachineIRBuilder MIB(I);
              auto LD = MIB.buildInstr(Haydn::LoopDec, {NewLC}, {PrevLC});
              if (!constrainSelectedInstRegOperands(*LD, TII, TRI, RBI))
                return false;
              auto LJ = MIB.buildInstr(Haydn::LoopJNZ, {}, {NewLC}).addMBB(
                  TrueBB);
              if (!constrainSelectedInstRegOperands(*LJ, TII, TRI, RBI))
                return false;
              CmpLHS->eraseFromParent();
              CondDef->eraseFromParent();
              I.eraseFromParent();
              return true;
            }
          }
        }
      }
    }

    MachineIRBuilder MIB(I);
    MachineInstr *Branch =
        MIB.buildInstr(Haydn::BNEZ_W).addReg(Cond).addMBB(TrueBB);
    if (!constrainSelectedInstRegOperands(*Branch, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_JUMP_TABLE: {
    // G_JUMP_TABLE: materialize jump table base address.
    // Jump tables live in.rodata, whose address (high memory) does not fit in
    // a single ADDI32 simm12 field — a bare ADDI32 produces a single LO16 fixup
    // that the linker cannot reach, corrupting the base (: base
    // became 0xFFF80100 → OOB load → jalr 0 → infinite restart).
    // Use the LOAD_ADDR pseudo (same path as G_GLOBAL_VALUE / G_BLOCK_ADDR) so
    // ExpandPseudos emits the full LUI + ADDI32_W pair with HI12 / LO20 fixups.
    Register DstReg = I.getOperand(0).getReg();
    unsigned JTI = I.getOperand(1).getIndex();

    MachineIRBuilder MIB(I);
    MachineInstr *JTAddr = MIB.buildInstr(Haydn::LOAD_ADDR)
                               .addDef(DstReg)
                               .addJumpTableIndex(JTI);
    if (!constrainSelectedInstRegOperands(*JTAddr, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_BRJT: {
    // Jump table indirect branch.
    // G_BRJT has: (jt_ptr, jti, index)
    // Sequence (matches RISC-V GISel / TargetLowering PIC contract):
    // 1. Scale index by 4 (32-bit entries)
    // 2. EntryAddr = JTBase + index*4; load EntryVal = [EntryAddr]
    // 3. Absolute target:
    // EK_BlockAddress (non-PIC): EntryVal is already absolute
    // EK_LabelDifference32 (PIC/PIE): EntryVal = target - JTBase
    // → Abs = EntryVal + JTBase (REQUIRED; missing this → BAD_PC
    // on BundleSim, e.g. jalr 0xffff5494 from relative JT entries
    // in llvm-libc printf_core under -fpie)
    // 4. BR_JT Abs → AsmPrinter expands to JALR
    Register JTBase = I.getOperand(0).getReg();
    unsigned JTI = I.getOperand(1).getIndex();
    Register Index = I.getOperand(2).getReg();

    MachineIRBuilder MIB(I);

    // Scale index by 4 for 32-bit entries
    Register ScaledIndex = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
    MachineInstr *ScaleMI = MIB.buildInstr(Haydn::SLLI32)
                                .addDef(ScaledIndex)
                                .addReg(Index)
                                .addImm(2); // shift left by 2 (multiply by 4)
    if (!constrainSelectedInstRegOperands(*ScaleMI, TII, TRI, RBI))
      return false;

    // Calculate jump table entry address: JTBase + ScaledIndex
    Register JTEAddr = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
    MachineInstr *AddrMI = MIB.buildInstr(Haydn::ADD32)
                              .addDef(JTEAddr)
                              .addReg(JTBase)
                              .addReg(ScaledIndex);
    if (!constrainSelectedInstRegOperands(*AddrMI, TII, TRI, RBI))
      return false;

    // Load the table entry value.
    Register EntryVal = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
    MachineInstr *LoadMI = MIB.buildInstr(Haydn::LD32)
                              .addDef(EntryVal)
                              .addReg(JTEAddr)
                              .addImm(0); // offset 0
    if (!constrainSelectedInstRegOperands(*LoadMI, TII, TRI, RBI))
      return false;

    Register TargetAddr = EntryVal;
    const MachineJumpTableInfo *MJTI = I.getMF()->getJumpTableInfo();
    // Default TargetLowering::getJumpTableEncoding uses LabelDifference32 under
    // PIC/PIE (-fpie is default for llvm-libc Haydn builds). Only BlockAddress
    // entries are absolute; relative kinds need +JTBase (RISC-V same rule).
    if (MJTI &&
        MJTI->getEntryKind() == MachineJumpTableInfo::EK_LabelDifference32) {
      Register AbsAddr = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      MachineInstr *AddMI = MIB.buildInstr(Haydn::ADD32)
                                .addDef(AbsAddr)
                                .addReg(EntryVal)
                                .addReg(JTBase);
      if (!constrainSelectedInstRegOperands(*AddMI, TII, TRI, RBI))
        return false;
      TargetAddr = AbsAddr;
    }

    // Branch to the target address using BR_JT pseudo-instruction
    // BR_JT will be expanded by the AsmPrinter to the actual JALR sequence.
    //
    // The JTI operand MUST be a MO_JumpTableIndex (not MO_Immediate) so that
    // BranchFolding's live-JT scan (BranchFolding.cpp:258, Op.isJTI) recognizes
    // this block as referencing the jump table. With MO_Immediate, BranchFolding
    // marks the JT dead and clears its MBBs (RemoveJumpTable), causing
    // "Undefined temporary symbol.LJTI" at assembly time.
    MachineInstr *BRJTMI = MIB.buildInstr(Haydn::BR_JT)
                                  .addReg(TargetAddr) // absolute target address
                                  .addJumpTableIndex(JTI); // jump table index (MO_JumpTableIndex)
    if (!constrainSelectedInstRegOperands(*BRJTMI, TII, TRI, RBI))
      return false;

    // Note: We do NOT clear the successor list here. The successors were set up
    // for G_BRJT (which implicitly branches to jump table entries), and after
    // expansion to BR_JT, the verifier still expects the block to have successors.
    // The BR_JT instruction is an indirect branch that can reach any of the
    // jump table targets, so the successor list remains valid.

    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_BRINDIRECT: {
    // Indirect branch: jump to address held in a register.
    // G_BRINDIRECT has one operand: the target address (p0).
    // Emit JALR_W R0, %src, 0 — R0 as destination discards the return address
    // making this a pure jump (not a call). The offset is 0 because the full
    // target address is already in the register.
    //
    // Logical JALR_W; Format E encode after post-RA placement. The
    // calltarget_wide_ri12 operand class emits FIXUP_HAYDN_WIDE_BranchSImm12
    // via getSImmOpValueXStepWide.
    Register Target = I.getOperand(0).getReg();

    MachineIRBuilder MIB(I);
    MachineInstr *JmpMI = MIB.buildInstr(Haydn::JALR_W)
                              .addReg(Haydn::R0, RegState::Define)
                              .addReg(Target)
                              .addImm(0);
    if (!constrainSelectedInstRegOperands(*JmpMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_STACKRESTORE: {
    // Restore SP from a saved value (used for VLAs and alloca).
    // G_STACKRESTORE has one operand: the pointer value to restore SP to.
    // Simply COPY the operand into R13 (SP).
    Register Src = I.getOperand(0).getReg();

    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, Haydn::GPR32RegClass, MRI);

    MachineIRBuilder MIB(I);
    MachineInstr *CopyMI =
        MIB.buildInstr(TargetOpcode::COPY,
                       {Register(Haydn::R13)}, {Src});
    if (!constrainSelectedInstRegOperands(*CopyMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_STACKSAVE: {
    // Save current SP for later restoration (used for VLAs and alloca).
    // G_STACKSAVE has one def: a pointer register to hold the saved SP.
    // Simply COPY R13 (SP) into the destination.
    Register Dst = I.getOperand(0).getReg();

    RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);

    MachineIRBuilder MIB(I);
    MachineInstr *CopyMI =
        MIB.buildInstr(TargetOpcode::COPY,
                       {Dst}, {Register(Haydn::R13)});
    if (!constrainSelectedInstRegOperands(*CopyMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_DYN_STACKALLOC: {
    // Dynamic stack allocation: allocate N bytes on the stack.
    // G_DYN_STACKALLOC: dst = SP - size, with optional alignment.
    // Lower to: SUB32 dst, SP, size; then COPY SP, dst.
    Register Dst = I.getOperand(0).getReg();
    Register Size = I.getOperand(1).getReg();

    MachineIRBuilder MIB(I);

    RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
    if (Size.isVirtual())
      RBI.constrainGenericRegister(Size, Haydn::GPR32RegClass, MRI);

    // dst = SP - size
    MachineInstr *SubMI = MIB.buildInstr(Haydn::SUB32)
                              .addDef(Dst)
                              .addReg(Haydn::R13)
                              .addReg(Size);
    if (!constrainSelectedInstRegOperands(*SubMI, TII, TRI, RBI))
      return false;

    // SP = dst
    MachineInstr *CopySP =
        MIB.buildInstr(TargetOpcode::COPY,
                       {Register(Haydn::R13)}, {Dst});
    if (!constrainSelectedInstRegOperands(*CopySP, TII, TRI, RBI))
      return false;

    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_ICMP: {
    // Integer comparison → SLT32/SLTU32/SEQ32 etc.
    // G_ICMP result may be s1 (legal for this type). Haydn compare instructions
    // produce s32 results in GPR32. Create a new s32 vreg and replace all uses
    // of the original s1/s32 dest with the new s32 compare result.
    Register Dst = I.getOperand(0).getReg();
    auto Pred =
        static_cast<CmpInst::Predicate>(I.getOperand(1).getPredicate());
    Register Op0 = I.getOperand(2).getReg();
    Register Op1 = I.getOperand(3).getReg();
    LLT Op0Ty = MRI.getType(Op0);
    bool IsS64 = Op0Ty.isValid() && Op0Ty.getSizeInBits() == 64;

    MachineIRBuilder MIB(I);

    // For s64 comparisons, we need to compare hi and lo parts separately
    // and combine the results.
    // EQ: hi_eq AND lo_eq
    // NE: hi_ne OR lo_ne
    // UGT: hi_gt OR (hi_eq AND lo_gt)
    // ULT: hi_lt OR (hi_eq AND lo_lt)
    // UGE: hi_gt OR (hi_eq AND lo_ge) == NOT(ULT)
    // ULE: hi_lt OR (hi_eq AND lo_le) == NOT(UGT)
    // SGT: sgt similar to UGT but using signed comparison for hi
    // SLT: slt similar to ULT but using signed comparison for hi
    // SGE: NOT(SLT)
    // SLE: NOT(SGT)

    if (IsS64) {
      // Split s64 operands into lo/hi s32 parts
      Register Op0Lo = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      Register Op0Hi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      Register Op1Lo = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      Register Op1Hi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);

      if (Op0.isVirtual())
        RBI.constrainGenericRegister(Op0, Haydn::DR64RegClass, MRI);
      if (Op1.isVirtual())
        RBI.constrainGenericRegister(Op1, Haydn::DR64RegClass, MRI);

      MachineInstr *Unmerge0 = MIB.buildInstr(Haydn::MOV_DR64_TO_GPR)
                                   .addDef(Op0Lo)
                                   .addDef(Op0Hi)
                                   .addReg(Op0);
      if (!constrainSelectedInstRegOperands(*Unmerge0, TII, TRI, RBI))
        return false;

      MachineInstr *Unmerge1 = MIB.buildInstr(Haydn::MOV_DR64_TO_GPR)
                                   .addDef(Op1Lo)
                                   .addDef(Op1Hi)
                                   .addReg(Op1);
      if (!constrainSelectedInstRegOperands(*Unmerge1, TII, TRI, RBI))
        return false;

      // Compare hi parts (LT direction: Op0Hi < Op1Hi)
      // Used directly by LT/GE cases; GT/LE cases compute swapped HiGt separately.
      unsigned HiCmpOpc;
      bool IsUnsigned = CmpInst::isUnsigned(Pred);
      HiCmpOpc = IsUnsigned ? Haydn::SLTU32 : Haydn::SLT32;

      Register HiCmp = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      MachineInstr *HiCmpMI = MIB.buildInstr(HiCmpOpc)
                                  .addDef(HiCmp)
                                  .addReg(Op0Hi)
                                  .addReg(Op1Hi);
      if (!constrainSelectedInstRegOperands(*HiCmpMI, TII, TRI, RBI))
        return false;

      // Check hi equality
      Register HiEq = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      MachineInstr *HiEqMI = MIB.buildInstr(Haydn::SEQ32)
                                 .addDef(HiEq)
                                 .addReg(Op0Hi)
                                 .addReg(Op1Hi);
      if (!constrainSelectedInstRegOperands(*HiEqMI, TII, TRI, RBI))
        return false;

      // Combine results based on predicate
      Register Result;
      switch (Pred) {
      case CmpInst::ICMP_EQ:
        // EQ: hi_eq AND lo_eq
        Result = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        {
          Register LoEq = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
          MachineInstr *LoEqMI = MIB.buildInstr(Haydn::SEQ32)
                                      .addDef(LoEq)
                                      .addReg(Op0Lo)
                                      .addReg(Op1Lo);
          if (!constrainSelectedInstRegOperands(*LoEqMI, TII, TRI, RBI))
            return false;
          MachineInstr *AndMI = MIB.buildInstr(Haydn::AND32)
                                    .addDef(Result)
                                    .addReg(HiEq)
                                    .addReg(LoEq);
          if (!constrainSelectedInstRegOperands(*AndMI, TII, TRI, RBI))
            return false;
        }
        break;

      case CmpInst::ICMP_NE:
        // NE: LOGICAL-NOT(hi_eq AND lo_eq).
        // AndResult is 0 or 1 (two SEQ32 results ANDed). LOGICAL-NOT of a 0/1
        // predicate must be `rs ^ 1`, NOT bitwise `NOT32 rs` (= ~rs). ~0 = -1
        // and ~1 = -2 are both nonzero, so a downstream BNEZ would ALWAYS
        // branch — e.g. `while(x != 0)` would never exit.
        // Haydn has no LOGNOT; the canonical idiom is XORI32 rd, rs, 1.
        {
          Register LoEq = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
          MachineInstr *LoEqMI = MIB.buildInstr(Haydn::SEQ32)
                                      .addDef(LoEq)
                                      .addReg(Op0Lo)
                                      .addReg(Op1Lo);
          if (!constrainSelectedInstRegOperands(*LoEqMI, TII, TRI, RBI))
            return false;
          Register AndResult = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
          MachineInstr *AndMI = MIB.buildInstr(Haydn::AND32)
                                    .addDef(AndResult)
                                    .addReg(HiEq)
                                    .addReg(LoEq);
          if (!constrainSelectedInstRegOperands(*AndMI, TII, TRI, RBI))
            return false;
          Result = emitInvert01(MIB, AndResult, TII, TRI, RBI, MRI);
          if (!Result.isValid())
            return false;
        }
        break;

      case CmpInst::ICMP_UGT:
      case CmpInst::ICMP_SGT: {
        // GT: hi_gt OR (hi_eq AND lo_gt)
        // HiCmp = SLT(Op0Hi, Op1Hi) = hi_lt, so swap to get hi_gt:
        // hi_gt = SLT(Op1Hi, Op0Hi)
        bool IsUnsigned = (Pred == CmpInst::ICMP_UGT);
        unsigned GtCmpOpc = IsUnsigned ? Haydn::SLTU32 : Haydn::SLT32;

        Register HiGt = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *HiGtMI = MIB.buildInstr(GtCmpOpc)
                                    .addDef(HiGt)
                                    .addReg(Op1Hi)
                                    .addReg(Op0Hi);
        if (!constrainSelectedInstRegOperands(*HiGtMI, TII, TRI, RBI))
          return false;

        // Low-half compare is always UNSIGNED (the low 32 bits carry no sign);
        // only the high half (GtCmpOpc above) reflects the predicate's signedness.
        unsigned LoGtCmp = Haydn::SLTU32;
        Register LoGt = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *LoGtMI = MIB.buildInstr(LoGtCmp)
                                    .addDef(LoGt)
                                    .addReg(Op1Lo)
                                    .addReg(Op0Lo);
        if (!constrainSelectedInstRegOperands(*LoGtMI, TII, TRI, RBI))
          return false;

        // lo_gt_and_hi_eq = lo_gt & hi_eq
        Register LoGtAndHiEq = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *AndMI = MIB.buildInstr(Haydn::AND32)
                                  .addDef(LoGtAndHiEq)
                                  .addReg(LoGt)
                                  .addReg(HiEq);
        if (!constrainSelectedInstRegOperands(*AndMI, TII, TRI, RBI))
          return false;

        // result = hi_gt | lo_gt_and_hi_eq
        Result = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *OrMI = MIB.buildInstr(Haydn::OR32)
                                 .addDef(Result)
                                 .addReg(HiGt)
                                 .addReg(LoGtAndHiEq);
        if (!constrainSelectedInstRegOperands(*OrMI, TII, TRI, RBI))
          return false;
        break;
      }

      case CmpInst::ICMP_ULT:
      case CmpInst::ICMP_SLT: {
        // LT: hi_lt OR (hi_eq AND lo_lt)
        // hi_lt is already in HiCmp (or we need to swap for ULT)
        // ICMP_ULT vs ICMP_SLT only differs in the HIGH-half signedness (carried
        // by HiCmp computed upstream); the low half is unsigned for both.
        Register HiLt = HiCmp;  // Already computed with SLTU32/SLT32

        Register LoLt = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        // Low-half compare is always UNSIGNED (low 32 bits carry no sign).
        unsigned LoLtCmp = Haydn::SLTU32;
        MachineInstr *LoLtMI = MIB.buildInstr(LoLtCmp)
                                    .addDef(LoLt)
                                    .addReg(Op0Lo)
                                    .addReg(Op1Lo);
        if (!constrainSelectedInstRegOperands(*LoLtMI, TII, TRI, RBI))
          return false;

        // lo_lt_and_hi_eq = lo_lt & hi_eq
        Register LoLtAndHiEq = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *AndMI = MIB.buildInstr(Haydn::AND32)
                                  .addDef(LoLtAndHiEq)
                                  .addReg(LoLt)
                                  .addReg(HiEq);
        if (!constrainSelectedInstRegOperands(*AndMI, TII, TRI, RBI))
          return false;

        // result = hi_lt | lo_lt_and_hi_eq
        Result = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *OrMI = MIB.buildInstr(Haydn::OR32)
                                 .addDef(Result)
                                 .addReg(HiLt)
                                 .addReg(LoLtAndHiEq);
        if (!constrainSelectedInstRegOperands(*OrMI, TII, TRI, RBI))
          return false;
        break;
      }

      case CmpInst::ICMP_UGE:
      case CmpInst::ICMP_SGE: {
        // GE: LOGICAL-NOT(LT). LtResult is 0 or 1; logical-not is XORI32 _,_,1.
        // See ICMP_NE: bitwise NOT32 of a 0/1 value is always nonzero and
        // corrupts any downstream branch on the predicate.
        // Compute LT first, then negate. ICMP_UGE vs ICMP_SGE only differs in
        // the HIGH-half signedness (carried by HiCmp); the low half is unsigned.
        unsigned LoLtCmp = Haydn::SLTU32;
        Register LoLt = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *LoLtMI = MIB.buildInstr(LoLtCmp)
                                    .addDef(LoLt)
                                    .addReg(Op0Lo)
                                    .addReg(Op1Lo);
        if (!constrainSelectedInstRegOperands(*LoLtMI, TII, TRI, RBI))
          return false;

        Register LoLtAndHiEq = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *AndMI = MIB.buildInstr(Haydn::AND32)
                                  .addDef(LoLtAndHiEq)
                                  .addReg(LoLt)
                                  .addReg(HiEq);
        if (!constrainSelectedInstRegOperands(*AndMI, TII, TRI, RBI))
          return false;

        Register LtResult = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *OrMI = MIB.buildInstr(Haydn::OR32)
                                 .addDef(LtResult)
                                 .addReg(HiCmp)
                                 .addReg(LoLtAndHiEq);
        if (!constrainSelectedInstRegOperands(*OrMI, TII, TRI, RBI))
          return false;

        Result = emitInvert01(MIB, LtResult, TII, TRI, RBI, MRI);

        if (!Result.isValid())

          return false;
        break;
      }

      case CmpInst::ICMP_ULE:
      case CmpInst::ICMP_SLE: {
        // LE: LOGICAL-NOT(GT). GtResult is 0 or 1; logical-not is XORI32 _,_,1.
        // See ICMP_NE: bitwise NOT32 of a 0/1 value is always nonzero and
        // corrupts any downstream branch on the predicate.
        // Compute GT first (with swapped operands), then negate.
        bool IsULE = (Pred == CmpInst::ICMP_ULE);
        unsigned GtCmpOpc = IsULE ? Haydn::SLTU32 : Haydn::SLT32;

        // hi_gt = SLT(Op1Hi, Op0Hi) — swapped to get GT from LT
        Register HiGt = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *HiGtMI = MIB.buildInstr(GtCmpOpc)
                                    .addDef(HiGt)
                                    .addReg(Op1Hi)
                                    .addReg(Op0Hi);
        if (!constrainSelectedInstRegOperands(*HiGtMI, TII, TRI, RBI))
          return false;

        // Low-half compare is always UNSIGNED (low 32 bits carry no sign);
        // the high half (GtCmpOpc above) reflects the predicate's signedness.
        unsigned LoGtCmp = Haydn::SLTU32;
        Register LoGt = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *LoGtMI = MIB.buildInstr(LoGtCmp)
                                    .addDef(LoGt)
                                    .addReg(Op1Lo)
                                    .addReg(Op0Lo);
        if (!constrainSelectedInstRegOperands(*LoGtMI, TII, TRI, RBI))
          return false;

        Register LoGtAndHiEq = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *AndMI = MIB.buildInstr(Haydn::AND32)
                                  .addDef(LoGtAndHiEq)
                                  .addReg(LoGt)
                                  .addReg(HiEq);
        if (!constrainSelectedInstRegOperands(*AndMI, TII, TRI, RBI))
          return false;

        Register GtResult = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *OrMI = MIB.buildInstr(Haydn::OR32)
                                 .addDef(GtResult)
                                 .addReg(HiGt)
                                 .addReg(LoGtAndHiEq);
        if (!constrainSelectedInstRegOperands(*OrMI, TII, TRI, RBI))
          return false;

        Result = emitInvert01(MIB, GtResult, TII, TRI, RBI, MRI);

        if (!Result.isValid())

          return false;
        break;
      }

      default:
        LLVM_DEBUG(dbgs() << "Unsupported ICMP predicate: " << Pred);
        return false;
      }

      // Replace all uses of the original Dst with our s32 compare result.
      RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
      if (Dst != Result)
        MRI.replaceRegWith(Dst, Result);

      I.eraseFromParent();
      return true;
    }

    // s32 comparison path (original code)
    unsigned CmpOpc;
    bool NeedSwap = false;
    bool NeedInvert = false;
    switch (Pred) {
    case CmpInst::ICMP_EQ:
      CmpOpc = Haydn::SEQ32;
      break;
    case CmpInst::ICMP_NE:
      CmpOpc = Haydn::SEQ32;
      NeedInvert = true;
      break;
    case CmpInst::ICMP_UGT:
      CmpOpc = Haydn::SLTU32;
      NeedSwap = true;
      break;
    case CmpInst::ICMP_UGE:
      CmpOpc = Haydn::SLTU32;
      NeedInvert = true;
      break;
    case CmpInst::ICMP_ULT:
      CmpOpc = Haydn::SLTU32;
      break;
    case CmpInst::ICMP_ULE:
      CmpOpc = Haydn::SLTU32;
      NeedSwap = true;
      NeedInvert = true;
      break;
    case CmpInst::ICMP_SGT:
      CmpOpc = Haydn::SLT32;
      NeedSwap = true;
      break;
    case CmpInst::ICMP_SGE:
      CmpOpc = Haydn::SLT32;
      NeedInvert = true;
      break;
    case CmpInst::ICMP_SLT:
      CmpOpc = Haydn::SLT32;
      break;
    case CmpInst::ICMP_SLE:
      CmpOpc = Haydn::SLT32;
      NeedSwap = true;
      NeedInvert = true;
      break;
    default:
      LLVM_DEBUG(dbgs() << "Unsupported ICMP predicate: " << Pred);
      return false;
    }

    // SLT sets result to 1 if Rs < Rt (signed), else 0.
    // Swap operands if needed (e.g., for GT we want SLT Rt, Rs).
    Register SrcL = NeedSwap ? Op1 : Op0;
    Register SrcR = NeedSwap ? Op0 : Op1;

    // Create s32 result register in GPR32
    Register CmpResult = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
    // Ensure input operands are constrained to GPR32
    if (SrcL.isVirtual())
      RBI.constrainGenericRegister(SrcL, Haydn::GPR32RegClass, MRI);
    if (SrcR.isVirtual())
      RBI.constrainGenericRegister(SrcR, Haydn::GPR32RegClass, MRI);

    auto Cmp = MIB.buildInstr(CmpOpc)
                   .addDef(CmpResult)
                   .addReg(SrcL)
                   .addReg(SrcR);
    if (!constrainSelectedInstRegOperands(*Cmp, TII, TRI, RBI))
      return false;

    if (NeedInvert) {
      // Single authority: logical-not of 0/1 is XORI imm1 (not ADDI+XOR dual path).
      // A/B: seed1 fails with XORI packing, seed5 fails with ADDI+XOR packing
      // both forms are bitwise-correct; residual is schedule/RA density (track
      // separately). Prefer ISA-canonical XORI.
      CmpResult = emitInvert01(MIB, CmpResult, TII, TRI, RBI, MRI);
      if (!CmpResult.isValid())
        return false;
    }


    // Replace all uses of the original Dst with our s32 compare result.
    // G_BRCOND and G_SELECT will use the s32 value (non-zero = true).
    // Also constrain the original Dst register so the selection verifier
    // doesn't complain about a vreg with a bank but no register class.
    RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
    if (Dst != CmpResult)
      MRI.replaceRegWith(Dst, CmpResult);

    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_SELECT: {
    // s32 + Cond s32 → MOVT32 Pat (HaydnGISel.td). C++ residual when
    // selectImpl misses: (1) s32 dest with Cond typed s1 (common after
    // IRTranslator — not the same type predicate as the Pat); (2) s64 dual
    // MOVT multi-instr. Not a dual home for the Pat shape.
    Register Dst = I.getOperand(0).getReg();
    Register Cond = I.getOperand(1).getReg();
    Register TrueVal = I.getOperand(2).getReg();
    Register FalseVal = I.getOperand(3).getReg();
    LLT DstTy = MRI.getType(Dst);
    if (!DstTy.isValid())
      return false;

    MachineIRBuilder MIB(I);
    const unsigned DstBits = DstTy.getSizeInBits();

    // Scalar s32 residual when Cond is not s32 (Pat covers Cond s32 only).
    if (DstBits == 32 && !DstTy.isVector()) {
      LLT CondTy = MRI.getType(Cond);
      if (CondTy.isValid() && CondTy.getSizeInBits() == 32)
        return false; // Pat should have matched; fail closed if not.
      RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
      if (Cond.isVirtual())
        RBI.constrainGenericRegister(Cond, Haydn::GPR32RegClass, MRI);
      if (TrueVal.isVirtual())
        RBI.constrainGenericRegister(TrueVal, Haydn::GPR32RegClass, MRI);
      if (FalseVal.isVirtual())
        RBI.constrainGenericRegister(FalseVal, Haydn::GPR32RegClass, MRI);
      MachineInstr *MovtMI = MIB.buildInstr(Haydn::MOVT32)
                                 .addDef(Dst)
                                 .addReg(FalseVal)
                                 .addReg(TrueVal)
                                 .addReg(Cond);
      if (!constrainSelectedInstRegOperands(*MovtMI, TII, TRI, RBI))
        return false;
      I.eraseFromParent();
      return true;
    }

    // 64-bit payload (s64 scalar or v2i32/v4i16 in DR64): dual MOVT32 on
    // lo/hi halves. Vector types are also 64 bits — must not use isScalar-only.
    if (DstBits != 64)
      return false;

    // Dual MOVT multi-instr; not a single Pat.
    Register TrueValLo = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
    Register TrueValHi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
    Register FalseValLo = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
    Register FalseValHi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
    Register DstLo = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
    Register DstHi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);

    if (TrueVal.isVirtual())
      RBI.constrainGenericRegister(TrueVal, Haydn::DR64RegClass, MRI);
    if (FalseVal.isVirtual())
      RBI.constrainGenericRegister(FalseVal, Haydn::DR64RegClass, MRI);
    if (Cond.isVirtual())
      RBI.constrainGenericRegister(Cond, Haydn::GPR32RegClass, MRI);

    MachineInstr *UnmergeTrue = MIB.buildInstr(Haydn::MOV_DR64_TO_GPR)
                                     .addDef(TrueValLo)
                                     .addDef(TrueValHi)
                                     .addReg(TrueVal);
    if (!constrainSelectedInstRegOperands(*UnmergeTrue, TII, TRI, RBI))
      return false;

    MachineInstr *UnmergeFalse = MIB.buildInstr(Haydn::MOV_DR64_TO_GPR)
                                      .addDef(FalseValLo)
                                      .addDef(FalseValHi)
                                      .addReg(FalseVal);
    if (!constrainSelectedInstRegOperands(*UnmergeFalse, TII, TRI, RBI))
      return false;

    MachineInstr *MovtLo = MIB.buildInstr(Haydn::MOVT32)
                               .addDef(DstLo)
                               .addReg(FalseValLo)
                               .addReg(TrueValLo)
                               .addReg(Cond);
    if (!constrainSelectedInstRegOperands(*MovtLo, TII, TRI, RBI))
      return false;

    MachineInstr *MovtHi = MIB.buildInstr(Haydn::MOVT32)
                               .addDef(DstHi)
                               .addReg(FalseValHi)
                               .addReg(TrueValHi)
                               .addReg(Cond);
    if (!constrainSelectedInstRegOperands(*MovtHi, TII, TRI, RBI))
      return false;

    RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
    MachineInstr *MergeMI = MIB.buildInstr(Haydn::MOV_GPR_TO_DR64)
                                .addDef(Dst)
                                .addReg(DstLo)
                                .addReg(DstHi);
    if (!constrainSelectedInstRegOperands(*MergeMI, TII, TRI, RBI))
      return false;

    I.eraseFromParent();
    return true;
  }

  // G_ADD/SUB/AND/OR/XOR and G_SMAX/SMIN/UMAX/UMIN: selectImpl Pats only
  // (HaydnGISel.td). s64/v2i32 stay on DR64 (add64/xor64/…); no GPR-pair
  // aliasing. G_MUL s32/v2i32 and G_SMULH/UMULH are Pats; leftover G_MUL
  // fails closed (legalizer owns s64 widen/schoolbook).

  case Haydn::G_MULA64:
  case Haydn::G_MULA64U: {
    // Target generic ops formed early by the pre-legalizer combiner.
    // G_MULA64  rd, ra, rs1, rs2 -> MULA64_LL   (signed x signed low lane)
    // G_MULA64U rd, ra, rs1, rs2 -> MULA64_ULUL (unsigned x unsigned low lane)
    // rd = ra + rs1[31:0] * rs2[31:0] with matching signedness.
    // NOTE: MULA64_ULL is u×s (mixed), not u×u — do not select it here.
    // Tied accumulator ($rd = $rd_in) via distinct vregs Dst/Ra; two-address
    // coalescer resolves the tie post-RA (mirrors selectAccMAC).
    Register Dst = I.getOperand(0).getReg();
    Register Ra = I.getOperand(1).getReg();
    Register Rs1 = I.getOperand(2).getReg();
    Register Rs2 = I.getOperand(3).getReg();
    if (MRI.getType(Dst) != LLT::scalar(64))
      return false;
    unsigned MulAOpc =
        Opcode == Haydn::G_MULA64U ? Haydn::MULA64_ULUL : Haydn::MULA64_LL;
    MachineIRBuilder MIB(I);
    for (Register R : {Dst, Ra, Rs1, Rs2})
      if (R.isVirtual())
        RBI.constrainGenericRegister(R, Haydn::DR64RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(MulAOpc)
                           .addDef(Dst)
                           .addReg(Ra)
                           .addReg(Rs1)
                           .addReg(Rs2);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case Haydn::G_HAYDN_MUL64_WIDEN:
  case Haydn::G_HAYDN_MUL64_WIDENU: {
    // Legalizer rewrites G_MUL<s64> to these target generics (not MUL64_*).
    // G_HAYDN_MUL64_WIDEN  rd, rs1, rs2 -> MUL64_LL   (signed x signed low)
    // G_HAYDN_MUL64_WIDENU rd, rs1, rs2 -> MUL64_ULUL (unsigned x unsigned)
    // NOTE: MUL64_ULL is u×s (mixed), not u×u — do not select it here.
    Register Dst = I.getOperand(0).getReg();
    Register Rs1 = I.getOperand(1).getReg();
    Register Rs2 = I.getOperand(2).getReg();
    if (MRI.getType(Dst) != LLT::scalar(64))
      return false;
    unsigned MulOpc = Opcode == Haydn::G_HAYDN_MUL64_WIDENU ? Haydn::MUL64_ULUL
                                                           : Haydn::MUL64_LL;
    MachineIRBuilder MIB(I);
    for (Register R : {Dst, Rs1, Rs2})
      if (R.isVirtual())
        RBI.constrainGenericRegister(R, Haydn::DR64RegClass, MRI);
    MachineInstr *MI =
        MIB.buildInstr(MulOpc).addDef(Dst).addReg(Rs1).addReg(Rs2);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  // AIE-style fused pre/post-inc/dec memory (gMIR → AGU writeback).
  // Formed by HaydnPostLegalizerCombiner from G_LOAD/ZEXTLOAD/SEXTLOAD/STORE +
  // G_PTR_ADD. Product fused-vs-split lives here (not a post-RA expander):
  // scaled-simm6 imm → *_POST/PRE_IMM; non-const → *_POST/PRE_REG; otherwise
  // logical LD/ST + ADDI32_W. Peer: AIE2InstructionSelector.cpp:419-435
  // (G_AIE_POSTINC_* at ISel); HexagonISelDAGToDAG.cpp:88,166 / 484,542
  // (IsValidInc ? *_pi : *_io). Access size from MMO (1/2/4/8).
  // Load forms carry imm is_sext (op4): 1 → S_LBS/S_LHWS, 0 → S_LBU/S_LHWU.
  case Haydn::G_HAYDN_POSTINC_LOAD:
  case Haydn::G_HAYDN_PREINC_LOAD:
  case Haydn::G_HAYDN_POSTINC_STORE:
  case Haydn::G_HAYDN_PREINC_STORE: {
    const bool IsLoad = Opcode == Haydn::G_HAYDN_POSTINC_LOAD ||
                        Opcode == Haydn::G_HAYDN_PREINC_LOAD;
    const bool IsPost = Opcode == Haydn::G_HAYDN_POSTINC_LOAD ||
                        Opcode == Haydn::G_HAYDN_POSTINC_STORE;

    Register Data, PtrOut, Base, OffsetReg;
    bool IsSExt = false;
    if (IsLoad) {
      Data = I.getOperand(0).getReg();
      PtrOut = I.getOperand(1).getReg();
      Base = I.getOperand(2).getReg();
      OffsetReg = I.getOperand(3).getReg();
      // is_sext imm from combiner (G_SEXTLOAD origin).
      if (I.getNumExplicitOperands() >= 5 && I.getOperand(4).isImm())
        IsSExt = I.getOperand(4).getImm() != 0;
    } else {
      PtrOut = I.getOperand(0).getReg();
      Data = I.getOperand(1).getReg();
      Base = I.getOperand(2).getReg();
      OffsetReg = I.getOperand(3).getReg();
    }

    // Access width from MMO (handles s8/s16 loaded into s32).
    unsigned MemBytes = 0;
    if (!I.memoperands_empty()) {
      auto Sz = (*I.memoperands_begin())->getSize();
      if (Sz.hasValue())
        MemBytes = Sz.getValue();
    }
    if (MemBytes == 0) {
      LLT DataTy = MRI.getType(Data);
      if (!DataTy.isValid() || DataTy.isPointer())
        return false;
      MemBytes = DataTy.getSizeInBits() / 8;
    }

    auto OffC = getIConstantVRegValWithLookThrough(OffsetReg, MRI);
    const bool IsRegStride = !OffC;
    int64_t Bytes = OffC ? OffC->Value.getSExtValue() : 0;
    unsigned ScaleShift =
        MemBytes == 1 ? 0 : MemBytes == 2 ? 1 : MemBytes == 4 ? 2 : 3;
    if (MemBytes != 1 && MemBytes != 2 && MemBytes != 4 && MemBytes != 8)
      return false;

    // Zero EA displacement (G_HAYDN_*INC) + scaled simm6 stride → fused AGU.
    // Same predicate the leftover *_POST_INC expander used (Offset==0 &&
    // stride%width==0 && isInt<6>(stride>>log2(width))).
    const bool CanFuseImm =
        !IsRegStride && isLegalScaledSimm6(Bytes, MemBytes);
    int64_t Scaled = 0;
    if (CanFuseImm)
      Scaled = Bytes >> ScaleShift;

    // ABI i64:32 allows 4-byte-aligned s64, but D_LDW/D_SDW need 8-byte EA
    // alignment (BundleSim MEMORY_FAULT on misaligned). Plain G_LOAD/G_STORE
    // already split to LD32×2 / ST32×2; the post-inc combiner must not re-
    // fuse those into D_LDW_POST_IMM (pr57344-3 packed i72 at align 4).
    Align MemAlign = Align(1);
    if (!I.memoperands_empty())
      MemAlign = (*I.memoperands_begin())->getAlign();
    const bool NeedS64AlignSplit = MemBytes == 8 && MemAlign < Align(8);
    // S_LW / S_SW also fault on misaligned EA — refuse 4-byte fusion below
    // natural word alignment (byte/half already use 1/2-byte forms).
    const bool NeedS32AlignSplit = MemBytes == 4 && MemAlign < Align(4);

    MachineIRBuilder MIB(I);
    if (Base.isVirtual())
      RBI.constrainGenericRegister(Base, Haydn::GPR32RegClass, MRI);
    if (PtrOut.isVirtual())
      RBI.constrainGenericRegister(PtrOut, Haydn::GPR32RegClass, MRI);
    if (OffsetReg.isVirtual() && IsRegStride)
      RBI.constrainGenericRegister(OffsetReg, Haydn::GPR32RegClass, MRI);
    if (Data.isVirtual()) {
      if (MemBytes == 8)
        RBI.constrainGenericRegister(Data, Haydn::DR64RegClass, MRI);
      else
        RBI.constrainGenericRegister(Data, Haydn::GPR32RegClass, MRI);
    }

    // Under-aligned s64 post/pre-inc: expand to split mem + separate AGU update.
    // Access EA is Base (post) or Base+stride (pre); PtrOut always gets the
    // updated pointer (Base+stride).
    if (NeedS64AlignSplit) {
      // Ptr for the memory access.
      Register AccessBase = Base;
      if (!IsPost) {
        // Pre-inc: update pointer first, then access through PtrOut.
        if (IsRegStride) {
          MachineInstr *Add =
              MIB.buildInstr(Haydn::ADD32).addDef(PtrOut).addReg(Base).addReg(
                  OffsetReg);
          if (!constrainSelectedInstRegOperands(*Add, TII, TRI, RBI))
            return false;
        } else {
          MachineInstr *Add = MIB.buildInstr(Haydn::ADDI32_W)
                                  .addDef(PtrOut)
                                  .addReg(Base)
                                  .addImm(Bytes);
          if (!constrainSelectedInstRegOperands(*Add, TII, TRI, RBI))
            return false;
        }
        AccessBase = PtrOut;
      }

      if (IsLoad) {
        Register Lo = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        Register Hi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        Register AddrHi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *AddHi = MIB.buildInstr(Haydn::ADDI32_W)
                                  .addDef(AddrHi)
                                  .addReg(AccessBase)
                                  .addImm(4);
        if (!constrainSelectedInstRegOperands(*AddHi, TII, TRI, RBI))
          return false;
        MachineInstr *LLo =
            MIB.buildInstr(Haydn::LD32).addDef(Lo).addReg(AccessBase).addImm(0);
        addSplitHalfMMO(LLo, MF, I, 0);
        if (!constrainSelectedInstRegOperands(*LLo, TII, TRI, RBI))
          return false;
        MachineInstr *LHi =
            MIB.buildInstr(Haydn::LD32).addDef(Hi).addReg(AddrHi).addImm(0);
        addSplitHalfMMO(LHi, MF, I, 4);
        if (!constrainSelectedInstRegOperands(*LHi, TII, TRI, RBI))
          return false;
        MachineInstr *Pack = MIB.buildInstr(Haydn::MOV_GPR_TO_DR64)
                                 .addDef(Data)
                                 .addReg(Lo)
                                 .addReg(Hi);
        if (!constrainSelectedInstRegOperands(*Pack, TII, TRI, RBI))
          return false;
      } else {
        Register Lo = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        Register Hi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        Register AddrHi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *LoMI =
            MIB.buildInstr(Haydn::MOVE32_DR_L).addDef(Lo).addReg(Data);
        if (!constrainSelectedInstRegOperands(*LoMI, TII, TRI, RBI))
          return false;
        MachineInstr *HiMI =
            MIB.buildInstr(Haydn::MOVE32_DR_H).addDef(Hi).addReg(Data);
        if (!constrainSelectedInstRegOperands(*HiMI, TII, TRI, RBI))
          return false;
        MachineInstr *AddHi = MIB.buildInstr(Haydn::ADDI32_W)
                                  .addDef(AddrHi)
                                  .addReg(AccessBase)
                                  .addImm(4);
        if (!constrainSelectedInstRegOperands(*AddHi, TII, TRI, RBI))
          return false;
        MachineInstr *SLo =
            MIB.buildInstr(Haydn::ST32).addReg(Lo).addReg(AccessBase).addImm(0);
        addSplitHalfMMO(SLo, MF, I, 0);
        if (!constrainSelectedInstRegOperands(*SLo, TII, TRI, RBI))
          return false;
        MachineInstr *SHi =
            MIB.buildInstr(Haydn::ST32).addReg(Hi).addReg(AddrHi).addImm(0);
        addSplitHalfMMO(SHi, MF, I, 4);
        if (!constrainSelectedInstRegOperands(*SHi, TII, TRI, RBI))
          return false;
      }

      if (IsPost) {
        if (IsRegStride) {
          MachineInstr *Add =
              MIB.buildInstr(Haydn::ADD32).addDef(PtrOut).addReg(Base).addReg(
                  OffsetReg);
          if (!constrainSelectedInstRegOperands(*Add, TII, TRI, RBI))
            return false;
        } else {
          MachineInstr *Add = MIB.buildInstr(Haydn::ADDI32_W)
                                  .addDef(PtrOut)
                                  .addReg(Base)
                                  .addImm(Bytes);
          if (!constrainSelectedInstRegOperands(*Add, TII, TRI, RBI))
            return false;
        }
      }
      I.eraseFromParent();
      return true;
    }

    // Under-aligned s32: do not fuse S_LW/S_SW. Word forms still require
    // natural align 4; if MemAlign < 4 the plain G_LOAD path byte-splits
    // via legalizer. Refuse so the residual is not mis-selected as S_LW.
    if (NeedS32AlignSplit)
      return false;

    // Imm stride that is not scaled simm6: emit logical LD/ST + ADDI32_W
    // (EA offset 0). Hexagon SelectIndexedStore: !IsValidInc → *_io plus
    // a separate increment (HexagonISelDAGToDAG.cpp:484,542).
    if (!IsRegStride && !CanFuseImm) {
      auto emitAgu = [&](Register DstPtr, Register SrcBase) -> bool {
        MachineInstr *Add = MIB.buildInstr(Haydn::ADDI32_W)
                                .addDef(DstPtr)
                                .addReg(SrcBase)
                                .addImm(Bytes);
        return constrainSelectedInstRegOperands(*Add, TII, TRI, RBI);
      };

      Register AccessBase = Base;
      if (!IsPost) {
        if (!emitAgu(PtrOut, Base))
          return false;
        AccessBase = PtrOut;
      }

      unsigned PlainOpc = 0;
      switch (MemBytes) {
      case 1:
        PlainOpc = IsLoad ? (IsSExt ? Haydn::LD8 : Haydn::LDU8) : Haydn::ST8;
        break;
      case 2:
        PlainOpc = IsLoad ? (IsSExt ? Haydn::LD16 : Haydn::LDU16) : Haydn::ST16;
        break;
      case 4:
        PlainOpc = IsLoad ? Haydn::LD32 : Haydn::ST32;
        break;
      case 8:
        PlainOpc = IsLoad ? Haydn::LD64 : Haydn::ST64;
        break;
      }
      if (!PlainOpc)
        return false;

      MachineInstr *MemMI;
      if (IsLoad)
        MemMI = MIB.buildInstr(PlainOpc)
                    .addDef(Data)
                    .addReg(AccessBase)
                    .addImm(0);
      else
        MemMI = MIB.buildInstr(PlainOpc)
                    .addReg(Data)
                    .addReg(AccessBase)
                    .addImm(0);
      cloneMemOperands(*MemMI, I);
      if (!constrainSelectedInstRegOperands(*MemMI, TII, TRI, RBI))
        return false;

      if (IsPost && !emitAgu(PtrOut, Base))
        return false;
      I.eraseFromParent();
      return true;
    }

    // Pick fused opcode. Byte/half loads: is_sext → S_LBS/S_LHWS else S_LBU/S_LHWU.
    unsigned FusedOpc = 0;
    auto pick = [&](unsigned PostImm, unsigned PreImm, unsigned PostReg,
                    unsigned PreReg) {
      if (IsRegStride)
        FusedOpc = IsPost ? PostReg : PreReg;
      else
        FusedOpc = IsPost ? PostImm : PreImm;
    };
    switch (MemBytes) {
    case 1:
      if (IsLoad) {
        if (IsSExt)
          pick(Haydn::S_LBS_POST_IMM, Haydn::S_LBS_PRE_IMM,
               Haydn::S_LBS_POST_REG, Haydn::S_LBS_PRE_REG);
        else
          pick(Haydn::S_LBU_POST_IMM, Haydn::S_LBU_PRE_IMM,
               Haydn::S_LBU_POST_REG, Haydn::S_LBU_PRE_REG);
      } else
        pick(Haydn::S_SB_POST_IMM, Haydn::S_SB_PRE_IMM, Haydn::S_SB_POST_REG,
             Haydn::S_SB_PRE_REG);
      break;
    case 2:
      if (IsLoad) {
        if (IsSExt)
          pick(Haydn::S_LHWS_POST_IMM, Haydn::S_LHWS_PRE_IMM,
               Haydn::S_LHWS_POST_REG, Haydn::S_LHWS_PRE_REG);
        else
          pick(Haydn::S_LHWU_POST_IMM, Haydn::S_LHWU_PRE_IMM,
               Haydn::S_LHWU_POST_REG, Haydn::S_LHWU_PRE_REG);
      } else
        pick(Haydn::S_SHW_POST_IMM, Haydn::S_SHW_PRE_IMM, Haydn::S_SHW_POST_REG,
             Haydn::S_SHW_PRE_REG);
      break;
    case 4:
      if (IsLoad)
        pick(Haydn::S_LW_POST_IMM, Haydn::S_LW_PRE_IMM, Haydn::S_LW_POST_REG,
             Haydn::S_LW_PRE_REG);
      else
        pick(Haydn::ST32_POST, Haydn::S_SW_PRE_IMM, Haydn::S_SW_POST_REG,
             Haydn::S_SW_PRE_REG);
      break;
    case 8:
      if (IsLoad)
        pick(Haydn::D_LDW_POST_IMM, Haydn::D_LDW_PRE_IMM, Haydn::D_LDW_POST_REG,
             Haydn::D_LDW_PRE_REG);
      else
        pick(Haydn::ST64_POST, Haydn::D_SDW_PRE_IMM, Haydn::D_SDW_POST_REG,
             Haydn::D_SDW_PRE_REG);
      break;
    }
    if (!FusedOpc)
      return false;

    MachineInstrBuilder Fused = MIB.buildInstr(FusedOpc);
    if (IsLoad) {
      Fused.addDef(Data).addDef(PtrOut).addReg(Base);
    } else {
      Fused.addDef(PtrOut).addReg(Data).addReg(Base);
    }
    if (IsRegStride)
      Fused.addReg(OffsetReg);
    else
      Fused.addImm(Scaled);
    for (auto *MMO : I.memoperands())
      Fused.addMemOperand(MMO);
    if (!constrainSelectedInstRegOperands(*Fused, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  // G_SMAX/SMIN/UMAX/UMIN: selectImpl Pats only. No C++ residual.

  case TargetOpcode::G_SHL: {
    // s32/s64 scalar: selectImpl Pats (SLL32/SLL64). Residual: SIMD only
    // (lane-0 extract is multi-instr).
    Register Dst = I.getOperand(0).getReg();
    Register Src = I.getOperand(1).getReg();
    Register Amt = I.getOperand(2).getReg();
    LLT DstTy = MRI.getType(Dst);
    MachineIRBuilder MIB(I);

    // SIMD shift: v2i32 -> X2SLL32, v4i16 -> X4SLL16
    // Per spec: dst=DR64, src=DR64, shift_amount=GPR32 (lane 0 of amt vector).
    if (!DstTy.isVector())
      return false;
    unsigned EltBits = DstTy.getElementType().getSizeInBits();
    unsigned Opc = 0;
    if (EltBits == 32)
      Opc = Haydn::X2SLL32;
    else if (EltBits == 16)
      Opc = Haydn::X4SLL16;
    else
      return false;
    if (Dst.isVirtual())
      RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, Haydn::DR64RegClass, MRI);
    Register AmtScalar = extractVecLane0AsGPR32(MIB, Amt, MRI);
    if (!AmtScalar.isValid())
      return false;
    MachineInstr *MI =
        MIB.buildInstr(Opc).addDef(Dst).addReg(Src).addReg(AmtScalar);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_LSHR: {
    // s32/s64 scalar: selectImpl Pats (SRL32/SRL64). Residual: SIMD only.
    Register Dst = I.getOperand(0).getReg();
    Register Src = I.getOperand(1).getReg();
    Register Amt = I.getOperand(2).getReg();
    LLT DstTy = MRI.getType(Dst);
    MachineIRBuilder MIB(I);

    // SIMD logical right shift: v2i32 -> X2SRL32, v4i16 -> X4SRL16
    if (!DstTy.isVector())
      return false;
    unsigned EltBits = DstTy.getElementType().getSizeInBits();
    unsigned Opc = 0;
    if (EltBits == 32)
      Opc = Haydn::X2SRL32;
    else if (EltBits == 16)
      Opc = Haydn::X4SRL16;
    else
      return false;
    if (Dst.isVirtual())
      RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, Haydn::DR64RegClass, MRI);
    Register AmtScalar = extractVecLane0AsGPR32(MIB, Amt, MRI);
    if (!AmtScalar.isValid())
      return false;
    MachineInstr *MI =
        MIB.buildInstr(Opc).addDef(Dst).addReg(Src).addReg(AmtScalar);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_ASHR: {
    // s32/s64 scalar: selectImpl Pats (SRA32/SRA64). Residual: SIMD only.
    Register Dst = I.getOperand(0).getReg();
    Register Src = I.getOperand(1).getReg();
    Register Amt = I.getOperand(2).getReg();
    LLT DstTy = MRI.getType(Dst);
    MachineIRBuilder MIB(I);

    // SIMD arithmetic right shift: v2i32 -> X2SRA32, v4i16 -> X4SRA16
    if (!DstTy.isVector())
      return false;
    unsigned EltBits = DstTy.getElementType().getSizeInBits();
    unsigned Opc = 0;
    if (EltBits == 32)
      Opc = Haydn::X2SRA32;
    else if (EltBits == 16)
      Opc = Haydn::X4SRA16;
    else
      return false;
    if (Dst.isVirtual())
      RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, Haydn::DR64RegClass, MRI);
    Register AmtScalar = extractVecLane0AsGPR32(MIB, Amt, MRI);
    if (!AmtScalar.isValid())
      return false;
    MachineInstr *MI =
        MIB.buildInstr(Opc).addDef(Dst).addReg(Src).addReg(AmtScalar);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_LOAD:
  case TargetOpcode::G_ZEXTLOAD:
  case TargetOpcode::G_SEXTLOAD: {
    // G_LOAD / extload: opcode from MMO width (not SSA result width).
    // Extloads come from legalizer splits of unaligned/i24 mem.
    // emit logical LD64 (PlacementAlternatives S0|S1); post-RA HR tryAdd +
    // setDesc materialize pick LD64_S*. Never key LD32 on s32←s8/s16.
    Register Dst = I.getOperand(0).getReg();
    Register Ptr = I.getOperand(1).getReg();
    LLT DstTy = MRI.getType(Dst);
    MachineIRBuilder MIB(I);
    const bool IsSExt = I.getOpcode() == TargetOpcode::G_SEXTLOAD;
    unsigned MemBytes =
        getMemAccessBytes(I, /*FallbackBytes=*/DstTy.getSizeInBits() / 8);
    unsigned Opc;
    switch (MemBytes) {
    case 1:  Opc = IsSExt ? Haydn::LD8 : Haydn::LDU8; break;
    case 2:  Opc = IsSExt ? Haydn::LD16 : Haydn::LDU16; break;
    case 4:  Opc = Haydn::LD32; break;
    case 8:  Opc = Haydn::LD64; break;
    default: return false;
    }
    const TargetRegisterClass *DstRC = memResultRC(MemBytes);

    // Mirror G_STORE: ABI i64:32 allows 4-byte-aligned s64, but LD64/D_LDW
    // requires 8-byte alignment. Split to LD32 lo/hi + MOV_GPR_TO_DR64.
    if (Opc == Haydn::LD64) {
      Align MemAlign = Align(1);
      if (!I.memoperands_empty())
        MemAlign = (*I.memoperands_begin())->getAlign();
      if (MemAlign < Align(8)) {
        if (Dst.isVirtual())
          RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
        if (Ptr.isVirtual())
          RBI.constrainGenericRegister(Ptr, Haydn::GPR32RegClass, MRI);
        Register Lo = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        Register Hi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        Register AddrHi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *AddMI = MIB.buildInstr(Haydn::ADDI32_W)
                                  .addDef(AddrHi)
                                  .addReg(Ptr)
                                  .addImm(4);
        if (!constrainSelectedInstRegOperands(*AddMI, TII, TRI, RBI))
          return false;
        MachineInstr *LLo =
            MIB.buildInstr(Haydn::LD32).addDef(Lo).addReg(Ptr).addImm(0);
        addSplitHalfMMO(LLo, MF, I, 0);
        if (!constrainSelectedInstRegOperands(*LLo, TII, TRI, RBI))
          return false;
        MachineInstr *LHi =
            MIB.buildInstr(Haydn::LD32).addDef(Hi).addReg(AddrHi).addImm(0);
        addSplitHalfMMO(LHi, MF, I, 4);
        if (!constrainSelectedInstRegOperands(*LHi, TII, TRI, RBI))
          return false;
        MachineInstr *Pack = MIB.buildInstr(Haydn::MOV_GPR_TO_DR64)
                                 .addDef(Dst)
                                 .addReg(Lo)
                                 .addReg(Hi);
        if (!constrainSelectedInstRegOperands(*Pack, TII, TRI, RBI))
          return false;
        I.eraseFromParent();
        return true;
      }
    }

    // Fold G_PTR_ADD const only when scaled index fits golden simm6.
    // LD32/LD64 may use WITH_REG for non-foldable offsets; byte/half leave
    // BaseReg = Ptr so G_PTR_ADD materializes as ADDI32.
    Register BaseReg = Ptr;
    int64_t Offset = 0;
    Register RuntimeOffsetReg = 0;

    if (Ptr.isVirtual()) {
      MachineInstr *PtrDef = MRI.getVRegDef(Ptr);
      if (PtrDef && PtrDef->getOpcode() == TargetOpcode::G_PTR_ADD) {
        Register OffsetReg = PtrDef->getOperand(2).getReg();
        auto OffsetCst = getIConstantVRegValWithLookThrough(OffsetReg, MRI);
        if (OffsetCst &&
            isLegalScaledSimm6(OffsetCst->Value.getSExtValue(), MemBytes)) {
          BaseReg = PtrDef->getOperand(1).getReg();
          Offset = OffsetCst->Value.getSExtValue();
        } else if (Opc == Haydn::LD32 || Opc == Haydn::LD64) {
          BaseReg = PtrDef->getOperand(1).getReg();
          RuntimeOffsetReg = OffsetReg;
        }
      }
    }

    // §6.5 register-offset (WITH_REG) path: runtime GPR offset, or any
    // constant that failed the scaled-simm6 fold above (LD32/LD64 only).
    // Materialize into a GPR and select logical REG-offset forms (:
    // never emit private *_S0 encode peers from ISel).
    if (RuntimeOffsetReg) {
      unsigned RegOpc = 0;
      if (Opc == Haydn::LD32)
        RegOpc = Haydn::LD32_REG_M0S0LS;
      else if (Opc == Haydn::LD64)
        RegOpc = Haydn::LD64_REG_M0S0LS;

      if (RegOpc) {
        Register OffReg = RuntimeOffsetReg;
        if (Dst.isVirtual())
          RBI.constrainGenericRegister(Dst, *DstRC, MRI);
        if (BaseReg.isVirtual())
          RBI.constrainGenericRegister(BaseReg, Haydn::GPR32RegClass, MRI);
        if (OffReg.isVirtual())
          RBI.constrainGenericRegister(OffReg, Haydn::GPR32RegClass, MRI);
        MachineInstr *MI = MIB.buildInstr(RegOpc)
                               .addDef(Dst)
                               .addReg(BaseReg)
                               .addReg(OffReg);
        MI->cloneMemRefs(MF, I);
        if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
          return false;
        I.eraseFromParent();
        return true;
      }
    }

    if (Dst.isVirtual())
      RBI.constrainGenericRegister(Dst, *DstRC, MRI);
    if (BaseReg.isVirtual())
      RBI.constrainGenericRegister(BaseReg, Haydn::GPR32RegClass, MRI);
    // Golden scaled imm: field = byte_offset / access_width
    // (EA = base + (imm << log2(width))). Offset is still bytes here.
    int64_t ImmField = Offset;
    if (MemBytes > 1) {
      assert(Offset % static_cast<int64_t>(MemBytes) == 0 &&
             "scaled LS fold requires width-aligned byte offset");
      ImmField = Offset / static_cast<int64_t>(MemBytes);
    }
    MachineInstr *MI =
        MIB.buildInstr(Opc).addDef(Dst).addReg(BaseReg).addImm(ImmField);
    MI->cloneMemRefs(MF, I);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_STORE: {
    // G_STORE: opcode from MMO width (trunc stores after i24 split → ST8/ST16).
    Register Val = I.getOperand(0).getReg();
    Register Ptr = I.getOperand(1).getReg();
    LLT ValTy = MRI.getType(Val);
    MachineIRBuilder MIB(I);
    unsigned MemBytes =
        getMemAccessBytes(I, /*FallbackBytes=*/ValTy.getSizeInBits() / 8);
    unsigned Opc;
    switch (MemBytes) {
    case 1:  Opc = Haydn::ST8; break;
    case 2:  Opc = Haydn::ST16; break;
    case 4:  Opc = Haydn::ST32; break;
    case 8:  Opc = Haydn::ST64; break;
    default: return false;
    }
    const TargetRegisterClass *ValRC = memResultRC(MemBytes);

    // ABI DataLayout is i64:32 (4-byte align for long long / double) but ISA
    // D_SDW (ST64) requires 8-byte alignment. BundleSim faults ALIGNMENT on
    // ST64 to e.g. struct {int x; long long v;}.v (offset 4). Split into two
    // 4-byte ST32 of the DR lanes (MOVE32_DR_L/H + ST32).
    if (Opc == Haydn::ST64) {
      Align MemAlign = Align(1);
      if (!I.memoperands_empty())
        MemAlign = (*I.memoperands_begin())->getAlign();
      if (MemAlign < Align(8)) {
        if (Val.isVirtual())
          RBI.constrainGenericRegister(Val, Haydn::DR64RegClass, MRI);
        if (Ptr.isVirtual())
          RBI.constrainGenericRegister(Ptr, Haydn::GPR32RegClass, MRI);
        Register Lo = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        Register Hi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        Register AddrHi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *LoMI =
            MIB.buildInstr(Haydn::MOVE32_DR_L).addDef(Lo).addReg(Val);
        if (!constrainSelectedInstRegOperands(*LoMI, TII, TRI, RBI))
          return false;
        MachineInstr *HiMI =
            MIB.buildInstr(Haydn::MOVE32_DR_H).addDef(Hi).addReg(Val);
        if (!constrainSelectedInstRegOperands(*HiMI, TII, TRI, RBI))
          return false;
        MachineInstr *AddMI = MIB.buildInstr(Haydn::ADDI32_W)
                                  .addDef(AddrHi)
                                  .addReg(Ptr)
                                  .addImm(4);
        if (!constrainSelectedInstRegOperands(*AddMI, TII, TRI, RBI))
          return false;
        MachineInstr *SLo =
            MIB.buildInstr(Haydn::ST32).addReg(Lo).addReg(Ptr).addImm(0);
        addSplitHalfMMO(SLo, MF, I, 0);
        if (!constrainSelectedInstRegOperands(*SLo, TII, TRI, RBI))
          return false;
        MachineInstr *SHi =
            MIB.buildInstr(Haydn::ST32).addReg(Hi).addReg(AddrHi).addImm(0);
        addSplitHalfMMO(SHi, MF, I, 4);
        if (!constrainSelectedInstRegOperands(*SHi, TII, TRI, RBI))
          return false;
        I.eraseFromParent();
        return true;
      }
    }

    // Fold G_PTR_ADD const only when scaled index fits golden simm6.
    Register BaseReg = Ptr;
    int64_t Offset = 0;
    Register RuntimeOffsetReg = 0;

    if (Ptr.isVirtual()) {
      MachineInstr *PtrDef = MRI.getVRegDef(Ptr);
      if (PtrDef && PtrDef->getOpcode() == TargetOpcode::G_PTR_ADD) {
        Register OffsetReg = PtrDef->getOperand(2).getReg();
        auto OffsetCst = getIConstantVRegValWithLookThrough(OffsetReg, MRI);
        if (OffsetCst &&
            isLegalScaledSimm6(OffsetCst->Value.getSExtValue(), MemBytes)) {
          BaseReg = PtrDef->getOperand(1).getReg();
          Offset = OffsetCst->Value.getSExtValue();
        } else if (Opc == Haydn::ST32 || Opc == Haydn::ST64) {
          // Non-constant or non-foldable offset: capture for the WITH_REG path
          // (ST32/ST64 only — byte/half stores leave BaseReg = Ptr).
          BaseReg = PtrDef->getOperand(1).getReg();
          RuntimeOffsetReg = OffsetReg;
        }
        // else: byte/half store with non-foldable offset — leave BaseReg = Ptr.
      }
    }

    // §6.5 register-offset (WITH_REG) path — see G_LOAD for full rationale.
    if (RuntimeOffsetReg) {
      unsigned RegOpc = 0;
      if (Opc == Haydn::ST32)
        RegOpc = Haydn::ST32_REG_M0S0LS;
      else if (Opc == Haydn::ST64)
        RegOpc = Haydn::ST64_REG_M0S0LS;

      if (RegOpc) {
        Register OffReg = RuntimeOffsetReg;
        if (Val.isVirtual())
          RBI.constrainGenericRegister(Val, *ValRC, MRI);
        if (BaseReg.isVirtual())
          RBI.constrainGenericRegister(BaseReg, Haydn::GPR32RegClass, MRI);
        if (OffReg.isVirtual())
          RBI.constrainGenericRegister(OffReg, Haydn::GPR32RegClass, MRI);
        MachineInstr *MI = MIB.buildInstr(RegOpc)
                               .addReg(Val)
                               .addReg(BaseReg)
                               .addReg(OffReg);
        MI->cloneMemRefs(MF, I);
        if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
          return false;
        I.eraseFromParent();
        return true;
      }
    }

    if (Val.isVirtual())
      RBI.constrainGenericRegister(Val, *ValRC, MRI);
    if (BaseReg.isVirtual())
      RBI.constrainGenericRegister(BaseReg, Haydn::GPR32RegClass, MRI);
    // Golden scaled imm: field = byte_offset / access_width.
    int64_t ImmField = Offset;
    if (MemBytes > 1) {
      assert(Offset % static_cast<int64_t>(MemBytes) == 0 &&
             "scaled LS fold requires width-aligned byte offset");
      ImmField = Offset / static_cast<int64_t>(MemBytes);
    }
    MachineInstr *MI =
        MIB.buildInstr(Opc).addReg(Val).addReg(BaseReg).addImm(ImmField);
    MI->cloneMemRefs(MF, I);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_ZEXT: {
    // Full pipeline: legalizer → G_AND/G_SHL + RI Pats. Residual is for
    // select-only MIR and cross-bank / narrow leftovers only.
    Register Dst = I.getOperand(0).getReg();
    Register Src = I.getOperand(1).getReg();
    LLT DstTy = MRI.getType(Dst);
    LLT SrcTy = MRI.getType(Src);
    unsigned DstBits = DstTy.getSizeInBits();
    unsigned SrcBits = SrcTy.getSizeInBits();

    MachineIRBuilder MIB(I);

    if (DstBits == 64 && SrcBits == 32) {
      if (Dst.isVirtual())
        RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
      if (Src.isVirtual())
        RBI.constrainGenericRegister(Src, Haydn::GPR32RegClass, MRI);
      MachineInstr *MI = MIB.buildInstr(Haydn::MOV_GPR_TO_DR64)
                             .addDef(Dst)
                             .addReg(Src)
                             .addReg(Haydn::R0);
      if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
        return false;
    } else if (DstBits == 8 && SrcBits == 1) {
      if (!emitALUImm(MIB, Haydn::ANDI32, Dst, Src, 1, TII, TRI, RBI, MRI))
        return false;
    } else if (DstBits == 32 && SrcBits >= 1 && SrcBits < 32) {
      // MIR select-only (legalizer skipped). Full pipeline never hits this.
      if (Src.isVirtual())
        RBI.constrainGenericRegister(Src, Haydn::GPR32RegClass, MRI);
      RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
      if (SrcBits <= 20) {
        const unsigned MaskVal = (1u << SrcBits) - 1;
        if (!emitALUImm(MIB, Haydn::ANDI32, Dst, Src, MaskVal, TII, TRI, RBI, MRI))
          return false;
      } else {
        const unsigned ShAmt = 32 - SrcBits;
        Register Tmp = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        if (!emitALUImm(MIB, Haydn::SLLI32, Tmp, Src, ShAmt, TII, TRI, RBI, MRI))
          return false;
        if (!emitALUImm(MIB, Haydn::SRLI32, Dst, Tmp, ShAmt, TII, TRI, RBI, MRI))
          return false;
      }
    } else if (DstBits == 64 && SrcBits >= 1 && SrcBits < 32) {
      // MIR / residual non-pow2 sN→s64 (product legalizer leaves s64 legal).
      Register Ext32 = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      if (Src.isVirtual())
        RBI.constrainGenericRegister(Src, Haydn::GPR32RegClass, MRI);
      if (SrcBits <= 20) {
        const unsigned MaskVal = (1u << SrcBits) - 1;
        if (!emitALUImm(MIB, Haydn::ANDI32, Ext32, Src, MaskVal, TII, TRI, RBI, MRI))
          return false;
      } else {
        const unsigned ShAmt = 32 - SrcBits;
        Register Tmp = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        if (!emitALUImm(MIB, Haydn::SLLI32, Tmp, Src, ShAmt, TII, TRI, RBI, MRI))
          return false;
        if (!emitALUImm(MIB, Haydn::SRLI32, Ext32, Tmp, ShAmt, TII, TRI, RBI, MRI))
          return false;
      }
      if (Dst.isVirtual())
        RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
      MachineInstr *MovMI = MIB.buildInstr(Haydn::MOV_GPR_TO_DR64)
                                .addDef(Dst)
                                .addReg(Ext32)
                                .addReg(Haydn::R0);
      if (!constrainSelectedInstRegOperands(*MovMI, TII, TRI, RBI))
        return false;
    } else {
      return false;
    }

    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_ANYEXT: {
    // Any-extend is like zero-extend but the upper bits are don't-care.
    // For Haydn, treat it the same as G_ZEXT.
    Register Dst = I.getOperand(0).getReg();
    Register Src = I.getOperand(1).getReg();
    LLT DstTy = MRI.getType(Dst);
    LLT SrcTy = MRI.getType(Src);
    unsigned DstBits = DstTy.getSizeInBits();
    unsigned SrcBits = SrcTy.getSizeInBits();

    MachineIRBuilder MIB(I);

    if (DstBits == 64 && SrcBits == 32) {
      // i32 → i64: treat as zext (upper half = R0 = 0)
      if (Dst.isVirtual())
        RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
      if (Src.isVirtual())
        RBI.constrainGenericRegister(Src, Haydn::GPR32RegClass, MRI);
      MachineInstr *MI = MIB.buildInstr(Haydn::MOV_GPR_TO_DR64)
                             .addDef(Dst).addReg(Src).addReg(Haydn::R0);
      if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
        return false;
    } else if (DstBits == 32 && SrcBits < 32) {
      // Smaller → i32: anyext upper bits are don't-care.
      // Both live in GPR32 — constrain both registers and replace Dst with Src.
      // Use replaceRegWith to redirect all users of Dst to Src. Since both are
      // in GPR32, this is valid at the machine level even though LLT types differ.
      RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
      if (Src.isVirtual())
        RBI.constrainGenericRegister(Src, Haydn::GPR32RegClass, MRI);
      if (Dst != Src)
        MRI.replaceRegWith(Dst, Src);
      I.eraseFromParent();
      return true;
    } else if (DstBits == 64 && SrcBits < 32) {
      // Smaller → i64: constrain src to GPR32, then zext to DR64.
      // Do NOT buildCopy between mismatched LLT sizes.
      if (Src.isVirtual())
        RBI.constrainGenericRegister(Src, Haydn::GPR32RegClass, MRI);
      if (Dst.isVirtual())
        RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
      MachineInstr *MI = MIB.buildInstr(Haydn::MOV_GPR_TO_DR64)
                             .addDef(Dst).addReg(Src).addReg(Haydn::R0);
      if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
        return false;
    } else if (DstBits == 64 && SrcBits > 32 && SrcBits < 64) {
      // s40/s37 packed bitfields (pr52979): both live in DR64. ANYEXT is
      // a bank identity — do not emit a COPY across LLTs.
      if (Dst.isVirtual())
        RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
      if (Src.isVirtual())
        RBI.constrainGenericRegister(Src, Haydn::DR64RegClass, MRI);
      if (Dst != Src)
        MRI.replaceRegWith(Dst, Src);
      I.eraseFromParent();
      return true;
    } else {
      return false;
    }
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_SEXT: {
    // Full pipeline: legalizer → G_SHL+G_ASHR + RI Pats. Residual: MIR +
    // cross-bank only.
    Register Dst = I.getOperand(0).getReg();
    Register Src = I.getOperand(1).getReg();
    LLT DstTy = MRI.getType(Dst);
    LLT SrcTy = MRI.getType(Src);
    unsigned DstBits = DstTy.getSizeInBits();
    unsigned SrcBits = SrcTy.getSizeInBits();

    MachineIRBuilder MIB(I);

    if (DstBits == 64 && SrcBits == 32) {
      if (Src.isVirtual())
        RBI.constrainGenericRegister(Src, Haydn::GPR32RegClass, MRI);
      if (Dst.isVirtual())
        RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
      MachineInstr *SextMI = MIB.buildInstr(Haydn::SEXT_GPR32_TO_DR64)
                                .addDef(Dst).addReg(Src);
      if (!constrainSelectedInstRegOperands(*SextMI, TII, TRI, RBI))
        return false;
    } else if (DstBits == 32 && SrcBits >= 1 && SrcBits < 32) {
      // MIR select-only (legalizer skipped).
      unsigned ShiftAmt = 32 - SrcBits;
      Register Tmp = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
      if (!emitALUImm(MIB, Haydn::SLLI32, Tmp, Src, ShiftAmt, TII, TRI, RBI, MRI))
        return false;
      if (!emitALUImm(MIB, Haydn::SRAI32, Dst, Tmp, ShiftAmt, TII, TRI, RBI, MRI))
        return false;
    } else if (DstBits == 64 && SrcBits >= 1 && SrcBits < 32) {
      Register Ext32 = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      Register Tmp = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      unsigned ShiftAmt = 32 - SrcBits;
      if (!emitALUImm(MIB, Haydn::SLLI32, Tmp, Src, ShiftAmt, TII, TRI, RBI, MRI))
        return false;
      if (!emitALUImm(MIB, Haydn::SRAI32, Ext32, Tmp, ShiftAmt, TII, TRI, RBI, MRI))
        return false;
      if (Dst.isVirtual())
        RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
      MachineInstr *SextMI = MIB.buildInstr(Haydn::SEXT_GPR32_TO_DR64)
                                .addDef(Dst).addReg(Ext32);
      if (!constrainSelectedInstRegOperands(*SextMI, TII, TRI, RBI))
        return false;
    } else {
      return false;
    }

    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_TRUNC: {
    Register Dst = I.getOperand(0).getReg();
    Register Src = I.getOperand(1).getReg();
    LLT DstTy = MRI.getType(Dst);
    LLT SrcTy = MRI.getType(Src);

    MachineIRBuilder MIB(I);

    if (SrcTy.getSizeInBits() == 64 && DstTy.getSizeInBits() == 32) {
      // s64 to s32 truncation: extract the low 32 bits.
      // use native MOVE32_DR_L (1 op, DR→GPR lane extract) instead of
      // MOV_DR64_TO_GPR (5-op stack spill for both lanes). This eliminates
      // the dominant cross-bank round-trip in FFT/vector kernels (24 sites
      // in fft_real16x16 alone, each saving 4 ops).
      if (Src.isVirtual())
        RBI.constrainGenericRegister(Src, Haydn::DR64RegClass, MRI);
      RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
      MachineInstr *MI = MIB.buildInstr(Haydn::MOVE32_DR_L)
                             .addDef(Dst)
                             .addReg(Src);
      if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
        return false;
    } else if (DstTy.getSizeInBits() == 1) {
      // Any scalar → s1: isolate BIT 0 into a clean 0/1 GPR32.
      // Closed rule for all legal G_TRUNC-to-s1 sources (s8/s16/s32/s64):
      // G_BRCOND tests the s32 value non-zero (BNEZ)
      // MOVT32 tests rs2[0]
      // so the result must be genuine 0/1, not "any non-zero low bits".
      // Bare replaceRegWith(Dst, Src) mis-branches on `trunc iN 2 to i1`
      // and (for s64) leaks the DR64 bank into GPR32 slots.
      // s64 needs MOVE32_DR_L first; sub-32 sources are already GPR32.
      Register BitSrc;
      if (SrcTy.getSizeInBits() == 64) {
        BitSrc = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        if (Src.isVirtual())
          RBI.constrainGenericRegister(Src, Haydn::DR64RegClass, MRI);
        MachineInstr *Ext =
            MIB.buildInstr(Haydn::MOVE32_DR_L).addDef(BitSrc).addReg(Src);
        if (!constrainSelectedInstRegOperands(*Ext, TII, TRI, RBI))
          return false;
      } else {
        if (Src.isVirtual())
          RBI.constrainGenericRegister(Src, Haydn::GPR32RegClass, MRI);
        BitSrc = Src;
      }
      Register Bit0 = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      if (!emitALUImm(MIB, Haydn::ANDI32, Bit0, BitSrc, 1, TII, TRI, RBI, MRI))
        return false;
      RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
      MRI.replaceRegWith(Dst, Bit0);
    } else if (SrcTy.getSizeInBits() == 64 && DstTy.getSizeInBits() > 32 &&
               DstTy.getSizeInBits() < 64) {
      // s64 → s40 (pr52979 packed i31+i6): both live in DR64. Trunc is a
      // bank identity.
      if (Dst.isVirtual())
        RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
      if (Src.isVirtual())
        RBI.constrainGenericRegister(Src, Haydn::DR64RegClass, MRI);
      if (Dst != Src)
        MRI.replaceRegWith(Dst, Src);
      I.eraseFromParent();
      return true;
    } else if (SrcTy.getSizeInBits() == 64) {
      // s64 -> any sub-32 (s8/s16/s24/…): the narrow result must live in
      // GPR32, but Src is DR64. replaceRegWith(Dst, Src) would alias the
      // narrow Dst to the DR64 Src, leaking the DR64 bank into GPR32 operand
      // slots of ST32/MOVT32 ("Illegal virtual register" verifier abort).
      // Extract the low 32 bits into a fresh GPR32 first (the s64 low lane
      // holds the truncated value), then alias Dst to it. (s64 -> s1 handled
      // above; s64 -> s32 is MOVE32_DR_L into Dst directly.)
      assert(DstTy.getSizeInBits() < 32 &&
             "unexpected s64 truncation width (s1/s32 handled above)");
      Register Lo32 = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      if (Src.isVirtual())
        RBI.constrainGenericRegister(Src, Haydn::DR64RegClass, MRI);
      MachineInstr *Ext = MIB.buildInstr(Haydn::MOVE32_DR_L)
                              .addDef(Lo32)
                              .addReg(Src);
      if (!constrainSelectedInstRegOperands(*Ext, TII, TRI, RBI))
        return false;
      RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
      MRI.replaceRegWith(Dst, Lo32);
    } else {
      // Sub-register truncation s32/s16/s8/sN -> sM (M < Src, M != 1): no-op
      // at register level (both operands are GPR32; the narrow value occupies
      // the low bits). s1 destinations are handled above (must AND with 1).
      // Covers non-pow2 bitfield widths (s24, s12, …) — residual.
      if (Src.isVirtual())
        RBI.constrainGenericRegister(Src, Haydn::GPR32RegClass, MRI);
      RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
      if (Dst != Src)
        MRI.replaceRegWith(Dst, Src);
    }

    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_PTR_ADD: {
    // Pointer arithmetic: use ADDI32 for constant offsets, ADD32 for register.
    Register Dst = I.getOperand(0).getReg();
    Register Base = I.getOperand(1).getReg();
    Register Offset = I.getOperand(2).getReg();
    MachineIRBuilder MIB(I);

    // If the offset is a constant that fits in simm16, use ADDI32 (packable
    // Slot012 form) rather than ADDI32_W (slot-0-only WIDE). Pointer bumps of
    // +8/+16 in DSP loops must share cycles with other ALU ops for SMS to
    // place the address PHI early enough for LD→MAC latency; ADDI32_W's S0
    // exclusivity routinely collides with ADD32 first-fit on S0 and forces
    // the bump onto a later cycle → II-independent es>ls on the load.
    auto OffsetCst = getIConstantVRegValWithLookThrough(Offset, MRI);
    if (OffsetCst && isInt<16>(OffsetCst->Value.getSExtValue())) {
      if (Dst.isVirtual())
        RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
      if (Base.isVirtual())
        RBI.constrainGenericRegister(Base, Haydn::GPR32RegClass, MRI);
      MachineInstr *MI = MIB.buildInstr(Haydn::ADDI32)
                             .addDef(Dst)
                             .addReg(Base)
                             .addImm(OffsetCst->Value.getSExtValue());
      if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
        return false;
    } else {
      MachineInstr *MI =
          MIB.buildInstr(Haydn::ADD32).addDef(Dst).addReg(Base).addReg(Offset);
      if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
        return false;
    }
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_INTTOPTR: {
    // Integer to pointer
    Register Dst = I.getOperand(0).getReg();
    Register Src = I.getOperand(1).getReg();
    LLT SrcTy = MRI.getType(Src);
    MachineIRBuilder MIB(I);

    if (SrcTy.isValid() && SrcTy.getSizeInBits() == 64) {
      // s64 -> p0: truncate to s32, then copy
      Register Tmp32 = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      // extract low 32 bits via native MOVE32_DR_L (1 op, no stack).
      if (Src.isVirtual())
        RBI.constrainGenericRegister(Src, Haydn::DR64RegClass, MRI);
      MachineInstr *Extract = MIB.buildInstr(Haydn::MOVE32_DR_L)
                                 .addDef(Tmp32)
                                 .addReg(Src);
      if (!constrainSelectedInstRegOperands(*Extract, TII, TRI, RBI))
        return false;
      // Copy low 32 bits to destination (p0)
      MachineInstr *Copy = MIB.buildCopy(Dst, Tmp32);
      if (!constrainSelectedInstRegOperands(*Copy, TII, TRI, RBI))
        return false;
    } else {
      // s32 -> p0: simple copy (both are 32-bit)
      MachineInstr *Copy = MIB.buildCopy(Dst, Src);
      if (!constrainSelectedInstRegOperands(*Copy, TII, TRI, RBI))
        return false;
    }
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_PTRTOINT: {
    // Pointer to integer
    Register Dst = I.getOperand(0).getReg();
    Register Src = I.getOperand(1).getReg();
    LLT DstTy = MRI.getType(Dst);
    MachineIRBuilder MIB(I);

    if (DstTy.isValid() && DstTy.getSizeInBits() == 64) {
      // p0 -> s64: copy to s32, then zero-extend to s64
      // First, constrain the source (pointer) to GPR32 (pointers are 32-bit)
      if (Src.isVirtual())
        RBI.constrainGenericRegister(Src, Haydn::GPR32RegClass, MRI);
      // Constrain destination to DR64
      if (Dst.isVirtual())
        RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
      // Zero-extend: MOV_GPR_TO_DR64 Dst, Src, R0 (R0 = 0 for upper half)
      MachineInstr *Merge = MIB.buildInstr(Haydn::MOV_GPR_TO_DR64)
                                .addDef(Dst)
                                .addReg(Src)
                                .addReg(Haydn::R0); // R0 = 0 for upper half
      if (!constrainSelectedInstRegOperands(*Merge, TII, TRI, RBI))
        return false;
    } else {
      // p0 -> s32: simple copy (both are 32-bit)
      MachineInstr *Copy = MIB.buildCopy(Dst, Src);
      if (!constrainSelectedInstRegOperands(*Copy, TII, TRI, RBI))
        return false;
    }
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_PTRMASK: {
    // Pointer mask: dst = ptr & mask (used for alignment in va_start)
    Register Dst = I.getOperand(0).getReg();
    Register Ptr = I.getOperand(1).getReg();
    Register Mask = I.getOperand(2).getReg();
    MachineIRBuilder MIB(I);
    MachineInstr *AndMI = MIB.buildInstr(Haydn::AND32)
                               .addDef(Dst)
                               .addReg(Ptr)
                               .addReg(Mask);
    if (!constrainSelectedInstRegOperands(*AndMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_SDIV:
  case TargetOpcode::G_UDIV:
  case TargetOpcode::G_SREM:
  case TargetOpcode::G_UREM:
    // Legalizer libcallFor owns integer div/rem. Residual G_* at ISel
    // is fail-closed (no post-RA LIBCALL_* ABI invent).
    return false;

  case TargetOpcode::G_MERGE_VALUES: {
    Register Dst = I.getOperand(0).getReg();
    LLT DstTy = MRI.getType(Dst);

    if (DstTy.getSizeInBits() == 64 && I.getNumOperands() == 3) {
      // 2 x s32 -> s64 merge.
      // Check if the destination is in the DR64 register bank.
      const RegisterBank *DstBank = RBI.getRegBank(Dst, MRI, TRI);
      if (DstBank && DstBank->getID() == Haydn::DR64RegBankID) {
        // Destination is DR64 — emit MOV_GPR_TO_DR64 pseudo.
        Register Lo = I.getOperand(1).getReg();
        Register Hi = I.getOperand(2).getReg();
        MachineIRBuilder MIB(I);
        MachineInstr *MovMI =
            MIB.buildInstr(Haydn::MOV_GPR_TO_DR64)
                .addDef(Dst)
                .addReg(Lo)
                .addReg(Hi);
        if (!constrainSelectedInstRegOperands(*MovMI, TII, TRI, RBI))
          return false;
      } else {
        // Destination is in GPR32 space — constrain and erase.
        RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
        for (unsigned Idx = 1; Idx < I.getNumOperands(); ++Idx) {
          Register Src = I.getOperand(Idx).getReg();
          if (Src.isVirtual())
            RBI.constrainGenericRegister(Src, Haydn::GPR32RegClass, MRI);
        }
      }
    } else if (DstTy.getSizeInBits() == 32 && I.getNumOperands() == 3) {
      // 2 x s16 -> s32 merge (legalizer: {S32, S16}, e.g. 20050316-1 test4
      // bitcast <2 x i16> -> i32). Pack as little-endian:
      //   dst = ((hi & 0xFFFF) << 16) | (lo & 0xFFFF)
      // Sources live in GPR32 after G_TRUNC (sub-reg identity); mask so
      // residual high bits from the parent s32 do not leak into the pack.
      Register Lo = I.getOperand(1).getReg();
      Register Hi = I.getOperand(2).getReg();
      MachineIRBuilder MIB(I);

      if (Lo.isVirtual())
        RBI.constrainGenericRegister(Lo, Haydn::GPR32RegClass, MRI);
      if (Hi.isVirtual())
        RBI.constrainGenericRegister(Hi, Haydn::GPR32RegClass, MRI);
      if (!RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI))
        return false;

      Register LoMasked = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      Register HiMasked = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
      Register HiShifted = MRI.createVirtualRegister(&Haydn::GPR32RegClass);

      MachineInstr *AndLo =
          MIB.buildInstr(Haydn::ANDI32).addDef(LoMasked).addReg(Lo).addImm(
              0xFFFF);
      if (!constrainSelectedInstRegOperands(*AndLo, TII, TRI, RBI))
        return false;

      MachineInstr *AndHi =
          MIB.buildInstr(Haydn::ANDI32).addDef(HiMasked).addReg(Hi).addImm(
              0xFFFF);
      if (!constrainSelectedInstRegOperands(*AndHi, TII, TRI, RBI))
        return false;

      MachineInstr *Shl =
          MIB.buildInstr(Haydn::SLLI32).addDef(HiShifted).addReg(HiMasked).addImm(
              16);
      if (!constrainSelectedInstRegOperands(*Shl, TII, TRI, RBI))
        return false;

      MachineInstr *Or =
          MIB.buildInstr(Haydn::OR32).addDef(Dst).addReg(LoMasked).addReg(
              HiShifted);
      if (!constrainSelectedInstRegOperands(*Or, TII, TRI, RBI))
        return false;
    } else {
      // Unknown merge shape — fail closed rather than erase and leave Dst
      // without a def (LiveIntervals "Reading virtual register without a def").
      return false;
    }
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_UNMERGE_VALUES: {
    // G_UNMERGE_VALUES layout: operands [0..N-1] are defs, operand [N] is src.
    // For %lo, %hi = G_UNMERGE_VALUES %src:
    // operand 0 = %lo (def), operand 1 = %hi (def), operand 2 = %src
    unsigned NumDefs = I.getNumDefs();
    Register Src = I.getOperand(NumDefs).getReg();
    LLT SrcTy = MRI.getType(Src);

    if (SrcTy.getSizeInBits() == 64 && NumDefs == 2) {
      // s64 -> 2 x s32 split. Use MOV_DR64_TO_GPR.
      LLT DstLoTy = MRI.getType(I.getOperand(0).getReg());
      LLT DstHiTy = MRI.getType(I.getOperand(1).getReg());
      if (DstLoTy.isValid() && DstLoTy.getSizeInBits() == 32 &&
          DstHiTy.isValid() && DstHiTy.getSizeInBits() == 32) {
        Register DstLo = I.getOperand(0).getReg();
        Register DstHi = I.getOperand(1).getReg();
        MachineIRBuilder MIB(I);

        if (Src.isVirtual()) {
          if (!RBI.constrainGenericRegister(Src, Haydn::DR64RegClass, MRI)) {
            LLVM_DEBUG(dbgs() << "Failed to constrain Src to DR64\n");
            return false;
          }
        }
        if (!RBI.constrainGenericRegister(DstLo, Haydn::GPR32RegClass, MRI)) {
          LLVM_DEBUG(dbgs() << "Failed to constrain DstLo to GPR32\n");
          return false;
        }
        if (!RBI.constrainGenericRegister(DstHi, Haydn::GPR32RegClass, MRI)) {
          LLVM_DEBUG(dbgs() << "Failed to constrain DstHi to GPR32\n");
          return false;
        }

        // use native MOVE32_DR_L + MOVE32_DR_H (2 ops, no stack) instead
        // of MOV_DR64_TO_GPR (5-op stack spill). Both are slot-0 ALU ops that
        // can bundle with neighbors in the VLIW packet.
        MachineInstr *LoMI =
            MIB.buildInstr(Haydn::MOVE32_DR_L).addDef(DstLo).addReg(Src);
        if (!constrainSelectedInstRegOperands(*LoMI, TII, TRI, RBI))
          return false;
        MachineInstr *HiMI =
            MIB.buildInstr(Haydn::MOVE32_DR_H).addDef(DstHi).addReg(Src);
        if (!constrainSelectedInstRegOperands(*HiMI, TII, TRI, RBI))
          return false;
        LLVM_DEBUG(dbgs() << " replaced MOV_DR64_TO_GPR with"
                             "MOVE32_DR_L + MOVE32_DR_H\n");
        I.eraseFromParent();
        return true;
      }
    }

    if (SrcTy.getSizeInBits() == 64 && NumDefs == 8) {
      // s64 -> 8 x s8 split (v8i8 lane extract; legalizer bitcasts v8i8 to
      // s64 then unmerges to 8 x s8 - see HaydnLegalizerInfo.cpp
      // G_EXTRACT_VECTOR_ELT). The legalizer treats v8i8 as DR64-resident
      // (Hard #2: DR64 is a separate bank), so every byte-lane extract must
      // materialise as a real cross-bank DR64->GPR32 lane extract before
      // regalloc. Emit MOVE32_DR_L (low 32 bits = bytes 0..3) + MOVE32_DR_H
      // (high 32 bits = bytes 4..7), then shift+mask each byte lane. This
      // mirrors the s64->2xs32 model (line above) and ensures no generic
      // cross-bank COPY survives to copyPhysReg (which would otherwise fatal
      // - Hard #2). Without this arm the generic fallback below emits
      // buildCopy(Dst, Src) for each byte -> a DR64->GPR32 COPY that
      // copyPhysReg rejects.
      LLT DstTy = MRI.getType(I.getOperand(0).getReg());
      if (DstTy.isValid() && DstTy.getSizeInBits() == 8) {
        MachineIRBuilder MIB(I);

        // Constrain source to DR64.
        if (Src.isVirtual()) {
          if (!RBI.constrainGenericRegister(Src, Haydn::DR64RegClass, MRI))
            return false;
        }

        // MOVE32_DR_L -> bytes 0..3, MOVE32_DR_H -> bytes 4..7.
        Register Lo32 = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        Register Hi32 = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        MachineInstr *LoMI =
            MIB.buildInstr(Haydn::MOVE32_DR_L).addDef(Lo32).addReg(Src);
        if (!constrainSelectedInstRegOperands(*LoMI, TII, TRI, RBI))
          return false;
        MachineInstr *HiMI =
            MIB.buildInstr(Haydn::MOVE32_DR_H).addDef(Hi32).addReg(Src);
        if (!constrainSelectedInstRegOperands(*HiMI, TII, TRI, RBI))
          return false;

        // Extract 8 x s8 from the 2 x s32.
        // Lo32 bits [7:0]=elt0, [15:8]=elt1, [23:16]=elt2, [31:24]=elt3
        // Hi32 bits [7:0]=elt4, [15:8]=elt5, [23:16]=elt6, [31:24]=elt7
        for (unsigned Idx = 0; Idx < 8; ++Idx) {
          Register Dst = I.getOperand(Idx).getReg();
          if (!RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI))
            return false;

          Register Src32 = (Idx < 4) ? Lo32 : Hi32;
          unsigned Shift = (Idx % 4) * 8;

          Register Tmp = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
          if (Shift > 0) {
            // SRLI32 Tmp, Src32, Shift
            MIB.buildInstr(Haydn::SRLI32)
                .addDef(Tmp)
                .addReg(Src32)
                .addImm(Shift);
          } else {
            Tmp = Src32;
          }

          // ANDI32 Dst, Tmp, 0xFF
          MIB.buildInstr(Haydn::ANDI32)
              .addDef(Dst)
              .addReg(Tmp)
              .addImm(0xFF);
        }

        I.eraseFromParent();
        return true;
      }
    }

    if (SrcTy.getSizeInBits() == 64 && NumDefs == 4) {
      // s64 -> 4 x s16 split.
      // Use MOV_DR64_TO_GPR to get 2 x s32, then shift+mask each.
      LLT DstTy = MRI.getType(I.getOperand(0).getReg());
      if (DstTy.isValid() && DstTy.getSizeInBits() == 16) {
        MachineIRBuilder MIB(I);

        // Constrain source to DR64.
        if (Src.isVirtual()) {
          if (!RBI.constrainGenericRegister(Src, Haydn::DR64RegClass, MRI))
            return false;
        }

        // Create temporaries for MOV_DR64_TO_GPR results.
        Register Lo32 = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        Register Hi32 = MRI.createVirtualRegister(&Haydn::GPR32RegClass);

        MIB.buildInstr(Haydn::MOV_DR64_TO_GPR)
            .addDef(Lo32)
            .addDef(Hi32)
            .addReg(Src);

        // Extract 4 x s16 from the 2 x s32.
        // Lo32 bits [15:0] = element 0, bits [31:16] = element 1
        // Hi32 bits [15:0] = element 2, bits [31:16] = element 3
        for (unsigned Idx = 0; Idx < 4; ++Idx) {
          Register Dst = I.getOperand(Idx).getReg();
          if (!RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI))
            return false;

          Register Src32 = (Idx < 2) ? Lo32 : Hi32;
          unsigned Shift = (Idx % 2) * 16;

          Register Tmp = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
          if (Shift > 0) {
            // SRLI32 Tmp, Src32, Shift
            MIB.buildInstr(Haydn::SRLI32)
                .addDef(Tmp)
                .addReg(Src32)
                .addImm(Shift);
          } else {
            Tmp = Src32;
          }

          // ANDI32 Dst, Tmp, 0xFFFF
          MIB.buildInstr(Haydn::ANDI32)
              .addDef(Dst)
              .addReg(Tmp)
              .addImm(0xFFFF);
        }

        I.eraseFromParent();
        return true;
      }
    }

    // 32-bit residual packs (<2 x s16>, <4 x s8>) live in ONE GPR32: shift+mask
    // each lane. The old fallback COPY'd the whole GPR into every def, so lane
    // 0 was accidentally right and higher lanes were wrong.
    if (SrcTy.getSizeInBits() == 32 && (NumDefs == 2 || NumDefs == 4)) {
      LLT DstTy = MRI.getType(I.getOperand(0).getReg());
      const unsigned LaneBits = 32 / NumDefs;
      if (DstTy.isValid() && DstTy.getSizeInBits() == LaneBits) {
        MachineIRBuilder MIB(I);
        if (Src.isVirtual()) {
          if (!RBI.constrainGenericRegister(Src, Haydn::GPR32RegClass, MRI))
            return false;
        }
        const unsigned Mask = (1u << LaneBits) - 1;
        for (unsigned Idx = 0; Idx < NumDefs; ++Idx) {
          Register Dst = I.getOperand(Idx).getReg();
          if (!RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI))
            return false;
          Register Tmp = Src;
          if (unsigned Shift = Idx * LaneBits) {
            Tmp = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
            MIB.buildInstr(Haydn::SRLI32).addDef(Tmp).addReg(Src).addImm(Shift);
          }
          MIB.buildInstr(Haydn::ANDI32).addDef(Dst).addReg(Tmp).addImm(Mask);
        }
        I.eraseFromParent();
        return true;
      }
    }

    // Fail closed for width-changing unmerge. COPY-to-every-def only works for
    // same-width degenerate unmerge; narrower shapes silently miscompiled
    // (every lane = lane 0 / illegal cross-bank MOVE32).
    MachineIRBuilder MIB(I);
    for (unsigned Idx = 0; Idx < NumDefs; ++Idx) {
      Register Dst = I.getOperand(Idx).getReg();
      LLT DstTy = MRI.getType(Dst);
      if (DstTy.isValid() && DstTy.getSizeInBits() != SrcTy.getSizeInBits()) {
        LLVM_DEBUG(dbgs() << "Unhandled G_UNMERGE_VALUES shape: " << SrcTy
                          << " -> " << NumDefs << " x " << DstTy << "\n");
        return false;
      }
    }
    for (unsigned Idx = 0; Idx < NumDefs; ++Idx) {
      Register Dst = I.getOperand(Idx).getReg();
      if (Dst != Src) {
        MachineInstr *Copy = MIB.buildCopy(Dst, Src);
        if (!constrainSelectedInstRegOperands(*Copy, TII, TRI, RBI))
          return false;
      }
    }
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_IMPLICIT_DEF: {
    // Convert G_IMPLICIT_DEF to target IMPLICIT_DEF. We must NOT erase
    // the instruction here: the virtual register it defines may still be
    // referenced by PHI nodes or other instructions. Erasing it would
    // leave those uses without a definition, crashing LiveVariables and
    // other passes that call MRI->getVRegDef.
    Register Dst = I.getOperand(0).getReg();
    constrainBankAware(Dst, MRI);
    I.setDesc(TII.get(TargetOpcode::IMPLICIT_DEF));
    return true;
  }

  case TargetOpcode::G_FREEZE: {
    // G_FREEZE is a no-op — convert to COPY so the register allocator
    // preserves the value without any optimization of undef bits.
    Register Dst = I.getOperand(0).getReg();
    Register Src = I.getOperand(1).getReg();
    // Constrain both registers to the appropriate class (bank-aware: a
    // 64-bit AR-banked operand keeps ARRegClass).
    constrainBankAware(Dst, MRI);
    if (Src.isVirtual())
      constrainBankAware(Src, MRI);
    I.setDesc(TII.get(TargetOpcode::COPY));
    return true;
  }

  case TargetOpcode::G_ASSERT_SEXT:
  case TargetOpcode::G_ASSERT_ZEXT: {
    // Hint instructions carrying extension info — identity ops at register level.
    // We handle these here (instead of relying on InstructionSelect's generic
    // handler) to ensure both registers get proper register classes via
    // constrainGenericRegister. The generic handler only propagates an existing
    // RegClass, which may not exist for bank-only vregs.
    Register Dst = I.getOperand(0).getReg();
    Register Src = I.getOperand(1).getReg();
    LLT DstTy = MRI.getType(Dst);
    const TargetRegisterClass &RC =
        (DstTy.getSizeInBits() == 64) ? Haydn::DR64RegClass
                                      : Haydn::GPR32RegClass;
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, RC, MRI);
    RBI.constrainGenericRegister(Dst, RC, MRI);
    if (Dst != Src)
      MRI.replaceRegWith(Dst, Src);
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_PHI: {
    // Convert G_PHI to target PHI for the register allocator / phi-elimination.
    // Must setDesc to PHI so the verifier no longer sees a generic instruction.
    // Follow the same pattern as RISC-V/AArch64: determine RC from def reg
    // convert opcode, then constrain the def register.
    const Register DefReg = I.getOperand(0).getReg();
    const LLT DefTy = MRI.getType(DefReg);
    const RegClassOrRegBank &RegClassOrBank = MRI.getRegClassOrRegBank(DefReg);

    const TargetRegisterClass *DefRC =
        dyn_cast<const TargetRegisterClass *>(RegClassOrBank);
    if (!DefRC) {
      if (!DefTy.isValid()) {
        LLVM_DEBUG(dbgs() << "G_PHI def has no type, not a virtual reg?\n");
        return false;
      }
      // Infer register class from type size.
      DefRC = (DefTy.getSizeInBits() == 64) ? &Haydn::DR64RegClass
                                             : &Haydn::GPR32RegClass;
    }

    I.setDesc(TII.get(TargetOpcode::PHI));
    return RBI.constrainGenericRegister(DefReg, *DefRC, MRI);
  }

  case TargetOpcode::G_VAARG: {
    // Thin map to VAARG_I32/I64. ExpandPseudos implements the full two-bank +
    // stack-overflow algorithm (same place as VASTART).
    // Operands: [Dst(0), VaListPtr(1), Align(2)].
    // Legalizer already rejects vectors / >64-bit; re-check here so a drifted
    // alwaysLegal rule cannot silently select the wrong bank pseudo.
    Register Dst = I.getOperand(0).getReg();
    Register VaListPtr = I.getOperand(1).getReg();
    LLT DstTy = MRI.getType(Dst);
    if (DstTy.isVector())
      return false;
    const unsigned BitWidth =
        DstTy.isPointer() ? 32u : DstTy.getSizeInBits();
    if (BitWidth != 32 && BitWidth != 64)
      return false;
    // Stack step is 8 bytes; over-aligned va_arg is not an ABI product path.
    if (I.getNumOperands() >= 3 && I.getOperand(2).isImm()) {
      const int64_t Align = I.getOperand(2).getImm();
      if (Align <= 0 || Align > 8 || (Align & (Align - 1)) != 0)
        return false;
    }
    MachineIRBuilder MIB(I);
    const bool IsI64 = BitWidth == 64;
    if (IsI64)
      RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
    else
      RBI.constrainGenericRegister(Dst, Haydn::GPR32RegClass, MRI);
    if (VaListPtr.isVirtual())
      RBI.constrainGenericRegister(VaListPtr, Haydn::GPR32RegClass, MRI);
    unsigned Opc = IsI64 ? Haydn::VAARG_I64 : Haydn::VAARG_I32;
    MachineInstr *Pseudo =
        MIB.buildInstr(Opc).addDef(Dst).addReg(VaListPtr);
    // Preserve any G_VAARG MMOs onto the expand-owned pseudo.
    if (!constrainSelectedMemInst(Pseudo, I, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_VASTART: {
    // G_VASTART carries MOStore of the full structured list (getVaListSizeInBits);
    // clone onto VASTART so ExpandPseudos can transfer onto real ST32s.
    Register VaListPtr = I.getOperand(0).getReg();
    MachineIRBuilder MIB(I);
    MachineInstr *Pseudo = MIB.buildInstr(Haydn::VASTART).addReg(VaListPtr);
    if (!constrainSelectedMemInst(Pseudo, I, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case TargetOpcode::G_INTRINSIC: {
    return selectIntrinsic(I);
  }

  case TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS: {
    // Handle varargs intrinsics that come as G_INTRINSIC_W_SIDE_EFFECTS.
    Intrinsic::ID IntrID =
        cast<GIntrinsic>(I).getIntrinsicID();
    switch (IntrID) {
    default:
      return false;
    case Intrinsic::vaend:
      // va_end is a no-op for baremetal.
      I.eraseFromParent();
      return true;
    case Intrinsic::vacopy: {
      // va_copy: clone MMOs from getTgtMemIntrinsic for ExpandPseudos transfer.
      Register DstPtr = I.getOperand(1).getReg();
      Register SrcPtr = I.getOperand(2).getReg();
      MachineIRBuilder MIB(I);
      MachineInstr *Pseudo =
          MIB.buildInstr(Haydn::VACOPY).addReg(DstPtr).addReg(SrcPtr);
      if (!constrainSelectedMemInst(Pseudo, I, TII, TRI, RBI))
        return false;
      I.eraseFromParent();
      return true;
    }
    // Circular Buffer and Bit-Reversed addressing intrinsics have memory
    // side effects, so they arrive as G_INTRINSIC_W_SIDE_EFFECTS.
    // Delegate to the main selectIntrinsic handler which has all the cases.
    case Intrinsic::haydn_ldw_cb_imm:
    case Intrinsic::haydn_ldw_cb_reg:
    case Intrinsic::haydn_sdw_cb_imm:
    case Intrinsic::haydn_sdw_cb_reg:
    // CBR-setup intrinsics (setcbr_begin/end) write CSRs — side-effecting
    // so they arrive as G_INTRINSIC_W_SIDE_EFFECTS.
    case Intrinsic::haydn_setcbr_begin:
    case Intrinsic::haydn_setcbr_end:
    case Intrinsic::haydn_ldw_brev_imm:
    case Intrinsic::haydn_ldw_brev_reg:
    case Intrinsic::haydn_lw_brev_imm:
    case Intrinsic::haydn_lw_brev_reg:
    case Intrinsic::haydn_sdw_brev_imm:
    case Intrinsic::haydn_sdw_brev_reg:
    case Intrinsic::haydn_sw_brev_imm:
    case Intrinsic::haydn_sw_brev_reg:
    // POST/PRE AGU writeback loads: multi-result {data, new_ptr}.
    case Intrinsic::haydn_d_ldw_post_imm:
    case Intrinsic::haydn_d_ldw_post_reg:
    case Intrinsic::haydn_d_ldw_pre_imm:
    case Intrinsic::haydn_d_ldw_pre_reg:
    case Intrinsic::haydn_d_lw_post_imm:
    case Intrinsic::haydn_d_lw_post_reg:
    case Intrinsic::haydn_d_lw_pre_imm:
    case Intrinsic::haydn_d_lw_pre_reg:
    case Intrinsic::haydn_d_lhw_post_imm:
    case Intrinsic::haydn_d_lhw_post_reg:
    case Intrinsic::haydn_d_lhw_pre_imm:
    case Intrinsic::haydn_d_lhw_pre_reg:
    case Intrinsic::haydn_s_lw_post_imm:
    case Intrinsic::haydn_s_lw_post_reg:
    case Intrinsic::haydn_s_lw_pre_imm:
    case Intrinsic::haydn_s_lw_pre_reg:
    case Intrinsic::haydn_s_lhws_post_imm:
    case Intrinsic::haydn_s_lhws_post_reg:
    case Intrinsic::haydn_s_lhws_pre_imm:
    case Intrinsic::haydn_s_lhws_pre_reg:
    case Intrinsic::haydn_s_lhwu_post_imm:
    case Intrinsic::haydn_s_lhwu_post_reg:
    case Intrinsic::haydn_s_lhwu_pre_imm:
    case Intrinsic::haydn_s_lhwu_pre_reg:
    case Intrinsic::haydn_s_lbs_post_imm:
    case Intrinsic::haydn_s_lbs_post_reg:
    case Intrinsic::haydn_s_lbs_pre_imm:
    case Intrinsic::haydn_s_lbs_pre_reg:
    case Intrinsic::haydn_s_lbu_post_imm:
    case Intrinsic::haydn_s_lbu_post_reg:
    case Intrinsic::haydn_s_lbu_pre_imm:
    case Intrinsic::haydn_s_lbu_pre_reg:
    // Golden LS POST/PRE stores: single-ret writeback (new_ptr), side-effecting
    // memory write → G_INTRINSIC_W_SIDE_EFFECTS. Bodies lower like sdw_cb to
    // (outs wb),(ins data,base,off). Without this allowlist they cannot-select.
    case Intrinsic::haydn_d_sdw_post_imm:
    case Intrinsic::haydn_d_sdw_post_reg:
    case Intrinsic::haydn_d_sdw_pre_imm:
    case Intrinsic::haydn_d_sdw_pre_reg:
    case Intrinsic::haydn_d_shw_post_imm:
    case Intrinsic::haydn_d_shw_post_reg:
    case Intrinsic::haydn_d_shw_pre_imm:
    case Intrinsic::haydn_d_shw_pre_reg:
    case Intrinsic::haydn_d_sw_h_post_imm:
    case Intrinsic::haydn_d_sw_h_post_reg:
    case Intrinsic::haydn_d_sw_h_pre_imm:
    case Intrinsic::haydn_d_sw_h_pre_reg:
    case Intrinsic::haydn_d_sw_l_post_imm:
    case Intrinsic::haydn_d_sw_l_post_reg:
    case Intrinsic::haydn_d_sw_l_pre_imm:
    case Intrinsic::haydn_d_sw_l_pre_reg:
    case Intrinsic::haydn_s_sb_post_imm:
    case Intrinsic::haydn_s_sb_post_reg:
    case Intrinsic::haydn_s_sb_pre_imm:
    case Intrinsic::haydn_s_sb_pre_reg:
    case Intrinsic::haydn_s_shw_post_imm:
    case Intrinsic::haydn_s_shw_post_reg:
    case Intrinsic::haydn_s_shw_pre_imm:
    case Intrinsic::haydn_s_shw_pre_reg:
    case Intrinsic::haydn_s_sw_post_imm:
    case Intrinsic::haydn_s_sw_post_reg:
    case Intrinsic::haydn_s_sw_pre_imm:
    case Intrinsic::haydn_s_sw_pre_reg:
    // Golden LS WITH_* (no AGU writeback): IntrHasSideEffects → arrive as
 // G_INTRINSIC_W_SIDE_EFFECTS. Bodies live in selectIntrinsic (~5454).
    // Without this allowlist, e.g. d_lw_with_imm fails "cannot select".
    case Intrinsic::haydn_d_ldw_with_imm:
    case Intrinsic::haydn_d_ldw_with_reg:
    case Intrinsic::haydn_d_lhw_with_imm:
    case Intrinsic::haydn_d_lhw_with_reg:
    case Intrinsic::haydn_d_lw_with_imm:
    case Intrinsic::haydn_d_lw_with_reg:
    case Intrinsic::haydn_s_lbs_with_imm:
    case Intrinsic::haydn_s_lbs_with_reg:
    case Intrinsic::haydn_s_lbu_with_imm:
    case Intrinsic::haydn_s_lbu_with_reg:
    case Intrinsic::haydn_s_lhws_with_imm:
    case Intrinsic::haydn_s_lhws_with_reg:
    case Intrinsic::haydn_s_lhwu_with_imm:
    case Intrinsic::haydn_s_lhwu_with_reg:
    case Intrinsic::haydn_s_lw_with_imm:
    case Intrinsic::haydn_s_lw_with_reg:
    case Intrinsic::haydn_d_sdw_with_imm:
    case Intrinsic::haydn_d_sdw_with_reg:
    case Intrinsic::haydn_d_shw_with_imm:
    case Intrinsic::haydn_d_shw_with_reg:
    case Intrinsic::haydn_d_sw_h_with_imm:
    case Intrinsic::haydn_d_sw_h_with_reg:
    case Intrinsic::haydn_d_sw_l_with_imm:
    case Intrinsic::haydn_d_sw_l_with_reg:
    case Intrinsic::haydn_s_sb_with_imm:
    case Intrinsic::haydn_s_sb_with_reg:
    case Intrinsic::haydn_s_shw_with_imm:
    case Intrinsic::haydn_s_shw_with_reg:
    case Intrinsic::haydn_s_sw_with_imm:
    case Intrinsic::haydn_s_sw_with_reg:
    // AR unaligned stream (PLDWWUA / D_*UA_POST / FLAR / WBARWUA) — AR state
    // + memory; arrive as G_INTRINSIC_W_SIDE_EFFECTS.
    case Intrinsic::haydn_pldwwua:
    case Intrinsic::haydn_d_lqhwua_post:
    case Intrinsic::haydn_d_ltwua_post:
    case Intrinsic::haydn_flar:
    case Intrinsic::haydn_wbarwua:
    case Intrinsic::haydn_d_sqhwua_post:
    case Intrinsic::haydn_d_stwua_post:
    // SFR-modifying intrinsics read/write the Status Flag Register
    // which is a side effect invisible to LLVM's memory model.
    // X2/X4 SIMD compares use IntrHasSideEffects so they arrive here as
    // G_INTRINSIC_W_SIDE_EFFECTS (not G_INTRINSIC). They were previously
    // selected only via tablegen Pats; those Pats are removed now that the
    // instructions use Defs/Uses=[SFR] + hasSideEffects=0 (SMS barrier fix).
    case Intrinsic::haydn_slt64:
    case Intrinsic::haydn_sle64:
    case Intrinsic::haydn_seq64:
    case Intrinsic::haydn_movt64:
    case Intrinsic::haydn_movf64:
    case Intrinsic::haydn_x2seq32:
    case Intrinsic::haydn_x2slt32:
    case Intrinsic::haydn_x2sle32:
    case Intrinsic::haydn_x2movf32:
    case Intrinsic::haydn_x2movt32:
    case Intrinsic::haydn_x4seq16:
    case Intrinsic::haydn_x4slt16:
    case Intrinsic::haydn_x4sle16:
    case Intrinsic::haydn_x4movf16:
    case Intrinsic::haydn_x4movt16:
    case Intrinsic::haydn_movesfr2gpr:
    case Intrinsic::haydn_movegpr2sfr:
    case Intrinsic::haydn_zero_sfr:
      return selectIntrinsic(I);
    // IR-level hardware-loop intrinsics. The upstream HardwareLoops
    // pass (llvm/lib/CodeGen/HardwareLoops.cpp) inserts these; we select
    // them to the SMS-safe LoopStart/PseudoLoopEnd pseudos here. Mirrors
    // AIE's AIEBaseInstructionSelector::selectSetLoopIterations
    // (AIEBaseInstructionSelector.cpp:134-142).
    // llvm.set.loop.iterations(N) → LoopStart N, adj=0 (preheader)
    // llvm.loop.decrement → handled in G_BRCOND (latch)
    // llvm.loop.decrement.reg → handled in G_BRCOND (JNZD latch)
    case Intrinsic::set_loop_iterations:
    case Intrinsic::test_set_loop_iterations:
    case Intrinsic::start_loop_iterations:
    case Intrinsic::test_start_loop_iterations: {
      // The trip-count value is operand 1 (for set/test_set) or operand 2
      // (for start/test_start which return the value). Both forms carry the
      // count in the last operand before any varargs.
      unsigned CountOpIdx = (IntrID == Intrinsic::start_loop_iterations ||
                             IntrID == Intrinsic::test_start_loop_iterations)
                                ? 2
                                : 1;
      Register TripCountReg = I.getOperand(CountOpIdx).getReg();
      MachineIRBuilder MIB(I);
      MachineInstr *LS = MIB.buildInstr(Haydn::LoopStart, {},
                                        {TripCountReg})
                             .addImm(0); // adj: pipeliner trip-count adjust
      if (!constrainSelectedInstRegOperands(*LS, TII, TRI, RBI))
        return false;
      I.eraseFromParent();
      return true;
    }
    case Intrinsic::loop_decrement:
    case Intrinsic::loop_decrement_reg: {
      // Folded into PseudoLoopEnd / LoopDec+LoopJNZ from G_BRCOND when the
      // def is one-use (T-GS4). Erase only if the result is dead. A live
      // loop.decrement.reg (CSE / extra copy) selects to LoopDec so remaining
      // users keep a def. Live ZOL loop.decrement has no GPR count — fail
      // closed rather than invent a boolean.
      Register Dst = I.getOperand(0).getReg();
      if (MRI.use_nodbg_empty(Dst)) {
        I.eraseFromParent();
        return true;
      }
      if (IntrID != Intrinsic::loop_decrement_reg)
        return false;
      Register PrevLC = I.getOperand(2).getReg();
      MachineIRBuilder MIB(I);
      MachineInstr *LD = MIB.buildInstr(Haydn::LoopDec, {Dst}, {PrevLC});
      if (!constrainSelectedInstRegOperands(*LD, TII, TRI, RBI))
        return false;
      I.eraseFromParent();
      return true;
    }
    }
  }

  case TargetOpcode::G_BUILD_VECTOR: {
    // Build a vector from scalar elements.
    // For SIMD types (v2i32, v4i16, v8i8), this merges scalars into DR64.
    Register Dst = I.getOperand(0).getReg();
    LLT DstTy = MRI.getType(Dst);
    unsigned NumElems = I.getNumOperands() - 1; // First operand is the destination

    if (!DstTy.isVector()) {
      return false; // Only handle vector types
    }

    unsigned ElemSize = DstTy.getElementType().getSizeInBits();

    // Only v2i32 reaches select: legalizer custom-packs v4i16/v8i8 into
    // scalar OR/SHL + legal G_BUILD_VECTOR <2 x s32> + G_BITCAST (AIE-shaped).
    if (DstTy.getSizeInBits() == 64 && NumElems == 2 && ElemSize == 32) {
      // v2i32: merge two s32 into s64 (DR64) — analogue of AIE selectLRegSequence.
      Register Elem0 = I.getOperand(1).getReg();
      Register Elem1 = I.getOperand(2).getReg();
      MachineIRBuilder MIB(I);

      if (Dst.isVirtual())
        RBI.constrainGenericRegister(Dst, Haydn::DR64RegClass, MRI);
      if (Elem0.isVirtual())
        RBI.constrainGenericRegister(Elem0, Haydn::GPR32RegClass, MRI);
      if (Elem1.isVirtual())
        RBI.constrainGenericRegister(Elem1, Haydn::GPR32RegClass, MRI);

      MachineInstr *MergeMI = MIB.buildInstr(Haydn::MOV_GPR_TO_DR64)
                                  .addDef(Dst)
                                  .addReg(Elem0)
                                  .addReg(Elem1);
      if (!constrainSelectedInstRegOperands(*MergeMI, TII, TRI, RBI))
        return false;
      I.eraseFromParent();
      return true;
    }

    return false;
  }

  case TargetOpcode::G_BITCAST: {
    // Bitcast between same-size types is just a COPY.
    // Both v2i32 and v4i16 are 64-bit DR64.
    Register Dst = I.getOperand(0).getReg();
    Register Src = I.getOperand(1).getReg();
    MachineIRBuilder MIB(I);

    // Both operands map to DR64 for vector types, or GPR32 for scalar.
    LLT DstTy = MRI.getType(Dst);
    const TargetRegisterClass &RC =
        (DstTy.getSizeInBits() == 64) ? Haydn::DR64RegClass
                                      : Haydn::GPR32RegClass;
    if (Dst.isVirtual())
      RBI.constrainGenericRegister(Dst, RC, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, RC, MRI);

    MachineInstr *Copy = MIB.buildCopy(Dst, Src);
    if (!constrainSelectedInstRegOperands(*Copy, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  // trap → noreturn call to abort (A.5). Never lower to RET (return into
  // following code). Do not create new MBBs here — InstructionSelect iterates
  // MBBs and CFG surgery crashes. Empty unreachable dead-ends still get soft
  // RET from HaydnEnsureTerminators (separate contract).
  case TargetOpcode::G_TRAP:
  case TargetOpcode::G_DEBUGTRAP:
  case TargetOpcode::G_UBSANTRAP: {
    MachineIRBuilder MIB(I);
    // Direct WIDE JAL to abort (same path as CallLowering external symbols).
    // If abort returns, call again (fail closed, no fallthrough RET).
    for (unsigned N = 0; N < 2; ++N) {
      MachineInstr *Call = MIB.buildInstr(Haydn::JAL_W)
                               .addDef(Haydn::R15)
                               .addExternalSymbol("abort");
      if (!constrainSelectedInstRegOperands(*Call, TII, TRI, RBI))
        return false;
    }
    I.eraseFromParent();
    return true;
  }

  // G_FENCE → compiler barrier. Haydn has no native fence; single-core
  // baremetal treats all orderings/scopes as MEMBARRIER (AsmPrinter no-op
  // comment). Covers __atomic_signal_fence / fence syncscope("singlethread")
  // and full-system fences that libc Atomic emits around seq_cst ops.
  case TargetOpcode::G_FENCE: {
    MachineIRBuilder MIB(I);
    MIB.buildInstr(TargetOpcode::MEMBARRIER);
    I.eraseFromParent();
    return true;
  }

  default:
    return false;
  }
}

bool HaydnInstructionSelector::selectIntrinsic(MachineInstr &I) {
  using namespace Intrinsic;
  using namespace Haydn;

  unsigned IntrID = cast<GIntrinsic>(I).getIntrinsicID();
  // For void-returning intrinsics, operand 0 is the intrinsic ID (not a def).
  // Extract DstReg only if the first operand is a register def.
  Register DstReg;
  if (I.getOperand(0).isReg())
    DstReg = I.getOperand(0).getReg();
  MachineIRBuilder MIB(I);
  MachineFunction &MF = *I.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  // ImmArg intrinsics lower constants as bare Imm/CImm on G_INTRINSIC, not
  // always as G_CONSTANT vregs. Accept Imm, CImm, or constant-looking vreg.
  auto getConstOpSExt = [&](const MachineOperand &MO,
                            int64_t &Out) -> bool {
    if (MO.isImm()) {
      Out = MO.getImm();
      return true;
    }
    if (MO.isCImm()) {
      Out = MO.getCImm()->getSExtValue();
      return true;
    }
    if (MO.isReg()) {
      auto C = getIConstantVRegValWithLookThrough(MO.getReg(), MRI);
      if (!C)
        return false;
      Out = C->Value.getSExtValue();
      return true;
    }
    return false;
  };
  auto getConstOpZExt = [&](const MachineOperand &MO,
                            uint64_t &Out) -> bool {
    if (MO.isImm()) {
      Out = static_cast<uint64_t>(MO.getImm());
      return true;
    }
    if (MO.isCImm()) {
      Out = MO.getCImm()->getZExtValue();
      return true;
    }
    if (MO.isReg()) {
      auto C = getIConstantVRegValWithLookThrough(MO.getReg(), MRI);
      if (!C)
        return false;
      Out = C->Value.getZExtValue();
      return true;
    }
    return false;
  };

  // Helper lambdas for common intrinsic patterns.

  // Select a unary intrinsic: dst = op(src).
  // Operand layout: def DstReg, intrinsic_id, src.
  auto selectUnary = [&](unsigned Opcode, const TargetRegisterClass &RC) {
    Register SrcReg = I.getOperand(2).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, RC, MRI);
    if (SrcReg.isVirtual())
      RBI.constrainGenericRegister(SrcReg, RC, MRI);
    MachineInstr *MI =
        MIB.buildInstr(Opcode).addDef(DstReg).addReg(SrcReg);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  };

  // R_GD unary: GPR32 dest + DR64 source (FormatsALU64 R_GD / POPCOUNT64 shape).
  // Same-bank selectUnary cannot express the cross-bank constraint.
  auto selectUnaryR_GD = [&](unsigned Opcode) {
    Register SrcReg = I.getOperand(2).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, GPR32RegClass, MRI);
    if (SrcReg.isVirtual())
      RBI.constrainGenericRegister(SrcReg, DR64RegClass, MRI);
    MachineInstr *MI =
        MIB.buildInstr(Opcode).addDef(DstReg).addReg(SrcReg);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  };

  // Select a binary intrinsic: dst = op(src1, src2).
  // Operand layout: def DstReg, intrinsic_id, src1, src2.
  auto selectBinary = [&](unsigned Opcode, const TargetRegisterClass &RC) {
    Register Src0 = I.getOperand(2).getReg();
    Register Src1 = I.getOperand(3).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, RC, MRI);
    if (Src0.isVirtual())
      RBI.constrainGenericRegister(Src0, RC, MRI);
    if (Src1.isVirtual())
      RBI.constrainGenericRegister(Src1, RC, MRI);
    MachineInstr *MI = MIB.buildInstr(Opcode)
                           .addDef(DstReg)
                           .addReg(Src0)
                           .addReg(Src1);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  };

  // Select a SIMD shift intrinsic: dst = op(src_dr64, amt_gpr32).
  // The instruction uses DR64 for the data operand and GPR32 for the shift
  // amount. Operand layout: def DstReg, intrinsic_id, src(DR64), amt.
  // After the lanewise retype the intrinsic signature is
  // `<vec>(<vec>, i32)`, so the amount operand already arrives as a scalar
  // i32 in the GPR32 bank — it can be used directly. (earlier the amount
  // was an opaque i64/DR64 and had to be narrowed via MOV_DR64_TO_GPR; that
  // path is retained below as a defensive fallback for any 64-bit/vector
  // amount that survives legalization, but it is no longer the common case.)
  // Bank detection: the register bank must be inferred from the operand
  // *type*, NOT from MRI.getRegBankOrNull. At this selection point the
  // bank-only vregs from RegBankSelect have been constrained to a register
  // class but getRegBankOrNull still returns null for them, so a bank
  // comparison wrongly classifies a genuine GPR32 scalar as DR64 and feeds
  // it to MOV_DR64_TO_GPR (verifier abort: "Expected a DR64 register, but
  // got a GPR32 register"). This was the regression surfaced by the
  // retype: the 8 vector-scalar shift intrinsics are the only mixed-bank
  // (DR64 data + GPR32 amount) retyped intrinsics.
  auto selectDR64ShiftGPR32 = [&](unsigned Opcode) {
    Register Src = I.getOperand(2).getReg();
    Register Amt = I.getOperand(3).getReg();

    // The shift amount must land in GPR32. If it is already a scalar ≤32 bits
    // it lives in GPR32 — use it directly. Only a genuine 64-bit/vector value
    // needs the cross-bank DR64→GPR32 narrowing below.
    Register AmtGPR32 = Amt;
    if (Amt.isVirtual()) {
      LLT AmtTy = MRI.getType(Amt);
      bool AlreadyScalarGPR32 =
          AmtTy.isValid() && !AmtTy.isVector() &&
          AmtTy.getSizeInBits() <= 32;
      if (!AlreadyScalarGPR32) {
        // Narrow a 64-bit/vector amount: extract the low GPR32 lane from the
        // DR64 shift-amount register. MOVE32 cannot be used here: it requires
        // both operands in GPR32 (DR64 is a separate register bank), so
        // emitting MOVE32 with a DR64 source triggers a verifier abort. The
        // MOV_DR64_TO_GPR pseudo is the cross-bank transfer: it extracts one
        // DR64 into a (low, high) pair of GPR32s. Only the low half is kept
        // the shift amount uses only the low bits (rs[5:0]/rs[4:0]) per spec
        // so the high half is dead and DCE'd after RA expansion.
        Register AmtLo = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        Register AmtHi = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
        RBI.constrainGenericRegister(Amt, Haydn::DR64RegClass, MRI);
        RBI.constrainGenericRegister(AmtLo, Haydn::GPR32RegClass, MRI);
        RBI.constrainGenericRegister(AmtHi, Haydn::GPR32RegClass, MRI);
        // MOV_DR64_TO_GPR $rd_lo, $rd_hi, $rs (DR64 → GPR32 pair).
        MIB.buildInstr(Haydn::MOV_DR64_TO_GPR, {AmtLo, AmtHi}, {Amt});
        AmtGPR32 = AmtLo;
      }
    }

    if (AmtGPR32.isVirtual())
      RBI.constrainGenericRegister(AmtGPR32, Haydn::GPR32RegClass, MRI);
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, Haydn::DR64RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, Haydn::DR64RegClass, MRI);

    MachineInstr *MI = MIB.buildInstr(Opcode)
                           .addDef(DstReg)
                           .addReg(Src)
                           .addReg(AmtGPR32);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  };

  // Select a ternary intrinsic: dst = op(src1, src2, src3).
  // Operand layout: def DstReg, intrinsic_id, src1, src2, src3.
  auto selectTernary = [&](unsigned Opcode, const TargetRegisterClass &RC) {
    Register Src0 = I.getOperand(2).getReg();
    Register Src1 = I.getOperand(3).getReg();
    Register Src2 = I.getOperand(4).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, RC, MRI);
    if (Src0.isVirtual())
      RBI.constrainGenericRegister(Src0, RC, MRI);
    if (Src1.isVirtual())
      RBI.constrainGenericRegister(Src1, RC, MRI);
    if (Src2.isVirtual())
      RBI.constrainGenericRegister(Src2, RC, MRI);
    MachineInstr *MI = MIB.buildInstr(Opcode)
                           .addDef(DstReg)
                           .addReg(Src0)
                           .addReg(Src1)
                           .addReg(Src2);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  };

  // (Path B): Select a TRUE 2-output SIMD MAC intrinsic (non-accum).
  // The intrinsic returns [i64, i64]; golden DR_Write_Port=[rtd1,rtd2].
  // Operand layout: def Dst0, def Dst1, intrinsic_id, src1, src2.
  // Emits a 2-def MachineInstr matching the _D_RR2 slot format classes
  // (every format-class bits-field binds 1:1 to a dag operand — no orphan).
  // Used by X2MUL32/X4MUL16/X2CMUL32*/X4FF2MUL16S (7 ops).
  auto selectSimdMac2Dest = [&](unsigned Opcode) {
    assert(I.getNumDefs() == 2 && "expected 2-def SIMD MAC intrinsic");
    Register Dst0 = I.getOperand(0).getReg();
    Register Dst1 = I.getOperand(1).getReg();
    Register Src0 = I.getOperand(3).getReg();
    Register Src1 = I.getOperand(4).getReg();
    if (Dst0.isVirtual())
      RBI.constrainGenericRegister(Dst0, DR64RegClass, MRI);
    if (Dst1.isVirtual())
      RBI.constrainGenericRegister(Dst1, DR64RegClass, MRI);
    if (Src0.isVirtual())
      RBI.constrainGenericRegister(Src0, DR64RegClass, MRI);
    if (Src1.isVirtual())
      RBI.constrainGenericRegister(Src1, DR64RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(Opcode)
                           .addDef(Dst0)
                           .addDef(Dst1)
                           .addReg(Src0)
                           .addReg(Src1);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  };

  // (Path B): Select a TRUE 2-output SIMD MAC accumulate/subtract
  // intrinsic. The intrinsic returns [i64, i64]; golden DR_Write_Port=
  // [rtd1,rtd2], DR_Read_Port=[rtd1,rtd2,rsd1,rsd2]. Both destinations are
  // also accumulator inputs (tied-def in the.td via Constraints).
  // Operand layout: def Dst0, def Dst1, intrinsic_id, acc1, acc2, src1, src2.
  // Emits a 2-def + 4-use MachineInstr (acc1->rtd1 tie, acc2->rtd2 tie)
  // matching the _D_RRA2 slot format classes (6 fields = 6 operands).
  // Used by X2MULA32/X2MULS32/X4MULA16*/X4MULS16*/X4FF2MULA16S/X4FF2MULS16S.
  auto selectSimdMacAcc2Dest = [&](unsigned Opcode) {
    assert(I.getNumDefs() == 2 && "expected 2-def SIMD MAC accum intrinsic");
    Register Dst0 = I.getOperand(0).getReg();
    Register Dst1 = I.getOperand(1).getReg();
    Register Acc0 = I.getOperand(3).getReg();
    Register Acc1 = I.getOperand(4).getReg();
    Register Src0 = I.getOperand(5).getReg();
    Register Src1 = I.getOperand(6).getReg();
    if (Dst0.isVirtual())
      RBI.constrainGenericRegister(Dst0, DR64RegClass, MRI);
    if (Dst1.isVirtual())
      RBI.constrainGenericRegister(Dst1, DR64RegClass, MRI);
    if (Acc0.isVirtual())
      RBI.constrainGenericRegister(Acc0, DR64RegClass, MRI);
    if (Acc1.isVirtual())
      RBI.constrainGenericRegister(Acc1, DR64RegClass, MRI);
    if (Src0.isVirtual())
      RBI.constrainGenericRegister(Src0, DR64RegClass, MRI);
    if (Src1.isVirtual())
      RBI.constrainGenericRegister(Src1, DR64RegClass, MRI);
    // Emit the tied-def MCInst: 2 defs + 4 uses (acc0,acc1 tied to dst0,dst1;
    // src0,src1 are the multiplier inputs). MIR SSA preserved because Dst0/Dst1
    // and Acc0/Acc1 are distinct vregs joined by the two-address Constraints.
    MachineInstr *MI = MIB.buildInstr(Opcode)
                           .addDef(Dst0)
                           .addDef(Dst1)
                           .addReg(Acc0)
                           .addReg(Acc1)
                           .addReg(Src0)
                           .addReg(Src1);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  };

  // Select an accumulator-form MAC intrinsic : rtd = rtd OP product.
  // Operand layout: def DstReg, intrinsic_id, acc, src1, src2.
  // The spec silicon (slot 1) reads rtd as the accumulator input
  // (haydn_instruction_db.json: slots.1.DR_Read_Port = [rsd1, rsd2, rtd]).
  // Every accumulator-form MAC def uses FmtALU64Acc with a tied-def
  // constraint `let Constraints = "$rd = $rd_in"`. The tied operand
  // forces regalloc to coalesce Acc and DstReg to the same physical
  // register, so silicon's rtd read (from the encoded destination field)
  // sees the accumulator value.
  // Implementation: emit a SINGLE MCInst whose 4 operands are
  // def DstReg, %rd_in=Acc (tied to DstReg), %rs1=Src0, %rs2=Src1.
  // MIR SSA is preserved because DstReg and Acc are distinct vregs joined
  // by the two-address constraint. The post-RA two-address coalescer
  // honors the tie and assigns both to the same physical register.
  // No OR64 seed, no implicit-use operand — those were the
  // pattern that silently miscomputed the accumulator (codex §1A).
  // All operands are DR64 (— DR64 is a separate bank).
  auto selectAccMAC = [&](unsigned Opcode) {
    Register Acc = I.getOperand(2).getReg();
    Register Src0 = I.getOperand(3).getReg();
    Register Src1 = I.getOperand(4).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (Acc.isVirtual())
      RBI.constrainGenericRegister(Acc, DR64RegClass, MRI);
    if (Src0.isVirtual())
      RBI.constrainGenericRegister(Src0, DR64RegClass, MRI);
    if (Src1.isVirtual())
      RBI.constrainGenericRegister(Src1, DR64RegClass, MRI);
    // Emit the tied-def MCInst: %rd = MAC %rd_in(tied to %rd), %rs1, %rs2.
    MachineInstr *MI = MIB.buildInstr(Opcode)
                           .addDef(DstReg)
                           .addReg(Acc)
                           .addReg(Src0)
                           .addReg(Src1);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  };

  // Select an F2MULAA32RS_* / F2MULAA32R_* dual-product accumulator MAC
  // folding to the binary zero-accumulator form (F2MULZAA*) when the
  // accumulator operand (intrinsic arg 2, MIR operand index 2) is a constant
  // zero. The ZAA form (`rtd = 0 + p0 + p1`) drops the redundant accumulator
  // read port; the AA form (`rtd = rtd + p0 + p1`) keeps it. The fold must run
  // here in the selector, where the acc operand is still a G_CONSTANT — by
  // PostSelectOptimize the zero has already been materialized into a DR64
  // stack-spill chain that getTargetConstant cannot see through.
  // \param AccOpcode the AA opcode (e.g. F2MULAA32RS_HHLL)
  // \param ZaaOpcode the matching binary ZAA opcode (e.g. F2MULZAA32RS_HHLL)
  auto selectF2MULAAOrZAA = [&](unsigned AccOpcode, unsigned ZaaOpcode) {
    Register AccReg = I.getOperand(2).getReg();
    if (auto C = getIConstantVRegValWithLookThrough(AccReg, MRI);
        C && C->Value.isZero()) {
      // Binary form: rtd = 0 + src0*src1_lanes. 2 source operands, no tied acc.
      Register Src0 = I.getOperand(3).getReg();
      Register Src1 = I.getOperand(4).getReg();
      if (DstReg.isVirtual())
        RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
      if (Src0.isVirtual())
        RBI.constrainGenericRegister(Src0, DR64RegClass, MRI);
      if (Src1.isVirtual())
        RBI.constrainGenericRegister(Src1, DR64RegClass, MRI);
      MachineInstr *MI = MIB.buildInstr(ZaaOpcode)
                             .addDef(DstReg)
                             .addReg(Src0)
                             .addReg(Src1);
      if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
        return false;
      I.eraseFromParent();
      return true;
    }
    return selectAccMAC(AccOpcode);
  };

  switch (IntrID) {
  default:
    break;

  // T-ABI10: @llvm.threadlocal.address(p) must not silently lower to p
  // (the SDAG identity in SelectionDAGBuilder.cpp:8176). On baremetal
  // Haydn the operand is a TLS template address, not this thread's
  // instance, so identity is wrong code. No TLS model exists — fail
  // closed with a named diagnostic.
  case threadlocal_address:
    reportGISelFailure(*I.getMF(), *MORE, "HaydnInstructionSelector",
                       "llvm.threadlocal.address: baremetal Haydn has no "
                       "TLS model (T-ABI10)",
                       I);
    return false;

  // CB-128: @llvm.returnaddress — depth 0 → LR ($r15); depth >0 unsupported → 0.
  case returnaddress: {
    // Operand layout: def Dst, intrinsic_id, depth (imm or G_CONSTANT vreg).
    if (!DstReg)
      return false;
    RBI.constrainGenericRegister(DstReg, Haydn::GPR32RegClass, MRI);
    unsigned Depth = 0;
    if (I.getNumOperands() >= 3) {
      const MachineOperand &DepthMO = I.getOperand(2);
      if (DepthMO.isImm())
        Depth = static_cast<unsigned>(DepthMO.getImm());
      else if (DepthMO.isCImm())
        Depth = static_cast<unsigned>(DepthMO.getCImm()->getZExtValue());
      else if (DepthMO.isReg()) {
        // Fold trivial G_CONSTANT depth.
        if (MachineInstr *Def = MRI.getVRegDef(DepthMO.getReg())) {
          if (Def->getOpcode() == TargetOpcode::G_CONSTANT &&
              Def->getOperand(1).isCImm())
            Depth =
                static_cast<unsigned>(Def->getOperand(1).getCImm()->getZExtValue());
          else
            Depth = 1; // unknown non-zero → null
        }
      }
    }
    if (Depth == 0) {
      // COPY from LR (R15). buildCopy + constrain.
      MachineInstr *Copy = MIB.buildCopy(DstReg, Register(Haydn::R15));
      if (!constrainSelectedInstRegOperands(*Copy, TII, TRI, RBI))
        return false;
    } else {
      MachineInstr *Z =
          MIB.buildInstr(Haydn::LOADI32).addDef(DstReg).addImm(0);
      if (!constrainSelectedInstRegOperands(*Z, TII, TRI, RBI))
        return false;
    }
    I.eraseFromParent();
    return true;
  }

  // llvm.frameaddress: depth 0 is the current FP (R14). setFrameAddressIsTaken
  // forces hasFP so PEI materializes FP = incoming SP. Depth > 0 would walk a
  // saved-FP chain; Haydn stores no such record (FP is CFA, not a linked
  // frame). Peer: AArch64InstructionSelector.cpp:6693 walks LDR; RISCV
  // lowerFRAMEADDR walks -2*XLen. Neither layout exists here — fail closed.
  case frameaddress: {
    if (!DstReg)
      return false;
    unsigned Depth = 0;
    if (I.getNumOperands() >= 3) {
      const MachineOperand &DepthMO = I.getOperand(2);
      if (DepthMO.isImm())
        Depth = static_cast<unsigned>(DepthMO.getImm());
      else if (DepthMO.isCImm())
        Depth = static_cast<unsigned>(DepthMO.getCImm()->getZExtValue());
      else if (DepthMO.isReg()) {
        if (MachineInstr *Def = MRI.getVRegDef(DepthMO.getReg())) {
          if (Def->getOpcode() == TargetOpcode::G_CONSTANT &&
              Def->getOperand(1).isCImm())
            Depth = static_cast<unsigned>(
                Def->getOperand(1).getCImm()->getZExtValue());
          else
            Depth = 1;
        }
      }
    }
    if (Depth != 0) {
      reportGISelFailure(*I.getMF(), *MORE, "HaydnInstructionSelector",
                         "llvm.frameaddress depth>0: no walkable frame chain",
                         I);
      return false;
    }
    MF.getFrameInfo().setFrameAddressIsTaken(true);
    Register FP = TRI.getFrameRegister(MF);
    RBI.constrainGenericRegister(DstReg, Haydn::GPR32RegClass, MRI);
    MachineInstr *Copy = MIB.buildCopy(DstReg, FP);
    if (!constrainSelectedInstRegOperands(*Copy, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===-----------------------------------------------------------------===
  // MUL64_LL (32x32->64) — fundamental DSP multiply
  // i32 x i32 -> i64: sign-extend both i32 operands to DR64, then MUL64_LL.
  //===-----------------------------------------------------------------===
  case haydn_mul64_ll: {
    // 32x32->64 widening multiply from GPR32 operands. Mirrors the
    // plain-IR `i64 = sext(i32) * sext(i32)` legalizer result : emit the
    // native SEXT_GPR32_TO_DR64 (the `sext32t64` instruction) on each
    // i32 source, then MUL64_LL. Replaces the prior 7-op LOADI32(31) +
    // SRA32(x,31) (sign mask) + MOV_GPR_TO_DR64(x, sign_mask) chain per
    // operand — the sext-fold (tryFoldSextMovToDirect) couldn't fire because
    // the operands were (A, SignA), not the (A,A) replicate shape.
    //
    // Operands: def DstReg(DR64), intrinsic_id, a(GPR32), b(GPR32)
    Register A = I.getOperand(2).getReg();
    Register B = I.getOperand(3).getReg();

    // Constrain i32 inputs to GPR32
    if (A.isVirtual())
      RBI.constrainGenericRegister(A, GPR32RegClass, MRI);
    if (B.isVirtual())
      RBI.constrainGenericRegister(B, GPR32RegClass, MRI);

    // Sign-extend i32 A and B to DR64 via the native sext32t64 instruction.
    Register AExt = MRI.createVirtualRegister(&DR64RegClass);
    Register BExt = MRI.createVirtualRegister(&DR64RegClass);
    MachineInstr *SextA =
        MIB.buildInstr(SEXT_GPR32_TO_DR64).addDef(AExt).addReg(A);
    if (!constrainSelectedInstRegOperands(*SextA, TII, TRI, RBI))
      return false;
    MachineInstr *SextB =
        MIB.buildInstr(SEXT_GPR32_TO_DR64).addDef(BExt).addReg(B);
    if (!constrainSelectedInstRegOperands(*SextB, TII, TRI, RBI))
      return false;

    // MUL64_LL DstReg, AExt, BExt
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    MachineInstr *MulMI =
        MIB.buildInstr(MUL64_LL).addDef(DstReg).addReg(AExt).addReg(BExt);
    if (!constrainSelectedInstRegOperands(*MulMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===-----------------------------------------------------------------===
  // MUL64 — 16 binary DR64 variants
  //===-----------------------------------------------------------------===
  case haydn_mul64_ss_ll:  return selectBinary(MUL64_LL,   DR64RegClass);
  case haydn_mul64_ss_lh:  return selectBinary(MUL64_LH,   DR64RegClass);
  case haydn_mul64_ss_hl:  return selectBinary(MUL64_HL,   DR64RegClass);
  case haydn_mul64_ss_hh:  return selectBinary(MUL64_HH,   DR64RegClass);
  case haydn_mul64_su_lul: return selectBinary(MUL64_LUL,  DR64RegClass);
  case haydn_mul64_su_ulh: return selectBinary(MUL64_ULH,  DR64RegClass);
  case haydn_mul64_su_uhl: return selectBinary(MUL64_UHL,  DR64RegClass);
  case haydn_mul64_su_uhh: return selectBinary(MUL64_UHH,  DR64RegClass);
  case haydn_mul64_us_luh: return selectBinary(MUL64_LUH,  DR64RegClass);
  case haydn_mul64_us_uhuh:return selectBinary(MUL64_UHUH, DR64RegClass);
  case haydn_mul64_us_hul: return selectBinary(MUL64_HUL,  DR64RegClass);
  case haydn_mul64_us_uhul:return selectBinary(MUL64_UHUL, DR64RegClass);
  case haydn_mul64_uu_uluh:return selectBinary(MUL64_ULUH, DR64RegClass);
  case haydn_mul64_uu_ulul:return selectBinary(MUL64_ULUL, DR64RegClass);
  case haydn_mul64_uu_ull: return selectBinary(MUL64_ULL,  DR64RegClass);
  case haydn_mul64_uu_ulh: return selectBinary(MUL64_ULH,  DR64RegClass);

  //===-----------------------------------------------------------------===
  // MULA64 — accumulator-form MACs (3-arg) for _ss_ variants;
  // 2-arg binary for the remaining 12 (_su_/_us_/_uu_) variants.
  //===-----------------------------------------------------------------===
  case haydn_mula64_ss_ll:  return selectAccMAC(MULA64_LL);
  case haydn_mula64_ss_lh:  return selectAccMAC(MULA64_LH);
  case haydn_mula64_ss_hl:  return selectAccMAC(MULA64_HL);
  case haydn_mula64_ss_hh:  return selectAccMAC(MULA64_HH);
  case haydn_mula64_su_lul: return selectAccMAC(MULA64_LUL);
  case haydn_mula64_su_ulh: return selectAccMAC(MULA64_ULH);
  case haydn_mula64_su_uhl: return selectAccMAC(MULA64_UHL);
  case haydn_mula64_su_uhh: return selectAccMAC(MULA64_UHH);
  case haydn_mula64_us_luh: return selectAccMAC(MULA64_LUH);
  case haydn_mula64_us_uhuh: return selectAccMAC(MULA64_UHUH);
  case haydn_mula64_us_hul: return selectAccMAC(MULA64_HUL);
  case haydn_mula64_us_uhul: return selectAccMAC(MULA64_UHUL);
  case haydn_mula64_uu_uluh: return selectAccMAC(MULA64_ULUH);
  case haydn_mula64_uu_ulul: return selectAccMAC(MULA64_ULUL);
  case haydn_mula64_uu_ull: return selectAccMAC(MULA64_ULL);
  case haydn_mula64_uu_ulh: return selectAccMAC(MULA64_ULH);

  //===-----------------------------------------------------------------===
  // MULS64 — accumulator-form MACs (3-arg) for _ss_ variants;
  // 2-arg binary for the remaining 12 (_su_/_us_/_uu_) variants.
  //===-----------------------------------------------------------------===
  case haydn_muls64_ss_ll:  return selectAccMAC(MULS64_LL);
  case haydn_muls64_ss_lh:  return selectAccMAC(MULS64_LH);
  case haydn_muls64_ss_hl:  return selectAccMAC(MULS64_HL);
  case haydn_muls64_ss_hh:  return selectAccMAC(MULS64_HH);
  case haydn_muls64_su_lul: return selectAccMAC(MULS64_LUL);
  case haydn_muls64_su_ulh: return selectAccMAC(MULS64_ULH);
  case haydn_muls64_su_uhl: return selectAccMAC(MULS64_UHL);
  case haydn_muls64_su_uhh: return selectAccMAC(MULS64_UHH);
  case haydn_muls64_us_luh: return selectAccMAC(MULS64_LUH);
  case haydn_muls64_us_uhuh: return selectAccMAC(MULS64_UHUH);
  case haydn_muls64_us_hul: return selectAccMAC(MULS64_HUL);
  case haydn_muls64_us_uhul: return selectAccMAC(MULS64_UHUL);
  case haydn_muls64_uu_uluh: return selectAccMAC(MULS64_ULUH);
  case haydn_muls64_uu_ulul: return selectAccMAC(MULS64_ULUL);
  case haydn_muls64_uu_ull: return selectAccMAC(MULS64_ULL);
  case haydn_muls64_uu_ulh: return selectAccMAC(MULS64_ULH);

  //===-----------------------------------------------------------------===
  // MULAS64 — 16 binary DR64 variants
  //===-----------------------------------------------------------------===
  case haydn_mulas64_ss_ll:  return selectAccMAC(MULAS64_LL);
  case haydn_mulas64_ss_lh:  return selectAccMAC(MULAS64_LH);
  case haydn_mulas64_ss_hl:  return selectAccMAC(MULAS64_HL);
  case haydn_mulas64_ss_hh:  return selectAccMAC(MULAS64_HH);
  case haydn_mulas64_su_lul: return selectAccMAC(MULAS64_LUL);
  case haydn_mulas64_su_ulh: return selectAccMAC(MULAS64_ULH);
  case haydn_mulas64_su_uhl: return selectAccMAC(MULAS64_UHL);
  case haydn_mulas64_su_uhh: return selectAccMAC(MULAS64_UHH);
  case haydn_mulas64_us_luh: return selectAccMAC(MULAS64_LUH);
  case haydn_mulas64_us_uhuh: return selectAccMAC(MULAS64_UHUH);
  case haydn_mulas64_us_hul: return selectAccMAC(MULAS64_HUL);
  case haydn_mulas64_us_uhul: return selectAccMAC(MULAS64_UHUL);
  case haydn_mulas64_uu_uluh: return selectAccMAC(MULAS64_ULUH);
  case haydn_mulas64_uu_ulul: return selectAccMAC(MULAS64_ULUL);
  case haydn_mulas64_uu_ull: return selectAccMAC(MULAS64_ULL);
  case haydn_mulas64_uu_ulh: return selectAccMAC(MULAS64_ULH);

  //===-----------------------------------------------------------------===
  // MULSS64 — 16 binary DR64 variants
  //===-----------------------------------------------------------------===
  case haydn_mulss64_ss_ll:  return selectAccMAC(MULSS64_LL);
  case haydn_mulss64_ss_lh:  return selectAccMAC(MULSS64_LH);
  case haydn_mulss64_ss_hl:  return selectAccMAC(MULSS64_HL);
  case haydn_mulss64_ss_hh:  return selectAccMAC(MULSS64_HH);
  case haydn_mulss64_su_lul: return selectAccMAC(MULSS64_LUL);
  case haydn_mulss64_su_ulh: return selectAccMAC(MULSS64_ULH);
  case haydn_mulss64_su_uhl: return selectAccMAC(MULSS64_UHL);
  case haydn_mulss64_su_uhh: return selectAccMAC(MULSS64_UHH);
  case haydn_mulss64_us_luh: return selectAccMAC(MULSS64_LUH);
  case haydn_mulss64_us_uhuh: return selectAccMAC(MULSS64_UHUH);
  case haydn_mulss64_us_hul: return selectAccMAC(MULSS64_HUL);
  case haydn_mulss64_us_uhul: return selectAccMAC(MULSS64_UHUL);
  case haydn_mulss64_uu_uluh: return selectAccMAC(MULSS64_ULUH);
  case haydn_mulss64_uu_ulul: return selectAccMAC(MULSS64_ULUL);
  case haydn_mulss64_uu_ull: return selectAccMAC(MULSS64_ULL);
  case haydn_mulss64_uu_ulh: return selectAccMAC(MULSS64_ULH);


  //===-----------------------------------------------------------------===
  // Phase 3A : 97 accumulator MAC intrinsics now wired via
  // selectAccMAC. These read rtd as accumulator (DB behavior); selected as
  // tied-def FmtALU64Acc. Covers the 79 -migrated instructions + 18
  // already-correct FmtALU64Acc + the f2mula/fmulss/mulaa32/mulas32/mul16aq
  // families.
  //===-----------------------------------------------------------------===
  case haydn_f2mulaa32r_hhll: return selectF2MULAAOrZAA(F2MULAA32R_HHLL, F2MULZAA32R_HHLL);
  case haydn_f2mulaa32r_hllh: return selectF2MULAAOrZAA(F2MULAA32R_HLLH, F2MULZAA32R_HLLH);
  case haydn_f2mulss32r_hhll: return selectAccMAC(F2MULSS32R_HHLL);
  case haydn_f2mulss32r_hllh: return selectAccMAC(F2MULSS32R_HLLH);
  case haydn_fmula16_hs00: return selectAccMAC(FMULA16_HS00);
  case haydn_fmula16_hs01: return selectAccMAC(FMULA16_HS01);
  case haydn_fmula16_hs02: return selectAccMAC(FMULA16_HS02);
  case haydn_fmula16_hs03: return selectAccMAC(FMULA16_HS03);
  case haydn_fmula16_hs11: return selectAccMAC(FMULA16_HS11);
  case haydn_fmula16_hs12: return selectAccMAC(FMULA16_HS12);
  case haydn_fmula16_hs13: return selectAccMAC(FMULA16_HS13);
  case haydn_fmula16_hs22: return selectAccMAC(FMULA16_HS22);
  case haydn_fmula16_hs23: return selectAccMAC(FMULA16_HS23);
  case haydn_fmula16_hs33: return selectAccMAC(FMULA16_HS33);
  case haydn_fmula16_ls00: return selectAccMAC(FMULA16_LS00);
  case haydn_fmula16_ls01: return selectAccMAC(FMULA16_LS01);
  case haydn_fmula16_ls02: return selectAccMAC(FMULA16_LS02);
  case haydn_fmula16_ls03: return selectAccMAC(FMULA16_LS03);
  case haydn_fmula16_ls11: return selectAccMAC(FMULA16_LS11);
  case haydn_fmula16_ls12: return selectAccMAC(FMULA16_LS12);
  case haydn_fmula16_ls13: return selectAccMAC(FMULA16_LS13);
  case haydn_fmula16_ls22: return selectAccMAC(FMULA16_LS22);
  case haydn_fmula16_ls23: return selectAccMAC(FMULA16_LS23);
  case haydn_fmula16_ls33: return selectAccMAC(FMULA16_LS33);
  case haydn_fmulaa32s_hhll: return selectAccMAC(FMULAA32S_HHLL);
  case haydn_fmulaa32s_hllh: return selectAccMAC(FMULAA32S_HLLH);
  case haydn_fmulss32s_hhll: return selectAccMAC(FMULSS32S_HHLL);
  case haydn_fmulss32s_hllh: return selectAccMAC(FMULSS32S_HLLH);
  case haydn_mul16aq: return selectAccMAC(MUL16AQ);
  case haydn_mula64_hh: return selectAccMAC(MULA64_HH);
  case haydn_mula64_hl: return selectAccMAC(MULA64_HL);
  case haydn_mula64_huh: return selectAccMAC(MULA64_HUH);
  case haydn_mula64_hul: return selectAccMAC(MULA64_HUL);
  case haydn_mula64_lh: return selectAccMAC(MULA64_LH);
  case haydn_mula64_ll: return selectAccMAC(MULA64_LL);
  case haydn_mula64_luh: return selectAccMAC(MULA64_LUH);
  case haydn_mula64_lul: return selectAccMAC(MULA64_LUL);
  case haydn_mula64_uhh: return selectAccMAC(MULA64_UHH);
  case haydn_mula64_uhl: return selectAccMAC(MULA64_UHL);
  case haydn_mula64_uhuh: return selectAccMAC(MULA64_UHUH);
  case haydn_mula64_uhul: return selectAccMAC(MULA64_UHUL);
  case haydn_mula64_ulh: return selectAccMAC(MULA64_ULH);
  case haydn_mula64_ull: return selectAccMAC(MULA64_ULL);
  case haydn_mula64_uluh: return selectAccMAC(MULA64_ULUH);
  case haydn_mula64_ulul: return selectAccMAC(MULA64_ULUL);
  case haydn_mulaa32_hhll: return selectAccMAC(MULAA32_HHLL);
  case haydn_mulaa32_hllh: return selectAccMAC(MULAA32_HLLH);
  case haydn_mulas32_hhll: return selectAccMAC(MULAS32_HHLL);
  case haydn_mulas32_hllh: return selectAccMAC(MULAS32_HLLH);
  case haydn_mulas64_hh: return selectAccMAC(MULAS64_HH);
  case haydn_mulas64_hl: return selectAccMAC(MULAS64_HL);
  case haydn_mulas64_huh: return selectAccMAC(MULAS64_HUH);
  case haydn_mulas64_hul: return selectAccMAC(MULAS64_HUL);
  case haydn_mulas64_lh: return selectAccMAC(MULAS64_LH);
  case haydn_mulas64_ll: return selectAccMAC(MULAS64_LL);
  case haydn_mulas64_luh: return selectAccMAC(MULAS64_LUH);
  case haydn_mulas64_lul: return selectAccMAC(MULAS64_LUL);
  case haydn_mulas64_uhh: return selectAccMAC(MULAS64_UHH);
  case haydn_mulas64_uhl: return selectAccMAC(MULAS64_UHL);
  case haydn_mulas64_uhuh: return selectAccMAC(MULAS64_UHUH);
  case haydn_mulas64_uhul: return selectAccMAC(MULAS64_UHUL);
  case haydn_mulas64_ulh: return selectAccMAC(MULAS64_ULH);
  case haydn_mulas64_ull: return selectAccMAC(MULAS64_ULL);
  case haydn_mulas64_uluh: return selectAccMAC(MULAS64_ULUH);
  case haydn_mulas64_ulul: return selectAccMAC(MULAS64_ULUL);
  case haydn_muls64_hh: return selectAccMAC(MULS64_HH);
  case haydn_muls64_hl: return selectAccMAC(MULS64_HL);
  case haydn_muls64_huh: return selectAccMAC(MULS64_HUH);
  case haydn_muls64_hul: return selectAccMAC(MULS64_HUL);
  case haydn_muls64_lh: return selectAccMAC(MULS64_LH);
  case haydn_muls64_ll: return selectAccMAC(MULS64_LL);
  case haydn_muls64_luh: return selectAccMAC(MULS64_LUH);
  case haydn_muls64_lul: return selectAccMAC(MULS64_LUL);
  case haydn_muls64_uhh: return selectAccMAC(MULS64_UHH);
  case haydn_muls64_uhl: return selectAccMAC(MULS64_UHL);
  case haydn_muls64_uhuh: return selectAccMAC(MULS64_UHUH);
  case haydn_muls64_uhul: return selectAccMAC(MULS64_UHUL);
  case haydn_muls64_ulh: return selectAccMAC(MULS64_ULH);
  case haydn_muls64_ull: return selectAccMAC(MULS64_ULL);
  case haydn_muls64_uluh: return selectAccMAC(MULS64_ULUH);
  case haydn_muls64_ulul: return selectAccMAC(MULS64_ULUL);
  case haydn_mulss64_hh: return selectAccMAC(MULSS64_HH);
  case haydn_mulss64_hl: return selectAccMAC(MULSS64_HL);
  case haydn_mulss64_huh: return selectAccMAC(MULSS64_HUH);
  case haydn_mulss64_hul: return selectAccMAC(MULSS64_HUL);
  case haydn_mulss64_lh: return selectAccMAC(MULSS64_LH);
  case haydn_mulss64_ll: return selectAccMAC(MULSS64_LL);
  case haydn_mulss64_luh: return selectAccMAC(MULSS64_LUH);
  case haydn_mulss64_lul: return selectAccMAC(MULSS64_LUL);
  case haydn_mulss64_uhh: return selectAccMAC(MULSS64_UHH);
  case haydn_mulss64_uhl: return selectAccMAC(MULSS64_UHL);
  case haydn_mulss64_uhuh: return selectAccMAC(MULSS64_UHUH);
  case haydn_mulss64_uhul: return selectAccMAC(MULSS64_UHUL);
  case haydn_mulss64_ulh: return selectAccMAC(MULSS64_ULH);
  case haydn_mulss64_ull: return selectAccMAC(MULSS64_ULL);
  case haydn_mulss64_uluh: return selectAccMAC(MULSS64_ULUH);
  case haydn_mulss64_ulul: return selectAccMAC(MULSS64_ULUL);
  //===-----------------------------------------------------------------===
  // Saturating arithmetic (32-bit GPR32 binary)
  //===-----------------------------------------------------------------===
  case haydn_add32s: return selectBinary(ADD32S, GPR32RegClass);
  case haydn_sub32s: return selectBinary(SUB32S, GPR32RegClass);

  // Golden slot0 scalar-32 shifts: RR (reg amount) and RI5 (imm amount).
  case haydn_sll32:  return selectBinary(SLL32,  GPR32RegClass);
  case haydn_srl32:  return selectBinary(SRL32,  GPR32RegClass);
  case haydn_sra32:  return selectBinary(SRA32,  GPR32RegClass);
  case haydn_sra32r: return selectBinary(SRA32R, GPR32RegClass);
  case haydn_slli32:
  case haydn_srli32:
  case haydn_srai32:
  case haydn_srai32r: {
    Register Src = I.getOperand(2).getReg();
 // ImmArg amount may be bare Imm after legalize.
    int64_t ShiftVal = 0;
    if (!getConstOpSExt(I.getOperand(3), ShiftVal)) {
      LLVM_DEBUG(dbgs() << "GPR32 imm shift: amount must be a constant\n");
      return false;
    }
    // Generated members: SLLI32/SRLI32/SRAI32/SRAI32R RI5 (uimm5).
    if (!expectUImm(ShiftVal, 5, "GPR32 imm shift"))
      return false;
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, GPR32RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, GPR32RegClass, MRI);
    unsigned Opc = SLLI32;
    switch (IntrID) {
    case Intrinsic::haydn_slli32:  Opc = SLLI32;  break;
    case Intrinsic::haydn_srli32:  Opc = SRLI32;  break;
    case Intrinsic::haydn_srai32:  Opc = SRAI32;  break;
    case Intrinsic::haydn_srai32r: Opc = SRAI32R; break;
    default:
      llvm_unreachable("unexpected GPR32 imm shift");
    }
    MachineInstr *MI =
        MIB.buildInstr(Opc).addDef(DstReg).addReg(Src).addImm(ShiftVal);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===-----------------------------------------------------------------===
  // BREV32 — Bit-reversed add for FFT butterfly addressing.
  // rt = bitreverse(bitreverse(rs1) + rs2). Maps 1:1 to NatureDSP
  // AE_ADDBRBA32. Operates on GPR32 operands directly.
  //===-----------------------------------------------------------------===
  case haydn_addbrba32: return selectBinary(BREV32, GPR32RegClass);

  // ABS32S is a binary instruction format but unary intrinsic.
  // The instruction takes (outs GPR32:$rd), (ins GPR32:$rs1, GPR32:$rs2)
  // but the intrinsic is unary. Duplicate the source register.
  case haydn_abs32s: {
    Register SrcReg = I.getOperand(2).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, GPR32RegClass, MRI);
    if (SrcReg.isVirtual())
      RBI.constrainGenericRegister(SrcReg, GPR32RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(ABS32S)
                           .addDef(DstReg)
                           .addReg(SrcReg)
                           .addReg(SrcReg);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case haydn_neg32s: return selectUnary(NEG32S, GPR32RegClass);

  //===-----------------------------------------------------------------===
  // Saturating arithmetic (64-bit DR64 binary)
  //===-----------------------------------------------------------------===
  case haydn_add64s: return selectBinary(ADD64S, DR64RegClass);

  // Golden D-ALU coverage (ADD64/SUB64/XOR64/MOVE64/… — not invent)
  case haydn_add64: return selectBinary(ADD64, DR64RegClass);
  case haydn_sub64: return selectBinary(SUB64, DR64RegClass);
  case haydn_xor64: return selectBinary(XOR64, DR64RegClass);
  case haydn_add64s_h: return selectBinary(ADD64S_H, DR64RegClass);
  case haydn_add64s_l: return selectBinary(ADD64S_L, DR64RegClass);
  case haydn_sub64s_h: return selectBinary(SUB64S_H, DR64RegClass);
  case haydn_sub64s_l: return selectBinary(SUB64S_L, DR64RegClass);
  case haydn_move64: return selectUnary(MOVE64, DR64RegClass);
  case haydn_transf64_h: return selectUnary(TRANSF64_H, DR64RegClass);
  case haydn_transf64_l: return selectUnary(TRANSF64_L, DR64RegClass);
  case haydn_transf64:
  case haydn_transf64f2: {
    Register Src = I.getOperand(2).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, GPR32RegClass, MRI);
    unsigned Opc =
        (IntrID == Intrinsic::haydn_transf64f2) ? TRANSF64F2 : TRANSF64;
    MachineInstr *MI = MIB.buildInstr(Opc).addDef(DstReg).addReg(Src);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_sext32t64: {
    // Auto-gen SEXT32T64 stub is (outs GPR32) and must not be selected —
    // that yields a post-RA cross-bank COPY D←R. Real opcode is
    // SEXT_GPR32_TO_DR64 (outs DR64, ins GPR32); Format E logical peels to
    // SEXT32T64.
    Register Src = I.getOperand(2).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, GPR32RegClass, MRI);
    MachineInstr *MI =
        MIB.buildInstr(SEXT_GPR32_TO_DR64).addDef(DstReg).addReg(Src);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_zero_dr: {
    // Logical ZERO_DR is isCodeGenOnly with the member dag (outs DR, no
    // imm). Post-RA LogicalMaterialize commits ZERO_DR_S1/S2. Do not emit
    // _S* here (D493: pre-RA logical opcodes only).
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(ZERO_DR).addDef(DstReg);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_sin_cos: {
    Register Src = I.getOperand(2).getReg();
    int64_t ImmVal = 0;
    if (!getConstOpSExt(I.getOperand(3), ImmVal))
      return false;
    // Golden: SIN_COS imm is uimm4 (RI4_DG member), not the td stub simm16.
    if (!expectUImm(ImmVal, 4, "SIN_COS"))
      return false;
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, GPR32RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(SIN_COS)
                           .addDef(DstReg)
                           .addReg(Src)
                           .addImm(ImmVal);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_movei_h:
  case haydn_movei_l: {
    // Select logical MOVEI_H/L; post-RA setDesc commits MOVEI_*_S0.
    // ImmArg bare Imm after legalize.
    int64_t ImmVal = 0;
    if (!getConstOpSExt(I.getOperand(2), ImmVal))
      return false;
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    unsigned Opc =
        (IntrID == Intrinsic::haydn_movei_h) ? MOVEI_H : MOVEI_L;
    MachineInstr *MI = MIB.buildInstr(Opc).addDef(DstReg).addImm(ImmVal);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_sub64s: return selectBinary(SUB64S, DR64RegClass);

  //===-----------------------------------------------------------------===
  // Saturating absolute/negate (64-bit DR64 unary)
  //===-----------------------------------------------------------------===
  case haydn_abs64s: return selectUnary(ABS64S, DR64RegClass);
  case haydn_neg64s: return selectUnary(NEG64S, DR64RegClass);

  //===-----------------------------------------------------------------===
  // Non-saturating absolute/negate (64-bit DR64 unary)
  //===-----------------------------------------------------------------===
  case haydn_abs64:  return selectUnary(ABS64, DR64RegClass);
  case haydn_neg64:  return selectUnary(NEG64, DR64RegClass);

  //===-----------------------------------------------------------------===
  // Fractional multiply (FMUL32S) — binary DR64
  //===-----------------------------------------------------------------===
  case haydn_fmul32s_ll: return selectBinary(FMUL32S_LL, DR64RegClass);
  case haydn_fmul32s_lh: return selectBinary(FMUL32S_LH, DR64RegClass);
  case haydn_fmul32s_hh: return selectBinary(FMUL32S_HH, DR64RegClass);

  //===-----------------------------------------------------------------===
  // Fractional multiply-accumulate (FMULA32S) — accumulator-form
  //===-----------------------------------------------------------------===
  case haydn_fmula32s_ll: return selectAccMAC(FMULA32S_LL);
  case haydn_fmula32s_lh: return selectAccMAC(FMULA32S_LH);
  case haydn_fmula32s_hh: return selectAccMAC(FMULA32S_HH);

  //===-----------------------------------------------------------------===
  // Fractional multiply-subtract (FMULS32S) — accumulator-form
  // for _ll/_hh; _lh stays 2-arg (no haydn_dsp.h wrapper yet).
  //===-----------------------------------------------------------------===
  case haydn_fmuls32s_ll: return selectAccMAC(FMULS32S_LL);
  case haydn_fmuls32s_lh: return selectAccMAC(FMULS32S_LH);
  case haydn_fmuls32s_hh: return selectAccMAC(FMULS32S_HH);

  //===-----------------------------------------------------------------===
  // FF2 Fractional Multiply with Symmetric Rounding.
  //
  // The FF2* intrinsic family maps 1:1 to the FF2* SINGLE-PRODUCT instruction
  // family (one product per call) per haydn_instruction_db.json — NOT the
  // dual-product F2MULAA32R_*/F2MULSS32R_*/F2MULAA32RS_*/F2MULSS32RS_* family
  // (which compute TWO lane products and sum them). Routing a single-lane
  // intrinsic to a dual-lane opcode doubled the work and changed the result
  // (the dual form adds an HH*HH + LL*LL sum that the intrinsic does not
  // request). Verified against DB Behaviors + debated with codex.
  // FF2MUL32R_LL : temp = rsd1[31:00] * rsd2[31:00] (one product)
  // FF2MUL32R_LH : temp = rsd1[31:00] * rsd2[63:32] (one product)
  // FF2MUL32R_HH : temp = rsd1[63:32] * rsd2[63:32] (one product)
  // F2MULAA32R_HHLL: temp1 = rsd1[63:32]*rsd2[63:32] + temp0 = rsd1[31:00]*rsd2[31:00] (TWO)
  // Lane mapping: _ll -> _LL, _lh -> _LH, _hh -> _HH (identity).
  //===-----------------------------------------------------------------===
  // FF2MUL32RS — saturating single-product mul with rounding
  case haydn_ff2mul32rs_ll:  return selectBinary(FF2MUL32RS_LL, DR64RegClass);
  case haydn_ff2mul32rs_lh:  return selectBinary(FF2MUL32RS_LH, DR64RegClass);
  case haydn_ff2mul32rs_hh:  return selectBinary(FF2MUL32RS_HH, DR64RegClass);
  // FF2MULA32RS — saturating single-product MAC with rounding (3-arg)
  case haydn_ff2mula32rs_ll: return selectAccMAC(FF2MULA32RS_LL);
  case haydn_ff2mula32rs_lh: return selectAccMAC(FF2MULA32RS_LH);
  case haydn_ff2mula32rs_hh: return selectAccMAC(FF2MULA32RS_HH);
  // FF2MULS32RS — saturating single-product subtract with rounding (3-arg).
  // DB FF2MULS32RS_*: rtd = rtd - single_rounded_product — NOT the dual-lane
  // F2MULSS32RS_HHLL which subtracts (HH*HH + LL*LL). Fixed in (was
  // routed to F2MULSS32RS_*).
  case haydn_ff2muls32rs_ll: return selectAccMAC(FF2MULS32RS_LL);
  case haydn_ff2muls32rs_lh: return selectAccMAC(FF2MULS32RS_LH);
  case haydn_ff2muls32rs_hh: return selectAccMAC(FF2MULS32RS_HH);
  // FF2MUL32R — non-saturating single-product mul with rounding. PURE multiply
  // (DB slot-1 DR_Read_Port = [rsd1, rsd2] — no rtd read), so 2-source
  // selectBinary is correct and stays binary/2-source end-to-end.
  case haydn_ff2mul32r_ll:  return selectBinary(FF2MUL32R_LL, DR64RegClass);
  case haydn_ff2mul32r_lh:  return selectBinary(FF2MUL32R_LH, DR64RegClass);
  case haydn_ff2mul32r_hh:  return selectBinary(FF2MUL32R_HH, DR64RegClass);
  // FF2MULA32R — non-saturating single-product MAC with rounding. DB behavior
  // is `rtd = rtd + single_product` (slot-1 reads rtd as accumulator), so this
  // is a tied-def accumulate. Routed via selectAccMAC, which emits the 4-operand
  // tied-def MI (rd, rd_in, rs1, rs2) into the FmtALU64Acc target opcode
  // (migrated from FmtALU64 in this wave). The intrinsic is ternary (acc, a, b).
  // Closes -followup bug #4 — previously selectBinary dropped the accumulate
  // operand (latent miscompute); selectAccMAC restores correct MAC semantics.
  // See.
  case haydn_ff2mula32r_ll: return selectAccMAC(FF2MULA32R_LL);
  case haydn_ff2mula32r_lh: return selectAccMAC(FF2MULA32R_LH);
  case haydn_ff2mula32r_hh: return selectAccMAC(FF2MULA32R_HH);
  // FF2MULS32R — non-saturating single-product subtract with rounding.
  // DB: rtd = rtd - single_product (reads rtd); tied-def accumulate, same as
  // FF2MULA32R_* above. selectBinary -> selectAccMAC per.
  case haydn_ff2muls32r_ll: return selectAccMAC(FF2MULS32R_LL);
  case haydn_ff2muls32r_lh: return selectAccMAC(FF2MULS32R_LH);
  case haydn_ff2muls32r_hh: return selectAccMAC(FF2MULS32R_HH);

  //===-----------------------------------------------------------------===
  // Cross-Width 32x16 Fractional MAC (MULFP32X16X2RAS) — NATIVE EMULATION.
  //
  // Haydn has NO native 32x16 MAC, but the existing FMULA16_HS/LS family
  // (16x16->32 fractional MAC with lane-select) can emulate it in 3-5 ops
  // ALL in DR64 (no GPR traffic, no stack spills). The previous decomposition
  // unpacked to GPR32 and used scalar mul64_ll+macq31 (~30 ops/call).
  //
  // Strategy: for acc += a32[31:00] * b16[15:00] (fractional):
  // = acc + (a32[15:00]*b16[15:00] + a32[31:16]*b16[15:00]) >> 15
  // Replicate b16 into both 16-bit lanes of a coef-splat, then:
  // FMULA16_HS00 acc, a32, b_splat → acc[63:32] += a32[15:00] * b
  // FMULA16_HS11 acc, a32, b_splat → acc[63:32] += a32[31:16] * b_splat[31:16]
  // (since b_splat[31:16] == b_splat[15:00] == b)
  //
  // For the x2 (dual-lane) variant, we do this for both acc lanes:
  // _low: b lanes 0,1 → acc lanes 0 (LS), 1 (HS)
  // _high: b lanes 2,3 → acc lanes 0 (LS), 1 (HS)
  //===-----------------------------------------------------------------===
  case haydn_mulfp32x16x2ras_low:
  case haydn_mulfp32x16x2ras_high: {
    bool IsHigh = (IntrID == haydn_mulfp32x16x2ras_high);

    // Operands: def DstReg(DR64), intrinsic_id, acc(DR64), a32(DR64), b16(DR64)
    Register AccDR64 = I.getOperand(2).getReg();
    Register A32DR64 = I.getOperand(3).getReg();
    Register B16DR64 = I.getOperand(4).getReg();

    // constrain all to DR64 — the entire emulation stays in DR64.
    for (Register R : {DstReg, AccDR64, A32DR64, B16DR64})
      if (R.isVirtual())
        RBI.constrainGenericRegister(R, Haydn::DR64RegClass, MRI);

    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (AccDR64.isVirtual())
      RBI.constrainGenericRegister(AccDR64, DR64RegClass, MRI);
    if (A32DR64.isVirtual())
      RBI.constrainGenericRegister(A32DR64, DR64RegClass, MRI);
    if (B16DR64.isVirtual())
      RBI.constrainGenericRegister(B16DR64, DR64RegClass, MRI);

    // Native DR64-only emulation using FMULA16 fractional MAC family.
    //
    // For each accumulator lane, we need: acc[lane] += a32[lane] * b16[lane]
    // where a32 is 32-bit and b16 is 16-bit (fractional Q1.15 × Q1.15 per half).
    // Split the 32-bit data into two 16-bit halves and MAC each by the coef.
    //
    // Step 1: Replicate each b16 coef lane into both half-word positions of
    // a temp DR64 so FMULA16_HS00 and HS11 both multiply by the same coef.
    // Use X2SLLI32+X2SRLI32 to broadcast lane 0→both lanes (or lane 2→both).

    // Helper: replicate 16-bit lane N of src into both 16-bit lanes of a
    // 32-bit half-word pair, producing a DR64 where [15:00]==[31:16]==laneN.
    auto ReplicateLane = [&](Register Src, unsigned Lane) -> Register {
      // Shift the desired lane into position [15:00], then replicate to [31:16].
      // Lane 0: already at [15:00]. Lane 1: shift right by 16.
      // Lane 2: shift right by 32 (X2SRLI32). Lane 3: shift right by 48.
      Register Shifted = MRI.createVirtualRegister(&Haydn::DR64RegClass);
      if (Lane == 0) {
        Shifted = Src; // already in position
      } else if (Lane == 1) {
        // Shift right by 16 bits: X2SRLI32 shifts per-32-bit-lane, so we need
        // a scalar shift. Use X2SRAI32 by 16 to bring lane1 to lane0 position
        // with sign extension, then X2SLLI32 by 16 + X2SRAI32 by 16 to replicate.
        Register Tmp = MRI.createVirtualRegister(&Haydn::DR64RegClass);
        MachineInstr *Sra = MIB.buildInstr(Haydn::X2SRAI32)
                                .addDef(Tmp).addReg(Src).addImm(16);
        if (!constrainSelectedInstRegOperands(*Sra, TII, TRI, RBI))
          return Register();
        Register Tmp2 = MRI.createVirtualRegister(&Haydn::DR64RegClass);
        MachineInstr *Sll = MIB.buildInstr(Haydn::X2SLLI32)
                                .addDef(Tmp2).addReg(Tmp).addImm(16);
        if (!constrainSelectedInstRegOperands(*Sll, TII, TRI, RBI))
          return Register();
        MachineInstr *Sra2 = MIB.buildInstr(Haydn::X2SRAI32)
                                 .addDef(Shifted).addReg(Tmp2).addImm(16);
        if (!constrainSelectedInstRegOperands(*Sra2, TII, TRI, RBI))
          return Register();
      } else if (Lane == 2) {
        // Lane 2 is in the high 32-bit half. X2SRLI32 by 32 brings it to low.
        MachineInstr *Srl = MIB.buildInstr(Haydn::X2SRLI32)
                                .addDef(Shifted).addReg(Src).addImm(32);
        if (!constrainSelectedInstRegOperands(*Srl, TII, TRI, RBI))
          return Register();
      } else {
        // Lane 3: X2SRLI32 by 32 to get high half, then X2SRAI32 by 16.
        Register Tmp = MRI.createVirtualRegister(&Haydn::DR64RegClass);
        MachineInstr *Srl = MIB.buildInstr(Haydn::X2SRLI32)
                                .addDef(Tmp).addReg(Src).addImm(32);
        if (!constrainSelectedInstRegOperands(*Srl, TII, TRI, RBI))
          return Register();
        MachineInstr *Sra = MIB.buildInstr(Haydn::X2SRAI32)
                                .addDef(Shifted).addReg(Tmp).addImm(16);
        if (!constrainSelectedInstRegOperands(*Sra, TII, TRI, RBI))
          return Register();
      }
      // Now Shifted has the coef in [15:00]. Replicate to [31:16]:
      // SLLI32 by 16 then SRAI32 by 16 gives {sext(coef[15:00]), sext(coef[15:00])}.
      Register TmpL = MRI.createVirtualRegister(&Haydn::DR64RegClass);
      MachineInstr *Sll = MIB.buildInstr(Haydn::X2SLLI32)
                              .addDef(TmpL).addReg(Shifted).addImm(16);
      if (!constrainSelectedInstRegOperands(*Sll, TII, TRI, RBI))
        return Register();
      Register Result = MRI.createVirtualRegister(&Haydn::DR64RegClass);
      MachineInstr *Sra = MIB.buildInstr(Haydn::X2SRAI32)
                              .addDef(Result).addReg(TmpL).addImm(16);
      if (!constrainSelectedInstRegOperands(*Sra, TII, TRI, RBI))
        return Register();
      return Result;
    };

    // Step 2: For each accumulator lane, do 2× FMULA16 to accumulate the
    // low-half and high-half products of the 32-bit data × replicated coef.
    // Each tied-def MAC creates a new vreg (SSA), chained from the previous.
    auto EmitLaneMAC = [&](Register &Acc, unsigned AccLane,
                           Register CoefSplatted) {
      unsigned Mac0 = (AccLane == 0) ? Haydn::FMULA16_LS00 : Haydn::FMULA16_HS00;
      unsigned Mac1 = (AccLane == 0) ? Haydn::FMULA16_LS11 : Haydn::FMULA16_HS11;
      Register Tmp0 = MRI.createVirtualRegister(&Haydn::DR64RegClass);
      MachineInstr *MI0 = MIB.buildInstr(Mac0)
                               .addDef(Tmp0).addReg(Acc)
                               .addReg(A32DR64).addReg(CoefSplatted);
      if (!constrainSelectedInstRegOperands(*MI0, TII, TRI, RBI))
        return false;
      Register Tmp1 = MRI.createVirtualRegister(&Haydn::DR64RegClass);
      MachineInstr *MI1 = MIB.buildInstr(Mac1)
                               .addDef(Tmp1).addReg(Tmp0)
                               .addReg(A32DR64).addReg(CoefSplatted);
      if (!constrainSelectedInstRegOperands(*MI1, TII, TRI, RBI))
        return false;
      Acc = Tmp1;
      return true;
    };

    // Start from the accumulator input.
    Register CurAcc = AccDR64;

    // Determine which b16 lanes to use.
    unsigned BLane0 = IsHigh ? 2 : 0;
    unsigned BLane1 = IsHigh ? 3 : 1;

    // Replicate coef lanes.
    Register B0Splat = ReplicateLane(B16DR64, BLane0);
    Register B1Splat = ReplicateLane(B16DR64, BLane1);
    if (!B0Splat.isValid() || !B1Splat.isValid())
      return false;

    // Emit MACs: lane 0 (LS) uses b0, lane 1 (HS) uses b1.
    // Chain: CurAcc → LS00 → LS11 → HS00 → HS11 → DstReg
    if (!EmitLaneMAC(CurAcc, 0, B0Splat) || !EmitLaneMAC(CurAcc, 1, B1Splat))
      return false;

    // Copy final result to DstReg.
    MIB.buildCopy(DstReg, CurAcc);

    I.eraseFromParent();
    return true;
  }

  //===-----------------------------------------------------------------===
  // 32-bit multiply high (GPR32 binary)
  //===-----------------------------------------------------------------===
  case haydn_mull:   return selectBinary(MULL,   GPR32RegClass);
  case haydn_mulssh: return selectBinary(MULSSH, GPR32RegClass);
  case haydn_mulsuh: return selectBinary(MULSUH, GPR32RegClass);
  case haydn_muluuh: return selectBinary(MULUUH, GPR32RegClass);

  //===-----------------------------------------------------------------===
  // FIR-specific MAC family — DECOMPOSITION.
  //
  // Haydn has no native dual/quad-output FIR MAC op (see ISA-09). The
  // HiFi3 AE_MUL{,A}FD32X16X2_FIR_HH/HL and AE_MUL{,A}FQ16X2_FIR_3/1
  // intrinsics each lower to a single Haydn building-block MAC step that the
  // kernel caller issues once per output accumulator lane.
  //
  // acc is the in/out DR64 accumulator lane. Because the building-block
  // instructions implicitly read $rd (the spec for FMULA32S_HH reads
  // rtd = SAT(rtd + rs1*rs2), but the tablegen def only models the write)
  // we COPY acc -> DstReg first so the MAC reads the prior accumulator
  // value through DstReg. Init variants (MULFQ16X2_FIR_*) ignore the
  // prior value; their building block (FMUL16_HS*) overwrites rtd, so
  // the COPY is harmless but kept uniform for codegen simplicity.
  //
  // COEFFICIENT-WIDEN CORRECTNESS FIX (A3-conservative, mandatory):
  // The two 32x32 FIR forms (haydn_mulaa32s_fir_{hh,hl}) lower to FMULA32S_HH
  // _LH, which read rsd2[63:32] as a Q1.31 coefficient (DB :6898/:6939). But
  // the NatureDSP compat wrappers pass the coef as a packed ae_int16x4 DR64
  // (4 x Q1.15), so rsd2[63:32] holds two adjacent 16-bit lanes
  // (c16_lane3 | (c16_lane2 << 16)), NOT a sign-extended Q1.31 coef. The MAC
  // would compute a wrong product. Before dispatching to selectAccMAC we
  // sign-extend the intended coef half-word to Q1.31 and repack into a fresh
  // DR64 so rsd2[63:32] holds the correct coef. This is a correctness gate
  // not an optimization — see A3, and the "Latent correctness
  // hazard" block of isa-proposed/MULAFD32X16X2-spec.md.
  //
  // See -fir-mac-family-decomposition.md for the full rationale and
  // ISA-09-fir-simd-mac-gap.md for the requested native instruction.
  //===-----------------------------------------------------------------===
  case haydn_mulaa32s_fir_hh:
  case haydn_mulaa32s_fir_hl:
  case haydn_mulfq16x2_fir_3:
  case haydn_mulfq16x2_fir_1:
  case haydn_mulafq16x2_fir_3:
  case haydn_mulafq16x2_fir_1: {
    // These 6 intrinsics are declared haydn_ternary_intrinsic (acc, a, b).
    // The four *A* (accumulate) forms read rtd via selectAccMAC
    // (FmtALU64Acc). The two non-*A* init forms lower to FMULZAA16_*
    // (dual-product overwrite, FmtALU64 binary) and drop the acc arg.
    unsigned MacOpc;
    bool IsAccumulate;
    bool NeedsCoefWiden;
    switch (IntrID) {
    default:
      llvm_unreachable("impossible FIR-MAC intrinsic ID");
    case haydn_mulaa32s_fir_hh:
      // Renamed from haydn_mulafd32x16x2_fir_hh (A2): the backing op is
      // 32x32 (FMULA32S_HH), not 32x16 as the old name claimed.
      MacOpc = FMULA32S_HH;
      IsAccumulate = true;
      NeedsCoefWiden = true;
      break;
    case haydn_mulaa32s_fir_hl:
      // Renamed from haydn_mulafd32x16x2_fir_hl (A2).
      MacOpc = FMULA32S_LH;
      IsAccumulate = true;
      NeedsCoefWiden = true;
      break;
    case haydn_mulfq16x2_fir_3:
      // Fresh dual-product (lanes 1+0): matches AE_MULFQ / FMULZAA16_HS_11_00
      // semantics (p1+p0 into rtd[63:32], no prior acc add). Previously
      // FMUL16_HS00 (single product) which under-counted vs the accumulate
      // form FMULAA16_HS_11_00 (two products).
      MacOpc = FMULZAA16_HS_11_00;
      IsAccumulate = false;
      NeedsCoefWiden = false;
      break;
    case haydn_mulfq16x2_fir_1:
      // Fresh dual-product (lanes 3+2): FMULZAA16_HS_33_22. Pair of the
      // accumulate form FMULAA16_HS_33_22 used by mulafq16x2_fir_1.
      MacOpc = FMULZAA16_HS_33_22;
      IsAccumulate = false;
      NeedsCoefWiden = false;
      break;
    case haydn_mulafq16x2_fir_3:
      MacOpc = FMULAA16_HS_11_00;
      IsAccumulate = true;
      NeedsCoefWiden = false;
      break;
    case haydn_mulafq16x2_fir_1:
      // Spec/DB: rtd[63:32] = SATQ1.31(rtd[63:32] + p3 + p2). Migrated to
      // FmtALU64Acc (encoding unchanged; tied $rd_in models the acc read).
      MacOpc = FMULAA16_HS_33_22;
      IsAccumulate = true;
      NeedsCoefWiden = false;
      break;
    }
    if (IsAccumulate) {
 // A3-conservative ( SEXT-fold defeat): for the 32x32 FIR
      // forms, sign-extend the packed-Q1.15 coef (operand 4 of the intrinsic
      // call) to Q1.31 and repack into a fresh DR64 so the MAC reads the
      // correct coef in rsd2[63:32]. The intended coef is the upper 16-bit
      // half-word of the upper GPR32 lane (ae_int16x4 lane 3). Sequence:
      // MOV_DR64_TO_GPR lo32, hi32, coef (hi32 = c3 | (c2<<16))
      // SRAI32 coefQ31, hi32, 16 (sext c3 -> Q1.31)
      // MOV_GPR_TO_DR64 coefW, R0, coefQ31 (high=coefQ31, low=R0=0)
      // selectAccMAC then reads operand 4 = coefW as rsd2; rsd2[63:32] =
      // coefQ31 (the correct Q1.31 coefficient), rsd2[31:00]=0 (dead — HH
      // reads rsd1[63:32]*rsd2[63:32]; LH reads rsd1[31:00]*rsd2[63:32]).
      // The low operand MUST be R0, not CoefQ31: R0 != CoefQ31 defeats the
      // Lo==Hi sext fold (tryFoldSextMovToDirect) that would otherwise
      // rewrite this to SEXT_GPR32_TO_DR64 and turn rsd2[63:32] into a sign
      // mask. See,.
      Register Acc = I.getOperand(2).getReg();
      Register Src0 = I.getOperand(3).getReg();
      Register Coef = I.getOperand(4).getReg();
      Register CoefWidened = Coef;
      if (NeedsCoefWiden) {
        Register CoefLo = MRI.createVirtualRegister(&GPR32RegClass);
        Register CoefHi = MRI.createVirtualRegister(&GPR32RegClass);
        MachineInstr *SplitMI = MIB.buildInstr(MOV_DR64_TO_GPR)
                                    .addDef(CoefLo)
                                    .addDef(CoefHi)
                                    .addReg(Coef);
        if (!constrainSelectedInstRegOperands(*SplitMI, TII, TRI, RBI))
          return false;
        // Sign-extend the upper 16-bit half-word of CoefHi (ae_int16x4 lane
        // 3) to a full Q1.31 value.
        Register CoefQ31 = MRI.createVirtualRegister(&GPR32RegClass);
        MachineInstr *SraMI = MIB.buildInstr(SRAI32)
                                   .addDef(CoefQ31)
                                   .addReg(CoefHi)
                                   .addImm(16);
        if (!constrainSelectedInstRegOperands(*SraMI, TII, TRI, RBI))
          return false;
        // Repack as DR64 with the Q1.31 coef in the HIGH half only
        // (rsd2[63:32], the bits FMULA32S_HH/_LH multiply per DB :6898/:6939).
        // The low half is R0 (soft-zero), which is DEAD for both forms:
        // FMULA32S_HH reads rsd1[63:32] * rsd2[63:32]
        // FMULA32S_LH reads rsd1[31:00] * rsd2[63:32] (rsd2[31:00] unused)
        // CRITICAL: the two MOV_GPR_TO_DR64 source operands MUST differ
        // (R0 != CoefQ31). The previous form used CoefQ31 for BOTH halves
        // which matched the sext-shape fold in HaydnPostSelectOptimize
        // (tryFoldSextMovToDirect, Lo==Hi trigger) and rewrote this to
        // SEXT_GPR32_TO_DR64. That instruction's behavior is SEXT32->64
        // (HaydnInstrInfo.td:515) — it replicates bit 31 into the upper half
        // turning rsd2[63:32] into a sign mask (0x00000000 or 0xFFFFFFFF)
        // instead of the coefficient. The MAC then multiplied the sample by
        // 0 or -1, not by the coef (codex-commit-review DEBATE 2, HIGH;
        // hifi-semantics audit row 1 was wrong to call this faithful — it
        // observed the `sext32t64` mnemonic without tracing the bit
        // semantics). R0 != CoefQ31 defeats the Lo==Hi fold trigger so the
        // 2-source merge survives, yielding CoefW[31:0]=0, CoefW[63:32]=CoefQ31.
        // Verified against DB + debated with codex (see).
        CoefWidened = MRI.createVirtualRegister(&DR64RegClass);
        MachineInstr *PackMI = MIB.buildInstr(MOV_GPR_TO_DR64)
                                    .addDef(CoefWidened)
                                    .addReg(Haydn::R0)
                                    .addReg(CoefQ31);
        if (!constrainSelectedInstRegOperands(*PackMI, TII, TRI, RBI))
          return false;
      }
      if (DstReg.isVirtual())
        RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
      if (Acc.isVirtual())
        RBI.constrainGenericRegister(Acc, DR64RegClass, MRI);
      if (Src0.isVirtual())
        RBI.constrainGenericRegister(Src0, DR64RegClass, MRI);
      if (CoefWidened.isVirtual())
        RBI.constrainGenericRegister(CoefWidened, DR64RegClass, MRI);
      // Emit the tied-def MCInst: %rd = MAC %rd_in(tied to %rd), %rs1, %rs2.
      MachineInstr *MI = MIB.buildInstr(MacOpc)
                             .addDef(DstReg)
                             .addReg(Acc)
                             .addReg(Src0)
                             .addReg(CoefWidened);
      if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
        return false;
      I.eraseFromParent();
      return true;
    }
    // Non-accumulate form: spec opcode takes (rsd1, rsd2) only. The
    // intrinsic's acc arg (operand 2) is dropped — it does not affect
    // silicon output for these overwriting ops. Source operands are slots
    // 3 and 4 of the intrinsic call.
    Register NAccSrc0 = I.getOperand(3).getReg();
    Register NAccSrc1 = I.getOperand(4).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (NAccSrc0.isVirtual())
      RBI.constrainGenericRegister(NAccSrc0, DR64RegClass, MRI);
    if (NAccSrc1.isVirtual())
      RBI.constrainGenericRegister(NAccSrc1, DR64RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(MacOpc)
                           .addDef(DstReg)
                           .addReg(NAccSrc0)
                           .addReg(NAccSrc1);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===-----------------------------------------------------------------===
  // Q-format multiply/accumulate (ternary)
  //
  // MULQ31/MACQ31/MULQ63 are NOT in the ISA DB (the golden
  // slot1_mac/slot2_mac JSONs list 42 FF2MULA32RS_*/FF2MUL32RS_*/FF2MULF32RS_*
  // variants as the real Q-format MAC family — no scalar GPR32 Q1.31 op
  // exists). The phantom MULQ31/MACQ31/MULQ63 instruction defs were removed
  // from HaydnInstrInfo.td; the source-level builtins/intrinsics are preserved
  // for compatibility and lowered here to real ISA sequences:
  // haydn_mulq31(a, b, _) -> MULSSH(a, b) (signed high-half product)
  // haydn_macq31(acc, a, b) -> ADD32(acc, MULSSH(a,b))
  // haydn_mulq63(a, b, _) -> MUL64_LL (full signed 64-bit product)
  //===-----------------------------------------------------------------===
  case haydn_mulq31: {
    Register A = I.getOperand(3).getReg();
    Register B = I.getOperand(4).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, GPR32RegClass, MRI);
    for (Register R : {A, B})
      if (R.isVirtual())
        RBI.constrainGenericRegister(R, GPR32RegClass, MRI);
    MachineInstr *MulMI = MIB.buildInstr(Haydn::MULSSH)
                              .addDef(DstReg)
                              .addReg(A)
                              .addReg(B);
    if (!constrainSelectedInstRegOperands(*MulMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_macq31: {
    Register Acc = I.getOperand(2).getReg();
    Register A = I.getOperand(3).getReg();
    Register B = I.getOperand(4).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, GPR32RegClass, MRI);
    for (Register R : {Acc, A, B})
      if (R.isVirtual())
        RBI.constrainGenericRegister(R, GPR32RegClass, MRI);
    Register Prod = MRI.createVirtualRegister(&GPR32RegClass);
    // Q31 product is high-half signed (MULSSH), not wrap low-half (MULL).
    MachineInstr *MulMI =
        MIB.buildInstr(Haydn::MULSSH).addDef(Prod).addReg(A).addReg(B);
    if (!constrainSelectedInstRegOperands(*MulMI, TII, TRI, RBI))
      return false;
    MachineInstr *AddMI = MIB.buildInstr(Haydn::ADD32)
                              .addDef(DstReg)
                              .addReg(Acc)
                              .addReg(Prod);
    if (!constrainSelectedInstRegOperands(*AddMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_mulq63: {
    // no native scalar DR64 Q1.63 op. Lower to MUL64_LL (full signed
    // 64-bit product) for source compatibility; saturation is a deferred ISA
    // gap (FF2MUL32RS_* is the real Q-format family but operates on packed
    // lanes, not scalar DR64).
    Register A = I.getOperand(3).getReg();
    Register B = I.getOperand(4).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    for (Register R : {A, B})
      if (R.isVirtual())
        RBI.constrainGenericRegister(R, DR64RegClass, MRI);
    MachineInstr *MulMI = MIB.buildInstr(Haydn::MUL64_LL)
                              .addDef(DstReg)
                              .addReg(A)
                              .addReg(B);
    if (!constrainSelectedInstRegOperands(*MulMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_mac32: {
    // MAC32 is not a DB opcode (MULA* is DR64). Lower to MULL + ADD32.
    Register Acc = I.getOperand(2).getReg();
    Register A = I.getOperand(3).getReg();
    Register B = I.getOperand(4).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, GPR32RegClass, MRI);
    for (Register R : {Acc, A, B})
      if (R.isVirtual())
        RBI.constrainGenericRegister(R, GPR32RegClass, MRI);
    Register Prod = MRI.createVirtualRegister(&GPR32RegClass);
    if (!emitScalarMul32(I, Prod, A, B, MIB, MRI))
      return false;
    MachineInstr *AddMI = MIB.buildInstr(Haydn::ADD32)
                              .addDef(DstReg)
                              .addReg(Acc)
                              .addReg(Prod);
    if (!constrainSelectedInstRegOperands(*AddMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===-----------------------------------------------------------------===
  // SIMD binary DR64
  //===-----------------------------------------------------------------===
  case haydn_x2add32s:    return selectBinary(X2ADD32S,    DR64RegClass);
  case haydn_x2sub32s:    return selectBinary(X2SUB32S,    DR64RegClass);
  case haydn_x2addsub32s: return selectBinary(X2ADDSUB32S, DR64RegClass);
  case haydn_x4add16s:    return selectBinary(X4ADD16S,    DR64RegClass);
  case haydn_x4sub16s:    return selectBinary(X4SUB16S,    DR64RegClass);

  //===-----------------------------------------------------------------===
  // SIMD ternary DR64 (MAC variants)
  //===-----------------------------------------------------------------===
  case haydn_x2mula32:  return selectSimdMacAcc2Dest(X2MULA32);
  case haydn_x2muls32:  return selectSimdMacAcc2Dest(X2MULS32);
  case haydn_x4mula16:  return selectSimdMacAcc2Dest(X4MULA16);
  case haydn_x4muls16:  return selectSimdMacAcc2Dest(X4MULS16);
  case haydn_x4mula16s: return selectSimdMacAcc2Dest(X4MULA16S);
  case haydn_x4muls16s: return selectSimdMacAcc2Dest(X4MULS16S);

  //===-----------------------------------------------------------------===
  // Transcendental (unary GPR32)
  //===-----------------------------------------------------------------===
  case haydn_log2:  return selectUnary(LOG2,  GPR32RegClass);
  case haydn_exp2:  return selectUnary(EXP2,  GPR32RegClass);
  case haydn_recip: return selectUnary(RECIP, GPR32RegClass);
  case haydn_sqrt:  return selectUnary(SQRT,  GPR32RegClass);

  //===-----------------------------------------------------------------===
  // Normalization (NSA)
  // NSA32/NSAU32: ALU32 R — GPR32→GPR32.
  // NSA64/NSAZ*/NSA*_L: ALU64 R_GD — DR64→GPR32 (match members / POPCOUNT64).
  //===-----------------------------------------------------------------===
  case haydn_nsa32:    return selectUnary(NSA32,    GPR32RegClass);
  case haydn_nsau32:   return selectUnary(NSAU32,   GPR32RegClass);
  case haydn_nsa64:    return selectUnaryR_GD(NSA64);
  case haydn_nsa16_l:  return selectUnaryR_GD(NSA16_L);
  case haydn_nsa32_l:  return selectUnaryR_GD(NSA32_L);
  case haydn_nsaz64:   return selectUnaryR_GD(NSAZ64);
  case haydn_nsaz16_l: return selectUnaryR_GD(NSAZ16_L);
  case haydn_nsaz32_l: return selectUnaryR_GD(NSAZ32_L);

  //===-----------------------------------------------------------------===
  // MULSA32/MULSS32 — Dual 32-bit multiply-accumulate/subtract (binary DR64)
  //===-----------------------------------------------------------------===
  case haydn_mulsa32_hhll: return selectBinary(MULSA32_HHLL, DR64RegClass);
  case haydn_mulsa32_hllh: return selectBinary(MULSA32_HLLH, DR64RegClass);
  case haydn_mulss32_hhll: return selectBinary(MULSS32_HHLL, DR64RegClass);
  case haydn_mulss32_hllh: return selectBinary(MULSS32_HLLH, DR64RegClass);

  // satsr64 / packsr32 / packsr32x2_* are not IR intrinsics (composites in
  // haydn_dsp.h only). Do not re-add selection cases.

  //===-----------------------------------------------------------------===
  // AE_MAXABS32S — per-lane saturating abs-max of two DR64 SIMD values.
  // result.L = SAT32(MAX(SAT_ABS(a.L), SAT_ABS(b.L)))
  // result.H = SAT32(MAX(SAT_ABS(a.H), SAT_ABS(b.H)))
  //
  // Decomposition (no native fused abs-max in Haydn ISA):
  // tmp_a = X2ABS32S a; DR64 -> DR64 per-lane saturating abs
  // tmp_b = X2ABS32S b
  // result = X2MAX32 tmp_a, tmp_b; per-lane max
  //===-----------------------------------------------------------------===
  case haydn_maxabs32s: {
    Register SrcA = I.getOperand(2).getReg();
    Register SrcB = I.getOperand(3).getReg();

    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (SrcA.isVirtual())
      RBI.constrainGenericRegister(SrcA, DR64RegClass, MRI);
    if (SrcB.isVirtual())
      RBI.constrainGenericRegister(SrcB, DR64RegClass, MRI);

    Register TmpA = MRI.createVirtualRegister(&Haydn::DR64RegClass);
    Register TmpB = MRI.createVirtualRegister(&Haydn::DR64RegClass);

    MachineInstr *MA =
        MIB.buildInstr(Haydn::X2ABS32S).addDef(TmpA).addReg(SrcA);
    if (!constrainSelectedInstRegOperands(*MA, TII, TRI, RBI))
      return false;

    MachineInstr *MB =
        MIB.buildInstr(Haydn::X2ABS32S).addDef(TmpB).addReg(SrcB);
    if (!constrainSelectedInstRegOperands(*MB, TII, TRI, RBI))
      return false;

    MachineInstr *MMax =
        MIB.buildInstr(Haydn::X2MAX32).addDef(DstReg).addReg(TmpA).addReg(TmpB);
    if (!constrainSelectedInstRegOperands(*MMax, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===-----------------------------------------------------------------===
  // Wave 2: LC3 BASOP 16x16 fractional multiply
  // _hs00: pure 2-input multiply. _hs_11_00: accumulator-form 3-arg.
  //===-----------------------------------------------------------------===
  case haydn_fmul16_hs00:     return selectBinary(FMUL16_HS00,       DR64RegClass);
  case haydn_fmulaa16_hs_11_00: return selectAccMAC(FMULAA16_HS_11_00);
  case haydn_fmulaa16_hs_33_22: return selectAccMAC(FMULAA16_HS_33_22);
  case haydn_fmulss16_hs_11_00: return selectAccMAC(FMULSS16_HS_11_00);

  //===-----------------------------------------------------------------===
  // Wave 2: IIR biquad fused dual MAC — accumulator-form 3-arg
  //===-----------------------------------------------------------------===
  // F2MULAA32RS_HHLL: dual-product accumulator MAC (sat+round). : folds to
  // the binary zero-accumulator form F2MULZAA* when acc==0.
  //===-----------------------------------------------------------------===
  case haydn_f2mulaa32rs_hhll: return selectF2MULAAOrZAA(F2MULAA32RS_HHLL, F2MULZAA32RS_HHLL);
  case haydn_f2mulaa32rs_hllh: return selectF2MULAAOrZAA(F2MULAA32RS_HLLH, F2MULZAA32RS_HLLH);
  case haydn_f2mulss32rs_hhll: return selectAccMAC(F2MULSS32RS_HHLL);
  case haydn_f2mulss32rs_hllh: return selectAccMAC(F2MULSS32RS_HLLH);

  //===-----------------------------------------------------------------===
  // Wave 2: SRAI64R / SRAI64 / SLLI64 / SRLI64 — DR64 imm shifts (RI6).
  // Intrinsic (i64, i32) with constant shift; reg twins use sra64/sll64/srl64.
  //===-----------------------------------------------------------------===
  case haydn_srai64r:
  case haydn_srai64:
  case haydn_slli64:
  case haydn_srli64: {
    Register Accum = I.getOperand(2).getReg();
    int64_t ShiftVal = 0;
    if (!getConstOpSExt(I.getOperand(3), ShiftVal)) {
      LLVM_DEBUG(dbgs() << "DR64 imm shift: shift amount must be a constant\n");
      return false;
    }
    // Generated members: SRAI64*/SLLI64/SRLI64 RI6 (uimm6).
    if (!expectUImm(ShiftVal, 6, "DR64 imm shift"))
      return false;

    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (Accum.isVirtual())
      RBI.constrainGenericRegister(Accum, DR64RegClass, MRI);

    unsigned Opc = SRAI64R;
    switch (IntrID) {
    case Intrinsic::haydn_srai64r: Opc = SRAI64R; break;
    case Intrinsic::haydn_srai64:  Opc = SRAI64;  break;
    case Intrinsic::haydn_slli64:  Opc = SLLI64;  break;
    case Intrinsic::haydn_srli64:  Opc = SRLI64;  break;
    default:
      llvm_unreachable("unexpected DR64 imm shift intrinsic");
    }
    MachineInstr *MI =
        MIB.buildInstr(Opc).addDef(DstReg).addReg(Accum).addImm(ShiftVal);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===-----------------------------------------------------------------===
  // Wave 2: Complex multiply — quad 16-bit (binary DR64)
  //===-----------------------------------------------------------------===
  case haydn_x4fcmul16rs:   return selectBinary(X4FCMUL16RS,   DR64RegClass);
  // X4FCMULA16RS/RSS are tied-def accumulators (DB reads rtd;)
  // select via selectAccMAC, which emits %rd = MAC %rd_in(tied), %rs1, %rs2.
  case haydn_x4fcmula16rs:  return selectAccMAC(X4FCMULA16RS);
  case haydn_x4fcmul16rss:  return selectBinary(X4FCMUL16RSS,  DR64RegClass);
  case haydn_x4fcmula16rss: return selectAccMAC(X4FCMULA16RSS);

  //===-----------------------------------------------------------------===
  // Wave 3: Complex multiply — dual 32-bit (ternary DR64)
  // X2CMUL32/X2CMUL32S use FmtMAC format with accumulator operand.
  //===-----------------------------------------------------------------===
  case haydn_x2cmul32:  return selectSimdMac2Dest(X2CMUL32);
  case haydn_x2cmul32s: return selectSimdMac2Dest(X2CMUL32S);

  //===-----------------------------------------------------------------===
  // Wave 3: SFR Flag Register Predication
  // Golden/ISA: SEQ/SLT/SLE are 2-op (rsd1, rsd2) → SFR only; MOVT/MOVF are
  // 2-op RMW (rtd, rsd). Do not emit a 3-operand DR dest form.
  //===-----------------------------------------------------------------===
  case haydn_x2seq32:
  case haydn_x2slt32:
  case haydn_x2sle32:
  case haydn_x4seq16:
  case haydn_x4slt16:
  case haydn_x4sle16: {
    unsigned Opc = IntrID == haydn_x2seq32   ? X2SEQ32
                   : IntrID == haydn_x2slt32 ? X2SLT32
                   : IntrID == haydn_x2sle32 ? X2SLE32
                   : IntrID == haydn_x4seq16 ? X4SEQ16
                   : IntrID == haydn_x4slt16 ? X4SLT16
                                             : X4SLE16;
    Register A = I.getOperand(2).getReg();
    Register B = I.getOperand(3).getReg();
    if (A.isVirtual())
      RBI.constrainGenericRegister(A, DR64RegClass, MRI);
    if (B.isVirtual())
      RBI.constrainGenericRegister(B, DR64RegClass, MRI);
    // Hardware writes SFR only (no DR dest).
    MachineInstr *CmpMI = MIB.buildInstr(Opc).addReg(A).addReg(B);
    if (!constrainSelectedInstRegOperands(*CmpMI, TII, TRI, RBI))
      return false;
    // C intrinsic still has a return value: passthrough rs1 (not on the wire).
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    MIB.buildCopy(DstReg, A);
    I.eraseFromParent();
    return true;
  }
  case haydn_x2movf32:
  case haydn_x2movt32:
  case haydn_x4movf16:
  case haydn_x4movt16: {
    // dst = movt/movf(false=rs1, true=rsd); Constraints $rd=$rs1 for RMW seed.
    unsigned Opc = IntrID == haydn_x2movf32   ? X2MOVF32
                   : IntrID == haydn_x2movt32 ? X2MOVT32
                   : IntrID == haydn_x4movf16 ? X4MOVF16
                                              : X4MOVT16;
    Register FalseV = I.getOperand(2).getReg();
    Register TrueV = I.getOperand(3).getReg();
    if (FalseV.isVirtual())
      RBI.constrainGenericRegister(FalseV, DR64RegClass, MRI);
    if (TrueV.isVirtual())
      RBI.constrainGenericRegister(TrueV, DR64RegClass, MRI);
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    MachineInstr *MovMI =
        MIB.buildInstr(Opc).addDef(DstReg).addReg(FalseV).addReg(TrueV);
    if (!constrainSelectedInstRegOperands(*MovMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===---------------------------------------------------------------===
 // : pure SSA predicate value + fused compare-select
  //
  // Hexagon peer: C2_cmplt → i32 pred; C2_mux(Pu, t, f). Expand only to
  // existing X2/X4SLT + MOVT + MOVESFR2GPR/MOVEGPR2SFR. SFR is a temporary
  // inside the multi-MI sequence; Defs/Uses=[SFR] keep epochs ordered.
  // Operand layouts (G_INTRINSIC):
  //   cmplt:  def pred, id, a, b
  //   mux:    def dst,  id, pred, true, false
  //   cmpsel: def dst,  id, a, b, true, false
  //===---------------------------------------------------------------===
  case haydn_x2cmplt32:
  case haydn_x4cmplt16: {
    // pred = movesfr2gpr(slt(a, b)). SLT is 2-op SFR write only.
    const bool IsX2 = IntrID == haydn_x2cmplt32;
    unsigned SltOpc = IsX2 ? X2SLT32 : X4SLT16;
    Register A = I.getOperand(2).getReg();
    Register B = I.getOperand(3).getReg();
    if (A.isVirtual())
      RBI.constrainGenericRegister(A, DR64RegClass, MRI);
    if (B.isVirtual())
      RBI.constrainGenericRegister(B, DR64RegClass, MRI);
    MachineInstr *SltMI = MIB.buildInstr(SltOpc).addReg(A).addReg(B);
    if (!constrainSelectedInstRegOperands(*SltMI, TII, TRI, RBI))
      return false;
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, GPR32RegClass, MRI);
    MachineInstr *MoveMI = MIB.buildInstr(MOVESFR2GPR).addDef(DstReg);
    if (!constrainSelectedInstRegOperands(*MoveMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_x2mux32:
  case haydn_x4mux16: {
    // dst = movt(false, true) after restoring pred into SFR.
    // Args: pred, true_val, false_val (Hexagon C2_mux order).
    // MOVT: %rd = MOVT %rs1(false, tied), %rsd(true) → asm "movt rd, rsd".
    const bool IsX2 = IntrID == haydn_x2mux32;
    unsigned MovtOpc = IsX2 ? X2MOVT32 : X4MOVT16;
    Register Pred = I.getOperand(2).getReg();
    Register TrueV = I.getOperand(3).getReg();
    Register FalseV = I.getOperand(4).getReg();
    if (Pred.isVirtual())
      RBI.constrainGenericRegister(Pred, GPR32RegClass, MRI);
    if (TrueV.isVirtual())
      RBI.constrainGenericRegister(TrueV, DR64RegClass, MRI);
    if (FalseV.isVirtual())
      RBI.constrainGenericRegister(FalseV, DR64RegClass, MRI);
    MachineInstr *SfrMI = MIB.buildInstr(MOVEGPR2SFR).addReg(Pred);
    if (!constrainSelectedInstRegOperands(*SfrMI, TII, TRI, RBI))
      return false;
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    MachineInstr *MovtMI = MIB.buildInstr(MovtOpc)
                               .addDef(DstReg)
                               .addReg(FalseV)
                               .addReg(TrueV);
    if (!constrainSelectedInstRegOperands(*MovtMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_x2cmpsel32:
  case haydn_x4cmpsel16: {
    // Fused: SLT(a,b) → SFR (2-op), then MOVT RMW (2-op asm). Args:
    // a, b, true_val, false_val.
    const bool IsX2 = IntrID == haydn_x2cmpsel32;
    // Logical SLT/MOVT only (D493). Post-RA LogicalMaterialize commits
    // *_S1/*_S2; pre-RA member opcodes are illegal.
    unsigned SltOpc = IsX2 ? X2SLT32 : X4SLT16;
    unsigned MovtOpc = IsX2 ? X2MOVT32 : X4MOVT16;
    Register A = I.getOperand(2).getReg();
    Register B = I.getOperand(3).getReg();
    Register TrueV = I.getOperand(4).getReg();
    Register FalseV = I.getOperand(5).getReg();
    if (A.isVirtual())
      RBI.constrainGenericRegister(A, DR64RegClass, MRI);
    if (B.isVirtual())
      RBI.constrainGenericRegister(B, DR64RegClass, MRI);
    if (TrueV.isVirtual())
      RBI.constrainGenericRegister(TrueV, DR64RegClass, MRI);
    if (FalseV.isVirtual())
      RBI.constrainGenericRegister(FalseV, DR64RegClass, MRI);
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    MachineInstr *SltMI = MIB.buildInstr(SltOpc).addReg(A).addReg(B);
    if (!constrainSelectedInstRegOperands(*SltMI, TII, TRI, RBI))
      return false;
    // Tied-def MOVT: %rd = MOVT %rs1(false, tied), %rsd(true).
    MachineInstr *MovtMI = MIB.buildInstr(MovtOpc)
                               .addDef(DstReg)
                               .addReg(FalseV)
                               .addReg(TrueV);
    if (!constrainSelectedInstRegOperands(*MovtMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===---------------------------------------------------------------===
  // Wave 4: X2 SIMD fractional multiply (binary DR64)
  //===---------------------------------------------------------------===
  case haydn_x2fmul32rs:  return selectBinary(X2FMUL32RS,  DR64RegClass);
  case haydn_x2fmul32rss: return selectBinary(X2FMUL32RSS, DR64RegClass);
  case haydn_x2fmul32ts:  return selectBinary(X2FMUL32TS,  DR64RegClass);

  // Golden: DR_Read includes rtd (tied acc). Ternary intrinsic + selectAccMAC.
  case haydn_x2fmula32rs:  return selectAccMAC(X2FMULA32RS);
  case haydn_x2fmula32rss: return selectAccMAC(X2FMULA32RSS);
  case haydn_x2fmula32ts:  return selectAccMAC(X2FMULA32TS);

  case haydn_x2fmuls32rs:  return selectAccMAC(X2FMULS32RS);
  case haydn_x2fmuls32rss: return selectAccMAC(X2FMULS32RSS);
  case haydn_x2fmuls32ts:  return selectAccMAC(X2FMULS32TS);

  //===---------------------------------------------------------------===
  // Wave 4: X4 SIMD fractional multiply (binary DR64)
  //===---------------------------------------------------------------===
  case haydn_x4fmul16rs:  return selectBinary(X4FMUL16RS,  DR64RegClass);
  case haydn_x4fmul16rss: return selectBinary(X4FMUL16RSS, DR64RegClass);
  case haydn_x4fmul16ts:  return selectBinary(X4FMUL16TS,  DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4: X2/X4 shift with rounding (binary DR64)
  //===---------------------------------------------------------------===
  case haydn_x2frsst32: return selectBinary(X2FRSST32, DR64RegClass);
  case haydn_x2frst32:  return selectBinary(X2FRST32,  DR64RegClass);
  case haydn_x4frsst16: return selectBinary(X4FRSST16, DR64RegClass);
  case haydn_x4frst16:  return selectBinary(X4FRST16,  DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4: X2SRAI32R / X4SRAI16R — immediate shift with rounding
  // (i64 val, i32 shift_imm) -> i64
  //===---------------------------------------------------------------===
  case haydn_x2srai32r: {
    Register Src = I.getOperand(2).getReg();
    int64_t ShiftVal = 0;
    if (!getConstOpSExt(I.getOperand(3), ShiftVal)) {
      LLVM_DEBUG(dbgs() << "X2SRAI32R: shift amount must be a constant\n");
      return false;
    }
    // Generated member: X2SRAI32R_*_RI5 (uimm5, 32-bit lanes).
    if (!expectUImm(ShiftVal, 5, "X2SRAI32R"))
      return false;

    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, DR64RegClass, MRI);

    MachineInstr *MI =
        MIB.buildInstr(X2SRAI32R).addDef(DstReg).addReg(Src).addImm(ShiftVal);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  case haydn_x4srai16r: {
    Register Src = I.getOperand(2).getReg();
    int64_t ShiftVal = 0;
    if (!getConstOpSExt(I.getOperand(3), ShiftVal)) {
      LLVM_DEBUG(dbgs() << "X4SRAI16R: shift amount must be a constant\n");
      return false;
    }
    // Generated member: X4SRAI16R_*_RI4 (uimm4, 16-bit lanes).
    if (!expectUImm(ShiftVal, 4, "X4SRAI16R"))
      return false;

    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, DR64RegClass, MRI);

    MachineInstr *MI =
        MIB.buildInstr(X4SRAI16R).addDef(DstReg).addReg(Src).addImm(ShiftVal);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===---------------------------------------------------------------===
  // Wave 4: FMUL16_HS remaining variants (binary DR64)
  //===---------------------------------------------------------------===
  case haydn_fmul16_hs01: return selectBinary(FMUL16_HS01, DR64RegClass);
  case haydn_fmul16_hs02: return selectBinary(FMUL16_HS02, DR64RegClass);
  case haydn_fmul16_hs03: return selectBinary(FMUL16_HS03, DR64RegClass);
  case haydn_fmul16_hs11: return selectBinary(FMUL16_HS11, DR64RegClass);
  case haydn_fmul16_hs12: return selectBinary(FMUL16_HS12, DR64RegClass);
  case haydn_fmul16_hs13: return selectBinary(FMUL16_HS13, DR64RegClass);
  case haydn_fmul16_hs22: return selectBinary(FMUL16_HS22, DR64RegClass);
  case haydn_fmul16_hs23: return selectBinary(FMUL16_HS23, DR64RegClass);
  case haydn_fmul16_hs33: return selectBinary(FMUL16_HS33, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4: FMUL16_LS variants (binary DR64)
  //===---------------------------------------------------------------===
  case haydn_fmul16_ls00: return selectBinary(FMUL16_LS00, DR64RegClass);
  case haydn_fmul16_ls01: return selectBinary(FMUL16_LS01, DR64RegClass);
  case haydn_fmul16_ls02: return selectBinary(FMUL16_LS02, DR64RegClass);
  case haydn_fmul16_ls03: return selectBinary(FMUL16_LS03, DR64RegClass);
  case haydn_fmul16_ls11: return selectBinary(FMUL16_LS11, DR64RegClass);
  case haydn_fmul16_ls12: return selectBinary(FMUL16_LS12, DR64RegClass);
  case haydn_fmul16_ls13: return selectBinary(FMUL16_LS13, DR64RegClass);
  case haydn_fmul16_ls22: return selectBinary(FMUL16_LS22, DR64RegClass);
  case haydn_fmul16_ls23: return selectBinary(FMUL16_LS23, DR64RegClass);
  case haydn_fmul16_ls33: return selectBinary(FMUL16_LS33, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4: FMULAA16 HS/LS MAC variants (binary DR64)
  //===---------------------------------------------------------------===
  case haydn_fmulaa16_hs_13_02: return selectBinary(FMULAA16_HS_13_02, DR64RegClass);
  case haydn_fmulaa16_ls_11_00: return selectBinary(FMULAA16_LS_11_00, DR64RegClass);
  case haydn_fmulaa16_ls_13_02: return selectBinary(FMULAA16_LS_13_02, DR64RegClass);
  case haydn_fmulaa16_ls_33_22: return selectBinary(FMULAA16_LS_33_22, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4: FMULSS16 HS/LS MSU variants (binary DR64)
  //===---------------------------------------------------------------===
  case haydn_fmulss16_hs_13_02: return selectBinary(FMULSS16_HS_13_02, DR64RegClass);
  case haydn_fmulss16_hs_33_22: return selectBinary(FMULSS16_HS_33_22, DR64RegClass);
  case haydn_fmulss16_ls_11_00: return selectBinary(FMULSS16_LS_11_00, DR64RegClass);
  case haydn_fmulss16_ls_13_02: return selectBinary(FMULSS16_LS_13_02, DR64RegClass);
  case haydn_fmulss16_ls_33_22: return selectBinary(FMULSS16_LS_33_22, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4: F2MUL zero-accumulator variants (binary DR64)
  //===---------------------------------------------------------------===
  // Saturating with rounding
  case haydn_f2mulas32rs_hhll: return selectBinary(F2MULAS32RS_HHLL, DR64RegClass);
  case haydn_f2mulas32rs_hllh: return selectBinary(F2MULAS32RS_HLLH, DR64RegClass);
  case haydn_f2mulsa32rs_hhll: return selectBinary(F2MULSA32RS_HHLL, DR64RegClass);
  case haydn_f2mulsa32rs_hllh: return selectBinary(F2MULSA32RS_HLLH, DR64RegClass);
  // Non-saturating with rounding
  case haydn_f2mulas32r_hhll: return selectBinary(F2MULAS32R_HHLL, DR64RegClass);
  case haydn_f2mulas32r_hllh: return selectBinary(F2MULAS32R_HLLH, DR64RegClass);
  case haydn_f2mulsa32r_hhll: return selectBinary(F2MULSA32R_HHLL, DR64RegClass);
  case haydn_f2mulsa32r_hllh: return selectBinary(F2MULSA32R_HLLH, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4: F2MUL zero-accumulator / binary form (Z prefix).
  // F2MULZAA32RS_HHLL: rtd = 0 + r_temp1 + r_temp0 (no accumulator read)
  // This is the binary dual-product MAC. ISA-51 wrongly claimed "no binary form
  // exists" — it does (the Z prefix). closes the compiler-lowering gap: the
  // intrinsic was declared but had no selector case, so it crashed select.
  // Routed to selectBinary (2 source operands, no tied accumulator — unlike the
  // AA form which uses selectAccMAC with a tied $rd_in).
  //===---------------------------------------------------------------===
  case haydn_f2mulzaa32rs_hhll: return selectBinary(F2MULZAA32RS_HHLL, DR64RegClass);
  case haydn_f2mulzaa32rs_hllh: return selectBinary(F2MULZAA32RS_HLLH, DR64RegClass);
  case haydn_f2mulzaa32r_hhll:  return selectBinary(F2MULZAA32R_HHLL,  DR64RegClass);
  case haydn_f2mulzaa32r_hllh:  return selectBinary(F2MULZAA32R_HLLH,  DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4 Tier 2: X2/X4 SIMD Unary ops (unary DR64)
  //===---------------------------------------------------------------===
  case haydn_x2abs32:    return selectUnary(X2ABS32,    DR64RegClass);
  case haydn_x2abs32s:   return selectUnary(X2ABS32S,   DR64RegClass);
  case haydn_x2neg32:    return selectUnary(X2NEG32,    DR64RegClass);
  case haydn_x2neg32s:   return selectUnary(X2NEG32S,   DR64RegClass);
  case haydn_x2neg32_l:  return selectUnary(X2NEG32_L,  DR64RegClass);
  case haydn_x2neg32s_l: return selectUnary(X2NEG32S_L, DR64RegClass);
  case haydn_x2swap32:   return selectUnary(X2SWAP32,   DR64RegClass);
  case haydn_x2mjswap32:  return selectUnary(X2MJSWAP32,  DR64RegClass);
  case haydn_x2mjswap32s: return selectUnary(X2MJSWAP32S, DR64RegClass);

  case haydn_x4abs16:    return selectUnary(X4ABS16,    DR64RegClass);
  case haydn_x4abs16s:   return selectUnary(X4ABS16S,   DR64RegClass);
  case haydn_x4neg16:    return selectUnary(X4NEG16,    DR64RegClass);
  case haydn_x4neg16s:   return selectUnary(X4NEG16S,   DR64RegClass);
  case haydn_x4swap16:   return selectUnary(X4SWAP16,   DR64RegClass);
  case haydn_x4mjswap16:  return selectUnary(X4MJSWAP16,  DR64RegClass);
  case haydn_x4mjswap16s: return selectUnary(X4MJSWAP16S, DR64RegClass);
  case haydn_x4conj16:   return selectUnary(X4CONJ16,   DR64RegClass);
  case haydn_x4conj16s:  return selectUnary(X4CONJ16S,  DR64RegClass);
  case haydn_x4energy16: return selectUnary(X4ENERGY16, DR64RegClass);
  // Logical X4CMUL16*; sparse AlternateInsts hold S1/S2 members. Post-RA
  // setDesc commits the encode member (no pre-RA slot suffix).
  case haydn_x4cmul16:   return selectUnary(X4CMUL16,   DR64RegClass);
  case haydn_x4cmul16s:  return selectUnary(X4CMUL16S,  DR64RegClass);
  case haydn_x4cmul16s_f2: return selectUnary(X4CMUL16S_F2, DR64RegClass);
  case haydn_x4cmul16_f2:  return selectUnary(X4CMUL16_F2,  DR64RegClass);

  // X2/X4 binary (DR64 binary)
  case haydn_x2max32:   return selectBinary(X2MAX32,   DR64RegClass);
  case haydn_x2min32:   return selectBinary(X2MIN32,   DR64RegClass);
  case haydn_x2clamp32: return selectBinary(X2CLAMP32, DR64RegClass);
  case haydn_x4max16:   return selectBinary(X4MAX16,   DR64RegClass);
  case haydn_x4min16:   return selectBinary(X4MIN16,   DR64RegClass);
  case haydn_x4clamp16: return selectBinary(X4CLAMP16, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4 Tier 2: Shuffle/Pack ops
  //===---------------------------------------------------------------===
  case haydn_x2sel32_hh: return selectBinary(X2SEL32_HH, DR64RegClass);
  case haydn_x2sel32_hl: return selectBinary(X2SEL32_HL, DR64RegClass);
  case haydn_x2sel32_lh: return selectBinary(X2SEL32_LH, DR64RegClass);
  case haydn_x2sel32_ll: return selectBinary(X2SEL32_LL, DR64RegClass);

  case haydn_x2addsub32:       return selectBinary(X2ADDSUB32,       DR64RegClass);
  // Note: haydn_x2addsub32s already handled in SIMD X2 section above.
  case haydn_x2addsub32_hllh:  return selectBinary(X2ADDSUB32_HLLH,  DR64RegClass);
  case haydn_x2addsub32s_hllh: return selectBinary(X2ADDSUB32S_HLLH, DR64RegClass);
  case haydn_x2subadd32:       return selectBinary(X2SUBADD32,       DR64RegClass);
  case haydn_x2subadd32s:      return selectBinary(X2SUBADD32S,      DR64RegClass);
  case haydn_x2subadd32_hllh:  return selectBinary(X2SUBADD32_HLLH,  DR64RegClass);
  case haydn_x2subadd32s_hllh: return selectBinary(X2SUBADD32S_HLLH, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4 Tier 2: X4 Complex Multiply variants (binary DR64)
  //===---------------------------------------------------------------===
  case haydn_x4cmul16s_h:  return selectBinary(X4CMUL16S_H,  DR64RegClass);
  case haydn_x4cmul16s_l:  return selectBinary(X4CMUL16S_L,  DR64RegClass);
  // Golden MAC: rtd is accumulator. Ternary intrinsic + selectAccMAC.
  case haydn_x4cmula16s_h: return selectAccMAC(X4CMULA16S_H);
  case haydn_x4cmula16s_l: return selectAccMAC(X4CMULA16S_L);

  case haydn_x4cjmul16s_h:  return selectBinary(X4CJMUL16S_H,  DR64RegClass);
  case haydn_x4cjmul16s_l:  return selectBinary(X4CJMUL16S_L,  DR64RegClass);
  case haydn_x4cjmula16s_h: return selectAccMAC(X4CJMULA16S_H);
  case haydn_x4cjmula16s_l: return selectAccMAC(X4CJMULA16S_L);

  //===---------------------------------------------------------------===
  // Wave 4 Tier 2: 64-bit scalar ops
  //===---------------------------------------------------------------===
  // Unary DR64
  case haydn_not64: return selectUnary(NOT64, DR64RegClass);
  case haydn_seq64: return selectUnary(SEQ64, DR64RegClass);

  // Binary DR64
  case haydn_max64:   return selectBinary(MAX64,   DR64RegClass);
  case haydn_min64:   return selectBinary(MIN64,   DR64RegClass);
  case haydn_add64_h: return selectBinary(ADD64_H, DR64RegClass);
  case haydn_add64_l: return selectBinary(ADD64_L, DR64RegClass);
  case haydn_sub64_h: return selectBinary(SUB64_H, DR64RegClass);
  case haydn_sub64_l: return selectBinary(SUB64_L, DR64RegClass);

  // SLL64/SRA64/SRL64: golden rtd,rsd,rs — DR64 data + GPR32 amount -> DR64.
  // Same shape as X2/X4 SIMD reg shifts (selectDR64ShiftGPR32).
  case haydn_sll64: return selectDR64ShiftGPR32(SLL64);
  // Tier 2: DR64 bitwise AND/OR (AE_AND32/OR32/AND64).
  case haydn_and64: return selectBinary(AND64, DR64RegClass);
  case haydn_or64:  return selectBinary(OR64, DR64RegClass);

  case haydn_sra64: return selectDR64ShiftGPR32(SRA64);
  case haydn_srl64: return selectDR64ShiftGPR32(SRL64);

  //===---------------------------------------------------------------===
  // Wave 4 Tier 3: SMULA16 — Signed 16-bit MAC with lane selection
  //===---------------------------------------------------------------===
  case haydn_smula16_00: return selectBinary(SMULA16_00, DR64RegClass);
  case haydn_smula16_10: return selectBinary(SMULA16_10, DR64RegClass);
  case haydn_smula16_11: return selectBinary(SMULA16_11, DR64RegClass);
  case haydn_smula16_20: return selectBinary(SMULA16_20, DR64RegClass);
  case haydn_smula16_21: return selectBinary(SMULA16_21, DR64RegClass);
  case haydn_smula16_22: return selectBinary(SMULA16_22, DR64RegClass);
  case haydn_smula16_30: return selectBinary(SMULA16_30, DR64RegClass);
  case haydn_smula16_31: return selectBinary(SMULA16_31, DR64RegClass);
  case haydn_smula16_32: return selectBinary(SMULA16_32, DR64RegClass);
  case haydn_smula16_33: return selectBinary(SMULA16_33, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4 Tier 3: SMULA16S — Signed 16-bit saturating MAC
  //===---------------------------------------------------------------===
  case haydn_smula16s_00: return selectBinary(SMULA16S_00, DR64RegClass);
  case haydn_smula16s_10: return selectBinary(SMULA16S_10, DR64RegClass);
  case haydn_smula16s_11: return selectBinary(SMULA16S_11, DR64RegClass);
  case haydn_smula16s_20: return selectBinary(SMULA16S_20, DR64RegClass);
  case haydn_smula16s_21: return selectBinary(SMULA16S_21, DR64RegClass);
  case haydn_smula16s_22: return selectBinary(SMULA16S_22, DR64RegClass);
  case haydn_smula16s_30: return selectBinary(SMULA16S_30, DR64RegClass);
  case haydn_smula16s_31: return selectBinary(SMULA16S_31, DR64RegClass);
  case haydn_smula16s_32: return selectBinary(SMULA16S_32, DR64RegClass);
  case haydn_smula16s_33: return selectBinary(SMULA16S_33, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4 Tier 3: SMULS16 — Signed 16-bit multiply-subtract
  //===---------------------------------------------------------------===
  case haydn_smuls16_00: return selectBinary(SMULS16_00, DR64RegClass);
  case haydn_smuls16_10: return selectBinary(SMULS16_10, DR64RegClass);
  case haydn_smuls16_11: return selectBinary(SMULS16_11, DR64RegClass);
  case haydn_smuls16_20: return selectBinary(SMULS16_20, DR64RegClass);
  case haydn_smuls16_21: return selectBinary(SMULS16_21, DR64RegClass);
  case haydn_smuls16_22: return selectBinary(SMULS16_22, DR64RegClass);
  case haydn_smuls16_30: return selectBinary(SMULS16_30, DR64RegClass);
  case haydn_smuls16_31: return selectBinary(SMULS16_31, DR64RegClass);
  case haydn_smuls16_32: return selectBinary(SMULS16_32, DR64RegClass);
  case haydn_smuls16_33: return selectBinary(SMULS16_33, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4 Tier 3: SMULS16S — Signed 16-bit saturating multiply-subtract
  //===---------------------------------------------------------------===
  case haydn_smuls16s_00: return selectBinary(SMULS16S_00, DR64RegClass);
  case haydn_smuls16s_10: return selectBinary(SMULS16S_10, DR64RegClass);
  case haydn_smuls16s_11: return selectBinary(SMULS16S_11, DR64RegClass);
  case haydn_smuls16s_20: return selectBinary(SMULS16S_20, DR64RegClass);
  case haydn_smuls16s_21: return selectBinary(SMULS16S_21, DR64RegClass);
  case haydn_smuls16s_22: return selectBinary(SMULS16S_22, DR64RegClass);
  case haydn_smuls16s_30: return selectBinary(SMULS16S_30, DR64RegClass);
  case haydn_smuls16s_31: return selectBinary(SMULS16S_31, DR64RegClass);
  case haydn_smuls16s_32: return selectBinary(SMULS16S_32, DR64RegClass);
  case haydn_smuls16s_33: return selectBinary(SMULS16S_33, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4 Tier 3: FMULS16 — Fractional 16-bit multiply-subtract
  //===---------------------------------------------------------------===
  case haydn_fmuls16_hs00: return selectBinary(FMULS16_HS00, DR64RegClass);
  case haydn_fmuls16_hs01: return selectBinary(FMULS16_HS01, DR64RegClass);
  case haydn_fmuls16_hs02: return selectBinary(FMULS16_HS02, DR64RegClass);
  case haydn_fmuls16_hs03: return selectBinary(FMULS16_HS03, DR64RegClass);
  case haydn_fmuls16_hs11: return selectBinary(FMULS16_HS11, DR64RegClass);
  case haydn_fmuls16_hs12: return selectBinary(FMULS16_HS12, DR64RegClass);
  case haydn_fmuls16_hs13: return selectBinary(FMULS16_HS13, DR64RegClass);
  case haydn_fmuls16_hs22: return selectBinary(FMULS16_HS22, DR64RegClass);
  case haydn_fmuls16_hs23: return selectBinary(FMULS16_HS23, DR64RegClass);
  case haydn_fmuls16_hs33: return selectBinary(FMULS16_HS33, DR64RegClass);

  case haydn_fmuls16_ls00: return selectBinary(FMULS16_LS00, DR64RegClass);
  case haydn_fmuls16_ls01: return selectBinary(FMULS16_LS01, DR64RegClass);
  case haydn_fmuls16_ls02: return selectBinary(FMULS16_LS02, DR64RegClass);
  case haydn_fmuls16_ls03: return selectBinary(FMULS16_LS03, DR64RegClass);
  case haydn_fmuls16_ls11: return selectBinary(FMULS16_LS11, DR64RegClass);
  case haydn_fmuls16_ls12: return selectBinary(FMULS16_LS12, DR64RegClass);
  case haydn_fmuls16_ls13: return selectBinary(FMULS16_LS13, DR64RegClass);
  case haydn_fmuls16_ls22: return selectBinary(FMULS16_LS22, DR64RegClass);
  case haydn_fmuls16_ls23: return selectBinary(FMULS16_LS23, DR64RegClass);
  case haydn_fmuls16_ls33: return selectBinary(FMULS16_LS33, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 4 Tier 3: FMULAS32S/FMULSA32S — Saturating fractional add-subtract
  // dual MAC. DB (7356/7397/8375/8416) shows these read rtd as accumulator
  // (read-modify-write), so route via selectAccMAC (the tied-def emit) not
  // selectBinary. Migrated from selectBinary as part of A1 full
  // (mirrors). Without this the regalloc may split acc/dst physical
  // registers and silicon reads the wrong rtd.
  //===---------------------------------------------------------------===
  case haydn_fmulas32s_hhll: return selectAccMAC(FMULAS32S_HHLL);
  case haydn_fmulas32s_hllh: return selectAccMAC(FMULAS32S_HLLH);
  case haydn_fmulsa32s_hhll: return selectAccMAC(FMULSA32S_HHLL);
  case haydn_fmulsa32s_hllh: return selectAccMAC(FMULSA32S_HLLH);

  //===---------------------------------------------------------------===
  // Wave 4 Tier 3: SRA64R — DR64 data + GPR32 amount -> DR64 (golden).
  // Pack-to-i32 is a cast at the C wrapper (haydn_packsr32), not ISel.
  //===---------------------------------------------------------------===
  case haydn_sra64r: return selectDR64ShiftGPR32(SRA64R);

  //===---------------------------------------------------------------===
  // Wave 5: Scalar 64-bit SFR Compare (unary DR64, like SEQ64)
  //===---------------------------------------------------------------===
  case haydn_slt64: return selectUnary(SLT64, DR64RegClass);
  case haydn_sle64: return selectUnary(SLE64, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 5: Scalar 64-bit SFR Conditional Move (unary DR64)
  //===---------------------------------------------------------------===
  case haydn_movt64: return selectUnary(MOVT64, DR64RegClass);
  case haydn_movf64: return selectUnary(MOVF64, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 5: SFR Register Transfer
  // MOVESFR2GPR: no source operands, reads SFR, writes GPR32 dst
  // MOVEGPR2SFR: reads GPR32 source, writes SFR, no dst
  // ZERO_SFR: no operands, writes SFR
  //===---------------------------------------------------------------===
  case haydn_movesfr2gpr: {
    // MOVESFR2GPR rt — reads SFR implicitly, writes GPR32
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, GPR32RegClass, MRI);
    MachineInstr *MI =
        MIB.buildInstr(MOVESFR2GPR).addDef(DstReg);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_movegpr2sfr: {
    // MOVEGPR2SFR rs — reads GPR32, writes SFR implicitly
    // Void intrinsic: operands are [intrinsic_id, src], so src is at index 1.
    Register Src = I.getOperand(1).getReg();
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, GPR32RegClass, MRI);
    MachineInstr *MI =
        MIB.buildInstr(MOVEGPR2SFR).addReg(Src);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_zero_sfr: {
    // ZERO_SFR — no operands, just clears SFR
    MachineInstr *MI = MIB.buildInstr(ZERO_SFR);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===---------------------------------------------------------------===
  // Cross-RF move: DR64 half-lane -> GPR32 (MOVE32_DR_L / MOVE32_DR_H)
  //
  // The first real DR->GPR cross-bank move in the ISA (Rev 2). Replaces the
  // MOV_DR64_TO_GPR SP-spill shim for the half-lane-extract case. Pure
  // (IntrNoMem): (i64) -> i32.
  //===---------------------------------------------------------------===
  case haydn_move32_dr_l:
  case haydn_move32_dr_h: {
    // Operand 0 = dst (GPR32), operand 1 = intrinsic id, operand 2 = src (DR64).
    Register Src = I.getOperand(2).getReg();
    unsigned Op =
        IntrID == Intrinsic::haydn_move32_dr_l ? MOVE32_DR_L : MOVE32_DR_H;
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, GPR32RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, DR64RegClass, MRI);
    MachineInstr *MI =
        MIB.buildInstr(Op).addDef(DstReg).addReg(Src);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===---------------------------------------------------------------===
  // Circular Buffer Setup (CBR programming via CSRW).
  //
  // Side-effecting void intrinsics: (cbr_sel, value). Lower to the
 // SETCBR_BEGIN / SETCBR_END pseudos; ExpandPseudos expands them to
  // final CSRW_W before post-RA pack. cbr_sel must be a constant 0 or 1.
  // Implicit-def CBR0/CBR1 so CB load/store (implicit-use) cannot reorder
  // past the boundary write after early expand.
  //===---------------------------------------------------------------===
  case haydn_setcbr_begin:
  case haydn_setcbr_end: {
    // Void side-effecting intrinsic: op(0) = intrinsic id
    // op(1) = cbr_sel ImmArg (bare Imm or G_CONSTANT), op(2) = value.
    uint64_t CbrSel = 0;
    if (!getConstOpZExt(I.getOperand(1), CbrSel) || CbrSel > 1) {
      LLVM_DEBUG(dbgs() << "SETCBR: cbr_sel must be a constant 0 or 1\n");
      return false;
    }
    Register Val = I.getOperand(2).getReg();
    if (Val.isVirtual())
      RBI.constrainGenericRegister(Val, GPR32RegClass, MRI);
    unsigned Op =
        IntrID == Intrinsic::haydn_setcbr_begin ? SETCBR_BEGIN : SETCBR_END;
    Register CbrReg = (CbrSel == 0) ? Haydn::CBR0 : Haydn::CBR1;
    MachineInstr *MI =
        MIB.buildInstr(Op)
            .addImm(static_cast<int64_t>(CbrSel))
            .addReg(Val)
            .addReg(CbrReg, RegState::ImplicitDefine);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===---------------------------------------------------------------===
  // MOVDA family — GPR32 <-> DR64 lane transfers.
  //
  // Golden has MOVE32_DR_L/H (DR->GPR extract) but no GPR->DR insert
  // (ISA-63). Each movda intrinsic lowers to MOV_GPR_TO_DR64. Two live
  // GPR halves expand post-RA via DR64PackFI (ST32/ST32/LD64); one-half
  // R0 packs stay stackless. Do not treat the public name as a 1-op move.
  //===---------------------------------------------------------------===
  case haydn_movda32:
  case haydn_movda16: {
    // i32 -> i64: place i32 in low lane, zero-fill high lane.
    // Lowers identically to G_ZEXT i32->i64: MOV_GPR_TO_DR64 rd, x, R0.
    // (movda16 has identical lowering: the DR64 lane is i32-wide so the
    // low 16 bits of the i32 input occupy the lane directly.)
    Register Src = I.getOperand(2).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, GPR32RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(MOV_GPR_TO_DR64)
                           .addDef(DstReg)
                           .addReg(Src)
                           .addReg(R0);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_movda32x2: {
    // (i32, i32) -> i64: place two i32 inputs into the two DR64 lanes.
    // MOV_GPR_TO_DR64 rd, lo, hi.
    Register Lo = I.getOperand(2).getReg();
    Register Hi = I.getOperand(3).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (Lo.isVirtual())
      RBI.constrainGenericRegister(Lo, GPR32RegClass, MRI);
    if (Hi.isVirtual())
      RBI.constrainGenericRegister(Hi, GPR32RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(MOV_GPR_TO_DR64)
                           .addDef(DstReg)
                           .addReg(Lo)
                           .addReg(Hi);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_movad32_low:
  case haydn_movad32_high: {
    // use the native MOVE32_DR_L/H instruction (1 op, slot 0 ALU)
    // instead of the MOV_DR64_TO_GPR stack-spill pseudo (5+ ops).
    Register Src = I.getOperand(2).getReg();
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, DR64RegClass, MRI);
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, GPR32RegClass, MRI);
    unsigned Op = (IntrID == haydn_movad32_low) ? MOVE32_DR_L : MOVE32_DR_H;
    MachineInstr *MI = MIB.buildInstr(Op).addDef(DstReg).addReg(Src);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===---------------------------------------------------------------===
  // Complex 32x16 MAC with rounding + saturation (FFT butterfly core).
  //
  // NatureDSP AE_MULFC32X16RAS_{L,H} compatibility intrinsics. The Haydn
  // ISA has no single instruction covering this exact operation (see
  // ~/haydn-plans/isa-improve/ISA-08-mulfc32x16ras.md). The C wrapper in
  // haydn.h sign-extends the 16-bit twiddle lanes to 32-bit (there
  // is no cross-lane 16->32 pack instruction on Haydn) and invokes this
  // intrinsic with the widened 32-bit complex twiddle in b.
  //
  // The intrinsic then lowers to the existing X2FCMULA32RS instruction
  // which performs a 32x32 complex MAC with rounding and saturation.
  // X2FCMULA32RS rtd, rsd1, rsd2: the silicon reads rtd implicitly as the
  // accumulator, but the.td models rtd as a pure destination with rsd1
  // rsd2 as the two pure sources. The case body below explains why we do
  // NOT seed DstReg with Acc first (SSA validity), and the lane-select
  // (_low vs _high) was already resolved by the C wrapper when it widened
  // the 16-bit twiddle, so both variants lower identically here.
  //===---------------------------------------------------------------===
  case haydn_mulfc32x16ras_low:
  case haydn_mulfc32x16ras_high: {
    // The mulfc32x16ras intrinsic is an acc-form 3-arg complex MAC. The C
    // wrapper sign-extends the 16-bit twiddle lanes to 32-bit and calls this
    // intrinsic; both _low and _high variants lower identically to
    // X2FCMULA32RS. Use selectAccMAC to emit OR64(Tmp,Acc) + MAC
    // with implicit-use of Tmp so the acc value is preserved (was previously
    // dropped — see).
    return selectAccMAC(X2FCMULA32RS);
  }

  //===---------------------------------------------------------------===
  // Circular Buffer Load/Store (CBR) — 2-ret load / ptr-ret store.
  // CB loads : intrinsic(ptr_base, cbr_sel, stride) -> {data, new_ptr}
  // gmir: op(0)=data, op(1)=new_ptr, op(2)=id, op(3)=ptr_base,
  //       op(4)=cbr_sel, op(5)=stride
  // CB stores: intrinsic(data, ptr_base, cbr_sel, stride) -> new_ptr
  // gmir: op(0)=new_ptr, op(1)=id, op(2)=data, op(3)=ptr_base,
  //       op(4)=cbr_sel, op(5)=stride
  // MI keeps tied rs_wb; now live IR def, not a dead vreg.
  //===---------------------------------------------------------------===
  case haydn_ldw_cb_imm: {
    // Intrinsic: (ptr, cbr_sel, stride) -> {data, new_ptr}
    // MI: rtd, rs_wb, rs, cbr_sel, imm.
    // ImmArg cbr_sel/stride may be bare Imm on G_INTRINSIC.
    Register DataReg = I.getOperand(0).getReg();
    Register WbReg = I.getOperand(1).getReg();
    Register PtrBase = I.getOperand(3).getReg();
    uint64_t CbrSel = 0;
    if (!getConstOpZExt(I.getOperand(4), CbrSel)) {
      LLVM_DEBUG(dbgs() << "LDW_CB_IMM: cbr_sel must be a constant\n");
      return false;
    }
    // cbr_sel is a uimm2 field selecting CBR0/CBR1 (addCircularBufferUse).
    if (!expectUImm(static_cast<int64_t>(CbrSel), 1, "LDW_CB_IMM cbr_sel"))
      return false;
    if (DataReg.isVirtual())
      RBI.constrainGenericRegister(DataReg, DR64RegClass, MRI);
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);
    if (PtrBase.isVirtual())
      RBI.constrainGenericRegister(PtrBase, GPR32RegClass, MRI);

    int64_t StrideImm = 0;
    if (getConstOpSExt(I.getOperand(5), StrideImm)) {
      // Generated members: D_LDW_CB_IMM_*_CBRI (imm8 = simm8 stride).
      if (!expectSImm(StrideImm, 8, "LDW_CB_IMM stride"))
        return false;
      MachineInstr *MI = MIB.buildInstr(D_LDW_CB_IMM)
                             .addDef(DataReg)
                             .addDef(WbReg)
                             .addReg(PtrBase)
                             .addImm(CbrSel)
                             .addImm(StrideImm);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
      addCircularBufferUse(MI, CbrSel);
      I.eraseFromParent();
      return true;
    }
    // Non-constant stride: fall back to REG form if still a vreg.
    if (!I.getOperand(5).isReg())
      return false;
    Register StrideReg = I.getOperand(5).getReg();
    if (StrideReg.isVirtual())
      RBI.constrainGenericRegister(StrideReg, GPR32RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(D_LDW_CB_REG)
                           .addDef(DataReg)
                           .addDef(WbReg)
                           .addImm(CbrSel)
                           .addReg(PtrBase)
                           .addReg(StrideReg);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    addCircularBufferUse(MI, CbrSel);
    I.eraseFromParent();
    return true;
  }
  case haydn_ldw_cb_reg: {
    // MI: rtd, rs1_wb, rs1, cbr_sel, rs2
    Register DataReg = I.getOperand(0).getReg();
    Register WbReg = I.getOperand(1).getReg();
    Register PtrBase = I.getOperand(3).getReg();
    uint64_t CbrSel = 0;
    if (!getConstOpZExt(I.getOperand(4), CbrSel)) {
      LLVM_DEBUG(dbgs() << "LDW_CB_REG: cbr_sel must be a constant\n");
      return false;
    }
    // cbr_sel is a uimm2 field selecting CBR0/CBR1 (addCircularBufferUse).
    if (!expectUImm(static_cast<int64_t>(CbrSel), 1, "LDW_CB_REG cbr_sel"))
      return false;
    if (!I.getOperand(5).isReg())
      return false;
    Register StrideReg = I.getOperand(5).getReg();
    if (DataReg.isVirtual())
      RBI.constrainGenericRegister(DataReg, DR64RegClass, MRI);
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);
    if (PtrBase.isVirtual())
      RBI.constrainGenericRegister(PtrBase, GPR32RegClass, MRI);
    if (StrideReg.isVirtual())
      RBI.constrainGenericRegister(StrideReg, GPR32RegClass, MRI);

    MachineInstr *MI = MIB.buildInstr(D_LDW_CB_REG)
                           .addDef(DataReg)
                           .addDef(WbReg)
                           .addImm(CbrSel)
                           .addReg(PtrBase)
                           .addReg(StrideReg);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    addCircularBufferUse(MI, CbrSel);
    I.eraseFromParent();
    return true;
  }
  case haydn_sdw_cb_imm: {
    // Intrinsic: (data, ptr_base, cbr_sel, stride) -> new_ptr
    // MI: rs_wb, rtd, rs, cbr_sel, imm — tied AGU writeback (B6).
    // DstReg is new_ptr (op0); args at op2..op5 after intrinsic id at op1.
    Register WbReg = I.getOperand(0).getReg();
    Register Data = I.getOperand(2).getReg();
    Register PtrBase = I.getOperand(3).getReg();
    uint64_t CbrSel = 0;
    if (!getConstOpZExt(I.getOperand(4), CbrSel)) {
      LLVM_DEBUG(dbgs() << "SDW_CB_IMM: cbr_sel must be a constant\n");
      return false;
    }
    // cbr_sel is a uimm2 field selecting CBR0/CBR1 (addCircularBufferUse).
    if (!expectUImm(static_cast<int64_t>(CbrSel), 1, "SDW_CB_IMM cbr_sel"))
      return false;
    if (Data.isVirtual())
      RBI.constrainGenericRegister(Data, DR64RegClass, MRI);
    if (PtrBase.isVirtual())
      RBI.constrainGenericRegister(PtrBase, GPR32RegClass, MRI);
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);

    int64_t StrideImm = 0;
    if (getConstOpSExt(I.getOperand(5), StrideImm)) {
      // Generated members: D_SDW_CB_IMM_*_CBRI (imm8 = simm8 stride).
      if (!expectSImm(StrideImm, 8, "SDW_CB_IMM stride"))
        return false;
      MachineInstr *MI = MIB.buildInstr(D_SDW_CB_IMM)
                             .addDef(WbReg)
                             .addImm(CbrSel)
                             .addReg(Data)
                             .addReg(PtrBase)
                             .addImm(StrideImm);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
      addCircularBufferUse(MI, CbrSel);
      I.eraseFromParent();
      return true;
    }
    if (!I.getOperand(5).isReg())
      return false;
    Register StrideReg = I.getOperand(5).getReg();
    if (StrideReg.isVirtual())
      RBI.constrainGenericRegister(StrideReg, GPR32RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(D_SDW_CB_REG)
                           .addDef(WbReg)
                           .addImm(CbrSel)
                           .addReg(Data)
                           .addReg(PtrBase)
                           .addReg(StrideReg);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    addCircularBufferUse(MI, CbrSel);
    I.eraseFromParent();
    return true;
  }
  case haydn_sdw_cb_reg: {
    // MI: rs1_wb, rtd, rs1, cbr_sel, rs2
    // Intrinsic returns new_ptr in op0; args shift by one vs void form.
 // ImmArg cbr_sel may be bare Imm on G_INTRINSIC (); use
    // getConstOpZExt — never getReg() on the ImmArg slot (assert crash).
    Register WbReg = I.getOperand(0).getReg();
    Register Data = I.getOperand(2).getReg();
    Register PtrBase = I.getOperand(3).getReg();
    uint64_t CbrSel = 0;
    if (!getConstOpZExt(I.getOperand(4), CbrSel)) {
      LLVM_DEBUG(dbgs() << "SDW_CB_REG: cbr_sel must be a constant\n");
      return false;
    }
    // cbr_sel is a uimm2 field selecting CBR0/CBR1 (addCircularBufferUse).
    if (!expectUImm(static_cast<int64_t>(CbrSel), 1, "SDW_CB_REG cbr_sel"))
      return false;
    if (!I.getOperand(5).isReg())
      return false;
    Register StrideReg = I.getOperand(5).getReg();
    if (Data.isVirtual())
      RBI.constrainGenericRegister(Data, DR64RegClass, MRI);
    if (PtrBase.isVirtual())
      RBI.constrainGenericRegister(PtrBase, GPR32RegClass, MRI);
    if (StrideReg.isVirtual())
      RBI.constrainGenericRegister(StrideReg, GPR32RegClass, MRI);
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);

    MachineInstr *MI = MIB.buildInstr(D_SDW_CB_REG)
                           .addDef(WbReg)
                           .addImm(CbrSel)
                           .addReg(Data)
                           .addReg(PtrBase)
                           .addReg(StrideReg);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    addCircularBufferUse(MI, CbrSel);
    I.eraseFromParent();
    return true;
  }

  //===---------------------------------------------------------------===
  // Bit-Reversed Addressing Load/Store (golden AGU writeback model)
  // BREV loads:  (ptr, stride) -> {data, new_ptr}   (like CB frexp)
  // BREV stores: (data, ptr, stride) -> new_ptr
  // MI order mirrors CB: live rs_wb is the IR new_ptr def.
  //===---------------------------------------------------------------===
  case haydn_ldw_brev_imm: {
    // Intrinsic: (ptr, imm) -> {i64 data, i32 new_ptr}
    // MI: rtd, rs_wb, rs, imm.
    Register DataReg = I.getOperand(0).getReg();
    Register WbReg = I.getOperand(1).getReg();
    Register PtrBase = I.getOperand(3).getReg();
    int64_t StrideImm = 0;
    if (!getConstOpSExt(I.getOperand(4), StrideImm)) {
      LLVM_DEBUG(dbgs() << "LDW_BREV_IMM: stride must be a constant\n");
      return false;
    }
    // Generated members: D_LDW_BREV_IMM_*_RI6 (simm6 stride field).
    if (!expectSImm(StrideImm, 6, "LDW_BREV_IMM stride"))
      return false;
    if (DataReg.isVirtual())
      RBI.constrainGenericRegister(DataReg, DR64RegClass, MRI);
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);
    if (PtrBase.isVirtual())
      RBI.constrainGenericRegister(PtrBase, GPR32RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(D_LDW_BREV_IMM)
                           .addDef(DataReg)
                           .addDef(WbReg)
                           .addReg(PtrBase)
                           .addImm(StrideImm);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_ldw_brev_reg: {
    // Intrinsic: (ptr, stride_reg) -> {i64 data, i32 new_ptr}
    Register DataReg = I.getOperand(0).getReg();
    Register WbReg = I.getOperand(1).getReg();
    Register PtrBase = I.getOperand(3).getReg();
    Register StrideReg = I.getOperand(4).getReg();
    if (DataReg.isVirtual())
      RBI.constrainGenericRegister(DataReg, DR64RegClass, MRI);
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);
    if (PtrBase.isVirtual())
      RBI.constrainGenericRegister(PtrBase, GPR32RegClass, MRI);
    if (StrideReg.isVirtual())
      RBI.constrainGenericRegister(StrideReg, GPR32RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(D_LDW_BREV_REG)
                           .addDef(DataReg)
                           .addDef(WbReg)
                           .addReg(PtrBase)
                           .addReg(StrideReg);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_lw_brev_imm: {
    // Intrinsic: (ptr, imm) -> {i32 data, i32 new_ptr}
    // MI: rt, rs_wb, rs, imm.
    Register DataReg = I.getOperand(0).getReg();
    Register WbReg = I.getOperand(1).getReg();
    Register PtrBase = I.getOperand(3).getReg();
    int64_t StrideImm = 0;
    if (!getConstOpSExt(I.getOperand(4), StrideImm)) {
      LLVM_DEBUG(dbgs() << "LW_BREV_IMM: stride must be a constant\n");
      return false;
    }
    // Generated members: S_LW_BREV_IMM_*_RI6 (simm6 stride field).
    if (!expectSImm(StrideImm, 6, "LW_BREV_IMM stride"))
      return false;
    if (DataReg.isVirtual())
      RBI.constrainGenericRegister(DataReg, GPR32RegClass, MRI);
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);
    if (PtrBase.isVirtual())
      RBI.constrainGenericRegister(PtrBase, GPR32RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(S_LW_BREV_IMM)
                           .addDef(DataReg)
                           .addDef(WbReg)
                           .addReg(PtrBase)
                           .addImm(StrideImm);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_lw_brev_reg: {
    // Intrinsic: (ptr, stride_reg) -> {i32 data, i32 new_ptr}
    Register DataReg = I.getOperand(0).getReg();
    Register WbReg = I.getOperand(1).getReg();
    Register PtrBase = I.getOperand(3).getReg();
    Register StrideReg = I.getOperand(4).getReg();
    if (DataReg.isVirtual())
      RBI.constrainGenericRegister(DataReg, GPR32RegClass, MRI);
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);
    if (PtrBase.isVirtual())
      RBI.constrainGenericRegister(PtrBase, GPR32RegClass, MRI);
    if (StrideReg.isVirtual())
      RBI.constrainGenericRegister(StrideReg, GPR32RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(S_LW_BREV_REG)
                           .addDef(DataReg)
                           .addDef(WbReg)
                           .addReg(PtrBase)
                           .addReg(StrideReg);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_sdw_brev_imm: {
    // Intrinsic: (i64 data, ptr, imm) -> new_ptr
    // MI: rs_wb, rtd, rs, imm.
    Register WbReg = I.getOperand(0).getReg();
    Register Data = I.getOperand(2).getReg();
    Register PtrBase = I.getOperand(3).getReg();
    int64_t StrideImm = 0;
    if (!getConstOpSExt(I.getOperand(4), StrideImm)) {
      LLVM_DEBUG(dbgs() << "SDW_BREV_IMM: stride must be a constant\n");
      return false;
    }
    // Generated members: D_SDW_BREV_IMM_*_RI6 (simm6 stride field).
    if (!expectSImm(StrideImm, 6, "SDW_BREV_IMM stride"))
      return false;
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);
    if (PtrBase.isVirtual())
      RBI.constrainGenericRegister(PtrBase, GPR32RegClass, MRI);
    if (Data.isVirtual())
      RBI.constrainGenericRegister(Data, DR64RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(D_SDW_BREV_IMM)
                           .addDef(WbReg)
                           .addReg(Data)
                           .addReg(PtrBase)
                           .addImm(StrideImm);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_sdw_brev_reg: {
    // Intrinsic: (i64 data, ptr, stride_reg) -> new_ptr
    Register WbReg = I.getOperand(0).getReg();
    Register Data = I.getOperand(2).getReg();
    Register PtrBase = I.getOperand(3).getReg();
    Register StrideReg = I.getOperand(4).getReg();
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);
    if (PtrBase.isVirtual())
      RBI.constrainGenericRegister(PtrBase, GPR32RegClass, MRI);
    if (Data.isVirtual())
      RBI.constrainGenericRegister(Data, DR64RegClass, MRI);
    if (StrideReg.isVirtual())
      RBI.constrainGenericRegister(StrideReg, GPR32RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(D_SDW_BREV_REG)
                           .addDef(WbReg)
                           .addReg(Data)
                           .addReg(PtrBase)
                           .addReg(StrideReg);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_sw_brev_imm: {
    // Intrinsic: (i32 data, ptr, imm) -> new_ptr
    // MI: rs_wb, rt, rs, imm.
    Register WbReg = I.getOperand(0).getReg();
    Register Data = I.getOperand(2).getReg();
    Register PtrBase = I.getOperand(3).getReg();
    int64_t StrideImm = 0;
    if (!getConstOpSExt(I.getOperand(4), StrideImm)) {
      LLVM_DEBUG(dbgs() << "SW_BREV_IMM: stride must be a constant\n");
      return false;
    }
    // Generated members: S_SW_BREV_IMM_*_RI6 (simm6 stride field).
    if (!expectSImm(StrideImm, 6, "SW_BREV_IMM stride"))
      return false;
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);
    if (Data.isVirtual())
      RBI.constrainGenericRegister(Data, GPR32RegClass, MRI);
    if (PtrBase.isVirtual())
      RBI.constrainGenericRegister(PtrBase, GPR32RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(S_SW_BREV_IMM)
                           .addDef(WbReg)
                           .addReg(Data)
                           .addReg(PtrBase)
                           .addImm(StrideImm);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_sw_brev_reg: {
    // Intrinsic: (i32 data, ptr, stride_reg) -> new_ptr
    Register WbReg = I.getOperand(0).getReg();
    Register Data = I.getOperand(2).getReg();
    Register PtrBase = I.getOperand(3).getReg();
    Register StrideReg = I.getOperand(4).getReg();
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);
    if (Data.isVirtual())
      RBI.constrainGenericRegister(Data, GPR32RegClass, MRI);
    if (PtrBase.isVirtual())
      RBI.constrainGenericRegister(PtrBase, GPR32RegClass, MRI);
    if (StrideReg.isVirtual())
      RBI.constrainGenericRegister(StrideReg, GPR32RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(S_SW_BREV_REG)
                           .addDef(WbReg)
                           .addReg(Data)
                           .addReg(PtrBase)
                           .addReg(StrideReg);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===---------------------------------------------------------------===
  // Golden LS POST/PRE loads — multi-result {data, new_ptr} frexp model.
  // gmir: op(0)=data, op(1)=new_ptr, op(2)=id, op(3)=base, op(4)=off
  // MI:   (outs data, rs_wb), (ins base, off) with Constraints $rs=$rs_wb
  //===---------------------------------------------------------------===
  case haydn_d_ldw_post_imm:
  case haydn_d_ldw_post_reg:
  case haydn_d_ldw_pre_imm:
  case haydn_d_ldw_pre_reg:
  case haydn_d_lw_post_imm:
  case haydn_d_lw_post_reg:
  case haydn_d_lw_pre_imm:
  case haydn_d_lw_pre_reg:
  case haydn_d_lhw_post_imm:
  case haydn_d_lhw_post_reg:
  case haydn_d_lhw_pre_imm:
  case haydn_d_lhw_pre_reg:
  case haydn_s_lw_post_imm:
  case haydn_s_lw_post_reg:
  case haydn_s_lw_pre_imm:
  case haydn_s_lw_pre_reg:
  case haydn_s_lhws_post_imm:
  case haydn_s_lhws_post_reg:
  case haydn_s_lhws_pre_imm:
  case haydn_s_lhws_pre_reg:
  case haydn_s_lhwu_post_imm:
  case haydn_s_lhwu_post_reg:
  case haydn_s_lhwu_pre_imm:
  case haydn_s_lhwu_pre_reg:
  case haydn_s_lbs_post_imm:
  case haydn_s_lbs_post_reg:
  case haydn_s_lbs_pre_imm:
  case haydn_s_lbs_pre_reg:
  case haydn_s_lbu_post_imm:
  case haydn_s_lbu_post_reg:
  case haydn_s_lbu_pre_imm:
  case haydn_s_lbu_pre_reg: {
    Register DataReg = I.getOperand(0).getReg();
    Register WbReg = I.getOperand(1).getReg();
    Register Base = I.getOperand(3).getReg();
    // op4 is ImmArg bare Imm for *_imm, or GPR for *_reg — don't getReg yet.

    unsigned Opc = S_LW_POST_IMM;
    bool IsDR = false;
    bool IsImm = false;
    switch (IntrID) {
    case Intrinsic::haydn_d_ldw_post_imm: Opc = D_LDW_POST_IMM; IsDR = true; IsImm = true; break;
    case Intrinsic::haydn_d_ldw_post_reg: Opc = D_LDW_POST_REG; IsDR = true; break;
    case Intrinsic::haydn_d_ldw_pre_imm:  Opc = D_LDW_PRE_IMM;  IsDR = true; IsImm = true; break;
    case Intrinsic::haydn_d_ldw_pre_reg:  Opc = D_LDW_PRE_REG;  IsDR = true; break;
    case Intrinsic::haydn_d_lw_post_imm:  Opc = D_LW_POST_IMM;  IsDR = true; IsImm = true; break;
    case Intrinsic::haydn_d_lw_post_reg:  Opc = D_LW_POST_REG;  IsDR = true; break;
    case Intrinsic::haydn_d_lw_pre_imm:   Opc = D_LW_PRE_IMM;   IsDR = true; IsImm = true; break;
    case Intrinsic::haydn_d_lw_pre_reg:   Opc = D_LW_PRE_REG;   IsDR = true; break;
    case Intrinsic::haydn_d_lhw_post_imm: Opc = D_LHW_POST_IMM; IsDR = true; IsImm = true; break;
    case Intrinsic::haydn_d_lhw_post_reg: Opc = D_LHW_POST_REG; IsDR = true; break;
    case Intrinsic::haydn_d_lhw_pre_imm:  Opc = D_LHW_PRE_IMM;  IsDR = true; IsImm = true; break;
    case Intrinsic::haydn_d_lhw_pre_reg:  Opc = D_LHW_PRE_REG;  IsDR = true; break;
    case Intrinsic::haydn_s_lw_post_imm:  Opc = S_LW_POST_IMM;  IsImm = true; break;
    case Intrinsic::haydn_s_lw_post_reg:  Opc = S_LW_POST_REG;  break;
    case Intrinsic::haydn_s_lw_pre_imm:   Opc = S_LW_PRE_IMM;   IsImm = true; break;
    case Intrinsic::haydn_s_lw_pre_reg:   Opc = S_LW_PRE_REG;   break;
    case Intrinsic::haydn_s_lhws_post_imm: Opc = S_LHWS_POST_IMM; IsImm = true; break;
    case Intrinsic::haydn_s_lhws_post_reg: Opc = S_LHWS_POST_REG; break;
    case Intrinsic::haydn_s_lhws_pre_imm:  Opc = S_LHWS_PRE_IMM;  IsImm = true; break;
    case Intrinsic::haydn_s_lhws_pre_reg:  Opc = S_LHWS_PRE_REG;  break;
    case Intrinsic::haydn_s_lhwu_post_imm: Opc = S_LHWU_POST_IMM; IsImm = true; break;
    case Intrinsic::haydn_s_lhwu_post_reg: Opc = S_LHWU_POST_REG; break;
    case Intrinsic::haydn_s_lhwu_pre_imm:  Opc = S_LHWU_PRE_IMM;  IsImm = true; break;
    case Intrinsic::haydn_s_lhwu_pre_reg:  Opc = S_LHWU_PRE_REG;  break;
    case Intrinsic::haydn_s_lbs_post_imm: Opc = S_LBS_POST_IMM; IsImm = true; break;
    case Intrinsic::haydn_s_lbs_post_reg: Opc = S_LBS_POST_REG; break;
    case Intrinsic::haydn_s_lbs_pre_imm:  Opc = S_LBS_PRE_IMM;  IsImm = true; break;
    case Intrinsic::haydn_s_lbs_pre_reg:  Opc = S_LBS_PRE_REG;  break;
    case Intrinsic::haydn_s_lbu_post_imm: Opc = S_LBU_POST_IMM; IsImm = true; break;
    case Intrinsic::haydn_s_lbu_post_reg: Opc = S_LBU_POST_REG; break;
    case Intrinsic::haydn_s_lbu_pre_imm:  Opc = S_LBU_PRE_IMM;  IsImm = true; break;
    case Intrinsic::haydn_s_lbu_pre_reg:  Opc = S_LBU_PRE_REG;  break;
    default:
      return false;
    }

    const TargetRegisterClass &DataRC =
        IsDR ? DR64RegClass : GPR32RegClass;
    if (DataReg.isVirtual())
      RBI.constrainGenericRegister(DataReg, DataRC, MRI);
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);
    if (Base.isVirtual())
      RBI.constrainGenericRegister(Base, GPR32RegClass, MRI);

    if (IsImm) {
      int64_t OffImm = 0;
      if (!getConstOpSExt(I.getOperand(4), OffImm))
        return false;
      // Generated members: *_POST_IMM/*_PRE_IMM_*_RI6 (simm6 field).
      if (!expectSImm(OffImm, 6, "POST/PRE load imm offset"))
        return false;
      MachineInstr *MI = MIB.buildInstr(Opc)
                             .addDef(DataReg)
                             .addDef(WbReg)
                             .addReg(Base)
                             .addImm(OffImm);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
    } else {
      if (!I.getOperand(4).isReg())
        return false;
      Register Off = I.getOperand(4).getReg();
      if (Off.isVirtual())
        RBI.constrainGenericRegister(Off, GPR32RegClass, MRI);
      MachineInstr *MI = MIB.buildInstr(Opc)
                             .addDef(DataReg)
                             .addDef(WbReg)
                             .addReg(Base)
                             .addReg(Off);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
    }
    I.eraseFromParent();
    return true;
  }

  //===---------------------------------------------------------------===
  // Golden LS POST/PRE stores — single-ret writeback like sdw_cb:
  //   IR: new_ptr = store(data, base, off)
  //   MI: (outs wb), (ins data, base, off)
  // G_INTRINSIC_W_SIDE_EFFECTS: op0=wb, op1=id, op2=data, op3=base, op4=off.
  // ImmArg offset uses getConstOpSExt (bare Imm after legalize).
  //===---------------------------------------------------------------===
  case haydn_d_sdw_post_imm:
  case haydn_d_sdw_post_reg:
  case haydn_d_sdw_pre_imm:
  case haydn_d_sdw_pre_reg:
  case haydn_d_shw_post_imm:
  case haydn_d_shw_post_reg:
  case haydn_d_shw_pre_imm:
  case haydn_d_shw_pre_reg:
  case haydn_d_sw_h_post_imm:
  case haydn_d_sw_h_post_reg:
  case haydn_d_sw_h_pre_imm:
  case haydn_d_sw_h_pre_reg:
  case haydn_d_sw_l_post_imm:
  case haydn_d_sw_l_post_reg:
  case haydn_d_sw_l_pre_imm:
  case haydn_d_sw_l_pre_reg: {
    // DR64 data writeback stores.
    Register WbReg = I.getOperand(0).getReg();
    Register Data = I.getOperand(2).getReg();
    Register Base = I.getOperand(3).getReg();
    unsigned Opc = D_SDW_POST_IMM;
    bool IsImm = false;
    switch (IntrID) {
    case Intrinsic::haydn_d_sdw_post_imm: Opc = D_SDW_POST_IMM; IsImm = true; break;
    case Intrinsic::haydn_d_sdw_post_reg: Opc = D_SDW_POST_REG; break;
    case Intrinsic::haydn_d_sdw_pre_imm:  Opc = D_SDW_PRE_IMM;  IsImm = true; break;
    case Intrinsic::haydn_d_sdw_pre_reg:  Opc = D_SDW_PRE_REG;  break;
    case Intrinsic::haydn_d_shw_post_imm: Opc = D_SHW_POST_IMM; IsImm = true; break;
    case Intrinsic::haydn_d_shw_post_reg: Opc = D_SHW_POST_REG; break;
    case Intrinsic::haydn_d_shw_pre_imm:  Opc = D_SHW_PRE_IMM;  IsImm = true; break;
    case Intrinsic::haydn_d_shw_pre_reg:  Opc = D_SHW_PRE_REG;  break;
    case Intrinsic::haydn_d_sw_h_post_imm: Opc = D_SW_H_POST_IMM; IsImm = true; break;
    case Intrinsic::haydn_d_sw_h_post_reg: Opc = D_SW_H_POST_REG; break;
    case Intrinsic::haydn_d_sw_h_pre_imm:  Opc = D_SW_H_PRE_IMM;  IsImm = true; break;
    case Intrinsic::haydn_d_sw_h_pre_reg:  Opc = D_SW_H_PRE_REG;  break;
    case Intrinsic::haydn_d_sw_l_post_imm: Opc = D_SW_L_POST_IMM; IsImm = true; break;
    case Intrinsic::haydn_d_sw_l_post_reg: Opc = D_SW_L_POST_REG; break;
    case Intrinsic::haydn_d_sw_l_pre_imm:  Opc = D_SW_L_PRE_IMM;  IsImm = true; break;
    case Intrinsic::haydn_d_sw_l_pre_reg:  Opc = D_SW_L_PRE_REG;  break;
    default:
      return false;
    }
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);
    if (Data.isVirtual())
      RBI.constrainGenericRegister(Data, DR64RegClass, MRI);
    if (Base.isVirtual())
      RBI.constrainGenericRegister(Base, GPR32RegClass, MRI);
    if (IsImm) {
      int64_t OffImm = 0;
      if (!getConstOpSExt(I.getOperand(4), OffImm))
        return false;
      // Generated members: *_POST_IMM/*_PRE_IMM_*_RI6 (simm6 field).
      if (!expectSImm(OffImm, 6, "POST/PRE DR64 store imm offset"))
        return false;
      MachineInstr *MI = MIB.buildInstr(Opc)
                             .addDef(WbReg)
                             .addReg(Data)
                             .addReg(Base)
                             .addImm(OffImm);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
    } else {
      if (!I.getOperand(4).isReg())
        return false;
      Register Off = I.getOperand(4).getReg();
      if (Off.isVirtual())
        RBI.constrainGenericRegister(Off, GPR32RegClass, MRI);
      MachineInstr *MI = MIB.buildInstr(Opc)
                             .addDef(WbReg)
                             .addReg(Data)
                             .addReg(Base)
                             .addReg(Off);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
    }
    I.eraseFromParent();
    return true;
  }
  case haydn_s_sb_post_imm:
  case haydn_s_sb_post_reg:
  case haydn_s_sb_pre_imm:
  case haydn_s_sb_pre_reg:
  case haydn_s_shw_post_imm:
  case haydn_s_shw_post_reg:
  case haydn_s_shw_pre_imm:
  case haydn_s_shw_pre_reg:
  case haydn_s_sw_post_imm:
  case haydn_s_sw_post_reg:
  case haydn_s_sw_pre_imm:
  case haydn_s_sw_pre_reg: {
    // GPR32 data writeback stores.
    Register WbReg = I.getOperand(0).getReg();
    Register Data = I.getOperand(2).getReg();
    Register Base = I.getOperand(3).getReg();
    unsigned Opc = S_SW_POST_IMM;
    bool IsImm = false;
    switch (IntrID) {
    case Intrinsic::haydn_s_sb_post_imm: Opc = S_SB_POST_IMM; IsImm = true; break;
    case Intrinsic::haydn_s_sb_post_reg: Opc = S_SB_POST_REG; break;
    case Intrinsic::haydn_s_sb_pre_imm:  Opc = S_SB_PRE_IMM;  IsImm = true; break;
    case Intrinsic::haydn_s_sb_pre_reg:  Opc = S_SB_PRE_REG;  break;
    case Intrinsic::haydn_s_shw_post_imm: Opc = S_SHW_POST_IMM; IsImm = true; break;
    case Intrinsic::haydn_s_shw_post_reg: Opc = S_SHW_POST_REG; break;
    case Intrinsic::haydn_s_shw_pre_imm:  Opc = S_SHW_PRE_IMM;  IsImm = true; break;
    case Intrinsic::haydn_s_shw_pre_reg:  Opc = S_SHW_PRE_REG;  break;
    case Intrinsic::haydn_s_sw_post_imm: Opc = S_SW_POST_IMM; IsImm = true; break;
    case Intrinsic::haydn_s_sw_post_reg: Opc = S_SW_POST_REG; break;
    case Intrinsic::haydn_s_sw_pre_imm:  Opc = S_SW_PRE_IMM;  IsImm = true; break;
    case Intrinsic::haydn_s_sw_pre_reg:  Opc = S_SW_PRE_REG;  break;
    default:
      return false;
    }
    if (WbReg.isVirtual())
      RBI.constrainGenericRegister(WbReg, GPR32RegClass, MRI);
    if (Data.isVirtual())
      RBI.constrainGenericRegister(Data, GPR32RegClass, MRI);
    if (Base.isVirtual())
      RBI.constrainGenericRegister(Base, GPR32RegClass, MRI);
    if (IsImm) {
      int64_t OffImm = 0;
      if (!getConstOpSExt(I.getOperand(4), OffImm))
        return false;
      // Generated members: *_POST_IMM/*_PRE_IMM_*_RI6 (simm6 field).
      if (!expectSImm(OffImm, 6, "POST/PRE GPR32 store imm offset"))
        return false;
      MachineInstr *MI = MIB.buildInstr(Opc)
                             .addDef(WbReg)
                             .addReg(Data)
                             .addReg(Base)
                             .addImm(OffImm);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
    } else {
      if (!I.getOperand(4).isReg())
        return false;
      Register Off = I.getOperand(4).getReg();
      if (Off.isVirtual())
        RBI.constrainGenericRegister(Off, GPR32RegClass, MRI);
      MachineInstr *MI = MIB.buildInstr(Opc)
                             .addDef(WbReg)
                             .addReg(Data)
                             .addReg(Base)
                             .addReg(Off);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
    }
    I.eraseFromParent();
    return true;
  }

  //===---------------------------------------------------------------===
  // Golden LS WITH_* families (no AGU writeback) — D_* DR loads + S_* GPR.
  // POST/PRE families share opcodes but need writeback defs; WITH is the
  // minimal complete ISel for coverage e2e of non-modifying addressing.
  //===---------------------------------------------------------------===
  case haydn_d_ldw_with_imm:
  case haydn_d_ldw_with_reg:
  case haydn_d_lhw_with_imm:
  case haydn_d_lhw_with_reg:
  case haydn_d_lw_with_imm:
  case haydn_d_lw_with_reg: {
    // gmir load: op(0)=data, op(1)=id, op(2)=base, op(3)=off
    // op3 is ImmArg bare Imm for *_imm, or GPR for *_reg.
    Register Base = I.getOperand(2).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (Base.isVirtual())
      RBI.constrainGenericRegister(Base, GPR32RegClass, MRI);
    unsigned Opc = D_LDW_WITH_IMM;
    switch (IntrID) {
    case Intrinsic::haydn_d_ldw_with_imm: Opc = D_LDW_WITH_IMM; break;
    case Intrinsic::haydn_d_ldw_with_reg: Opc = D_LDW_WITH_REG; break;
    case Intrinsic::haydn_d_lhw_with_imm: Opc = D_LHW_WITH_IMM; break;
    case Intrinsic::haydn_d_lhw_with_reg: Opc = D_LHW_WITH_REG; break;
    case Intrinsic::haydn_d_lw_with_imm: Opc = D_LW_WITH_IMM; break;
    case Intrinsic::haydn_d_lw_with_reg: Opc = D_LW_WITH_REG; break;
    default: break;
    }
    bool IsImm = (IntrID == Intrinsic::haydn_d_ldw_with_imm ||
                  IntrID == Intrinsic::haydn_d_lhw_with_imm ||
                  IntrID == Intrinsic::haydn_d_lw_with_imm);
    if (IsImm) {
      int64_t OffImm = 0;
      if (!getConstOpSExt(I.getOperand(3), OffImm))
        return false;
      // Generated members: D_*_WITH_IMM_*_RI6 (simm6 field).
      if (!expectSImm(OffImm, 6, "D_*_WITH_IMM load offset"))
        return false;
      MachineInstr *MI =
          MIB.buildInstr(Opc).addDef(DstReg).addReg(Base).addImm(OffImm);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
    } else {
      if (!I.getOperand(3).isReg())
        return false;
      Register Off = I.getOperand(3).getReg();
      if (Off.isVirtual())
        RBI.constrainGenericRegister(Off, GPR32RegClass, MRI);
      MachineInstr *MI =
          MIB.buildInstr(Opc).addDef(DstReg).addReg(Base).addReg(Off);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
    }
    I.eraseFromParent();
    return true;
  }
  case haydn_s_lbs_with_imm:
  case haydn_s_lbs_with_reg:
  case haydn_s_lbu_with_imm:
  case haydn_s_lbu_with_reg:
  case haydn_s_lhws_with_imm:
  case haydn_s_lhws_with_reg:
  case haydn_s_lhwu_with_imm:
  case haydn_s_lhwu_with_reg:
  case haydn_s_lw_with_imm:
  case haydn_s_lw_with_reg: {
    Register Base = I.getOperand(2).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, GPR32RegClass, MRI);
    if (Base.isVirtual())
      RBI.constrainGenericRegister(Base, GPR32RegClass, MRI);
    unsigned Opc = S_LW_WITH_IMM;
    switch (IntrID) {
    case Intrinsic::haydn_s_lbs_with_imm: Opc = S_LBS_WITH_IMM; break;
    case Intrinsic::haydn_s_lbs_with_reg: Opc = S_LBS_WITH_REG; break;
    case Intrinsic::haydn_s_lbu_with_imm: Opc = S_LBU_WITH_IMM; break;
    case Intrinsic::haydn_s_lbu_with_reg: Opc = S_LBU_WITH_REG; break;
    case Intrinsic::haydn_s_lhws_with_imm: Opc = S_LHWS_WITH_IMM; break;
    case Intrinsic::haydn_s_lhws_with_reg: Opc = S_LHWS_WITH_REG; break;
    case Intrinsic::haydn_s_lhwu_with_imm: Opc = S_LHWU_WITH_IMM; break;
    case Intrinsic::haydn_s_lhwu_with_reg: Opc = S_LHWU_WITH_REG; break;
    case Intrinsic::haydn_s_lw_with_imm: Opc = S_LW_WITH_IMM; break;
    case Intrinsic::haydn_s_lw_with_reg: Opc = S_LW_WITH_REG; break;
    default: break;
    }
    bool IsImm = (IntrID == Intrinsic::haydn_s_lbs_with_imm ||
                  IntrID == Intrinsic::haydn_s_lbu_with_imm ||
                  IntrID == Intrinsic::haydn_s_lhws_with_imm ||
                  IntrID == Intrinsic::haydn_s_lhwu_with_imm ||
                  IntrID == Intrinsic::haydn_s_lw_with_imm);
    if (IsImm) {
      int64_t OffImm = 0;
      if (!getConstOpSExt(I.getOperand(3), OffImm))
        return false;
      // Generated members: S_*_WITH_IMM_*_RI6 (simm6 field).
      if (!expectSImm(OffImm, 6, "S_*_WITH_IMM load offset"))
        return false;
      MachineInstr *MI =
          MIB.buildInstr(Opc).addDef(DstReg).addReg(Base).addImm(OffImm);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
    } else {
      if (!I.getOperand(3).isReg())
        return false;
      Register Off = I.getOperand(3).getReg();
      if (Off.isVirtual())
        RBI.constrainGenericRegister(Off, GPR32RegClass, MRI);
      MachineInstr *MI =
          MIB.buildInstr(Opc).addDef(DstReg).addReg(Base).addReg(Off);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
    }
    I.eraseFromParent();
    return true;
  }
  case haydn_d_sdw_with_imm:
  case haydn_d_sdw_with_reg:
  case haydn_d_shw_with_imm:
  case haydn_d_shw_with_reg:
  case haydn_d_sw_h_with_imm:
  case haydn_d_sw_h_with_reg:
  case haydn_d_sw_l_with_imm:
  case haydn_d_sw_l_with_reg: {
    // void store: op0=id, op1=data, op2=base, op3=off (G_INTRINSIC_W_SIDE_EFFECTS)
    Register Data = I.getOperand(1).getReg();
    Register Base = I.getOperand(2).getReg();
    if (Data.isVirtual())
      RBI.constrainGenericRegister(Data, DR64RegClass, MRI);
    if (Base.isVirtual())
      RBI.constrainGenericRegister(Base, GPR32RegClass, MRI);
    unsigned Opc = D_SDW_WITH_IMM;
    switch (IntrID) {
    case Intrinsic::haydn_d_sdw_with_imm: Opc = D_SDW_WITH_IMM; break;
    case Intrinsic::haydn_d_sdw_with_reg: Opc = D_SDW_WITH_REG; break;
    case Intrinsic::haydn_d_shw_with_imm: Opc = D_SHW_WITH_IMM; break;
    case Intrinsic::haydn_d_shw_with_reg: Opc = D_SHW_WITH_REG; break;
    case Intrinsic::haydn_d_sw_h_with_imm: Opc = D_SW_H_WITH_IMM; break;
    case Intrinsic::haydn_d_sw_h_with_reg: Opc = D_SW_H_WITH_REG; break;
    case Intrinsic::haydn_d_sw_l_with_imm: Opc = D_SW_L_WITH_IMM; break;
    case Intrinsic::haydn_d_sw_l_with_reg: Opc = D_SW_L_WITH_REG; break;
    default: break;
    }
    bool IsImm = (IntrID == Intrinsic::haydn_d_sdw_with_imm ||
                  IntrID == Intrinsic::haydn_d_shw_with_imm ||
                  IntrID == Intrinsic::haydn_d_sw_h_with_imm ||
                  IntrID == Intrinsic::haydn_d_sw_l_with_imm);
    if (IsImm) {
      int64_t OffImm = 0;
      if (!getConstOpSExt(I.getOperand(3), OffImm))
        return false;
      // Generated members: D_*_WITH_IMM_*_RI6 (simm6 field).
      if (!expectSImm(OffImm, 6, "D_*_WITH store offset"))
        return false;
      MachineInstr *MI =
          MIB.buildInstr(Opc).addReg(Data).addReg(Base).addImm(OffImm);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
    } else {
      if (!I.getOperand(3).isReg())
        return false;
      Register Off = I.getOperand(3).getReg();
      if (Off.isVirtual())
        RBI.constrainGenericRegister(Off, GPR32RegClass, MRI);
      MachineInstr *MI =
          MIB.buildInstr(Opc).addReg(Data).addReg(Base).addReg(Off);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
    }
    I.eraseFromParent();
    return true;
  }
  case haydn_s_sb_with_imm:
  case haydn_s_sb_with_reg:
  case haydn_s_shw_with_imm:
  case haydn_s_shw_with_reg:
  case haydn_s_sw_with_imm:
  case haydn_s_sw_with_reg: {
    Register Data = I.getOperand(1).getReg();
    Register Base = I.getOperand(2).getReg();
    if (Data.isVirtual())
      RBI.constrainGenericRegister(Data, GPR32RegClass, MRI);
    if (Base.isVirtual())
      RBI.constrainGenericRegister(Base, GPR32RegClass, MRI);
    unsigned Opc = S_SW_WITH_IMM;
    switch (IntrID) {
    case Intrinsic::haydn_s_sb_with_imm: Opc = S_SB_WITH_IMM; break;
    case Intrinsic::haydn_s_sb_with_reg: Opc = S_SB_WITH_REG; break;
    case Intrinsic::haydn_s_shw_with_imm: Opc = S_SHW_WITH_IMM; break;
    case Intrinsic::haydn_s_shw_with_reg: Opc = S_SHW_WITH_REG; break;
    case Intrinsic::haydn_s_sw_with_imm: Opc = S_SW_WITH_IMM; break;
    case Intrinsic::haydn_s_sw_with_reg: Opc = S_SW_WITH_REG; break;
    default: break;
    }
    bool IsImm = (IntrID == Intrinsic::haydn_s_sb_with_imm ||
                  IntrID == Intrinsic::haydn_s_shw_with_imm ||
                  IntrID == Intrinsic::haydn_s_sw_with_imm);
    if (IsImm) {
      int64_t OffImm = 0;
      if (!getConstOpSExt(I.getOperand(3), OffImm))
        return false;
      // Generated members: S_*_WITH_IMM_*_RI6 (simm6 field).
      if (!expectSImm(OffImm, 6, "S_*_WITH store offset"))
        return false;
      MachineInstr *MI =
          MIB.buildInstr(Opc).addReg(Data).addReg(Base).addImm(OffImm);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
    } else {
      if (!I.getOperand(3).isReg())
        return false;
      Register Off = I.getOperand(3).getReg();
      if (Off.isVirtual())
        RBI.constrainGenericRegister(Off, GPR32RegClass, MRI);
      MachineInstr *MI =
          MIB.buildInstr(Opc).addReg(Data).addReg(Base).addReg(Off);
      if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
        return false;
    }
    I.eraseFromParent();
    return true;
  }

  //===---------------------------------------------------------------===
  // AR unaligned stream (golden LS #103-#109).
  // emit **logical** opcodes only (PLDWWUA / FLAR / …). Slot is NOT in
  // the opcode — post-RA HR auction writes AltDescs; leaveRegion setDesc
  // commits private _S0/_S1/_S2 members (Desc-as-is at encode). Hardwiring
  // *_S0 made dual AR ops look exclusive-S0 and oversubscribed the cycle.
  // ar_sel / dir_sel must be compile-time constants (uimm2 / uimm1).
  //
  // Pointer contract: HW AGU post-inc is lowered as a *dead* tied-def so the
  // MC shape is correct. The IR/C cursor is ordinary ptr arithmetic (not HW
  // writeback readback). SCEV only needs the C/IR next-ptr GEP chain.
  //===---------------------------------------------------------------===
  case haydn_pldwwua: {
    // void pldwwua(ar_sel, ptr) — G_INTRINSIC_W_SIDE_EFFECTS:
    // op(0)=id, op(1)=ar_sel ImmArg, op(2)=ptr. Writes AR[ar_sel].
    // Product ISel admits AR0/AR1 only. Encoding 2/3 is unmapped.
    uint64_t ArSel = 0;
    Register PtrReg = I.getOperand(2).getReg();
    if (!getConstOpZExt(I.getOperand(1), ArSel) || ArSel > 1) {
      LLVM_DEBUG(dbgs() << "PLDWWUA: ar_sel must be constant 0 or 1\n");
      return false;
    }
    if (PtrReg.isVirtual())
      RBI.constrainGenericRegister(PtrReg, GPR32RegClass, MRI);
    // Logical dag is (uimm2 ar_sel, GPR32 rs) — golden wire order so
    // occupancy binds PLDWWUA_POST members positionally.
    static const MCPhysReg ArRegs[] = {Haydn::AR0, Haydn::AR1};
    MachineInstr *MI = MIB.buildInstr(PLDWWUA)
                           .addImm(static_cast<int64_t>(ArSel))
                           .addReg(PtrReg)
                           .addDef(ArRegs[ArSel], RegState::Implicit);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_flar: {
    // void flar(ar_sel) — op(0)=id, op(1)=ar_sel ImmArg
    // Model Def of ARn so PostRA pack cannot co-issue two AR writers on the
    // same stream (AR WRITE_CONFLICT). Product ISel admits AR0/AR1 only.
    uint64_t ArSel = 0;
    if (!getConstOpZExt(I.getOperand(1), ArSel) || ArSel > 1) {
      LLVM_DEBUG(dbgs() << "FLAR: ar_sel must be constant 0 or 1\n");
      return false;
    }
    static const MCPhysReg ArRegs[] = {Haydn::AR0, Haydn::AR1};
    MachineInstr *MI =
        MIB.buildInstr(FLAR)
            .addImm(static_cast<int64_t>(ArSel))
            .addDef(ArRegs[ArSel], RegState::Implicit);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_wbarwua: {
    // void wbarwua(ar_sel, ptr, dir_sel) — ar_sel/dir ImmArg.
    // op(0)=id, op(1)=ar_sel, op(2)=ptr, op(3)=dir_sel
    uint64_t ArSel = 0, DirSel = 0;
    Register PtrReg = I.getOperand(2).getReg();
    if (!getConstOpZExt(I.getOperand(1), ArSel) || ArSel > 1 ||
        !getConstOpZExt(I.getOperand(3), DirSel) || DirSel > 1) {
      LLVM_DEBUG(dbgs() << "WBARWUA: ar_sel/dir_sel must be constant 0/1\n");
      return false;
    }
    if (PtrReg.isVirtual())
      RBI.constrainGenericRegister(PtrReg, GPR32RegClass, MRI);
    // Logical: (outs), (ins GPR32:$rs, uimm2:$ar_sel, uimm1:$dir_sel)
    static const MCPhysReg ArRegs[] = {Haydn::AR0, Haydn::AR1};
    MachineInstr *MI = MIB.buildInstr(WBARWUA)
                           .addReg(PtrReg)
                           .addImm(static_cast<int64_t>(ArSel))
                           .addImm(static_cast<int64_t>(DirSel))
                           .addDef(ArRegs[ArSel], RegState::Implicit)
                           .addUse(ArRegs[ArSel],
                                   RegState::Implicit | RegState::Undef);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_d_lqhwua_post:
  case haydn_d_ltwua_post: {
    // i64 load(ptr, ar_sel, stride, dir_sel)
    // MC: [rtd, rs1_wb, rs1, rs2, ar_sel, dir_sel]
    // C next-ptr is IR GEP (haydn_dsp.h); HW base writeback stays Dead for MC
    // shape. Implicit AR Def so PostRA cannot pack two same-stream UA/FLAR
    // ops. Product ISel admits ar_sel {0,1}; 2/3 stay unmapped.
    unsigned Opc = (IntrID == haydn_d_lqhwua_post) ? D_LQHWUA_POST
                                                   : D_LTWUA_POST;
    Register PtrReg = I.getOperand(2).getReg();
    Register StrideReg = I.getOperand(4).getReg();
    uint64_t ArSel = 0, DirSel = 0;
    if (!getConstOpZExt(I.getOperand(3), ArSel) || ArSel > 1 ||
        !getConstOpZExt(I.getOperand(5), DirSel) || DirSel > 1) {
      LLVM_DEBUG(dbgs() << "D_*UA_POST load: ar_sel/dir_sel must be 0 or 1\n");
      return false;
    }
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (PtrReg.isVirtual())
      RBI.constrainGenericRegister(PtrReg, GPR32RegClass, MRI);
    if (StrideReg.isVirtual())
      RBI.constrainGenericRegister(StrideReg, GPR32RegClass, MRI);
    static const MCPhysReg ArRegs[] = {Haydn::AR0, Haydn::AR1};
    Register WbReg = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
    MachineInstr *MI = MIB.buildInstr(Opc)
                           .addDef(DstReg)
                           .addDef(WbReg, RegState::Dead)
                           .addReg(PtrReg)
                           .addReg(StrideReg)
                           .addImm(static_cast<int64_t>(ArSel))
                           .addImm(static_cast<int64_t>(DirSel))
                           .addDef(ArRegs[ArSel], RegState::Implicit)
                           .addUse(ArRegs[ArSel],
                                   RegState::Implicit | RegState::Undef);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_d_sqhwua_post:
  case haydn_d_stwua_post: {
    // void store(data, ptr, ar_sel, stride, dir_sel)
    // MC: [rs1_wb, rtd, rs1, rs2, ar_sel, dir_sel] + implicit AR write
    unsigned Opc = (IntrID == haydn_d_sqhwua_post) ? D_SQHWUA_POST
                                                   : D_STWUA_POST;
    Register DataReg = I.getOperand(1).getReg();
    Register PtrReg = I.getOperand(2).getReg();
    Register StrideReg = I.getOperand(4).getReg();
    uint64_t ArSel = 0, DirSel = 0;
    if (!getConstOpZExt(I.getOperand(3), ArSel) || ArSel > 1 ||
        !getConstOpZExt(I.getOperand(5), DirSel) || DirSel > 1) {
      LLVM_DEBUG(dbgs() << "D_*UA_POST store: ar_sel/dir_sel must be 0 or 1\n");
      return false;
    }
    if (DataReg.isVirtual())
      RBI.constrainGenericRegister(DataReg, DR64RegClass, MRI);
    if (PtrReg.isVirtual())
      RBI.constrainGenericRegister(PtrReg, GPR32RegClass, MRI);
    if (StrideReg.isVirtual())
      RBI.constrainGenericRegister(StrideReg, GPR32RegClass, MRI);
    static const MCPhysReg ArRegs[] = {Haydn::AR0, Haydn::AR1};
    Register WbReg = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
    MachineInstr *MI = MIB.buildInstr(Opc)
                           .addDef(WbReg, RegState::Dead)
                           .addReg(DataReg)
                           .addReg(PtrReg)
                           .addReg(StrideReg)
                           .addImm(static_cast<int64_t>(ArSel))
                           .addImm(static_cast<int64_t>(DirSel))
                           .addDef(ArRegs[ArSel], RegState::Implicit)
                           .addUse(ArRegs[ArSel],
                                   RegState::Implicit | RegState::Undef);
    if (!constrainSelectedMemInst(MI, I, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===---------------------------------------------------------------===
  // Wave 5: X2/X4 SIMD non-saturating arithmetic (binary DR64)
  //===---------------------------------------------------------------===
  case haydn_x2add32: return selectBinary(X2ADD32, DR64RegClass);
  case haydn_x2sub32: return selectBinary(X2SUB32, DR64RegClass);
  case haydn_x4add16: return selectBinary(X4ADD16, DR64RegClass);
  case haydn_x4sub16: return selectBinary(X4SUB16, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 5: X2 SIMD HLLH cross-lane variants (binary DR64)
  //===---------------------------------------------------------------===
  case haydn_x2add32_hllh:  return selectBinary(X2ADD32_HLLH,  DR64RegClass);
  case haydn_x2add32s_hllh: return selectBinary(X2ADD32S_HLLH, DR64RegClass);
  case haydn_x2sub32_hllh:  return selectBinary(X2SUB32_HLLH,  DR64RegClass);
  case haydn_x2sub32s_hllh: return selectBinary(X2SUB32S_HLLH, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 5: X2/X4 SIMD register shifts
  // Per spec: dst=DR64, src=DR64, shift_amount=GPR32.
  // The intrinsic uses (i64, i64)->i64, so the shift amount (i64) must
  // be truncated to i32 (GPR32) before emitting the machine instruction.
  //===---------------------------------------------------------------===
  case haydn_x2sll32: return selectDR64ShiftGPR32(X2SLL32);
  case haydn_x2sra32: return selectDR64ShiftGPR32(X2SRA32);
  case haydn_x2srl32: return selectDR64ShiftGPR32(X2SRL32);
  case haydn_x4sll16: return selectDR64ShiftGPR32(X4SLL16);
  case haydn_x4sra16: return selectDR64ShiftGPR32(X4SRA16);
  case haydn_x4srl16: return selectDR64ShiftGPR32(X4SRL16);

  //===---------------------------------------------------------------===
  // Wave 5: X2/X4 SIMD immediate shifts (DR64 src, i32 imm) -> DR64
  //===---------------------------------------------------------------===
  case haydn_x2slli32:
  case haydn_x2srai32:
  case haydn_x2srli32:
  case haydn_x4slli16:
  case haydn_x4srai16:
  case haydn_x4srli16: {
 // ImmArg shift amount: bare Imm or G_CONSTANT vreg.
    Register Src = I.getOperand(2).getReg();
    int64_t ShiftVal = 0;
    if (!getConstOpSExt(I.getOperand(3), ShiftVal)) {
      LLVM_DEBUG(dbgs() << "X2/X4 imm shift: amount must be a constant\n");
      return false;
    }
    // Generated members: X2* = RI5 (uimm5, 32-bit lanes), X4* = RI4
    // (uimm4, 16-bit lanes).
    if (!expectUImm(ShiftVal, IntrID == Intrinsic::haydn_x4slli16 ||
                                   IntrID == Intrinsic::haydn_x4srai16 ||
                                   IntrID == Intrinsic::haydn_x4srli16
                               ? 4
                               : 5,
                    "X2/X4 imm shift"))
      return false;
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, DR64RegClass, MRI);
    unsigned Opc = X2SLLI32;
    switch (IntrID) {
    case Intrinsic::haydn_x2slli32: Opc = X2SLLI32; break;
    case Intrinsic::haydn_x2srai32: Opc = X2SRAI32; break;
    case Intrinsic::haydn_x2srli32: Opc = X2SRLI32; break;
    case Intrinsic::haydn_x4slli16: Opc = X4SLLI16; break;
    case Intrinsic::haydn_x4srai16: Opc = X4SRAI16; break;
    case Intrinsic::haydn_x4srli16: Opc = X4SRLI16; break;
    default:
      llvm_unreachable("unexpected X2/X4 imm shift");
    }
    MachineInstr *ShiftMI =
        MIB.buildInstr(Opc).addDef(DstReg).addReg(Src).addImm(ShiftVal);
    if (!constrainSelectedInstRegOperands(*ShiftMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }

  //===---------------------------------------------------------------===
  // Wave 5: X2/X4 SIMD shift with rounding
  // Same mixed-register layout: dst=DR64, src=DR64, shift_amount=GPR32.
  //===---------------------------------------------------------------===
  case haydn_x2sra32r: return selectDR64ShiftGPR32(X2SRA32R);
  case haydn_x4sra16r: return selectDR64ShiftGPR32(X4SRA16R);

  //===---------------------------------------------------------------===
  // Wave 5: X2/X4 SIMD horizontal reductions (unary DR64)
  //===---------------------------------------------------------------===
  case haydn_x2hadd32_h:  return selectUnary(X2HADD32_H,  DR64RegClass);
  case haydn_x2hadd32s_h: return selectUnary(X2HADD32S_H, DR64RegClass);
  case haydn_x2hadd32_l:  return selectUnary(X2HADD32_L,  DR64RegClass);
  case haydn_x2hadd32s_l: return selectUnary(X2HADD32S_L, DR64RegClass);
  case haydn_x4hadd16_h:  return selectUnary(X4HADD16_H,  DR64RegClass);
  case haydn_x4hadd16_l:  return selectUnary(X4HADD16_L,  DR64RegClass);
  // R_GD horizontal max/min: DR64 src → GPR32 dst (not unary DR64).
  case haydn_x2hmax32: return selectUnaryR_GD(X2HMAX32);
  case haydn_x2hmin32: return selectUnaryR_GD(X2HMIN32);
  case haydn_x4hmax16: return selectUnaryR_GD(X4HMAX16);
  case haydn_x4hmin16: return selectUnaryR_GD(X4HMIN16);

  //===---------------------------------------------------------------===
  // Wave 5: X2/X4 SIMD dot product (binary DR64)
  //===---------------------------------------------------------------===
  case haydn_x2dot32: return selectBinary(X2DOT32, DR64RegClass);
  case haydn_x4dot16: return selectBinary(X4DOT16, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 5: X2/X4 SIMD multiply (ternary DR64)
  //===---------------------------------------------------------------===
  case haydn_x2mul32: return selectSimdMac2Dest(X2MUL32);
  case haydn_x4mul16: return selectSimdMac2Dest(X4MUL16);

  //===---------------------------------------------------------------===
  // Wave 5: X2 SIMD multiply-pair variants (binary DR64)
  //===---------------------------------------------------------------===
  case haydn_x2mulph32:  return selectBinary(X2MULPH32,  DR64RegClass);
  case haydn_x2mulpl32:  return selectBinary(X2MULPL32,  DR64RegClass);
  // Golden MAC forms: rtd is accumulator (tied-def). Ternary + selectAccMAC.
  case haydn_x2mulaph32: return selectAccMAC(X2MULAPH32);
  case haydn_x2mulapl32: return selectAccMAC(X2MULAPL32);
  case haydn_x2mulsph32: return selectAccMAC(X2MULSPH32);
  case haydn_x2mulspl32: return selectAccMAC(X2MULSPL32);

  //===---------------------------------------------------------------===
  // Wave 5: X2 32-bit complex fractional multiply
  // X2FCMUL* (pure 2-in mul): 2-arg. X2FCMULA* (acc-form): 3-arg.
  //===---------------------------------------------------------------===
  case haydn_x2fcmul32rs:  return selectBinary(X2FCMUL32RS,  DR64RegClass);
  case haydn_x2fcmul32rss: return selectBinary(X2FCMUL32RSS, DR64RegClass);
  case haydn_x2fcmula32rs:  return selectAccMAC(X2FCMULA32RS);
  case haydn_x2fcmula32rss: return selectAccMAC(X2FCMULA32RSS);

  //===---------------------------------------------------------------===
  // Wave 5: X2 CMUL F2 variants (ternary DR64)
  //===---------------------------------------------------------------===
  case haydn_x2cmul32_f2:  return selectSimdMac2Dest(X2CMUL32_F2);
  case haydn_x2cmul32s_f2: return selectSimdMac2Dest(X2CMUL32S_F2);

  //===---------------------------------------------------------------===
  // Wave 5: X2 FF2 shift variants (binary DR64)
  //===---------------------------------------------------------------===
  case haydn_x2ff2rsst32: return selectBinary(X2FF2RSST32, DR64RegClass);
  case haydn_x2ff2rst32:  return selectBinary(X2FF2RST32,  DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 5: X4 FF2MUL variants (ternary DR64)
  //===---------------------------------------------------------------===
  case haydn_x4ff2mul16s:  return selectSimdMac2Dest(X4FF2MUL16S);
  case haydn_x4ff2mula16s: return selectSimdMacAcc2Dest(X4FF2MULA16S);
  case haydn_x4ff2muls16s: return selectSimdMacAcc2Dest(X4FF2MULS16S);

  //===---------------------------------------------------------------===
  // Wave 5: X4 pack/sat (binary DR64)
  //===---------------------------------------------------------------===
  case haydn_x4sat32t16: return selectBinary(X4SAT32T16, DR64RegClass);

  //===---------------------------------------------------------------===
  // Wave 5: X4SELI16 / X4SEL16 — per-lane 16-bit select
  // (DR64 src0, DR64 src1, i32 mask) -> DR64
  // Imm form (X4SELI16 / haydn_x4seli16): mask is a uimm4 constant.
  // Reg form (X4SEL16 / haydn_x4sel16): mask lives in a GPR32.
  // Both are first-class ISA encodings — not expand paths.
  //===---------------------------------------------------------------===
  case haydn_x4seli16:
  case haydn_x4sel16: {
    Register Src0 = I.getOperand(2).getReg();
    Register Src1 = I.getOperand(3).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, DR64RegClass, MRI);
    if (Src0.isVirtual())
      RBI.constrainGenericRegister(Src0, DR64RegClass, MRI);
    if (Src1.isVirtual())
      RBI.constrainGenericRegister(Src1, DR64RegClass, MRI);

    MachineInstr *SelMI = nullptr;
 // haydn_x4seli16: ImmArg mask (uimm4 bare Imm after ).
    // haydn_x4sel16: always reg form (explicit reg API).
    if (IntrID == Intrinsic::haydn_x4seli16) {
      uint64_t ImmVal = 0;
      if (!getConstOpZExt(I.getOperand(4), ImmVal) || ImmVal > 15) {
        LLVM_DEBUG(dbgs() << "X4SELI16: mask must be constant uimm4\n");
        return false;
      }
      SelMI = MIB.buildInstr(X4SELI16)
                  .addDef(DstReg)
                  .addReg(Src0)
                  .addReg(Src1)
                  .addImm(static_cast<int64_t>(ImmVal));
    } else {
      Register MaskReg = I.getOperand(4).getReg();
      if (MaskReg.isVirtual())
        RBI.constrainGenericRegister(MaskReg, GPR32RegClass, MRI);
      SelMI = MIB.buildInstr(X4SEL16)
                  .addDef(DstReg)
                  .addReg(Src0)
                  .addReg(Src1)
                  .addReg(MaskReg);
    }
    if (!constrainSelectedInstRegOperands(*SelMI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  //===---------------------------------------------------------------===
  // Phase 3A A5 gap fixes: arctan, popcount32, popcount64.
  //===---------------------------------------------------------------===
  case haydn_popcount32: {
    // POPCOUNT32 rt, rs1 — GPR32 -> GPR32 unary.
    Register Src = I.getOperand(2).getReg();
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, GPR32RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, GPR32RegClass, MRI);
    MachineInstr *MI = MIB.buildInstr(POPCOUNT32).addDef(DstReg).addReg(Src);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  case haydn_popcount64:
    // POPCOUNT64 rt, rsd — R_GD cross-bank DR64 src -> GPR32 dst.
    return selectUnaryR_GD(POPCOUNT64);
  case haydn_arctan: {
 // ARCTAN rt, rsd, uimm4 — GPR32 dst, DR64 src, ImmArg uimm4.
    Register Src = I.getOperand(2).getReg();
    int64_t ImmVal = 0;
    if (!getConstOpSExt(I.getOperand(3), ImmVal))
      return false;
    // Golden: uimm4 (RI4_GD member), not the td stub simm16.
    if (!expectUImm(ImmVal, 4, "ARCTAN"))
      return false;
    if (DstReg.isVirtual())
      RBI.constrainGenericRegister(DstReg, GPR32RegClass, MRI);
    if (Src.isVirtual())
      RBI.constrainGenericRegister(Src, DR64RegClass, MRI);
    MachineInstr *MI =
        MIB.buildInstr(ARCTAN).addDef(DstReg).addReg(Src).addImm(ImmVal);
    if (!constrainSelectedInstRegOperands(*MI, TII, TRI, RBI))
      return false;
    I.eraseFromParent();
    return true;
  }
  }

  LLVM_DEBUG(dbgs() << "Unhandled G_INTRINSIC: " << IntrID << "\n");
  return false;
}

namespace llvm {

// NOLINTNEXTLINE: public factory function, must be in llvm namespace
InstructionSelector *
createHaydnInstructionSelector(const HaydnSubtarget &ST,
                               const HaydnRegisterBankInfo &RBI) {
  return new HaydnInstructionSelector(ST, RBI);
}

} // end namespace llvm
