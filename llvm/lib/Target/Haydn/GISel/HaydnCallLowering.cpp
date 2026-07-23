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
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Alignment.h"
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
  unsigned FirstUnallocatedGPR = std::size(HaydnArgGPRs);
  // Index of the first unallocated DR arg reg. Default = all consumed.
  unsigned FirstUnallocatedDR = std::size(HaydnArgDRs);

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

    auto FIN = MIRBuilder.buildFrameIndex(PtrTy, GprFI);
    for (unsigned I = FirstUnallocGPR; I < std::size(HaydnArgGPRs); ++I) {
      MCPhysReg PhysReg = HaydnArgGPRs[I];
      Register VReg = MRI.createGenericVirtualRegister(S32);
      MCRegister PhysMCReg = PhysReg;
      MRI.addLiveIn(PhysMCReg);
      MIRBuilder.getMBB().addLiveIn(PhysMCReg);
      MIRBuilder.buildCopy(VReg, Register(PhysMCReg));

      unsigned SlotOff = (I - FirstUnallocGPR) * HaydnGPRSaveSize;
      auto MPO = MachinePointerInfo::getFixedStack(MF, GprFI, SlotOff);
      MIRBuilder.buildStore(VReg, FIN, MPO, inferAlignFromPtrInfo(MF, MPO));
      if (I + 1 < std::size(HaydnArgGPRs)) {
        auto Step = MIRBuilder.buildConstant(S32, HaydnGPRSaveSize);
        FIN = MIRBuilder.buildPtrAdd(PtrTy, FIN.getReg(0), Step);
      }
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

    auto FIN = MIRBuilder.buildFrameIndex(PtrTy, DrFI);
    for (unsigned I = FirstUnallocDR; I < std::size(HaydnArgDRs); ++I) {
      MCPhysReg PhysReg = HaydnArgDRs[I];
      Register VReg = MRI.createGenericVirtualRegister(S64);
      MCRegister PhysMCReg = PhysReg;
      MRI.addLiveIn(PhysMCReg);
      MIRBuilder.getMBB().addLiveIn(PhysMCReg);
      MIRBuilder.buildCopy(VReg, Register(PhysMCReg));

      unsigned SlotOff = (I - FirstUnallocDR) * HaydnDRSaveSize;
      auto MPO = MachinePointerInfo::getFixedStack(MF, DrFI, SlotOff);
      MIRBuilder.buildStore(VReg, FIN, MPO, inferAlignFromPtrInfo(MF, MPO));
      if (I + 1 < std::size(HaydnArgDRs)) {
        auto Step = MIRBuilder.buildConstant(S32, HaydnDRSaveSize);
        FIN = MIRBuilder.buildPtrAdd(PtrTy, FIN.getReg(0), Step);
      }
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

} // end anonymous namespace

// canLowerReturn / lowerReturn follow X86CallLowering (tablegen RetCC_*) and
// RISCVCallLowering (splitToValueTypes + determineAndHandleAssignments + sret
// demotion hooks from CallLowering base).
bool HaydnCallLowering::canLowerReturn(MachineFunction &MF,
                                       CallingConv::ID CallConv,
                                       SmallVectorImpl<BaseArgInfo> &Outs,
                                       bool IsVarArg) const {
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

  auto RetMI = MIRBuilder.buildInstrNoInsert(Haydn::RET);

  if (!FLI.CanLowerReturn) {
    insertSRetStores(MIRBuilder, Val->getType(), VRegs, FLI.DemoteRegister);
  } else if (!VRegs.empty()) {
    const Function &F = MF.getFunction();
    const DataLayout &DL = F.getDataLayout();
    CallingConv::ID CC = F.getCallingConv();

    ArgInfo OrigRetInfo(VRegs, Val->getType(), 0);
    setArgFlags(OrigRetInfo, AttributeList::ReturnIndex, DL, F);

    SmallVector<ArgInfo, 4> SplitRetInfos;
    splitToValueTypes(OrigRetInfo, SplitRetInfos, DL, CC);

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

  SmallVector<ArgInfo, 8> SplitArgs;

  // Hidden sret pointer when the return value does not fit RetCC (R1–R2 / D0).
  if (!FLI.CanLowerReturn)
    insertSRetIncomingArgument(F, SplitArgs, FLI.DemoteRegister, MRI, DL);

  for (auto &Arg : F.args()) {
    unsigned Idx = Arg.getArgNo();
    ArgInfo OrigArg(VRegs[Idx], Arg, Idx);
    setArgFlags(OrigArg, Idx + 1, DL, F);
    splitToValueTypes(OrigArg, SplitArgs, DL, F.getCallingConv());
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

bool HaydnCallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                  CallLoweringInfo &Info) const {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const DataLayout &DL = MF.getDataLayout();

  // F17: Emit ADJCALLSTACKDOWN before the call sequence and ADJCALLSTACKUP
  // after it, sized by the outgoing stack-argument bytes. Without this pair
  // outgoing stack arguments are written at the current SP and clobber the
  // caller's frame (callee would interpret them as incoming args at the wrong
  // offset, and a signal/interrupt during the call would corrupt live data).
  // `Assigner.StackSize` is populated by determineAndHandleAssignments below;
  // we patch the immediate into the already-inserted ADJCALLSTACKDOWN afterward
  // (RISC-V pattern). The instr is inserted before arg stores so the scavenger
  // FI eliminator see the correct SP.
  MachineInstrBuilder CallSeqStart =
      MIRBuilder.buildInstr(Haydn::ADJCALLSTACKDOWN);

  // Build the call instruction. Direct calls (symbol/imm callee
  // MO_GlobalAddress, MO_ExternalSymbol, MO_Immediate) use JAL
  // (jump-and-link): (outs GPR32:$rd), (ins calltarget:$target).
  // Indirect calls (function pointer — MO_Register) must use JALR
  // (jump-and-link-register): (outs GPR32:$rd), (ins GPR32:$rs
  // calltarget:$target), because JAL's `calltarget` operand cannot hold a
  // register. Routing a reg callee through JAL produced `jal lr,` with a
  // blank target : the ISS decoded the missing target as r0=0 and the
  // program self-looped on every function-pointer call. $rd receives the
  // return address (link register R15 = LR).
  MachineInstrBuilder MIB;
  if (Info.Callee.isReg()) {
    // Indirect call through a function pointer. Emit PseudoCALLIndirect
    // (expanded to JALR R15, rs, 0 by ExpandPseudos). The callee vreg is a
    // generic pointer (p0); constrain it to GPR32 so the pseudo's $rs operand
    // satisfies the verifier. PseudoCALLIndirect is isCall=1 but NOT a
    // terminator (control returns), unlike raw JALR (terminator, for RET).
    MIB = MIRBuilder.buildInstrNoInsert(Haydn::PseudoCALLIndirect);
    MIB.addReg(Haydn::R15, RegState::Define); // link register ($rd)
    // The callee vreg is a generic pointer (p0) at this (legalizer) stage.
    // Bank+constrain it to GPR32 via RBI.constrainGenericRegister (the MRI
    // constrainRegClass asserts on an unbanked generic vreg). regalloc then
    // assigns a phys GPR that expandPseudoCALLIndirect reads.
    Register CalleeReg = Info.Callee.getReg();
    if (CalleeReg.isVirtual())
      if (auto *RBI = MF.getSubtarget().getRegBankInfo())
        RBI->constrainGenericRegister(CalleeReg, Haydn::GPR32RegClass, MRI);
    MIB.addReg(CalleeReg); // function pointer ($rs)
  } else {
    // Direct call: preserve the callee operand verbatim. The generic libcall
    // path (LegalizerHelper::createLibcall) constructs the callee as an
    // MO_ExternalSymbol referencing the runtime function name (e.g.
    // "__addsf3"); normal call lowering constructs MO_GlobalAddress. Adding
    // Info.Callee directly — rather than reconstructing it via
    // addGlobalAddress/addExternalSymbol — guarantees the callee operand
    // (and its MCSymbol, after MCInst lowering) survives to the emitter.
    // Mirrors RISC-V's lowerCall idiom (RISCVCallLowering.cpp:
    // `.add(Info.Callee)`). See.
    // Phase 1a: route direct call to the 48-bit WIDE form (JAL_W
    // encoding_manual.md §5.5 Class 001). The legacy Haydn32 FmtJ parcel is
    // being purged from CodeGen selection; JAL/JALR defs remain in the.td for
    // the asm parser / decoder until Phase 3. The brtarget_wide_i20 operand
    // class emits FIXUP_HAYDN_WIDE_CallSImm20 via getSImmOpValueXStepWide — no
    // MCCodeEmitter call/branch fixup-kind routing needed for the _W opcodes.
    MIB = MIRBuilder.buildInstrNoInsert(Haydn::JAL_W);
    MIB.addReg(Haydn::R15, RegState::Define); // link register
    MIB.add(Info.Callee);
  }

  // Handle arguments. For IR calls that need sret demotion, CallLowering's
  // shared path already prepended the hidden sret ArgInfo to OrigArgs.
  SmallVector<ArgInfo, 8> SplitArgs;
  for (auto &OrigArg : Info.OrigArgs)
    splitToValueTypes(OrigArg, SplitArgs, DL, Info.CallConv);

  HaydnOutgoingValueHandler Handler(MIRBuilder, MRI, MIB);
  CallLowering::OutgoingValueAssigner Assigner(CC_Haydn);

  if (!determineAndHandleAssignments(Handler, Assigner, SplitArgs,
                                     MIRBuilder, Info.CallConv,
                                     Info.IsVarArg))
    return false;

  // Patch the now-known outgoing stack size into the already-inserted
  // ADJCALLSTACKDOWN, then insert the call and ADJCALLSTACKUP. The pseudos set
  // MaxCallFrameSize (read by determineFrameLayout) and are expanded by
  // eliminateCallFramePseudoInstr. Round StackSize up to the target stack
  // alignment so SP never becomes ≡4 mod 8 across a call (B1: single i32
  // stack arg with Size=4 left StackSize=4; callee DR st64 then faults).
  // Bundle128 is 16-byte text only — SP ABI stays StackAlign(8) for DR.
  // Round StackSize up to StackAlign so SP never becomes ≡4 mod 8 across a
  // call (B1). Operand 1 of ADJCALLSTACK* is the FrameSetup/Destroy twin
  // amount for the verifier (must match op0, not "align") — keep 0 as
  // Haydn/RISC-V style second imm when unused, or pass the same amount.
  // Bundle128 is 16-byte text only; SP ABI stays StackAlign(8) for DR st64.
  const Align StackAlign =
      MF.getSubtarget<HaydnSubtarget>().getFrameLowering()->getStackAlign();
  const uint64_t CallFrameBytes =
      alignTo(static_cast<uint64_t>(Assigner.StackSize), StackAlign);
  CallSeqStart.addImm(static_cast<int64_t>(CallFrameBytes)).addImm(0);

  // Call-preserved regmask via getCallPreservedMask: CSR bank R8–R11, R14,
  // R15, D8–D15 (R14 always CSR; hasFP reserves it as frame base). R12 is
  // normal call-clobbered.
  {
    const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
    const uint32_t *Mask = TRI->getCallPreservedMask(MF, Info.CallConv);
    assert(Mask && "Missing call preserved mask for calling convention");
    MIB.addRegMask(Mask);
  }

  MIRBuilder.insertInstr(MIB);
  MIRBuilder.buildInstr(Haydn::ADJCALLSTACKUP)
      .addImm(static_cast<int64_t>(CallFrameBytes))
      .addImm(0);

  // Copy returned values into result vregs. Mirrors RISCVCallLowering::lowerCall
  // X86CallLowering::lowerCall: splitToValueTypes + RetCC + CallReturnHandler
  // (IncomingValueHandler that marks call implicit-defs). Not a hand-walked
  // R1/R2/D0 index.
  if (Info.CanLowerReturn && !Info.OrigRet.Ty->isVoidTy()) {
    SmallVector<ArgInfo, 4> SplitRetInfos;
    splitToValueTypes(Info.OrigRet, SplitRetInfos, DL, Info.CallConv);

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
