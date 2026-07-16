add_lldb_library(lldbPluginABIHaydn PLUGIN
  ABISysV_haydn.cpp

  LINK_COMPONENTS
    Support
    TargetParser
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbValueObject
  )
