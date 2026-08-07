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
#include "HaydnBitSimplify.h"
#include "HaydnConditionOptimizer.h"
#include "HaydnCopyElim.h"
#include "HaydnExpandPseudos.h"
#include "HaydnExpandPostIncEarly.h"
#include "HaydnEnsureTerminators.h"
#include "HaydnFinalizeBundle.h"
#include "HaydnVerifyBundles.h"
#include "HaydnPEIPeephole.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnMachineScheduler.h"
#include "HaydnPostRASchedStrategy.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetTransformInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
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
// WP2 — Bundled two-address rewrite (SMS hard-root RA survival)
//===----------------------------------------------------------------------===//
//
// TwoAddressInstruction only walks bundle-level MBB iterators, so it never
// sees tied def/use pairs on *children* of multi-member BUNDLE roots. SMS
// handoff freezes legal product cycles as logical hard roots before
// TwoAddress; children may still carry non-identical tied operands (e.g.
// F2MULAA accumulator). After TwoAddress sets TiedOpsRewritten, the machine
// verifier requires identity on every tied pair — including bundled children.
//
// Placement: AFTER PHIElimination and BEFORE TwoAddress. insertPass after
// TwoAddress is too late under -verify-machineinstrs (the verifier runs
// immediately after TwoAddress, before any insertPass followers). Pre-fixing
// bundled ties here means TwoAddress sees already-identical child ties and
// only has to rewrite bare MIs; post-TwoAddress verify is clean.
//
// Rewrite shape matches TwoAddress for bare MIs: prepend
//   dst = COPY src
// before the hard root, then set the tied use to dst. RegisterCoalescer folds
// the COPY. Architectural dual-load / rematch roots without ties are no-ops.
// Spill/reload around the hard root is ordinary RA; isSchedulingBoundary keeps
// membership intact through machine-scheduler → coalescer → greedy → VRW.
//===----------------------------------------------------------------------===//

namespace {

bool rewriteBundledTiedTwoAddressOps(MachineFunction &MF) {
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    // Snapshot roots so COPY insertion before a root does not disturb the
    // bundle-level walk.
    SmallVector<MachineInstr *, 8> Roots;
    for (MachineInstr &MI : MBB) {
      if (MI.isBundle() && MI.getBundleSize() >= 2)
        Roots.push_back(&MI);
    }

    for (MachineInstr *Root : Roots) {
      for (MachineBasicBlock::instr_iterator II =
               std::next(Root->getIterator());
           II != MBB.instr_end() && II->isBundledWithPred(); ++II) {
        MachineInstr &Child = *II;
        for (unsigned SrcIdx = 0, E = Child.getNumOperands(); SrcIdx != E;
             ++SrcIdx) {
          unsigned DstIdx = 0;
          if (!Child.isRegTiedToDefOperand(SrcIdx, &DstIdx))
            continue;
          MachineOperand &SrcMO = Child.getOperand(SrcIdx);
          MachineOperand &DstMO = Child.getOperand(DstIdx);
          if (!SrcMO.isReg() || !DstMO.isReg())
            continue;
          Register SrcReg = SrcMO.getReg();
          Register DstReg = DstMO.getReg();
          if (!SrcReg || !DstReg || SrcReg == DstReg)
            continue;

          // TwoAddress trivial path: undef tied use rewrites in place.
          if (SrcMO.isUndef() && !DstMO.getSubReg()) {
            if (DstReg.isVirtual() && SrcReg.isVirtual())
              MRI.constrainRegClass(DstReg, MRI.getRegClass(SrcReg));
            SrcMO.setReg(DstReg);
            SrcMO.setSubReg(0);
            Changed = true;
            continue;
          }

          // Pre-RA SMS hard roots use virtual registers only.
          if (!SrcReg.isVirtual() || !DstReg.isVirtual())
            continue;

          unsigned SubRegB = SrcMO.getSubReg();
          const TargetRegisterClass *RC = MRI.getRegClass(SrcReg);
          MRI.constrainRegClass(DstReg, RC);
          BuildMI(MBB, *Root, Child.getDebugLoc(), TII.get(TargetOpcode::COPY),
                  DstReg)
              .addReg(SrcReg, 0, SubRegB);
          SrcMO.setReg(DstReg);
          SrcMO.setSubReg(0);
          if (SrcMO.isKill())
            SrcMO.setIsKill(false);
          Changed = true;
        }
      }
    }
  }
  return Changed;
}

class HaydnBundledTwoAddressRewrite : public MachineFunctionPass {
public:
  static char ID;
  HaydnBundledTwoAddressRewrite();

  StringRef getPassName() const override {
    return "Haydn bundled two-address rewrite (SMS hard-root RA survival)";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    // Preserve LiveVariables so the common no-op path (no multi-member hard
    // roots) does not drop LV before TwoAddress — that would perturb RA for
    // unrelated kernels. When this pass inserts COPYs it does not update LV
    // kill sets; TwoAddress still rewrites correctly without relying on them.
    AU.setPreservesCFG();
    AU.addPreservedID(LiveVariablesID);
    AU.addPreservedID(MachineLoopInfoID);
    AU.addPreservedID(MachineDominatorsID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    return rewriteBundledTiedTwoAddressOps(MF);
  }
};

} // end anonymous namespace

char HaydnBundledTwoAddressRewrite::ID = 0;

// INITIALIZE_PASS expands initialize* in ::llvm; declare before defining.
namespace llvm {
void initializeHaydnBundledTwoAddressRewritePass(PassRegistry &);
} // namespace llvm

INITIALIZE_PASS(HaydnBundledTwoAddressRewrite, "haydn-bundled-twoaddr-rewrite",
                "Haydn bundled two-address rewrite for SMS hard roots", false,
                false)

HaydnBundledTwoAddressRewrite::HaydnBundledTwoAddressRewrite()
    : MachineFunctionPass(ID) {
  initializeHaydnBundledTwoAddressRewritePass(*PassRegistry::getPassRegistry());
}

namespace {
// Pass ID for insertPass (same TU as the class; anon-ns ID is link-local).
char &HaydnBundledTwoAddressRewriteID = HaydnBundledTwoAddressRewrite::ID;
} // namespace

//===----------------------------------------------------------------------===//
// Per-pass enable flags (pass bisection). Default ON; disable with
// `-mllvm -haydn-enable-<name>=0` (llc) / `-mllvm -haydn-enable-<name>=0` (clang).
// Pipeline-gated (conditional addPass) — the AArch64/AMDGPU/Hexagon idiom.
// Load-bearing passes (marked *) warn in desc; disabling them yields invalid
// asm. See ~/haydn-pass-pipeline.md for the full table.
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
// Post-inc: form = GISel only; expand residual = ExpandPostIncEarly (ON).
static cl::opt<bool> EnableHaydnExpandPostIncEarly(
    "haydn-enable-expand-post-inc-early", cl::init(true), cl::Hidden,
    cl::desc("Enable HaydnExpandPostIncEarly (residual *_POST_INC → LD/ST + "
             "ADDI pre-pack). Default ON. Form is GISel-only."));
// FULL FATE (2026-07-23): invent densify deleted permanently — not default-OFF
// quarantine. FATED: LoadStoreOpt, CircularBuffer stats, RedundantCopyElim,
// FormUpdateAddr, PostPipeliner Stage-0, InterBlock Stage-0, formMACs, Role B
// convert. Sole AGU form = GISel. contracts/pipeline.md §6.
// (2026-07-27): HaydnCFGOptimizer deleted. Post-PEI BranchFolder +
// MachineBlockPlacement already cover empty-forward / identical-succ /
// unreachable / tail-merge; ON/OFF asm identity across Haydn lit kernels with
// no unique VLIW residue. Do not revive a second generic CFG folder.
static cl::opt<bool> EnableHaydnConditionOptimizer(
    "haydn-enable-cond-opt", cl::init(true), cl::Hidden,
    cl::desc("Enable HaydnConditionOptimizer (compare simplification)."));
static cl::opt<bool> EnableHaydnCopyElim(
    "haydn-enable-copy-elim", cl::init(true), cl::Hidden,
    cl::desc("Enable HaydnCopyElim (identity/dead/R0 copies)."));
static cl::opt<bool> EnableHaydnHardwareLoops(
    "haydn-enable-hwloops", cl::init(false), cl::Hidden,
    cl::desc("Enable HaydnHardwareLoops (Role A expand; Role B deleted). "
             "Default off for Format E product: SET_HWLOOP_F2 multi-slot encode "
             "still mis-packs (memcpy body runs once). Re-enable after E96 "
             "HWLRIIR parcel + solo placement are green."));
static cl::opt<bool> EnableHaydnPostRASched(
    "haydn-enable-post-ra-sched", cl::init(true), cl::Hidden,
    cl::desc("Enable post-RA VLIW scheduler (bundle formation). LOAD-BEARING: "
             "disabling yields unbundled/invalid asm; for bisection only."));
static cl::opt<bool> EnableHaydnExpandPseudos(
    "haydn-enable-expand-pseudos", cl::init(true), cl::Hidden,
    cl::desc("Enable HaydnExpandPseudos (expand remaining pseudos, incl. in "
             "bundles). LOAD-BEARING: disabling yields invalid asm; bisection only."));
static cl::opt<bool> EnableHaydnPEIPeephole(
    "haydn-enable-pei-peephole", cl::init(true), cl::Hidden,
    cl::desc("Enable HaydnPEIPeephole (dead ZERO_GPR/FP-setup/prologue waste)."));
// HaydnPushPopOpt deleted (default-off zombie with ABI/SP/CFI bugs).
// Not re-enabled under FrameLowering; PEIPeephole remains for prologue waste.
static cl::opt<bool> EnableHaydnBitSimplify(
    "haydn-enable-bit-simplify", cl::init(true), cl::Hidden,
    cl::desc("Enable HaydnBitSimplify (identity masks, AND+OR pairs, XOR fold)."));

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
  initializeHaydnExpandPostIncEarlyPass(PR);
  initializeHaydnConditionOptimizerPass(PR);
  initializeHaydnCopyElimPass(PR);
  initializeHaydnPEIPeepholePass(PR);
  initializeHaydnEnsureTerminatorsPass(PR);
  initializeHaydnBitSimplifyPass(PR);
  initializeHaydnFinalizeBundlePass(PR);
  initializeHaydnVerifyBundlesPass(PR);
  initializeHaydnHandoffBundleRootDefsPass(PR);
  initializeHaydnBundledTwoAddressRewritePass(PR);
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
 // Do not revive VLIWMachineScheduler / ConvergingVLIWScheduler ( UAF).
  return createHaydnPreRAScheduler(C);
}

ScheduleDAGInstrs *
HaydnTargetMachine::createPostMachineScheduler(MachineSchedContext *C) const {
  // Stream B Phase B2: bundle formation in leaveRegion/leaveMBB
  // (HaydnScheduleDAGMI + HaydnPostRASchedStrategy + HaydnHazardRecognizer).
  // UAF inapplicable: never instantiates VLIWMachineScheduler.
  return createHaydnPostRAScheduler(C);
}

namespace {

//===----------------------------------------------------------------------===//
// Haydn codegen pass pipeline (execution order). Every Haydn pass has a
// DEBUG_TYPE for -debug-only= / -print-after= (assertions build). Full table +
// opt-level (O0 vs O1) notes + bisection recipe:
// ~/haydn-pass-pipeline.md.
//
// IR: AtomicExpand; HardwareLoops(O1) [haydn-tti]
// GISel: IRTranslator; PreLegalizerCombiner;
// Legalizer; PostLegalizerCombiner(O1); RegBankSelect;
// InstructionSelect; PostSelectOptimize(O1)
// Pre-RA: MachinePipeliner/SMS(O2); (AGU fuse is GISel-only)
// DeadMIElim after SMS; MachineScheduler/HaydnPreRASchedStrategy *
// register allocation (upstream)
// Post-RA (addPreSched2, AIE2-aligned):
// EnsureTerminators *; ExpandPostIncEarly * (product post-inc);
// cond/copy peeps (O1); MBP (O1) BEFORE HardwareLoops;
// (CFGOptimizer deleted — rely on BranchFolder late opt)
// HardwareLoops (O1); ExpandPseudos *; BitSimplify/PEIPeephole (O1);
// PostMachineScheduler/HaydnPostRA pack * (sole pack, all levels);
// HaydnFinalizeBundle * (singleton → BUNDLE + FormatID; AIE FinalizeBundle)
// Layout: addBlockPlacement empty (AIE2: placement already in PreSched2)
// Pre-emit: BranchRelaxation; FixupHwLoops(O1); BranchRelaxation
// (Haydn-specific range/hwloop — AIE PreEmit is empty)
// Asm: AsmPrinter
// (O1) = opt-gated; * = load-bearing / legal encode. Deleted: PushPopOpt
// CommonGEP deleted. PreRALoadPromote deleted.
//
// Pack ownership follows AIE2 (AIE2TargetMachine::addPreSched2):
// DeadMI → MBP (O1) → HardwareLoops → PseudoExpand → PostMachineScheduler
// Suppress generic post-pack MBP via addBlockPlacement override. PreEmit never
// re-packs.
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
  // After TwoAddress: restore BUNDLE root vreg defs for SMS handoff roots.
  void addOptimizedRegAlloc() override;
  // EnsureTerminators before PEI so invented RET gets epilogue.
  void addPostRegAlloc() override;
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

void HaydnPassConfig::addPreRegAlloc() {
  // AGU pre/post-inc form is GISel-only (HaydnPostLegalizerCombiner).

  // Software pipelining (Swing Modulo Scheduling) for VLIW DSP loops.
  // Runs on the naive countable loop (SEQ32/SLT32 + BNEZ/BEQZ), so the expander
  // never has to round-trip hwloop pseudos -- no expander-compatibility surface.
  // Hardware-loop formation happens POST-RA (addPreSched2 -> HaydnHardwareLoops)
 // after SMS. AIE2 order: MBP (O1) then HardwareLoops (still post-RA). The
  // pre-RA hwloop pass was removed : forming hwloops pre-RA corrupted
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

void HaydnPassConfig::addOptimizedRegAlloc() {
  // WP2 SMS hard-root RA survival (G-SMS-PRE-RA-HEXAGON):
  //
  // Pipeline after pre-RA MachinePipeliner handoff materialize:
  //   IsSSA passes see child-only vreg defs on BUNDLE roots (no dual-def).
  //   PHIElimination leaves SSA.
  //   → haydn-bundled-twoaddr-rewrite (before TwoAddress): COPY+identity for
  //     residual tied children inside multi-member hard roots. Must run
  //     *before* TwoAddress under -verify-machineinstrs — the verifier fires
  //     immediately after TwoAddress (TiedOpsRewritten) and would abort before
  //     any insertPass-after-TwoAddress follower. Spill/copy recovery surface;
  //     coalescer folds the COPY.
  //   TwoAddress rewrites bare ties and sets TiedOpsRewritten.
  //   → haydn-handoff-bundle-root-defs: re-attach child defs on multi-member
  //     roots so LIS/coalescer see the architectural dual-def surface.
  //   RegisterCoalescer → MachineScheduler (isSchedulingBoundary fence) →
  //   greedy → VirtRegRewriter → postmisched exact-commit inside roots.
  insertPass(&PHIEliminationID, &HaydnBundledTwoAddressRewriteID);
  insertPass(&TwoAddressInstructionPassID, &HaydnHandoffBundleRootDefsID);
  TargetPassConfig::addOptimizedRegAlloc();
}

void HaydnPassConfig::addPreSched2() {
  // AIE2 order (AIE2TargetMachine::addPreSched2):
 // DeadMIElim → MBP (O1) → HardwareLoops → PseudoExpand → PostMachineScheduler
  // EnsureTerminators already ran in addPostRegAlloc (pre-PEI).
  // O1+ peeps, then MBP → HardwareLoops → ExpandPseudos → PostRA pack.

  // Post-inc residual expand (form is GISel-only). All opt levels.
  if (EnableHaydnExpandPostIncEarly)
    addPass(createHaydnExpandPostIncEarlyPass());

  // Profitability peeps: O1+ only. Not required for legal encode.
  // CFG simplification: generic BranchFolder (addMachineLateOptimization,
  // post-PEI) already performs empty-block forward, identical-successor fold,
  // dead-block elim, and tail merge. HaydnCFGOptimizer was a pure duplicate
 // ( delete) — no Haydn-only VLIW CFG residue remained.
  if (getOptLevel() != CodeGenOptLevel::None) {
    if (EnableHaydnConditionOptimizer)
      addPass(createHaydnConditionOptimizerPass());
    if (EnableHaydnCopyElim)
      addPass(createHaydnCopyElimPass());

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
  // Safety: addPreSched2 runs AFTER PEI (post-RA + post-PEI) so
  // MFI.isCalleeSavedInfoValid and hasVarSizedObjects are final; ABI
  // physical regs exist. ExpandPseudos moves args to those physical regs.
  if (EnableHaydnExpandPseudos)
    addPass(createHaydnExpandPseudosPass());

  // BitSimplify / PEIPeephole: profitability peeps, O1+ only (PL / 1e).
  if (getOptLevel() != CodeGenOptLevel::None) {
    if (EnableHaydnBitSimplify)
      addPass(createHaydnBitSimplifyPass());

    // PEIPeephole: post-PEI, pre-pack (AIE keeps PreEmit empty of MI rewrites).
    if (EnableHaydnPEIPeephole)
      addPass(createHaydnPEIPeepholePass());
  }

  // Sole Format E pack: leaveRegion/leaveMBB. Packetizer retired.
  // AIE2 always runs PostRA for bundle/NoOp correctness (incl. O0).
  // targetSchedulesPostRAScheduling skips the duplicate upstream slot.
  // CopyConstrain is pre-RA only (AIE CopyConstrain placement).
  if (EnableHaydnPostRASched)
    addPass(&PostMachineSchedulerID);
  // After scheduling, wrap remaining standalone MIs as singleton BUNDLEs
  // with FormatID imm (AIE2TargetMachine.cpp:242-244 createAIEFinalizeBundle;
  // AIEFinalizeBundle.cpp:40-59). Multi-MI already stamped in
 // HaydnPostRASchedStrategy::finalizeLegalMultiMI.
  if (EnableHaydnPostRASched)
    addPass(createHaydnFinalizeBundlePass());
 // : fail-closed committed-cycle verifier immediately after finalize
  // (AIEBaseInstrInfo.cpp:1440-1459 verifyInstruction peer; AIE finalize
  // commit surface AIEHazardRecognizer.cpp:278-312 under test).
  if (EnableHaydnPostRASched)
    addPass(createHaydnVerifyBundlesPass());
}

void HaydnPassConfig::addBlockPlacement() {
  // AIE2TargetMachine::addBlockPlacement: placement already done in addPreSched2.
}

void HaydnPassConfig::addPreEmitPass() {
  // AIE PreEmit is empty (AIE2TargetMachine.cpp:88;
  // AIEBaseTargetMachine.cpp:388) — setDesc+finalize never need a second
  // pass. Haydn needs Format E branch range + hwloop Off fixups after pack
  // (size model). Pattern matches Hexagon: relax then target fixup that can
  // grow layout, then relax again.
  // 1. BranchRelaxation — Format E simm fields
  // 2. HaydnFixupHwLoops — SET_HWLOOP Off1/Off2 ÷4; product demote-first
  //    (LoopDec+LoopJNZ when free counter; fatal if live demote fails).
  //    demote OFF = debug erase-setup only — not product.
  // 3. BranchRelaxation — re-close after Fixup growth (e.g. long BEQZ_W)
 // 4. late layout firewall: re-apply AIE commit surfaces after allowed
  //    late growth (no 2nd packer / no silent reshape / no MCFlags):
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
  // Late re-commit only when pack path produced committed cycles (same gate
  // as post-RA Finalize/Verify in addPreSched2).
  if (EnableHaydnPostRASched) {
    addPass(createHaydnFinalizeBundlePass());
    addPass(createHaydnVerifyBundlesPass());
  }
}
