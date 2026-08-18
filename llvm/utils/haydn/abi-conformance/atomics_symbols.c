/* Three-layer atomics seat: compile-only symbol demand against the sysroot.
 * Backend libcalls every atomic; Clang still advertises lock-free i32.
 */

int atomic_i32_load(int *p) {
  return __atomic_load_n(p, __ATOMIC_SEQ_CST);
}

void atomic_i32_store(int *p, int v) {
  __atomic_store_n(p, v, __ATOMIC_SEQ_CST);
}

int atomic_i32_xchg(int *p, int v) {
  return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);
}
