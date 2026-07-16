include_directories(
  ${PROJECT_SOURCE_DIR}/lib/Target/Haydn
  ${PROJECT_SOURCE_DIR}/lib/Target/Haydn/MCTargetDesc
  ${PROJECT_BINARY_DIR}/lib/Target/Haydn
  )

set(LLVM_LINK_COMPONENTS
  HaydnCodeGen
  HaydnDesc
  HaydnInfo
  CodeGen
  CodeGenTypes
  Core
  GlobalISel
  MC
  Support
  TargetParser
  )

add_llvm_target_unittest(HaydnTests
  HaydnBundleTest.cpp
  HaydnMCFormatsTest.cpp
  HaydnHazardRecognizerTest.cpp
  )
