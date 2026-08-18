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
#include "lldb/Utility/Status.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/TargetParser/Triple.h"

#include <string>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABISysV_haydn, ABIHaydn)

// DWARF numbers match llvm/lib/Target/Haydn/HaydnRegisterInfo.td.
// Process-plugin numbers match BundleSim k_reg_meta RSP order (PC at 16,
// AR, then DR). Remote qRegisterInfo / target.xml must advertise DWARF,
// not the RSP index. PC has no compiler DWARF (must not claim D0=16).
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
  // 34–35 unused (no compiler-visible AR2/AR3).
  dwarf_sfr = 36,
  dwarf_csr = 37,
  dwarf_cbr0 = 38,
  dwarf_cbr1 = 39,
};

// BundleSim gdb-remote process-plugin / g-packet indices.
enum rsp_regnums {
  rsp_r0 = 0,
  rsp_r1,
  rsp_r2,
  rsp_r3,
  rsp_r4,
  rsp_r5,
  rsp_r6,
  rsp_r7,
  rsp_r8,
  rsp_r9,
  rsp_r10,
  rsp_r11,
  rsp_r12,
  rsp_sp = 13,
  rsp_fp = 14,
  rsp_lr = 15,
  rsp_pc = 16,
  rsp_ar0 = 17,
  rsp_ar1 = 18,
  rsp_ar2 = 19,
  rsp_ar3 = 20,
  rsp_d0 = 21,
  rsp_d1,
  rsp_d2,
  rsp_d3,
  rsp_d4,
  rsp_d5,
  rsp_d6,
  rsp_d7,
  rsp_d8,
  rsp_d9,
  rsp_d10,
  rsp_d11,
  rsp_d12,
  rsp_d13,
  rsp_d14,
  rsp_d15,
  rsp_hwlr0_begin = 37,
  rsp_hwlr0_end,
  rsp_hwlr0_count,
  rsp_hwlr1_begin,
  rsp_hwlr1_end,
  rsp_hwlr1_count,
  rsp_cbr0_begin,
  rsp_cbr0_end,
  rsp_cbr1_begin,
  rsp_cbr1_end,
};

static_assert(dwarf_d0 == ABISysV_haydn::kDwarfD0, "D0 DWARF");
static_assert(dwarf_ar0 == ABISysV_haydn::kDwarfAR0, "AR0 DWARF");
static_assert(dwarf_sfr == ABISysV_haydn::kDwarfSFR, "SFR DWARF");
static_assert(dwarf_csr == ABISysV_haydn::kDwarfCSR, "CSR DWARF");
static_assert(dwarf_cbr0 == ABISysV_haydn::kDwarfCBR0, "CBR0 DWARF");
static_assert(dwarf_cbr1 == ABISysV_haydn::kDwarfCBR1, "CBR1 DWARF");
static_assert(rsp_pc == ABISysV_haydn::kRspPC, "PC RSP");
static_assert(rsp_ar0 == ABISysV_haydn::kRspAR0, "AR0 RSP");
static_assert(rsp_d0 == ABISysV_haydn::kRspDR0, "DR0 RSP");
// Same integer 16 in two namespaces: PC RSP vs D0 DWARF. DR RSP is 21.
static_assert(static_cast<unsigned>(rsp_pc) ==
              static_cast<unsigned>(dwarf_d0));
static_assert(static_cast<unsigned>(rsp_d0) !=
              static_cast<unsigned>(dwarf_d0));

// kinds[]: ehframe, dwarf, generic, process-plugin (RSP), lldb.
// Peer: ABISysV_hexagon.cpp process-plugin slot; ABISysV_riscv.cpp generic.
#define DEFINE_REG(name, alt, size, dwarf_num, generic, rsp_num)               \
  {                                                                            \
    name, alt, size, 0, eEncodingUint, eFormatHex,                             \
        {dwarf_num, dwarf_num, generic, rsp_num, LLDB_INVALID_REGNUM},         \
        nullptr, nullptr, nullptr,                                             \
  }

#define DEFINE_GPR(name, alt, dwarf_num, generic, rsp_num)                     \
  DEFINE_REG(name, alt, 4, dwarf_num, generic, rsp_num)

#define DEFINE_DR(name, alt, dwarf_num, rsp_num)                               \
  DEFINE_REG(name, alt, 8, dwarf_num, LLDB_INVALID_REGNUM, rsp_num)

#define DEFINE_AR(name, dwarf_num, rsp_num)                                    \
  DEFINE_REG(name, nullptr, 8, dwarf_num, LLDB_INVALID_REGNUM, rsp_num)

#define DEFINE_SYS32(name, dwarf_num, rsp_num)                                 \
  DEFINE_REG(name, nullptr, 4, dwarf_num, LLDB_INVALID_REGNUM, rsp_num)

static RegisterInfo g_register_infos[] = {
    DEFINE_GPR("r0", nullptr, dwarf_r0, LLDB_INVALID_REGNUM, rsp_r0),
    // R1 is first argument and integer/pointer return (R0 is soft-zero).
    // R1-R7 are the integer argument bank. Peer: ABISysV_riscv.cpp a0-a7
    // GENERIC_ARG* mapping. Haydn has seven GPR argument seats (R1-R7).
    DEFINE_GPR("r1", nullptr, dwarf_r1, LLDB_REGNUM_GENERIC_ARG1, rsp_r1),
    DEFINE_GPR("r2", nullptr, dwarf_r2, LLDB_REGNUM_GENERIC_ARG2, rsp_r2),
    DEFINE_GPR("r3", nullptr, dwarf_r3, LLDB_REGNUM_GENERIC_ARG3, rsp_r3),
    DEFINE_GPR("r4", nullptr, dwarf_r4, LLDB_REGNUM_GENERIC_ARG4, rsp_r4),
    DEFINE_GPR("r5", nullptr, dwarf_r5, LLDB_REGNUM_GENERIC_ARG5, rsp_r5),
    DEFINE_GPR("r6", nullptr, dwarf_r6, LLDB_REGNUM_GENERIC_ARG6, rsp_r6),
    DEFINE_GPR("r7", nullptr, dwarf_r7, LLDB_REGNUM_GENERIC_ARG7, rsp_r7),
    DEFINE_GPR("r8", nullptr, dwarf_r8, LLDB_INVALID_REGNUM, rsp_r8),
    DEFINE_GPR("r9", nullptr, dwarf_r9, LLDB_INVALID_REGNUM, rsp_r9),
    DEFINE_GPR("r10", nullptr, dwarf_r10, LLDB_INVALID_REGNUM, rsp_r10),
    DEFINE_GPR("r11", nullptr, dwarf_r11, LLDB_INVALID_REGNUM, rsp_r11),
    DEFINE_GPR("r12", nullptr, dwarf_r12, LLDB_INVALID_REGNUM, rsp_r12),
    DEFINE_GPR("sp", "r13", dwarf_sp, LLDB_REGNUM_GENERIC_SP, rsp_sp),
    DEFINE_GPR("fp", "r14", dwarf_fp, LLDB_REGNUM_GENERIC_FP, rsp_fp),
    DEFINE_GPR("lr", "r15", dwarf_lr, LLDB_REGNUM_GENERIC_RA, rsp_lr),
    // Synthetic PC: RSP 16, no compiler DWARF (D0 owns DWARF 16).
    DEFINE_REG("pc", nullptr, 4, LLDB_INVALID_REGNUM, LLDB_REGNUM_GENERIC_PC,
               rsp_pc),
    // DR64 bank. Alt names match BundleSim RSP (dr0–dr15). RSP 21–36.
    DEFINE_DR("d0", "dr0", dwarf_d0, rsp_d0),
    DEFINE_DR("d1", "dr1", dwarf_d1, rsp_d1),
    DEFINE_DR("d2", "dr2", dwarf_d2, rsp_d2),
    DEFINE_DR("d3", "dr3", dwarf_d3, rsp_d3),
    DEFINE_DR("d4", "dr4", dwarf_d4, rsp_d4),
    DEFINE_DR("d5", "dr5", dwarf_d5, rsp_d5),
    DEFINE_DR("d6", "dr6", dwarf_d6, rsp_d6),
    DEFINE_DR("d7", "dr7", dwarf_d7, rsp_d7),
    DEFINE_DR("d8", "dr8", dwarf_d8, rsp_d8),
    DEFINE_DR("d9", "dr9", dwarf_d9, rsp_d9),
    DEFINE_DR("d10", "dr10", dwarf_d10, rsp_d10),
    DEFINE_DR("d11", "dr11", dwarf_d11, rsp_d11),
    DEFINE_DR("d12", "dr12", dwarf_d12, rsp_d12),
    DEFINE_DR("d13", "dr13", dwarf_d13, rsp_d13),
    DEFINE_DR("d14", "dr14", dwarf_d14, rsp_d14),
    DEFINE_DR("d15", "dr15", dwarf_d15, rsp_d15),
    DEFINE_AR("ar0", dwarf_ar0, rsp_ar0),
    DEFINE_AR("ar1", dwarf_ar1, rsp_ar1),
    // Remote-visible only; no compiler DWARF (34–35 unused).
    DEFINE_AR("ar2", LLDB_INVALID_REGNUM, rsp_ar2),
    DEFINE_AR("ar3", LLDB_INVALID_REGNUM, rsp_ar3),
    DEFINE_SYS32("sfr", dwarf_sfr, LLDB_INVALID_REGNUM),
    DEFINE_SYS32("csr", dwarf_csr, LLDB_INVALID_REGNUM),
    // Compiler-manifest CBR wholes (HaydnRegisterInfo.td). RSP exposes the
    // begin/end halves below; those halves have no compiler DWARF.
    DEFINE_SYS32("cbr0", dwarf_cbr0, LLDB_INVALID_REGNUM),
    DEFINE_SYS32("cbr1", dwarf_cbr1, LLDB_INVALID_REGNUM),
    // Readable hardware-loop / circular-buffer CSRs. Remote RSP only.
    DEFINE_SYS32("hwlr0_begin", LLDB_INVALID_REGNUM, rsp_hwlr0_begin),
    DEFINE_SYS32("hwlr0_end", LLDB_INVALID_REGNUM, rsp_hwlr0_end),
    DEFINE_SYS32("hwlr0_count", LLDB_INVALID_REGNUM, rsp_hwlr0_count),
    DEFINE_SYS32("hwlr1_begin", LLDB_INVALID_REGNUM, rsp_hwlr1_begin),
    DEFINE_SYS32("hwlr1_end", LLDB_INVALID_REGNUM, rsp_hwlr1_end),
    DEFINE_SYS32("hwlr1_count", LLDB_INVALID_REGNUM, rsp_hwlr1_count),
    DEFINE_SYS32("cbr0_begin", LLDB_INVALID_REGNUM, rsp_cbr0_begin),
    DEFINE_SYS32("cbr0_end", LLDB_INVALID_REGNUM, rsp_cbr0_end),
    DEFINE_SYS32("cbr1_begin", LLDB_INVALID_REGNUM, rsp_cbr1_begin),
    DEFINE_SYS32("cbr1_end", LLDB_INVALID_REGNUM, rsp_cbr1_end),
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
  // Peer: ABISysV_riscv.cpp:204-248 register-only PrepareTrivialCall.
  // Haydn overlay: seven GPR seats (R1-R7), 8-byte SP, no GPR pair.
  RegisterContextSP reg_ctx_sp = thread.GetRegisterContext();
  if (!reg_ctx_sp)
    return false;
  if (args.size() > kGPRArgumentCount)
    return false;
  if ((sp & 0x7) != 0)
    return false;
  if ((pc & 0x1) != 0 || (ra & 0x1) != 0)
    return false;

  for (size_t I = 0, E = args.size(); I != E; ++I) {
    const RegisterInfo *RegInfo = reg_ctx_sp->GetRegisterInfo(
        eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1 + I);
    if (!RegInfo || !reg_ctx_sp->WriteRegisterFromUnsigned(RegInfo, args[I]))
      return false;
  }

  const RegisterInfo *PCInfo =
      reg_ctx_sp->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  const RegisterInfo *SPInfo =
      reg_ctx_sp->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);
  const RegisterInfo *RAInfo =
      reg_ctx_sp->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_RA);
  if (!PCInfo || !SPInfo || !RAInfo)
    return false;
  const addr_t FixedPC = pc & ~(addr_t)1;
  const addr_t FixedRA = ra & ~(addr_t)1;
  if (!reg_ctx_sp->WriteRegisterFromUnsigned(PCInfo, FixedPC))
    return false;
  if (!reg_ctx_sp->WriteRegisterFromUnsigned(SPInfo, sp))
    return false;
  if (!reg_ctx_sp->WriteRegisterFromUnsigned(RAInfo, FixedRA))
    return false;
  return true;
}

bool ABISysV_haydn::GetArgumentValues(Thread &thread, ValueList &values) const {
  (void)thread;
  (void)values;
  return false;
}

static const RegisterInfo *find_reg_by_names(RegisterContext &reg_ctx,
                                             llvm::ArrayRef<const char *> names) {
  for (const char *name : names) {
    if (const RegisterInfo *info = reg_ctx.GetRegisterInfoByName(name))
      return info;
  }
  return nullptr;
}

Status ABISysV_haydn::SetReturnValueObject(StackFrameSP &frame_sp,
                                           ValueObjectSP &new_value_sp) {
  // Peer: ABISysV_riscv.cpp SetReturnValueObject writes integer/pointer
  // returns through GENERIC_ARG1 (and ARG2 for a GPR pair). Haydn overlay:
  // ReturnRegNameForBytes: R1 for <=4 bytes and D0 for 8 bytes — never a
  // GPR pair. Larger values stay sret / unsupported.
  Status result;
  if (!new_value_sp) {
    result = Status::FromErrorString("Empty value object for return value.");
    return result;
  }
  if (!frame_sp) {
    result = Status::FromErrorString("Null stack frame for return value.");
    return result;
  }

  CompilerType compiler_type = new_value_sp->GetCompilerType();
  if (!compiler_type) {
    result = Status::FromErrorString("Null compiler type for return value.");
    return result;
  }

  Thread *thread = frame_sp->GetThread().get();
  if (!thread || !thread->GetRegisterContext()) {
    result = Status::FromErrorString("Null register context for return value.");
    return result;
  }
  RegisterContext &reg_ctx = *thread->GetRegisterContext();

  bool is_signed = false;
  bool is_complex = false;
  const bool is_int = compiler_type.IsIntegerOrEnumerationType(is_signed);
  const bool is_ptr = compiler_type.IsPointerType();
  const bool is_float = compiler_type.IsFloatingPointType(is_complex);
  if (!is_int && !is_ptr && !is_float) {
    result = Status::FromErrorString(
        "Haydn return-value write supports integer, pointer, and float only");
    return result;
  }

  DataExtractor data;
  const size_t num_bytes = new_value_sp->GetData(data, result);
  if (result.Fail()) {
    result = Status::FromErrorStringWithFormat(
        "Couldn't convert return value to raw data: %s", result.AsCString());
    return result;
  }
  if (num_bytes == 0 || num_bytes > 8) {
    result = Status::FromErrorString(
        "Haydn return values larger than 8 bytes are sret / unsupported");
    return result;
  }

  offset_t offset = 0;
  const uint64_t raw_value = data.GetMaxU64(&offset, num_bytes);
  const RegisterInfo *reg_info = nullptr;
  if (num_bytes <= 4) {
    // i32 / pointer / f32 in R1 (R0 is soft-zero).
    reg_info =
        reg_ctx.GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1);
    if (!reg_info) {
      const char *r1_names[] = {"r1"};
      reg_info = find_reg_by_names(reg_ctx, r1_names);
    }
  } else {
    // i64 / f64 / 64-bit SIMD in D0. Not R1+R2.
    const char *dr_names[] = {"d0", "dr0"};
    reg_info = find_reg_by_names(reg_ctx, dr_names);
  }
  if (!reg_info || !reg_ctx.WriteRegisterFromUnsigned(reg_info, raw_value)) {
    result = Status::FromErrorString(
        "Couldn't write Haydn return value to r1/d0");
    return result;
  }
  return result;
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
  // Peer: ABISysV_hexagon.cpp CreateFunctionEntryUnwindPlan names RA.
  plan_sp->SetReturnAddressRegister(LLDB_REGNUM_GENERIC_RA);
  plan_sp->SetSourceName("haydn at-func-entry default");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  // Peer: ABISysV_riscv.cpp CreateFunctionEntryUnwindPlan:737-738.
  plan_sp->SetUnwindPlanForSignalTrap(eLazyBoolNo);
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
  // Peer: ABISysV_riscv.cpp CreateDefaultUnwindPlan:766.
  plan_sp->SetUnwindPlanForSignalTrap(eLazyBoolNo);
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

ABISysV_haydn::RegNamespaceClass
ABISysV_haydn::ClassifyRegNamespaces(llvm::StringRef name, uint32_t rsp,
                                     uint32_t dwarf, bool has_rsp,
                                     bool has_dwarf) {
  // Twin of llvm/utils/haydn/classify_lldb_step.py classify_reg_namespaces.
  // Preferred when advertised DWARF matches the compiler table, not RSP.
  const std::string lowered = name.lower();
  const llvm::StringRef key = lowered;
  uint32_t want_rsp = LLDB_INVALID_REGNUM;
  uint32_t want_dwarf = LLDB_INVALID_REGNUM;
  bool expect_rsp = true;
  bool expect_dwarf = true;

  if (key == "pc") {
    want_rsp = kRspPC;
    expect_dwarf = false;
  } else if (key == "ar0") {
    want_rsp = kRspAR0;
    want_dwarf = kDwarfAR0;
  } else if (key == "ar1") {
    want_rsp = kRspAR0 + 1;
    want_dwarf = kDwarfAR0 + 1;
  } else if (key == "ar2") {
    want_rsp = kRspAR0 + 2;
    expect_dwarf = false;
  } else if (key == "ar3") {
    want_rsp = kRspAR0 + 3;
    expect_dwarf = false;
  } else if (key == "sfr") {
    expect_rsp = false;
    want_dwarf = kDwarfSFR;
  } else if (key == "csr") {
    expect_rsp = false;
    want_dwarf = kDwarfCSR;
  } else if (key == "sp" || key == "r13") {
    want_rsp = 13;
    want_dwarf = 13;
  } else if (key == "fp" || key == "r14") {
    want_rsp = 14;
    want_dwarf = 14;
  } else if (key == "lr" || key == "r15") {
    want_rsp = 15;
    want_dwarf = 15;
  } else if (key.size() >= 2 && (key.starts_with("dr") ||
                                 (key.front() == 'd' && key[1] != 'r'))) {
    llvm::StringRef rest = key.starts_with("dr") ? key.drop_front(2)
                                                 : key.drop_front(1);
    unsigned idx = 0;
    if (rest.getAsInteger(10, idx) || !rest.empty() || idx > 15)
      return RegNamespaceClass::CollapsedResidual;
    want_rsp = kRspDR0 + idx;
    want_dwarf = kDwarfD0 + idx;
  } else if (key.size() >= 2 && key.front() == 'r') {
    llvm::StringRef rest = key.drop_front(1);
    unsigned idx = 0;
    if (rest.getAsInteger(10, idx) || !rest.empty() || idx > 12)
      return RegNamespaceClass::CollapsedResidual;
    want_rsp = idx;
    want_dwarf = idx;
  } else {
    return RegNamespaceClass::CollapsedResidual;
  }

  if (has_rsp && expect_rsp && rsp != want_rsp)
    return RegNamespaceClass::CollapsedResidual;
  if (!expect_dwarf) {
    if (has_dwarf)
      return RegNamespaceClass::CollapsedResidual;
    return RegNamespaceClass::SplitOk;
  }
  if (!has_dwarf || dwarf != want_dwarf)
    return RegNamespaceClass::CollapsedResidual;
  if (has_rsp && expect_rsp && rsp == dwarf && want_rsp != want_dwarf)
    return RegNamespaceClass::CollapsedResidual;
  return RegNamespaceClass::SplitOk;
}

uint32_t ABISysV_haydn::GenericNumForName(llvm::StringRef name) {
  // Peer: ABISysV_riscv.cpp:815 GetGenericNum (a0-a7). Haydn overlay: R1-R7.
  return llvm::StringSwitch<uint32_t>(name)
      .Case("pc", LLDB_REGNUM_GENERIC_PC)
      .Cases({"lr", "r15"}, LLDB_REGNUM_GENERIC_RA)
      .Cases({"sp", "r13"}, LLDB_REGNUM_GENERIC_SP)
      .Cases({"fp", "r14"}, LLDB_REGNUM_GENERIC_FP)
      .Case("r1", LLDB_REGNUM_GENERIC_ARG1)
      .Case("r2", LLDB_REGNUM_GENERIC_ARG2)
      .Case("r3", LLDB_REGNUM_GENERIC_ARG3)
      .Case("r4", LLDB_REGNUM_GENERIC_ARG4)
      .Case("r5", LLDB_REGNUM_GENERIC_ARG5)
      .Case("r6", LLDB_REGNUM_GENERIC_ARG6)
      .Case("r7", LLDB_REGNUM_GENERIC_ARG7)
      .Default(LLDB_INVALID_REGNUM);
}

static llvm::StringRef AltNameFor(llvm::StringRef name) {
  if (name == "sp")
    return "r13";
  if (name == "r13")
    return "sp";
  if (name == "fp")
    return "r14";
  if (name == "r14")
    return "fp";
  if (name == "lr")
    return "r15";
  if (name == "r15")
    return "lr";
  if (name.size() >= 2 && name.front() == 'd' && name[1] != 'r') {
    unsigned idx = 0;
    if (!name.drop_front(1).getAsInteger(10, idx) && idx <= 15)
      return llvm::StringSwitch<llvm::StringRef>(name)
          .Case("d0", "dr0")
          .Case("d1", "dr1")
          .Case("d2", "dr2")
          .Case("d3", "dr3")
          .Case("d4", "dr4")
          .Case("d5", "dr5")
          .Case("d6", "dr6")
          .Case("d7", "dr7")
          .Case("d8", "dr8")
          .Case("d9", "dr9")
          .Case("d10", "dr10")
          .Case("d11", "dr11")
          .Case("d12", "dr12")
          .Case("d13", "dr13")
          .Case("d14", "dr14")
          .Case("d15", "dr15")
          .Default("");
  }
  if (name.starts_with("dr")) {
    return llvm::StringSwitch<llvm::StringRef>(name)
        .Case("dr0", "d0")
        .Case("dr1", "d1")
        .Case("dr2", "d2")
        .Case("dr3", "d3")
        .Case("dr4", "d4")
        .Case("dr5", "d5")
        .Case("dr6", "d6")
        .Case("dr7", "d7")
        .Case("dr8", "d8")
        .Case("dr9", "d9")
        .Case("dr10", "d10")
        .Case("dr11", "d11")
        .Case("dr12", "d12")
        .Case("dr13", "d13")
        .Case("dr14", "d14")
        .Case("dr15", "d15")
        .Default("");
  }
  return "";
}

void ABISysV_haydn::AugmentRegisterInfo(
    std::vector<DynamicRegisterInfo::Register> &regs) {
  // Peer: ABISysV_riscv.cpp:832 and ABISysV_loongarch.cpp:639.
  // Parent fills missing dwarf/ehframe/generic from the ABI table by name.
  // Overlay: (1) dN/drN and r13/sp alts (2) GENERIC_ARG2-7 — remote only
  // marks r1 as arg1 (3) compiler DWARF wins so RSP index cannot collapse
  // onto D0=16 / AR0=32.
  RegInfoBasedABI::AugmentRegisterInfo(regs);

  for (DynamicRegisterInfo::Register &info : regs) {
    const llvm::StringRef name = info.name.GetStringRef();
    const llvm::StringRef alt = info.alt_name.GetStringRef();

    if (info.alt_name.IsEmpty()) {
      const llvm::StringRef mapped = AltNameFor(name);
      if (!mapped.empty())
        info.alt_name.SetString(mapped);
    }

    uint32_t generic = GenericNumForName(name);
    if (generic == LLDB_INVALID_REGNUM)
      generic = GenericNumForName(alt);
    if (generic != LLDB_INVALID_REGNUM)
      info.regnum_generic = generic;

    RegisterInfo abi_info;
    const bool found = GetRegisterInfoByName(name, abi_info) ||
                       (!alt.empty() && GetRegisterInfoByName(alt, abi_info));
    if (!found)
      continue;

    const uint32_t abi_dwarf = abi_info.kinds[eRegisterKindDWARF];
    const uint32_t abi_eh = abi_info.kinds[eRegisterKindEHFrame];
    // Compiler table is the DWARF manifest. Empty ABI dwarf (PC, AR2/AR3,
    // HWLR/CBR halves) stays invalid so RSP 16 cannot alias D0.
    info.regnum_dwarf = abi_dwarf;
    info.regnum_ehframe = abi_eh;
  }
}

void ABISysV_haydn::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "Baremetal SysV-style ABI for Haydn targets",
                                CreateInstance);
}

void ABISysV_haydn::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}
