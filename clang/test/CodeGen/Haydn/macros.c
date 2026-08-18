// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu generic -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu generic -dM -E %s | FileCheck --check-prefix=MACROS %s

// Test predefined macros for Haydn target (C3.2: arch + capability)

// MACROS: #define __ELF__ 1
// MACROS-DAG: #define __haydn_32__ 1
// MACROS-DAG: #define __haydn_LE__ 1
// MACROS-DAG: #define __haydn__ 1
// MACROS-DAG: #define __HAYDN_32__ 1
// MACROS-DAG: #define __HAYDN_ARCH__ 1
// MACROS-DAG: #define __HAYDN_CPU_GENERIC__ 1
// MACROS-DAG: #define __HAYDN_FEATURE_AGU__ 1
// MACROS-DAG: #define __HAYDN_FEATURE_HWLOOP__ 1
// MACROS-DAG: #define __HAYDN_LE__ 1
// MACROS-DAG: #define __HAYDN_SOFT_FLOAT__ 1
// MACROS-DAG: #define __HAYDN__ 1
// MACROS-DAG: #define __SOFTFP__ 1
// MACROS-NOT: #define __HAYDN_FEATURE_SIMD__
// MACROS-NOT: #define __HAYDN_FEATURE_CIRCULAR_BUFFER__

// Check data layout
// CHECK: target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"

void test_macros(void) {
    int x = 0;
#ifdef __haydn__
    x++;
#endif
#ifdef __HAYDN__
    x++;
#endif
#ifdef __ELF__
    x++;
#endif
#ifdef __HAYDN_LE__
    x++;
#endif
#if __HAYDN_ARCH__ >= 1
    x++;
#endif
#ifdef __HAYDN_FEATURE_AGU__
    x++;
#endif
}
