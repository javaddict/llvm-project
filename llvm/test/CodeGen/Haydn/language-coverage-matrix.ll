; RUN: rm -rf %t && split-file %s %t
;
; Role: verifier — advertised IR surface + explicit unsupported diagnostics.
;
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=asm %t/supported.ll -o - | FileCheck %s --check-prefix=SUP
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=obj %t/supported.ll -o %t/supported.o
; RUN: llvm-objdump -d %t/supported.o | FileCheck %s --check-prefix=OBJ
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/musttail.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=MUSTTAIL
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/swiftcc.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SWIFTCC
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/va_i128.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=VA_I128
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/ptrauth.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=PTRAUTH
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/swiftcc_def.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SWIFTCC_DEF
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/preserve_most.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=PRESERVE_MOST
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/nest.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=NEST
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/byref.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=BYREF
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/swiftasync.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SWIFTASYNC
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/ghccc.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=GHCCC
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/f128_add.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=F128
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/va_v2i32.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=VA_VEC
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/half_cc.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=HALF_CC

;--- supported.ll
declare i32 @ext_i32(i32)
declare void @llvm.memcpy.p0.p0.i32(ptr noalias nocapture writeonly, ptr noalias nocapture readonly, i32, i1 immarg)
declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
declare void @llvm.va_copy(ptr, ptr)

define i32 @sup_add_mul_call(i32 %a, i32 %b) {
; SUP-LABEL: sup_add_mul_call:
; SUP: jal
; OBJ: jal
  %s = add i32 %a, %b
  %p = mul i32 %s, 3
  %r = call i32 @ext_i32(i32 %p)
  ret i32 %r
}

define i64 @sup_i64_arith(i64 %a, i64 %b) {
; SUP-LABEL: sup_i64_arith:
; SUP: add64
  %s = add i64 %a, %b
  ret i64 %s
}

define void @sup_memcpy(ptr %d, ptr %s) {
; SUP-LABEL: sup_memcpy:
; SUP: memcpy
  call void @llvm.memcpy.p0.p0.i32(ptr %d, ptr %s, i32 16, i1 false)
  ret void
}

define i32 @sup_atomic_load(ptr %p) {
; SUP-LABEL: sup_atomic_load:
; SUP: __atomic_load_4
  %v = load atomic i32, ptr %p monotonic, align 4
  ret i32 %v
}

define i32 @sup_va_arg_s32(i32 %n, ...) {
; SUP-LABEL: sup_va_arg_s32:
  %ap = alloca i8, i32 48
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i32
  call void @llvm.va_end(ptr %ap)
  ret i32 %v
}

define i64 @sup_va_arg_s64(i32 %n, ...) {
; SUP-LABEL: sup_va_arg_s64:
  %ap = alloca i8, i32 48
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i64
  call void @llvm.va_end(ptr %ap)
  ret i64 %v
}

define void @sup_va_copy(ptr %d, ptr %s) {
; SUP-LABEL: sup_va_copy:
; SUP: {{ld32|st32|lw|sw}}
  call void @llvm.va_copy(ptr %d, ptr %s)
  ret void
}

define i32 @sup_indirect_call(ptr %fp, i32 %x) {
; SUP-LABEL: sup_indirect_call:
; SUP: jalr
  %r = call i32 %fp(i32 %x)
  ret i32 %r
}

define i32 @sup_i8_i16(i8 %a, i16 %b) {
; SUP-LABEL: sup_i8_i16:
  %za = zext i8 %a to i32
  %zb = zext i16 %b to i32
  %s = add i32 %za, %zb
  ret i32 %s
}

define i32 @sup_byval(ptr byval([4 x i32]) %p) {
; SUP-LABEL: sup_byval:
  %v = load i32, ptr %p
  ret i32 %v
}

declare i32 @sink_byval4(ptr byval([4 x i32]) %p)

define i32 @sup_byval_caller(ptr %src) {
; SUP-LABEL: sup_byval_caller:
; SUP: st32
; SUP: jal
  %r = call i32 @sink_byval4(ptr byval([4 x i32]) %src)
  ret i32 %r
}

define void @sup_sret(ptr sret([2 x i32]) %out, i32 %a, i32 %b) {
; SUP-LABEL: sup_sret:
  store i32 %a, ptr %out
  %p1 = getelementptr [2 x i32], ptr %out, i32 0, i32 1
  store i32 %b, ptr %p1
  ret void
}

define float @sup_softfloat_add(float %a, float %b) {
; SUP-LABEL: sup_softfloat_add:
; SUP: __addsf3
  %r = fadd float %a, %b
  ret float %r
}

define i32 @sup_atomic_rmw_add(ptr %p, i32 %v) {
; SUP-LABEL: sup_atomic_rmw_add:
; SUP: __atomic_fetch_add_4
  %r = atomicrmw add ptr %p, i32 %v monotonic
  ret i32 %r
}

declare float @llvm.minimum.f32(float, float)
declare float @llvm.maximum.f32(float, float)

; llvm.minimum/maximum currently legalize through minnum/maxnum → fminf/fmaxf.
; That is a residual substitution, not IEEE-754 minimum/maximum (NaN and
; signed-zero). This seat only proves the advertised IR does not abort.
define float @sup_fminimum_residual(float %a, float %b) {
; SUP-LABEL: sup_fminimum_residual:
; SUP: {{fminf|jal}}
  %r = call float @llvm.minimum.f32(float %a, float %b)
  ret float %r
}

define float @sup_fmaximum_residual(float %a, float %b) {
; SUP-LABEL: sup_fmaximum_residual:
; SUP: {{fmaxf|jal}}
  %r = call float @llvm.maximum.f32(float %a, float %b)
  ret float %r
}


;--- musttail.ll
declare void @callee(i32)
define void @musttail_reject(i32 %x) {
  musttail call void @callee(i32 %x)
  ret void
}
; MUSTTAIL: unable to translate instruction: call

;--- swiftcc.ll
declare swiftcc void @swift_callee(i32)
define void @swiftcc_reject(i32 %x) {
  ; SWIFTCC: {{unable to lower|failed to lower|Cannot select|unable to translate|unsupported}}
  call swiftcc void @swift_callee(i32 %x)
  ret void
}

;--- va_i128.ll
declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
define void @va_i128(i32 %n, ...) {
  %ap = alloca i8, i32 48
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i128
  call void @llvm.va_end(ptr %ap)
  ret void
}
; VA_I128: unable to legalize instruction: {{.*}}G_VAARG

;--- ptrauth.ll
@g = external global i32
define ptr @ptrauth_reject() {
  ; PTRAUTH: {{unable to legalize instruction: .*G_PTRAUTH_GLOBAL_VALUE|cannot select: .*G_PTRAUTH_GLOBAL_VALUE}}
  ret ptr ptrauth (ptr @g, i32 0)
}

;--- swiftcc_def.ll
define swiftcc i32 @swiftcc_def_reject(i32 %x) {
  ret i32 %x
}
; SWIFTCC_DEF: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}

;--- preserve_most.ll
define preserve_mostcc i32 @preserve_most_reject(i32 %x) {
  ret i32 %x
}
; PRESERVE_MOST: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}

;--- nest.ll
define i32 @nest_reject(ptr nest %p, i32 %x) {
  %v = load i32, ptr %p
  %r = add i32 %v, %x
  ret i32 %r
}
; NEST: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}

;--- byref.ll
define i32 @byref_reject(ptr byref([4 x i32]) %p) {
  %v = load i32, ptr %p
  ret i32 %v
}
; BYREF: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}

;--- swiftasync.ll
define void @swiftasync_reject(ptr swiftasync %ctx) {
  ret void
}
; SWIFTASYNC: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}

;--- ghccc.ll
define ghccc i32 @ghccc_reject(i32 %x) {
  ret i32 %x
}
; GHCCC: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}

;--- f128_add.ll
define fp128 @f128_add_reject(fp128 %a, fp128 %b) {
  %r = fadd fp128 %a, %b
  ret fp128 %r
}
; F128: {{unable to legalize instruction: .*G_(FADD|STORE)|unable to lower|failed to legalize}}

;--- va_v2i32.ll
declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
define void @va_v2i32(i32 %n, ...) {
  %ap = alloca i8, i32 48
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, <2 x i32>
  call void @llvm.va_end(ptr %ap)
  ret void
}
; VA_VEC: unable to legalize instruction: {{.*}}G_VAARG

;--- half_cc.ll
define half @half_cc_reject(half %x) {
  ret half %x
}
; HALF_CC: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
