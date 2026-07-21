# D490 — SSA EarlyIfConversion replaces GenMux Pattern 2 (CB-117)

**Status:** accepted  
**Date:** 2026-07-14  
**Related:** CB-117 seed3148 MEMORY_FAULT (`movf` of 0/−1 used as load base)

## Decision

Adopt the **preferred architecture** from the Hexagon / AIE / generic-LLVM survey:

1. **SSA EarlyIfConversion** (generic pass) + **`HaydnInstrInfo::{canInsertSelect,insertSelect}`** emitting `COPY` + `MOVT32`/`MOVF32`.
2. **Keep** GISel **`G_SELECT s32 → MOVT32`** (already correct).
3. **Retire GenMux Pattern 2** (post-RA branch+MOVE diamond → CMOV). No longer invoked.
4. **Keep GenMux Pattern 1** only as an **optional** post-RA fuse of the s64 bitwise select chain (not required for correctness). Flag: `-haydn-enable-gen-mux` (still default on for Pattern 1 only).

## Why not keep GenMux Pattern 2

GenMux Pattern 2 hand-edited CFG after RA:

- Erased the cond branch and **dropped Join from successors** when Join was not layout-next.
- Relied on **accidental fallthrough** past an emptied MoveBlock.
- Small tests stayed green; **BlockPlacement** reordered a critical-edge `ld` after the CMOV → load through `0xffffffff` (seed3148).

That class of bug is exactly what Hexagon’s EarlyIfConv avoids by always rebuilding control with **real successors + terminators**.

## References used

| Source | Role |
|--------|------|
| **HexagonEarlyIfConv** | SSA diamond/triangle, PHI→MUX/select, explicit CFG rebuild (`addSuccessor` + jump + `updateTerminator`) |
| **HexagonExpandCondsets** | Post-SSA mux expand with LiveIntervals (not needed yet for Haydn) |
| **EarlyIfConversion.cpp** | Generic speculate + `insertSelect` (AArch64 CSEL, etc.) |
| **AIE (Peano)** | `enableEarlyIfConversion` + `EarlyIfConverterLegacy`; branch→SEL map; **no post-RA GenMux** |
| **ARM MVE VPT** | Not used — vector then/else blocks, not scalar cmov diamonds |

## Can we fully retire GenMux?

| Piece | Retire now? | Reason |
|-------|-------------|--------|
| **Pattern 2 (branch CMOV)** | **Yes** | Replaced by EarlyIfConv + insertSelect |
| **Pattern 1 (s64 bitwise→CMOV)** | **Not yet** | s64 `G_SELECT` still emits lo/hi bitwise chain; Pattern 1 is a size/speed opt only. Delete when s64 is dual-MOVT/MOVF or similar at isel. |
| **Whole pass** | **After s64 isel cleanup** | Then default off / remove pass registration |

So: **Pattern 2 is retired today.** The pass file remains as Pattern-1-only until s64 lands a direct cmov path.

## Implementation sketch (landed with this decision)

```text
HaydnSubtarget::enableEarlyIfConversion() = true
HaydnPassConfig::addILPOpts → EarlyIfConverterLegacyID

HaydnInstrInfo::canInsertSelect
  → Cond = [BEQZ_W|BNEZ_W, CondReg], GPR32 only

HaydnInstrInfo::insertSelect
  → single SSA def: %Dst = MOVT/MOVF %False(tied), %True, %Cond
  → (no dual-def COPY+MOVT — that broke MachineCSE)

HaydnSchedModel::MispredictPenalty = 8
  → EarlyIfConv CritLimit = 4 so pure PHI triangles convert
    (was 2 → CritLimit 1 → "Not enough available ILP")

HaydnGenMux::runOnMachineFunction
  → Phase 1 only (OR32 bitwise); Phase 2 not called
```

## Test expectations

- `cb117-genmux-branch-cmov-join-edge.ll` — land 0/−1 + add + store (e2e).
- `cb117-genmux-branch-cmov-join-edge.mir` — historical CFG unit for Pattern 2 (still valid if someone re-enables the function for archaeology; Phase 2 no longer runs in production pipeline).
- New: EarlyIfConv-oriented IR test that a simple diamond becomes `movt`/`movf` without a live branch over a move-only block (when heuristics accept the diamond).
- seed3148 e2e: no MEMORY_FAULT on land path (integration).

## Non-goals (this decision)

- Full Hexagon-style **predicated stores** / speculation of multi-instr side blocks beyond what generic EarlyIfConv allows.
- MVE VPT-style vector predication.
- Dual-MOVT s64 isel (follow-up to delete Pattern 1).

## Follow-ups

1. MIR/IR lit that forces EarlyIfConv on a diamond and CHECKs `movt`/`movf`.
2. seed3148 full MATCH host.
3. s64 G_SELECT → paired MOVT/MOVF → delete GenMux entirely.
4. Optionally mark `MOVT32`/`MOVF32` `isSelect = 1` for `optimizeSelect` later.

---

# D491 — Full GenMux retirement (s64 dual MOVT)

**Status:** accepted  
**Date:** 2026-07-14  

## Decision

- `G_SELECT s64` → unmerge + **dual MOVT32** + merge (isel).
- **Delete** `HaydnGenMux.{cpp,h}`, CMake entry, pass registration, `-haydn-enable-gen-mux`.
- Pattern 1 no longer needed; Pattern 2 already retired in D490.

## Why

Bitwise NEG/AND/NOT/OR chain for s64 existed only to feed GenMux Pattern 1.
Direct dual MOVT matches s32 contract (bit0 Cond) and removes the last
post-RA CMOV rewrite pass.

## Tests

- `select-s64-dual-movt.ll`
- Existing `muxgen-bitwise-select.ll` / `early-ifcvt-insert-select.ll` / `genmux-cmov.ll` (rewritten for EarlyIfConv)
