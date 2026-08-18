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
// FULL FATE (2026-07-23): invent densify deleted permanently — not default-OFF
// quarantine. FATED: LoadStoreOpt, CircularBuffer stats, RedundantCopyElim,
// FormUpdateAddr, PostPipeliner Stage-0, InterBlock Stage-0, formMACs, Role B
// convert. Sole AGU form = GISel. contracts/pipeline.md.
// (2026-07-27): HaydnCFGOptimizer deleted. Post-PEI BranchFolder +
// MachineBlockPlacement already cover empty-forward / identical-succ /
// unreachable / tail-merge; ON/OFF asm identity across Haydn lit kernels with
// no unique VLIW residue. Do not revive a second generic CFG folder.
// CopyElim/ConditionOptimizer deleted; MCP(UseCopyInstr) replaces them.
// Hardware-loop product default. AIE inserts HardwareLoops unconditionally
// at O1+ (AIE2TargetMachine.cpp:81-82). Hexagon defaults ON via
// DisableHardwareLoops (HexagonTargetMachine.cpp:48-49). Haydn stays OFF
// until independent SMS QUALIFY (hwloops OFF, parcels==II), independent
// SCEV-proven hwloop QUALIFY (SMS OFF), then the combined
// trip/CFG/prologue/kernel/epilogue matrix, then a separate policy-only
// flip of hardwareLoopsProductDefaultEnabled(). This wave does not flip.
// Force-ON is CLI only. SCEV-proven IR + retained-state expansion only;
// late physical semantic rediscovery is deleted. Never revive pre-RA
// multi-member SMS BUNDLE or force-coissue.
// Multi-stage product default is not this flag; it lives on
// HaydnMultiStageSMS::productDefaultEnabled() and is not flipped here.
static_assert(!HaydnTargetMachine::hardwareLoopsProductDefaultEnabled(),
              "hardware-loop product default stays OFF until independent "
              "then combined qualification and a separate policy-only flip");
static cl::opt<bool> EnableHaydnHardwareLoops(
    "haydn-enable-hwloops",
    cl::init(HaydnTargetMachine::hardwareLoopsProductDefaultEnabled()),
    cl::Hidden,
    cl::desc("Enable the SCEV-proven Haydn hardware-loop path; late physical "
             "semantic rediscovery is deleted. Product default follows "
             "HaydnTargetMachine::hardwareLoopsProductDefaultEnabled(); remains "
             "OFF until independent then combined qualification and a "
             "separate policy-only flip."));
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
  // Multi-stage SMS (HaydnPostRAMultiStage / HaydnMultiStageSMS) hooks inside
  // HaydnScheduleDAGMI::schedule after ordinary convergence; product default OFF
  // (-haydn-enable-multistage-sms). That default is not flipped here.
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
// HardwareLoops (O1, product default OFF); ExpandPseudos *;
// PostMachineScheduler/HaydnPostRA pack * (sole pack, all levels);
// HaydnLatencyStalls * (Haydn overlay: RAW net; stall NOPs committed next);
// HaydnFinalizeBundle * + HaydnVerifyBundles * (AIE FinalizeBundle :243)
// Layout: addBlockPlacement empty (AIE2TargetMachine.cpp:250-253)
// Pre-emit: BranchRelaxation; FixupHwLoops(O1+hwloops) + second BR;
// late Finalize+Verify after BR at every opt level (same Finalize/Verify;
// AIE PreEmit empty :88 — AIE has no BR). insertIndirectBranch emits
// real LUI+ADDI32_W+JALR_W that must rejoin that late lane.
// Asm: AsmPrinter
// (O1) = opt-gated; * = load-bearing / legal encode. Deleted: PushPopOpt
// CommonGEP deleted. PreRALoadPromote deleted.
// CopyElim/ConditionOptimizer/BitSimplify/PEIPeephole/CFGOptimizer deleted.
//
// Pack ownership follows AIE2 (AIE2TargetMachine::addPreSched2):
// DeadMI → MBP (O1) → HardwareLoops → PseudoExpand → PostMachineScheduler
// → (Haydn overlay) LatencyStalls → Finalize+Verify. Suppress generic
// post-pack MBP via addBlockPlacement. PreEmit is BR (+ Fixup/BR when
// hwloops ON) then the same Finalize+Verify — not a second packer.
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
  // Exposed-pipeline RAW net between pack and first commit. AIE2 addPreSched2
  // is PostMachineScheduler then createAIEFinalizeBundle
  // (AIE2TargetMachine.cpp:242-244; AIE PreEmit empty at :88). Stall NOPs are
  // committed by the following Finalize+Verify. BranchRelaxation can still
  // emit bare LUI+ADDI32_W+JALR_W (insertIndirectBranch); addPreEmitPass
  // re-runs the same Finalize+Verify after BR so those parcels commit.
  addPass(createHaydnLatencyStallsPass());
  // After scheduling (or after an optnone skip), wrap remaining standalone
  // MIs as singleton BUNDLEs with FormatID imm (AIE2TargetMachine.cpp:242-244
  // createAIEFinalizeBundle; AIEFinalizeBundle.cpp:40-59). Multi-MI already
  // stamped in HaydnPostRASchedStrategy::finalizeLegalMultiMI. Finalize and
  // Verify never call skipFunction: they are target-local no-reorder commit
  // ownership so product emission never sees uncommitted bare encode MIR.
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
  //      FinalizeBundle singleton wrap + FormatID
  //        (AIEFinalizeBundle.cpp:40-59; AIE2TargetMachine.cpp:242-244)
  //      fail-closed verifyCommittedBundle
  //        (AIEBaseInstrInfo.cpp:1440-1459; haydn-verify-bundles)
  // Do not move BR before pack (sizes wrong). No PostMachineScheduler here.
  addPass(&BranchRelaxationPassID);
  if (getOptLevel() != CodeGenOptLevel::None && EnableHaydnHardwareLoops) {
    addPass(createHaydnFixupHwLoopsPass());
    addPass(&BranchRelaxationPassID);
  }
  // Late Finalize+Verify after BR at every opt level (same Finalize/Verify).
  addPass(createHaydnFinalizeBundlePass());
  addPass(createHaydnVerifyBundlesPass());
}
