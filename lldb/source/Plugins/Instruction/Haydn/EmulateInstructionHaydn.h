//===-- EmulateInstructionHaydn.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Software single-step for Haydn Format E. Peer skeleton is
// EmulateInstructionLoongArch (ReadInstruction + AutoAdvancePC WritePC only
// when PC is unchanged). AIE has no Instruction plugin. Overlay: EncodedBytes
// is always 12; this plugin never decodes members or invents branch targets.
// Same-PC is repaired by +12; already-moved / non-12 PCs stay residual.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_INSTRUCTION_HAYDN_EMULATEINSTRUCTIONHAYDN_H
#define LLDB_SOURCE_PLUGINS_INSTRUCTION_HAYDN_EMULATEINSTRUCTIONHAYDN_H

#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Utility/Status.h"
#include <optional>

namespace lldb_private {

class EmulateInstructionHaydn : public EmulateInstruction {
public:
  static constexpr uint32_t kFormatEParcelBytes = 12;

  static llvm::StringRef GetPluginNameStatic() { return "haydn"; }

  static llvm::StringRef GetPluginDescriptionStatic() {
    return "Parcel-size step for Haydn Format E (12-byte; no member decode).";
  }

  static bool SupportsThisInstructionType(InstructionType inst_type) {
    switch (inst_type) {
    case eInstructionTypeAny:
    case eInstructionTypePCModifying:
      return true;
    case eInstructionTypePrologueEpilogue:
    case eInstructionTypeAll:
      return false;
    }
    return false;
  }

  static bool SupportsThisArch(const ArchSpec &arch);

  static EmulateInstruction *CreateInstance(const ArchSpec &arch,
                                            InstructionType inst_type);

  static void Initialize();

  static void Terminate();

  EmulateInstructionHaydn(const ArchSpec &arch) : EmulateInstruction(arch) {}

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  bool SupportsEmulatingInstructionsOfType(InstructionType inst_type) override {
    return SupportsThisInstructionType(inst_type);
  }

  bool SetTargetTriple(const ArchSpec &arch) override;
  bool ReadInstruction() override;
  std::optional<uint32_t> GetLastInstrSize() override {
    return kFormatEParcelBytes;
  }
  bool EvaluateInstruction(uint32_t options) override;
  bool TestEmulation(Stream &out_stream, ArchSpec &arch,
                     OptionValueDictionary *test_data) override;
  std::optional<RegisterInfo> GetRegisterInfo(lldb::RegisterKind reg_kind,
                                              uint32_t reg_num) override;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_INSTRUCTION_HAYDN_EMULATEINSTRUCTIONHAYDN_H
