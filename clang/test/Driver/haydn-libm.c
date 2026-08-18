// UNSUPPORTED: system-windows
//
// Haydn link geometry is Format E 12-byte parcels. AIE AIE.cpp:36 passes
// --nmagic so the default 4 KiB page pack cannot leave 4096 % 12 == 4.
// Matching-sysroot -lm is added only when libm.a is present (AIE.cpp:42-43
// -lc then -lm; BareMetal already emits -lc). No private haydn.ld inject.

// RUN: rm -rf %t && mkdir -p %t/lib
// RUN: touch %t/lib/libm.a
// RUN: %clang -### --target=haydn-unknown-elf --sysroot=%t %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LIBM
// LIBM: "--nmagic"
// LIBM: "-lm"

// RUN: %clang -### --target=haydn-unknown-elf --sysroot=%t -nostdlib %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NOSTDLIB
// NOSTDLIB: "--nmagic"
// NOSTDLIB-NOT: "-lm"

// RUN: %clang -### --target=haydn-unknown-elf --sysroot=%t.missing %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NOLIBM
// NOLIBM: "--nmagic"
// NOLIBM-NOT: "-lm"

void f(void) {}
