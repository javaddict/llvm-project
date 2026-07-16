//===-- ABISysV_haydn.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABISysV_haydn.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Log.h"

#include "llvm/TargetParser/Triple.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABISysV_haydn, ABIHaydn)

// DWARF register numbers match llvm/lib/Target/Haydn/HaydnRegisterInfo.td.
// BundleSim GDB RSP uses a different layout for remote (PC=16, AR, DR, …);
// remote discovery is via qRegisterInfo. This table is for local ABI/unwind.
enum dwarf_regnums {
  dwarf_r0 = 0,
  dwarf_r1,
  dwarf_r2,
  dwarf_r3,
  dwarf_r4,
  dwarf_r5,
  dwarf_r6,
  dwarf_r7,
  dwarf_r8,
  dwarf_r9,
  dwarf_r10,
  dwarf_r11,
  dwarf_r12,
  dwarf_sp = 13, // R13
  dwarf_fp = 14, // R14
  dwarf_lr = 15, // R15
};

// DEFINE_REG(name, alt, size, dwarf, generic)
#define DEFINE_GPR(name, alt, dwarf_num, generic)                              \
  {                                                                            \
    name, alt, 4, 0, eEncodingUint, eFormatHex,                                \
        {dwarf_num, dwarf_num, generic, LLDB_INVALID_REGNUM,                   \
         LLDB_INVALID_REGNUM},                                                 \
        nullptr, nullptr, nullptr,                                             \
  }

static RegisterInfo g_register_infos[] = {
    DEFINE_GPR("r0", nullptr, dwarf_r0, LLDB_INVALID_REGNUM),
    DEFINE_GPR("r1", nullptr, dwarf_r1, LLDB_INVALID_REGNUM),
    DEFINE_GPR("r2", nullptr, dwarf_r2, LLDB_INVALID_REGNUM),
    DEFINE_GPR("r3", nullptr, dwarf_r3, LLDB_INVALID_REGNUM),
    DEFINE_GPR("r4", nullptr, dwarf_r4, LLDB_INVALID_REGNUM),
    DEFINE_GPR("r5", nullptr, dwarf_r5, LLDB_INVALID_REGNUM),
    DEFINE_GPR("r6", nullptr, dwarf_r6, LLDB_INVALID_REGNUM),
    DEFINE_GPR("r7", nullptr, dwarf_r7, LLDB_INVALID_REGNUM),
    DEFINE_GPR("r8", nullptr, dwarf_r8, LLDB_INVALID_REGNUM),
    DEFINE_GPR("r9", nullptr, dwarf_r9, LLDB_INVALID_REGNUM),
    DEFINE_GPR("r10", nullptr, dwarf_r10, LLDB_INVALID_REGNUM),
    DEFINE_GPR("r11", nullptr, dwarf_r11, LLDB_INVALID_REGNUM),
    DEFINE_GPR("r12", nullptr, dwarf_r12, LLDB_INVALID_REGNUM),
    DEFINE_GPR("sp", "r13", dwarf_sp, LLDB_REGNUM_GENERIC_SP),
    DEFINE_GPR("fp", "r14", dwarf_fp, LLDB_REGNUM_GENERIC_FP),
    DEFINE_GPR("lr", "r15", dwarf_lr, LLDB_REGNUM_GENERIC_RA),
    // Synthetic PC for unwind (not a DWARF GP). GDB remote maps PC at regnum
    // 16; generic PC is filled by the remote register context.
    {"pc",
     nullptr,
     4,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_REGNUM_GENERIC_PC,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr},
};

static const uint32_t k_num_register_infos =
    sizeof(g_register_infos) / sizeof(RegisterInfo);

const RegisterInfo *ABISysV_haydn::GetRegisterInfoArray(uint32_t &count) {
  count = k_num_register_infos;
  return g_register_infos;
}

size_t ABISysV_haydn::GetRedZoneSize() const { return 0; }

ABISP ABISysV_haydn::CreateInstance(ProcessSP process_sp, const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::haydn) {
    return ABISP(
        new ABISysV_haydn(std::move(process_sp), MakeMCRegisterInfo(arch)));
  }
  return ABISP();
}

bool ABISysV_haydn::PrepareTrivialCall(Thread &thread, lldb::addr_t sp,
                                       lldb::addr_t pc, lldb::addr_t ra,
                                       llvm::ArrayRef<addr_t> args) const {
  // No JIT expression path for freestanding BundleSim yet.
  (void)thread;
  (void)sp;
  (void)pc;
  (void)ra;
  (void)args;
  return false;
}

bool ABISysV_haydn::GetArgumentValues(Thread &thread, ValueList &values) const {
  (void)thread;
  (void)values;
  return false;
}

Status ABISysV_haydn::SetReturnValueObject(StackFrameSP &frame_sp,
                                           ValueObjectSP &new_value_sp) {
  (void)frame_sp;
  (void)new_value_sp;
  return Status();
}

ValueObjectSP
ABISysV_haydn::GetReturnValueObjectImpl(Thread &thread,
                                        CompilerType &return_compiler_type) const {
  (void)thread;
  (void)return_compiler_type;
  return ValueObjectSP();
}

// At function entry: CFA = SP, return address in LR.
UnwindPlanSP ABISysV_haydn::CreateFunctionEntryUnwindPlan() {
  UnwindPlan::Row row;
  row.GetCFAValue().SetIsRegisterPlusOffset(LLDB_REGNUM_GENERIC_SP, 0);
  row.SetRegisterLocationToRegister(LLDB_REGNUM_GENERIC_PC,
                                    LLDB_REGNUM_GENERIC_RA,
                                    /*can_replace=*/true);
  row.SetRegisterLocationToIsCFAPlusOffset(LLDB_REGNUM_GENERIC_SP, 0, true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindGeneric);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("haydn at-func-entry default");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  return plan_sp;
}

// Default (mid-function): CFA = FP, RA at [FP+0] after prologue when FP used;
// fall back to LR if no FP (leaf / -fomit-frame-pointer common on freestanding).
UnwindPlanSP ABISysV_haydn::CreateDefaultUnwindPlan() {
  UnwindPlan::Row row;
  // Prefer SP-based plan: freestanding often omits FP. CFA = SP; PC from LR.
  row.GetCFAValue().SetIsRegisterPlusOffset(LLDB_REGNUM_GENERIC_SP, 0);
  row.SetRegisterLocationToRegister(LLDB_REGNUM_GENERIC_PC,
                                    LLDB_REGNUM_GENERIC_RA, true);
  row.SetRegisterLocationToIsCFAPlusOffset(LLDB_REGNUM_GENERIC_SP, 0, true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindGeneric);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("haydn default unwind plan");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return plan_sp;
}

bool ABISysV_haydn::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

bool ABISysV_haydn::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  if (!reg_info || !reg_info->name)
    return false;
  llvm::StringRef name(reg_info->name);
  // Callee-saved: R8–R11, FP (R14), LR (R15). R0 soft-zero; R1–R7 / R12 args.
  if (name == "r8" || name == "r9" || name == "r10" || name == "r11")
    return true;
  if (name == "fp" || name == "r14" || name == "lr" || name == "r15")
    return true;
  if (name == "sp" || name == "r13")
    return true;
  return false;
}

void ABISysV_haydn::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "Baremetal SysV-style ABI for Haydn targets",
                                CreateInstance);
}

void ABISysV_haydn::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}
