//===-- HaydnSubtarget.cpp - Define Subtarget for the Haydn --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Haydn specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "HaydnSubtarget.h"
#include "GISel/HaydnCallLowering.h"
#include "GISel/HaydnLegalizerInfo.h"
#include "llvm/Support/CommandLine.h"
#include "GISel/HaydnRegisterBankInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/LibcallLoweringInfo.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/IR/RuntimeLibcalls.h"

using namespace llvm;

namespace llvm {
// Forward declare the factory function defined in HaydnInstructionSelector.cpp
InstructionSelector *
createHaydnInstructionSelector(const HaydnSubtarget &ST,
                               const HaydnRegisterBankInfo &RBI);
} // namespace llvm

#define DEBUG_TYPE "haydn-subtarget"
#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "HaydnGenSubtargetInfo.inc"

void HaydnSubtarget::anchor() {}

HaydnSubtarget &HaydnSubtarget::initializeSubtargetDependencies(
    const Triple &TT, StringRef CPUName, StringRef TuneCPUName, StringRef FS) {

  // Default to the "generic" CPU. The generic model carries FeatureHWLoop
  // + FeatureAGU (product baseline: post/pre-inc fuse is the sole update-addr
  // path). Full "haydn" CPU adds CircularBuffer / BitReversed / SIMD via
  // -mcpu=haydn. Disable AGU densify with -mattr=-agu.
  if (CPUName.empty())
    CPUName = "generic";
  if (TuneCPUName.empty())
    TuneCPUName = CPUName;

  // Initialize feature flags to default values
  HasAGU = false;
  HasCircularBuffer = false;
  HasBitReversed = false;
  HasHWLoop = false;
  HasSIMD = false;
  // AIE model: R12 is a normal allocatable caller-saved GPR (no free AT).
  // mattr=+frame-pointer → force FP.
  UseFramePointer = false;

  ParseSubtargetFeatures(CPUName, TuneCPUName, FS);
  return *this;
}

HaydnSubtarget::HaydnSubtarget(const Triple &TT, StringRef CPU, StringRef TuneCPU,
                             StringRef FS, const TargetMachine &TM)
    : HaydnGenSubtargetInfo(TT, CPU, TuneCPU, FS),
      FrameLowering(initializeSubtargetDependencies(TT, CPU, TuneCPU, FS)),
      InstrInfo(*this), TLInfo(TM, *this) {

  // Immutable production ObjectEncodingProfile (E96). The target machine
  // resolves the same production identity; synthetic profiles are never
  // selected for a real subtarget. (void)TM keeps the ctor signature stable.
  (void)TM;
  EncodingProfile = haydn::format::ObjectEncodingProfileID::E96;
  assert(haydn::format::isProductionProfile(EncodingProfile) &&
         "Haydn subtarget must use the production object-encoding profile");

  // Construct ItinData from the itinerary arrays (extern-declared by the CTOR
  // section above) and the sched model from the base MCSubtargetInfo.
  ItinData = InstrItineraryData(
      MCSubtargetInfo::getSchedModel(), HaydnStages,
      HaydnOperandCycles, HaydnForwardingPaths);

  // Initialize GlobalISel objects
  CallLoweringInfo = std::make_unique<HaydnCallLowering>(TLInfo);
  InlineAsmLoweringInfo =
      std::make_unique<InlineAsmLowering>(getTargetLowering());
  Legalizer = std::make_unique<HaydnLegalizerInfo>(*this);
  RegBankInfo = std::make_unique<HaydnRegisterBankInfo>();
  InstSelector.reset(createHaydnInstructionSelector(
      *this, *static_cast<const HaydnRegisterBankInfo *>(RegBankInfo.get())));
  // PostLegalizerCombiner not implemented for M0
}

HaydnSubtarget::~HaydnSubtarget() = default;

// declared in HaydnInstrInfo.cpp at global scope (uses 'using namespace
// llvm' but the variable itself is global, not in llvm::).
extern llvm::cl::opt<bool> EnableZOLPipelining;

void HaydnSubtarget::overrideSchedPolicy(MachineSchedPolicy &Policy,
                                         const SchedRegion &Region) const {
  // AIE/RISCV: pressure tracking is critical even for small loops (spills are
  // expensive on VLIW). Default GenericScheduler skips small regions.
  (void)Region;
  Policy.ShouldTrackPressure = true;
  // Prefer bottom-up (AIE PreRA OnlyBottomUp contract); PreRA initPolicy also
  // forces this when -haydn-premisched-force-bottom-up is on.
  Policy.OnlyBottomUp = true;
  Policy.OnlyTopDown = false;
}

void HaydnSubtarget::overridePostRASchedPolicy(MachineSchedPolicy &Policy,
                                               const SchedRegion &Region) const {
  // Post-RA pack path uses HaydnPostRASchedStrategy + HR; pressure tracking is
  // meaningless on physregs (upstream notes). Keep default direction.
  (void)Region;
  Policy.ShouldTrackPressure = false;
}

bool HaydnSubtarget::enableWindowScheduler() const {
  // The upstream WindowScheduler crashes on ZOL-form loops (its TripleMBB
  // cloning mishandles the PseudoLoopEnd meta-terminator). When ZOL
  // pipelining is enabled, use only the SwingModuloScheduler, which handles
  // ZOL loops correctly via PipelinerLoopInfo.
  if (EnableZOLPipelining)
    return false;
  return true;
}

void HaydnSubtarget::adjustSchedDependency(
    SUnit *Def, int DefOpIdx, SUnit *Use, int UseOpIdx, SDep &Dep,
    const TargetSchedModel *SchedModel) const {
  // Only rewrite edges between real instructions with known operand indices.
  // Order/output/chain edges arrive with DefOpIdx/UseOpIdx == -1 and must keep
  // their upstream latency.
  if (!Def->isInstr() || !Use->isInstr())
    return;
  if (DefOpIdx < 0 || UseOpIdx < 0)
    return;

  MachineInstr *DefMI = Def->getInstr();
  MachineInstr *UseMI = Use->getInstr();
  // Pseudo/PHI operands have no meaningful itinerary latency; leave the
  // upstream defaults (the SMS recurrence edges go through PHI SUnits).
  if (DefMI->isTransient() || UseMI->isTransient())
    return;

  // Recompute the per-operand latency from the itinerary. For MAC this yields
  // the asymmetric value encoded in OperandCycles [2,1,1,2]: acc->acc = 1
  // result->other = 2. For all other instructions the itinerary OperandCycles
  // are uniform [1], so the result equals the upstream default and is a no-op.
  // Returns std::nullopt when the itinerary has no operand-cycle entry for the
  // given indices; in that case keep the upstream default latency.
  std::optional<unsigned> Latency = InstrInfo.getOperandLatency(
      &ItinData, *DefMI, DefOpIdx, *UseMI, UseOpIdx);
  if (Latency)
    Dep.setLatency(*Latency);

  if (Dep.getKind() != SDep::Data)
    return;

  // Load→use: itinerary is Data_Latency=2. Soften to 1 for non-accumulator
  // consumers so dct-style LateStart load placement still has chain room
  // under ResMII≈II. Keep latency 2 when the use is an accumulator-MAC
  // (tied-def) so dual-load reductions (vec_dot) retain enough schedule
  // span for MaxStageCount≥1.
  if (DefMI->mayLoad() && Dep.getLatency() > 1) {
    bool UseIsAccMAC = false;
    for (const MachineOperand &MO : UseMI->operands()) {
      if (MO.isReg() && MO.isUse() && MO.isTied()) {
        UseIsAccMAC = true;
        break;
      }
    }
    if (!UseIsAccMAC)
      Dep.setLatency(1);
  }

  // Haydn has **no** intra-bundle / same-cycle register forwarding: every
  // slot in a product cycle reads the pre-cycle register snapshot
  // (BundleSim execution model; durable-rules §Schedule). Data edges must
  // keep latency ≥1 so a producer and its consumer never share ReadyCycle.
  //
  // A prior ALU→ALU latency 1→0 collapse ("same-cycle VLIW forwarding") was
  // architecturally false and densified MOVE32_DR_*→SEXT (soft-float half
  // extract) into one multi-MI product cycle — muldf3 O1/O2 miscompile
  // (1.5*2.5). Do not reintroduce forwarding via latency collapse.
  // Post-RA hasSameBundleRAW + cycleMembersHaveTrueRAW are belts only.
}

void HaydnSubtarget::initLibcallLoweringInfo(
    LibcallLoweringInfo &Info) const {
  // Haydn is a baremetal target with no hardware division/remainder.
  // Use the standard compiler-rt libcall names.
  Info.setLibcallImpl(RTLIB::SDIV_I32, RTLIB::impl___divsi3);
  Info.setLibcallImpl(RTLIB::UDIV_I32, RTLIB::impl___udivsi3);
  Info.setLibcallImpl(RTLIB::SREM_I32, RTLIB::impl___modsi3);
  Info.setLibcallImpl(RTLIB::UREM_I32, RTLIB::impl___umodsi3);

  Info.setLibcallImpl(RTLIB::SDIV_I64, RTLIB::impl___divdi3);
  Info.setLibcallImpl(RTLIB::UDIV_I64, RTLIB::impl___udivdi3);
  Info.setLibcallImpl(RTLIB::SREM_I64, RTLIB::impl___moddi3);
  Info.setLibcallImpl(RTLIB::UREM_I64, RTLIB::impl___umoddi3);

  // 128-bit division (for completeness)
  Info.setLibcallImpl(RTLIB::SDIV_I128, RTLIB::impl___divti3);
  Info.setLibcallImpl(RTLIB::UDIV_I128, RTLIB::impl___udivti3);
  Info.setLibcallImpl(RTLIB::SREM_I128, RTLIB::impl___modti3);
  Info.setLibcallImpl(RTLIB::UREM_I128, RTLIB::impl___umodti3);

  // Memory intrinsics - use standard names
  Info.setLibcallImpl(RTLIB::MEMCPY, RTLIB::impl_memcpy);
  Info.setLibcallImpl(RTLIB::MEMMOVE, RTLIB::impl_memmove);
  Info.setLibcallImpl(RTLIB::MEMSET, RTLIB::impl_memset);
  Info.setLibcallImpl(RTLIB::BZERO, RTLIB::impl_bzero);

  // Atomic libcalls (: atomics as libcalls). Haydn has no native
  // atomics; with MaxAtomicSizeInBitsSupported=0, AtomicExpandPass lowers
  // every atomic load/store/RMW/cmpxchg to a __atomic_* runtime libcall
  // at IR level. The default RuntimeLibcallsInfo for an unrecognized
  // baremetal triple (haydn-unknown-elf) leaves these Impl entries as
  // Unsupported, so getLibcallName returns null and AtomicExpandPass
  // silently strips the atomic to `ret poison`. Setting the standard
  // __atomic_* compiler-rt/libgcc Impl names makes the libcall available.
  // See atomics-libcall-expansion.
  Info.setLibcallImpl(RTLIB::ATOMIC_LOAD, RTLIB::impl___atomic_load);
  Info.setLibcallImpl(RTLIB::ATOMIC_LOAD_1, RTLIB::impl___atomic_load_1);
  Info.setLibcallImpl(RTLIB::ATOMIC_LOAD_2, RTLIB::impl___atomic_load_2);
  Info.setLibcallImpl(RTLIB::ATOMIC_LOAD_4, RTLIB::impl___atomic_load_4);
  Info.setLibcallImpl(RTLIB::ATOMIC_LOAD_8, RTLIB::impl___atomic_load_8);
  Info.setLibcallImpl(RTLIB::ATOMIC_LOAD_16, RTLIB::impl___atomic_load_16);
  Info.setLibcallImpl(RTLIB::ATOMIC_STORE, RTLIB::impl___atomic_store);
  Info.setLibcallImpl(RTLIB::ATOMIC_STORE_1, RTLIB::impl___atomic_store_1);
  Info.setLibcallImpl(RTLIB::ATOMIC_STORE_2, RTLIB::impl___atomic_store_2);
  Info.setLibcallImpl(RTLIB::ATOMIC_STORE_4, RTLIB::impl___atomic_store_4);
  Info.setLibcallImpl(RTLIB::ATOMIC_STORE_8, RTLIB::impl___atomic_store_8);
  Info.setLibcallImpl(RTLIB::ATOMIC_STORE_16, RTLIB::impl___atomic_store_16);
  Info.setLibcallImpl(RTLIB::ATOMIC_EXCHANGE, RTLIB::impl___atomic_exchange);
  Info.setLibcallImpl(RTLIB::ATOMIC_EXCHANGE_1,
                      RTLIB::impl___atomic_exchange_1);
  Info.setLibcallImpl(RTLIB::ATOMIC_EXCHANGE_2,
                      RTLIB::impl___atomic_exchange_2);
  Info.setLibcallImpl(RTLIB::ATOMIC_EXCHANGE_4,
                      RTLIB::impl___atomic_exchange_4);
  Info.setLibcallImpl(RTLIB::ATOMIC_EXCHANGE_8,
                      RTLIB::impl___atomic_exchange_8);
  Info.setLibcallImpl(RTLIB::ATOMIC_EXCHANGE_16,
                      RTLIB::impl___atomic_exchange_16);
  Info.setLibcallImpl(RTLIB::ATOMIC_COMPARE_EXCHANGE,
                      RTLIB::impl___atomic_compare_exchange);
  Info.setLibcallImpl(RTLIB::ATOMIC_COMPARE_EXCHANGE_1,
                      RTLIB::impl___atomic_compare_exchange_1);
  Info.setLibcallImpl(RTLIB::ATOMIC_COMPARE_EXCHANGE_2,
                      RTLIB::impl___atomic_compare_exchange_2);
  Info.setLibcallImpl(RTLIB::ATOMIC_COMPARE_EXCHANGE_4,
                      RTLIB::impl___atomic_compare_exchange_4);
  Info.setLibcallImpl(RTLIB::ATOMIC_COMPARE_EXCHANGE_8,
                      RTLIB::impl___atomic_compare_exchange_8);
  Info.setLibcallImpl(RTLIB::ATOMIC_COMPARE_EXCHANGE_16,
                      RTLIB::impl___atomic_compare_exchange_16);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_ADD_1,
                      RTLIB::impl___atomic_fetch_add_1);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_ADD_2,
                      RTLIB::impl___atomic_fetch_add_2);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_ADD_4,
                      RTLIB::impl___atomic_fetch_add_4);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_ADD_8,
                      RTLIB::impl___atomic_fetch_add_8);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_ADD_16,
                      RTLIB::impl___atomic_fetch_add_16);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_SUB_1,
                      RTLIB::impl___atomic_fetch_sub_1);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_SUB_2,
                      RTLIB::impl___atomic_fetch_sub_2);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_SUB_4,
                      RTLIB::impl___atomic_fetch_sub_4);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_SUB_8,
                      RTLIB::impl___atomic_fetch_sub_8);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_SUB_16,
                      RTLIB::impl___atomic_fetch_sub_16);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_AND_1,
                      RTLIB::impl___atomic_fetch_and_1);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_AND_2,
                      RTLIB::impl___atomic_fetch_and_2);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_AND_4,
                      RTLIB::impl___atomic_fetch_and_4);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_AND_8,
                      RTLIB::impl___atomic_fetch_and_8);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_AND_16,
                      RTLIB::impl___atomic_fetch_and_16);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_OR_1,
                      RTLIB::impl___atomic_fetch_or_1);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_OR_2,
                      RTLIB::impl___atomic_fetch_or_2);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_OR_4,
                      RTLIB::impl___atomic_fetch_or_4);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_OR_8,
                      RTLIB::impl___atomic_fetch_or_8);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_OR_16,
                      RTLIB::impl___atomic_fetch_or_16);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_XOR_1,
                      RTLIB::impl___atomic_fetch_xor_1);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_XOR_2,
                      RTLIB::impl___atomic_fetch_xor_2);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_XOR_4,
                      RTLIB::impl___atomic_fetch_xor_4);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_XOR_8,
                      RTLIB::impl___atomic_fetch_xor_8);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_XOR_16,
                      RTLIB::impl___atomic_fetch_xor_16);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_NAND_1,
                      RTLIB::impl___atomic_fetch_nand_1);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_NAND_2,
                      RTLIB::impl___atomic_fetch_nand_2);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_NAND_4,
                      RTLIB::impl___atomic_fetch_nand_4);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_NAND_8,
                      RTLIB::impl___atomic_fetch_nand_8);
  Info.setLibcallImpl(RTLIB::ATOMIC_FETCH_NAND_16,
                      RTLIB::impl___atomic_fetch_nand_16);

  // Soft-float (no FPU): compiler-rt arithmetic/compare/convert + libm
  // rounding/min/max. Keep in sync with HaydnTargetLowering setLibcallImpl
  // (GISel LegalizerHelper::createLibcall reads TLI; LibcallLoweringInfo
  // feeds IR-level expands). Baremetal defaults leave floorf/fminf unset.
  Info.setLibcallImpl(RTLIB::ADD_F32, RTLIB::impl___addsf3);
  Info.setLibcallImpl(RTLIB::SUB_F32, RTLIB::impl___subsf3);
  Info.setLibcallImpl(RTLIB::MUL_F32, RTLIB::impl___mulsf3);
  Info.setLibcallImpl(RTLIB::DIV_F32, RTLIB::impl___divsf3);
  Info.setLibcallImpl(RTLIB::OEQ_F32, RTLIB::impl___eqsf2);
  Info.setLibcallImpl(RTLIB::UNE_F32, RTLIB::impl___nesf2);
  Info.setLibcallImpl(RTLIB::OLT_F32, RTLIB::impl___ltsf2);
  Info.setLibcallImpl(RTLIB::OLE_F32, RTLIB::impl___lesf2);
  Info.setLibcallImpl(RTLIB::OGE_F32, RTLIB::impl___gesf2);
  Info.setLibcallImpl(RTLIB::OGT_F32, RTLIB::impl___gtsf2);
  Info.setLibcallImpl(RTLIB::UO_F32, RTLIB::impl___unordsf2);
  Info.setLibcallImpl(RTLIB::FPTOSINT_F32_I32, RTLIB::impl___fixsfsi);
  Info.setLibcallImpl(RTLIB::FPTOSINT_F32_I64, RTLIB::impl___fixsfdi);
  Info.setLibcallImpl(RTLIB::FPTOUINT_F32_I32, RTLIB::impl___fixunssfsi);
  Info.setLibcallImpl(RTLIB::FPTOUINT_F32_I64, RTLIB::impl___fixunssfdi);
  Info.setLibcallImpl(RTLIB::SINTTOFP_I32_F32, RTLIB::impl___floatsisf);
  Info.setLibcallImpl(RTLIB::SINTTOFP_I64_F32, RTLIB::impl___floatdisf);
  Info.setLibcallImpl(RTLIB::UINTTOFP_I32_F32, RTLIB::impl___floatunsisf);
  Info.setLibcallImpl(RTLIB::UINTTOFP_I64_F32, RTLIB::impl___floatundisf);

  Info.setLibcallImpl(RTLIB::ADD_F64, RTLIB::impl___adddf3);
  Info.setLibcallImpl(RTLIB::SUB_F64, RTLIB::impl___subdf3);
  Info.setLibcallImpl(RTLIB::MUL_F64, RTLIB::impl___muldf3);
  Info.setLibcallImpl(RTLIB::DIV_F64, RTLIB::impl___divdf3);
  Info.setLibcallImpl(RTLIB::OEQ_F64, RTLIB::impl___eqdf2);
  Info.setLibcallImpl(RTLIB::UNE_F64, RTLIB::impl___nedf2);
  Info.setLibcallImpl(RTLIB::OLT_F64, RTLIB::impl___ltdf2);
  Info.setLibcallImpl(RTLIB::OLE_F64, RTLIB::impl___ledf2);
  Info.setLibcallImpl(RTLIB::OGE_F64, RTLIB::impl___gedf2);
  Info.setLibcallImpl(RTLIB::OGT_F64, RTLIB::impl___gtdf2);
  Info.setLibcallImpl(RTLIB::UO_F64, RTLIB::impl___unorddf2);
  Info.setLibcallImpl(RTLIB::FPTOSINT_F64_I32, RTLIB::impl___fixdfsi);
  Info.setLibcallImpl(RTLIB::FPTOSINT_F64_I64, RTLIB::impl___fixdfdi);
  Info.setLibcallImpl(RTLIB::FPTOUINT_F64_I32, RTLIB::impl___fixunsdfsi);
  Info.setLibcallImpl(RTLIB::FPTOUINT_F64_I64, RTLIB::impl___fixunsdfdi);
  Info.setLibcallImpl(RTLIB::SINTTOFP_I32_F64, RTLIB::impl___floatsidf);
  Info.setLibcallImpl(RTLIB::SINTTOFP_I64_F64, RTLIB::impl___floatdidf);
  Info.setLibcallImpl(RTLIB::UINTTOFP_I32_F64, RTLIB::impl___floatunsidf);
  Info.setLibcallImpl(RTLIB::UINTTOFP_I64_F64, RTLIB::impl___floatundidf);
  Info.setLibcallImpl(RTLIB::FPEXT_F32_F64, RTLIB::impl___extendsfdf2);
  Info.setLibcallImpl(RTLIB::FPROUND_F64_F32, RTLIB::impl___truncdfsf2);

  // libm (compile succeeds; missing libm → link error, not legalizer crash)
  Info.setLibcallImpl(RTLIB::FLOOR_F32, RTLIB::impl_floorf);
  Info.setLibcallImpl(RTLIB::FLOOR_F64, RTLIB::impl_floor);
  Info.setLibcallImpl(RTLIB::CEIL_F32, RTLIB::impl_ceilf);
  Info.setLibcallImpl(RTLIB::CEIL_F64, RTLIB::impl_ceil);
  Info.setLibcallImpl(RTLIB::RINT_F32, RTLIB::impl_rintf);
  Info.setLibcallImpl(RTLIB::RINT_F64, RTLIB::impl_rint);
  Info.setLibcallImpl(RTLIB::NEARBYINT_F32, RTLIB::impl_nearbyintf);
  Info.setLibcallImpl(RTLIB::NEARBYINT_F64, RTLIB::impl_nearbyint);
  Info.setLibcallImpl(RTLIB::FMIN_F32, RTLIB::impl_fminf);
  Info.setLibcallImpl(RTLIB::FMIN_F64, RTLIB::impl_fmin);
  Info.setLibcallImpl(RTLIB::FMAX_F32, RTLIB::impl_fmaxf);
  Info.setLibcallImpl(RTLIB::FMAX_F64, RTLIB::impl_fmax);
}
