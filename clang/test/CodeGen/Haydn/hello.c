// REQUIRES: clang, haydn-registered-target
//
// Compile smoke for haydn-unknown-elf. Full link uses llvm-libc / compiler-rt
// from the configured sysroot (no private libhaydn auto-link).
//
// RUN: %clang -target haydn-unknown-elf -c %s -o %t.o
// RUN: llvm-readobj --file-headers %t.o | FileCheck %s --check-prefix=ELF
//
// ELF:      Format: elf32-unknown
// ELF:      Arch: haydn
// ELF:      Machine: 0x103

// Minimal program that exercises scalar ALU.
int main(void) {
  volatile int a = 4;
  volatile int b = 5;
  return a + b;  // expect 9
}
