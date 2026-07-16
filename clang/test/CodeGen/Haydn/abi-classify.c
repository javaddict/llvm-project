// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s
// REQUIRES: haydn-registered-target

// REGRESSION TEST: Haydn ABI classification is reachable (F16).
//
// Bug (F16 / consolidated-fix-list §2): HaydnABIInfo declared non-virtual
// classifyReturnType / classifyArgumentType that shadowed DefaultABIInfo's
// virtuals. computeInfo dispatched to the base, so the custom classifiers
// were dead code. The fix removes the shadowing methods and documents that
// the real ABI lives in HaydnCallingConv.td (R1-R7 for i32, D0-D3 for i64).
//
// Test design: this forces Clang's CodeGen to lower a function with mixed
// i32 / i64 arguments and a 64-bit return. If the ABI path regresses (e.g.
// arguments get dropped or mis-classified), the IR below changes shape.

// 64-bit return uses D0-D3; 64-bit args also use D0-D3.
// CHECK: define dso_local i64 @abi_int64_args(i64 noundef %a, i64 noundef %b)
long long abi_int64_args(long long a, long long b) {
  // CHECK: ret i64
  return a + b;
}

// 32-bit return uses R1-R2; 32-bit args use R1-R7.
// CHECK: define dso_local i32 @abi_int32_args(i32 noundef %a, i32 noundef %b)
int abi_int32_args(int a, int b) {
  // CHECK: ret i32
  return a + b;
}

// Mixed-width call forces the CodeGen path through classifyArgumentType.
// CHECK: define dso_local i32 @abi_mixed_call
int abi_mixed_call(int x, long long y) {
  // CHECK: call i64 @abi_int64_args
  long long acc = abi_int64_args(y, y);
  // CHECK: call i32 @abi_int32_args
  return abi_int32_args(x, (int)acc);
}
