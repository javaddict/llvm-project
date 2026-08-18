//===-- EmulateInstructionHaydn.cpp -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "EmulateInstructionHaydn.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/lldb-enumerations.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(EmulateInstructionHaydn, InstructionHaydn)

// Peer: EmulateInstructionLoongArch.cpp EvaluateInstruction auto-advance
// (ReadPC, then WritePC(old + inst_size) when PC is unchanged). Overlay:
// inst_size is Format E EncodedBytes (12). This file does not decode members.

bool EmulateInstructionHaydn::SupportsThisArch(const ArchSpec &arch) {
  return arch.GetTriple().isHaydn();
}

void EmulateInstructionHaydn::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void EmulateInstructionHaydn::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

EmulateInstruction *
EmulateInstructionHaydn::CreateInstance(const ArchSpec &arch,
                                        InstructionType inst_type) {
  if (SupportsThisInstructionType(inst_type) && SupportsThisArch(arch))
    return new EmulateInstructionHaydn(arch);
  return nullptr;
}

bool EmulateInstructionHaydn::SetTargetTriple(const ArchSpec &arch) {
  return SupportsThisArch(arch);
}

bool EmulateInstructionHaydn::ReadInstruction() {
  auto addr = ReadPC();
  if (!addr) {
    m_addr = LLDB_INVALID_ADDRESS;
    return false;
  }
  m_addr = *addr;

  uint8_t bytes[kFormatEParcelBytes] = {};
  Context ctx;
  ctx.type = eContextReadOpcode;
  ctx.SetNoArgs();
  const size_t n = ReadMemory(ctx, m_addr, bytes, sizeof(bytes));
  if (n != sizeof(bytes)) {
    m_addr = LLDB_INVALID_ADDRESS;
    return false;
  }
  m_opcode.SetOpcodeBytes(bytes, sizeof(bytes));
  return true;
}

bool EmulateInstructionHaydn::EvaluateInstruction(uint32_t options) {
  // Peer: EmulateInstructionLoongArch.cpp:85-116 — ReadPC, emulate, then
  // WritePC(old + inst_size) only when PC is unchanged.
  // Overlay: No member decode.
  // Product step is EncodedBytes=12. Same-PC residual is repaired by the
  // +12 write; an already-moved PC is left alone so a live hardware step
  // cannot become +24 and a branch target is not clobbered.
  if (!(options & eEmulateInstructionOptionAutoAdvancePC))
    return true;
  if (m_addr == LLDB_INVALID_ADDRESS)
    return false;

  auto maybe_pc = ReadPC();
  if (!maybe_pc)
    return false;
  const lldb::addr_t old_pc = *maybe_pc;
  const uint32_t step = GetLastInstrSize().value_or(kFormatEParcelBytes);
  // Refuse a non-12 step rather than invent a member-sized advance.
  if (step != kFormatEParcelBytes)
    return false;
  if (old_pc != m_addr)
    return true;
  return WritePC(old_pc + step);
}

bool EmulateInstructionHaydn::TestEmulation(Stream &, ArchSpec &,
                                            OptionValueDictionary *) {
  return false;
}

std::optional<RegisterInfo>
EmulateInstructionHaydn::GetRegisterInfo(RegisterKind reg_kind,
                                         uint32_t reg_num) {
  // Minimal generic map for parcel step. Full DWARF/RSP tables live in
  // ABISysV_haydn (do not collapse RSP 16 onto D0).
  if (reg_kind != eRegisterKindGeneric)
    return {};

  auto make = [](const char *name, const char *alt, uint32_t generic,
                 uint32_t byte_size) -> RegisterInfo {
    RegisterInfo info = {};
    info.name = name;
    info.alt_name = alt;
    info.byte_size = byte_size;
    info.encoding = eEncodingUint;
    info.format = eFormatHex;
    info.kinds[eRegisterKindEHFrame] = LLDB_INVALID_REGNUM;
    info.kinds[eRegisterKindDWARF] = LLDB_INVALID_REGNUM;
    info.kinds[eRegisterKindGeneric] = generic;
    info.kinds[eRegisterKindProcessPlugin] = LLDB_INVALID_REGNUM;
    info.kinds[eRegisterKindLLDB] = LLDB_INVALID_REGNUM;
    return info;
  };

  switch (reg_num) {
  case LLDB_REGNUM_GENERIC_PC:
    return make("pc", nullptr, LLDB_REGNUM_GENERIC_PC, 4);
  case LLDB_REGNUM_GENERIC_SP:
    return make("sp", "r13", LLDB_REGNUM_GENERIC_SP, 4);
  case LLDB_REGNUM_GENERIC_FP:
    return make("fp", "r14", LLDB_REGNUM_GENERIC_FP, 4);
  case LLDB_REGNUM_GENERIC_RA:
    return make("lr", "r15", LLDB_REGNUM_GENERIC_RA, 4);
  default:
    return {};
  }
}
