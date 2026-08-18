set_property(DIRECTORY PROPERTY LLDB_PLUGIN_KIND Instruction)

add_subdirectory(ARM)
add_subdirectory(ARM64)
add_subdirectory(LoongArch)
add_subdirectory(MIPS)
add_subdirectory(MIPS64)
add_subdirectory(PPC64)
add_subdirectory(RISCV)

# Haydn is an experimental target (LLVM_EXPERIMENTAL_TARGETS_TO_BUILD).
if ("Haydn" IN_LIST LLVM_TARGETS_TO_BUILD OR
    "Haydn" IN_LIST LLVM_EXPERIMENTAL_TARGETS_TO_BUILD)
  add_subdirectory(Haydn)
endif()
