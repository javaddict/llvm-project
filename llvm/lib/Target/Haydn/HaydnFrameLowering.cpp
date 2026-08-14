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
#include "HaydnRegisterInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMatInt.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "haydn-frame-lowering"

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
  HaydnMatInt::InstSeq Seq = HaydnMatInt::generate(Imm);
  Register Current = Haydn::R0;
  for (const HaydnMatInt::Inst &Inst : Seq) {
    BuildMI(MBB, MBBI, DL, TII->get(Inst.Opc), Dst)
        .addReg(Current)
        .addImm(Inst.Imm)
        .setMIFlag(FrameFlag);
    Current = Dst;
  }
}

// PEI post-RA scratch for prologue/epilogue CSR addressing.
// ABI-safe filter = call-clobbered set (not CSRs), not entry live-in, not Avoid.
// ProtectRetCC (epilogue only): also exclude RetCC R1/R2 so large FrameDestroy
// materialize cannot clobber live return values.
static Register getPEIScratchReg(const MachineFunction &MF, Register Avoid,
                                 Register Avoid2 = Register(),
                                 bool ProtectRetCC = false) {
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const HaydnRegisterInfo *TRI = ST.getRegisterInfo();

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const MachineBasicBlock &Entry = MF.front();
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

  auto isABISafeScratch = [&](MCPhysReg Reg) {
    if (Reg == Avoid || Reg == Avoid2 || Reg == Haydn::R0 || Reg == Haydn::R14)
      return false;
    if (ProtectRetCC && isRetCCPhys(Reg))
      return false;
    if (MRI.isReserved(Reg))
      return false;
    if (Entry.isLiveIn(Reg))
      return false;
    if (isCalleeSavedPhys(Reg))
      return false;
    return Haydn::GPR32NoSPNoLRRegClass.contains(Reg);
  };

  for (MCPhysReg Reg : Haydn::GPR32NoSPNoLRRegClass)
    if (isABISafeScratch(Reg) && !MRI.isPhysRegUsed(Reg))
      return Reg;

  for (MCPhysReg Reg : Haydn::GPR32NoSPNoLRRegClass)
    if (isABISafeScratch(Reg))
      return Reg;

  if (Avoid != Haydn::R12 && Avoid2 != Haydn::R12 &&
      !(ProtectRetCC && isRetCCPhys(Haydn::R12)) &&
      !MRI.isReserved(Haydn::R12) && !Entry.isLiveIn(Haydn::R12) &&
      !isCalleeSavedPhys(Haydn::R12))
    return Haydn::R12;

  report_fatal_error(
      "Haydn PEI: no ABI-safe scratch GPR (need call-clobbered free reg)");
}

static int64_t getCalleeSavedCFAOffset(const MachineFunction &MF, int FI) {
  return MF.getFrameInfo().getObjectOffset(FI);
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
// immediate-offset ST32/ST64 is emitted (migrates to ST32_M0S0LS via the
// finalizer). When the offset is out of range (negative or large CSR slots)
// the offset is materialized into a PEI scratch (\c getPEIScratchReg: ABI
// call-clobbered / reserved R12 — same contract as EFI scavenger) and the
// ST32_REG_M0S0LS / ST64_REG_M0S0LS Mode-0 register-offset variant is emitted.
// This eliminates the legacy Haydn32 LS emit path for CSR spills (Gap 1
// tryDecodeLegacyLSProbe).
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
  // logical REG forms only; private *_S0 peers are MC encode-only.
  unsigned RegOpc = (StoreOpc == Haydn::ST64) ? Haydn::ST64_REG_M0S0LS
                                              : Haydn::ST32_REG_M0S0LS;
  // Short-lived offset: soft-zero R0 when free; restore after store.
  assert(SrcReg != Haydn::R0 && BaseReg != Haydn::R0);
  Register OffReg = Haydn::R0;
  if (Offset != 0) {
    if (isInt<20>(Offset)) {
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::ADDI32_W), OffReg)
          .addReg(OffReg)
          .addImm(Offset)
          .setMIFlag(FrameFlag);
    } else {
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::LOADI32), OffReg)
          .addImm(Offset)
          .setMIFlag(FrameFlag);
    }
  }
  BuildMI(MBB, MBBI, DL, TII->get(RegOpc))
      .addReg(SrcReg)
      .addReg(BaseReg)
      .addReg(OffReg)
      .setMIFlag(FrameFlag);
  BuildMI(MBB, MBBI, DL, TII->get(Haydn::XOR32), Haydn::R0)
      .addReg(Haydn::R0)
      .addReg(Haydn::R0)
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
  // Soft-zero R0 as short-lived offset temp (avoids stealing R1 return).
  assert(DstReg != Haydn::R0 && BaseReg != Haydn::R0);
  Register OffReg = Haydn::R0;
  if (Offset != 0) {
    if (isInt<20>(Offset)) {
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::ADDI32_W), OffReg)
          .addReg(OffReg)
          .addImm(Offset)
          .setMIFlag(FrameFlag);
    } else {
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::LOADI32), OffReg)
          .addImm(Offset)
          .setMIFlag(FrameFlag);
    }
  }
  BuildMI(MBB, MBBI, DL, TII->get(RegOpc), DstReg)
      .addReg(BaseReg)
      .addReg(OffReg)
      .setMIFlag(FrameFlag);
  BuildMI(MBB, MBBI, DL, TII->get(Haydn::XOR32), Haydn::R0)
      .addReg(Haydn::R0)
      .addReg(Haydn::R0)
      .setMIFlag(FrameFlag);
}

// Determine the size of the frame and maximum call frame size.
void HaydnFrameLowering::determineFrameLayout(MachineFunction &MF) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // Get the number of bytes to allocate from the FrameInfo.
  uint64_t FrameSize = MFI.getStackSize();

  // Get the alignment.
  Align StackAlign = getStackAlign();

  // Get the maximum call frame size of all the calls.
  uint64_t MaxCallFrameSize = MFI.getMaxCallFrameSize();

  // If we have dynamic alloca then MaxCallFrameSize needs to be aligned so
  // that allocations will be aligned.
  if (MFI.hasVarSizedObjects())
    MaxCallFrameSize = alignTo(MaxCallFrameSize, StackAlign);

  // Update maximum call frame size.
  MFI.setMaxCallFrameSize(MaxCallFrameSize);

  // Include call frame size in total.
  // hasReservedCallFrame is false for Haydn since we adjust SP before calls
  if (!(hasReservedCallFrame(MF) && MFI.adjustsStack()))
    FrameSize += MaxCallFrameSize;

  // Make sure the frame is aligned.
  FrameSize = alignTo(FrameSize, StackAlign);

  // Update frame info.
  MFI.setStackSize(FrameSize);
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

  return false;
}

// Check if hasReservedCallFrame - not used for Hayden (always false).
bool HaydnFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  // Haydn adjusts SP before calls, so call frame space is not reserved
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

  return true;
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
    // slice Z: zero R0 via XOR32 R0,R0,R0 (x^x=0 identity), not the
    // retired ZERO_GPR pseudo. The finalizer rewrites standalone XOR32 ->
    // XOR32_M0 (EW_64Bit), so this is slot-OR compatible — the packetizer
    // bundled prologue zero no longer fatals under -haydn-pure-tblgen-emit
    // (the 8 obj-emit fatals that gated row-auction retirement).
    BuildMI(MBB, MBBI, DL, TII->get(Haydn::XOR32), Haydn::R0)
        .addReg(Haydn::R0)
        .addReg(Haydn::R0)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Determine the correct frame layout
  determineFrameLayout(MF);

  // Get the number of bytes to allocate from the FrameInfo.
  uint64_t StackSize = MFI.getStackSize();

  // Align the stack if needed
  Align StackAlign = getStackAlign();
  uint64_t AlignedStackSize = alignTo(StackSize, StackAlign);

  // Update MFI with aligned size
  MFI.setStackSize(AlignedStackSize);

  // Get callee-saved registers
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

  // Allocate stack space FIRST, before saving callee-saved registers.
  // This is critical for interrupt safety on baremetal: if an interrupt fires
  // between saving registers and decrementing SP, the saved values (stored below
  // the current SP) could be corrupted by the ISR's stack usage. By decrementing
  // SP first, all saves go into the properly-allocated frame above the new SP.
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
      Register TempReg = getPEIScratchReg(MF, /*Avoid=*/Haydn::R13);
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
    struct CSReg { Register Reg; Register BaseReg; int Offset; };
    SmallVector<CSReg, 8> GPRCSRegs;
    for (const CalleeSavedInfo &CI : llvm::reverse(CSI)) {
      Register Reg = CI.getReg();
      int FrameIdx = CI.getFrameIdx();
      const TargetRegisterClass *RC = Reg.isVirtual()
          ? MRI.getRegClassOrNull(Reg)
          : TRI->getMinimalPhysRegClass(Reg);
      if (RC && (RC == &Haydn::GPR32RegClass ||
                 RC == &Haydn::GPR32NoSPNoLRRegClass)) {
        Register FrameReg;
        int Offset = static_cast<int>(
            getFrameIndexReference(MF, FrameIdx, FrameReg).getFixed());
        // address CSR slots SP-relative. In the prologue the frame
        // pointer is NOT yet established (FP is set up after these stores), so
        // an FP-relative store would hit the caller's FP. In the epilogue FP is
        // live but SP is still decremented (CSR restores precede `add sp,N`)
        // and FP == SP + AlignedStackSize, so [FP+off] == [SP+StackSize+off] at
        // runtime — SP-relative addressing reads/writes the exact slot the
        // prologue saved. getFrameIndexReference folded the stack size in for
        // the !hasFP (SP-base) case; fold it in here for the hasFP (FP) case.
        int SpOffset = Offset;
        if (FrameReg != Haydn::R13)
          SpOffset += static_cast<int>(MFI.getStackSize());
        GPRCSRegs.push_back({Reg, Haydn::R13, SpOffset});
      }
    }

    // PEI scratch for stride-4 base; fall back to individual stores if that
    // scratch is itself in the CSR list (would clobber before saving it).
    Register StrideScratch = getPEIScratchReg(MF, /*Avoid=*/Haydn::R13);
    bool ScratchNeedsSaving = false;
    for (const auto &E : GPRCSRegs) {
      if (E.Reg == StrideScratch) {
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
    struct CSReg { Register Reg; Register BaseReg; int Offset; };
    SmallVector<CSReg, 8> DRCSRegs;
    for (const CalleeSavedInfo &CI : llvm::reverse(CSI)) {
      Register Reg = CI.getReg();
      int FrameIdx = CI.getFrameIdx();
      const TargetRegisterClass *RC = Reg.isVirtual()
          ? MRI.getRegClassOrNull(Reg)
          : TRI->getMinimalPhysRegClass(Reg);
      if (RC && RC == &Haydn::DR64RegClass) {
        Register FrameReg;
        int Offset = static_cast<int>(
            getFrameIndexReference(MF, FrameIdx, FrameReg).getFixed());
        // SP-relative CSR addressing (FP not yet set in prologue;
        // SP still decremented in epilogue). See the GPR block for rationale.
        int SpOffset = Offset;
        if (FrameReg != Haydn::R13)
          SpOffset += static_cast<int>(MFI.getStackSize());
        DRCSRegs.push_back({Reg, Haydn::R13, SpOffset});
      }
    }

    if (DRCSRegs.size() >= 2) {
      unsigned RunLen = 1;
      while (RunLen < DRCSRegs.size() &&
             DRCSRegs[RunLen].Offset ==
                 DRCSRegs[RunLen - 1].Offset + 8) {
        ++RunLen;
      }

      if (RunLen >= 2) {
        Register BaseReg = getPEIScratchReg(MF, /*Avoid=*/Haydn::R13);
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
      Register TempReg = getPEIScratchReg(MF, /*Avoid=*/FP);
      emitMaterializeImm32(MBB, MBBI, DL, TII, TempReg,
                           static_cast<int64_t>(AlignedStackSize),
                           MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, DL, TII->get(Haydn::ADD32), FP)
          .addReg(Haydn::R13)
          .addReg(TempReg)
          .setMIFlag(MachineInstr::FrameSetup);
    }

    CFIIndex = MF.addFrameInst(MCCFIInstruction::createDefCfaRegister(
        nullptr, MCRI->getDwarfRegNum(FP, true)));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlags(MachineInstr::FrameSetup);
  }

  // Emit.cfi_offset directives for each saved callee-saved register.
  // CFI offsets are CFA-relative (CFA = SP for non-FP frames, FP otherwise).
  // getObjectOffset is frame-layout-relative (negative, below the post-prologue
  // SP). For SP-CFA we must add getStackSize to get the CFA-relative offset
  // otherwise the offset is double-counted. getFrameIndexReference handles the
  // FP-vs-SP distinction and matches the path used by eliminateFrameIndex.
  // See cfi-sp-cfa-requires-stacksize and F12.
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
  MachineRegisterInfo &MRI = MF.getRegInfo();

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

  // Re-zero soft-zero R0 before CSR restores. Long-branch / far-jump
  // sequences historically used `JALR R0, scratch, 0` to "discard" the link;
  // the ISS still writes PC_next into R0 (not hardwired zero). LOADI32 in
  // emitCSRLoad then materializes offsets from a non-zero R0 and restores
  // CSRs from the wrong stack slots — breaking values live across calls
  // (e.g. rem pointer in R8 after __udivmoddi4). Harmless if R0 is already 0.
  BuildMI(MBB, MBBI, DL, TII->get(Haydn::XOR32), Haydn::R0)
      .addReg(Haydn::R0)
      .addReg(Haydn::R0)
      .setMIFlag(MachineInstr::FrameDestroy);

  // Get the number of bytes to allocate from the FrameInfo.
  uint64_t StackSize = MFI.getStackSize();

  // Dyn-alloca / VLA moves SP. CSR restores need post-prologue SP.
  // Recover from FP (R14): prologue set FP = SP + StackSize, so
  // fixed-frame SP = FP - StackSize. BP is folded into FP.
  if (MFI.hasVarSizedObjects()) {
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
      Register TempReg = getPEIScratchReg(MF, /*Avoid=*/FP, /*Avoid2=*/Haydn::R13,
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

  // Restore callee-saved registers in reverse order of saving
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

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
    struct CSReg { Register Reg; Register BaseReg; int Offset; };
    SmallVector<CSReg, 8> DRCSRegs;
    for (const CalleeSavedInfo &CI : llvm::reverse(CSI)) {
      Register Reg = CI.getReg();
      int FrameIdx = CI.getFrameIdx();
      const TargetRegisterClass *RC = Reg.isVirtual()
          ? MRI.getRegClassOrNull(Reg)
          : TRI->getMinimalPhysRegClass(Reg);
      if (RC && RC == &Haydn::DR64RegClass) {
        Register FrameReg;
        int Offset = static_cast<int>(
            getFrameIndexReference(MF, FrameIdx, FrameReg).getFixed());
        // SP-relative CSR addressing (FP not yet set in prologue;
        // SP still decremented in epilogue). See the GPR block for rationale.
        int SpOffset = Offset;
        if (FrameReg != Haydn::R13)
          SpOffset += static_cast<int>(MFI.getStackSize());
        DRCSRegs.push_back({Reg, Haydn::R13, SpOffset});
      }
    }

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
    struct CSReg { Register Reg; Register BaseReg; int Offset; };
    SmallVector<CSReg, 8> GPRCSRegs;
    for (const CalleeSavedInfo &CI : llvm::reverse(CSI)) {
      Register Reg = CI.getReg();
      int FrameIdx = CI.getFrameIdx();
      const TargetRegisterClass *RC = Reg.isVirtual()
          ? MRI.getRegClassOrNull(Reg)
          : TRI->getMinimalPhysRegClass(Reg);
      if (RC && (RC == &Haydn::GPR32RegClass ||
                 RC == &Haydn::GPR32NoSPNoLRRegClass)) {
        Register FrameReg;
        int Offset = static_cast<int>(
            getFrameIndexReference(MF, FrameIdx, FrameReg).getFixed());
        // address CSR slots SP-relative. In the prologue the frame
        // pointer is NOT yet established (FP is set up after these stores), so
        // an FP-relative store would hit the caller's FP. In the epilogue FP is
        // live but SP is still decremented (CSR restores precede `add sp,N`)
        // and FP == SP + AlignedStackSize, so [FP+off] == [SP+StackSize+off] at
        // runtime — SP-relative addressing reads/writes the exact slot the
        // prologue saved. getFrameIndexReference folded the stack size in for
        // the !hasFP (SP-base) case; fold it in here for the hasFP (FP) case.
        int SpOffset = Offset;
        if (FrameReg != Haydn::R13)
          SpOffset += static_cast<int>(MFI.getStackSize());
        GPRCSRegs.push_back({Reg, Haydn::R13, SpOffset});
      }
    }

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
      Register TempReg = getPEIScratchReg(MF, /*Avoid=*/Haydn::R13,
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

  // Use FP if we have one, otherwise use SP
  FrameReg = TRI->getFrameRegister(MF);

  // The offset is the object's offset from the frame pointer/stack pointer
  // For SP (when no FP), the offset is negative (below SP)
  // For FP, the offset is positive (above FP for locals, below for saved regs)
  int64_t Offset = MFI.getObjectOffset(FI);

  // If using SP, adjust offset by stack size
  if (!hasFP(MF)) {
    Offset += MFI.getStackSize();
  }

  return StackOffset::getFixed(Offset);
}

bool HaydnFrameLowering::allocateScavengingFrameIndexesNearIncomingSP(
    const MachineFunction &MF) const {
  // hasFP → near FP; !hasFP → late near final SP (see header).
  return hasFP(MF);
}

void HaydnFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  if (!RS)
    return;

  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const TargetRegisterInfo *TRI = ST.getRegisterInfo();
  const HaydnInstrInfo *TII = ST.getInstrInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterClass &RC = Haydn::GPR32RegClass;
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();

  // Emergency-slot policy (AIE model: no free AT). EFI uses vregs;
  // scavengeFrameVirtualRegs may spill under full pressure even when
  // estimateStackSize is small (yarpgen_seed2). Keep one emergency FI
  // 4 bytes, same insurance as ARM without a free IP. Never pad N>1.
  // Nested phys scavenge is gone (vreg EFI).
  unsigned ScavSlotsNum = 1;

  // Far branch relaxation: product parcels are Format E (12 B via
  // productParcelBytes). Large functions may need insertIndirectBranch;
  // reserve a FI for full-pressure (BranchRelaxation's fresh RegScavenger).
  unsigned EstBytes = 0;
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      EstBytes += TII->getInstSizeInBytes(MI);
  bool MayNeedBranchRelax = !isInt<16>(static_cast<int64_t>(EstBytes));
  if (MayNeedBranchRelax)
    ScavSlotsNum = std::max(ScavSlotsNum, 1u);

  for (unsigned I = 0; I < ScavSlotsNum; ++I) {
    int FI = MFI.CreateSpillStackObject(TRI->getSpillSize(RC),
                                        TRI->getSpillAlign(RC));
    RS->addScavengingFrameIndex(FI);
    // First slot doubles as the branch-relax dedicated spill (RISC-V style).
    if (FuncInfo->getBranchRelaxationScratchFI() < 0)
      FuncInfo->setBranchRelaxationScratchFI(FI);
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

  // F13: JAL writes LR (R15). Non-leaf must save/restore R15.
  if (MF.getFrameInfo().hasCalls()) {
    SavedRegs.set(Haydn::R15);
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

  // Permanent 8-byte in-frame pack slot for DR64 construction from two GPR32
  // halves (LOADI64 both-halves-nonzero constants; MOV_GPR_TO_DR64 two-live-
  // GPR general case). Reserved ONLY when such a pack is present so leaf
  // functions pay no frame growth. The single fixed slot is reused by every
  // pack in the function; each pack is a local store-store-load with no SP
  // motion. This replaces the dynamic SUBI32/ADDI32_W $r13,8 transient that
  // shifted SP mid-function and corrupted sibling SP-relative fixed objects.
  // Align(8) places it first among locals (smallest offset → short-form LS).
  if (FuncInfo->getDR64PackFI() < 0) {
    bool NeedsPack = false;
    for (const MachineBasicBlock &ScanBB : MF) {
      for (const MachineInstr &ScanMI : ScanBB) {
        if (ScanMI.getOpcode() == Haydn::LOADI64) {
          // Register-only fast paths (Hi==0 zero-extend, Hi==-1&&Lo<0
          // sign-extend) need no slot; only general both-halves-nonzero
          // constants do. Non-immediate (relocatable) LOADI64 is rare but
          // conservatively treated as a general pack.
          if (!ScanMI.getOperand(1).isImm()) {
            NeedsPack = true;
          } else {
            uint64_t V =
                static_cast<uint64_t>(ScanMI.getOperand(1).getImm());
            int32_t Lo = static_cast<int32_t>(V & 0xFFFFFFFFu);
            int32_t Hi = static_cast<int32_t>((V >> 32) & 0xFFFFFFFFu);
            if (Hi != 0 && !(Hi == -1 && Lo < 0))
              NeedsPack = true;
          }
        } else if (ScanMI.getOpcode() == Haydn::MOV_GPR_TO_DR64) {
          // R0-half packs take the stackless shift path; only the general
          // two-live-GPR case (neither source is R0) needs the slot.
          if (ScanMI.getNumOperands() > 2 && ScanMI.getOperand(1).isReg() &&
              ScanMI.getOperand(2).isReg()) {
            Register SrcLo = ScanMI.getOperand(1).getReg();
            Register SrcHi = ScanMI.getOperand(2).getReg();
            if (SrcLo != Haydn::R0 && SrcHi != Haydn::R0)
              NeedsPack = true;
          }
        }
        if (NeedsPack)
          break;
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
  // F17: Expand ADJCALLSTACKDOWN/UP. hasReservedCallFrame is false, so each
  // call sequence must adjust SP by the outgoing stack-arg size. Operand 0 is
  // the raw byte amount from CallLowering; ADJCALLSTACKDOWN decrements SP,
  // ADJCALLSTACKUP restores it. Zero (after alignment) is a no-op.
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
      Register TempReg = getPEIScratchReg(MF, /*Avoid=*/Haydn::R13);
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
