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
#include "Haydn.h"
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
  // + FeatureAGU as ISA baseline (post/pre-inc fuse is the sole update-addr
  // path). FeatureHWLoop does not enable HardwareLoops formation.
  // Full "haydn" CPU adds CircularBuffer / BitReversed / SIMD via
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

  // Initialize GlobalISel objects. Combiners are pipeline passes, not
  // subtarget members (HaydnTargetMachine addPreLegalizeMachineIR /
  // addPreRegBankSelect).
  CallLoweringInfo = std::make_unique<HaydnCallLowering>(TLInfo);
  InlineAsmLoweringInfo =
      std::make_unique<InlineAsmLowering>(getTargetLowering());
  Legalizer = std::make_unique<HaydnLegalizerInfo>(*this);
  RegBankInfo = std::make_unique<HaydnRegisterBankInfo>();
  InstSelector.reset(createHaydnInstructionSelector(
      *this, *static_cast<const HaydnRegisterBankInfo *>(RegBankInfo.get())));
}

HaydnSubtarget::~HaydnSubtarget() = default;

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
  // D1.29 verified mechanism (2026-08-30; pinned by
  // llvm/test/CodeGen/Haydn/d129-window-scheduler-forfeit.ll, tracked as the
  // GOALS "WindowScheduler forfeit" row): this override is the ONLY layer
  // that keeps the generic WindowScheduler off Haydn's ZOL loops on the
  // product arm. MachinePipeliner::canPipelineLoop PASSES ZOL loops to WS —
  // analyzeLoopForPipelining returns the HaydnPipelinerLoopInfo whenever
  // -haydn-zol-pipelining is on — so after SMS declines
  // (MachinePipeliner.cpp useWindowScheduler: WS_On && !Changed, and always
  // under -window-sched=force) the WindowScheduler WOULD run on them. It
  // must not:
  //  - PseudoLoopEnd (isMeta + isTerminator, HaydnPseudos.td) is skipped by
  //    WindowScheduler::initialize() with the other meta/terminator MIs and
  //    by generateTripleMBB() in all three copies, leaving the TripleMBB
  //    with no ZOL latch while WS's expand() implements no PipelinerLoopInfo
  //    ZOL law at all (no adjustTripCount/$adj edit, no guarded prologues).
  //  - Soft counted loops are excluded independently: initialize() rejects
  //    any MI in the target ignore set ("Special MI defined by target is not
  //    allowed in window scheduling!") — HaydnPipelinerLoopInfo puts the
  //    loop-control chain (EndLoop/CmpMI/InvertMI) in that set.
  // Unblocking WS for Haydn is an HC#0 item (smallest common delta recorded
  // in contracts/pipeline.md): meta-terminator TripleMBB handling plus a ZOL
  // expand law, and a target-owned ignore-set law for the soft arm.
  // (-haydn-zol-pipelining consumed via the one accessor; SMS ZOL admission
  // reads the same bit in shouldUseSchedule.)
  if (haydnZOLPipeliningEnabled())
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

  // No-forwarding floor (2026-08-21 latency P3): the itinerary edge formula
  // is DefCycle - UseCycle + 1, so a def booked at operand cycle 1 feeding a
  // use operand booked at cycle 2 (AccFirst acc reads, the late wb-shape
  // operand) computes an ENGAGED ZERO. That is the exact latency-0 collapse
  // class banned since 4f54cc3f (MOVE32_DR→SEXT muldf3 densify): a zero
  // Data edge lets a true-RAW producer and consumer share a ReadyCycle,
  // which the no-interlock hardware cannot honor. The documented law
  // "Data edges must keep latency >= 1" is enforced here — the ONE floor
  // for every Data edge, not a per-family case.
  if (Dep.getLatency() < 1)
    Dep.setLatency(1);

  // Architectural Data_Latency is left intact for every Data edge, including
  // load→use (itinerary 2). Schedulers must see the true ISA latency so II
  // and density are measurable; empty-cycle materialization and the
  // HaydnLatencyStalls addPreSched2 net insert any remaining stalls. There is
  // no load→use soften: a non-architectural heuristic would make every
  // initiation-interval figure fiction until stalls were patched in later.

  // Haydn has **no** intra-bundle / same-cycle register forwarding: every
  // slot in a product cycle reads the pre-cycle register snapshot
  // (BundleSim execution model; durable-rules §Schedule). Data edges must
  // keep latency ≥1 so a producer and its consumer never share **available
  // / ReadyCycle** — that is the primary coissue separator (layer 1 of the
  // product coissue law in HaydnBundleMaterialize.h).
  //
  // Anti/Output stay latency 0 so they *may* share a ready cycle; emission
  // order must still preserve Anti (use before redef) under Format field
  // order (layer 3 canCoissueProductCycle). Do not collapse Data latency to
  // 0 (prior ALU→ALU "forwarding" densified MOVE32_DR_*→SEXT — muldf3 bug).
  // Reg-level true-RAW checks are belts over this dep-graph / avail-cycle
  // contract, not the sole coissue authority.
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

  // Soft-float (no FPU): compiler-rt arithmetic/compare/convert + libm.
  // This is the ONE Haydn libcall-name table. TargetLoweringBase constructs
  // TLI.Libcalls via this hook, so GISel createLibcall (TLI.getLibcallName)
  // and IR-level LibcallLoweringInfo share the same names. Haydn is not a
  // SystemRuntimeLibrary / isDefaultLibcallArch, so baremetal defaults leave
  // libm and even compiler-rt arith unset until registered here.
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

  // IEEE half ↔ f32/f64. compiler-rt symbols exist (__extendhfsf2 etc.).
  // Haydn CC has no f16; the legalizer custom path passes integer bits in a
  // GPR32 (compiler-rt without COMPILER_RT_HAS_FLOAT16 is uint16_t).
  Info.setLibcallImpl(RTLIB::FPEXT_F16_F32, RTLIB::impl___extendhfsf2);
  Info.setLibcallImpl(RTLIB::FPEXT_F16_F64, RTLIB::impl___extendhfdf2);
  Info.setLibcallImpl(RTLIB::FPROUND_F32_F16, RTLIB::impl___truncsfhf2);
  Info.setLibcallImpl(RTLIB::FPROUND_F64_F16, RTLIB::impl___truncdfhf2);

  // libm (compile succeeds; missing libm → link error, not legalizer crash)
  Info.setLibcallImpl(RTLIB::REM_F32, RTLIB::impl_fmodf);
  Info.setLibcallImpl(RTLIB::REM_F64, RTLIB::impl_fmod);
  Info.setLibcallImpl(RTLIB::SQRT_F32, RTLIB::impl_sqrtf);
  Info.setLibcallImpl(RTLIB::SQRT_F64, RTLIB::impl_sqrt);
  Info.setLibcallImpl(RTLIB::FMA_F32, RTLIB::impl_fmaf);
  Info.setLibcallImpl(RTLIB::FMA_F64, RTLIB::impl_fma);
  Info.setLibcallImpl(RTLIB::SIN_F32, RTLIB::impl_sinf);
  Info.setLibcallImpl(RTLIB::SIN_F64, RTLIB::impl_sin);
  Info.setLibcallImpl(RTLIB::COS_F32, RTLIB::impl_cosf);
  Info.setLibcallImpl(RTLIB::COS_F64, RTLIB::impl_cos);
  Info.setLibcallImpl(RTLIB::EXP_F32, RTLIB::impl_expf);
  Info.setLibcallImpl(RTLIB::EXP_F64, RTLIB::impl_exp);
  Info.setLibcallImpl(RTLIB::LOG_F32, RTLIB::impl_logf);
  Info.setLibcallImpl(RTLIB::LOG_F64, RTLIB::impl_log);
  Info.setLibcallImpl(RTLIB::LOG2_F32, RTLIB::impl_log2f);
  Info.setLibcallImpl(RTLIB::LOG2_F64, RTLIB::impl_log2);
  Info.setLibcallImpl(RTLIB::LOG10_F32, RTLIB::impl_log10f);
  Info.setLibcallImpl(RTLIB::LOG10_F64, RTLIB::impl_log10);
  Info.setLibcallImpl(RTLIB::POW_F32, RTLIB::impl_powf);
  Info.setLibcallImpl(RTLIB::POW_F64, RTLIB::impl_pow);
  Info.setLibcallImpl(RTLIB::POWI_F32, RTLIB::impl___powisf2);
  Info.setLibcallImpl(RTLIB::POWI_F64, RTLIB::impl___powidf2);
  Info.setLibcallImpl(RTLIB::TRUNC_F32, RTLIB::impl_truncf);
  Info.setLibcallImpl(RTLIB::TRUNC_F64, RTLIB::impl_trunc);
  Info.setLibcallImpl(RTLIB::ROUND_F32, RTLIB::impl_roundf);
  Info.setLibcallImpl(RTLIB::ROUND_F64, RTLIB::impl_round);
  Info.setLibcallImpl(RTLIB::ROUNDEVEN_F32, RTLIB::impl_roundevenf);
  Info.setLibcallImpl(RTLIB::ROUNDEVEN_F64, RTLIB::impl_roundeven);
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
