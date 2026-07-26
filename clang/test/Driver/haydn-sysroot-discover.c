// UNSUPPORTED: system-windows
//
// G-SYSROOT: when bin/../sysroot/haydn-unknown-elf exists, the baremetal
// driver prefers it over empty clang-runtimes (BundleSim install layout:
// include/ + lib/libc.a [+ optional lib/crt0.o from board BSP]).

// RUN: rm -rf %t.root && mkdir -p %t.root/bin %t.root/sysroot/haydn-unknown-elf/include %t.root/sysroot/haydn-unknown-elf/lib
// RUN: touch %t.root/sysroot/haydn-unknown-elf/lib/libc.a
// RUN: touch %t.root/sysroot/haydn-unknown-elf/lib/crt0.o
// RUN: ln -sf %clang %t.root/bin/clang
// RUN: %t.root/bin/clang -### -target haydn-unknown-elf -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SYS
// SYS: "-internal-isystem" "{{.*}}/sysroot/haydn-unknown-elf/include"

// Explicit --sysroot still wins.
// RUN: %t.root/bin/clang -### -target haydn-unknown-elf --sysroot=%t.root/sysroot/haydn-unknown-elf -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=EXPLICIT
// EXPLICIT: "-internal-isystem" "{{.*}}/sysroot/haydn-unknown-elf/include"

// Optional board crt0 staged at $sysroot/lib/crt0.o is on the link line.
// RUN: %t.root/bin/clang -### -target haydn-unknown-elf %s -o %t.out 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CRT0
// CRT0: "{{.*}}/sysroot/haydn-unknown-elf/lib{{/|\\\\}}crt0.o"

void f(void) {}
