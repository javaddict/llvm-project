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
#include "Haydn.h"
#include "HaydnCallingConv.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "llvm/ADT/bit.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsHaydn.h"
#include "llvm/IR/Type.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-isel-lowering"

HaydnTargetLowering::HaydnTargetLowering(const TargetMachine &TM,
                                        const HaydnSubtarget &STI)
    : TargetLowering(TM, STI), Subtarget(STI) {
  // Set up the register classes. i32→GPR32; i64 and 64-bit SIMD→DR64.
 // : v2i32/v4i16/v8i8/v2f32 must be registered so CallLowering/CC.td
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

  // Soft-float / libm names live in HaydnSubtarget::initLibcallLoweringInfo
  // (the one table). TargetLoweringBase already applied that hook to
  // TLI.Libcalls during this constructor's base init, so GISel
  // createLibcall / TLI.getLibcallName see the same registrations. Do not
  // re-list a subset here — that was the split-brain that left frem/sqrt/…
  // as shipping ICEs. copysign is .lower (bit trick), not a libcall.

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
  // G_JUMP_TABLE (LOAD_ADDR of the table) and G_BRJT (scale+load+BR_JT).
  // AsmPrinter expands BR_JT to JALR_W r0 (HaydnAsmPrinter.cpp:813).
  setMinimumJumpTableEntries(4);

  // Product Format E parcels use registry EncodedBytes (12). Function
  // alignment must be a power of two that *divides* that size so any
  // MCAssembler / LLD pad between pure parcel streams is a multiple of
  // EncodedBytes (no residual 4/8-byte holes). Largest such power of two is
  // 1 << countr_zero(EncodedBytes) = Align(4). Larger power-of-two requests
  // (Align(8)/16/256, e.g. __attribute__((aligned(256)))) force pads with
  // size ≡ 4 or 8 (mod 12) and break the parcel stream at link time.
  // Min/Pref are both that product maximum; AsmPrinter also clamps IR/user
  // function alignment to this value (see HaydnAsmPrinter).
  const unsigned ParcelBytes =
      haydn::format::maxEncodedBytesInProfile(
          haydn::format::ObjectEncodingProfileID::E96)
          .Value;
  assert(ParcelBytes != 0 && "production EncodedBytes must be non-zero");
  const Align ProductFnAlign(1u << llvm::countr_zero(ParcelBytes));
  setMinFunctionAlignment(ProductFnAlign);
  setPrefFunctionAlignment(ProductFnAlign);
}

bool HaydnTargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  // Peer: HexagonISelLowering.cpp:225-234 CheckReturn(RetCC_*).
  // Overlay: i128/half/bfloat are not RetCC types. split Outs of 2×i64
  // is not a documented C ABI; CallLowering fail-closes on the original
  // type before sret demotion. Do not reject interrupt/naked here —
  // false would sret-demote (AIE1ISelLowering.cpp:964 rejects at
  // return lowering; Haydn CallLowering matches that).
  if (RetTy) {
    if (const IntegerType *IT = dyn_cast<IntegerType>(RetTy)) {
      if (IT->getBitWidth() > 64)
        return false;
    }
    if (RetTy->isHalfTy() || RetTy->isBFloatTy())
      return false;
  }
  for (const ISD::OutputArg &Out : Outs) {
    if (HaydnCCAssignRejectsType(Out.VT))
      return false;
  }
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  if (!CCInfo.CheckReturn(Outs, RetCC_Haydn))
    return false;
  for (const CCValAssign &VA : RVLocs)
    if (VA.isRegLoc() && HaydnLocIsReservedSoftZero(VA.getLocReg()))
      return false;
  return true;
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

bool HaydnTargetLowering::allowsMisalignedMemoryAccesses(
    EVT VT, unsigned /*AddrSpace*/, Align Alignment,
    MachineMemOperand::Flags /*Flags*/, unsigned *Fast) const {
  // BundleSim faults EA that is not naturally aligned for the access width
  // (byte any, halfword %2, word %4, dword/LD64 %8). Mark Fast=0 always so
  // even borderline cases are not preferred over scalar splits.
  if (Fast)
    *Fast = 0;

  TypeSize TS = VT.getStoreSizeInBits();
  if (TS.isScalable())
    return false;
  const uint64_t SizeBits = TS.getFixedValue();
  if (SizeBits == 0)
    return false;
  if (SizeBits <= 8)
    return true; // byte — any alignment

  // ABI DataLayout i64:32 (scalar) / v64:64 (vector): a 4-byte-aligned
  // 64-bit scalar or DR value is representable; ISel splits LD64/ST64 into
  // LD32 pairs when MMO align < 8 (LD32 needs align 4). Accept Align>=4 for
  // those shapes.
  if (SizeBits == 64 && (VT.isScalarInteger() || VT.isVector()))
    return Alignment >= Align(4);

  // Other widths: full-size natural alignment (halfword≥2, word≥4, …).
  // Rejects LV/SLP `load <4 x i16> align 2` (coremark matrix_add_const).
  //
  // Natural alignment is representable as Align only for power-of-two byte
  // widths. Non-power-of-two store sizes (i96/v6i16=12B, i80=10B, i48=6B)
  // are served by tiling into power-of-two pieces; the strictest alignment
  // that tiling imposes is the largest power of two dividing the byte width
  // (12B→3×LD32 needs ≥4; 10B→5×LD16 needs ≥2). Never construct Align from
  // a raw byte count — Align(12) asserts isPowerOf2_64 (yarpgen seed14:
  // ConstantHoisting → getIntImmCostInst(Store) → this hook on an i96
  // constant store at align 1 aborted clang, exit 134). For every
  // power-of-two width the shift below reproduces the byte count exactly.
  const uint64_t StoreBytes = SizeBits / 8;
  return Alignment >= Align(uint64_t(1) << llvm::countr_zero(StoreBytes));
}

bool HaydnTargetLowering::areJTsAllowed(const Function *Fn) const {
  // Jump tables are enabled. Selector G_BRJT → BR_JT; printer → JALR_W r0.
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

TargetLowering::ConstraintType
HaydnTargetLowering::getConstraintType(StringRef Constraint) const {
  // Peer: RISCVISelLowering.cpp:24570 ('f' → C_RegisterClass). Without this,
  // InlineAsmLowering treats 'd' as C_Unknown and asserts.
  if (Constraint.size() == 1 && Constraint[0] == 'd')
    return C_RegisterClass;
  return TargetLowering::getConstraintType(Constraint);
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
    case 'd':
      // DR64 data register. Peer: RISCV 'f' / AArch64 'w' typed-file
      // constraint. 64-bit values and 64-bit SIMD packs live here.
      if (VT == MVT::i64 || VT == MVT::f64 ||
          (VT.isVector() && VT.getSizeInBits() == 64))
        return std::make_pair(0U, &Haydn::DR64RegClass);
      break;
    default:
      break;
    }
  }
  // getRegAsmName is the TableGen def name (R13/R14/R15), not the asm
  // spelling (sp/fp/lr). Map both so `~{lr}` / `~{r15}` resolve; an empty
  // RC silently drops the clobber and PEI never sees the def.
  StringRef PhysConstraint = Constraint;
  if (Constraint.size() >= 3 && Constraint.front() == '{' &&
      Constraint.back() == '}') {
    const StringRef Inner = Constraint.drop_front().drop_back();
    if (Inner.equals_insensitive("lr") || Inner.equals_insensitive("r15"))
      PhysConstraint = "{R15}";
    else if (Inner.equals_insensitive("sp") || Inner.equals_insensitive("r13"))
      PhysConstraint = "{R13}";
    else if (Inner.equals_insensitive("fp") || Inner.equals_insensitive("r14"))
      PhysConstraint = "{R14}";
    else if (Inner.equals_insensitive("r0"))
      PhysConstraint = "{R0}";
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, PhysConstraint, VT);
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
 // MMO policy (peer Hexagon L2_load*_pbr vs V6_vgatherm*):
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
  // ISA-65 fused round-sat-store: stores the 32-bit saturated lane (mem32).
  case Intrinsic::haydn_d_sw_f64rs_post_imm:
  case Intrinsic::haydn_d_sw_f64rs_post_reg:
  case Intrinsic::haydn_d_sw_f64rs_with_imm:
  case Intrinsic::haydn_d_sw_f64rs_with_reg:
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

  // llvm.va_copy(dest, src): 5×i32 structured va_list (20 bytes).
  case Intrinsic::vacopy: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = EVT::getIntegerVT(I.getContext(), 160);
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = Align(4);
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOStore;
    return true;
  }

  default:
    return false;
  }
}
