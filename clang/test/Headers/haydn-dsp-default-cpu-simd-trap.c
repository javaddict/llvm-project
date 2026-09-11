// RUN: %clang_cc1 -triple haydn-unknown-elf -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-feature +agu \
// RUN:   -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu generic -fsyntax-only \
// RUN:   -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only \
// RUN:   -ffreestanding %s
// RUN: not %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn \
// RUN:   -target-feature -simd -fsyntax-only -ffreestanding %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NOSIMD
//
// REQUIRES: haydn-registered-target
//
// Empty / generic / haydn are the same full ISA and compile haydn_dsp.h.
// Only explicit -target-feature -simd fail-closes (one #error before
// including generated haydn.h). Peer: xmmintrin.h:13-15.
//
// NOSIMD: haydn_dsp.h needs target feature simd
// NOSIMD-NOT: always_inline

#include <haydn_dsp.h>
