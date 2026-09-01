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
#include "HaydnMachineAlignment.h"
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
#include "llvm/Target/TargetOptions.h"
#include <fcntl.h>
#include <optional>
#include <unistd.h>

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
// W68.2R: S2/LateConvergence driver flag. Product default ON since the
// G004 flip 2026-08-27 (entry-qualified relocations a7473774858a + read-old
// dissolve order 554957730440 closed the two defect classes behind the
// original keep-off; same-artifact matrix 659/659 sms2-ON, torture
// 1479/1479, CM -3.76% bundles, DH -0.86%). The three -haydn-postra-*
// edge mutations stay default OFF: with them ON the QoR bisect
// (2026-08-27, CM/DHRY ON-vs-OFF) is super-additively bundle-regressive
// (CM +33.38%, DH +12.18%); see HaydnSchedMutations.h. When on, the same
// post-RA scheduler implementation runs at addPostBBSections after the
// common executable tail (outliner/split/BB sections) and before the
// closure Finalize. S1 commits reopenable provisional BUNDLEs (the first
// S2 invocation canonicalizes them back to logicals under the direct-shape
// and CB-167 read-old order gates). HC#0 walker hooks
// stay declined; this flag does not port them.
static constexpr bool haydnLateConvergenceProductDefaultEnabled() {
  // G004 flip 2026-08-27 (@ 5549577): sms2-only arm of the same-artifact
  // matrix green — 659-suite 659/659, torture -O2 1479/1479, CoreMark
  // PASS -3.76% bundles, Dhrystone PASS -0.86%, default lit+MC+lld
  // 1071/0F, HaydnTests 599/599, BundleSim full 687/687. The
  // postra-edges=ON arms were measured super-additively regressive and
  // are NOT part of this default.
  return true;
}
static cl::opt<bool> EnableHaydnSMS2(
    "haydn-sms2", cl::Hidden,
    cl::init(haydnLateConvergenceProductDefaultEnabled()),
    cl::desc("W68.2R: invoke the post-RA scheduler a second time (S2; "
             "addPostBBSections after the common executable tail). "
             "Product default ON (G004 flip 2026-08-27)."));

bool llvm::haydnSMS2Enabled() { return EnableHaydnSMS2; }

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
  initializeHaydnMachineAlignmentPass(PR);
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
// GR2.3: fail-closed forced-enable rejection for unsupported executable
// common-tail writers (MachineOutliner, MachineFunctionSplitter,
// BasicBlockSections). One predicate, one owner (the Haydn addPreEmitPass
// override), one mechanism: read-only TargetMachine state + read-only
// cl::getRegisteredOptions() introspection of the two llc-only *hidden
// static* flags (-enable-machine-outliner, -enable-split-machine-functions)
// that generic code owns and no TargetMachine bit carries.
//
// Why the registered-option lookup is needed at all: every clang/C-API arm
// arrives as a TM option bit (Options.EnableMachineOutliner,
// Options.EnableMachineFunctionSplitter, getBBSectionsType()) and is read
// directly, but the llc-only hidden static cl::opts in TargetPassConfig.cpp
// are file-static in generic CodeGen. Exposing them would be a generic-code
// edit (forbidden by hard constraint #1); cl::getRegisteredOptions() +
// getNumOccurrences() + printOptionValue() is the public, read-only
// introspection surface, so this stays target-owned.
//
// Why the PassConfig seat and not the TM constructor: TargetOptions are
// final at TM creation, but tools speculatively create targets; the
// PassConfig seat is where all pipeline law already lives (peer idiom:
// every other Haydn pipeline invariant), and it fires strictly before the
// common-tail writer block (see addPreEmitPass comment for the ordering
// proof), so rejection precedes any writer pass construction.
//
// Deliberate exclusions (scope boundary is itself law):
//  - static-data splitting (-split-static-data / partition-static-data
//    sections): data-section-only, cannot change executable layout
//    (contracts/pipeline.md common tail).
//  - -basic-block-address-map alone: metadata-only, contract-admitted.
//  - disable/no-op spellings (-enable-machine-outliner=never,
//    -basic-block-sections=none, =false values): admitted, so shared
//    multi-triple command lines that globally pass a disable spelling keep
//    working on Haydn. This seat rejects ENABLE requests only.
//
// -run-pass/-start-after/-stop-after carve-outs never invoke
// addMachinePasses, hence never this seat — the same carve-out shape the
// D1.13 freeze seat documents. Product pipelines are never limited, and a
// carve cannot contain these writers anyway (they are only ever added by
// the full common tail), so the carve-out cannot mask this wall.
//
// Residual: a future Haydn new-PM CodeGenPassBuilder entry would need the
// same predicate at its addPreEmitPass hook; every reachable Haydn pipeline
// today (llc legacy, clang legacy addPassesToEmitFile, C API) is guarded.
//===----------------------------------------------------------------------===//
static void haydnRejectUnsupportedCommonTailWriters(const HaydnTargetMachine &TM) {
  // Stable diagnostic family; GenCrashDiag=false so the process exits 1
  // with the "LLVM ERROR:" prefix (plain `not llc` arm) rather than
  // aborting — the HaydnLateConvergence/VerifyBundles diagnostic idiom.
  auto Reject = [](const char *What) {
    report_fatal_error(Twine("Haydn: unsupported forced common-tail writer: ") +
                           What,
                       /*GenCrashDiag=*/false);
  };

  // (1) BasicBlockSections: the TM bit covers every enable spelling
  // (-basic-block-sections=... via llc CommandFlags, clang
  // -fbasic-block-sections, C API). None = not requested; admitted.
  if (TM.getBBSectionsType() != BasicBlockSection::None)
    Reject("basic-block-sections (-basic-block-sections=/-fbasic-block-sections) "
           "is not qualified for the one-commit Format E packet lifecycle "
           "(contracts/pipeline.md common tail; GR2.3)");

  // The llc-only hidden static flags are file-static in generic CodeGen;
  // cl::getRegisteredOptions() is the public read-only way to reach them.
  // getNumOccurrences() is the occurred gate (an untouched option prints
  // its default and must never reject); printOptionValue() classifies the
  // requested value. Pinned printed forms (gr23 lit test):
  //   RunOutliner enum : "= always" | "= never" | "= optimistic-pgo" |
  //                      "= conservative-pgo" | "= *unknown option value*"
  //                      (the last is the untouched default; a bare
  //                      ValueOptional occurrence prints "always")
  //   bool flag        : "= 1" | "= 0"
  // printOptionValue writes to llvm::outs() (Support offers no stream
  // parameter and no public buffer swap), so the value is captured by
  // redirecting fd 1 to a scratch file for the call only. This is the same
  // sink -print-all-options uses; the redirect is local, restored on every
  // path, and never spans user-visible output (outs() is flushed around
  // the swap).
  auto PrintedFlagValue = [](StringRef Name, SmallVectorImpl<char> &Out) {
    auto It = cl::getRegisteredOptions().find(Name);
    if (It == cl::getRegisteredOptions().end())
      return false;
    cl::Option *O = It->second;
    if (O->getNumOccurrences() == 0)
      return false;
    llvm::outs().flush();
    fflush(stdout);
    int SavedFD = dup(STDOUT_FILENO);
    if (SavedFD < 0)
      return false;
    char Scr[] = "/tmp/haydn-gr23-optval-XXXXXX";
    int ScrFD = mkstemp(Scr);
    if (ScrFD < 0) {
      close(SavedFD);
      return false;
    }
    dup2(ScrFD, STDOUT_FILENO);
    O->printOptionValue(O->getOptionWidth(), /*Force=*/true);
    llvm::outs().flush();
    fflush(stdout);
    dup2(SavedFD, STDOUT_FILENO);
    close(SavedFD);
    off_t Len = lseek(ScrFD, 0, SEEK_CUR);
    if (Len > 0) {
      lseek(ScrFD, 0, SEEK_SET);
      Out.resize(Len);
      ssize_t Read = read(ScrFD, Out.data(), Len);
      Out.resize(Read > 0 ? Read : 0);
    }
    close(ScrFD);
    unlink(Scr);
    return true;
  };

  SmallString<128> Printed;

  // (2) MachineOutliner. TM bit (clang -moutline via -mllvm, C API
  // LLVMSetTargetMachineMachineOutliner) OR the llc hidden static flag
  // with an enable value. TargetDefault/never/absent are admitted.
  if (TM.Options.EnableMachineOutliner)
    Reject("machine-outliner (-enable-machine-outliner/-moutline) is not "
           "qualified for the one-commit Format E packet lifecycle "
           "(contracts/pipeline.md common tail; GR2.3)");
  Printed.clear();
  if (PrintedFlagValue("enable-machine-outliner", Printed)) {
    // "= never" and the untouched "= *unknown option value*" default are
    // disable/no-request spellings; everything else (always /
    // optimistic-pgo / conservative-pgo, including the bare sentinel
    // print) is an enable request.
    StringRef V(Printed.data(), Printed.size());
    bool IsNever = V.contains("= never");
    bool IsUnknownDefault = V.contains("*unknown option value*");
    if (!IsNever && !IsUnknownDefault)
      Reject("machine-outliner (-enable-machine-outliner/-moutline) is not "
             "qualified for the one-commit Format E packet lifecycle "
             "(contracts/pipeline.md common tail; GR2.3)");
  }

  // (3) MachineFunctionSplitter. TM bit (-split-machine-functions via llc
  // CommandFlags, clang -fsplit-machine-functions) OR the llc hidden
  // static flag -enable-split-machine-functions with a true value.
  if (TM.Options.EnableMachineFunctionSplitter)
    Reject("machine-function-splitter "
           "(-enable-split-machine-functions/-split-machine-functions/"
           "-fsplit-machine-functions) is not qualified for the one-commit "
           "Format E packet lifecycle (contracts/pipeline.md common tail; "
           "GR2.3)");
  Printed.clear();
  if (PrintedFlagValue("enable-split-machine-functions", Printed)) {
    // bool flag: "= 1" is enable; "= 0" is an explicit disable, admitted.
    StringRef SV(Printed.data(), Printed.size());
    if (SV.contains("= 1"))
      Reject("machine-function-splitter "
             "(-enable-split-machine-functions/-split-machine-functions/"
             "-fsplit-machine-functions) is not qualified for the one-commit "
             "Format E packet lifecycle (contracts/pipeline.md common tail; "
             "GR2.3)");
  }
}

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
// is at addPostBBSections under -haydn-sms2 (product default ON, G004
// flip 2026-08-27), after the common executable tail and before closure
// Finalize. insertIndirectBranch
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
  // GR2.4: HaydnSubtarget::forcePostRAScheduling() makes this one invocation
  // mandatory for EVERY function, including optnone — scheduling and its
  // sequential singleton fallback commit are legal-encode ownership, not
  // reorder quality. optnone bodies may still co-issue independent ops like
  // plain O0; the only remaining non-entries are the explicit
  // -enable-post-ra-machine-sched=false product flag and pipeline truncation
  // (-stop-after/-run-pass).
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
  // After scheduling, wrap remaining standalone MIs as singleton BUNDLEs and
  // stamp generated Format E members (AIE2TargetMachine.cpp:242-244
  // createAIEFinalizeBundle; AIEFinalizeBundle.cpp:40-59). Multi-MI already
  // stamped in HaydnPostRASchedStrategy::finalizeLegalMultiMI. Finalize and
  // Verify never call skipFunction: they are target-local no-reorder commit
  // ownership so product emission never sees uncommitted bare encode MIR.
  // Do not reopen skipFunction on Finalize/Verify.
  // GR2.4: with forcePostRAScheduling() the scheduler has already committed
  // every function incl. optnone; Finalize owns only true residual commits
  // (e.g. late BranchRelaxation insertIndirectBranch parcels re-committed by
  // the addPreEmitPass re-run). VerifyBundles' optnone bare-encode refusal is
  // unchanged. Leave only committed Format-E cycles for MC (underfill/top-pad
  // invent stays fail-closed when golden is silent).
  addPass(createHaydnFinalizeBundlePass());
  // Fail-closed committed-cycle verifier immediately after finalize
  // (AIEBaseInstrInfo.cpp:1440-1459 verifyInstruction peer; AIE finalize
  // commit surface AIEHazardRecognizer.cpp:278-312 under test). Also refuses
  // optnone bare-encode escape and mixed committed+bare encode residual.
  // Invariant-only seat (D1.13): freeze identity is bound by registration
  // argument at the addPreEmitPass2 seat, never by instance count.
  addPass(createHaydnVerifyBundlesPass(/*IsFreezeSeat=*/false));
}

void HaydnPassConfig::addBlockPlacement() {
  // AIE2TargetMachine::addBlockPlacement: placement already done in addPreSched2.
}

void HaydnPassConfig::addPreEmitPass() {
  // GR2.3 fail-closed wall: reject any request for an unsupported
  // executable common-tail writer BEFORE the common tail can add it.
  // TargetPassConfig::addMachinePasses invokes addPreEmitPass strictly
  // before its own writer block (MachineOutliner, MachineFunctionSplitter,
  // static-data splitting, BasicBlockSections) and before addPostBBSections
  // (TargetPassConfig.cpp addPreEmitPass call -> writer passes ->
  // addPostBBSections -> addPreEmitPass2), so a rejection here fires at
  // pipeline construction: no outlining/splitting/reordering MI is ever
  // created, and nothing unqualified ever reaches the S2 closure, the
  // addPostBBSections closure Finalize/Verify, or the addPreEmitPass2
  // freeze verifier. contracts/pipeline.md common tail: "A configuration
  // requesting an unsupported executable common-tail writer rejects."
  haydnRejectUnsupportedCommonTailWriters(getHaydnTargetMachine());
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
  // Invariant-only seat (D1.13): explicit non-freeze registration.
  addPass(createHaydnVerifyBundlesPass(/*IsFreezeSeat=*/false));
}

void HaydnPassConfig::addPostBBSections() {
  // W68.2R late VLIW closure owner (contracts/pipeline.md "Required
  // terminal multi-format lifecycle"): this seat runs AFTER every common
  // executable writer — RegUsageInfoCollector/IPRA, FuncletLayout,
  // RemoveLoadsIntoFakeUses, StackMapLiveness, LiveDebugValues, sanitizer
  // metadata, MachineOutliner, function/data splitting, and
  // BasicBlockSections all precede addPostBBSections. S2
  // (HaydnLateConvergence, -haydn-sms2 product default ON) chooses current
  // physical MIs here, then the
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
  // product-pipeline-only. Flag is product default ON (G004 flip
  // 2026-08-27, haydnLateConvergenceProductDefaultEnabled).
  if (haydnSMS2Enabled())
    addPass(createHaydnLateConvergencePass());
  if (TargetPassConfig::hasLimitedCodeGenPipeline())
    return;
  addPass(createHaydnFinalizeBundlePass());
  // Invariant-only seat (D1.13): the freeze verifier is the later
  // addPreEmitPass2 registration, not this closure Verify.
  addPass(createHaydnVerifyBundlesPass(/*IsFreezeSeat=*/false));
  // W70.2 function-alignment writer (GOALS/contract: "AIE MachineAlignment
  // seat after first Finalize and after S2 closure: pad with a legal
  // generated idle row. Delete printer emitFunctionEntryLabel growth.
  // Prefix budgets charge the same pad."). Peer AIE2TargetMachine.cpp:247
  // seats createAIEMachineAlignment directly after createAIEFinalizeBundle.
  // This seat is after the FIRST Finalize (addPreSched2) and after the S2
  // closure Finalize+Verify above, and before the addPreEmitPass2 freeze
  // verifier — the pads are real committed idle-parcel BUNDLEs, so the
  // freeze verifier and every byte-distance consumer (BR/HWLoop walks via
  // getInstSizeInBytes) charge them for free. Pad-only (no AIE elongation):
  // both product rows encode the same single EncodedBytes parcel and golden
  // admits no underfill/top-pad. Runs at every opt level including optnone
  // (layout, not optimization); same limited-pipeline carve-out as the
  // closure seats above it.
  addPass(createHaydnMachineAlignmentPass());
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
  // D1.13 closed invariant: freeze identity is pinned HERE, by the
  // IsFreezeSeat=true registration argument — the only such instance in
  // the pipeline. The three earlier Verify seats (addPreSched2,
  // addPreEmitPass, addPostBBSections) are invariant-only by explicit
  // false; no instance-count heuristic and no cross-instance global
  // exists, so neither pipeline census drift nor per-thread pass cloning
  // under parallel codegen can silently move or disable a freeze wall.
  // The factory takes no default argument: any future seat must state
  // its identity at the call site (compile error otherwise).
  //
  // Same limited-pipeline probe carve-out as addPostBBSections: the
  // freeze is a property of the COMPLETE pipeline only.
  if (TargetPassConfig::hasLimitedCodeGenPipeline())
    return;
  addPass(createHaydnVerifyBundlesPass(/*IsFreezeSeat=*/true));
}
