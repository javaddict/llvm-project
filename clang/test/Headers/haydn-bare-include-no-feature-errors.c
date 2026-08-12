// RUN: %clang_cc1 -triple haydn-unknown-elf -ffreestanding -emit-llvm \
// RUN:     -o /dev/null -verify=bare %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding \
// RUN:     -emit-llvm -o /dev/null -verify=full %s
//
// -emit-llvm, not -fsyntax-only: the call-site diagnostic is emitted by
// CodeGen when it tries to inline an always_inline function whose target
// features the caller lacks, so a syntax-only run sees nothing and the test
// would pass for the wrong reason.
//
// REQUIRES: haydn-registered-target
//
// CB-136: `#include <haydn.h>` on its own must compile.
//
// The default Haydn feature set is `+agu,+hwloop,-bit-reversed,
// -circular-buffer,-simd`, and 168 wrapper BODIES in the header call builtins
// that need one of the absent three. The bodies were unguarded, so the include
// failed with 159 "needs target feature" errors before the user had written
// any code — a header you cannot include is not an API. The workaround was
// consumer-side: BundleSim's run_c passes -mcpu=haydn, which turns the
// features on and hides it.
//
// The wrappers now carry `__attribute__((target(...)))` from the op's own
// TableGen Features expression, the way immintrin.h does. That moves the
// diagnostic from the DEFINITION to the CALL, which is where a user can act
// on it: the message below names the function and the feature it wants.

#include <haydn.h>

// The bare run has no blanket "no diagnostics" marker on purpose: the include
// is silent, and the point of that run is that the CALL is what complains.
//
// Not spelling that marker out is also deliberate. -verify reads its
// directives out of comments, so writing one in a sentence ABOUT it makes it
// real — which is exactly what happened on the first draft of this file, and
// it is the same trap as FORMAT-E-SWITCH-PLAN.md 6.16.

haydn_x2int32 use_simd(haydn_x2int32 a) {
  // bare-error@+1 {{always_inline function 'haydn_x2abs32' requires target feature 'simd'}}
  return haydn_x2abs32(a);
}

// Only ONE gated call is asserted in the bare run. CodeGen stops after the
// first always_inline-feature error, so a second would be "expected but not
// seen" — the two features are both exercised by the -target-cpu run below,
// where all of this compiles.
int use_brev(int a, int b) {
#ifdef __HAYDN_FEATURE_BIT_REVERSED__
  return haydn_brev32(a, b);
#else
  return a + b;
#endif
}

// full-no-diagnostics
