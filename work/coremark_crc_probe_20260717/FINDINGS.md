> **Product follow-up (2026-07-17):** ZOL exit→preheader hoist **deleted** entirely (AIE does not do it; reject-contract cost exceeded benefit). CoreMark stays fixed without the invent; Stage-0 IB = fallthrough pack only.

# CoreMark CRC — FINDINGS (2026-07-17, parallel lane C)

## Status
**FIXED** (compiler miscompile). Full CoreMark qualification (iterations=1) host ILP32 vs Haydn BundleSim **MATCH** after fix.

## First-bad CRC component
| Order | field | host | guest (pre-fix) |
|-------|-------|------|------------------|
| 0 | seed_crc | 0xe9f5 | **MATCH** |
| 1 | **list_crc** | 0xe714 | 0x4aba **FIRST DIFF** |
| 2 | matrix_crc | 0x1fd7 | 0x5f97 |
| 3 | state_crc | 0x8e3a | 0x545b |
| 4 | final_crc | 0xe714 | 0x4aba |
| | error_count | 0 | 3 |

Note: CoreMark `iterate()` only runs `core_bench_list`; matrix/state CRCs are side-effects via `calc_func` during list. All three CRCs failing together is expected for a matrix kernel miscompile.

## Isolation
1. Qualification iters=1 reconfirmed ORACLE_MISMATCH (seed match).
2. All TUs `-O0` → **PASS**.
3. Raise one TU to stock opt: only **`core_matrix.c` at -O1** fails (same CRC set). Stock opts with matrix `-O0` → **PASS**.
4. Function `optnone` bisect on matrix: only **`matrix_mul_const`** at optnone (rest O1) → **PASS**.
5. Minimal freestanding micro (`reduced/mulconst_micro/repro.c`, N=4):
   - O0 MATCH
   - O1/O2 FAIL: row 0 of C left as poison `0x5a5a5a5a`; products for A[0..] written starting at C[4].

## Root cause
**Haydn Stage-0 inter-block ZOL exit→preheader hoist** (`HaydnInterBlockScheduling::tryZOLExitToPreheader`).

After `haydn-hwloops`, MIR is correct:
```
bb.2: SET_HWLOOP; r4=MOVE r3; r8=MOVE r2   ; row bases
bb.3: inner ZOL body
bb.4: r5++; r2+=stride; r3+=stride; cmp/branch
```

Post-RA `postmisched` leaveMBB runs interblock (default ON). Hoist moved `r2+=stride` (and `r5++`) into the preheader **after SET but before** `r8=MOVE r2`, so the first outer iteration uses C+rowstride and skips row 0.

Independence checks covered loop-body uses of live defs (CB-107) but **not remaining post-SET preheader uses** of those defs.

Workaround (pre-fix): `-mllvm -haydn-enable-interblock=false` or `-mllvm -haydn-enable-post-ra-sched=false` on matrix TU.

## Fix
`llvm/lib/Target/Haydn/HaydnInterBlockScheduling.cpp`: reject ZOL exit→preheader hoist when a live def is read by any non-terminator remaining preheader MI after the post-SET insert point.

Lit: `llvm/test/CodeGen/Haydn/interblock-zol-hoist-preheader-iv-use.mir`

## Classification
- **Compiler miscompile** (not CoreMark port, not harness LP64-long, not UB in source).
- Not CB-113.

## Paths
- Work: `/ssd2/mhyang/BundleSim/work/coremark_parallel_20260717/`
- Micro: `reduced/mulconst_micro/`
- Builds: `builds/stock_default` (pre), `builds/stock_after_fix` (post MATCH)
- Prior probe: `BundleSim/work/coremark_crc_probe_20260717/`, monorepo twin under `llvm-head/work/coremark_crc_probe_20260717/`
