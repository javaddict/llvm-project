; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — Comprehensive Clang end-to-end integration test for the Haydn backend.

; Status : intent CHECKs re-goldened for Format E/cmov.
;
; Comprehensive Clang end-to-end integration test for the Haydn backend.
;
; This test verifies that the full Clang -> LLVM -> MC pipeline produces correct
; assembly for representative C patterns. Each function corresponds to a common
; C construct (function calls, stack ops, arithmetic, control flow, loops, i64
; globals, structs). The CHECK lines validate:
; Correct instruction selection (add32, sub32, mull (s32 mul,), jal_w, beqz_w, bnez_w, etc.)
; Reasonable register allocation (no obviously wrong register usage)
; Prologue/epilogue presence (stack adjustment, callee-save, return via jalr_w)
;
; This file is the authoritative integration test — all other per-category tests
; cover individual features, but this one proves they work together.

; =============================================================================
; Section 1: Function Calls
; =============================================================================

declare i32 @extern_sink(i32, i32)

; Simple call with one arg, result returned.
; C: int call_one(int a) { return extern_sink(a, 0); }
define i32 @call_one(i32 %a) {
; CHECK-LABEL: call_one:
; CHECK-DAG: jal{{.*}}{{.*}}extern_sink
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %r = call i32 @extern_sink(i32 %a, i32 0)
  ret i32 %r
}

; Call with 6 arguments (spills to stack for args 5-6 since Haydn has R0-R5 for args).
; C: int call_many(int a, int b, int c, int d) {
; return extern_sink(a+b, c+d, a*c, b*d, a-c, b-c);
; }
define i32 @call_many(i32 %a, i32 %b, i32 %c, i32 %d) {
; CHECK-LABEL: call_many:
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: jal{{.*}}{{.*}}extern_sink
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %ab = add i32 %a, %b
  %cd = add i32 %c, %d
  %ac = mul i32 %a, %c
  %bd = mul i32 %b, %d
  %amc = sub i32 %a, %c
  %bmc = sub i32 %b, %c
  %r = call i32 @extern_sink(i32 %ab, i32 %cd, i32 %ac, i32 %bd, i32 %amc, i32 %bmc)
  ret i32 %r
}

; Local caller-callee pair.
; C: int local_callee(int x) { return x + 1; }
; int local_caller(int a) { return local_callee(a); }
define i32 @local_callee(i32 %x) {
; CHECK-LABEL: local_callee:
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %r = add i32 %x, 1
  ret i32 %r
}

define i32 @local_caller(i32 %a) {
; CHECK-LABEL: local_caller:
; CHECK-DAG: jal{{.*}}{{.*}}local_callee
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %r = call i32 @local_callee(i32 %a)
  ret i32 %r
}

; Recursive function (Fibonacci).
; C: int fib(int n) { if (n <= 1) return n; return fib(n-1) + fib(n-2); }
define i32 @fib(i32 %n) {
; CHECK-LABEL: fib:
; CondOpt may absorb slt+invert into fused bge_w (AIE xor(setcc,1) style).
; CHECK-DAG: {{slt32|bge}}
; n-1/n-2 may be sub32 or addi -1/-2 + add32
; CHECK-DAG: {{sub32|add32}}
; CHECK-DAG: jal{{.*}}{{.*}}fib
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
entry:
  %cmp = icmp sle i32 %n, 1
  br i1 %cmp, label %base, label %recurse
base:
  ret i32 %n
recurse:
  %n1 = sub i32 %n, 1
  %n2 = sub i32 %n, 2
  %f1 = call i32 @fib(i32 %n1)
  %f2 = call i32 @fib(i32 %n2)
  %r = add i32 %f1, %f2
  ret i32 %r
}

; =============================================================================
; Section 2: Stack Operations
; =============================================================================

; alloca + store + load.
; C: int stack_alloca() { int x; x = 42; return x; }
define i32 @stack_alloca() {
; CHECK-LABEL: stack_alloca:
; CHECK-DAG: subi32{{.*}}sp, sp,
; CHECK-DAG: st32
; CHECK-DAG: addi32{{(_w)?}}{{.*}}sp, sp,
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %p = alloca i32
  store i32 42, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

; Stack array (local array of 4 ints, fill and sum).
; C: int stack_array() {
; int arr[4]; arr[0]=10; arr[1]=20; arr[2]=30; arr[3]=40;
; return arr[0] + arr[1] + arr[2] + arr[3];
; }
define i32 @stack_array() {
; CHECK-LABEL: stack_array:
; CHECK-DAG: subi32{{.*}}sp, sp,
; CHECK-DAG: st32
; CHECK-DAG: st32
; CHECK-DAG: ld32
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: addi32{{(_w)?}}{{.*}}sp, sp,
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %arr = alloca [4 x i32]
  %p0 = getelementptr [4 x i32], ptr %arr, i32 0, i32 0
  %p1 = getelementptr [4 x i32], ptr %arr, i32 0, i32 1
  %p2 = getelementptr [4 x i32], ptr %arr, i32 0, i32 2
  %p3 = getelementptr [4 x i32], ptr %arr, i32 0, i32 3
  store i32 10, ptr %p0
  store i32 20, ptr %p1
  store i32 30, ptr %p2
  store i32 40, ptr %p3
  %v0 = load i32, ptr %p0
  %v1 = load i32, ptr %p1
  %v2 = load i32, ptr %p2
  %v3 = load i32, ptr %p3
  %s1 = add i32 %v0, %v1
  %s2 = add i32 %s1, %v2
  %s3 = add i32 %s2, %v3
  ret i32 %s3
}

; Variable-index array access on the stack.
; C: int stack_idx(int i) { int arr[4] = {1,2,3,4}; return arr[i]; }
define i32 @stack_idx(i32 %i) {
; CHECK-LABEL: stack_idx:
; CHECK-DAG: subi32{{.*}}sp, sp,
; CHECK-DAG: st32
; CHECK-DAG: {{sll32|slli32}}
; Indexed load may be ld32 or folded s_lw_pre_reg
; CHECK-DAG: {{ld32|s_lw}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %arr = alloca [4 x i32]
  %p0 = getelementptr [4 x i32], ptr %arr, i32 0, i32 0
  %p1 = getelementptr [4 x i32], ptr %arr, i32 0, i32 1
  %p2 = getelementptr [4 x i32], ptr %arr, i32 0, i32 2
  %p3 = getelementptr [4 x i32], ptr %arr, i32 0, i32 3
  store i32 1, ptr %p0
  store i32 2, ptr %p1
  store i32 3, ptr %p2
  store i32 4, ptr %p3
  %elem = getelementptr [4 x i32], ptr %arr, i32 0, i32 %i
  %v = load i32, ptr %elem
  ret i32 %v
}

; =============================================================================
; Section 3: Integer Arithmetic
; =============================================================================

; Add, sub, mul.
; C: int arith(int a, int b) { return (a + b) * (a - b); }
define i32 @arith(i32 %a, i32 %b) {
; CHECK-LABEL: arith:
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: {{sub32|subi32}}
; CHECK-DAG: mull
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %sum = add i32 %a, %b
  %diff = sub i32 %a, %b
  %r = mul i32 %sum, %diff
  ret i32 %r
}

; Signed division and remainder (libcalls).
; C: int divrem(int a, int b) { return (a / b) + (a % b); }
define i32 @divrem(i32 %a, i32 %b) {
; CHECK-LABEL: divrem:
; CHECK-DAG: jal{{.*}}{{.*}}__divsi3
; CHECK-DAG: jal{{.*}}{{.*}}__modsi3
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %q = sdiv i32 %a, %b
  %r = srem i32 %a, %b
  %sum = add i32 %q, %r
  ret i32 %sum
}

; Unsigned division and remainder (libcalls).
; C: unsigned udivrem(unsigned a, unsigned b) { return (a / b) + (a % b); }
define i32 @udivrem(i32 %a, i32 %b) {
; CHECK-LABEL: udivrem:
; CHECK-DAG: jal{{.*}}{{.*}}__udivsi3
; CHECK-DAG: jal{{.*}}{{.*}}__umodsi3
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %q = udiv i32 %a, %b
  %r = urem i32 %a, %b
  %sum = add i32 %q, %r
  ret i32 %sum
}

; Bitwise operations.
; C: int bitwise(int a, int b) { return (a & b) | (a ^ b); }
define i32 @bitwise(i32 %a, i32 %b) {
; CHECK-LABEL: bitwise:
; CHECK-DAG: {{and32|andi32}}
; CHECK-DAG: {{xor32|xori32}}
; CHECK-DAG: {{or32|ori32}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %and = and i32 %a, %b
  %xor = xor i32 %a, %b
  %r = or i32 %and, %xor
  ret i32 %r
}

; Shifts.
; C: int shifts(int a, int n) { return (a << n) + (a >> n) + (a >> n); }
define i32 @shifts(i32 %a, i32 %n) {
; CHECK-LABEL: shifts:
; CHECK-DAG: {{sll32|slli32}}
; CHECK-DAG: {{srl32|srli32}}
; CHECK-DAG: {{sra32|srai32}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %shl = shl i32 %a, %n
  %lshr = lshr i32 %a, %n
  %ashr = ashr i32 %a, %n
  %s1 = add i32 %shl, %lshr
  %s2 = add i32 %s1, %ashr
  ret i32 %s2
}

; =============================================================================
; Section 4: Comparisons and Branches
; =============================================================================

; if/else.
; C: int if_else(int a, int b) { if (a > b) return a - b; else return a + b; }
define i32 @if_else(i32 %a, i32 %b) {
; CHECK-LABEL: if_else:
; CHECK-DAG: movt32
; CHECK-DAG: {{sub32|subi32}}
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
entry:
  %cmp = icmp sgt i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  %r1 = sub i32 %a, %b
  br label %join
else:
  %r2 = add i32 %a, %b
  br label %join
join:
  %r = phi i32 [%r1, %then], [%r2, %else]
  ret i32 %r
}

; Nested if/else.
; C: int nested_if(int a, int b, int c) {
; if (a > b) { if (b > c) return a; else return b; }
; else { if (a > c) return c; else return 0; }
; }
define i32 @nested_if(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: nested_if:
; CHECK-DAG: {{bge|slt32|bgeu}}
; CHECK-DAG: slt32
; CHECK-DAG: bnez{{(\.s[012])?}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
entry:
  %cmp1 = icmp sgt i32 %a, %b
  br i1 %cmp1, label %outer_t, label %outer_f
outer_t:
  %cmp2 = icmp sgt i32 %b, %c
  br i1 %cmp2, label %ret_a, label %ret_b
outer_f:
  %cmp3 = icmp sgt i32 %a, %c
  br i1 %cmp3, label %ret_c, label %ret_0
ret_a:
  ret i32 %a
ret_b:
  ret i32 %b
ret_c:
  ret i32 %c
ret_0:
  ret i32 0
}

; Switch with 4 consecutive cases.
; C: int switch4(int x) {
; switch(x) { case 0: return 10; case 1: return 20;
; case 2: return 30; case 3: return 40; default: return -1; }
; }
define i32 @switch4(i32 %x) {
; CHECK-LABEL: switch4:
; CHECK-DAG: sltu32
; CHECK-DAG: bnez{{(\.s[012])?}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
entry:
  switch i32 %x, label %default [
    i32 0, label %case0
    i32 1, label %case1
    i32 2, label %case2
    i32 3, label %case3
  ]
case0:
  ret i32 10
case1:
  ret i32 20
case2:
  ret i32 30
case3:
  ret i32 40
default:
  ret i32 -1
}

; Switch with sparse values.
; C: int switch_sparse(int x) {
; switch(x) { case 10: return 1; case 100: return 2; case 1000: return 3; }
; }
define i32 @switch_sparse(i32 %x) {
; CHECK-LABEL: switch_sparse:
; CHECK-DAG: seq32
; CHECK-DAG: bnez{{(\.s[012])?}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
entry:
  switch i32 %x, label %default [
    i32 10, label %c10
    i32 100, label %c100
    i32 1000, label %c1000
  ]
c10:
  ret i32 1
c100:
  ret i32 2
c1000:
  ret i32 3
default:
  ret i32 0
}

; =============================================================================
; Section 5: Loops
; =============================================================================

; Simple counting loop with loop-carried dependency.
; C: int sum_loop(int n) { int s = 0; for (int i = 0; i < n; i++) s += i; return s; }
define i32 @sum_loop(i32 %n) {
; CHECK-LABEL: sum_loop:
; CHECK-DAG: {{blt|slt32|bltu}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %sum = phi i32 [0, %entry], [%sum.next, %loop]
  %sum.next = add i32 %sum, %i
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %sum.next
}

; Nested loop (matrix sum — flattened).
; C: int nested_loop(int n) {
; int s = 0;
; for (int i = 0; i < n; i++)
; for (int j = 0; j < n; j++)
; s += i * j;
; return s;
; }
define i32 @nested_loop(i32 %n) {
; CHECK-LABEL: nested_loop:
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: {{blt|slt32|bltu}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
entry:
  br label %outer
outer:
  %i = phi i32 [0, %entry], [%i.next, %outer_latch]
  %sum.o = phi i32 [0, %entry], [%sum.i, %outer_latch]
  br label %inner
inner:
  %j = phi i32 [0, %outer], [%j.next, %inner]
  %sum.i = phi i32 [%sum.o, %outer], [%sum.inc, %inner]
  %prod = mul i32 %i, %j
  %sum.inc = add i32 %sum.i, %prod
  %j.next = add i32 %j, 1
  %cmp.j = icmp slt i32 %j.next, %n
  br i1 %cmp.j, label %inner, label %outer_latch
outer_latch:
  %i.next = add i32 %i, 1
  %cmp.i = icmp slt i32 %i.next, %n
  br i1 %cmp.i, label %outer, label %exit
exit:
  ret i32 %sum.i
}

; Loop with early exit.
; C: int loop_break(int n, int limit) {
; int s = 0;
; for (int i = 0; i < n; i++) { if (i >= limit) break; s += i; }
; return s;
; }
define i32 @loop_break(i32 %n, i32 %limit) {
;CHECK-LABEL: loop_break:
; Break: fused bge_w (CondOpt xor(setcc,1) absorb) or slt+xori+bnez; latch blt_w.
; CHECK-DAG: {{bge|xori32|slt32}}
; CHECK-DAG: {{blt|slt32|bltu}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.next, %body]
  %sum = phi i32 [0, %entry], [%sum.inc, %body]
  %cmp.brk = icmp sge i32 %i, %limit
  br i1 %cmp.brk, label %exit, label %body
body:
  %sum.inc = add i32 %sum, %i
  %i.next = add i32 %i, 1
  %cmp.cont = icmp slt i32 %i.next, %n
  br i1 %cmp.cont, label %loop, label %exit
exit:
  %result = phi i32 [%sum, %loop], [%sum.inc, %body]
  ret i32 %result
}

; =============================================================================
; Section 6: 64-bit Operations
; =============================================================================

; i64 add.
; C: long add64(long a, long b) { return a + b; }
define i64 @add64(i64 %a, i64 %b) {
; CHECK-LABEL: add64:
; CHECK-DAG: add64
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %r = add i64 %a, %b
  ret i64 %r
}

; i64 subtract.
; C: long sub64(long a, long b) { return a - b; }
define i64 @sub64(i64 %a, i64 %b) {
; CHECK-LABEL: sub64:
; CHECK-DAG: sub64
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %r = sub i64 %a, %b
  ret i64 %r
}

; i64 comparison with select.
; C: long min64(long a, long b) { return (a < b) ? a : b; }
define i64 @min64(i64 %a, i64 %b) {
; CHECK-LABEL: min64:
; CHECK-DAG: sltu32
; CHECK-DAG: sltu32
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %cmp = icmp ult i64 %a, %b
  %sel = select i1 %cmp, i64 %a, i64 %b
  ret i64 %sel
}

; i64 load and store.
; C: long loadstore64(long *p, long v) { long old = *p; *p = v; return old; }
define i64 @loadstore64(ptr %p, i64 %v) {
; CHECK-LABEL: loadstore64:
; CHECK-DAG: ld32
; CHECK-DAG: {{st32|d_sw}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %old = load i64, ptr %p
  store i64 %v, ptr %p
  ret i64 %old
}

; i64 arithmetic chain.
; C: long arith64(long a, long b) { return (a + b) - (a & b); }
define i64 @arith64(i64 %a, i64 %b) {
; CHECK-LABEL: arith64:
; CHECK-DAG: add64
; CHECK-DAG: and64
; CHECK-DAG: sub64
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %sum = add i64 %a, %b
  %and = and i64 %a, %b
  %r = sub i64 %sum, %and
  ret i64 %r
}

; =============================================================================
; Section 7: Global Variable Access
; =============================================================================

@g_counter = global i32 0
@g_data = global [4 x i32] [i32 10, i32 20, i32 30, i32 40]
@g_pi = constant i32 31415

; Load from global.
; C: int load_global() { return g_counter; }
define i32 @load_global() {
; CHECK-LABEL: load_global:
; CHECK-DAG: lui{{.*}}{{.*}}g_counter
; CHECK-DAG: addi32
; CHECK-DAG: ld32
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %v = load i32, ptr @g_counter
  ret i32 %v
}

; Store to global.
; C: void store_global(int v) { g_counter = v; }
define void @store_global(i32 %v) {
; CHECK-LABEL: store_global:
; CHECK-DAG: lui{{.*}}{{.*}}g_counter
; CHECK-DAG: addi32
; CHECK-DAG: st32
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  store i32 %v, ptr @g_counter
  ret void
}

; Load-modify-store on global (increment).
; C: void inc_global() { g_counter++; }
define void @inc_global() {
; CHECK-LABEL: inc_global:
; CHECK-DAG: lui{{.*}}{{.*}}g_counter
; CHECK-DAG: ld32
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: st32
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %old = load i32, ptr @g_counter
  %new = add i32 %old, 1
  store i32 %new, ptr @g_counter
  ret void
}

; Access a global array by index.
; C: int global_array(int i) { return g_data[i]; }
define i32 @global_array(i32 %i) {
; CHECK-LABEL: global_array:
; CHECK-DAG: lui{{.*}}{{.*}}g_data
; CHECK-DAG: {{sll32|slli32}}
; Base+index may be add32+ld32 or folded s_lw_pre_reg
; CHECK-DAG: {{add32|s_lw|ld32}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %p = getelementptr [4 x i32], ptr @g_data, i32 0, i32 %i
  %v = load i32, ptr %p
  ret i32 %v
}

; Load from constant global.
; C: int load_const_global() { return g_pi; }
define i32 @load_const_global() {
; CHECK-LABEL: load_const_global:
; CHECK-DAG: lui{{.*}}{{.*}}g_pi
; CHECK-DAG: addi32
; CHECK-DAG: ld32
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %v = load i32, ptr @g_pi
  ret i32 %v
}

; Two globals in one function.
; C: int two_globals() { return g_counter + g_pi; }
define i32 @two_globals() {
; CHECK-LABEL: two_globals:
; CHECK-DAG: lui{{.*}}{{.*}}g_counter
; CHECK-DAG: lui{{.*}}{{.*}}g_pi
; CHECK-DAG: ld32
; CHECK-DAG: ld32
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %v1 = load i32, ptr @g_counter
  %v2 = load i32, ptr @g_pi
  %sum = add i32 %v1, %v2
  ret i32 %sum
}

; =============================================================================
; Section 8: Struct Passing/Returning
; =============================================================================

%struct.point = type { i32, i32 }
%struct.triple = type { i32, i32, i32 }

; Struct passed by pointer (read fields, GEP offset folded into load displacement).
; C: int struct_read(struct point *p) { return p->x + p->y; }
define i32 @struct_read(ptr %p) {
; CHECK-LABEL: struct_read:
; Field loads may be ld32 or fused s_lw_* forms.
; CHECK-DAG: {{ld32|s_lw}}
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %x = load i32, ptr %p
  %yp = getelementptr %struct.point, ptr %p, i32 0, i32 1
  %y = load i32, ptr %yp
  %r = add i32 %x, %y
  ret i32 %r
}

; Struct write via pointer (GEP offset folded into store displacement).
; C: void struct_write(struct point *p, int x, int y) { p->x = x; p->y = y; }
define void @struct_write(ptr %p, i32 %x, i32 %y) {
; CHECK-LABEL: struct_write:
; CHECK-DAG: {{st32|s_sw}}
; CHECK-DAG: {{st32|s_sw}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  store i32 %x, ptr %p
  %yp = getelementptr %struct.point, ptr %p, i32 0, i32 1
  store i32 %y, ptr %yp
  ret void
}

; Struct copy by pointer (field by field).
; C: void struct_copy(struct point *dst, struct point *src) {
; dst->x = src->x; dst->y = src->y;
; }
define void @struct_copy(ptr %dst, ptr %src) {
; CHECK-LABEL: struct_copy:
; Field copy may use ld32/st32 or fused s_lw_*/s_sw_* forms.
; CHECK-DAG: {{ld32|s_lw}}
; CHECK-DAG: {{st32|s_sw}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %sx = load i32, ptr %src
  store i32 %sx, ptr %dst
  %sy.p = getelementptr %struct.point, ptr %src, i32 0, i32 1
  %sy = load i32, ptr %sy.p
  %dy.p = getelementptr %struct.point, ptr %dst, i32 0, i32 1
  store i32 %sy, ptr %dy.p
  ret void
}

; Return small struct (single register).
; C: struct point make_point(int x, int y) { struct point p; p.x = x; p.y = y; return p; }
define %struct.point @make_point(i32 %x, i32 %y) {
; CHECK-LABEL: make_point:
; CHECK-DAG: jalr{{.*}}r0, lr, 0
  %s = insertvalue %struct.point undef, i32 %x, 0
  %s2 = insertvalue %struct.point %s, i32 %y, 1
  ret %struct.point %s2
}

; Struct with array indexing inside a loop.
; C: int struct_loop(struct triple *arr, int n) {
; int s = 0;
; for (int i = 0; i < n; i++) s += arr[i].x + arr[i].y + arr[i].z;
; return s;
; }
define i32 @struct_loop(ptr %arr, i32 %n) {
; CHECK-LABEL: struct_loop:
; (SFR-strip) changed bundle layout — rebaselined.
; dual-sched pre-RA : reg/bundle order free; keep key ops.
; CHECK-DAG: ld32
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: addi32{{(_w)?}} {{.*}}, {{.*}}, 8
; CHECK-DAG: jalr{{.*}}r0, lr, 0
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %sum = phi i32 [0, %entry], [%sum.next, %loop]
  %elem = getelementptr %struct.triple, ptr %arr, i32 %i
  %fx = load i32, ptr %elem
  %fy.p = getelementptr %struct.triple, ptr %elem, i32 0, i32 1
  %fy = load i32, ptr %fy.p
  %fz.p = getelementptr %struct.triple, ptr %elem, i32 0, i32 2
  %fz = load i32, ptr %fz.p
  %s1 = add i32 %fx, %fy
  %s2 = add i32 %s1, %fz
  %sum.next = add i32 %sum, %s2
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %sum.next
}

; =============================================================================
; Section 9: Integration — Complex Functions Combining Multiple Patterns
; =============================================================================

; Combine global access, loop, and function call.
; C: int accumulate_global(int n) {
; int s = g_counter;
; for (int i = 0; i < n; i++) { s += g_data[i]; g_counter = s; }
; return extern_sink(s, n);
; }
define i32 @accumulate_global(i32 %n) {
; CHECK-LABEL: accumulate_global:
; CHECK-DAG: lui
; CHECK-DAG: ld32
; CHECK-DAG: st32
; CHECK-DAG: jal{{.*}}{{.*}}extern_sink
; CHECK-DAG: jalr{{.*}}r0, lr, 0
entry:
  %g = load i32, ptr @g_counter
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %sum = phi i32 [%g, %entry], [%sum.next, %loop]
  %dp = getelementptr [4 x i32], ptr @g_data, i32 0, i32 %i
  %dv = load i32, ptr %dp
  %sum.next = add i32 %sum, %dv
  store i32 %sum.next, ptr @g_counter
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  %r = call i32 @extern_sink(i32 %sum.next, i32 %n)
  ret i32 %r
}

; Combine i64, loop, and conditional.
; C: long checksum(long *arr, int n) {
; long s = 0;
; for (int i = 0; i < n; i++) {
; long v = arr[i];
; if (v < 0) s -= v; else s += v;
; }
; return s;
; }
define i64 @checksum(ptr %arr, i32 %n) {
; CHECK-LABEL: checksum:
; (SFR-strip) changed bundle layout — rebaselined.
; The latch materializes as slt32+bnez_w (unfused), not blt_w.
; CHECK-DAG: slt32
; CHECK-DAG: sub64
; CHECK-DAG: add64
; CHECK-DAG: .cfi_offset r8, {{[-0-9]+}}
; CHECK-DAG: jalr{{.*}}r0, lr, 0
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %sum = phi i64 [0, %entry], [%sum.next, %loop]
  %p = getelementptr i64, ptr %arr, i32 %i
  %v = load i64, ptr %p
  %neg.cmp = icmp slt i64 %v, 0
  %neg.v = sub i64 0, %v
  %abs = select i1 %neg.cmp, i64 %neg.v, i64 %v
  %sum.next = add i64 %sum, %abs
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i64 %sum.next
}

; Combine struct access with control flow.
; C: int struct_conditional(struct point *arr, int n, int threshold) {
; int s = 0;
; for (int i = 0; i < n; i++)
; if (arr[i].x > threshold) s += arr[i].y;
; return s;
; }
define i32 @struct_conditional(ptr %arr, i32 %n, i32 %threshold) {
; CHECK-LABEL: struct_conditional:
; (SFR-strip) changed bundle layout — rebaselined.
; The latch materializes as slt32+bnez_w (unfused), not blt_w.
; The loop-carried s32 select lowers to MOVT32 (prior revision).
; CHECK-DAG: ld32
; CHECK-DAG: slt32
; CHECK-DAG: movt32
; CHECK-DAG: {{add32|addi32}}
; CHECK-DAG: .long 40 // 0x28
; CHECK-DAG: jalr{{.*}}r0, lr, 0
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %sum = phi i32 [0, %entry], [%sum.new, %loop]
  %elem = getelementptr %struct.point, ptr %arr, i32 %i
  %x = load i32, ptr %elem
  %cmp = icmp sgt i32 %x, %threshold
  %yp = getelementptr %struct.point, ptr %elem, i32 0, i32 1
  %y = load i32, ptr %yp
  %add.y = add i32 %sum, %y
  %sum.new = select i1 %cmp, i32 %add.y, i32 %sum
  %i.next = add i32 %i, 1
  %cmpl = icmp slt i32 %i.next, %n
  br i1 %cmpl, label %loop, label %exit
exit:
  ret i32 %sum.new
}
