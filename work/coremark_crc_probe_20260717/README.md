# CoreMark CRC probe 2026-07-17

**Status: FIXED** — see FINDINGS.md and parallel work
`/ssd2/mhyang/BundleSim/work/coremark_parallel_20260717/`.

First-bad reported CRC: **list_crc** (seed_crc matches). Root: `matrix_mul_const`
miscompile from interblock ZOL exit→preheader hoist of outer IV before row-base MOVE.

Fix: `HaydnInterBlockScheduling.cpp` + lit `interblock-zol-hoist-preheader-iv-use.mir`.
