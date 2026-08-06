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
// CodeGenFormat backend : 128-bit-only Bundle128 slot model.
// gen-instr-format emits HaydnGenFormats.inc (slot enum + MCFormatDesc
// PacketFormats tables) consumed by the migrated emitter/decoder. The backend
// filters to InstFormat-derived instructions (CodeGenFormat.cpp), so the legacy
// HaydnInst defs below are skipped until each is migrated to HaydnFormatInst.
//===----------------------------------------------------------------------===//
include "llvm/Target/CodeGenFormat.td"
include "HaydnSlots.td"
// P1b-scale : per-FU representative slot formats, byte-accurate vs
// HaydnFlexLayout. Each establishes the FU x slot format-class template; the
// full families follow these patterns. Additive alongside legacy defs.
include "HaydnFormatEEncoding.td" // generated: <logical>_P<form><pos>_<unit>
// B2.1: MultiSlot_Pseudo / materializableInto class (AIE2InstrFormats.td:21-31).
// After slot-format members so list<HaydnFormatInst> can name *_S0/*_S1/*_S2.
// Explicit MultiSlot_Pseudo defs are optional; CodeGenFormat also synthesizes
// GET_ALTERNATE_INST_OPCODE_FUNC from Full-format `_S*` members via
// collectSparseAltSlotMemberRows (sparse AlternateInsts slot-member discovery).
include "HaydnMultiSlotPseudo.td"
// P4: re-include the 128-bit Bundle128 composite. This forces
// gen-emitter into the APInt getBinaryCodeForInstr signature (max Inst width
// 128 > 64) and -gen-instr-format to emit the composite MCFormatDesc. The
// HaydnMCCodeEmitter cutover (P2b) lands together with this re-include: the
// encoder adopts the AIE two-step APInt model + 24-line encodeInstruction.
// Format E's composites, split out for the same reason Bundle128's were: a
// composite AsmString opens with an operand and -gen-asm-matcher rejects that,
// so HaydnAsmMatcher.td includes the members above and stops there.
//
// Bundle128's members and format E's could never be included together: one
// indexes alternates by slot, the other by (entry position, unit), and the two
// spaces overlap at 0/1/2. CodeGenFormat rejects that collision rather than
// dropping a member, which is why the two had to change over atomically.
include "HaydnFormatEComposites.td"  // BUNDLE_E2 / BUNDLE_E3
def HaydnCodeGenFormat : CodeGenFormat {}

//===----------------------------------------------------------------------===//
// R4 + : Mode-0/3 decode-variant islands RETIRED (Bundle128-only).
// Slot encoding is exclusively the `_S<k>` families in HaydnFormats*.td
// + BUNDLE128_FULL. Former HaydnInstrInfoM0/MACM0/MACM1/M3/S*M0.td islands
// live in legacy-retired/ (historical reference only; NOT included).
//===----------------------------------------------------------------------===//
