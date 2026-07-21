//===-- HaydnMCCodeEmitter.h - Haydn Code Emitter interface -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the HaydnMCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCCODEEMITTER_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCCODEEMITTER_H

namespace llvm {
class MCCodeEmitter;
class MCInstrInfo;
class MCContext;

// This is a stub declaration. The full implementation will be generated
// by TableGen when instruction definitions are added.
MCCodeEmitter *createHaydnMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCCODEEMITTER_H
