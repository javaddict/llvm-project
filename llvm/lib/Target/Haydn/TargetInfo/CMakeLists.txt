add_llvm_component_library(LLVMHaydnInfo
  HaydnTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  Haydn
  )
