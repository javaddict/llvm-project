add_llvm_component_group(Haydn)

# Per-call LLVM_TARGET_DEFINITIONS (standard AMDGPU/X86 pattern, — no
# custom -gen-haydn-* backends). The tablegen function reads this directory
# variable at each call site, so re-setting it between calls selects the.td
# per backend.
#
# gen-asm-matcher uses HaydnAsmMatcher.td; every other backend uses Haydn.td.
# Mode-0/3 decode-variant islands (HaydnInstrInfoM0/MACM0/M3/S*M0.td)
# are archived under legacy-retired/ and are NOT included by either root.td.
# Live encoding is `_S<k>_FLEX` + BUNDLE128_FULL (HaydnFormats*.td
# HaydnCompositeFormats.td). Historical matcher-vs-variant split notes: L251.

set(LLVM_TARGET_DEFINITIONS HaydnAsmMatcher.td)
tablegen(LLVM HaydnGenAsmMatcher.inc -gen-asm-matcher)

set(LLVM_TARGET_DEFINITIONS Haydn.td)
tablegen(LLVM HaydnGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM HaydnGenCallingConv.inc -gen-callingconv)
tablegen(LLVM HaydnGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM HaydnGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM HaydnGenFormats.inc -gen-instr-format)
tablegen(LLVM HaydnGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM HaydnGenMCPseudoLowering.inc -gen-pseudo-lowering)
tablegen(LLVM HaydnGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM HaydnGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM HaydnGenGlobalISel.inc -gen-global-isel)
tablegen(LLVM HaydnGenPostLegalizeGICombiner.inc -gen-global-isel-combiner
              -combiners="HaydnPostLegalizerCombiner")
tablegen(LLVM HaydnGenPreLegalizeGICombiner.inc -gen-global-isel-combiner
              -combiners="HaydnPreLegalizerCombiner")
tablegen(LLVM HaydnGenRegisterBank.inc -gen-register-bank)
tablegen(LLVM HaydnGenDFAPacketizer.inc -gen-dfa-packetizer)
# Exegesis not supported - requires performance counter bindings
# tablegen(LLVM HaydnGenExegesis.inc -gen-exegesis)

add_public_tablegen_target(HaydnCommonTableGen)

# Stub.cpp files — will be implemented in Streams C, D, E
# For M1 we only need the build to succeed with minimal stubs
add_llvm_target(HaydnCodeGen
  HaydnCallingConv.cpp
  HaydnTargetMachine.cpp
  HaydnTargetTransformInfo.cpp
  HaydnSubtarget.cpp
  HaydnRegisterInfo.cpp
  HaydnPostRAScratch.cpp
  HaydnInstrInfo.cpp
  HaydnFrameLowering.cpp
  HaydnISelLowering.cpp
  HaydnAsmPrinter.cpp
  HaydnExpandPseudos.cpp
  HaydnExpandPostIncEarly.cpp
  HaydnMCInstLower.cpp
  HaydnMachineFunctionInfo.cpp
  HaydnMachineScheduler.cpp
  HaydnPreRASchedStrategy.cpp
  HaydnSchedMutations.cpp
  HaydnPostRASchedStrategy.cpp
  HaydnFinalizeBundle.cpp
  HaydnLatencyStalls.cpp
  HaydnVerifyBundles.cpp
  HaydnHazardRecognizer.cpp
  HaydnHardwareLoops.cpp
  HaydnFixupHwLoops.cpp
  HaydnCFGOptimizer.cpp
  HaydnConditionOptimizer.cpp
  HaydnCopyElim.cpp
  HaydnPEIPeephole.cpp
  HaydnEnsureTerminators.cpp
  HaydnBitSimplify.cpp
  HaydnAlternateDescriptors.cpp

  # GlobalISel sources
  GISel/HaydnCallLowering.cpp
  GISel/HaydnInstructionSelector.cpp
  GISel/HaydnLegalizerInfo.cpp
  GISel/HaydnRegisterBankInfo.cpp
  GISel/HaydnPostLegalizerCombiner.cpp
  GISel/HaydnPreLegalizerCombiner.cpp
  GISel/HaydnPostSelectOptimize.cpp

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Analysis
  TransformUtils
  AsmPrinter
  HaydnDesc
  HaydnInfo
  CodeGen
  CodeGenTypes
  Core
  GlobalISel
  MC
  SelectionDAG
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  Haydn
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(TargetInfo)
