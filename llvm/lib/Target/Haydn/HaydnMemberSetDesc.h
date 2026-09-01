//===- HaydnMemberSetDesc.h - logical/FieldSlot → Format E setDesc -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// One keep-map rewrite for setDesc onto a generated Format E member.
// Post-RA materialize / hazard bake share this so raw setDesc cannot leave
// extra ties (MAC acc) or vestigial uses (MOVE32 rs2). FinalizeBundle is
// construction-only and does not call these.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNMEMBERSETDESC_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNMEMBERSETDESC_H

#include "HaydnFormatERecords.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {

class MachineInstr;
class TargetInstrInfo;

// isGeneratedFormatEMemberName moved to HaydnFormatERecords.h (W64 QW3):
// one canonical member-name test for CodeGen and MCTargetDesc — the
// MCTargetDesc clone had drifted to contains_insensitive. This header
// re-exports nothing; including HaydnFormatERecords.h makes the canonical
// inline visible to this header's users.

/// True when FieldSlot/logical explicit operands have a keep-map onto
/// \p MemberOpc. Reloc CSRW_W uses the 2-op identity/swap map; kindOk
/// accepts a Global/Symbol on the uimm8 slot. Generated CSR I8 members
/// carry the committed (row, entry, MemberId) plus TypeName I8 so encode
/// binds FIXUP_HAYDN_CSR_UImm8 / R_HAYDN_CSR_UImm8, never untyped NONE.
/// Mixed MemberId + leftover FieldSlot is not a keep-map success.
bool memberDescCompatible(const MachineInstr &MI, unsigned MemberOpc,
                          const TargetInstrInfo &TII);

/// setDesc to \p MemberOpc and drop operands the member does not keep.
/// Caller already proved memberDescCompatible. Scheduler/materialize bake
/// only — not FinalizeBundle.
void rewriteFieldSlotToMember(MachineInstr &MI, unsigned MemberOpc,
                              const TargetInstrInfo &TII);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNMEMBERSETDESC_H
