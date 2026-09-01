// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -S -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -emit-llvm -o - %s | FileCheck %s --check-prefix=IR

// Golden v2_2 AR_CBR surface: the seven WUA-CB builtins lower to the hardware
// circular post step; the returned / written-back pointer is the WRAPPED HW
// cursor that must feed the next stream op. Forward circular _IC macros in
// haydn_dsp.h lower onto these same hardware paths (was software haydn_cbr_step).

typedef struct { long long data; void *new_ptr; } cbld_t;

void *prime_tw(const void *p) { return __builtin_haydn_pltwwua_cb_post(1, 0, p); }
// CHECK-LABEL: prime_tw
// CHECK: pltwwua_cb_post{{.*}}1{{.*}}0{{.*}}r

void *prime_qh(const void *p) { return __builtin_haydn_plqhwua_cb_post(0, 1, p); }
// CHECK-LABEL: prime_qh
// CHECK: plqhwua_cb_post{{.*}}0{{.*}}1{{.*}}r

long long ld_tw(const void *p) { cbld_t r; r.data = __builtin_haydn_ltwua_cb_post_pair(&r.new_ptr, p, 1, 0); return r.data; }
// CHECK-LABEL: ld_tw
// CHECK: d_ltwua_cb_post{{.*}}1{{.*}}0{{.*}}d

long long ld_qh(const void *p) { cbld_t r; r.data = __builtin_haydn_lqhwua_cb_post_pair(&r.new_ptr, p, 0, 1); return r.data; }
// CHECK-LABEL: ld_qh
// CHECK: d_lqhwua_cb_post{{.*}}0{{.*}}1{{.*}}d

void *st_tw(long long d, void *p) { return __builtin_haydn_stwua_cb_post(d, p, 1, 1); }
// CHECK-LABEL: st_tw
// CHECK: d_stwua_cb_post{{.*}}1{{.*}}1{{.*}}d

void *st_qh(long long d, void *p) { return __builtin_haydn_sqhwua_cb_post(d, p, 0, 0); }
// CHECK-LABEL: st_qh
// CHECK: d_sqhwua_cb_post{{.*}}0{{.*}}0{{.*}}d

void *wb_cb(void *p) { return __builtin_haydn_wbarwua_cb(1, 0, p); }
// CHECK-LABEL: wb_cb
// CHECK: wbarwua_cb{{.*}}1{{.*}}0{{.*}}r

// IR: declare ptr @llvm.haydn.pltwwua.cb.post(i32 immarg, i32 immarg, ptr captures(none))
// IR: declare { i64, ptr } @llvm.haydn.ltwua.cb.post(ptr captures(none), i32 immarg, i32 immarg)
