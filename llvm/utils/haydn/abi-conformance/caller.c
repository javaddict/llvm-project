/* Two-TU ABI caller. Symbols resolve against callee.c on one artifact. */

struct agg {
  int x;
};

int ret_i32(int);
long long ret_i64(long long);
int *ret_ptr(int *);
int take_agg(struct agg);
int many_args(int, int, int, int, int, int, int, int);
float ret_f32(float);
double ret_f64(double);

int consume(int i32, long long i64, int *p, struct agg s, float f32, double f64) {
  int acc = ret_i32(i32);
  acc += (int)ret_i64(i64);
  acc += *ret_ptr(p);
  acc += take_agg(s);
  acc += many_args(1, 2, 3, 4, 5, 6, 7, 8);
  acc += (int)ret_f32(f32);
  acc += (int)ret_f64(f64);
  return acc;
}
