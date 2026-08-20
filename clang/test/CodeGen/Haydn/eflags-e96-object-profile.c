// REQUIRES: haydn-registered-target
//
// Clang emit-obj stamps EM_HAYDN=259 (Machine 0x103) and EF_HAYDN_E96
// (Flags 0x1). Official ELF 259 is Kalray KVX — distinguisher is the
// flag. Do not invent a replacement e_machine. Companion MC pin:
// llvm/test/MC/Haydn/eflags-e96-product-profile.s
//
// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-obj -o %t.o %s
// RUN: llvm-readobj --file-headers %t.o | FileCheck %s

// CHECK: Arch: haydn
// CHECK: Machine: 0x103
// CHECK: Flags [ (0x1)

int eflags_clang_probe(int a, int b) { return a + b; }
