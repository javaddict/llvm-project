// UNSUPPORTED: system-windows
//
// Regression test for D179: the Clang driver must check runtime-artifact
// EXISTENCE (llvm::sys::fs::exists on the resolved path), not
// GetFilePath().empty(). ToolChain::GetFilePath returns the UNRESOLVED
// basename when no file is found, so the old `!GetFilePath(name).empty()`
// guard was ALWAYS true, causing `-Thaydn.ld` / `libhaydn.a` to be passed
// even when those files do not exist -> link fails with
// "ld.lld: error: cannot find linker script haydn.ld".
//
// The driver must SILENTLY OMIT a missing runtime artifact (so an
// out-of-tree/standalone runtime supplied via -L still links) and only add it
// when the file actually exists on disk.

// -----------------------------------------------------------------------------
// Case 1: runtime artifacts ABSENT from the resource dir.
//   The driver must NOT emit `-Thaydn.ld` or `libhaydn.a` on the link line,
//   and must not error.
// -----------------------------------------------------------------------------
// RUN: rm -rf %t.absent && mkdir -p %t.absent/lib
// RUN: %clang -### %s --target=haydn-unknown-elf -resource-dir=%t.absent 2>&1 \
// RUN:   | FileCheck --check-prefix=ABSENT %s
// ABSENT-NOT: "-T{{.*}}haydn.ld"
// ABSENT-NOT: libhaydn.a

// -----------------------------------------------------------------------------
// Case 2: runtime artifacts PRESENT in the resource dir.
//   ToolChain::GetFilePath resolves bare names against the resource dir root
//   (<resource-dir>/<name>), so place the artifacts there. The driver must
//   emit `-T<path>/haydn.ld` and `<path>/libhaydn.a`.
// -----------------------------------------------------------------------------
// RUN: rm -rf %t.present && mkdir -p %t.present
// RUN: touch %t.present/haydn.ld %t.present/libhaydn.a
// RUN: %clang -### %s --target=haydn-unknown-elf -resource-dir=%t.present 2>&1 \
// RUN:   | FileCheck --check-prefix=PRESENT %s
// PRESENT:      "-T{{.*}}haydn.ld"
// PRESENT-SAME: "{{.*}}libhaydn.a"
