//===-- HaydnFrameLowering.cpp - Haydn Frame Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Haydn implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "HaydnFrameLowering.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnPostRAScratch.h"
#include "HaydnRegisterInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMatInt.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Type.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "haydn-frame-lowering"

namespace llvm {
/// Shared with expandPostRAPseudo (HaydnInstrInfo.cpp). True iff LOADI64 /
/// MOV_GPR_TO_DR64 will pack through DR64PackFI.
bool haydnInstrNeedsDR64PackSlot(const MachineInstr &MI);
} // namespace llvm

using namespace llvm;

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

// Materialise a 32-bit absolute constant into \p Dst via HaydnMatInt.
// LUI is imm12→bits[31:20] (<<20); never LUI+(<<16)+ORI.
static void emitMaterializeImm32(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MBBI,
                                 const DebugLoc &DL, const HaydnInstrInfo *TII,
                                 Register Dst, int64_t Imm,
                                 MachineInstr::MIFlag FrameFlag) {
  // Large-frame SP adjust seeds MatInt from R0. Local restore + assert so
  // a dirty soft-zero cannot produce a wrong SUB32/ADD32 size. Carry the
  // caller's FrameSetup/Destroy flag so a shrink-wrap restore stays in the
  // PEI CFI transaction.
  ensureSoftZeroR0Clean(MBB, MBBI, DL, *TII, FrameFlag);
  HaydnMatInt::InstSeq Seq = HaydnMatInt::generate(Imm);
  Register Current = Haydn::R0;
  for (const HaydnMatInt::Inst &Inst : Seq) {
    // LUI is dest+imm (logical matches Format E members).
    MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, DL, TII->get(Inst.Opc), Dst);
    if (Inst.Opc != Haydn::LUI)
      MIB.addReg(Current);
    MIB.addImm(Inst.Imm).setMIFlag(FrameFlag);
    Current = Dst;
  }
}

// True when emitPrologue/emitEpilogue must MatInt a stack size into a scratch
// (SUBI32/ADDI32 simm16 does not fit). Used by canUseAsPrologue/Epilogue so
// shrink-wrap will not commit a save point that PEI cannot satisfy.
static bool peiRequiresScratch(const MachineFunction &MF) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t Est = MFI.getStackSize();
  uint64_t Est2 = MFI.estimateStackSize(MF);
  if (Est2 > Est)
    Est = Est2;
  return !isInt<16>(static_cast<int64_t>(Est));
}

// PEI post-RA scratch at insertion point I, not function entry live-ins.
//
// Peer overlay:
//   AArch64FrameLowering.cpp:888-929 findScratchNonCalleeSaveRegister
//     (LivePhysRegs + CSR-as-used so ShrinkWrap canUseAsPrologue and
//      emitPrologue see the same set)
//   PPCFrameLowering.cpp:425-525 findScratchRegister
//     (RegScavenger at block start / first terminator for non-entry)
//
// ABI-safe = call-clobbered GPR (not CSRs / R0 / R14 / reserved / Avoid).
// ProtectRetCC (epilogue): also exclude RetCC R1/R2.
// Returns Register() if none is free at I — never a live/used register.
static Register getPEIScratchReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I, Register Avoid,
                                 Register Avoid2 = Register(),
                                 bool ProtectRetCC = false) {
  MachineFunction &MF = *MBB.getParent();
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const HaydnRegisterInfo *TRI = ST.getRegisterInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const MCPhysReg *CSRs = TRI->getCalleeSavedRegs(&MF);

  auto isCalleeSavedPhys = [&](MCPhysReg Reg) {
    for (const MCPhysReg *P = CSRs; *P; ++P)
      if (TRI->regsOverlap(*P, Reg))
        return true;
    return false;
  };

  auto isRetCCPhys = [](MCPhysReg Reg) {
    return Reg == Haydn::R1 || Reg == Haydn::R2;
  };

  // Sibcall outgoing args live in R1–R7 (CC_Haydn). Epilogue scratch at a
  // JAL_W_MSP / JALR_W_MSP must not clobber them. Peer: AArch64
  // findScratchNonCalleeSaveRegister (AArch64FrameLowering.cpp:888-929)
  // plus HaydnOutgoingValueHandler implicit uses on the tail opcode.
  auto isTailCallArgPhys = [](MCPhysReg Reg) {
    return Reg == Haydn::R1 || Reg == Haydn::R2 || Reg == Haydn::R3 ||
           Reg == Haydn::R4 || Reg == Haydn::R5 || Reg == Haydn::R6 ||
           Reg == Haydn::R7;
  };

  auto isABISafeScratch = [&](MCPhysReg Reg) {
    if (Reg == Avoid || Reg == Avoid2 || Reg == Haydn::R0 || Reg == Haydn::R14)
      return false;
    if (ProtectRetCC && isRetCCPhys(Reg))
      return false;
    if (ProtectRetCC && MF.getFrameInfo().hasTailCall() &&
        isTailCallArgPhys(Reg))
      return false;
    if (MRI.isReserved(Reg))
      return false;
    if (isCalleeSavedPhys(Reg))
      return false;
    return Haydn::GPR32NoSPNoLRRegClass.contains(Reg);
  };

  // Liveness at I (HaydnPostRAScratch.cpp:101-106 / LivePhysRegs stepBackward).
  LivePhysRegs LiveRegs(*TRI);
  LiveRegs.addLiveOuts(MBB);
  for (MachineBasicBlock::iterator II = MBB.end(); II != I;) {
    --II;
    LiveRegs.stepBackward(*II);
  }
  // CSRs may look free during ShrinkWrap canUseAsPrologue but PEI later
  // adds them as live-in (AArch64FrameLowering.cpp:888-896).
  for (const MCPhysReg *P = CSRs; *P; ++P)
    LiveRegs.addReg(*P);

  for (MCPhysReg Reg : Haydn::GPR32NoSPNoLRRegClass)
    if (isABISafeScratch(Reg) && LiveRegs.available(MRI, Reg))
      return Reg;

  return Register();
}

static Register requirePEIScratchReg(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I,
                                     Register Avoid,
                                     Register Avoid2 = Register(),
                                     bool ProtectRetCC = false) {
  Register Reg = getPEIScratchReg(MBB, I, Avoid, Avoid2, ProtectRetCC);
  if (!Reg)
    report_fatal_error(
        "Haydn PEI: no ABI-safe scratch GPR at insertion point "
        "(need call-clobbered free reg; refuse used-reg fallback)");
  return Reg;
}

static int64_t getCalleeSavedCFAOffset(const MachineFunction &MF, int FI) {
  return MF.getFrameInfo().getObjectOffset(FI);
}

// One CSR slot collector for prologue stores and epilogue loads. Peer:
// RISCVFrameLowering.cpp storeRegToStackSlot / loadRegFromStackSlot look
// up each CSI FI once; AIE AIEBaseFrameLowering.cpp:218 spillCalleeSaved
// is the same single walk. Haydn overlay: SP-relative address after the
// prologue subtract (FP is not live yet; epilogue restores before add SP).
enum class CSRBank { GPR32, DR64 };

struct CSRSlot {
  Register Reg;
  Register BaseReg;
  int Offset;
};

static bool isCSRBankReg(const MachineRegisterInfo &MRI,
                         const HaydnRegisterInfo *TRI, Register Reg,
                         CSRBank Bank) {
  const TargetRegisterClass *RC = Reg.isVirtual()
                                      ? MRI.getRegClassOrNull(Reg)
                                      : TRI->getMinimalPhysRegClass(Reg);
  if (!RC)
    return false;
  if (Bank == CSRBank::GPR32)
    return RC == &Haydn::GPR32RegClass || RC == &Haydn::GPR32NoSPNoLRRegClass;
  return RC == &Haydn::DR64RegClass;
}

static void collectCalleeSavedSlots(const HaydnFrameLowering &TFL,
                                    const MachineFunction &MF, CSRBank Bank,
                                    SmallVectorImpl<CSRSlot> &Out) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const HaydnRegisterInfo *TRI =
      MF.getSubtarget<HaydnSubtarget>().getRegisterInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  for (const CalleeSavedInfo &CI : llvm::reverse(MFI.getCalleeSavedInfo())) {
    Register Reg = CI.getReg();
    if (!isCSRBankReg(MRI, TRI, Reg, Bank))
      continue;
    Register FrameReg;
    int Offset = static_cast<int>(
        TFL.getFrameIndexReference(MF, CI.getFrameIdx(), FrameReg).getFixed());
    // getFrameIndexReference already returns SP-relative for the CSI FI
    // range. Fold StackSize only if that path fell through to FP.
    if (FrameReg != Haydn::R13)
      Offset += static_cast<int>(MFI.getStackSize());
    Out.push_back({Reg, Haydn::R13, Offset});
  }
}

// Offset temp for out-of-range CSR st/ld. R0 is only the zero base of
// ADDI32_W and must be clean (shrink-wrap prologue is not the entry XOR).
static Register emitCSROffsetScratch(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI,
                                     const DebugLoc &DL,
                                     const HaydnInstrInfo *TII, Register Avoid,
                                     Register Avoid2, bool ProtectRetCC,
                                     int Offset,
                                     MachineInstr::MIFlag FrameFlag) {
  ensureSoftZeroR0Clean(MBB, MBBI, DL, *TII, FrameFlag);
  Register OffReg =
      requirePEIScratchReg(MBB, MBBI, Avoid, Avoid2, ProtectRetCC);
  if (isInt<20>(Offset)) {
    BuildMI(MBB, MBBI, DL, TII->get(Haydn::ADDI32_W), OffReg)
        .addReg(Haydn::R0)
        .addImm(Offset)
        .setMIFlag(FrameFlag);
  } else {
    BuildMI(MBB, MBBI, DL, TII->get(Haydn::LOADI32), OffReg)
        .addImm(Offset)
        .setMIFlag(FrameFlag);
  }
  return OffReg;
}

// Emit a sequence to set a GPR32 register to BaseReg + Offset.
// Small offsets use ADDI32_W; large use MatInt(offset) + ADD32.
static void emitMaterializeOffset(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MBBI,
                                  const DebugLoc &DL,
                                  const HaydnInstrInfo *TII,
                                  Register DestReg, Register BaseReg,
                                  int Offset, MachineInstr::MIFlag FrameFlag) {
  if (Offset == 0) {
    // DestReg = BaseReg (copy via OR32)
    BuildMI(MBB, MBBI, DL, TII->get(Haydn::OR32), DestReg)
        .addReg(BaseReg)
        .addReg(BaseReg)
        .setMIFlag(FrameFlag);
  } else if (isInt<20>(Offset)) {
    // DestReg = BaseReg + Offset (ADDI32_W simm20)
    BuildMI(MBB, MBBI, DL, TII->get(Haydn::ADDI32_W), DestReg)
        .addReg(BaseReg)
        .addImm(Offset)
        .setMIFlag(FrameFlag);
  } else {
    emitMaterializeImm32(MBB, MBBI, DL, TII, DestReg, Offset, FrameFlag);
    BuildMI(MBB, MBBI, DL, TII->get(Haydn::ADD32), DestReg)
        .addReg(BaseReg)
        .addReg(DestReg)
        .setMIFlag(FrameFlag);
  }
}

// Emit a callee-saved register store at an arbitrary byte offset from
// BaseReg. When the offset fits the s0 LS imm4 scaled range
// ([0,60]/4-aligned for ST32, [0,120]/8-aligned for ST64) the plain
// immediate-offset ST32/ST64 is emitted. When the offset is out of range
// (negative or large CSR slots) the offset is materialized into a PEI
// scratch (\c getPEIScratchReg: ABI call-clobbered / reserved R12 — same
// contract as EFI scavenger) and the logical ST32_REG_M0S0LS /
// ST64_REG_M0S0LS register-offset form is emitted. Product encode is Format E.
static void emitCSRStore(MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                         const HaydnInstrInfo *TII, unsigned StoreOpc,
                         Register SrcReg, Register BaseReg, int Offset,
                         MachineInstr::MIFlag FrameFlag) {
  unsigned Shift = (StoreOpc == Haydn::ST64) ? 3 : 2;
  int64_t MaxOff = static_cast<int64_t>(15) << Shift;
  bool InImm4Range = (Offset >= 0 && Offset <= MaxOff &&
                      (Offset & ((1 << Shift) - 1)) == 0);
  if (InImm4Range) {
    // Golden scaled imm: field = byte_offset >> log2(width).
    BuildMI(MBB, MBBI, DL, TII->get(StoreOpc))
        .addReg(SrcReg)
        .addReg(BaseReg)
        .addImm(Offset >> Shift)
        .setMIFlag(FrameFlag);
    return;
  }
  // Logical REG forms only; product encode is Format E members.
  unsigned RegOpc = (StoreOpc == Haydn::ST64) ? Haydn::ST64_REG_M0S0LS
                                              : Haydn::ST32_REG_M0S0LS;
  // Offset temp is a PEI scratch, never soft-zero R0 (F21). R0 is only
  // the clean zero base of ADDI32_W. Offset 0 is always in imm4 range
  // so this path does not use R0 as OffReg.
  assert(SrcReg != Haydn::R0 && BaseReg != Haydn::R0);
  Register OffReg =
      emitCSROffsetScratch(MBB, MBBI, DL, TII, /*Avoid=*/SrcReg,
                           /*Avoid2=*/BaseReg, /*ProtectRetCC=*/false, Offset,
                           FrameFlag);
  BuildMI(MBB, MBBI, DL, TII->get(RegOpc))
      .addReg(SrcReg)
      .addReg(BaseReg)
      .addReg(OffReg)
      .setMIFlag(FrameFlag);
}

// Emit a callee-saved register load (restore) at an arbitrary byte offset
// from BaseReg. Mirrors emitCSRStore for the epilogue restore path. When the
// offset fits the s0 LS imm4 scaled range the plain immediate-offset LD32
// LD64 is emitted; otherwise the offset is materialized into a PEI scratch
// (getPEIScratchReg) and the LD32_REG_M0S0LS / LD64_REG_M0S0LS register
// offset variant is emitted. A pure-constant scratch (LOADI32/ADDI feeding
// the LD) is live into the load so it is not DCE'd — unlike a reserved-reg
// stride base that had no consumer after frame-destroy.
static void emitCSRLoad(MachineBasicBlock &MBB,
                        MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                        const HaydnInstrInfo *TII, unsigned LoadOpc, Register DstReg,
                        Register BaseReg, int Offset,
                        MachineInstr::MIFlag FrameFlag) {
  // Golden scaled simm6 : EA = base + (simm6 << log2(width)).
  // LD64 scale 8; LD32 scale 4. Outside [-32,31] scaled → REG-offset.
  bool Is64 = (LoadOpc == Haydn::LD64);
  unsigned Shift = Is64 ? 3 : 2;
  bool Aligned = (Offset & ((1 << Shift) - 1)) == 0;
  bool InSimm6Range = Aligned && isInt<6>(Offset >> Shift);
  if (InSimm6Range) {
    // Golden scaled imm: field = byte_offset >> log2(width).
    BuildMI(MBB, MBBI, DL, TII->get(LoadOpc), DstReg)
        .addReg(BaseReg)
        .addImm(Offset >> Shift)
        .setMIFlag(FrameFlag);
    return;
  }
  unsigned RegOpc = Is64 ? Haydn::LD64_REG_M0S0LS : Haydn::LD32_REG_M0S0LS;
  // Offset temp is a PEI scratch, never soft-zero R0 (F21). Protect
  // RetCC R1/R2 on the epilogue path. R0 is only the clean zero base.
  assert(DstReg != Haydn::R0 && BaseReg != Haydn::R0);
  Register OffReg = emitCSROffsetScratch(
      MBB, MBBI, DL, TII, /*Avoid=*/DstReg, /*Avoid2=*/BaseReg,
      /*ProtectRetCC=*/true, Offset, FrameFlag);
  BuildMI(MBB, MBBI, DL, TII->get(RegOpc), DstReg)
      .addReg(BaseReg)
      .addReg(OffReg)
      .setMIFlag(FrameFlag);
}

// Snap the PEI-assigned stack size. Peer:
//   AIEBaseFrameLowering.cpp:47-62 — realign snaps FrameSize to MaxAlign
//     so post-AND SP-relative locals stay MaxAlign-aligned; otherwise ABI
//     StackAlign.
//   RISCVFrameLowering.cpp:509-527 — FrameSize align only; it never writes
//     MaxCallFrameSize. That value is finalized in
//     processFunctionBeforeFrameFinalized after calculateCallFrameInfo.
void HaydnFrameLowering::determineFrameLayout(MachineFunction &MF) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *RI = STI.getRegisterInfo();
  uint64_t FrameSize = MFI.getStackSize();
  Align FrameAlign =
      RI->hasStackRealignment(MF) ? MFI.getMaxAlign() : getStackAlign();
  MFI.setStackSize(alignTo(FrameSize, FrameAlign));
}

// Returns true if the specified function should have a dedicated frame
// pointer register (R14).
// Forced by:
// mattr=+frame-pointer (subtarget feature)
// fno-omit-frame-pointer / FramePointerKind (driver Options)
// Required by ABI even when omit is on:
// stack realignment, VLAs / var-sized objects, llvm.frameaddress
bool HaydnFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const TargetRegisterInfo *TRI = ST.getRegisterInfo();

  if (ST.useFramePointer() || MF.getTarget().Options.DisableFramePointerElim(MF))
    return true;

  if (TRI->hasStackRealignment(MF) || MFI.hasVarSizedObjects() ||
      MFI.isFrameAddressTaken())
    return true;

  // A call (ADJCALLSTACK / adjustsStack) is not enough to keep FP.
  // Peer: AIEBaseFrameLowering.cpp:40-43 and RISCVFrameLowering.cpp:464-470
  // use DisableFramePointerElim / VLA / frameaddress / realign only.
  // Forcing FP on every caller reserved R14, grew CSI, and rewrote
  // `st32 lr, sp, 3` into a temp-addressed pair. Locals stay SP-relative;
  // post-RA FI users add getCallFrameSPAdj so a live ADJCALLSTACKDOWN
  // cannot land a pack/scratch store on outgoing stack slots.
  return false;
}

bool HaydnFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  // Dynamic-only: outgoing args are never pre-reserved in the prologue.
  // eliminateCallFramePseudoInstr expands ADJCALLSTACKDOWN/UP. Do not
  // also add MaxCallFrameSize into FrameSize (F20).
  (void)MF;
  return false;
}

//===----------------------------------------------------------------------===//
// Shrink-Wrapping Support
//===----------------------------------------------------------------------===//

bool HaydnFrameLowering::enableShrinkWrapping(
    const MachineFunction &MF) const {
  // Shrink-wrapping is not beneficial without optimizations.
  if (MF.getFunction().hasOptNone())
    return false;

  // Do not shrink-wrap if FP is required (variable-sized objects, frame
  // address taken, etc.) — the prologue setup is tightly coupled with the
  // FP establishment and moving it could break assumptions in the epilogue.
  // This is a conservative starting point; we can relax this later.
  if (hasFP(MF))
    return false;

  // CFIFixup is product-enabled (TM setCFIFixup + enableCFIFixup).
  // Shrink-wrapped multi-exit frames rely on that pass plus epilogue
  // FrameDestroy CFI from emitEpilogue.
  return true;
}

bool HaydnFrameLowering::enableCFIFixup(const MachineFunction &MF) const {
  return TargetFrameLowering::enableCFIFixup(MF);
}

bool HaydnFrameLowering::canUseAsPrologue(const MachineBasicBlock &MBB) const {
  // AArch64FrameLowering.cpp:932-970 / RISCVFrameLowering.cpp:2323-2348:
  // shrink-wrap must not pick a block where PEI would clobber a live
  // call-clobbered GPR. If a scratch is free at block start, the point is
  // safe; if PEI does not require one (small SUBI32, skip stride), also safe.
  MachineBasicBlock &Tmp = const_cast<MachineBasicBlock &>(MBB);
  if (getPEIScratchReg(Tmp, Tmp.begin(), Haydn::R13))
    return true;
  return !peiRequiresScratch(*MBB.getParent());
}

bool HaydnFrameLowering::canUseAsEpilogue(const MachineBasicBlock &MBB) const {
  // PPCFrameLowering.cpp:558-561: scavenger at first terminator / return.
  MachineBasicBlock &Tmp = const_cast<MachineBasicBlock &>(MBB);
  MachineBasicBlock::iterator I =
      Tmp.isReturnBlock() ? Tmp.getLastNonDebugInstr() : Tmp.getFirstTerminator();
  if (getPEIScratchReg(Tmp, I, Haydn::R13, Register(), /*ProtectRetCC=*/true))
    return true;
  return !peiRequiresScratch(*MBB.getParent());
}

//===----------------------------------------------------------------------===//
// Prologue/Epilogue Emission
//===----------------------------------------------------------------------===//

void HaydnFrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const HaydnInstrInfo *TII = MF.getSubtarget<HaydnSubtarget>().getInstrInfo();
  const HaydnRegisterInfo *TRI = MF.getSubtarget<HaydnSubtarget>().getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  MCContext &Context = MF.getContext();
  const MCRegisterInfo *MCRI = Context.getRegisterInfo();

  MachineBasicBlock::iterator MBBI = MBB.begin();

  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc DL;

  // R0 is reserved as the soft-zero register. Ensure it is live-in and
  // initialized to zero at function entry so the register allocator and
  // liveness analysis treat R0 as always-defined.
  // R0 is NOT used for argument passing (arguments start at R1), so it is
  // always safe to zero R0 in the prologue.
  // With shrink-wrapping, the prologue may be emitted in a non-entry block.
  // R0 zeroing must happen at the entry block only, since the calling
  // convention guarantees R0=0 at entry and nothing modifies it (it is
  // reserved).
  bool IsEntryBlock = &MF.front() == &MBB;
  if (IsEntryBlock) {
    if (!MBB.isLiveIn(Haydn::R0))
      MBB.addLiveIn(Haydn::R0);
    // F31: Always zero R0 at function entry. The previous guard
    // `!MBBI->isImplicitDef` skipped the zeroing when the first instruction
    // was an implicit-def (common after fast-isel/regalloc), leaving R0
    // uninitialized. R0 is the soft-zero register — it must be zero at entry.
    // See CLAUDE.md register map (R0=soft-zero).
    //
    // slice Z: one restore emitter (XOR32 R0,R0,R0 identity), not the
    // retired ZERO_GPR pseudo. Post-RA commit places XOR32 as a Format E
    // member; the packetizer bundled prologue zero is a product parcel.
    restoreSoftZeroR0(MBB, MBBI, DL, *TII, MachineInstr::FrameSetup);
  }

  // StackSize comes from PEI assignFrameOffsets. Snap to StackAlign only.
  // MaxCallFrameSize was finalized in processFunctionBeforeFrameFinalized;
  // do not rewrite it here.
  determineFrameLayout(MF);
  uint64_t AlignedStackSize = MFI.getStackSize();

  // Get callee-saved registers
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

  // Allocate stack space first, then save CSRs into the new frame. Saves sit
  // above the updated SP so they are not in the unallocated region. This is
  // stack discipline, not an ISR ABI: interrupt stays fail-closed. Naked
  // never reaches here — generic PEI skips insertPrologEpilogCode for Naked.
  // SUBI32 R13, R13, AlignedStackSize
  if (AlignedStackSize != 0) {
    // If the stack size fits in simm16, use single instruction
    if (isInt<16>(AlignedStackSize)) {
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::SUBI32), Haydn::R13)
          .addReg(Haydn::R13)
          .addImm(AlignedStackSize)
          .setMIFlag(MachineInstr::FrameSetup);
    } else {
      // Large stack: MatInt(size) into PEI scratch, SUB32 SP, SP, scratch.
      Register TempReg =
          requirePEIScratchReg(MBB, MBBI, /*Avoid=*/Haydn::R13);
      emitMaterializeImm32(MBB, MBBI, DL, TII, TempReg,
                           static_cast<int64_t>(AlignedStackSize),
                           MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::SUB32), Haydn::R13)
          .addReg(Haydn::R13)
          .addReg(TempReg)
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  // Save callee-saved GPRs (R8-R11, R14 if used as FP).
  // SP has already been decremented, so offsets are from new SP.
  //
  // Optimization: for consecutive callee-saves at stride-4 offsets, use a
  // PEI scratch as base pointer (unused allocatable call-clobbered GPR via
  // getPEIScratchReg). Set it to SP + first_offset once, then emit stores at
  // increasing offsets (0, 4, 8,...) from the base. Skip the stride path
  // when the chosen scratch is itself a CSR being saved (self-clobber).
  {
    // each CSR slot must be addressed via the FrameReg returned
    // by getFrameIndexReference (FP when hasFP, SP otherwise), NOT a hardcoded
    // R13. When hasFP is true, getFrameIndexReference returns FP-relative
    // negative offsets (e.g. -4 for the LR slot); emitting `ST32 reg, R13, -4`
    // after `sub sp,N` writes BELOW the new SP into the callee's frame region.
    // The callee's locals then alias and clobber the saved LR. Using FrameReg
    // matches eliminateFrameIndex and the O1/O2 SP-base path when there is no FP.
    SmallVector<CSRSlot, 8> GPRCSRegs;
    collectCalleeSavedSlots(*this, MF, CSRBank::GPR32, GPRCSRegs);

    // PEI scratch for stride-4 base. Skip the stride path when none is free
    // at MBBI (all caller-saved live) or the chosen scratch is itself a CSR
    // being saved (would clobber before saving it). Never a used-reg pick.
    Register StrideScratch =
        getPEIScratchReg(MBB, MBBI, /*Avoid=*/Haydn::R13);
    bool ScratchNeedsSaving = !StrideScratch;
    for (const auto &E : GPRCSRegs) {
      if (StrideScratch && E.Reg == StrideScratch) {
        ScratchNeedsSaving = true;
        break;
      }
    }

    if (GPRCSRegs.size() >= 2 && !ScratchNeedsSaving) {
      // Find the longest consecutive run starting from index 0 with stride 4.
      // The stride-4 base-pointer optimization assumes every CSR in the run
      // shares the same BaseReg (FP or SP) — guaranteed because hasFP is a
      // per-function property, so all CSRs see the same FrameReg.
      unsigned RunLen = 1;
      while (RunLen < GPRCSRegs.size() &&
             GPRCSRegs[RunLen].Offset ==
                 GPRCSRegs[RunLen - 1].Offset + 4) {
        ++RunLen;
      }

      if (RunLen >= 2) {
        Register BaseReg = StrideScratch;
        Register FrameReg = GPRCSRegs[0].BaseReg;
        int BaseOffset = GPRCSRegs[0].Offset;
        emitMaterializeOffset(MBB, MBBI, DL, TII, BaseReg, FrameReg,
                              BaseOffset, MachineInstr::FrameSetup);

        // Store each register at its offset relative to the base.
        // ST32 imm is word element index (EA = base + (imm << 2)).
        for (unsigned J = 0; J < RunLen; ++J) {
          int RelOffset = GPRCSRegs[J].Offset - BaseOffset;
          BuildMI(MBB, MBBI, DL, TII->get(Haydn::ST32))
              .addReg(GPRCSRegs[J].Reg)
              .addReg(BaseReg)
              .addImm(RelOffset >> 2)
              .setMIFlag(MachineInstr::FrameSetup);
        }

        // Remaining non-consecutive registers: plain stores from their FrameReg.
        for (unsigned J = RunLen; J < GPRCSRegs.size(); ++J) {
          emitCSRStore(MBB, MBBI, DL, TII, Haydn::ST32, GPRCSRegs[J].Reg,
                       GPRCSRegs[J].BaseReg, GPRCSRegs[J].Offset,
                       MachineInstr::FrameSetup);
        }
      } else {
        // No consecutive run of 2+ — plain stores from each CSR's FrameReg.
        for (const auto &E : GPRCSRegs) {
          emitCSRStore(MBB, MBBI, DL, TII, Haydn::ST32, E.Reg, E.BaseReg,
                       E.Offset, MachineInstr::FrameSetup);
        }
      }
    } else {
      // 0 or 1 GPR callee-saves, or scratch needs saving — individual stores.
      for (const auto &E : GPRCSRegs) {
        emitCSRStore(MBB, MBBI, DL, TII, Haydn::ST32, E.Reg, E.BaseReg,
                     E.Offset, MachineInstr::FrameSetup);
      }
    }
  }

  // Save callee-saved DR64 registers (D8-D15).
  // Same optimization as GPR: stride-8 consecutive offsets use PEI scratch as
  // base (getPEIScratchReg). GPR CSRs were already emitted above.
  {
    // use each CSR's FrameReg (FP when hasFP, SP otherwise)
    // not a hardcoded R13. See the GPR block above for the full rationale.
    SmallVector<CSRSlot, 8> DRCSRegs;
    collectCalleeSavedSlots(*this, MF, CSRBank::DR64, DRCSRegs);

    if (DRCSRegs.size() >= 2) {
      unsigned RunLen = 1;
      while (RunLen < DRCSRegs.size() &&
             DRCSRegs[RunLen].Offset ==
                 DRCSRegs[RunLen - 1].Offset + 8) {
        ++RunLen;
      }

      if (RunLen >= 2) {
        Register BaseReg =
            getPEIScratchReg(MBB, MBBI, /*Avoid=*/Haydn::R13);
        if (!BaseReg) {
          for (const auto &E : DRCSRegs) {
            emitCSRStore(MBB, MBBI, DL, TII, Haydn::ST64, E.Reg, E.BaseReg,
                         E.Offset, MachineInstr::FrameSetup);
          }
        } else {
          Register FrameReg = DRCSRegs[0].BaseReg;
          int BaseOffset = DRCSRegs[0].Offset;
          emitMaterializeOffset(MBB, MBBI, DL, TII, BaseReg, FrameReg,
                                BaseOffset, MachineInstr::FrameSetup);

          // ST64 imm is dword element index (EA = base + (imm << 3)).
          for (unsigned J = 0; J < RunLen; ++J) {
            int RelOffset = DRCSRegs[J].Offset - BaseOffset;
            BuildMI(MBB, MBBI, DL, TII->get(Haydn::ST64))
                .addReg(DRCSRegs[J].Reg)
                .addReg(BaseReg)
                .addImm(RelOffset >> 3)
                .setMIFlag(MachineInstr::FrameSetup);
          }

          for (unsigned J = RunLen; J < DRCSRegs.size(); ++J) {
            emitCSRStore(MBB, MBBI, DL, TII, Haydn::ST64, DRCSRegs[J].Reg,
                         DRCSRegs[J].BaseReg, DRCSRegs[J].Offset,
                         MachineInstr::FrameSetup);
          }
        }
      } else {
        for (const auto &E : DRCSRegs) {
          emitCSRStore(MBB, MBBI, DL, TII, Haydn::ST64, E.Reg, E.BaseReg,
                       E.Offset, MachineInstr::FrameSetup);
        }
      }
    } else {
      for (const auto &E : DRCSRegs) {
        emitCSRStore(MBB, MBBI, DL, TII, Haydn::ST64, E.Reg, E.BaseReg,
                     E.Offset, MachineInstr::FrameSetup);
      }
    }
  }

  // AR0–AR3 are caller-saved: not in CSR_Haydn. No prologue save.
  // Product UA residual uses AR as scratch for PLDWWUA/FLAR/….

  // Emit CFI directives.
  // cfi_def_cfa_offset StackSize — only emit when there's an actual stack
  // allocation. The initial CFA offset is 0 per getInitialCFAOffset, so
  // emitting ".cfi_def_cfa_offset 0" is a no-op that wastes code bytes. This
  // matches the convention used by RISC-V, ARM, and AArch64 frame lowering.
  // See (frame-lowering-opt) for rationale.
  unsigned CFIIndex;
  if (AlignedStackSize != 0) {
    CFIIndex = MF.addFrameInst(
        MCCFIInstruction::cfiDefCfaOffset(nullptr, AlignedStackSize));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlags(MachineInstr::FrameSetup);
  }

  // Set up frame pointer if needed.
  // FP (R14) = top of fixed frame (old SP): FP = SP + AlignedStackSize.
  // BP is folded into FP: dyn-alloca only moves SP; epilogue recovers
  // fixed-frame SP as SP = FP - StackSize. No separate base-pointer reg.
  if (hasFP(MF)) {
    Register FP = TRI->getFrameRegister(MF); // R14
    if (AlignedStackSize == 0) {
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::ADDI32_W), FP)
          .addReg(Haydn::R13)
          .addImm(0)
          .setMIFlag(MachineInstr::FrameSetup);
    } else if (isInt<16>(AlignedStackSize)) {
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::ADDI32_W), FP)
          .addReg(Haydn::R13)
          .addImm(AlignedStackSize)
          .setMIFlag(MachineInstr::FrameSetup);
    } else {
      Register TempReg =
          requirePEIScratchReg(MBB, MBBI, /*Avoid=*/FP);
      emitMaterializeImm32(MBB, MBBI, DL, TII, TempReg,
                           static_cast<int64_t>(AlignedStackSize),
                           MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::ADD32), FP)
          .addReg(Haydn::R13)
          .addReg(TempReg)
          .setMIFlag(MachineInstr::FrameSetup);
    }

    // After FP = SP + AlignedStackSize, FP equals incoming SP (entry CFA).
    // CFA must be FP+0. DW_CFA_def_cfa_register keeps the prior
    // .cfi_def_cfa_offset StackSize, yielding CFA = FP + StackSize =
    // incoming_SP + StackSize — wrong by the whole frame. DW_CFA_def_cfa
    // sets register and offset together (RISCV/LoongArch shape).
    CFIIndex = MF.addFrameInst(MCCFIInstruction::cfiDefCfa(
        nullptr, MCRI->getDwarfRegNum(FP, true), /*Offset=*/0));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlags(MachineInstr::FrameSetup);

    // Realign SP after FP captures incoming SP. Peer:
    // RISCVFrameLowering.cpp:1142-1153 `ANDI SP, SP, -MaxAlign`. ANDI32
    // is uimm20 ZEXT; materialise the mask and AND32.
    if (TRI->hasStackRealignment(MF)) {
      assert(!MFI.hasVarSizedObjects() &&
             "Haydn: SP realignment with VLAs requires a base pointer");
      const int64_t Mask =
          -static_cast<int64_t>(MFI.getMaxAlign().value());
      Register MaskReg = requirePEIScratchReg(MBB, MBBI, /*Avoid=*/Haydn::R13,
                                              /*Avoid2=*/FP);
      emitMaterializeImm32(MBB, MBBI, DL, TII, MaskReg, Mask,
                           MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::AND32), Haydn::R13)
          .addReg(Haydn::R13)
          .addReg(MaskReg)
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  // Emit .cfi_offset for each saved CSR. Offsets come from
  // getCalleeSavedCFAOffset = getObjectOffset (frame-layout FI offset).
  // That is the CFA-relative value this backend emits; do not add
  // getStackSize here (that would double-count). See
  // cfi-sp-cfa-requires-stacksize.
  for (const CalleeSavedInfo &CI : CSI) {
    Register Reg = CI.getReg();
    int FrameIdx = CI.getFrameIdx();

    // Handle GPR32 registers (R8-R11, R14)
    const TargetRegisterClass *RC = Reg.isVirtual() ?
        MRI.getRegClassOrNull(Reg) : TRI->getMinimalPhysRegClass(Reg);
    if (RC && (RC == &Haydn::GPR32RegClass || RC == &Haydn::GPR32NoSPNoLRRegClass)) {
      int64_t Offset = getCalleeSavedCFAOffset(MF, FrameIdx);
      CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
          nullptr, MCRI->getDwarfRegNum(Reg, true), Offset));
      BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlags(MachineInstr::FrameSetup);
    }
    // Handle DR64 registers (D8-D15)
    else if (RC && RC == &Haydn::DR64RegClass) {
      int64_t Offset = getCalleeSavedCFAOffset(MF, FrameIdx);
      CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
          nullptr, MCRI->getDwarfRegNum(Reg, true), Offset));
      BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlags(MachineInstr::FrameSetup);
    }
  }

  // belt: empty entry dead-end (whole-function `unreachable`). Primary
  // invariant is HaydnEnsureTerminators (post-PEI): every succ-empty MBB gets
  // RET, including mid-function cases PEI never visits. Keep this so entry is
  // terminated even if that pass is disabled.
  if (IsEntryBlock && MBB.succ_empty() &&
      MBB.getFirstTerminator() == MBB.end()) {
    BuildMI(MBB, MBB.end(), DebugLoc(), TII->get(Haydn::RET))
        .setMIFlag(MachineInstr::FrameSetup);
  }
}

void HaydnFrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const HaydnInstrInfo *TII = MF.getSubtarget<HaydnSubtarget>().getInstrInfo();
  const HaydnRegisterInfo *TRI = MF.getSubtarget<HaydnSubtarget>().getRegisterInfo();

  // For return blocks, insert before the return instruction.
  // For non-return blocks (shrink-wrapping), insert before the first
  // terminator so restores happen before any branches.
  //
  // do NOT early-return when the block is empty. LLVM mid-end can
  // fold a whole function to `unreachable` (yarpgen UB / div-by-zero after
  // ILP32 long truncation, etc.). IRTranslator then leaves an empty MBB
  // with no terminator; isReturnBlock is false, and the previous early
  // return left only soft-zero R0 (or nothing) — fall-through past the last
  // bundle → BundleSim "PC not found". Always leave a RET on succ-empty
  // blocks (see end of this function).
  MachineBasicBlock::iterator MBBI;
  if (MBB.isReturnBlock()) {
    MBBI = MBB.getLastNonDebugInstr();
    // Empty return block: insert at end (MBBI == end is OK for BuildMI).
  } else {
    MBBI = MBB.getFirstTerminator();
  }

  DebugLoc DL;
  if (MBBI != MBB.end())
    DL = MBBI->getDebugLoc();
  else if (!MBB.empty())
    DL = MBB.back().getDebugLoc();

  // Re-zero soft-zero R0 before CSR restores when a call or a CSR spill
  // may have left R0 dirty. Long-branch / far-jump sequences historically
  // used `JALR R0, scratch, 0` to "discard" the link; the ISS still writes
  // PC_next into R0 (not hardwired zero). LOADI32 in emitCSRLoad then
  // materializes offsets from a non-zero R0 and restores CSRs from the
  // wrong stack slots (e.g. rem pointer in R8 after __udivmoddi4).
  // Caller-side re-zero after JAL/JALR is already HaydnExpandPseudos.
  // Leaf no-call frames with empty CSI never borrow R0 — skip the
  // epilogue xor (F24). One predicate: hasCalls() || !CSI.empty().
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  if (MFI.hasCalls() || !CSI.empty())
    restoreSoftZeroR0(MBB, MBBI, DL, *TII, MachineInstr::FrameDestroy);

  // Get the number of bytes to allocate from the FrameInfo.
  uint64_t StackSize = MFI.getStackSize();

  // Dyn-alloca / VLA or SP realign moves SP off FP - StackSize. CSR
  // restores need the pre-realign SP. Recover from FP (R14): prologue set
  // FP = incoming SP, so pre-realign SP = FP - StackSize. Peer:
  // RISCVFrameLowering.cpp:1259-1308 RestoreSPFromFP.
  if (MFI.hasVarSizedObjects() || TRI->hasStackRealignment(MF)) {
    assert(hasFP(MF) && "var-sized objects require FP (BP folded into FP)");
    Register FP = TRI->getFrameRegister(MF);
    if (StackSize == 0) {
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::ADDI32_W), Haydn::R13)
          .addReg(FP)
          .addImm(0)
          .setMIFlag(MachineInstr::FrameDestroy);
    } else if (isInt<16>(static_cast<int64_t>(StackSize))) {
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::ADDI32_W), Haydn::R13)
          .addReg(FP)
          .addImm(-static_cast<int64_t>(StackSize))
          .setMIFlag(MachineInstr::FrameDestroy);
    } else {
      Register TempReg = requirePEIScratchReg(MBB, MBBI, /*Avoid=*/FP,
                                              /*Avoid2=*/Haydn::R13,
                                              /*ProtectRetCC=*/true);
      emitMaterializeImm32(MBB, MBBI, DL, TII, TempReg,
                           static_cast<int64_t>(StackSize),
                           MachineInstr::FrameDestroy);
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::SUB32), Haydn::R13)
          .addReg(FP)
          .addReg(TempReg)
          .setMIFlag(MachineInstr::FrameDestroy);
    }
  }

  // Restore callee-saved registers in reverse order of saving.
  // CSI was fetched above for the F24 R0 re-zero gate.

  // AR0–AR3 caller-saved: nothing to restore.

  // Restore callee-saved DR64 registers (D8-D15).
  // No stride base-pointer optimization in the epilogue: a PEI scratch
  // materialize of Base+off can be DCE'd when the scratch is reserved (no
  // live range) or clobber a live value when it is not. Restore each CSR
  // via emitCSRLoad (imm when legal, else REG-offset + getPEIScratchReg).
  {
    // restore from each CSR's FrameReg (FP when hasFP, SP
    // otherwise), not a hardcoded R13. See the prologue GPR block for the
    // full rationale.
    SmallVector<CSRSlot, 8> DRCSRegs;
    collectCalleeSavedSlots(*this, MF, CSRBank::DR64, DRCSRegs);

    for (const auto &E : DRCSRegs) {
      // never emit bare LD64 SP, imm for large frames (e.g. +480).
      // emitCSRLoad picks imm when scaled simm6-legal, else LD64_REG + scratch.
      // Logical LD64 only; slot from placement / setDesc materialize.
      emitCSRLoad(MBB, MBBI, DL, TII, Haydn::LD64, E.Reg, E.BaseReg,
                  E.Offset, MachineInstr::FrameDestroy);
    }
  }

  // Restore callee-saved GPRs (R8-R11, R14 if used as FP).
  // Epilogue restores from each CSR's FrameReg (FP when hasFP, SP otherwise).
  // No stride-4 base-pointer optimization (same DCE / clobber reason as DR64).
  {
    SmallVector<CSRSlot, 8> GPRCSRegs;
    collectCalleeSavedSlots(*this, MF, CSRBank::GPR32, GPRCSRegs);

    for (const auto &E : GPRCSRegs) {
      emitCSRLoad(MBB, MBBI, DL, TII, Haydn::LD32, E.Reg, E.BaseReg,
                  E.Offset, MachineInstr::FrameDestroy);
    }
  }

  // Deallocate stack space
  // ADDI32 R13, R13, AlignedStackSize
  if (StackSize != 0) {
    if (isInt<16>(StackSize)) {
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::ADDI32_W), Haydn::R13)
          .addReg(Haydn::R13)
          .addImm(StackSize)
          .setMIFlag(MachineInstr::FrameDestroy);
    } else {
      // Large stack restore: MatInt(size) into PEI scratch, ADD32 SP.
      // RetCC R1/R2 are excluded by getPEIScratchReg permanently.
      Register TempReg = requirePEIScratchReg(MBB, MBBI, /*Avoid=*/Haydn::R13,
                                              /*Avoid2=*/Register(),
                                              /*ProtectRetCC=*/true);
      emitMaterializeImm32(MBB, MBBI, DL, TII, TempReg,
                           static_cast<int64_t>(StackSize),
                           MachineInstr::FrameDestroy);
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::ADD32), Haydn::R13)
          .addReg(Haydn::R13)
          .addReg(TempReg)
          .setMIFlag(MachineInstr::FrameDestroy);
    }
  }

  // Epilogue FrameDestroy CFI: restore CFA to SP+0 once the frame is gone.
  if (MF.needsFrameMoves() && (StackSize != 0 || hasFP(MF))) {
    unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::cfiDefCfa(
        nullptr,
        MF.getContext().getRegisterInfo()->getDwarfRegNum(Haydn::R13, true),
        0));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlags(MachineInstr::FrameDestroy);
  }

  // Emit return instruction if the terminator isn't already a return.
  // CallLowering::lowerReturn emits RET, so only add one if missing.
  // With shrink-wrapping, the epilogue may be in a non-return block (e.g.
  // a block that branches to a shared return block). Only emit RET for
  // actual return blocks — OR for succ-empty blocks with no terminator
  // (empty `unreachable` body).
  const bool NeedsRet =
      (MBB.isReturnBlock() && (MBBI == MBB.end() || !MBBI->isReturn())) ||
      (MBB.succ_empty() &&
       (MBB.getFirstTerminator() == MBB.end() ||
        !MBB.getFirstTerminator()->isTerminator()));
  if (NeedsRet) {
    BuildMI(MBB, MBB.end(), DL, TII->get(Haydn::RET))
        .setMIFlag(MachineInstr::FrameDestroy);
  }
}

StackOffset
HaydnFrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                           Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const HaydnRegisterInfo *TRI = MF.getSubtarget<HaydnSubtarget>().getRegisterInfo();

  // AIE2RegisterInfo.cpp:169 / RISCVFrameLowering.cpp:1368: object offset
  // plus OffsetAdjustment. Haydn never writes a non-zero adjustment today
  // (no ARM-style FP-spill bias); keep the add so a later writer is not a
  // silent FI-coordinate drift.
  int64_t Offset = MFI.getObjectOffset(FI) + MFI.getOffsetAdjustment();

  // CSR spill slots live above the realign pad. Address them from the
  // pre-realign SP (prologue stores before AND32; epilogue RestoreSPFromFP
  // then loads). Peer: RISCVFrameLowering.cpp:1376-1388 uses CSI front/back
  // as a contiguous FI range; exact membership so a scavenger/pack FI that
  // happens to sit numerically between CSRs is not treated as a CSR slot.
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  for (const CalleeSavedInfo &CI : CSI) {
    if (CI.getFrameIdx() == FI) {
      FrameReg = Haydn::R13;
      Offset += static_cast<int64_t>(MFI.getStackSize());
      return StackOffset::getFixed(Offset);
    }
  }

  // After AND32, non-fixed locals sit at the realigned SP. No BP: VLAs +
  // realign is fail-closed in processFunctionBeforeFrameFinalized. Peer:
  // RISCVFrameLowering.cpp:1428-1467.
  if (TRI->hasStackRealignment(MF) && !MFI.isFixedObjectIndex(FI)) {
    assert(!MFI.hasVarSizedObjects() &&
           "Haydn: stack realignment with VLAs requires a base pointer");
    FrameReg = Haydn::R13;
    Offset += static_cast<int64_t>(MFI.getStackSize());
    return StackOffset::getFixed(Offset);
  }

  FrameReg = TRI->getFrameRegister(MF);
  if (!hasFP(MF))
    Offset += static_cast<int64_t>(MFI.getStackSize());

  return StackOffset::getFixed(Offset);
}

int64_t HaydnFrameLowering::getCallFrameSPAdj(
    const MachineBasicBlock &MBB,
    MachineBasicBlock::const_iterator I) const {
  // Reconstruct PEI SPAdj after ADJCALLSTACK has become SUBI32/ADDI32_W.
  // Prologue/epilogue carries FrameSetup/Destroy and is already in
  // getFrameIndexReference (StackSize). VLA already forces hasFP so
  // callers use FP and ignore this delta. Walk instrs() so a bundled
  // expand is not skipped by the bundle iterator.
  int64_t Adj = 0;
  const MachineInstr *Stop = (I == MBB.end()) ? nullptr : &*I;
  for (const MachineInstr &MI : MBB.instrs()) {
    if (Stop && &MI == Stop)
      break;
    if (MI.isBundle() || MI.getFlag(MachineInstr::FrameSetup) ||
        MI.getFlag(MachineInstr::FrameDestroy))
      continue;
    if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
        !MI.getOperand(0).isDef() || MI.getOperand(0).getReg() != Haydn::R13)
      continue;
    const unsigned Opc = MI.getOpcode();
    if ((Opc != Haydn::SUBI32 && Opc != Haydn::ADDI32 &&
         Opc != Haydn::ADDI32_W) ||
        !MI.getOperand(1).isReg() || MI.getOperand(1).getReg() != Haydn::R13 ||
        !MI.getOperand(2).isImm())
      continue;
    const int64_t Imm = MI.getOperand(2).getImm();
    Adj += (Opc == Haydn::SUBI32) ? Imm : -Imm;
  }
  return Adj;
}

StackOffset HaydnFrameLowering::getFrameIndexReferenceAt(
    const MachineFunction &MF, int FI, Register &FrameReg,
    const MachineBasicBlock &MBB,
    MachineBasicBlock::const_iterator I) const {
  StackOffset Off = getFrameIndexReference(MF, FI, FrameReg);
  if (FrameReg == Haydn::R13)
    if (int64_t Adj = getCallFrameSPAdj(MBB, I))
      Off += StackOffset::getFixed(Adj);
  return Off;
}

bool HaydnFrameLowering::allocateScavengingFrameIndexesNearIncomingSP(
    const MachineFunction &MF) const {
  // hasFP && !realign → near FP; realign / !hasFP → late near final SP.
  const TargetRegisterInfo *TRI =
      MF.getSubtarget<HaydnSubtarget>().getRegisterInfo();
  return hasFP(MF) && !TRI->hasStackRealignment(MF);
}

void HaydnFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const Align StackAlign = getStackAlign();
  const Function &F = MF.getFunction();

  // Interrupt / stack-protector / i128 / half / inreg / nest / swift* /
  // byref have no product frame/CC seat. CallLowering rejects the IR path
  // first; this is the PEI last line if those attributes still reach
  // layout. ISR stays fail-closed (no CC_ISR). Naked is a product seat:
  // generic PEI never inserts prologue/epilogue or CSR code for Naked
  // (PrologEpilogInserter.cpp spillCalleeSavedRegs /
  // insertPrologEpilogCode), so this hook contributes only scavenging
  // frame indexes that an asm-only body never references (naked-fn.ll).
  // Legal musttail sibcall is JAL_W_MSP / JALR_W_MSP; ineligible musttail
  // stays fail-closed at CallLowering. AIE1ISelLowering.cpp:964 rejects
  // interrupt at return lowering; RISCV has a CC_ISR analog only when
  // an ISR vector exists.
  auto IsUnsupportedCCType = [](Type *Ty) {
    if (IntegerType *IT = dyn_cast<IntegerType>(Ty))
      return IT->getBitWidth() > 64;
    return Ty->isHalfTy() || Ty->isBFloatTy() || Ty->isFP128Ty();
  };
  if (F.hasFnAttribute("interrupt") ||
      F.hasFnAttribute(Attribute::StackProtect) ||
      F.hasFnAttribute(Attribute::StackProtectReq) ||
      F.hasFnAttribute(Attribute::StackProtectStrong) ||
      IsUnsupportedCCType(F.getReturnType())) {
    report_fatal_error(
        "Haydn: interrupt/stack-protector/i128 have no product frame ABI",
        /*GenCrashDiag=*/false);
  }
  for (const Argument &Arg : F.args()) {
    if (IsUnsupportedCCType(Arg.getType()) ||
        Arg.hasAttribute(Attribute::InReg) || Arg.hasNestAttr() ||
        Arg.hasByRefAttr() || Arg.hasAttribute(Attribute::SwiftSelf) ||
        Arg.hasAttribute(Attribute::SwiftAsync) ||
        Arg.hasAttribute(Attribute::SwiftError) ||
        Arg.hasAttribute(Attribute::InAlloca) ||
        Arg.hasAttribute(Attribute::Preallocated))
      report_fatal_error(
          "Haydn: i128/inreg/nest/swift/byref have no product calling-convention seat",
          /*GenCrashDiag=*/false);
  }

  // Sole MaxCallFrameSize writer after PEI calculateCallFrameInfo.
  // emitPrologue / determineFrameLayout must not rewrite it.
  // Static MaxAlign > StackAlign(8) realigns in emitPrologue (AND32 +
  // MatInt(-MaxAlign); RISCVFrameLowering.cpp:1142-1153). VLAs + realign
  // still fail closed: no BP, so a VLA would move the post-AND SP used as
  // the local base (RISCV hasBP at RISCVFrameLowering.cpp:494-505).
  if (MFI.getMaxAlign() > StackAlign && MFI.hasVarSizedObjects()) {
    report_fatal_error("Haydn: stack object alignment " +
                           Twine(MFI.getMaxAlign().value()) +
                           " exceeds ABI StackAlign(8) with VLAs; SP "
                           "realignment needs a base pointer",
                       /*GenCrashDiag=*/false);
  }

  // Dynamic-only call-frame model: hasReservedCallFrame is always false, so
  // outgoing args are never pre-reserved in the prologue.
  // eliminateCallFramePseudoInstr expands ADJCALLSTACKDOWN/UP.
  // Align MaxCallFrameSize for VLAs so the recorded outgoing amount stays
  // StackAlign; do not add it into FrameSize — that would run both the
  // reserved and dynamic models (every caller burned MaxCallFrameSize
  // twice). Peer: RISCVFrameLowering.cpp:509-527 never adds
  // MaxCallFrameSize; reserved vs dynamic is exclusive.
  uint64_t MaxCallFrameSize = MFI.getMaxCallFrameSize();
  if (MFI.hasVarSizedObjects())
    MaxCallFrameSize = alignTo(MaxCallFrameSize, StackAlign);
  MFI.setMaxCallFrameSize(MaxCallFrameSize);

  // After RA: Format E CB members drop the tied AGU writeback dest.
  // Implicit-def of that physreg survives rewriteFieldSlotToMember.
  STI.getInstrInfo()->preserveCircularBufferWritebackDefs(MF);

  if (!RS)
    return;

  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const TargetRegisterInfo *TRI = ST.getRegisterInfo();
  const TargetRegisterClass &RC = Haydn::GPR32RegClass;
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();

  // Emergency-slot policy (AIE model: no free AT). EFI uses vregs;
  // scavengeFrameVirtualRegs may spill under full pressure even when
  // estimateStackSize is small (yarpgen_seed2). Keep one emergency FI
  // 4 bytes, same insurance as ARM without a free IP. Never pad N>1.
  // Nested phys scavenge is gone (vreg EFI).
  // One emergency FI. BranchRelaxation shares it (RISC-V style); a second
  // slot was never reserved — MayNeedBranchRelax used max(1, 1).
  const unsigned ScavSlotsNum = 1;

  for (unsigned I = 0; I < ScavSlotsNum; ++I) {
    int FI = MFI.CreateSpillStackObject(TRI->getSpillSize(RC),
                                        TRI->getSpillAlign(RC));
    RS->addScavengingFrameIndex(FI);
    // First slot doubles as the branch-relax dedicated spill (RISC-V style).
    if (FuncInfo->getBranchRelaxationScratchFI() < 0)
      FuncInfo->setBranchRelaxationScratchFI(FI);
  }

  // CB-162: hwloop-demote live-trip save home defaults to the shared
  // PostRAScratchFI word (reserved in determineCalleeSaves; every framed
  // function has it). The demote's stack-counter home is
  // BranchRelaxationScratchFI when present — just created above — so the
  // two normally stay disjoint without frame growth. A dedicated slot is
  // needed only in the rare both-map-to-PostRA case (no scavenging slot, so
  // the counter home is also PostRAScratchFI). Frame deadline rule: every
  // possible demotion home is reserved HERE, before
  // calculateFrameObjectOffsets — the post-RA demote (HaydnHardwareLoops)
  // must never CreateStackObject. Guarded on an actual hwloop setup so
  // loop-free leaf functions pay no frame growth.
  if (FuncInfo->getHwLoopDemoteSaveFI() < 0) {
    const HaydnInstrInfo &HII = *ST.getInstrInfo();
    const int PostRASaveFI = FuncInfo->getPostRAScratchFI();
    int CounterFI = FuncInfo->getBranchRelaxationScratchFI();
    if (CounterFI < 0)
      CounterFI = PostRASaveFI;
    const bool SharedHomeDisjoint =
        PostRASaveFI >= 0 && PostRASaveFI != CounterFI;
    bool HasHWLoopSetup = false;
    for (const MachineBasicBlock &ScanBB : MF) {
      for (const MachineInstr &ScanMI : ScanBB) {
        if (HII.isHardwareLoopSetupOpcode(ScanMI.getOpcode())) {
          HasHWLoopSetup = true;
          break;
        }
      }
      if (HasHWLoopSetup)
        break;
    }
    if (HasHWLoopSetup && !SharedHomeDisjoint) {
      int FI = MF.getFrameInfo().CreateStackObject(/*Size=*/4,
                                                   /*Alignment=*/Align(4),
                                                   /*SpillSlot=*/true);
      FuncInfo->setHwLoopDemoteSaveFI(FI);
    }
  }
}

void HaydnFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                              BitVector &SavedRegs,
                                              RegScavenger *RS) const {
  // Call the base implementation to get the default callee-saved registers
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  // R14 is always CSR. When hasFP it is the reserved frame base (BP folded
  // in): force PEI save even if the allocator never "used" it as a temp.
  if (hasFP(MF)) {
    SavedRegs.set(Haydn::R14);
  }

  // JAL writes LR (R15). Non-leaf must save/restore R15.
  if (MF.getFrameInfo().hasCalls()) {
    SavedRegs.set(Haydn::R15);
  }

  // Inline-asm clobber of reserved roles. R15 is clobberable and must be
  // force-saved (leaf + `~{lr}` / `~{r15}` is not hasCalls). R0 / SP /
  // live FP cannot be restored — fail closed.
  const bool FramePointerLive = hasFP(MF);
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (!MI.isInlineAsm())
        continue;
      for (unsigned I = InlineAsm::MIOp_FirstOperand, NumOps = MI.getNumOperands();
           I < NumOps; ++I) {
        const MachineOperand &FlagMO = MI.getOperand(I);
        if (!FlagMO.isImm())
          continue;
        const InlineAsm::Flag F(FlagMO.getImm());
        const unsigned NRegs = F.getNumOperandRegisters();
        if (F.isClobberKind()) {
          for (unsigned K = 1; K <= NRegs && I + K < NumOps; ++K) {
            const MachineOperand &RegMO = MI.getOperand(I + K);
            if (!RegMO.isReg() || !RegMO.getReg().isPhysical())
              continue;
            const Register R = RegMO.getReg();
            if (R == Haydn::R15) {
              SavedRegs.set(Haydn::R15);
              continue;
            }
            if (R == Haydn::R0 || R == Haydn::R13 ||
                (R == Haydn::R14 && FramePointerLive)) {
              const Function &Fn = MF.getFunction();
              Fn.getContext().diagnose(DiagnosticInfoUnsupported(
                  Fn,
                  "inline asm clobbers reserved register (r0, sp, or live "
                  "fp); Haydn cannot restore architectural frame/zero "
                  "roles"));
            }
          }
        }
        I += NRegs;
      }
    }
  }

  // Permanent in-frame 4-byte spill *home* for post-RA scavenge when no free
  // GPR is available (HaydnPostRAScratch). Any physreg may use it — not R12
  // specific. Lives ABOVE SP (no red zone). Do NOT open a transient
  // `subi sp, 8` around scavenge windows: FI addresses are PEI-relative.
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();
  if (FuncInfo->getPostRAScratchFI() < 0) {
    int FI = MF.getFrameInfo().CreateStackObject(/*Size=*/4, /*Alignment=*/Align(4),
                                                 /*SpillSlot=*/true);
    FuncInfo->setPostRAScratchFI(FI);
  }

  // CB-162 note: the hwloop-demote save home decision lives at the end of
  // processFunctionBeforeFrameFinalized (after the BranchRelaxation scratch
  // slot above exists). This block deliberately reserves nothing.

  // Permanent 8-byte in-frame pack slot for DR64 construction from two GPR32
  // halves (LOADI64 both-halves-nonzero constants; MOV_GPR_TO_DR64 two-live-
  // GPR general case). Reserved ONLY when such a pack is present so leaf
  // functions pay no frame growth. The single fixed slot is reused by every
  // pack in the function; each pack is a local store-store-load with no SP
  // motion. This replaces the dynamic SUBI32/ADDI32_W $r13,8 transient that
  // shifted SP mid-function and corrupted sibling SP-relative fixed objects.
  // Align(8) places it first among locals (smallest offset → short-form LS).
  // Scan uses haydnInstrNeedsDR64PackSlot — the same predicate
  // expandPostRAPseudo uses — so CSI-valid FI<0 is reservation drift, not a
  // lazy CreateStackObject. Hexagon reserves scavenger FIs here
  // (HexagonFrameLowering.cpp:2098-2132); RISC-V getMoveF64FrameIndex is
  // ISel-time and is declined as a post-RA pattern.
  if (FuncInfo->getDR64PackFI() < 0) {
    bool NeedsPack = false;
    for (const MachineBasicBlock &ScanBB : MF) {
      for (const MachineInstr &ScanMI : ScanBB) {
        if (haydnInstrNeedsDR64PackSlot(ScanMI)) {
          NeedsPack = true;
          break;
        }
      }
      if (NeedsPack)
        break;
    }
    if (NeedsPack) {
      int FI = MF.getFrameInfo().CreateStackObject(/*Size=*/8,
                                                   /*Alignment=*/Align(8),
                                                   /*SpillSlot=*/true);
      FuncInfo->setDR64PackFI(FI);
      // Dedicated 4-byte spill home for the large-frame pack-base scavenger
      // (withDR64PackBase fallback). Kept disjoint from PostRAScratchFI so the
      // outer pack-base spill and the nested MatInt-scratch spill (inside
      // LOADI64's pack emission) never collide. Never indexed for SP motion.
      int BaseSpillFI = MF.getFrameInfo().CreateStackObject(/*Size=*/4,
                                                            /*Alignment=*/Align(4),
                                                            /*SpillSlot=*/true);
      FuncInfo->setDR64PackBaseSpillFI(BaseSpillFI);
    }
  }
}

int HaydnFrameLowering::getInitialCFAOffset(const MachineFunction &MF) const {
  return 0;
}

Register
HaydnFrameLowering::getInitialCFARegister(const MachineFunction &MF) const {
  return Haydn::R13; // SP
}

bool HaydnFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  // Return true to tell PEI we handle all callee-save spills ourselves
  // in emitPrologue. This prevents PEI from inserting default individual
  // storeRegToStackSlot calls that would duplicate our optimized sequence.
  (void)MBB;
  (void)MI;
  (void)CSI;
  (void)TRI;
  return true;
}

bool HaydnFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI,
    const TargetRegisterInfo *TRI) const {
  // Return true to tell PEI we handle all callee-save restores ourselves
  // in emitEpilogue. This prevents PEI from inserting default individual
  // loadRegToStackSlot calls that would duplicate our optimized sequence.
  // Mark all registers as restored so PEI doesn't try to handle them.
  for (auto &CS : CSI)
    CS.setRestored(true);
  (void)MBB;
  (void)MI;
  (void)TRI;
  return true;
}

MachineBasicBlock::iterator HaydnFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  // Sole owner of ADJCALLSTACKDOWN/UP (PEI). ExpandPseudos does not expand
  // them. Peer: RISCVFrameLowering.cpp:1868 eliminateCallFramePseudoInstr.
  // hasReservedCallFrame is false, so each call sequence must adjust SP by
  // the outgoing stack-arg size. Operand 0 is the raw byte amount from
  // CallLowering; ADJCALLSTACKDOWN decrements SP, ADJCALLSTACKUP restores
  // it. Zero (after alignment) is a no-op.
  //
  // Alignment (B1 / fir_blms* freestanding dual): SP must stay StackAlign(8)
  // across the call so the callee's DR CSR st64 spills never see ≡4 mod 8.
  // CC can report StackSize=4 for a single i32 stack arg if Size=4 was used;
  // always round Amount up to StackAlign (and to operand-1 align if larger).
  const HaydnInstrInfo *TII = MF.getSubtarget<HaydnSubtarget>().getInstrInfo();
  DebugLoc DL = MI->getDebugLoc();
  int64_t Amount = MI->getOperand(0).getImm();
  // Defensive: CallLowering already rounds; re-align so any other producer
  // of ADJCALLSTACK* cannot leave SP ≡4 mod StackAlign.
  if (Amount > 0)
    Amount = static_cast<int64_t>(
        alignTo(static_cast<uint64_t>(Amount), getStackAlign()));

  if (Amount != 0) {
    unsigned AdjOpc;
    if (MI->getOpcode() == Haydn::ADJCALLSTACKDOWN) {
      // SP must DECREASE by Amount on ADJCALLSTACKDOWN. SUBI32 sp,sp,+Amount
      // does exactly that. Do NOT negate Amount here — a prior bug plus
      // SUBI32 cancelled to a SP *increase* (grew into the caller's frame).
      AdjOpc = Haydn::SUBI32;
    } else {
      AdjOpc = Haydn::ADDI32_W;
    }

    if (isInt<20>(Amount)) {
      BuildMI(MBB, MI, DL, TII->get(AdjOpc), Haydn::R13)
          .addReg(Haydn::R13)
          .addImm(Amount);
    } else {
      // Large adjustment: MatInt into PEI scratch, then SUB32/ADD32.
      Register TempReg =
          requirePEIScratchReg(MBB, MI, /*Avoid=*/Haydn::R13);
      emitMaterializeImm32(MBB, MI, DL, TII, TempReg, Amount,
                           MachineInstr::NoFlags);
      unsigned WideOpc = (MI->getOpcode() == Haydn::ADJCALLSTACKDOWN)
                             ? Haydn::SUB32
                             : Haydn::ADD32;
      BuildMI(MBB, MI, DL, TII->get(WideOpc), Haydn::R13)
          .addReg(Haydn::R13)
          .addReg(TempReg);
    }
  }

  return MBB.erase(MI);
}
