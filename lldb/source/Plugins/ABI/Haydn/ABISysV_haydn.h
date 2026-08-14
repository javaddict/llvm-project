//===-- ABISysV_haydn.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Baremetal / freestanding Haydn ILP32 ABI for LLDB (BundleSim gdb-remote).
// DWARF names/order: R0–R15, D0–D15, AR0/AR1 (HaydnRegisterInfo.td). Remote
// RSP may also expose PC/AR/DR/HWLR/CBR via qRegisterInfo / target.xml.
// Returns: i32/ptr in R1, i64/f64/64-bit SIMD in D0 (RetCC_Haydn).
// Callee-saved: R8–R11, FP/R14, LR/R15, SP, D8–D15.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ABI_HAYDN_ABISYSV_HAYDN_H
#define LLDB_SOURCE_PLUGINS_ABI_HAYDN_ABISYSV_HAYDN_H

#include "lldb/Target/ABI.h"
#include "lldb/lldb-private.h"

class ABISysV_haydn : public lldb_private::RegInfoBasedABI {
public:
  ~ABISysV_haydn() override = default;

  size_t GetRedZoneSize() const override;

  bool PrepareTrivialCall(lldb_private::Thread &thread, lldb::addr_t sp,
                          lldb::addr_t functionAddress,
                          lldb::addr_t returnAddress,
                          llvm::ArrayRef<lldb::addr_t> args) const override;

  bool GetArgumentValues(lldb_private::Thread &thread,
                         lldb_private::ValueList &values) const override;

  lldb_private::Status
  SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                       lldb::ValueObjectSP &new_value) override;

  lldb::ValueObjectSP
  GetReturnValueObjectImpl(lldb_private::Thread &thread,
                           lldb_private::CompilerType &type) const override;

  lldb::UnwindPlanSP CreateFunctionEntryUnwindPlan() override;

  lldb::UnwindPlanSP CreateDefaultUnwindPlan() override;

  bool RegisterIsVolatile(const lldb_private::RegisterInfo *reg_info) override;

  bool CallFrameAddressIsValid(lldb::addr_t cfa) override {
    // Haydn stack is 8-byte aligned; reject zero.
    if (cfa == 0 || (cfa & 0x7))
      return false;
    return true;
  }

  bool CodeAddressIsValid(lldb::addr_t pc) override {
    // Format E96 bundle addresses are 2-byte aligned at minimum.
    return (pc & 0x1) == 0;
  }

  bool GetPointerReturnRegister(const char *&name) override;

  bool GetFallbackRegisterLocation(
      const lldb_private::RegisterInfo *reg_info,
      lldb_private::UnwindPlan::Row::AbstractRegisterLocation &unwind_regloc)
      override;

  const lldb_private::RegisterInfo *
  GetRegisterInfoArray(uint32_t &count) override;

  // Static Functions

  static void Initialize();

  static void Terminate();

  static lldb::ABISP CreateInstance(lldb::ProcessSP process_sp,
                                    const lldb_private::ArchSpec &arch);

  static llvm::StringRef GetPluginNameStatic() { return "sysv-haydn"; }

  // PluginInterface protocol

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  bool RegisterIsCalleeSaved(const lldb_private::RegisterInfo *reg_info);

private:
  using lldb_private::RegInfoBasedABI::RegInfoBasedABI;
};

#endif // LLDB_SOURCE_PLUGINS_ABI_HAYDN_ABISYSV_HAYDN_H
