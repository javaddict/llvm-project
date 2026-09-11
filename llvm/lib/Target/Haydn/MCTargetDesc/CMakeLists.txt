add_llvm_component_library(LLVMHaydnDesc
  HaydnMCTargetDesc.cpp
  HaydnMCAsmInfo.cpp
  HaydnInstPrinter.cpp
  HaydnAsmBackend.cpp
  HaydnRelocLayout.cpp
  HaydnCallRelax.cpp
  HaydnELFObjectWriter.cpp
  HaydnMCELFStreamer.cpp
  HaydnMCCodeEmitter.cpp
  HaydnMCChecker.cpp
  HaydnMatInt.cpp
  HaydnFormat.cpp
  HaydnMCFormats.cpp

  LINK_COMPONENTS
  HaydnInfo
  MC
  MCDisassembler
  Support

  ADD_TO_COMPONENT
  Haydn
  )

# tablegen(LLVM HaydnGenRegisterBank.inc -gen-register-bank) already in parent
