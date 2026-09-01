//===-- HaydnTargetMachine.cpp - Define TargetMachine for Haydn ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the info about Haydn target spec.
//
//===----------------------------------------------------------------------===//

#include "HaydnTargetMachine.h"
#include "GISel/HaydnPostLegalizerCombiner.h"
#include "GISel/HaydnPostSelectOptimize.h"
#include "GISel/HaydnPreLegalizerCombiner.h"
#include "Haydn.h"
#include "HaydnExpandPseudos.h"
#include "HaydnEnsureTerminators.h"
#include "HaydnFinalizeBundle.h"
#include "HaydnLateConvergence.h"
#include "HaydnLatencyStalls.h"
#include "HaydnVerifyBundles.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnMachineScheduler.h"
#include "HaydnPostRASchedStrategy.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetTransformInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "TargetInfo/HaydnTargetInfo.h"
#include "llvm/CodeGen/BranchRelaxation.h"
#include "llvm/CodeGen/Passes.h" // EarlyIfConverterLegacyID
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/CodeGen/MachinePipeliner.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/VLIWMachineScheduler.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include <optional>

using namespace llvm;

//===----------------------------------------------------------------------===//
// Per-pass enable flags (pass bisection). Default ON; disable with
// `-mllvm -haydn-enable-<name>=0` (llc) / `-mllvm -haydn-enable-<name>=0` (clang).
// Pipeline-gated (conditional addPass) — the AArch64/AMDGPU/Hexagon idiom.
// Load-bearing passes (marked *) warn in desc; disabling them yields invalid
// asm. See contracts/pipeline.md for the full table.
//===----------------------------------------------------------------------===//
static cl::opt<bool> EnableHaydnPreLegalizerCombiner(
    "haydn-enable-prelegalizer-combiner", cl::init(true), cl::Hidden,
    cl::desc("Enable HaydnPreLegalizerCombiner (GISel pre-legalize)."));
static cl::opt<bool> EnableHaydnPostLegalizerCombiner(
    "haydn-enable-postlegalizer-combiner", cl::init(true), cl::Hidden,
    cl::desc("Enable HaydnPostLegalizerCombiner (GISel post-legalize; "
             "installed at addPreRegBankSelect, O1+)."));
static cl::opt<bool> EnableHaydnPostSelectOptimize(
    "haydn-enable-postselect-opt", cl::init(true), cl::Hidden,
    cl::desc("Enable HaydnPostSelectOptimize (O1+; live: cross-bank elide)."));
// Post-inc: form + fused-vs-split at InstructionSelect. Leftover *_POST_INC
// (MIR-injected) expands in ExpandPseudos via one helper.
// Tombstone (do not revive): LoadStoreOpt, CircularBuffer stats,
// RedundantCopyElim, FormUpdateAddr, PostPipeliner Stage-0, InterBlock
// Stage-0, formMACs, Role B convert. Sole AGU form is GISel.
// contracts/pipeline.md.
// (2026-07-27): HaydnCFGOptimizer deleted. Post-PEI BranchFolder +
// MachineBlockPlacement already cover empty-forward / identical-succ /
// unreachable / tail-merge; ON/OFF asm identity across Haydn lit kernels with
// no unique VLIW residue. Do not revive a second generic CFG folder.
// CopyElim/ConditionOptimizer deleted; MCP(UseCopyInstr) replaces them.
// Hardware-loop product default. AIE inserts HardwareLoops unconditionally
// at O1+ (AIE2TargetMachine.cpp:81-82). Hexagon defaults ON via
// DisableHardwareLoops (HexagonTargetMachine.cpp:48-49). Haydn flipped ON
// 2026-08-22 after the two qualification legs the park-pin demanded:
// independent (hwloop only: ILSS bkfir gate 160/160 bit-exact, zero hangs,
// 2.6-12.7% bundle win) and combined (hwloop + multi-stage SMS forced ON:
// 160/160 after the FixupHwLoops lift-legality fix closed the 32x32 M=8
// undefined-register miscompile; the lift mechanism itself was then
// deleted by the 2026-08-22 PM2 peer-verified realignment — see the
// HaydnFixupHwLoops.cpp file header). SCEV-proven IR + retained-state
// expansion only; late physical semantic rediscovery is deleted. Never
// revive pre-RA multi-member SMS BUNDLE or force-coissue.
// W68.1: multi-stage SMS is owned by the generic pre-RA MachinePipeliner
// (soft + ZOL, form-uniform PPS-3 bound); the bespoke post-RA host and its
// flags are deleted. Stage-0 PostPipeliner / InterBlock stay deleted
// (tombstone above). Finalize/Verify never call skipFunction — do not reopen that skip.
static_assert(HaydnTargetMachine::hardwareLoopsProductDefaultEnabled(),
              "hardware-loop product default is ON after the 2026-08-22 "
              "independent + combined qualification; this assert pins the "
              "policy against accidental re-parking without evidence");
// W68.2R: S2/LateConvergence driver flag. Default remains off until the
// same-artifact BundleSim + default decision gate. When on, the same
// post-RA scheduler implementation runs at addPostBBSections after the
// common executable tail (outliner/split/BB sections) and before the
// closure Finalize. S1 in addPreSched2 is disposable.
static cl::opt<bool> EnableHaydnSMS2(
    "haydn-sms2", cl::Hidden, cl::init(false),
    cl::desc("W68.2R: invoke the post-RA scheduler a second time (S2; "
             "addPostBBSections after the common executable tail). "
             "Default off."));

static bool HaydnSMS2Enabled() { return EnableHaydnSMS2; }

static cl::opt<bool> EnableHaydnHardwareLoops(
    "haydn-enable-hwloops",
    cl::init(HaydnTargetMachine::hardwareLoopsProductDefaultEnabled()),
    cl::Hidden,
    cl::desc("Enable the SCEV-proven Haydn hardware-loop path; late physical "
             "semantic rediscovery is deleted. Product default follows "
             "HaydnTargetMachine::hardwareLoopsProductDefaultEnabled(); "
             "qualified ON 2026-08-22 (independent + combined legs, ILSS "
             "gate 160/160 bit-exact each)."));
// Pack/Finalize/Verify are unconditional (Finalize/Verify never skip).
// ExpandPseudos is unconditional product legalization.
// The old -haydn-enable-expand-pseudos product-disable switch is retired:
// residual executable pseudos must never reach pack/printer as a "bisect"
// path. Use pass isolation / stop-after for debugging, not a silent skip.
// HaydnPushPopOpt deleted (default-off zombie with ABI/SP/CFI bugs).
// HaydnPEIPeephole deleted; FrameLowering emits FP setup only when hasFP().

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeHaydnTarget() {
  RegisterTargetMachine<HaydnTargetMachine> X(getTheHaydnTarget());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeIRTranslatorPass(PR);
  initializeLegalizerPass(PR);
  initializeRegBankSelectPass(PR);
  initializeInstructionSelectPass(PR);
  initializeGISelCSEAnalysisWrapperPassPass(PR);
  initializeHaydnPostLegalizerCombinerPass(PR);
  initializeHaydnPreLegalizerCombinerPass(PR);
  initializeHaydnPostSelectOptimizePass(PR);
  initializeHaydnExpandPseudosPass(PR);
  initializeHaydnEnsureTerminatorsPass(PR);
  initializeHaydnFinalizeBundlePass(PR);
  initializeHaydnVerifyBundlesPass(PR);
  initializeHaydnLatencyStallsPass(PR);
  initializeHaydnHardwareLoopsPass(PR);
  initializeHaydnFixupHwLoopsPass(PR);
  initializeHaydnLateConvergencePassPass(PR);
  initializeBranchRelaxationLegacyPass(PR);
  initializeMachinePipelinerPass(PR);
}

HaydnTargetMachine::HaydnTargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     std::optional<Reloc::Model> RM,
                                     std::optional<CodeModel::Model> CM,
                                     CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT.computeDataLayout(), TT, CPU, FS, Options,
                               RM.value_or(Reloc::Static),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()) {
  initAsmInfo();
  setGlobalISel(true);
  setGlobalISelAbort(GlobalISelAbortMode::Enable); // abort=1, no SDAG fallback
  setSupportsDefaultOutlining(false);
  // Epilogue FrameDestroy CFI is emitted; enable generic CFIFixup so
  // shrink-wrapped / multi-exit paths restore CFA and CSR state.
  setCFIFixup(true);
}

const HaydnSubtarget *
HaydnTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute TuneAttr = F.getFnAttribute("tune-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string TuneCPU =
      TuneAttr.isValid() ? TuneAttr.getValueAsString().str() : CPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;

  std::string Key = CPU + TuneCPU + FS;
  auto &I = SubtargetMap[Key];
  if (!I) {
    I = std::make_unique<HaydnSubtarget>(TargetTriple, CPU, TuneCPU, FS, *this);
  }
  return I.get();
}

MachineFunctionInfo *HaydnTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return HaydnMachineFunctionInfo::create<HaydnMachineFunctionInfo>(Allocator, F,
                                                                     STI);
}

ScheduleDAGInstrs *
HaydnTargetMachine::createMachineScheduler(MachineSchedContext *C) const {
  // AIE2 dual-scheduler contract: always return an owned
  // pre-RA DAG. NEVER return nullptr — upstream treats null as GenericScheduler
  // (MachineScheduler.cpp createSchedLive), which caused silent O2 miscompiles
  // (yarpgen seed1 sticky) and 19 historical regressions.
  //
  // Pre-RA = pressure / order for RA (HaydnPreRASchedStrategy).
  // Post-RA = sole VLIW pack owner (createPostMachineScheduler).
  // Do not revive VLIWMachineScheduler / ConvergingVLIWScheduler.
  return createHaydnPreRAScheduler(C);
}

ScheduleDAGInstrs *
HaydnTargetMachine::createPostMachineScheduler(MachineSchedContext *C) const {
  // Post-RA pack owner: bundle formation in leaveRegion/leaveMBB
  // (HaydnScheduleDAGMI + HaydnPostRASchedStrategy + HaydnHazardRecognizer).
  // W68.1: multi-stage SMS is owned by the generic pre-RA MachinePipeliner;
  // the bespoke post-RA host is deleted — no post-RA SMS engine remains.
  // UAF inapplicable: never instantiates VLIWMachineScheduler.
  return createHaydnPostRAScheduler(C);
}

namespace {

//===----------------------------------------------------------------------===//
// Haydn codegen pass pipeline (execution order). Every Haydn pass has a
// DEBUG_TYPE for -debug-only= / -print-after= (assertions build). Full table +
// opt-level (O0 vs O1) notes + bisection recipe:
// contracts/pipeline.md.
//
// IR: AtomicExpand; HardwareLoops(O1) [haydn-tti]
// GISel: IRTranslator; PreLegalizerCombiner;
// Legalizer; PostLegalizerCombiner(O1); RegBankSelect;
// InstructionSelect; PostSelectOptimize(O1)
// Pre-RA: MachinePipeliner/SMS(O2); (AGU fuse is GISel-only)
// DeadMIElim after SMS; MachineScheduler/HaydnPreRASchedStrategy *
// register allocation (upstream)
// Post-RA: EnsureTerminators * (addPostRegAlloc, pre-PEI)
// addPreSched2 (AIE2TargetMachine.cpp:229-244):
// DeadMI (O1); MBP (O1) BEFORE HardwareLoops;
// HardwareLoops (O1, product default ON); ExpandPseudos *;
// PostMachineScheduler/HaydnPostRA pack * (sole pack, all levels);
// HaydnLatencyStalls * (Haydn overlay: RAW net; stall NOPs committed next);
// HaydnFinalizeBundle * + HaydnVerifyBundles * (AIE FinalizeBundle :243)
// Layout: addBlockPlacement empty (AIE2TargetMachine.cpp:250-253)
// Pre-emit: BranchRelaxation; FixupHwLoops(O1+hwloops) + second BR;
// mid Finalize+Verify after BR (bare-parcel recommit). S2/LateConvergence
// is at addPostBBSections under -haydn-sms2 (default off), after the
// common executable tail and before closure Finalize. insertIndirectBranch
// emits real LUI+ADDI32_W+JALR_W that must rejoin the mid/late lanes.
// AIE PreEmit empty :88 — AIE has no BR. Asm: AsmPrinter
// (O1) = opt-gated; * = load-bearing / legal encode. Deleted: PushPopOpt
// CommonGEP deleted. PreRALoadPromote deleted.
// CopyElim/ConditionOptimizer/BitSimplify/PEIPeephole/CFGOptimizer deleted.
//
// Pack ownership follows AIE2 (AIE2TargetMachine::addPreSched2):
// DeadMI → MBP (O1) → HardwareLoops → PseudoExpand → PostMachineScheduler
// → (Haydn overlay) LatencyStalls → Finalize+Verify. Suppress generic
// post-pack MBP via addBlockPlacement. PreEmit is BR (+ Fixup/BR when
// hwloops ON) then the same Finalize+Verify — not a second packer. S2 is
// addPostBBSections (AArch64TargetMachine.cpp:883-891 late writers after
// BB sections), not PreEmit.
//===----------------------------------------------------------------------===//
class HaydnPassConfig : public TargetPassConfig {
public:
  HaydnPassConfig(HaydnTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {
    // AIE2 dual-sched: pre-RA MachineScheduler stays in the pipeline;
    // createMachineScheduler never returns nullptr (no silent GenericScheduler).
  }

  HaydnTargetMachine &getHaydnTargetMachine() const {
    return getTM<HaydnTargetMachine>();
  }

  void addIRPasses() override;
  bool addPreISel() override;
  bool addIRTranslator() override;
  void addPreLegalizeMachineIR() override;
  bool addLegalizeMachineIR() override;
  void addPreRegBankSelect() override;
  bool addRegBankSelect() override;
  bool addGlobalInstructionSelect() override;
  bool addInstSelector() override;
  // SSA EarlyIfConversion (speculate + insertSelect → MOVT/MOVF).
  bool addILPOpts() override;
  void addPreRegAlloc() override;
  // EnsureTerminators before PEI so invented RET gets epilogue.
  void addPostRegAlloc() override;
  void addMachineLateOptimization() override;
  void addPreSched2() override;
  // AIE2: MBP runs in addPreSched2 before PostRA pack — suppress late MBP.
  void addBlockPlacement() override;
  void addPreEmitPass() override;
  // W68.2R: the late VLIW closure runs after every common executable
  // writer (outlining/splitting/BB-sections) and the terminal read-only
  // verifier is the freeze gate (contracts/pipeline.md required seats).
  void addPostBBSections() override;
  void addPreEmitPass2() override;

  // AIE peer: full CSE above O0; empty at O0 (fast RA).
  std::unique_ptr<CSEConfigBase> getCSEConfig() const override;
};

} // namespace

TargetTransformInfo
HaydnTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<HaydnTTIImpl>(this, F));
}

TargetPassConfig *HaydnTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new HaydnPassConfig(*this, PM);
}

void HaydnPassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());
  TargetPassConfig::addIRPasses();
}

bool HaydnPassConfig::addPreISel() {
  // IR-level hardware-loop recognition. The upstream HardwareLoops pass
  // (llvm/lib/CodeGen/HardwareLoops.cpp) queries
  // HaydnTargetTransformInfo::isHardwareLoopProfitable, which uses
  // ScalarEvolution to derive trip counts symbolically. This runs BEFORE
  // instruction selection, so the IV/limit/step are still clean SSA values
  // SCEV resolves runtime inits, runtime limits, and non-unit strides that
  // the post-RA recognizer cannot recover from physical registers after
  // spills. The pass inserts llvm.set.loop.iterations / llvm.loop.decrement
  // intrinsics, which the GlobalISel selector (Phase 3) lowers to
  // LoopStart / PseudoLoopEnd pseudos. Mirrors AIE's addPreISel
  // (AIE2TargetMachine.cpp:80-86). Runs at O1+ only.
  if (TM->getOptLevel() != CodeGenOptLevel::None && EnableHaydnHardwareLoops)
    addPass(createHardwareLoopsLegacyPass());
  return false;
}

bool HaydnPassConfig::addIRTranslator() {
  addPass(new IRTranslator(getOptLevel()));
  return false;
}

void HaydnPassConfig::addPreLegalizeMachineIR() {
  // Pre-legalizer combiner: simplifies generic G_* instructions before
  // type/action legalization. Handles trunc-of-anyext, redundant extensions
  // identity and/or, constant folding, shift-by-zero, and chained ptr_add.
  if (EnableHaydnPreLegalizerCombiner)
    addPass(createHaydnPreLegalizerCombiner());
  // CommonGEP deleted. PreLegalizer already folds chained G_PTR_ADD;
  // optional CSE is via standard CSE config, not a duplicate pass.
}

bool HaydnPassConfig::addLegalizeMachineIR() {
  // Legalizer only — PostLegalizerCombiner lives at addPreRegBankSelect
  // (AIE2 / standard GlobalISel boundary; Track C).
  addPass(new Legalizer());
  return false;
}

void HaydnPassConfig::addPreRegBankSelect() {
  // O1+: profitable post-legalize combines before register-bank selection.
  if (getOptLevel() != CodeGenOptLevel::None && EnableHaydnPostLegalizerCombiner)
    addPass(createHaydnPostLegalizerCombiner());
}

bool HaydnPassConfig::addRegBankSelect() {
  addPass(new RegBankSelect());
  return false;
}

bool HaydnPassConfig::addGlobalInstructionSelect() {
  addPass(new InstructionSelect(getOptLevel()));
  // Profitability (MAC fusion etc.): O1+ only (PL / 1e).
  if (getOptLevel() != CodeGenOptLevel::None && EnableHaydnPostSelectOptimize)
    addPass(createHaydnPostSelectOptimizePass());
  return false;
}

bool HaydnPassConfig::addInstSelector() {
  // GlobalISel-only backend: no SelectionDAG ISel
  return false;
}

bool HaydnPassConfig::addILPOpts() {
  // generic SSA EarlyIfConverter rewrites diamonds/triangles
  // via HaydnInstrInfo::insertSelect (MOVT32/MOVF32). Replaces GenMux
  // Pattern 2 (post-RA branch CMOV). Gated by Subtarget::enableEarlyIfConversion.
  // Ref: HexagonEarlyIfConv, AIE EarlyIfConverterLegacy, EarlyIfConversion.cpp.
  addPass(&EarlyIfConverterLegacyID);
  return true;
}

std::unique_ptr<CSEConfigBase> HaydnPassConfig::getCSEConfig() const {
  // AIE peer: no CSE at O0 (RegAllocFast); standard CSE above O0.
  // Legalizer must not raw-constrain CSE-tracked vregs (G_MUL widen path).
  if (TM->getOptLevel() == CodeGenOptLevel::None)
    return std::make_unique<CSEConfigBase>();
  return getStandardCSEConfigForOpt(TM->getOptLevel());
}

void HaydnPassConfig::addPostRegAlloc() {
  // invent soft RET on dead-end MBBs *before* PEI so epilogue/CSR
  // restore can attach. Post-PEI insert left returns without frame teardown.
  addPass(createHaydnEnsureTerminatorsPass());
}

void HaydnPassConfig::addMachineLateOptimization() {
  addPass(&MachineLateInstrsCleanupID);
  addPass(&BranchFolderPassID);
  if (!TM->requiresStructuredCFG())
    addPass(&TailDuplicateLegacyID);
  addPass(createMachineCopyPropagationPass(/*UseCopyInstr=*/true));
}

void HaydnPassConfig::addPreRegAlloc() {
  // AGU pre/post-inc form is GISel-only (HaydnPostLegalizerCombiner).

  // Software pipelining (Swing Modulo Scheduling) for VLIW DSP loops.
  // Runs on the naive countable loop (SEQ32/SLT32 + BNEZ/BEQZ), so the expander
  // never has to round-trip hwloop pseudos -- no expander-compatibility surface.
  // Hardware-loop formation happens POST-RA (addPreSched2 -> HaydnHardwareLoops)
  // after SMS. AIE2 order: MBP (O1) then HardwareLoops (still post-RA). The
  // pre-RA hwloop pass was removed: forming hwloops pre-RA corrupted
  // SET_HWLOOP_REG MBB operands (.LBB_-1) because later block-placement
  // renumbering moved or erased referenced blocks. SMS uses virtual registers
  // for renaming across pipeline stages, so it must run before RA.
  if (getOptLevel() >= CodeGenOptLevel::Default) {
    // PreRALoadPromote / promoteLoadsToSlot1 deleted (B3.exit.3): dual-load
    // packing is HR tryAddProduct PlacementAlternatives → setDesc members
    // (AIEHazardRecognizer.cpp:389; AIEMachineScheduler.cpp:1121-1132).
    addPass(&MachinePipelinerID);
    // AIE-faithful: remove unused debris after SWP (AIE2TargetMachine
    // addPreRegAlloc: MachinePipeliner → DeadMachineInstructionElim).
    addPass(&DeadMachineInstructionElimID);
  }
}

void HaydnPassConfig::addPreSched2() {
  // AIE2 order (AIE2TargetMachine::addPreSched2):
  // CopyElim/ConditionOptimizer/BitSimplify/PEIPeephole deleted.
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(&DeadMachineInstructionElimID);

  // DeadMIElim → MBP (O1) → HardwareLoops → PseudoExpand → PostMachineScheduler
  // → LatencyStalls → Finalize+Verify (AIE2TargetMachine.cpp:229-244; Haydn
  // overlay is LatencyStalls between pack and first commit).
  // EnsureTerminators already ran in addPostRegAlloc (pre-PEI).
  // CopyElim/ConditionOptimizer/BitSimplify/PEIPeephole stay deleted.
  // CFG simplification: generic BranchFolder (addMachineLateOptimization,
  // post-PEI) already performs empty-block forward, identical-successor fold,
  // dead-block elim, and tail merge. HaydnCFGOptimizer was a pure duplicate
  // — no Haydn-only VLIW CFG residue remained.
  if (getOptLevel() != CodeGenOptLevel::None) {
    // MBP BEFORE HardwareLoops (AIE2). Role A expand only (Role B deleted).
    addPass(&MachineBlockPlacementID);
    if (EnableHaydnHardwareLoops)
      addPass(createHaydnHardwareLoopsPass());
  }

  // Pseudo expansion BEFORE the post-RA scheduler so ALL MIs exist
  // before bundle formation (leaveRegion/leaveMBB). Mirrors AIE
  // (AIEPseudoBranchExpansion in addPreSched2; AIE PreEmit empty).
  // Load-bearing: needed for legal encode at all opt levels.
  //
  // Safety: addPreSched2 runs AFTER PEI (post-RA + post-PEI). ExpandPseudos
  // owns LOAD_ADDR / SETCBR / leftover *_POST_INC / leftover generic
  // SET_HWLOOP rewrite. Product SET is SET_HWLOOP_F2_W at HardwareLoops
  // (HaydnHardwareLoops.cpp:701). Post-call soft-zero R0 is
  // HaydnPostRAScratch; Expand calls it after leftover expand so real
  // JAL_W is visible. ADJCALLSTACK is PEI; calls are JAL_W from
  // CallLowering; va_arg/libcall are the legalizer. Always on — residual
  // pseudos are fatal at Verify/AsmPrinter.
  addPass(createHaydnExpandPseudosPass());

  // Sole Format E pack: leaveRegion/leaveMBB. Packetizer retired.
  // AIE2 always runs PostRA for bundle/NoOp correctness (incl. O0).
  // targetSchedulesPostRAScheduling skips the duplicate upstream slot.
  // CopyConstrain is pre-RA only (AIE CopyConstrain placement).
  // Generic PostMachineScheduler still quality-skips optnone (no reorder);
  // plain O0 without optnone still enters the post-RA pack path first and may
  // form multi-MI full-fill packs for independent ops.
  addPass(&PostMachineSchedulerID);
  // W68.2 S2: second invocation of the SAME scheduler implementation,
  // flag-gated. S1 (above) scheduled the function and recorded per-MI
  // issue cycles into the inter-block DDGs; S2 reschedules with those
  // depths feeding the effective-latency cut (successors are now
  // "scheduled" from S1's perspective). Same impl = no second scheduler.
  // W68.3 will seat the late range/layout mutations BETWEEN the two.
  // Exposed-pipeline RAW net between pack and first commit. AIE2 addPreSched2
  // is PostMachineScheduler then createAIEFinalizeBundle
  // (AIE2TargetMachine.cpp:242-244; AIE PreEmit empty at :88). Stall NOPs are
  // committed by the following Finalize+Verify. BranchRelaxation can still
  // emit bare LUI+ADDI32_W+JALR_W (insertIndirectBranch); addPreEmitPass
  // re-runs the same Finalize+Verify after BR so those parcels commit.
  addPass(createHaydnLatencyStallsPass());
  // After scheduling (or after an optnone skip), wrap remaining standalone
  // MIs as singleton BUNDLEs and stamp generated Format E members
  // (AIE2TargetMachine.cpp:242-244 createAIEFinalizeBundle;
  // AIEFinalizeBundle.cpp:40-59). Multi-MI already
  // stamped in HaydnPostRASchedStrategy::finalizeLegalMultiMI. Finalize and
  // Verify never call skipFunction: they are target-local no-reorder commit
  // ownership so product emission never sees uncommitted bare encode MIR.
  // Do not reopen skipFunction on Finalize/Verify.
  // Plain O0 (no optnone) keeps any multi-MI packs from postmisched; optnone
  // is no-reorder singleton commit only. Leave only committed Format-E cycles
  // for MC (underfill/top-pad invent stays fail-closed when golden is silent).
  addPass(createHaydnFinalizeBundlePass());
  // Fail-closed committed-cycle verifier immediately after finalize
  // (AIEBaseInstrInfo.cpp:1440-1459 verifyInstruction peer; AIE finalize
  // commit surface AIEHazardRecognizer.cpp:278-312 under test). Also refuses
  // optnone bare-encode escape and mixed committed+bare encode residual.
  addPass(createHaydnVerifyBundlesPass());
}

void HaydnPassConfig::addBlockPlacement() {
  // AIE2TargetMachine::addBlockPlacement: placement already done in addPreSched2.
}

void HaydnPassConfig::addPreEmitPass() {
  // AIE PreEmit is empty (AIE2TargetMachine.cpp:88;
  // AIEBaseTargetMachine.cpp:388) — AIE has no BranchRelaxation. Haydn
  // keeps BR after the first commit (Format E simm fields).
  // insertIndirectBranch emits real LUI+ADDI32_W+JALR_W
  // (HaydnInstrInfo.cpp); those are one-parcel real MIs. Re-run the same
  // Finalize+Verify after BR at every opt level so mixed committed+bare
  // never reaches the printer. Not a second commit implementation:
  // AIEFinalizeBundle.cpp:40-59 is identity on already-bundled roots.
  //
  // 1. BranchRelaxation — Format E simm fields
  // 2. HaydnFixupHwLoops (hwloops ON) — SET_HWLOOP Off1/Off2 ÷4; product
  //    demote-first (fatal if live demote fails). demote OFF = debug
  //    erase-setup only — not product.
  // 3. BranchRelaxation — re-close after Fixup growth (e.g. long BEQZ_W)
  // 4. Late Finalize+Verify after BR (product default and hwloops ON):
  //      materialize bare MIs via empty-cycle tryAdd → setDesc
  //        (AIEMachineScheduler.cpp:1121-1139; AIEHazardRecognizer.cpp:174-214;
  //         HaydnBundleMaterialize commitLateProductCycle)
  //      FinalizeBundle singleton wrap + generated member stamp
  //        (AIEFinalizeBundle.cpp:40-59; AIE2TargetMachine.cpp:242-244)
  //      fail-closed verifyCommittedBundle
  //        (AIEBaseInstrInfo.cpp:1440-1459; haydn-verify-bundles)
  // Do not move BR before pack (sizes wrong). No PostMachineScheduler here.
  addPass(&BranchRelaxationPassID);
  if (getOptLevel() != CodeGenOptLevel::None && EnableHaydnHardwareLoops) {
    addPass(createHaydnFixupHwLoopsPass());
    addPass(&BranchRelaxationPassID);
  }
  // Mid Finalize+Verify after BR at every opt level (same Finalize/Verify;
  // identity on already-bundled roots, recommit for BR bare parcels). S2/
  // LateConvergence is NOT here: it must choose current physical MIs after
  // the common executable tail. The W68.2R S2 + closure + freeze seats live
  // at addPostBBSections/addPreEmitPass2. Peer: AArch64 addPostBBSections
  // seats late BranchRelaxation after BB sections
  // (AArch64TargetMachine.cpp:883-891); AIE2 PreEmit is empty
  // (AIE2TargetMachine.cpp:90).
  addPass(createHaydnFinalizeBundlePass());
  addPass(createHaydnVerifyBundlesPass());
}

void HaydnPassConfig::addPostBBSections() {
  // W68.2R late VLIW closure owner (contracts/pipeline.md "Required
  // terminal multi-format lifecycle"): this seat runs AFTER every common
  // executable writer — RegUsageInfoCollector/IPRA, FuncletLayout,
  // RemoveLoadsIntoFakeUses, StackMapLiveness, LiveDebugValues, sanitizer
  // metadata, MachineOutliner, function/data splitting, and
  // BasicBlockSections all precede addPostBBSections. S2 (HaydnLateConvergence,
  // -haydn-sms2 default-off) chooses current physical MIs here, then the
  // one closure Finalize commits any bare MI those writers reintroduced
  // (outlined sequences rejoin committed state) and Verify fail-closes the
  // committed-cycle invariants. Optional CFIFixup (metadata/CFI only,
  // enabled by setCFIFixup(true) in the TM ctor) follows this seat via
  // the common TargetPassConfig tail; the terminal read-only verifier at
  // addPreEmitPass2 is the executable freeze.
  //
  // A -start-after/-stop-…-carved pipeline is a seat PROBE, not the
  // product pipeline: the closure must not commit state the probe
  // deliberately left bare (e96/vf5/mc printer fixtures predate the
  // freeze seat and probe serialization directly). Product runs are
  // never limited (llc/clang full pipelines), so this gate cannot mask
  // a real freeze.
  //
  // S2 still seats under the limited-pipeline carve-out so -stop-after=
  // haydn-late-convergence can observe the pass; Finalize/Verify remain
  // product-pipeline-only. Flag stays default-off (cl::init(false)).
  if (HaydnSMS2Enabled())
    addPass(createHaydnLateConvergencePass());
  if (TargetPassConfig::hasLimitedCodeGenPipeline())
    return;
  addPass(createHaydnFinalizeBundlePass());
  addPass(createHaydnVerifyBundlesPass());
}

void HaydnPassConfig::addPreEmitPass2() {
  // W68.2R executable freeze gate: terminal read-only VerifyBundles after
  // optional CFIFixup and frame-layout analysis. Only serialization
  // (AsmPrinter/MC) follows. Read-only by construction — VerifyBundles
  // never mutates MIR; a violation here is a hard diagnostic, never a
  // repair. A second Finalize is deliberately NOT seated here: closure
  // owned addPostBBSections; anything bare at this seat is a pipeline
  // contract violation the verifier reports.
  //
  // Same limited-pipeline probe carve-out as addPostBBSections: the
  // freeze is a property of the COMPLETE pipeline only.
  if (TargetPassConfig::hasLimitedCodeGenPipeline())
    return;
  addPass(createHaydnVerifyBundlesPass());
}
