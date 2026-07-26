//===-- HaydnISelLowering.cpp - Haydn DAG Lowering Implementation ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that Haydn uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "HaydnISelLowering.h"
#include "HaydnSubtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsHaydn.h"
#include "llvm/IR/RuntimeLibcalls.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-isel-lowering"

HaydnTargetLowering::HaydnTargetLowering(const TargetMachine &TM,
                                        const HaydnSubtarget &STI)
    : TargetLowering(TM, STI), Subtarget(STI) {
  // Set up the register classes. i32→GPR32; i64 and 64-bit SIMD→DR64.
  // G-ABI-VEC: v2i32/v4i16/v8i8/v2f32 must be registered so CallLowering/CC.td
  // assign whole DRs (not multi-scalar split into GPRs).
  // Residual 32-bit SLP shapes (v4i8/v2i16) live in one GPR32; legalizer
  // scalarizes arithmetic and packs via BUILD_VECTOR custom.
  addRegisterClass(MVT::i32, &Haydn::GPR32RegClass);
  addRegisterClass(MVT::i64, &Haydn::DR64RegClass);
  addRegisterClass(MVT::v2i32, &Haydn::DR64RegClass);
  addRegisterClass(MVT::v4i16, &Haydn::DR64RegClass);
  addRegisterClass(MVT::v8i8, &Haydn::DR64RegClass);
  addRegisterClass(MVT::v2f32, &Haydn::DR64RegClass);
  addRegisterClass(MVT::v4i8, &Haydn::GPR32RegClass);
  addRegisterClass(MVT::v2i16, &Haydn::GPR32RegClass);

  // Compute register properties
  computeRegisterProperties(STI.getRegisterInfo());

  // Soft-float libm family: baremetal leaves floorf/fminf unset; GISel
  // createLibcall reads TLI.getLibcallName. Mirror of
  // HaydnSubtarget::initLibcallLoweringInfo. copysign is .lower (bit trick),
  // not a libcall.
  setLibcallImpl(RTLIB::FLOOR_F32, RTLIB::impl_floorf);
  setLibcallImpl(RTLIB::FLOOR_F64, RTLIB::impl_floor);
  setLibcallImpl(RTLIB::CEIL_F32, RTLIB::impl_ceilf);
  setLibcallImpl(RTLIB::CEIL_F64, RTLIB::impl_ceil);
  setLibcallImpl(RTLIB::RINT_F32, RTLIB::impl_rintf);
  setLibcallImpl(RTLIB::RINT_F64, RTLIB::impl_rint);
  setLibcallImpl(RTLIB::NEARBYINT_F32, RTLIB::impl_nearbyintf);
  setLibcallImpl(RTLIB::NEARBYINT_F64, RTLIB::impl_nearbyint);
  setLibcallImpl(RTLIB::FMIN_F32, RTLIB::impl_fminf);
  setLibcallImpl(RTLIB::FMIN_F64, RTLIB::impl_fmin);
  setLibcallImpl(RTLIB::FMAX_F32, RTLIB::impl_fmaxf);
  setLibcallImpl(RTLIB::FMAX_F64, RTLIB::impl_fmax);

  // Haydn has no native atomic instructions. CLAUDE.md mandates
  // "atomics as libcalls": atomic ops lower to __atomic_* runtime calls
  // (compiler-rt/libgcc). A max atomic size of 0 tells AtomicExpandPass that
  // NO size is supported, so every atomic load/store/RMW/cmpxchg is rewritten
  // to a __atomic_* libcall at IR level (before GISel), via
  // expandAtomic{Load,Store,RMW}ToLibcall. With MaxAtomicSizeInBitsSupported
  // > 0, atomicSizeSupported returns true and the pass skips the libcall
  // path entirely, leaving atomics for GISel to fail on ("unable to legalize
  // G_ATOMICRMW_*"). The shouldExpandAtomic*InIR overrides below then never
  // run. See atomics-libcall-expansion,.
  setMaxAtomicSizeInBitsSupported(0);

  // Enable jump tables for dense switches. The GISel pipeline handles
  // G_JUMP_TABLE (materialize base) and G_BRJT (indirect branch) via
  // the instruction selector. BR_JT is expanded to JALR by AsmPrinter.
  setMinimumJumpTableEntries(4);

  // Bundle128 product: every instruction/parcel is 16 bytes (MCAsmInfo
  // MinInstAlignment). Functions must be 16-byte aligned so call/JALR
  // targets and LR return PCs are exact Bundle128 records — same contract
  // as AIE (AIEBaseISelLowering Min/Pref FunctionAlignment Align(16)).
  // Leaving the TargetLowering default Align(1) risks BAD_PC when a callee
  // entry is not a multiple of 16 after link (next PC not an exact record).
  setMinFunctionAlignment(Align(16));
  setPrefFunctionAlignment(Align(16));
}

EVT HaydnTargetLowering::getSetCCResultType(const DataLayout &DL,
                                            LLVMContext &Context,
                                            EVT VT) const {
  return MVT::i32;
}

MVT HaydnTargetLowering::getScalarShiftAmountTy(const DataLayout &DL,
                                                EVT VT) const {
  return MVT::i32;
}

unsigned HaydnTargetLowering::getMinimumJumpTableEntries() const {
  // Use jump tables for switches with 4+ dense cases.
  return 4;
}

bool HaydnTargetLowering::areJTsAllowed(const Function *Fn) const {
  // Jump tables are enabled. The GISel selector handles G_BRJT → BR_JT
  // and AsmPrinter expands BR_JT to JALR for the indirect branch.
  return true;
}

TargetLowering::AtomicExpansionKind
HaydnTargetLowering::shouldExpandAtomicLoadInIR(LoadInst *LI) const {
  // With setMaxAtomicSizeInBitsSupported(0), atomicSizeSupported returns
  // false for every size, so AtomicExpandPass routes loads through
  // expandAtomicLoadToLibcall BEFORE consulting this hook. The hook therefore
  // never runs in practice — it's kept only as a defensive belt-and-suspenders
  // for any future code path that reaches it. None = "no preference".
  return AtomicExpansionKind::None;
}

TargetLowering::AtomicExpansionKind
HaydnTargetLowering::shouldExpandAtomicStoreInIR(StoreInst *SI) const {
  // See shouldExpandAtomicLoadInIR — never reached; kept defensively.
  return AtomicExpansionKind::None;
}

TargetLowering::AtomicExpansionKind
HaydnTargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *RMW) const {
  // See shouldExpandAtomicLoadInIR — never reached; kept defensively.
  return AtomicExpansionKind::None;
}

std::pair<unsigned, const TargetRegisterClass *>
HaydnTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                  StringRef Constraint,
                                                  MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      // General-purpose register. Haydn GPRs are 32-bit; pointers are i32
      // (datalayout p:32:32). i64 values live in DR64.
      if (VT == MVT::i64 || VT == MVT::f64)
        return std::make_pair(0U, &Haydn::DR64RegClass);
      // i1/i8/i16/i32/f32 and any other ≤32-bit integer (incl. pointer VT).
      if (VT == MVT::i32 || VT == MVT::i16 || VT == MVT::i8 || VT == MVT::i1 ||
          VT == MVT::f32 || (VT.isInteger() && VT.getSizeInBits() <= 32))
        return std::make_pair(0U, &Haydn::GPR32RegClass);
      break;
    default:
      break;
    }
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

/// Fill IntrinsicInfo for a Haydn public memory intrinsic.
/// PtrArgIdx is the IR argument index of the address (0 for frexp/WITH loads,
/// 1 for frexp/WITH/POST/PRE stores with a leading data operand).
static bool setHaydnMemIntrinsic(TargetLowering::IntrinsicInfo &Info,
                                 const CallBase &I, EVT MemVT,
                                 unsigned PtrArgIdx,
                                 MachineMemOperand::Flags Flags) {
  Info.opc = ISD::INTRINSIC_W_CHAIN;
  Info.memVT = MemVT;
  Info.ptrVal = I.getArgOperand(PtrArgIdx);
  Info.offset = 0;
  // Natural alignment of the access width (not necessarily ABI of the
  // IR return type — dual-lane DR loads still access MemVT bytes).
  Info.align = Align(MemVT.getStoreSize().getFixedValue());
  Info.flags = Flags;
  return true;
}

bool HaydnTargetLowering::getTgtMemIntrinsic(IntrinsicInfo &Info,
                                             const CallBase &I,
                                             MachineFunction &MF,
                                             unsigned IntrID) const {
  // C2.3 / G-MEM-INTRIN MMO policy (peer Hexagon L2_load*_pbr vs V6_vgatherm*):
  //   Ordinary — Golden LS WITH/POST/PRE + BREV: MOLoad / MOStore only.
  //     Disjoint ordinary argmem may co-issue (AA-safe, non-volatile MMOs).
  //   Stateful — CB + UA StateMem: MO* | MOVolatile so MIR prints
  //     `load/store volatile` and the scheduler treats them as ordered.
  const MachineMemOperand::Flags LoadF = MachineMemOperand::MOLoad;
  const MachineMemOperand::Flags StoreF = MachineMemOperand::MOStore;
  const MachineMemOperand::Flags StatefulLoadF =
      MachineMemOperand::MOLoad | MachineMemOperand::MOVolatile;
  const MachineMemOperand::Flags StatefulStoreF =
      MachineMemOperand::MOStore | MachineMemOperand::MOVolatile;

  switch (IntrID) {
  //===--------------------------------------------------------------------===//
  // Circular buffer (StateMem) — 64-bit. Load: (base,cbr,stride)->{i64,ptr}
  // Store: (i64, base, cbr, stride) -> ptr. Volatile MMO: CBR sticky state.
  //===--------------------------------------------------------------------===//
  case Intrinsic::haydn_ldw_cb_imm:
  case Intrinsic::haydn_ldw_cb_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i64, /*PtrArgIdx=*/0,
                                StatefulLoadF);
  case Intrinsic::haydn_sdw_cb_imm:
  case Intrinsic::haydn_sdw_cb_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i64, /*PtrArgIdx=*/1,
                                StatefulStoreF);

  //===--------------------------------------------------------------------===//
  // Bit-reversed addressing (Hexagon L2_load*_pbr peer) — ordinary MMO.
  //===--------------------------------------------------------------------===//
  case Intrinsic::haydn_ldw_brev_imm:
  case Intrinsic::haydn_ldw_brev_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i64, 0, LoadF);
  case Intrinsic::haydn_lw_brev_imm:
  case Intrinsic::haydn_lw_brev_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i32, 0, LoadF);
  case Intrinsic::haydn_sdw_brev_imm:
  case Intrinsic::haydn_sdw_brev_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i64, 1, StoreF);
  case Intrinsic::haydn_sw_brev_imm:
  case Intrinsic::haydn_sw_brev_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i32, 1, StoreF);

  //===--------------------------------------------------------------------===//
  // Golden LS — 64-bit DR doubleword (LDW/SDW)
  // frexp load: (base, off) -> {i64, ptr}; WITH load: (base, off) -> i64
  // frexp store: (i64, base, off) -> ptr; WITH store: (i64, base, off) -> void
  //===--------------------------------------------------------------------===//
  case Intrinsic::haydn_d_ldw_post_imm:
  case Intrinsic::haydn_d_ldw_post_reg:
  case Intrinsic::haydn_d_ldw_pre_imm:
  case Intrinsic::haydn_d_ldw_pre_reg:
  case Intrinsic::haydn_d_ldw_with_imm:
  case Intrinsic::haydn_d_ldw_with_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i64, 0, LoadF);
  case Intrinsic::haydn_d_sdw_post_imm:
  case Intrinsic::haydn_d_sdw_post_reg:
  case Intrinsic::haydn_d_sdw_pre_imm:
  case Intrinsic::haydn_d_sdw_pre_reg:
  case Intrinsic::haydn_d_sdw_with_imm:
  case Intrinsic::haydn_d_sdw_with_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i64, 1, StoreF);

  // Dual-word / word into DR: 32-bit element access (offset scale 4).
  case Intrinsic::haydn_d_lw_post_imm:
  case Intrinsic::haydn_d_lw_post_reg:
  case Intrinsic::haydn_d_lw_pre_imm:
  case Intrinsic::haydn_d_lw_pre_reg:
  case Intrinsic::haydn_d_lw_with_imm:
  case Intrinsic::haydn_d_lw_with_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i32, 0, LoadF);
  case Intrinsic::haydn_d_sw_h_post_imm:
  case Intrinsic::haydn_d_sw_h_post_reg:
  case Intrinsic::haydn_d_sw_h_pre_imm:
  case Intrinsic::haydn_d_sw_h_pre_reg:
  case Intrinsic::haydn_d_sw_h_with_imm:
  case Intrinsic::haydn_d_sw_h_with_reg:
  case Intrinsic::haydn_d_sw_l_post_imm:
  case Intrinsic::haydn_d_sw_l_post_reg:
  case Intrinsic::haydn_d_sw_l_pre_imm:
  case Intrinsic::haydn_d_sw_l_pre_reg:
  case Intrinsic::haydn_d_sw_l_with_imm:
  case Intrinsic::haydn_d_sw_l_with_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i32, 1, StoreF);

  // Dual-halfword into DR / halfword stores: 16-bit element (scale 2).
  case Intrinsic::haydn_d_lhw_post_imm:
  case Intrinsic::haydn_d_lhw_post_reg:
  case Intrinsic::haydn_d_lhw_pre_imm:
  case Intrinsic::haydn_d_lhw_pre_reg:
  case Intrinsic::haydn_d_lhw_with_imm:
  case Intrinsic::haydn_d_lhw_with_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i16, 0, LoadF);
  case Intrinsic::haydn_d_shw_post_imm:
  case Intrinsic::haydn_d_shw_post_reg:
  case Intrinsic::haydn_d_shw_pre_imm:
  case Intrinsic::haydn_d_shw_pre_reg:
  case Intrinsic::haydn_d_shw_with_imm:
  case Intrinsic::haydn_d_shw_with_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i16, 1, StoreF);

  // Scalar GPR word / half / byte
  case Intrinsic::haydn_s_lw_post_imm:
  case Intrinsic::haydn_s_lw_post_reg:
  case Intrinsic::haydn_s_lw_pre_imm:
  case Intrinsic::haydn_s_lw_pre_reg:
  case Intrinsic::haydn_s_lw_with_imm:
  case Intrinsic::haydn_s_lw_with_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i32, 0, LoadF);
  case Intrinsic::haydn_s_sw_post_imm:
  case Intrinsic::haydn_s_sw_post_reg:
  case Intrinsic::haydn_s_sw_pre_imm:
  case Intrinsic::haydn_s_sw_pre_reg:
  case Intrinsic::haydn_s_sw_with_imm:
  case Intrinsic::haydn_s_sw_with_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i32, 1, StoreF);

  case Intrinsic::haydn_s_lhws_post_imm:
  case Intrinsic::haydn_s_lhws_post_reg:
  case Intrinsic::haydn_s_lhws_pre_imm:
  case Intrinsic::haydn_s_lhws_pre_reg:
  case Intrinsic::haydn_s_lhws_with_imm:
  case Intrinsic::haydn_s_lhws_with_reg:
  case Intrinsic::haydn_s_lhwu_post_imm:
  case Intrinsic::haydn_s_lhwu_post_reg:
  case Intrinsic::haydn_s_lhwu_pre_imm:
  case Intrinsic::haydn_s_lhwu_pre_reg:
  case Intrinsic::haydn_s_lhwu_with_imm:
  case Intrinsic::haydn_s_lhwu_with_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i16, 0, LoadF);
  case Intrinsic::haydn_s_shw_post_imm:
  case Intrinsic::haydn_s_shw_post_reg:
  case Intrinsic::haydn_s_shw_pre_imm:
  case Intrinsic::haydn_s_shw_pre_reg:
  case Intrinsic::haydn_s_shw_with_imm:
  case Intrinsic::haydn_s_shw_with_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i16, 1, StoreF);

  case Intrinsic::haydn_s_lbs_post_imm:
  case Intrinsic::haydn_s_lbs_post_reg:
  case Intrinsic::haydn_s_lbs_pre_imm:
  case Intrinsic::haydn_s_lbs_pre_reg:
  case Intrinsic::haydn_s_lbs_with_imm:
  case Intrinsic::haydn_s_lbs_with_reg:
  case Intrinsic::haydn_s_lbu_post_imm:
  case Intrinsic::haydn_s_lbu_post_reg:
  case Intrinsic::haydn_s_lbu_pre_imm:
  case Intrinsic::haydn_s_lbu_pre_reg:
  case Intrinsic::haydn_s_lbu_with_imm:
  case Intrinsic::haydn_s_lbu_with_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i8, 0, LoadF);
  case Intrinsic::haydn_s_sb_post_imm:
  case Intrinsic::haydn_s_sb_post_reg:
  case Intrinsic::haydn_s_sb_pre_imm:
  case Intrinsic::haydn_s_sb_pre_reg:
  case Intrinsic::haydn_s_sb_with_imm:
  case Intrinsic::haydn_s_sb_with_reg:
    return setHaydnMemIntrinsic(Info, I, MVT::i8, 1, StoreF);

  //===--------------------------------------------------------------------===//
  // AR unaligned stream (StateMem) — mem through ptr + AR sticky state.
  // pldwwua(ar_sel, ptr): ptr at arg1; UA post loads: ptr at arg0
  // UA stores: data, ptr, ... → ptr at arg1; wbarwua(ar, ptr, dir) → arg1
  // Volatile MMO: AR state is not IR-visible; order vs other mem.
  //===--------------------------------------------------------------------===//
  case Intrinsic::haydn_pldwwua:
    return setHaydnMemIntrinsic(Info, I, MVT::i64, 1, StatefulLoadF);
  case Intrinsic::haydn_d_lqhwua_post:
  case Intrinsic::haydn_d_ltwua_post:
    return setHaydnMemIntrinsic(Info, I, MVT::i64, 0, StatefulLoadF);
  case Intrinsic::haydn_wbarwua:
    return setHaydnMemIntrinsic(Info, I, MVT::i64, 1, StatefulStoreF);
  case Intrinsic::haydn_d_sqhwua_post:
  case Intrinsic::haydn_d_stwua_post:
    return setHaydnMemIntrinsic(Info, I, MVT::i64, 1, StatefulStoreF);

  default:
    return false;
  }
}
