//===-- HaydnMCCodeEmitter.h - Haydn Code Emitter interface -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn Format E code emitter (implementation lives in the .cpp, same
// TU-local class shape as RISCVMCCodeEmitter.cpp:39). Isolation wall:
//   * TargetOpcode::BUNDLE residuals with public logicals fatal in
//     encodeBundle (skip-Finalize never DFS/fill).
//   * Committed MemberId BUNDLE_E96_* serialize as-is
//     (trySerializeFormatECompositeAsIs / encodeInstructionFromCompilerRoot);
//     never enter standalone DFS.
//   * encodeSlotSubInst is serialize-only (generated members / NOP).
//   * fillFormatEMemberInstFromCompilerRoot always fails closed (no bag-sort).
//   * fillFormatEMemberInstFromRawBundle is standalone/hand-asm only.
//     Residual FieldSlot never enters fill. Compiler extra-op
//     (MOVE32 3-op vs member 2-op) never enters fill.
// Peer: AIEBaseMCCodeEmitter.cpp:45-68 serializes typed members as-is;
// HexagonMCCodeEmitter.h:34-54 keeps the emitter class in the header,
// but Haydn's TableGen include stays in the .cpp (RISCV peer).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCCODEEMITTER_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCCODEEMITTER_H

namespace llvm {
class MCCodeEmitter;
class MCInstrInfo;
class MCContext;

MCCodeEmitter *createHaydnMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCCODEEMITTER_H
