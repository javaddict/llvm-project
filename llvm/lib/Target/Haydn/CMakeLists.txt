add_llvm_component_group(Haydn)

# Per-call LLVM_TARGET_DEFINITIONS (standard AMDGPU/X86 pattern, — no
# custom -gen-haydn-* backends). The tablegen function reads this directory
# variable at each call site, so re-setting it between calls selects the.td
# per backend.
#
# gen-asm-matcher uses HaydnAsmMatcher.td; every other backend uses Haydn.td.
# Mode-0/3 decode-variant islands and Bundle128 composites are deleted —
# not included by either root.td.
# Live product encoding is Format E (BUNDLE_E96_* in HaydnFormatE.td).
# Residual `_S*` FieldSlot families are deleted.

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
  HaydnInterBlockScheduling.cpp
  HaydnPipelinerLoopInfo.cpp
  HaydnFrameLowering.cpp
  HaydnISelLowering.cpp
  HaydnAsmPrinter.cpp
  HaydnExpandPseudos.cpp
  HaydnMCInstLower.cpp
  HaydnMachineFunctionInfo.cpp
  HaydnMachineScheduler.cpp
  HaydnPreRASchedStrategy.cpp
  HaydnSchedMutations.cpp
  HaydnPostRASchedStrategy.cpp
  HaydnFinalizeBundle.cpp
  HaydnVerifyBundles.cpp
  HaydnBundleVerify.cpp
  HaydnBundleFormatSolver.cpp
  HaydnBundleMaterialize.cpp
  HaydnResourceCycle.cpp
  HaydnLatencyStalls.cpp
  HaydnHazardRecognizer.cpp
  HaydnLateConvergence.cpp
  HaydnMachineAlignment.cpp
  HaydnHardwareLoops.cpp
  HaydnHWLoopDemote.cpp
  HaydnFixupHwLoops.cpp
  HaydnEnsureTerminators.cpp
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

# Generated-source regeneration is an explicit developer step, NOT part of the
# build. Ordinary builds consume the checked-in generated outputs
# (HaydnGenFormatE*.inc, HaydnFormatsE96Members.td.inc,
# HaydnInstrInfoGolden.td.inc) the same way they consume authored sources.
# To regenerate after a golden v2_2 update, run the importer manually against
# the pinned nine-file golden bundle (see contracts/pipeline.md
# "Generated/authored input and check contract" and GOALS P19):
#   python3 FormatE/generate_format_e_records.py \
#     --json  <golden>/format_e_bit_layout_v2_2.json \
#     --xlsx  <golden>/format_e_bit_layout_v2_2.xlsx \
#     --canonical-vectors <golden>/format_e_canonical_vectors_v1.json \
#     --out-dir .
# and generate_sched_records.py likewise (adds --constraints). Both default to
# v2_2 names and pin golden hashes. Do not wire golden paths into CMake: they
# are machine-specific and would silently skip (or worse, pass stale) on hosts
# without the plans-tree bundle.
