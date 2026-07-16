# Pattern gate + hot-core dump (Wave A harness)

## Tools

| Script | Role |
|--------|------|
| `dump_hot_core.py` | Per-`.s` ZOL II / ops / fill / shell; optional SMS/PP logs |
| `pattern_gate.sh` | Golden 5-kernel AIE II non-growth gate (off vs default) |
| `freestanding-shims/ndsp_private_overlay/common.h` | Restores lvalue `castxcc` after NatureDSP `common.h` so `AE_*_IP` post-inc works (D185 / F24 streams) |

## Golden kernels (must not grow II or drop ZOL)

Defaults after P11: **PP ON + IB ON**.

| Kernel | II | fill | Notes |
|--------|---:|-----:|-------|
| `vec_dot32x32_hifi3` | 2 | 2.0 | dual-LD+MAC; P1 residual fill vs HiFi 3.0 |
| `bkfir32x32_hifi3` L1 | 10 | 2.5 | dense MAC; P3 residual |
| `raw_corr32x32_hifi3` | 2 | 3.0 | golden pack |
| `vec_add32x32_hifi3` | 2 | 2.5 | golden |
| `vec_scale32x32_hifi3` | 3 | 1.33 | P2 residual; IB must not grow II |

## Run

```bash
bash pattern_gate.sh /tmp/pattern-gate          # off vs default (PP+IB)
python3 dump_hot_core.py K.s
# off forces -haydn-enable-post-pipeliner=false -haydn-enable-interblock=false
```

## P11 fix

Post-convert ZOL bodies lack CFG self-edges. IB now detects SET begin-MBB
targets (`isHwLoopBody`) and never fallthrough-packs into them (was pulling
exit `and32` into vec_scale → II 3→4).

## Residual (next sprints)

- **P1** fill: need safe unroll or denser SMS keep (IR unroll broke ZOL)
- **P2** scale II 3→2: PP tryII OK but prolog peel liveness aborts materialize
- **P5–P10**: missing ZOL / GISel bloat / FFT soft / nest polish — separate PRs
