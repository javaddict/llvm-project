//===-- HaydnCallLowering.cpp - Call lowering -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// This file implements the lowering of LLVM calls to machine code calls for
// GlobalISel.
//===----------------------------------------------------------------------===//

#include "HaydnCallLowering.h"
#include "HaydnFrameLowering.h"
#include "HaydnISelLowering.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnRegisterInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-call-lowering"

// Include the tablegen-generated calling convention functions (static).
// These define CC_Haydn and RetCC_Haydn used by IncomingValueAssigner
// OutgoingValueAssigner below.
#include "HaydnGenCallingConv.inc"

namespace {
// The Haydn GPR argument registers, R1–R7. R0 is reserved as soft-zero and is
// not used for argument passing (CLAUDE.md register map). Used by the varargs
// reg-save-area setup.
static const MCPhysReg HaydnArgGPRs[] = {Haydn::R1, Haydn::R2, Haydn::R3,
                                         Haydn::R4, Haydn::R5, Haydn::R6,
                                         Haydn::R7};
// The Haydn DR64 argument registers, D0–D3. i64/f64/SIMD args are assigned to
// this bank (HaydnCallingConv.td). Used by the varargs DR save-area setup so
// unnamed variadic i64/f64 args spilled here are reachable by va_arg. See
// (two-bank varargs) /.
static const MCPhysReg HaydnArgDRs[] = {Haydn::D0, Haydn::D1, Haydn::D2,
                                        Haydn::D3};
constexpr unsigned HaydnGPRSaveSize = 4;  // GPR32 = 4 bytes per saved reg
constexpr unsigned HaydnDRSaveSize = 8;   // DR64 = 8 bytes per saved reg

// Custom incoming-arg assigner that captures the first-unallocated index of
// BOTH argument banks (GPR R1–R7 and DR D0–D3) as VALUES, plus the final
// stack-arg size, so lowerFormalArguments can size the varargs save areas
// without holding a pointer to the (callee-local) CCState. See F15/ for
// the original GPR save area; removed the dangling `CCState *State`
// member (use-after-scope UB); added the DR bank.
struct HaydnIncomingAssigner : public CallLowering::IncomingValueAssigner {
  // Index of the first unallocated GPR arg reg after the fixed args are
  // placed. Default = size(HaydnArgGPRs) means all GPR arg regs were consumed
  // by fixed args -> no varargs GPRs to spill.
  unsigned FirstUnallocatedGPR = 0;
  unsigned FirstUnallocatedDR = 0;

  using IncomingValueAssigner::IncomingValueAssigner;

  bool assignArg(unsigned ValNo, EVT OrigVT, MVT ValVT, MVT LocVT,
                 CCValAssign::LocInfo LocInfo, const CallLowering::ArgInfo &Info,
                 ISD::ArgFlagsTy Flags, CCState &CCState) override {
    // Delegate first so the CC function updates the allocation state; then
    // snapshot the first-unallocated indices as values. After the final arg
    // these hold the correct values for the varargs spill loops. : no
    // pointer to the callee-local CCState is retained.
    bool Fail = IncomingValueAssigner::assignArg(ValNo, OrigVT, ValVT, LocVT,
                                                 LocInfo, Info, Flags, CCState);
    FirstUnallocatedGPR = CCState.getFirstUnallocated(HaydnArgGPRs);
    FirstUnallocatedDR = CCState.getFirstUnallocated(HaydnArgDRs);
    return Fail;
  }
};
} // end anonymous namespace

HaydnCallLowering::HaydnCallLowering(const HaydnTargetLowering &TLI)
    : CallLowering(&TLI) {}

namespace {

// Spill the unallocated GPR arg registers (R1–R7 minus those consumed by the
// fixed arguments) AND the unallocated DR arg registers (D0–D3 minus those
// consumed by fixed i64/f64/SIMD args) into TWO register save areas on the
// stack, and record the frame indices + sizes so G_VASTART / va_arg can find
// them. This is the two-bank varargs ABI : a single GPR-only
// save area lost unnamed variadic i64/f64 args because they arrived in D
// registers that va_arg never read. The structured va_list (initialized by
// VASTART in HaydnAsmPrinter) carries an independent cursor per bank.
// Layout (negative offsets below the incoming stack-arg region):
// GPR save area: [VarArgsGprFI, VarArgsGprSize bytes] R1..R7 tail, 4B each
// [optional 4B pad so DR base is 8-aligned when GprSize is 4 mod 8 — ]
// DR save area: [VarArgsDrFI, VarArgsDrSize bytes] D0..D3 tail, 8B each
// overflow stack: [VarArgsStackFI] first stack vararg
// `__gr_top` points one past the end of the GPR area, `__dr_top` one past the
// end of the DR area (downward walk), `__stack` at the overflow base.
static void saveVarArgRegisters(MachineIRBuilder &MIRBuilder,
                                HaydnIncomingAssigner &Assigner) {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  HaydnMachineFunctionInfo *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();

  // read values (no pointer to the destroyed CCState). : both banks.
  unsigned FirstUnallocGPR = Assigner.FirstUnallocatedGPR;
  unsigned FirstUnallocDR = Assigner.FirstUnallocatedDR;
  unsigned NumVarGPRs = std::size(HaydnArgGPRs) - FirstUnallocGPR;
  unsigned NumVarDRs = std::size(HaydnArgDRs) - FirstUnallocDR;
  int VarArgsGprSize = static_cast<int>(NumVarGPRs * HaydnGPRSaveSize);
  int VarArgsDrSize = static_cast<int>(NumVarDRs * HaydnDRSaveSize);

  const LLT PtrTy = LLT::pointer(0, 32);
  const LLT S32 = LLT::scalar(32);
  const LLT S64 = LLT::scalar(64);

  // GPR save area (R1–R7 tail)
  int GprFI;
  if (VarArgsGprSize == 0) {
    // All GPR arg regs consumed: no GPR varargs. Park a zero-size placeholder
    // at the overflow base so VASTART has a valid FI; __gr_off starts at 0 and
    // immediately overflows to the stack region.
    GprFI = MFI.CreateFixedObject(HaydnGPRSaveSize,
                                  static_cast<int64_t>(Assigner.StackSize), true);
  } else {
    // Negative offset below the incoming stack args (RISC-V-style layout).
    int64_t SaveAreaOffset = -VarArgsGprSize;
    GprFI = MFI.CreateFixedObject(VarArgsGprSize, SaveAreaOffset, true);

    // Address each slot as FI + constant offset from a *shared* base.
    // Do NOT walk a G_PTR_ADD chain (base = base + step) across spills: the
    // GISel pre/post-inc combiner may fuse one store into D_SDW_PRE /
    // S_SW_PRE and the packetizer can then schedule a later sibling store
    // after the writeback, so its imm is applied to the advanced base and
    // lands in the wrong bank (GPR save / incoming stack args) → ABORT.
    auto GprBase = MIRBuilder.buildFrameIndex(PtrTy, GprFI);
    for (unsigned I = FirstUnallocGPR; I < std::size(HaydnArgGPRs); ++I) {
      MCPhysReg PhysReg = HaydnArgGPRs[I];
      Register VReg = MRI.createGenericVirtualRegister(S32);
      MCRegister PhysMCReg = PhysReg;
      MRI.addLiveIn(PhysMCReg);
      MIRBuilder.getMBB().addLiveIn(PhysMCReg);
      MIRBuilder.buildCopy(VReg, Register(PhysMCReg));

      unsigned SlotOff = (I - FirstUnallocGPR) * HaydnGPRSaveSize;
      Register Addr = GprBase.getReg(0);
      if (SlotOff != 0) {
        auto Off = MIRBuilder.buildConstant(S32, SlotOff);
        Addr = MIRBuilder.buildPtrAdd(PtrTy, GprBase.getReg(0), Off).getReg(0);
      }
      auto MPO = MachinePointerInfo::getFixedStack(MF, GprFI, SlotOff);
      MIRBuilder.buildStore(VReg, Addr, MPO, inferAlignFromPtrInfo(MF, MPO));
    }
  }

  // DR save area (D0–D3 tail)
  // previously only the GPR bank was spilled, so unnamed variadic
  // i64/f64 args (which CC_Haydn places in D0–D3) were lost. Spill the DR tail
  // into a second frame object so the va_list DR cursor can read them.
  int DrFI;
  if (VarArgsDrSize == 0) {
    DrFI = MFI.CreateFixedObject(HaydnDRSaveSize,
                                 static_cast<int64_t>(Assigner.StackSize), true);
  } else {
    // Place the DR save area below the GPR save area. Each D reg is 8B and
    // ST64/D_SDW requires 8-byte alignment. Incoming SP and the final
    // FrameSize are both StackAlign(8), so DR base
    // IncomingSP - alignTo(GprSize, 8) - DrSize
    // is 8-aligned iff the padded GPR region is a multiple of 8. When
    // VarArgsGprSize is 4 mod 8 (odd count of leftover GPRs, e.g. two fixed
    // args leaving R3–R7 = 20 bytes), insert 4 bytes of padding between the
    // banks so st64 is not emitted at sp+4k+4.
    int64_t AlignedGprRegion = static_cast<int64_t>(
        alignTo(static_cast<uint64_t>(VarArgsGprSize), Align(HaydnDRSaveSize)));
    int64_t DrSaveAreaOffset = -(AlignedGprRegion + VarArgsDrSize);
    DrFI = MFI.CreateFixedObject(VarArgsDrSize, DrSaveAreaOffset, true);
    MFI.setObjectAlignment(DrFI, Align(HaydnDRSaveSize));

    // Independent FI+offset per DR slot (same reason as GPR: no pre-inc chain).
    auto DrBase = MIRBuilder.buildFrameIndex(PtrTy, DrFI);
    for (unsigned I = FirstUnallocDR; I < std::size(HaydnArgDRs); ++I) {
      MCPhysReg PhysReg = HaydnArgDRs[I];
      Register VReg = MRI.createGenericVirtualRegister(S64);
      MCRegister PhysMCReg = PhysReg;
      MRI.addLiveIn(PhysMCReg);
      MIRBuilder.getMBB().addLiveIn(PhysMCReg);
      MIRBuilder.buildCopy(VReg, Register(PhysMCReg));

      unsigned SlotOff = (I - FirstUnallocDR) * HaydnDRSaveSize;
      Register Addr = DrBase.getReg(0);
      if (SlotOff != 0) {
        auto Off = MIRBuilder.buildConstant(S32, SlotOff);
        Addr = MIRBuilder.buildPtrAdd(PtrTy, DrBase.getReg(0), Off).getReg(0);
      }
      auto MPO = MachinePointerInfo::getFixedStack(MF, DrFI, SlotOff);
      MIRBuilder.buildStore(VReg, Addr, MPO, inferAlignFromPtrInfo(MF, MPO));
    }
  }

  // Overflow stack-arg base
  // Unnamed varargs that did not fit in either register bank live in the
  // caller's stack-arg region, starting at Assigner.StackSize (the byte offset
  // of the first stack arg relative to the incoming SP).
  int StackFI =
      MFI.CreateFixedObject(HaydnGPRSaveSize,
                            static_cast<int64_t>(Assigner.StackSize), true);

  FuncInfo->setVarArgsGprFI(GprFI);
  FuncInfo->setVarArgsGprSize(VarArgsGprSize);
  FuncInfo->setVarArgsDrFI(DrFI);
  FuncInfo->setVarArgsDrSize(VarArgsDrSize);
  FuncInfo->setVarArgsStackFI(StackFI);
  // Retained for any reader of the legacy single-cursor offset (now unused by
  // VASTART, which initializes the structured va_list from the fields above).
  FuncInfo->setVarArgsFrameIndex(GprFI);
  FuncInfo->setVarArgsStackOffset(0);
  // Signal VASTART that the save areas exist. The FIs themselves cannot be
  // tested with `< 0` because CreateFixedObject returns negative indices.
  FuncInfo->setHasVarArgsSaveAreas(true);
}

// Incoming-side handlers (formal args + call returns). Pattern from
// RISCVCallLowering / X86CallLowering / AArch64CallLowering:
// shared IncomingValueHandler base
// FormalArgHandler: phys reg is BB live-in
// CallReturnHandler: phys reg is implicit-def of the call
struct HaydnIncomingValueHandler : public CallLowering::IncomingValueHandler {
  HaydnIncomingValueHandler(MachineIRBuilder &MIRBuilder,
                             MachineRegisterInfo &MRI)
      : IncomingValueHandler(MIRBuilder, MRI) {}

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    auto &MFI = MIRBuilder.getMF().getFrameInfo();
    int FI = MFI.CreateFixedObject(Size, Offset, true);
    MPO = MachinePointerInfo::getFixedStack(MIRBuilder.getMF(), FI);
    return MIRBuilder.buildFrameIndex(LLT::pointer(0, 32), FI).getReg(0);
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override {
    markPhysRegUsed(PhysReg);
    IncomingValueHandler::assignValueToReg(ValVReg, PhysReg, VA);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    MachineFunction &MF = MIRBuilder.getMF();
    auto *MMO = MF.getMachineMemOperand(
        MPO, MachineMemOperand::MOLoad | MachineMemOperand::MOInvariant, MemTy,
        inferAlignFromPtrInfo(MF, MPO));
    MIRBuilder.buildLoad(ValVReg, Addr, *MMO);
  }

  // Formal parameters → BB live-in; call return → implicit-def of the call.
  virtual void markPhysRegUsed(MCRegister PhysReg) = 0;
};

struct HaydnFormalArgHandler : public HaydnIncomingValueHandler {
  HaydnFormalArgHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI)
      : HaydnIncomingValueHandler(MIRBuilder, MRI) {}

  void markPhysRegUsed(MCRegister PhysReg) override {
    MIRBuilder.getMRI()->addLiveIn(PhysReg);
    MIRBuilder.getMBB().addLiveIn(PhysReg);
  }
};

struct HaydnCallReturnHandler : public HaydnIncomingValueHandler {
  HaydnCallReturnHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                         MachineInstrBuilder MIB)
      : HaydnIncomingValueHandler(MIRBuilder, MRI), MIB(MIB) {}

  void markPhysRegUsed(MCRegister PhysReg) override {
    MIB.addDef(PhysReg, RegState::Implicit);
  }

  MachineInstrBuilder MIB;
};

struct HaydnOutgoingValueHandler : public CallLowering::OutgoingValueHandler {
  HaydnOutgoingValueHandler(MachineIRBuilder &MIRBuilder,
                             MachineRegisterInfo &MRI,
                             MachineInstrBuilder &MIB)
      : OutgoingValueHandler(MIRBuilder, MRI), MIB(MIB) {}

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    MachineFunction &MF = MIRBuilder.getMF();
    LLT PtrTy = LLT::pointer(0, 32);
    LLT S32 = LLT::scalar(32);

    if (!SPReg)
      SPReg = MIRBuilder.buildCopy(PtrTy, Register(Haydn::R13)).getReg(0);

    auto OffsetReg = MIRBuilder.buildConstant(S32, Offset);
    auto AddrReg = MIRBuilder.buildPtrAdd(PtrTy, SPReg, OffsetReg);

    MPO = MachinePointerInfo::getStack(MF, Offset);
    return AddrReg.getReg(0);
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override {
    Register ExtReg = extendRegister(ValVReg, VA);
    MIRBuilder.buildCopy(PhysReg, ExtReg);
    MIB.addReg(PhysReg, RegState::Implicit);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    MachineFunction &MF = MIRBuilder.getMF();
    uint64_t LocMemOffset = VA.getLocMemOffset();
    auto *MMO = MF.getMachineMemOperand(
        MPO, MachineMemOperand::MOStore, MemTy,
        commonAlignment(Align(8), LocMemOffset));
    Register ExtReg = extendRegister(ValVReg, VA);
    MIRBuilder.buildStore(ExtReg, Addr, *MMO);
  }

private:
  MachineInstrBuilder &MIB;
  Register SPReg;
};

// Product C ABI family only. Non-C CCs must not silently reuse CC_Haydn.
static bool isSupportedCallingConv(CallingConv::ID CC) {
  switch (CC) {
  case CallingConv::C:
  case CallingConv::Fast:
  case CallingConv::Cold:
    return true;
  default:
    return false;
  }
}

// Nest/swift/byref/inalloca/preallocated require dedicated ABI seats Haydn
// does not own. Reject before any formal/call/return mutation so they cannot
// map as plain args or silently reuse CC_Haydn.
static bool hasUnsupportedABIArgFlags(const CallLowering::ArgInfo &Arg) {
  for (const ISD::ArgFlagsTy &F : Arg.Flags) {
    if (F.isNest() || F.isSwiftError() || F.isSwiftSelf() || F.isSwiftAsync() ||
        F.isInAlloca() || F.isPreallocated() || F.isByRef() || F.isInReg())
      return true;
  }
  return false;
}

static bool hasUnsupportedABIArgFlags(ArrayRef<CallLowering::ArgInfo> Args) {
  for (const CallLowering::ArgInfo &Arg : Args)
    if (hasUnsupportedABIArgFlags(Arg))
      return true;
  return false;
}

// IR-level preflight for formals: generic setArgFlags can assert on some
// unsupported param attrs (e.g. inalloca size path) before ArgFlags are
// available. Reject from Function attributes before that machinery runs.
static bool hasUnsupportedIRParamAttrs(const Argument &Arg) {
  return Arg.hasNestAttr() || Arg.hasByRefAttr() ||
         Arg.hasAttribute(Attribute::SwiftSelf) ||
         Arg.hasAttribute(Attribute::SwiftAsync) ||
         Arg.hasAttribute(Attribute::SwiftError) ||
         Arg.hasAttribute(Attribute::InAlloca) ||
         Arg.hasAttribute(Attribute::Preallocated) ||
         Arg.hasAttribute(Attribute::InReg);
}

// i128 (and wider integers) have no product CC. splitToValueTypes would
// invent 2×i64 in D0/D1. half / bfloat are storage or libcall types, not
// CC types (soft-float product CC is float=i32 / double=i64).
// Peer: AIE/RISCV fail-close unsupported widths at CC preflight rather
// than silently splitting into the GPR/DR assignment tables.
static bool isUnsupportedABIType(Type *Ty) {
  if (!Ty || Ty->isVoidTy())
    return false;
  if (IntegerType *IT = dyn_cast<IntegerType>(Ty))
    return IT->getBitWidth() > 64;
  // half / bfloat are storage or libcall types, not CC types. fp128 stays
  // fail-closed at the legalizer (libcall-legalizer-fp-surface.ll) so raw
  // IR can still reach G_FPEXT/G_FADD diagnostics instead of a CC abort.
  return Ty->isHalfTy() || Ty->isBFloatTy();
}

// Interrupt, naked, and stack-protector have no Haydn ABI. AIE rejects
// interrupt at return lowering (AIE1ISelLowering.cpp:964). Keep musttail
// and these rows fail-closed until a legal JALR/frame/ISR story exists.
static bool hasUnsupportedFnABI(const Function &F) {
  if (F.hasFnAttribute("interrupt") || F.hasFnAttribute(Attribute::Naked))
    return true;
  return F.hasFnAttribute(Attribute::StackProtect) ||
         F.hasFnAttribute(Attribute::StackProtectReq) ||
         F.hasFnAttribute(Attribute::StackProtectStrong);
}

} // end anonymous namespace


// Isolate a byval source into a private stack object with word-wise
// G_LOAD/G_STORE (never G_MEMCPY, which nests ADJCALLSTACK under the outer
// call frame). Returns the frame-index pointer vreg.
static Register isolateByValArgument(MachineIRBuilder &MIRBuilder,
                                     MachineRegisterInfo &MRI, Register SrcPtr,
                                     uint64_t MemSize, Align SrcAlign) {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  Align ObjAlign = std::max(SrcAlign, Align(4));
  int FI = MFI.CreateStackObject(MemSize, ObjAlign, /*isSS=*/false);
  LLT PtrTy = LLT::pointer(0, 32);
  LLT S32 = LLT::scalar(32);
  Register DstPtr = MIRBuilder.buildFrameIndex(PtrTy, FI).getReg(0);
  MachinePointerInfo DstMPO = MachinePointerInfo::getFixedStack(MF, FI);
  MachinePointerInfo SrcMPO(SrcPtr);

  uint64_t Offset = 0;
  while (Offset < MemSize) {
    unsigned Chunk = 1;
    if (Offset + 8 <= MemSize && (Offset % 8) == 0 && ObjAlign.value() >= 8 &&
        SrcAlign.value() >= 8)
      Chunk = 8;
    else if (Offset + 4 <= MemSize)
      Chunk = 4;
    else if (Offset + 2 <= MemSize)
      Chunk = 2;
    LLT Ty = LLT::scalar(Chunk * 8);
    Register SrcAddr = SrcPtr;
    Register DstAddr = DstPtr;
    if (Offset != 0) {
      auto Off = MIRBuilder.buildConstant(S32, Offset);
      SrcAddr = MIRBuilder.buildPtrAdd(PtrTy, SrcPtr, Off).getReg(0);
      DstAddr = MIRBuilder.buildPtrAdd(PtrTy, DstPtr, Off).getReg(0);
    }
    auto *SrcMMO = MF.getMachineMemOperand(
        SrcMPO.getWithOffset(Offset),
        MachineMemOperand::MOLoad | MachineMemOperand::MODereferenceable, Ty,
        commonAlignment(SrcAlign, Offset));
    auto *DstMMO = MF.getMachineMemOperand(
        DstMPO.getWithOffset(Offset),
        MachineMemOperand::MOStore | MachineMemOperand::MODereferenceable, Ty,
        commonAlignment(ObjAlign, Offset));
    Register Tmp = MRI.createGenericVirtualRegister(Ty);
    MIRBuilder.buildLoad(Tmp, SrcAddr, *SrcMMO);
    MIRBuilder.buildStore(Tmp, DstAddr, *DstMMO);
    Offset += Chunk;
  }
  return DstPtr;
}

// canLowerReturn / lowerReturn follow X86CallLowering (tablegen RetCC_*) and
// RISCVCallLowering (splitToValueTypes + determineAndHandleAssignments + sret
// demotion hooks from CallLowering base).
bool HaydnCallLowering::canLowerReturn(MachineFunction &MF,
                                       CallingConv::ID CallConv,
                                       SmallVectorImpl<BaseArgInfo> &Outs,
                                       bool IsVarArg) const {
  // Do not reject unsupported CCs here: returning false makes the generic
  // CallLowering wrapper attempt sret demotion (insertSRetOutgoingArgument)
  // for call returns, which is wrong for fail-closed CC ownership and can
  // assert on unsized void returns. Unsupported CCs are rejected in
  // lowerReturn / lowerFormalArguments / lowerCall before mutation.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs,
                 MF.getFunction().getContext());
  return checkReturn(CCInfo, Outs, RetCC_Haydn);
}

bool HaydnCallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                    const Value *Val,
                                    ArrayRef<Register> VRegs,
                                    FunctionLoweringInfo &FLI) const {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const Function &F = MF.getFunction();
  if (!isSupportedCallingConv(F.getCallingConv()))
    return false;
  if (hasUnsupportedFnABI(F))
    return false;
  if (isUnsupportedABIType(F.getReturnType()))
    return false;

  auto RetMI = MIRBuilder.buildInstrNoInsert(Haydn::RET);

  if (!FLI.CanLowerReturn) {
    insertSRetStores(MIRBuilder, Val->getType(), VRegs, FLI.DemoteRegister);
  } else if (!VRegs.empty()) {
    const DataLayout &DL = F.getDataLayout();
    CallingConv::ID CC = F.getCallingConv();

    ArgInfo OrigRetInfo(VRegs, Val->getType(), 0);
    setArgFlags(OrigRetInfo, AttributeList::ReturnIndex, DL, F);
    if (hasUnsupportedABIArgFlags(OrigRetInfo))
      return false;

    SmallVector<ArgInfo, 4> SplitRetInfos;
    splitToValueTypes(OrigRetInfo, SplitRetInfos, DL, CC);

    // Preflight return assignment before any return-value copy mutation so
    // unsupported RetCC seats match call-site fail-closed ownership.
    {
      CallLowering::OutgoingValueAssigner Preflight(RetCC_Haydn);
      SmallVector<CCValAssign, 8> RetLocs;
      CCState RetCCInfo(CC, F.isVarArg(), MF, RetLocs, F.getContext());
      if (!determineAssignments(Preflight, SplitRetInfos, RetCCInfo))
        return false;
    }

    CallLowering::OutgoingValueAssigner Assigner(RetCC_Haydn);
    HaydnOutgoingValueHandler Handler(MIRBuilder, MRI, RetMI);
    if (!determineAndHandleAssignments(Handler, Assigner, SplitRetInfos,
                                       MIRBuilder, CC, F.isVarArg()))
      return false;
  }

  // RET uses LR (R15) for the return address.
  RetMI.addReg(Haydn::R15, RegState::Implicit);
  MIRBuilder.insertInstr(RetMI);
  return true;
}

bool HaydnCallLowering::lowerFormalArguments(
    MachineIRBuilder &MIRBuilder, const Function &F,
    ArrayRef<ArrayRef<Register>> VRegs,
    FunctionLoweringInfo &FLI) const {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const DataLayout &DL = F.getDataLayout();

  // Fail closed on non-C CC, interrupt/naked/ssp, i128, and nest/swift/
  // byref/inalloca/inreg formals before mutation (IR attrs first so
  // setArgFlags never asserts on unsupported seats).
  if (!isSupportedCallingConv(F.getCallingConv()))
    return false;
  if (hasUnsupportedFnABI(F))
    return false;
  if (isUnsupportedABIType(F.getReturnType()))
    return false;
  for (const Argument &Arg : F.args()) {
    if (hasUnsupportedIRParamAttrs(Arg))
      return false;
    if (isUnsupportedABIType(Arg.getType()))
      return false;
  }

  SmallVector<ArgInfo, 8> SplitArgs;

  // Hidden sret pointer when the return value does not fit RetCC (R1–R2 / D0).
  if (!FLI.CanLowerReturn)
    insertSRetIncomingArgument(F, SplitArgs, FLI.DemoteRegister, MRI, DL);

  for (auto &Arg : F.args()) {
    unsigned Idx = Arg.getArgNo();
    ArgInfo OrigArg(VRegs[Idx], Arg, Idx);
    setArgFlags(OrigArg, Idx + 1, DL, F);
    if (hasUnsupportedABIArgFlags(OrigArg))
      return false;
    splitToValueTypes(OrigArg, SplitArgs, DL, F.getCallingConv());
  }

  // Preflight formal assignment before any copy-from-physreg mutation so
  // unsupported CC seats leave zero partial formal MIR (parity with call
  // assignment preflight before CALLSEQ).
  {
    CallLowering::IncomingValueAssigner Preflight(CC_Haydn);
    SmallVector<CCValAssign, 16> ArgLocs;
    CCState ArgCCInfo(F.getCallingConv(), F.isVarArg(), MF, ArgLocs,
                      F.getContext());
    if (!determineAssignments(Preflight, SplitArgs, ArgCCInfo))
      return false;
  }

  HaydnFormalArgHandler Handler(MIRBuilder, MRI);
  // Use the custom assigner so we can capture CCState for varargs.
  HaydnIncomingAssigner Assigner(CC_Haydn);

  if (!determineAndHandleAssignments(Handler, Assigner, SplitArgs,
                                     MIRBuilder, F.getCallingConv(),
                                     F.isVarArg()))
    return false;

  // F15: Set up the varargs register save area so va_arg can read unnamed args
  // that arrived in R1–R7. This records VarArgsFrameIndex + VarArgsStackOffset
  // in the machine function info; VASTART stores the frame address into the
  // va_list pointer. Previously the offset was hardcoded 0 and VASTART was a
  // no-op, so va_arg read garbage. See -varargs-reg-save-area.
  if (F.isVarArg()) {
    saveVarArgRegisters(MIRBuilder, Assigner);
  }

  return true;
}

// AIE AIECallLowering.cpp:592. Target-independent IsTailCall plus no
// byval formals. JALR/frame discipline still has to supply a tail opcode
// (AIE2 PseudoJ_TCO_*; AIE1 getCallOpcode tail is unreachable).
bool HaydnCallLowering::isEligibleForTailCallOptimization(
    MachineIRBuilder &MIRBuilder, CallLoweringInfo &Info) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &CallerF = MF.getFunction();

  if (!Info.IsTailCall && !Info.IsMustTailCall) {
    LLVM_DEBUG(dbgs() << "Call is not marked tail/musttail\n");
    return false;
  }
  if (any_of(CallerF.args(),
             [](const Argument &A) { return A.hasByValAttr(); })) {
    LLVM_DEBUG(dbgs() << "Cannot tail call from callers with byval\n");
    return false;
  }
  if (CallerF.isVarArg() || Info.IsVarArg) {
    LLVM_DEBUG(dbgs() << "Cannot tail call varargs\n");
    return false;
  }
  if (!isSupportedCallingConv(Info.CallConv) ||
      !isSupportedCallingConv(CallerF.getCallingConv()))
    return false;
  if (hasUnsupportedFnABI(CallerF) || hasUnsupportedABIArgFlags(Info.OrigArgs))
    return false;
  for (const ArgInfo &A : Info.OrigArgs)
    if (isUnsupportedABIType(A.Ty))
      return false;
  return true;
}

// AIE AIECallLowering.cpp:622 emits TII.getCallOpcode(..., /*isTailCall*/true),
// which is isReturn+isCall+isTerminator so PEI inserts the epilogue on that
// block (isReturnBlock = back().isReturn()). Haydn JAL_W is isCall only;
// JALR_W is isTerminator+isCall+isIndirectBranch but not isReturn. Emitting
// either as a tail would skip the PEI epilogue. AIE1 has the same hole
// (getCallOpcode tail is unreachable). Fail closed until a tail opcode
// exists: no CALLSEQ, no callee mutation, no ordinary-call fallthrough
// for musttail. Soft `tail` stays JAL_W + RET in lowerCall.
bool HaydnCallLowering::lowerTailCall(MachineIRBuilder &MIRBuilder,
                                      CallLoweringInfo &Info) const {
  Info.LoweredTailCall = false;
  if (!isEligibleForTailCallOptimization(MIRBuilder, Info))
    return false;
  // Eligible IR, but no product tail opcode (AIE2 PseudoJ_TCO analog).
  LLVM_DEBUG(dbgs() << "Tail eligible but no isReturn tail opcode\n");
  return false;
}

bool HaydnCallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                  CallLoweringInfo &Info) const {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const DataLayout &DL = MF.getDataLayout();

  // Soft `tail` is an ordinary JAL_W + RET. musttail has no fallthrough —
  // lowerTailCall is the AIE-shaped seat and stays fail-closed until a
  // legal tail opcode exists. Interrupt/naked/i128 stay fail-closed below.
  if (Info.IsMustTailCall)
    return lowerTailCall(MIRBuilder, Info);
  Info.IsTailCall = false;
  if (hasUnsupportedFnABI(MF.getFunction()))
    return false;
  if (Info.CB) {
    if (const Function *Callee = Info.CB->getCalledFunction())
      if (hasUnsupportedFnABI(*Callee))
        return false;
  }
  if (isUnsupportedABIType(Info.OrigRet.Ty))
    return false;
  for (const ArgInfo &OrigArg : Info.OrigArgs)
    if (isUnsupportedABIType(OrigArg.Ty))
      return false;

  // Only the default C ABI family is implemented. Unsupported CCs and
  // nest/swift/byref/inalloca/preallocated args or returns fail closed with
  // zero partial call-sequence MIR (no silent fallback onto CC_Haydn).
  if (!isSupportedCallingConv(Info.CallConv))
    return false;
  if (hasUnsupportedABIArgFlags(Info.OrigArgs))
    return false;
  if (hasUnsupportedABIArgFlags(Info.OrigRet))
    return false;

  // Isolate byval sources into private stack objects before CC assignment.
  // Pass the isolated pointer as a plain argument (no byval flag) so the
  // callee observes a copy rather than the caller's original object, and so
  // assignment never falls into the generic G_MEMCPY byval path.
  for (ArgInfo &OrigArg : Info.OrigArgs) {
    if (OrigArg.Flags.empty() || !OrigArg.Flags[0].isByVal())
      continue;
    assert(OrigArg.Regs.size() == 1 && "split byval pointer unexpected");
    uint64_t MemSize = OrigArg.Flags[0].getByValSize();
    Align SrcAlign = OrigArg.Flags[0].getNonZeroByValAlign();
    Register Isolated =
        isolateByValArgument(MIRBuilder, MRI, OrigArg.Regs[0], MemSize, SrcAlign);
    OrigArg = ArgInfo(Isolated, OrigArg.Ty, OrigArg.OrigArgIndex,
                      /*Flags=*/{}, OrigArg.OrigValue);
  }

  // Split args/returns and preflight CC assignment before any CALLSEQ or
  // call emission so unsupported types leave zero partial MIR.
  // Shared path may have prepended a hidden sret ArgInfo to OrigArgs.
  SmallVector<ArgInfo, 8> SplitArgs;
  for (auto &OrigArg : Info.OrigArgs)
    splitToValueTypes(OrigArg, SplitArgs, DL, Info.CallConv);

  SmallVector<ArgInfo, 4> SplitRetInfos;
  if (Info.CanLowerReturn && !Info.OrigRet.Ty->isVoidTy())
    splitToValueTypes(Info.OrigRet, SplitRetInfos, DL, Info.CallConv);

  CallLowering::OutgoingValueAssigner Assigner(CC_Haydn);
  {
    SmallVector<CCValAssign, 16> ArgLocs;
    CCState ArgCCInfo(Info.CallConv, Info.IsVarArg, MF, ArgLocs,
                      MF.getFunction().getContext());
    if (!determineAssignments(Assigner, SplitArgs, ArgCCInfo))
      return false;
  }
  if (!SplitRetInfos.empty()) {
    CallLowering::IncomingValueAssigner RetAssigner(RetCC_Haydn);
    SmallVector<CCValAssign, 8> RetLocs;
    CCState RetCCInfo(Info.CallConv, Info.IsVarArg, MF, RetLocs,
                      MF.getFunction().getContext());
    if (!determineAssignments(RetAssigner, SplitRetInfos, RetCCInfo))
      return false;
  }

  // Emit ADJCALLSTACKDOWN only after assignment preflight succeeds. Size is
  // known from Assigner.StackSize; round up to StackAlign so SP never becomes
  // ≡4 mod 8 across a call (B1 / DR st64).
  const Align StackAlign =
      MF.getSubtarget<HaydnSubtarget>().getFrameLowering()->getStackAlign();
  const uint64_t CallFrameBytes =
      alignTo(static_cast<uint64_t>(Assigner.StackSize), StackAlign);
  MIRBuilder.buildInstr(Haydn::ADJCALLSTACKDOWN)
      .addImm(static_cast<int64_t>(CallFrameBytes))
      .addImm(0);

  // Direct: real JAL_W with callee MO (GlobalAddress / ExternalSymbol) and
  // call-preserved regmask. Peer: AArch64CallLowering.cpp:1079,1192 (BL +
  // addRegMask). Do not emit PseudoCALL — ExpandPseudos does not expand it.
  // Indirect: PseudoCALLIndirect (printer → JALR_W); constrain callee vreg
  // to GPR32 so the $rs operand verifies.
  MachineInstrBuilder MIB;
  if (Info.Callee.isReg()) {
    MIB = MIRBuilder.buildInstrNoInsert(Haydn::PseudoCALLIndirect);
    MIB.addReg(Haydn::R15, RegState::Define); // link register ($rd)
    Register CalleeReg = Info.Callee.getReg();
    if (CalleeReg.isVirtual())
      if (auto *RBI = MF.getSubtarget().getRegBankInfo())
        RBI->constrainGenericRegister(CalleeReg, Haydn::GPR32RegClass, MRI);
    MIB.addReg(CalleeReg); // function pointer ($rs)
  } else {
    MIB = MIRBuilder.buildInstrNoInsert(Haydn::JAL_W);
    MIB.addReg(Haydn::R15, RegState::Define); // link register
    MIB.add(Info.Callee);
  }

  // Call-preserved regmask: CSR bank R8–R11, R14, R15, D8–D15.
  {
    const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
    const uint32_t *Mask = TRI->getCallPreservedMask(MF, Info.CallConv);
    assert(Mask && "Missing call preserved mask for calling convention");
    MIB.addRegMask(Mask);
  }

  {
    HaydnOutgoingValueHandler Handler(MIRBuilder, MRI, MIB);
    CallLowering::OutgoingValueAssigner EmitAssigner(CC_Haydn);
    if (!determineAndHandleAssignments(Handler, EmitAssigner, SplitArgs,
                                       MIRBuilder, Info.CallConv,
                                       Info.IsVarArg))
      return false;
  }

  MIRBuilder.insertInstr(MIB);
  MIRBuilder.buildInstr(Haydn::ADJCALLSTACKUP)
      .addImm(static_cast<int64_t>(CallFrameBytes))
      .addImm(0);

  // Copy returned values into result vregs (RetCC + CallReturnHandler).
  if (!SplitRetInfos.empty()) {
    CallLowering::IncomingValueAssigner RetAssigner(RetCC_Haydn);
    HaydnCallReturnHandler RetHandler(MIRBuilder, MRI, MIB);
    if (!determineAndHandleAssignments(RetHandler, RetAssigner, SplitRetInfos,
                                       MIRBuilder, Info.CallConv,
                                       Info.IsVarArg))
      return false;
  }

  if (!Info.CanLowerReturn)
    insertSRetLoads(MIRBuilder, Info.OrigRet.Ty, Info.OrigRet.Regs,
                    Info.DemoteRegister, Info.DemoteStackIndex);

  return true;
}
