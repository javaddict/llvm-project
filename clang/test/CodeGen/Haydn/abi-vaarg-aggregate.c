// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s
// REQUIRES: haydn-registered-target
//
// va_arg of a C aggregate / overaligned scalar must be a pointer-typed
// llvm.va_arg (Indirect, ByVal=false). Emitting byval memcpy's the
// contents into 8-byte pointer slots so multi-struct varargs overlap
// (va-arg-22). Direct i32 stays a value va_arg for the GPR cursor.

#include <stdarg.h>

struct Tiny {
  int x;
};

struct Big {
  long long a, b, c, d;
};

typedef int A16 __attribute__((aligned(16)));

// CHECK-LABEL: define dso_local i32 @va_tiny
// CHECK: va_arg ptr {{.*}}, ptr
// CHECK-NOT: byval
int va_tiny(int n, ...) {
  va_list ap;
  va_start(ap, n);
  struct Tiny t = va_arg(ap, struct Tiny);
  va_end(ap);
  return t.x;
}

// CHECK-LABEL: define dso_local i64 @va_big
// CHECK: va_arg ptr {{.*}}, ptr
// CHECK-NOT: byval
long long va_big(int n, ...) {
  va_list ap;
  va_start(ap, n);
  struct Big b = va_arg(ap, struct Big);
  va_end(ap);
  return b.a;
}

// CHECK-LABEL: define dso_local i32 @va_a16
// Direct overaligned scalar: va_arg of i32, then store into align-16 local.
// CHECK: alloca i32, align 16
// CHECK: va_arg ptr {{.*}}, i32
int va_a16(int n, ...) {
  va_list ap;
  va_start(ap, n);
  A16 v = va_arg(ap, A16);
  va_end(ap);
  return v;
}

// CHECK-LABEL: define dso_local i32 @va_i32
// CHECK: va_arg ptr {{.*}}, i32
int va_i32(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int v = va_arg(ap, int);
  va_end(ap);
  return v;
}
