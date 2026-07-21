add_llvm_component_library(LLVMHaydnAsmParser
  HaydnAsmParser.cpp

  LINK_COMPONENTS
  HaydnDesc
  HaydnInfo
  MC
  MCParser
  Support

  ADD_TO_COMPONENT
  Haydn
  )
