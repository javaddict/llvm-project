// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu generic -fsyntax-only -verify=full %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -verify=full %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -target-feature -simd -fsyntax-only -verify=nosimd %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -target-feature -circular-buffer -target-feature -bit-reversed -fsyntax-only -verify=nocb %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -target-feature -agu -fsyntax-only -verify=noagu %s
//
// C3.1 / G-CAPI-FEATURE: BuiltinsHaydn Features vs HaydnTargetInfo feature map.
//   generic = haydn = all five ISA features
//   nosimd  = haydn -simd
//   nocb    = haydn -circular-buffer -bit-reversed
//   noagu   = haydn -agu
// full-prefix expects zero diagnostics (product CPU enables all gated ops).
// full-no-diagnostics

typedef long long int64_t;
typedef int haydn_x2int32 __attribute__((__vector_size__(8)));
typedef short haydn_x4int16 __attribute__((__vector_size__(8)));

// Baseline scalar ALU — always available (empty Features string).
int baseline_ok(int a, int b) {
  return __builtin_haydn_add32s(a, b);
}

// SIMD — fails only with -simd.
haydn_x2int32 simd_x2add(haydn_x2int32 a, haydn_x2int32 b) {
  // nosimd-error@+1 {{'__builtin_haydn_x2add32' needs target feature simd}}
  return __builtin_haydn_x2add32(a, b);
}

int64_t simd_x2mul_pair(haydn_x2int32 a, haydn_x2int32 b) {
  int64_t lo;
  // nosimd-error@+1 {{'__builtin_haydn_x2mul32_pair' needs target feature simd}}
  return __builtin_haydn_x2mul32_pair(&lo, a, b);
}

// circular-buffer — fails when CB is stripped.
int64_t cb_load(const void *base) {
  void *np;
  // nocb-error@+1 {{'__builtin_haydn_ldw_cb_imm_pair' needs target feature circular-buffer}}
  return __builtin_haydn_ldw_cb_imm_pair(&np, base, /*cbr=*/0, /*stride=*/1);
}

void cb_setcbr(int begin) {
  // nocb-error@+1 {{'__builtin_haydn_setcbr_begin' needs target feature circular-buffer}}
  __builtin_haydn_setcbr_begin(/*cbr_sel=*/0, begin);
}

// bit-reversed — fails when BREV is stripped.
int64_t brev_load(const void *base) {
  void *np;
  // nocb-error@+1 {{'__builtin_haydn_ldw_brev_imm_pair' needs target feature bit-reversed}}
  return __builtin_haydn_ldw_brev_imm_pair(&np, base, /*stride=*/1);
}

int brev32_addr(int idx, int off) {
  // nocb-error@+1 {{'__builtin_haydn_brev32' needs target feature bit-reversed}}
  return __builtin_haydn_brev32(idx, off);
}

// agu — fails only with -agu.
void agu_flar(void) {
  // noagu-error@+1 {{'__builtin_haydn_flar' needs target feature agu}}
  __builtin_haydn_flar(/*ar_sel=*/0);
}
