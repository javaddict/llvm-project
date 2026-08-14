//===-- ABISysV_haydn.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABISysV_haydn.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABISysV_haydn, ABIHaydn)

// DWARF register numbers match llvm/lib/Target/Haydn/HaydnRegisterInfo.td.
// Remote GDB RSP uses a different layout (PC at 16, AR, then DR); remote
// discovery is via qRegisterInfo / target.xml, which must advertise these
// DWARF numbers (not the RSP index) plus alt-name dN for drN.
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
  dwarf_d0 = 16,
  dwarf_d1,
  dwarf_d2,
  dwarf_d3,
  dwarf_d4,
  dwarf_d5,
  dwarf_d6,
  dwarf_d7,
  dwarf_d8,
  dwarf_d9,
  dwarf_d10,
  dwarf_d11,
  dwarf_d12,
  dwarf_d13,
  dwarf_d14,
  dwarf_d15,
  dwarf_ar0 = 32,
  dwarf_ar1 = 33,
};

// DEFINE_REG(name, alt, size, dwarf, generic)
#define DEFINE_GPR(name, alt, dwarf_num, generic)                              \
  {                                                                            \
    name, alt, 4, 0, eEncodingUint, eFormatHex,                                \
        {dwarf_num, dwarf_num, generic, LLDB_INVALID_REGNUM,                   \
         LLDB_INVALID_REGNUM},                                                 \
        nullptr, nullptr, nullptr,                                             \
  }

#define DEFINE_DR(name, alt, dwarf_num)                                        \
  {                                                                            \
    name, alt, 8, 0, eEncodingUint, eFormatHex,                                \
        {dwarf_num, dwarf_num, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,       \
         LLDB_INVALID_REGNUM},                                                 \
        nullptr, nullptr, nullptr,                                             \
  }

#define DEFINE_AR(name, dwarf_num)                                             \
  {                                                                            \
    name, nullptr, 8, 0, eEncodingUint, eFormatHex,                            \
        {dwarf_num, dwarf_num, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,       \
         LLDB_INVALID_REGNUM},                                                 \
        nullptr, nullptr, nullptr,                                             \
  }

static RegisterInfo g_register_infos[] = {
    DEFINE_GPR("r0", nullptr, dwarf_r0, LLDB_INVALID_REGNUM),
    // R1 is first argument and integer/pointer return (R0 is soft-zero).
    DEFINE_GPR("r1", nullptr, dwarf_r1, LLDB_REGNUM_GENERIC_ARG1),
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
    // DR64 bank. Alt names match BundleSim RSP (dr0–dr15).
    DEFINE_DR("d0", "dr0", dwarf_d0),
    DEFINE_DR("d1", "dr1", dwarf_d1),
    DEFINE_DR("d2", "dr2", dwarf_d2),
    DEFINE_DR("d3", "dr3", dwarf_d3),
    DEFINE_DR("d4", "dr4", dwarf_d4),
    DEFINE_DR("d5", "dr5", dwarf_d5),
    DEFINE_DR("d6", "dr6", dwarf_d6),
    DEFINE_DR("d7", "dr7", dwarf_d7),
    DEFINE_DR("d8", "dr8", dwarf_d8),
    DEFINE_DR("d9", "dr9", dwarf_d9),
    DEFINE_DR("d10", "dr10", dwarf_d10),
    DEFINE_DR("d11", "dr11", dwarf_d11),
    DEFINE_DR("d12", "dr12", dwarf_d12),
    DEFINE_DR("d13", "dr13", dwarf_d13),
    DEFINE_DR("d14", "dr14", dwarf_d14),
    DEFINE_DR("d15", "dr15", dwarf_d15),
    DEFINE_AR("ar0", dwarf_ar0),
    DEFINE_AR("ar1", dwarf_ar1),
};

static const uint32_t k_num_register_infos =
    sizeof(g_register_infos) / sizeof(RegisterInfo);

const RegisterInfo *ABISysV_haydn::GetRegisterInfoArray(uint32_t &count) {
  count = k_num_register_infos;
  return g_register_infos;
}

size_t ABISysV_haydn::GetRedZoneSize() const { return 0; }

bool ABISysV_haydn::GetPointerReturnRegister(const char *&name) {
  // ILP32 returns pointers / i32 in R1 (R0 is reserved soft-zero).
  name = "r1";
  return true;
}

bool ABISysV_haydn::GetFallbackRegisterLocation(
    const RegisterInfo *reg_info,
    UnwindPlan::Row::AbstractRegisterLocation &unwind_regloc) {
  // Caller-saved / volatile locations are not recoverable from the next
  // frame. Nested unwind still prefers compiler CFI when present.
  if (RegisterIsVolatile(reg_info)) {
    unwind_regloc.SetUndefined();
    return true;
  }
  return false;
}

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

static const RegisterInfo *find_reg_by_names(RegisterContext &reg_ctx,
                                             llvm::ArrayRef<const char *> names) {
  for (const char *name : names) {
    if (const RegisterInfo *info = reg_ctx.GetRegisterInfoByName(name))
      return info;
  }
  return nullptr;
}

ValueObjectSP
ABISysV_haydn::GetReturnValueObjectImpl(Thread &thread,
                                        CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  if (!return_compiler_type)
    return return_valobj_sp;

  RegisterContextSP reg_ctx_sp = thread.GetRegisterContext();
  if (!reg_ctx_sp)
    return return_valobj_sp;

  const uint32_t type_flags = return_compiler_type.GetTypeInfo();
  const size_t byte_size =
      llvm::expectedToOptional(return_compiler_type.GetByteSize(&thread))
          .value_or(0);
  if (byte_size == 0)
    return return_valobj_sp;

  const RegisterInfo *reg_info = nullptr;
  const bool is_scalar =
      (type_flags & (eTypeIsInteger | eTypeIsPointer | eTypeIsFloat |
                     eTypeIsVector)) != 0;
  if (!is_scalar)
    return return_valobj_sp;

  // RetCC_Haydn: i64/f64/64-bit SIMD in D0. Remote stub names this dr0.
  if (byte_size == 8) {
    const char *dr_names[] = {"d0", "dr0"};
    reg_info = find_reg_by_names(*reg_ctx_sp, dr_names);
  } else if (byte_size <= 4) {
    // i32/ptr/f32 and 32-bit SLP vectors in R1 (R0 is soft-zero).
    const char *r1_names[] = {"r1"};
    reg_info = find_reg_by_names(*reg_ctx_sp, r1_names);
    if (!reg_info)
      reg_info = reg_ctx_sp->GetRegisterInfo(eRegisterKindGeneric,
                                             LLDB_REGNUM_GENERIC_ARG1);
  }
  if (!reg_info)
    return return_valobj_sp;

  RegisterValue reg_value;
  if (!reg_ctx_sp->ReadRegister(reg_info, reg_value))
    return return_valobj_sp;

  DataExtractor data;
  if (!reg_value.GetData(data))
    return return_valobj_sp;
  if (data.GetByteSize() > byte_size)
    data = DataExtractor(data, 0, byte_size);

  return ValueObjectConstResult::Create(thread.GetStackFrameAtIndex(0).get(),
                                        return_compiler_type, ConstString(""),
                                        data);
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

static bool name_is_callee_saved(llvm::StringRef name) {
  if (name.empty())
    return false;
  if (name == "r8" || name == "r9" || name == "r10" || name == "r11")
    return true;
  if (name == "fp" || name == "r14" || name == "lr" || name == "r15")
    return true;
  if (name == "sp" || name == "r13")
    return true;
  // Callee-saved DR bank D8–D15 (DWARF dN / remote drN).
  llvm::StringRef rest = name;
  if (rest.consume_front("dr") || rest.consume_front("d")) {
    unsigned idx = 0;
    if (!rest.getAsInteger(10, idx) && rest.empty() && idx >= 8 && idx <= 15)
      return true;
  }
  return false;
}

bool ABISysV_haydn::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  if (!reg_info || !reg_info->name)
    return false;
  if (name_is_callee_saved(llvm::StringRef(reg_info->name)))
    return true;
  if (reg_info->alt_name &&
      name_is_callee_saved(llvm::StringRef(reg_info->alt_name)))
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
