//===-- HaydnAsmMatcher.td - AsmMatcher-only.td (generic, no variants) --===//
//
// Used EXCLUSIVELY by -gen-asm-matcher (CMakeLists.txt sets
// LLVM_TARGET_DEFINITIONS=HaydnAsmMatcher.td for that one tablegen call).
// Every other backend (-gen-disassembler, -gen-asm-writer, -gen-instr-info
// gen-emitter, etc.) uses Haydn.td.
//
// Historical note: Mode-0/3 decode-variant islands
// (HaydnInstrInfoM0/MACM0/M3/S*M0.td) used bare mnemonics that hijacked
// the DEFAULT asm-match namespace; those islands are deleted. Live encoding
// is `_S<k>` placement + Format E composite.
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
// getSlotKind / Bundle placement (AIE shape). Residual `_S*` FieldSlots
// use AsmVariantName fieldslot and are not in the public matcher.
// Decoder keeps S0/S1/S2 tries. Public logical AsmStrings use the same
// user spelling as FieldSlots / assembler_mnemonic (MAC lane `_` → `.`).
//
// Mirrors the slot block in Haydn.td EXCEPT HaydnCompositeFormats.td is
// intentionally omitted: the Format E composite is wider than 64 bits and
// would force the matcher into the APInt getBinaryCodeForInst path, which
// is unnecessary for asm parsing and not wired into the matcher generator.
// Without the composite, the matcher stays on the uint64 path and recognizes
// every per-slot slot instruction.
//===----------------------------------------------------------------------===//
include "llvm/Target/CodeGenFormat.td"
include "HaydnSlots.td"
include "HaydnFormatsALU32.td"  // remaining WFI/NOP/*_W FieldSlots
include "HaydnFormatsALU64.td"  // residual MOVEI_H/L + SEXT32T64 FieldSlots
include "HaydnFormatsLS.td"     // LD32_S0 (LS s0, imm6)
include "HaydnFormatsLD.td"     // LD-unit format classes; FieldSlots retired
include "HaydnFormatsMAC.td"    // residual MAC FU classes (FieldSlots retired)
def HaydnCodeGenFormat : CodeGenFormat {}
