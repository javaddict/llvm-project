//===-- ABISysV_haydn.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Baremetal / freestanding Haydn ILP32 ABI for LLDB (BundleSim gdb-remote).
// DWARF names/order: R0–R15, D0–D15, AR0/AR1, SFR, CSR (HaydnRegisterInfo.td).
// Remote RSP is a different namespace (PC at 16, AR, then DR); qRegisterInfo /
// target.xml must advertise compiler DWARF numbers, not RSP indices.
// Returns: i32/ptr in R1, i64/f64/64-bit SIMD in D0 (RetCC_Haydn).
// Callee-saved: R8–R11, FP/R14, LR/R15, SP, D8–D15.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ABI_HAYDN_ABISYSV_HAYDN_H
#define LLDB_SOURCE_PLUGINS_ABI_HAYDN_ABISYSV_HAYDN_H

#include "lldb/Target/ABI.h"
#include "lldb/lldb-private.h"
#include "llvm/ADT/StringRef.h"

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
    // MinInstAlignment is 2. Product step is a 12-byte Format E parcel;
    // debug locations may land on the 2-byte architectural PC grid.
    return (pc & 0x1) == 0;
  }

  // Peer: ABISysV_arm.h FixCodeAddress strips bit 0 (Thumb mode bit).
  // Haydn has no mode bit; the same mask keeps PCs on MinInstAlignment=2.
  lldb::addr_t FixCodeAddress(lldb::addr_t pc) override {
    return pc & ~(lldb::addr_t)1;
  }

  // Format E product step unit. Same-PC / non-12 deltas are residual.
  // Peer: ABISysV_arm.h FixCodeAddress (mode-bit mask); LoongArch
  // AutoAdvancePC size overlay. Parcel ±12 is preferred, never qualified.
  static constexpr lldb::addr_t kFormatEParcelBytes = 12;
  static constexpr bool kStepNeverQualified = false;
  // Integer/pointer argument bank is R1-R7 (RetCC/CC_Haydn). Peer: RISC-V a0-a7.
  static constexpr size_t kGPRArgumentCount = 7;

  // BundleSim RSP process-plugin indices (k_reg_meta order). Distinct from
  // compiler DWARF: PC RSP 16 is not D0 DWARF 16; DR RSP is 21+i / DWARF 16+i.
  static constexpr uint32_t kRspPC = 16;
  static constexpr uint32_t kRspAR0 = 17;
  static constexpr uint32_t kRspDR0 = 21;
  static constexpr uint32_t kDwarfD0 = 16;
  static constexpr uint32_t kDwarfAR0 = 32;
  static constexpr uint32_t kDwarfSFR = 36;
  static constexpr uint32_t kDwarfCSR = 37;
  static constexpr uint32_t kDwarfCBR0 = 38;
  static constexpr uint32_t kDwarfCBR1 = 39;

  enum class StepDeltaClass { Parcel12, SamePCResidual, NonParcelResidual };

  struct StepDeltaReport {
    StepDeltaClass Class = StepDeltaClass::NonParcelResidual;
    lldb::addr_t FromPC = 0;
    lldb::addr_t ToPC = 0;
    int64_t Delta = 0;
    bool Qualified = kStepNeverQualified;
    bool SemanticQualify = false;
  };

  static StepDeltaClass ClassifyStepDelta(lldb::addr_t from_pc,
                                         lldb::addr_t to_pc) {
    const lldb::addr_t delta =
        to_pc > from_pc ? to_pc - from_pc : from_pc - to_pc;
    if (from_pc == to_pc)
      return StepDeltaClass::SamePCResidual;
    if (delta == kFormatEParcelBytes)
      return StepDeltaClass::Parcel12;
    return StepDeltaClass::NonParcelResidual;
  }

  static llvm::StringRef StepDeltaClassName(StepDeltaClass C) {
    switch (C) {
    case StepDeltaClass::Parcel12:
      return "parcel12";
    case StepDeltaClass::SamePCResidual:
      return "same-pc-residual";
    case StepDeltaClass::NonParcelResidual:
      return "non12-residual";
    }
    return "non12-residual";
  }

  static StepDeltaReport ClassifyStepReport(lldb::addr_t from_pc,
                                           lldb::addr_t to_pc) {
    StepDeltaReport R;
    R.FromPC = from_pc;
    R.ToPC = to_pc;
    if (to_pc >= from_pc)
      R.Delta = static_cast<int64_t>(to_pc - from_pc);
    else
      R.Delta = -static_cast<int64_t>(from_pc - to_pc);
    R.Class = ClassifyStepDelta(from_pc, to_pc);
    R.Qualified = kStepNeverQualified;
    R.SemanticQualify = false;
    return R;
  }

  // RetCC_Haydn: <=4 in r1, 8 in d0, else sret / unsupported. Peer:
  // ABISysV_riscv.cpp SetReturnValueObject (ARG1; Haydn never a GPR pair).
  static llvm::StringRef ReturnRegNameForBytes(size_t num_bytes) {
    if (num_bytes == 0)
      return "unsupported";
    if (num_bytes <= 4)
      return "r1";
    if (num_bytes == 8)
      return "d0";
    return "sret";
  }

  enum class RegNamespaceClass { SplitOk, CollapsedResidual };

  // Compiler DWARF vs BundleSim RSP. Collapsing RSP 16 onto D0=16 is residual.
  static RegNamespaceClass ClassifyRegNamespaces(llvm::StringRef name,
                                                 uint32_t rsp,
                                                 uint32_t dwarf,
                                                 bool has_rsp, bool has_dwarf);

  bool GetPointerReturnRegister(const char *&name) override;

  bool GetFallbackRegisterLocation(
      const lldb_private::RegisterInfo *reg_info,
      lldb_private::UnwindPlan::Row::AbstractRegisterLocation &unwind_regloc)
      override;

  const lldb_private::RegisterInfo *
  GetRegisterInfoArray(uint32_t &count) override;

  // Peer: ABISysV_riscv.cpp GetGenericNum / AugmentRegisterInfo. Overlay
  // Haydn names (r1-r7 args, dN/drN alt, compiler DWARF over RSP collapse).
  static uint32_t GenericNumForName(llvm::StringRef name);

protected:
  void AugmentRegisterInfo(
      std::vector<lldb_private::DynamicRegisterInfo::Register> &regs) override;

public:
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
