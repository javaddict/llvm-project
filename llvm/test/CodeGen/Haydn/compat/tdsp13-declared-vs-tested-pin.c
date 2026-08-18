// REQUIRES: haydn-registered-target
// RUN: python3 %S/../../../../utils/haydn/tdsp13_declared_vs_tested_pin.py \
// RUN:   --llvm-src %S/../../../../../
// RUN: python3 %S/../../../../utils/haydn/tdsp13_declared_vs_tested_pin.py \
// RUN:   --self-test
//
// T-DSP13 declared-vs-tested inventory pin. Names LS pre/post-inc + sat
// ALU64 residual and leftover empty-Semantics set. Does not author a
// 746-name harness or QUALIFY.

void tdsp13_declared_vs_tested_pin(void) {}
