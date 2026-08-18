# Pattern gate + hot-core dump (leftover M16 / T-TI3..6)

This is **not** a product gate and **not** QUALIFY evidence.

Product defaults stay OFF (`EnableHaydnHardwareLoops` `cl::init(false)`,
`HaydnMultiStageSMS::productDefaultEnabled() == false`). Stage-0
PostPipeliner / interblock densify are deleted; the historical
`-haydn-enable-post-pipeliner` / `-haydn-enable-interblock` flags are
absent. Do not treat II/fill numbers below as competitive KPI or as a
reason to flip policy.

M16 leftover (frozen until SF1-SF3): per-pass coverage closure, MC/disasm
fuzzer, stored perf baselines, fresh-seed yarpgen. This harness must not
invent those seats.

## Tools

| Script | Role |
|--------|------|
| `dump_hot_core.py` | Per-`.s` ZOL II / ops / fill / shell; optional SMS logs |
| `pattern_gate.sh` | Leftover NatureDSP compile/II compare (off vs product-default-OFF) |
| `freestanding-shims/ndsp_private_overlay/common.h` | Restores lvalue `castxcc` after NatureDSP `common.h` so `AE_*_IP` post-inc works |

## Historical kernel table (inventory only; not a QUALIFY pin)

| Kernel | last recorded II | last recorded fill | Notes |
|--------|---:|-----:|-------|
| `vec_dot32x32_hifi3` | 2 | 2.0 | leftover measurement |
| `bkfir32x32_hifi3` L1 | 10 | 2.5 | leftover measurement |
| `raw_corr32x32_hifi3` | 2 | 3.0 | leftover measurement |
| `vec_add32x32_hifi3` | 2 | 2.5 | leftover measurement |
| `vec_scale32x32_hifi3` | 3 | 1.33 | leftover measurement |

## Run

```bash
bash pattern_gate.sh /tmp/pattern-gate
python3 dump_hot_core.py K.s
# "off" and "default" are both product-default-OFF. IB mode is leftover only.
```

## Residual (M16 leftover; do not invent)

- T-TI3 per-pass coverage closure
- T-TI4 MC/disasm fuzzer
- T-TI5 stored perf baselines
- T-TI6 fresh-seed yarpgen / llvm-stress
