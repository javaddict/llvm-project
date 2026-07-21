# Haydn legacy-retired TableGen islands (D488)

These `.td` files are **not included** by the live backend after D488
(Bundle128-only Mode-0/3 retirement, following D487/R4).

## Why they exist

Pre-D488 Haydn encoded multi-width Mode-0 / Mode-3 decode variants as
per-slot TableGen islands (`_M0S0`, `_M0S1`, `_M0S2`, `_M3S2`, …). That
path is retired: slot encoding is exclusively the `_S<k>_FLEX` families
in `HaydnFormats*.td` plus `BUNDLE128_FULL` in `HaydnCompositeFormats.td`.

`Haydn.td` and `HaydnAsmMatcher.td` intentionally do **not** include any
file in this directory. The files are kept only for historical reference
(diff archaeology, design notes) and must not be re-included without an
explicit design decision to reopen multi-width encoding.

## Archived files

| File | Former role |
|------|-------------|
| `HaydnInstrInfoM0.td` | Mode-0 s1 ALU64 funct-routed `_M0S1` variants |
| `HaydnInstrInfoMACM0.td` | Mode-0 s1 MAC accumulate `_M0S1` |
| `HaydnInstrInfoMACM1.td` | Mode-1 (1011) MAC variants |
| `HaydnInstrInfoM3.td` | Mode-3 s2 MAC `_M3S2` |
| `HaydnInstrInfoS0LSM0.td` | Mode-0 s0 LS width `_M0S0LS` (D321) |
| `HaydnInstrInfoS1LDM0.td` | Mode-0 s1 LD `_M0S1LD` (D322) |
| `HaydnInstrInfoS0ALUM0.td` | Mode-0 s0 ALU32 `_M0S0ALU` (D323) |
| `HaydnInstrInfoS1ALU64M0.td` | Mode-0 s1 ALU64 flat encode (D325) |
| `HaydnInstrInfoS1ALU32M0.td` | Mode-0 s1 ALU32 sub-row encode (D327) |
| `HaydnInstrInfoS2ALU32M0.td` | Mode-0 s2 ALU32 sub-row encode (D328) |

## Live path (do not confuse)

Still active and **must not** be moved here:

- `HaydnFormatsALU32.td`, `HaydnFormatsALU64.td`, `HaydnFormatsLS.td`,
  `HaydnFormatsLD.td`, `HaydnFormatsMAC.td` — FLEX per-FU formats
- `HaydnCompositeFormats.td` — `BUNDLE128_FULL`
- Generic / formats / schedule / register `.td` under `lib/Target/Haydn/`

## CMake / TableGen

No CMakeLists entry or `include "..."` path points at these files.
Moving them out of the Haydn root does not affect TableGen inputs
(`LLVM_TARGET_DEFINITIONS` is only `Haydn.td` / `HaydnAsmMatcher.td`).
