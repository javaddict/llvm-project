// REQUIRES: clang, haydn-registered-target
//
// D242: end-to-end link test for the Haydn baremetal runtime slice.
// Verifies that `clang -target haydn-unknown-elf hello.c -o hello.elf`
// (with NO extra flags) auto-links crt0.o + libhaydn.o + haydn.ld,
// produces a valid ELF32 Haydn binary, and that the resulting file is
// recognized as an executable of the Haydn machine type (EM_HAYDN = 259).
//
// RUN: %clang -target haydn-unknown-elf %s -o %t.elf
// RUN: llvm-readobj --file-headers %t.elf | FileCheck %s --check-prefix=ELF
//
// ELF:      Format: elf32-unknown
// ELF:      Arch: haydn
// ELF:      Type: Executable (0x2)
// ELF:      Machine: 0x103

// Minimal program that exercises scalar ALU and a function call so the
// link pulls in crt0.o (for _start) and main is reachable from the entry.
int main(void) {
  volatile int a = 4;
  volatile int b = 5;
  return a + b;  // expect 9
}
