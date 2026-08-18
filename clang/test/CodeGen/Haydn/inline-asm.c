// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s
//
// Inline asm: 'r' is GPR32, 'd' is DR64. Size mismatches and 'm'/'i' stay
// Sema-reject (Sema/haydn-feature-gates.c).

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

// DR64 file constraint. Backend getRegForInlineAsmConstraint maps 'd' to
// DR64RegClass; C Sema now accepts the same constraint on 64-bit values.
long long test_dr_constraint(long long a) {
    long long result;
    // CHECK: define dso_local i64 @test_dr_constraint(i64 noundef %a)
    // CHECK: call i64 asm
    // CHECK-SAME: "=d,d"
    asm("" : "=d"(result) : "d"(a));
    return result;
}
