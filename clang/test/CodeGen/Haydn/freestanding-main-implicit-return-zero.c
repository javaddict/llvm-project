// RUN: %clang_cc1 -triple haydn-unknown-elf -ffreestanding -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=HAYDN-O0
// RUN: %clang_cc1 -triple haydn-unknown-elf -ffreestanding -O1 -emit-llvm \
// RUN:   -o - %s | FileCheck %s --check-prefix=HAYDN-O1
// Negative control: the generic freestanding behavior (no implicit return
// zero for main) is unchanged on a target that does not keep hosted main
// semantics under -ffreestanding.
// RUN: %clang_cc1 -triple x86_64-unknown-elf -ffreestanding -O1 -emit-llvm \
// RUN:   -o - %s | FileCheck %s --check-prefix=GENERIC-O1
// REQUIRES: haydn-registered-target
//
// D1.23: Haydn keeps hosted `main` semantics under -ffreestanding via the
// TargetInfo hook treatsMainAsEntryUnderFreestanding() (single owning seat:
// HaydnTargetInfo override). C99 5.1.2.2.3 fallthrough-to-zero therefore
// still applies: bare `main` returns 0, never undef. Non-main functions
// keep returning undef (main-only seat, no blanket undef elimination).

int main(void) {
  volatile int a = 1;
  (void)a;
}

int not_main(void) {
  volatile int a = 1;
  (void)a;
}

// HAYDN-O0: define dso_local i32 @main
// HAYDN-O0: ret i32 0
// HAYDN-O0-NOT: ret i32 undef

// HAYDN-O1: define dso_local noundef i32 @main
// HAYDN-O1: ret i32 0

// Negative control in the same file: not_main still falls through to undef
// on Haydn (main-only special casing).
// HAYDN-O1: define dso_local i32 @not_main
// HAYDN-O1: ret i32 undef

// GENERIC-O1: define dso_local i32 @main
// GENERIC-O1: ret i32 undef
