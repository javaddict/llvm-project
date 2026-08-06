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
// getSlotKind / Bundle placement (AIE shape). Ambiguous matchables: first
// match wins for standalone; decoder keeps all three slots (no isCodeGenOnly
// on S1/S2).
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
include "HaydnFormatsALU32.td"  // ADD32_S1 (ALU32 s1)
include "HaydnFormatsALU64.td"  // ADD64_S1 (ALU64 s1, DR64)
include "HaydnFormatsLS.td"     // LD32_S0 (LS s0, imm6)
include "HaydnFormatsLD.td"     // LD64_S1 (LD s1, DR64+GPR32 base)
include "HaydnFormatsMAC.td"    // X2MULA32_S2 (MAC s2, RRRR accum)
def HaydnCodeGenFormat : CodeGenFormat {}
