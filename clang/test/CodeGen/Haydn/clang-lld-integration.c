// UNSUPPORTED: system-windows
// REQUIRES: x86-registered-target

// M6 integration test: Clang compile + LLD link produces valid Haydn ELF.
// Tests the full pipeline: Clang frontend -> LLVM CodeGen (GlobalISel) -> MC -> LLD.
//
// This test verifies the M6 milestone requirement:
//   clang -target haydn-unknown-elf -c test.c && ld.lld -m haydn test.o -o test.elf
//
// The Haydn target is registered as an experimental target; we cross-compile
// from x86 host. The test uses -nostdlib because baremetal Haydn has no
// runtime libraries yet.

// Test 1: Compile to .o, then link with ld.lld directly
// RUN: %clang -target haydn-unknown-elf -mllvm -global-isel-abort=1 \
// RUN:   -c %s -o %t.o
// RUN: ld.lld %t.o -o %t.elf
// RUN: llvm-readobj -h %t.elf | FileCheck --check-prefix=HEADER %s

// HEADER: Class: 32-bit
// HEADER: Machine: 0x103

// Test 2: Single-step compile+link via Clang driver with -fuse-ld=lld
// RUN: %clang -target haydn-unknown-elf -mllvm -global-isel-abort=1 \
// RUN:   -fuse-ld=lld -nostdlib -e main %s -o %t2.elf
// RUN: llvm-readobj -h %t2.elf | FileCheck --check-prefix=HEADER %s

// Test 3: Verify .text section exists and is executable
// RUN: llvm-readobj -S %t2.elf | FileCheck --check-prefix=SECTIONS %s

// SECTIONS: Name: .text
// SECTIONS: SHF_ALLOC
// SECTIONS: SHF_EXECINSTR

int main(void) { return 0; }
