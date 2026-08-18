/* Product-ld executable seat. Provides _start so the two-TU objects can
 * link against haydn-rt/haydn.ld without BSP crt0. Not BundleSim execution
 * and not semantic QUALIFY.
 */

struct agg {
  int x;
};

int consume(int, long long, int *, struct agg, float, double);

void _start(void) {
  int v = 1;
  struct agg s;
  s.x = 2;
  (void)consume(3, 4, &v, s, 1.0f, 2.0);
  for (;;)
    ;
}
