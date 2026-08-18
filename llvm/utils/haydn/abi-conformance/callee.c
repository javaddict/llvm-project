/* Two-TU ABI callee. Keep freestanding; no hosted headers. */

struct agg {
  int x;
};

int ret_i32(int a) { return a + 1; }

long long ret_i64(long long a) { return a + 1; }

int *ret_ptr(int *p) { return p; }

int take_agg(struct agg s) { return s.x + 1; }

int many_args(int a, int b, int c, int d, int e, int f, int g, int h) {
  /* Eighth i32 is the first stack-passed leftover after R1-R7. */
  return a + h;
}

/* Soft-float CC: f32 travels as i32 bits in GPR; f64 as i64 bits in DR. */
float ret_f32(float a) { return a; }

double ret_f64(double a) { return a; }
