# II Tracking Suite — Hot-Loop Phase 0-2 Regression Tests

This directory tracks hot-loop II (initiation interval) improvements from
`~/haydn-plans/next-phase-fixes-plan-2026-06-14.md`.

## Status

All five tracking tests **PASS** under

```
llc -mtriple=haydn-unknown-elf -global-isel-abort=1
```

The original `XFAIL: *` markers were dropped as each gap closed. README kept
as the index of what each test guards.

## Tracked gaps

| Test | Gap class | Plan ref | Status |
|------|-----------|----------|--------|
| `ii-scheduler-reorder.ll`       | SCHEDULER (S1)  | Phase 0 | PASS — multi-slot packing |
| `ii-hwloop-pointer-iv.ll`       | HWLOOP (S2)     | Phase 0 | PASS — `set_hwloop_f2` |
| `ii-hwloop-multibb.ll`          | HWLOOP (S2)     | Phase 0 | PASS — multi-BB stays software |
| `ii-mul-native-not-libcall.ll`  | MUL lowering     | Phase 1 | PASS — native mul, no libcall |
| `ii-gpr-dr64-sext-no-spill.ll`  | SEXT GPR→DR64    | Phase 2 | PASS — no st32/ld64 round-trip |

## RUN line convention

```
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
```

`-global-isel-abort=1` is mandatory (no SDAG fallback). Default CPU carries
`FeatureHWLoop`, so HWLoop tests do not pass `-mattr=+hwloop`.
