// UNSUPPORTED: system-windows
//
// Regression test for D179: the Clang driver must check runtime-artifact
// EXISTENCE (llvm::sys::fs::exists on the resolved path), not
// GetFilePath().empty(). ToolChain::GetFilePath returns the UNRESOLVED
// basename when no file is found, so the old `!GetFilePath(name).empty()`
// guard was ALWAYS true, causing `-Thaydn.ld` / `libhaydn.o` to be passed
// even when those files do not exist -> link fails with
// "ld.lld: error: cannot find linker script haydn.ld".
//
// The driver must SILENTLY OMIT a missing runtime artifact (so an
// out-of-tree/standalone runtime supplied via -L still links) and only add it
// when the file actually exists on disk.

// -----------------------------------------------------------------------------
// Case 1: runtime artifacts ABSENT from the resource dir.
//   The driver must NOT emit `-Thaydn.ld` or `libhaydn.o` on the link line,
//   and must not error.
// -----------------------------------------------------------------------------
// RUN: rm -rf %t.absent && mkdir -p %t.absent/lib
// RUN: %clang -### %s --target=haydn-unknown-elf -resource-dir=%t.absent 2>&1 \
// RUN:   | FileCheck --check-prefix=ABSENT %s
// ABSENT-NOT: "-T{{.*}}haydn.ld"
// ABSENT-NOT: "libhaydn.o"

// -----------------------------------------------------------------------------
// Case 2: runtime artifacts PRESENT in the resource dir's lib/ subdir.
//   The driver must emit `-T<path>/haydn.ld` and `<path>/libhaydn.o`.
// -----------------------------------------------------------------------------
// RUN: rm -rf %t.present && mkdir -p %t.present/lib
// RUN: touch %t.present/lib/haydn.ld %t.present/lib/libhaydn.o
// RUN: %clang -### %s --target=haydn-unknown-elf -resource-dir=%t.present 2>&1 \
// RUN:   | FileCheck --check-prefix=PRESENT %s
// PRESENT:      "-T{{.*}}haydn.ld"
// PRESENT-SAME: "libhaydn.o"
