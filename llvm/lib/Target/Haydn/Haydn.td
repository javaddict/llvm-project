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
// HaydnInstrInfoManual.td is included from HaydnGeneric.td. The filename is
// historical; generate_format_e_records.py does not emit it. Treat that
// file as hand-maintained hypothesized encodings (isCodeGenOnly quarantine).

//===----------------------------------------------------------------------===//
// CodeGenFormat backend : product Format E entry model only (FE8).
// gen-instr-format emits HaydnGenFormats.inc (slot enum + MCFormatDesc
// PacketFormats tables). Product PacketFormats rows are Format E Size=12
// (BUNDLE_E96_TWO_ENTRY / BUNDLE_E96_THREE_ENTRY). Retired 128-bit composite
// opcode deleted. Backend filters to InstFormat-derived instructions; legacy
// HaydnInst defs are skipped.
// Residual `_S*` families are occupancy / setDesc placement tables, not a
// product alternate source (suffix name discovery fails closed at TableGen).
//===----------------------------------------------------------------------===//
include "llvm/Target/CodeGenFormat.td"
include "HaydnSlots.td"
// Residual `_S*` FU member families (placement tables; not product composite).
include "HaydnFormatsALU32.td"
include "HaydnFormatsALU64.td"
include "HaydnFormatsLS.td"
include "HaydnFormatsMAC.td"
// Format E entry bases (product placement scaffolding).
include "HaydnFormatsE96.td"
// MultiSlot_Pseudo / materializableInto (sparse AlternateInsts for residual
// `_S*` members; Format E alts come from generated records).
include "HaydnMultiSlotPseudo.td"
// Product Format E composites only (Size=12).
include "HaydnCompositeFormats.td"
def HaydnCodeGenFormat : CodeGenFormat {}

//===----------------------------------------------------------------------===//
// Mode-0/3 decode-variant islands and Bundle128 composites deleted (FE8).
// Product composites are Format E 12-byte only (HaydnCompositeFormats.td).
// Residual `_S<k>` families remain as Format E placement/setDesc members.
//===----------------------------------------------------------------------===//
