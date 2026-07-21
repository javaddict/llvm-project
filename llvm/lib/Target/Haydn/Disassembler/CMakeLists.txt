add_llvm_component_library(LLVMHaydnDisassembler
  HaydnDisassembler.cpp

  LINK_COMPONENTS
  HaydnDesc
  HaydnInfo
  MC
  MCDisassembler
  Support

  ADD_TO_COMPONENT
  Haydn
  )
