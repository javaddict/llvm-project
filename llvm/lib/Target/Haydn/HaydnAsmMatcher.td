//===-- HaydnAsmMatcher.td - AsmMatcher-only root -*- tablegen -*-===//
//
// Used EXCLUSIVELY by -gen-asm-matcher (CMakeLists.txt sets
// LLVM_TARGET_DEFINITIONS=HaydnAsmMatcher.td for that one tablegen call).
// Every other backend uses Haydn.td.
//
// Matcher root is HaydnGeneric.td only: public logicals, no Format E
// composites (96-bit Inst would force the APInt getBinaryCodeForInst
// path), no residual `_S*` FieldSlots (AsmVariantName fieldslot occupancy
// retired). Product encode/decode uses Haydn.td members + composites.
//
//===----------------------------------------------------------------------===//

include "HaydnGeneric.td"
