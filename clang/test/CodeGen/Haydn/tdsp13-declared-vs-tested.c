// RUN: python3 %S/tdsp13-declared-vs-tested.py \
// RUN:   %S/../../../../llvm/include/llvm/IR/IntrinsicsHaydn.td \
// RUN:   %S/../../../include/clang/Basic/BuiltinsHaydn.td \
// RUN:   %S/../../../../llvm/test/CodeGen/Haydn
//
// REQUIRES: haydn-registered-target
//
// T-DSP13: set-difference pin of IntrinsicsHaydn.td vs CodeGen/Haydn
// plus PublicEnabled ⇒ Semantics publish-gate. Empty-Semantics leftover
// set is closed. This test does not author a 746-name harness.

// Dummy TU so lit has a source file. The RUN line is the pin.
void tdsp13_declared_vs_tested_pin(void) {}
