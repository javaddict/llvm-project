//===-- Haydn.td - Describe the Haydn Target Machine -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Top-level.td for every backend EXCEPT -gen-asm-matcher:
// gen-disassembler (decoder tables — variants REQUIRED)
// gen-asm-writer (objdump prints variant MCInsts — variants REQUIRED)
// gen-instr-info (opcode enum — Haydn::X2MULA32_M0S1 etc. — REQUIRED)
// gen-emitter (encoder — variants REQUIRED)
// gen-callingconv / -gen-register-info / -gen-subtarget / -gen-global-isel
// (don't care, but get the full set for consistency)
//
// gen-asm-matcher uses HaydnAsmMatcher.td (generic only — see that file's
// header for the asm-hijack rationale). CMakeLists.txt sets
// LLVM_TARGET_DEFINITIONS per-call.
//
// The generic content (everything except the 6 decode-variant files below)
// lives in HaydnGeneric.td, shared with HaydnAsmMatcher.td.
//
//===----------------------------------------------------------------------===//

include "HaydnGeneric.td"

//===----------------------------------------------------------------------===//
// CodeGenFormat backend : product Format E entry model only (FE8).
// gen-instr-format emits HaydnGenFormats.inc (slot enum + MCFormatDesc
// PacketFormats tables). Product PacketFormats rows are Format E Size=12
// (BUNDLE_E96_TWO_ENTRY / BUNDLE_E96_THREE_ENTRY). Retired 128-bit composite
// opcode deleted. Backend filters to InstFormat-derived instructions; legacy
// HaydnInst defs are skipped. Residual `_S*` FieldSlot families are deleted.
//===----------------------------------------------------------------------===//
include "llvm/Target/CodeGenFormat.td"
include "HaydnSlots.td"
include "HaydnFormatE.td"
include "HaydnFamilies.td"
include "HaydnMultiSlotPseudo.td"
def HaydnCodeGenFormat : CodeGenFormat {}
