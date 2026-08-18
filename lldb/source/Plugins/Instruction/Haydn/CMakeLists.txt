add_lldb_library(lldbPluginInstructionHaydn PLUGIN
  EmulateInstructionHaydn.cpp

  LINK_COMPONENTS
    Support
    TargetParser
  LINK_LIBS
    lldbCore
    lldbInterpreter
    lldbSymbol
  )
