//===-- HaydnMCTargetDesc.h - Haydn Target Descriptions -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides Haydn specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCTARGETDESC_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCTARGETDESC_H

#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCRegisterInfo;
class MCObjectTargetWriter;
class MCSubtargetInfo;
class Target;
class Triple;

/// Process-wide MCInstrInfo for occupancy / Format E setDesc shape queries.
/// Init lives in this TU (GET_INSTRINFO_MC_DESC is not includable twice).
const MCInstrInfo &getHaydnSharedMCInstrInfo();

std::unique_ptr<MCObjectTargetWriter> createHaydnELFObjectWriter();

MCAsmBackend *createHaydnAsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                     const MCRegisterInfo &MRI,
                                     const MCTargetOptions &Options);

MCCodeEmitter *createHaydnMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx);
} // namespace llvm

#define GET_REGINFO_ENUM
#include "HaydnGenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "HaydnGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "HaydnGenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCTARGETDESC_H
