//===- HaydnMemberSetDesc.h - logical/FieldSlot → Format E setDesc -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// One keep-map rewrite for setDesc onto a generated Format E member.
// Finalize and post-RA materialize share this so raw setDesc cannot leave
// extra ties (MAC acc) or vestigial uses (MOVE32 rs2).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNMEMBERSETDESC_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNMEMBERSETDESC_H

#include "llvm/ADT/StringRef.h"

namespace llvm {

class MachineInstr;
class TargetInstrInfo;

/// True when \p Name is a generated Format E private member.
inline bool isGeneratedFormatEMemberName(StringRef Name) {
  return Name.contains("_E2_") || Name.contains("_E3_");
}

/// True when FieldSlot/logical explicit operands have a keep-map onto
/// \p MemberOpc (same closed drop rules as Finalize cutover).
/// Reloc CSRW_W uses the 2-op identity/swap map; kindOk accepts a
/// Global/Symbol on the uimm8 slot. Generated CSR I8 members encode that
/// slot with FIXUP_HAYDN_CSR_UImm8 / R_HAYDN_CSR_UImm8 (uimm8
/// EncoderMethod), never untyped NONE.
bool memberDescCompatible(const MachineInstr &MI, unsigned MemberOpc,
                          const TargetInstrInfo &TII);

/// setDesc to \p MemberOpc and drop operands the member does not keep.
/// Caller already proved memberDescCompatible.
void rewriteFieldSlotToMember(MachineInstr &MI, unsigned MemberOpc,
                              const TargetInstrInfo &TII);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNMEMBERSETDESC_H
