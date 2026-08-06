//===-- HaydnAsmMatcher.td - AsmMatcher-only.td (generic, no variants) --===//
//
// Used EXCLUSIVELY by -gen-asm-matcher (CMakeLists.txt sets
// LLVM_TARGET_DEFINITIONS=HaydnAsmMatcher.td for that one tablegen call).
// Every other backend (-gen-disassembler, -gen-asm-writer, -gen-instr-info
// gen-emitter, etc.) uses Haydn.td.
//
// Historical note (earlier): Mode-0/3 decode-variant islands
// (HaydnInstrInfoM0/MACM0/M3/S*M0.td) used bare mnemonics that hijacked
// the DEFAULT asm-match namespace; the matcher.td deliberately excluded
// them. After /R4 + those islands are retired and live only under
// legacy-retired/. Live encoding is `_S<k>` + Bundle128.
//
// Pattern is the standard AMDGPU/X86 split (per-call
// LLVM_TARGET_DEFINITIONS) — compliant (no custom -gen-haydn-*).
//
//===----------------------------------------------------------------------===//

include "HaydnGeneric.td"

//===----------------------------------------------------------------------===//
// Flex format includes for asm-match. Phase 3 stripped
// `.sN` from AsmStrings (bare `add32` / `ld32` / `x2mula32` / …); per-slot
// defs share identical mnemonics. Slot is not in the mnemonic: member
// getSlotKind / Bundle placement (AIE shape). Ambiguous matchables: first
// match wins for standalone; decoder keeps all three slots (no isCodeGenOnly
// on S1/S2).
//
// This must name the same member set as Haydn.td. The matcher's MatchTable
// records member opcodes by name, so a root that still knows the Bundle128
// members while Haydn.td has moved to format E generates matches against
// opcodes the instruction enum no longer defines.
//===----------------------------------------------------------------------===//
include "llvm/Target/CodeGenFormat.td"
include "HaydnSlots.td"
include "HaydnFormatEEncoding.td" // generated: <logical>_P<form><pos>_<unit>
def HaydnCodeGenFormat : CodeGenFormat {}
