add_llvm_component_group(Haydn)

# Per-call LLVM_TARGET_DEFINITIONS (standard AMDGPU/X86 pattern, — no
# custom -gen-haydn-* backends). The tablegen function reads this directory
# variable at each call site, so re-setting it between calls selects the.td
# per backend.
#
# gen-asm-matcher uses HaydnAsmMatcher.td; every other backend uses Haydn.td.
# Mode-0/3 decode-variant islands and Bundle128 composites are deleted —
# not included by either root.td.
# Live product encoding is Format E (BUNDLE_E96_* in HaydnCompositeFormats.td).
# Residual `_S*` FU members remain in HaydnFormats*.td for placement tables.

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
  HaydnPipelinerLoopInfo.cpp
  HaydnFrameLowering.cpp
  HaydnISelLowering.cpp
  HaydnAsmPrinter.cpp
  HaydnExpandPseudos.cpp
  HaydnMCInstLower.cpp
  HaydnMachineFunctionInfo.cpp
  HaydnMachineScheduler.cpp
  HaydnPostRAMultiStage.cpp
  HaydnPreRASchedStrategy.cpp
  HaydnSchedMutations.cpp
  HaydnPostRASchedStrategy.cpp
  HaydnFinalizeBundle.cpp
  HaydnVerifyBundles.cpp
  HaydnBundleVerify.cpp
  HaydnBundleFormatSolver.cpp
  HaydnLatencyStalls.cpp
  HaydnHazardRecognizer.cpp
  HaydnHardwareLoops.cpp
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

# T-TII2 / R1: fail-closed Format E importer --check (generated records +
# XLSX↔JSON parity + canonical-vector ledger round-trip). Peer is BundleSim
# generate_catalog.py --check wired as add_custom_target / add_test
# (bundlesim/CMakeLists.txt:82-83 and :538-546). AIE has no golden importer;
# formats come from TableGen -gen-instr-format (AIE/CMakeLists.txt:18) and
# AIEMCFormats.h:45-47 generated includes.
if(Python3_EXECUTABLE)
  if(DEFINED ENV{HAYDN_GOLDEN_DIR} AND NOT "$ENV{HAYDN_GOLDEN_DIR}" STREQUAL "")
    set(_HAYDN_GOLDEN_DIR "$ENV{HAYDN_GOLDEN_DIR}")
  elseif(DEFINED ENV{BUNDLESIM_GOLDEN_DIR} AND NOT "$ENV{BUNDLESIM_GOLDEN_DIR}" STREQUAL "")
    set(_HAYDN_GOLDEN_DIR "$ENV{BUNDLESIM_GOLDEN_DIR}")
  else()
    set(_HAYDN_GOLDEN_DIR "/ssd2/mhyang/haydn-plans/Database/golden")
  endif()
  if(EXISTS "${_HAYDN_GOLDEN_DIR}/golden/format_e_bit_layout_v2.json"
     AND NOT EXISTS "${_HAYDN_GOLDEN_DIR}/format_e_bit_layout_v2.json")
    set(_HAYDN_GOLDEN_DIR "${_HAYDN_GOLDEN_DIR}/golden")
  endif()
  # Discovery, not trust (content is hash-pinned): fall back to the user
  # database copy when the plans tree is not on this machine.
  if(NOT EXISTS "${_HAYDN_GOLDEN_DIR}/format_e_bit_layout_v2.json"
     AND EXISTS "$ENV{HOME}/haydn/format_e_bit_layout_v2.json")
    set(_HAYDN_GOLDEN_DIR "$ENV{HOME}/haydn")
  endif()
  # The canonical-vector ledger exists only where it was authored (and must
  # be regenerated against the repaired golden before --check passes again).
  # Without it the checks cannot run — degrade to a loud skip instead of
  # wedging every build that depends on HaydnTests.
  if(NOT EXISTS "${_HAYDN_GOLDEN_DIR}/format_e_canonical_vectors_v1.json")
    set(_HAYDN_CHECK_SKIP TRUE)
  endif()
  if(_HAYDN_CHECK_SKIP)
    add_custom_target(HaydnFormatERecordsCheck
      COMMAND "${CMAKE_COMMAND}" -E echo
              "HaydnFormatERecordsCheck SKIPPED: canonical-vector ledger not found under ${_HAYDN_GOLDEN_DIR} (regenerate it against the repaired golden on the plans machine)"
      COMMENT "T-TII2: Format E importer --check SKIPPED (no canonical ledger)"
      VERBATIM
    )
    add_custom_target(HaydnSchedRecordsCheck
      COMMAND "${CMAKE_COMMAND}" -E echo
              "HaydnSchedRecordsCheck SKIPPED: canonical-vector ledger not found under ${_HAYDN_GOLDEN_DIR}"
      COMMENT "T-TII2: sched records --check SKIPPED (no canonical ledger)"
      VERBATIM
    )
  endif()
  if(NOT _HAYDN_CHECK_SKIP)
  add_custom_target(HaydnFormatERecordsCheck
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/FormatE/generate_format_e_records.py"
            --check
            --json "${_HAYDN_GOLDEN_DIR}/format_e_bit_layout_v2.json"
            --xlsx "${_HAYDN_GOLDEN_DIR}/format_e_bit_layout_v2.xlsx"
            --canonical-vectors "${_HAYDN_GOLDEN_DIR}/format_e_canonical_vectors_v1.json"
            --out-dir "${CMAKE_CURRENT_SOURCE_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "T-TII2: Format E importer --check (records + XLSX/JSON + canonical vectors)"
    VERBATIM
  )
  add_custom_target(HaydnSchedRecordsCheck
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/FormatE/generate_sched_records.py"
            --check
            --json "${_HAYDN_GOLDEN_DIR}/format_e_bit_layout_v2.json"
            --xlsx "${_HAYDN_GOLDEN_DIR}/format_e_bit_layout_v2.xlsx"
            --canonical-vectors "${_HAYDN_GOLDEN_DIR}/format_e_canonical_vectors_v1.json"
            --constraints "${_HAYDN_GOLDEN_DIR}/VLIW_Engine_Compiler_Constraints.md"
            --out-dir "${CMAKE_CURRENT_SOURCE_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Published itinerary generator --check (HaydnGenSchedRecords.inc)"
    VERBATIM
  )
  endif()
endif()
