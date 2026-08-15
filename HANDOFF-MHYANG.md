# Handoff: open items on `haydn-on-mhyang-2` (for mhyang)

2026-08-16, from ckchen's reconciliation line. Branch tip `3e8a693823cf`
(= fork). Full histories are reachable: the closing merge `ceec20e84125`
carries round-1 (`haydn-on-mhyang`) as second parent, which in turn
carries the original `haydn` line. Long-form entries live in
`OPEN-COMPILER-BUGS.md`; this file is the prioritized distillation of
what needs you, plus the decisions I made inside your design surfaces
that you may want to ratify or revert.

## Where the branch stands

```
gcc-torture -O3    1417 PASS / 0 FAIL / 0 timeout      (full house)
BundleSim ctest    222 / 223   (only cb100 = CB-151 tracking red, deliberate)
Haydn lit battery  880 / 1     (only ae-compat-selp24 = CB-150)
HaydnTests         460 / 460
CoreMark           521,503 committed bundles  (+0.76% vs round-1's 517,586;
                    the wave tip was +8.8%)
vacuous-NOT gate   0  (tool recalibrated for this tree; 122 dead guards removed)
llc strlen-5.c     1.38 s      (wave tip: 30.5 s)
```

## What needs you, in priority order

### 1. CB-154 — accumulating intrinsics have no accumulator input (correctness)

Golden says `MULSS32_HHLL: rtd = rtd - hh - ll` (DR_Read_Port lists
`rtd`), but `int_haydn_mulss32_hhll` is **two-argument** and selects via
`selectBinary`. The accumulator input is unmodeled at the intrinsic, the
builtin, the logical, and the MI — so the value accumulated at runtime
is whatever stale content RA left in the destination register. e2e tests
pass by allocation luck. 87 instructions are in this class (the
SMULA16/SMULS16 and FMULS16/FMULAA16/FMULSS16 grids, MULSS32/MULSA32,
F2MULAS32/F2MULSA32, MOVT64/MOVF64, MOVEI_H/L, ...); the generator's
fail-closed pin prints the full list if you flip
`len(divergent_non_ls) != 87` in
`FormatE/generate_format_e_records.py`.

The fix starts at the intrinsic/builtin signatures (3-arg accumulate
forms) and flows through `haydn_dsp.h` — your public API surface, which
is why I did not guess at it. Once the logicals carry the tie, delete
the 87-pin and the member generator inherits the ties automatically
(that machinery landed in `1a3c67de4344`).

### 2. CB-151 — the AR-ua logical shape is a fabrication vs golden (correctness)

Golden's whole UA family has **no rs2 and no dir_sel**
(`D_LTWUA_POST rtd, ar_sel, rs`; Behavior reads only rs and
ar[ar_sel], writes rtd / ar[ar_sel] / rs+8). The five-operand LLVM
logical is not in the ISA, and the 3-field wire member the generator
emits IS the correct shape — the "dropped operands" of the encode
residual are phantom limbs. But the public builtins
(`int64_t(void const*, int, int, int)`) mirror the fat form, so the
real fix reshapes the AE-compat lowering to expand the 4-arg public
semantic onto golden-shaped ops. Same layer as CB-150; your intent
needed. cb100_ar_unaligned stays red as the tracking signal until then.

### 3. CB-153b tail — the HR never packs at schedule time (+0.76%)

The density story is 90% closed (see the numbers below), and what
remains is structural: the hazard recognizer accepts each pick into its
own hazard cycle with an open-cycle member placement and advances, so
ALL pairing happens at leaveMBB. The last ~9 multi-issue bundles on
core_matrix (214 vs round-1's 223) and the +0.76% live there.

Two things you should know before touching it:

* A commit-time assignment-ladder prototype (retry mirrored/other-row
  placements) recovered density but **trips
  `syncSlotMapFromPreferred`** — re-baking member entries at commit
  time desyncs the HR fixed-claim bookkeeping. The re-bake has to be
  owned by the scheduler side. The landed fixes deliberately stop at
  ONE deterministic re-solve (plus one reversed re-bind for field-RAW
  flips) precisely to stay outside that machinery.
* One observation, flagged not changed: `countSFRPorts` charges every
  **dead** implicit `$sfr` def as the exclusive SFR writer
  (PackLegality rule 3). Compiled MIs no longer carry those defs, so
  this is inert for compiled code today — but the HR conflict model
  still applies rule 3 to dead defs while my commit-surface port gate
  counts live SFR writes only. Worth unifying when you're in there.

### 4. CB-150 — AE tier regeneration (your machine)

Pre-existing at `1c740f0d5708`: tier inventory counts drift
(macros=600 surface=673 td_tiers=661) and `ae-compat-selp24-f24-satshift.c`
expects `llvm.smax`-shaped compat IR the current headers no longer
produce. Needs your tier regeneration workflow; not guessed at here.

### 5. Plans-machine chore — canonical vectors vs the repaired golden

`format_e_canonical_vectors_v1.json` exists only on your machine and
embeds the golden layout hash in its oracle block. Both importers are
pinned to the **repaired** golden now
(`8465132c…` layout, `e77908e9…` index — the two deterministic repair
passes, byte-reproducible from the delivery). Regenerate the vectors
against that, and the `--check` lit gates (currently loud-SKIP behind
the `haydn-golden-canonical` feature) come back everywhere.

## Decisions made inside your design surfaces — ratify or revert

Each is an isolated commit with the full argument in its message.

| Commit | What I decided | Why |
|---|---|---|
| `e8b92d12b1b1` | LLD `trapInstr` back to **zero**; `trap-fill-not-zero.s` rewritten as `trap-fill-zero-inert.s` | Your idle-seeded 4-byte filler put a 0b111 indicator into sub-parcel residues (aligned(N) functions leave 4/8-byte gaps that cannot hold a bundle); the linear-sweep disassembler decoded a phantom bundle across the next function's entry and BundleSim rejected the image (gcc-torture align-3, end-to-end red → green). Whole-parcel padding still uses your idle parcel via nopInstrs. |
| `6cdc8f406d64` | Closed singletons settle on the **E2** row | Your doc (`ProductDefaultRowID`) and your own MIR test (`singleton-bundle-formatid` pins `BUNDLE 0`) against two mechanism-derived unit pins; policy artifacts won. E3-only menus keep E3. |
| `1a3c67de4344` | Members of tied logicals mirror the tie (tied acc input first in `(ins)`, no encoded bits) + per-slot AccFirst itineraries | Member Desc arity must match the MI it setDescs onto (machine verifier was tripping on F2MULAA32R); the finalize DropTies path was silently erasing accumulate deps from post-finalize MIR. Tie set = golden Write∩Read ports ∩ TD-modeled ties, no hand list. |
| `ef9d799e282c` | `exactSolveProductOpcodes` output coherence (re-bind onto one settled row, correcting `Plan.Row` when it is the stale side) + explicit port-budget gate on the commit surfaces | The solver returned mixed-row member lists (`row=0` carrying `_E3_` members) — every consumer choked; and with coherent members a 3-GPR-write cycle committed (GPR is 2W) because the commit surfaces bypass the HR and the budgets were only ever enforced by the incoherence's garbage. |
| `9a794b762320` + `1d9f2b5a87fa` | Single-shot mixed-member re-solve at commit, plus one reversed re-bind when the only failure is the field-order law | This is the density recovery (numbers below). One deterministic assignment per attempt, identical validation to the as-is path, never the `Bundle<MCInst>` exactPack path. |
| `3e8a693823cf` | `haydn_vacuous_not.py` recalibrated; 122 dead CHECK-NOTs deleted | The old RETIRED list encoded round-1's retirements; this tree un-retired most of them (your printer emits logical mnemonics again; `_W`/`_S<k>` residuals live in MIR). The liveness check against the generated tables was already precise and now does the judging. |
| round-2 (`8a8e6776a3f5`) | Both golden pins repointed to the repaired delivery | Same repair verdict as round 1: the only live-member delta is `X4SEL16_E3_E1_ALU1_RRR`; your typed-MemberId serialization reproduced the rsd1/rsd2 swap byte-for-byte until the canonical-order emission was re-applied. |

## The CB-153 numbers, condensed

| Stage | core_matrix multi-issue | core_matrix bundles | CoreMark committed |
|---|---|---|---|
| wave tip (`11b1d70b`) | 107 | 884 | 563,224 (+8.8%) |
| + solver coherence/ports | 107 | 884 | (unchanged) |
| + single-shot re-solve | 199 | 777 | 523,456 (+1.1%) |
| + reversed re-bind | **214** | **772** | **521,503 (+0.76%)** |
| round-1 reference | 223 | 765 | 517,586 |

Compile time (CB-153a, fixed): the tryCandidate ready-subset auction
re-ran the full subset/permutation solve per comparison (~2.3 ms each).
Three decision-identical layers (opcode-multiset memo, per-pick SU
cache, score-only twin) took strlen-5 llc from 30.5 s to 1.38 s;
proven by an 845-file byte-identical corpus compare and a
twin-equality unit test (~4,200 combinations).

## Reproduction rig

* Round-1 A/B toolchain: `~/haydn/llvm-r1wt` (worktree of
  `8be849e16400` + build). Mixed-toolchain LINKS are ABI-incompatible
  across GE96-03 (branch fields changed scale; BundleSim rejects such
  images with "direct control target is not an exact code record") —
  compare app objects statically, don't link across.
* Density one-liner: objdump the object, count `{...}` lines and the
  non-nop ops inside; the exact script is in the CB-153 commits'
  messages and `scratchpad` history.
* Key tests: `postmisched-hard-root-exact-commit.mir` (three-ADD
  sequentializes BY the port gate now, not by accident),
  `CoissueProductCycle_AntiPreservedBySolve` /
  `_AntiNotPreservable_StAdd` (the WAR field-order pair),
  `ScoreOnlyTwinMatchesFullAuction` (auction twin equality),
  `trap-fill-zero-inert.s` (both directions of the filler history).
