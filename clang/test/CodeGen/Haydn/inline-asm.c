// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s

// Test inline assembly with register constraints for Haydn

// Basic register constraint 'r' for GPR (R0-R15)
int test_gpr_constraint(int a, int b) {
    int result;
    // CHECK: define dso_local i32 @test_gpr_constraint(i32 noundef %a, i32 noundef %b)
    // CHECK: call i32 asm
    // CHECK-SAME: "add32 $0, $1, $2"
    // CHECK-SAME: "=r,r,r"
    asm("add32 %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
    return result;
}

// DR64 register constraint 'd' for D0-D15
long long test_dr64_constraint(long long a, long long b) {
    long long result;
    // CHECK: define dso_local i64 @test_dr64_constraint(i64 noundef %a, i64 noundef %b)
    // CHECK: call i64 asm
    // CHECK-SAME: "add64 $0, $1, $2"
    // CHECK-SAME: "=d,d,d"
    asm("add64 %0, %1, %2" : "=d"(result) : "d"(a), "d"(b));
    return result;
}

// Test with explicit GPR register names
void test_explicit_gpr(void) {
    register int r0 asm("r0");
    register int r1 asm("r1");
    // CHECK: define dso_local void @test_explicit_gpr()
    // CHECK: call void asm sideeffect
    // CHECK-SAME: "{r0},{r1}"
    asm volatile("" : : "r"(r0), "r"(r1));
}

// Test SP register alias
void test_sp_register(void) {
    register int sp asm("sp");
    // CHECK: define dso_local void @test_sp_register()
    // CHECK: call void asm sideeffect
    // CHECK-SAME: "{sp}"
    asm volatile("" : : "r"(sp));
}

// Test FP register alias
void test_fp_register(void) {
    register int fp asm("fp");
    // CHECK: define dso_local void @test_fp_register()
    // CHECK: call void asm sideeffect
    // CHECK-SAME: "{fp}"
    asm volatile("" : : "r"(fp));
}

// Test LR register alias
void test_lr_register(void) {
    register int lr asm("lr");
    // CHECK: define dso_local void @test_lr_register()
    // CHECK: call void asm sideeffect
    // CHECK-SAME: "{lr}"
    asm volatile("" : : "r"(lr));
}

// Test DR64 register names
void test_explicit_dr64(void) {
    register long long d0 asm("d0");
    register long long d1 asm("d1");
    // CHECK: define dso_local void @test_explicit_dr64()
    // CHECK: call void asm sideeffect
    // CHECK-SAME: "{d0},{d1}"
    asm volatile("" : : "d"(d0), "d"(d1));
}
