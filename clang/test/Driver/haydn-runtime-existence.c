// UNSUPPORTED: system-windows
//
// Haydn no longer auto-injects a private libhaydn / haydn.ld runtime.
// Link defaults match other baremetal targets (compiler-rt + -lc from
// llvm-libc when present). The driver must not pass those private artifacts
// even if files with the old names happen to exist in the resource dir.

// RUN: rm -rf %t.present && mkdir -p %t.present/lib
// RUN: touch %t.present/lib/haydn.ld %t.present/lib/libhaydn.a %t.present/lib/libhaydn.o
// RUN: %clang -### %s --target=haydn-unknown-elf -resource-dir=%t.present 2>&1 \
// RUN:   | FileCheck %s
// CHECK-NOT: "-T{{.*}}haydn.ld"
// CHECK-NOT: "libhaydn.a"
// CHECK-NOT: "libhaydn.o"

void f(void) {}
