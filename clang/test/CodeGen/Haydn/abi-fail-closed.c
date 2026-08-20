// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -triple haydn-unknown-elf -fsyntax-only -verify %t/sema.c
// RUN: not %clang_cc1 -triple haydn-unknown-elf -ffreestanding -fsyntax-only \
// RUN:   %t/generic_dsp.h.c 2>&1 | FileCheck %s --check-prefix=GENERIC
// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %t/musttail.c \
// RUN:   | FileCheck %s --check-prefix=MUSTIR
// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-obj \
// RUN:   -o %t/musttail.o %t/musttail.c
// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %t/isr.c \
// RUN:   2>&1 | FileCheck %s --check-prefix=ISR
// REQUIRES: haydn-registered-target
//
// C-reachable ABI holes stay fail-closed. Sema owns __int128 / _Float16.
// Register-only musttail sibcall is JAL_W_MSP (isReturn+isCall terminator).
// interrupt is not a Haydn attribute — do not invent CC_ISR.

//--- sema.c
// expected-error@+1 {{__int128 is not supported on this target}}
__int128 bad_i128;

// expected-error@+1 {{_Float16 is not supported on this target}}
_Float16 bad_f16;

// 'r' is GPR32; i64 must use 'd'. Size mismatch and unknown/'i' stay Sema-reject.
void ok_d(void) {
  long long x;
  __asm__ volatile("" : "=d"(x));
}

void bad_r_i64(void) {
  long long x;
  // expected-error@+1 {{invalid output size for constraint '=r'}}
  __asm__ volatile("" : "=r"(x));
}

void bad_d_i32(void) {
  int x;
  // expected-error@+1 {{invalid output size for constraint '=d'}}
  __asm__ volatile("" : "=d"(x));
}

void bad_i(void) {
  int x;
  // expected-error@+1 {{invalid output constraint '=i' in asm}}
  __asm__ volatile("" : "=i"(x));
}

//--- generic_dsp.h.c
// GENERIC: haydn_dsp.h needs target feature simd
// GENERIC-NOT: always_inline
#include <haydn_dsp.h>

//--- musttail.c
void sink(int);
void musttail_caller(int x) {
  // MUSTIR: musttail call void @sink
  [[clang::musttail]] return sink(x);
}

//--- isr.c
// ISR: unknown attribute 'interrupt' ignored
// ISR-NOT: "interrupt"
void isr(void) __attribute__((interrupt));
void isr(void) {}
