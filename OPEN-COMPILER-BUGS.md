# OPEN Haydn compiler bugs blocking BundleSim

> **STATUS (2026-07-24; runtime packaging refresh 2026-08-07)** — live tools:
> `$HAYDN_BIN` / BundleSim `build/`.
> Product path: `build/BundleSim` + `build/run_c` + board `haydn_bsp` +
> toolchain sysroot (`llvm-libc` + `libclang_rt.builtins` from
> `scripts/build_haydn_llvm_libc.sh` / `build_haydn_compiler_rt.sh`).
> Torture gate: **`scripts/run_gcc_torture_lit.sh`** (llvm-lit + freestanding
> link + BundleSim). Upstream clang disables = llvm-testsuite
> `execute/CMakeLists.txt` `TestsToSkip` (**84**). Lit-enabled = **1430**.
>
> **gcc-c-torture/execute lit-enabled @ -O3** (2026-07-24 full lit retest):
>
> | Result | Count | Note |
> |--------|------:|------|
> | **PASS** | **1408** | GUEST_EXIT 0 (incl. CB-133/135 + complex-5) |
> | FAIL | 14 | freestanding link / target / harness only |
> | TIMEOUT | 1 | `920501-6` default 120s budget |
> | Haydn UNSUPPORTED | 7 | hang×5 + freestanding×1 + target×1 |
> | Upstream TestsToSkip | 84 | not run (clang unsupported / known fail) |
> | Total `.c` | 1514 | log: `/tmp/bundlesim-$UID/lit-residual.log` |
>
> Earlier O2 ad-hoc runs (pre-lit) kept for history only:
>
> | Run | PASS | skip | ICE | ABORT | other | note |
> |-----|------|------|-----|-------|-------|------|
> | baseline (pre CB-126..130) | 1312 | 86 | 47 | 31 | 38 | old `run_torture.py` |
> | after CB-131 VAARG | cluster | — | 0 crash | residual | residual | va-arg cluster |
>
> ### Currently OPEN (compiler / runtime product)
>
> | ID | Pri | Class | Tests / symptom |
> |----|-----|-------|-----------------|
> | **CB-163 CLOSED 2026-08-26 (2nd retest)** | — | **GONE on fresh toolchain `c6200933db5d`** | Exact ledger repro (`./build/run_c -O0/-O2 --bundle-limit 60000000 $BS/tests/fir/fir_gate_blk.c -I …kernel_v4/include`): **guest_exit=0 both levels** (O0 54,209,472 bundles / O2 12,262,784). Objdump of the -O2 object: **1198/1198 `move32_dr_[lh]` take a `dN` source, 0 `rN`-source**. First "repro" was an in-flight `.so` relink (stale `libLLVMHaydnCodeGen` labeled MOVE32 c1=0x2 rows as MOVE32_DR_L c1=0x4); the fresh-build retest shows no defect. Full row + original evidence in git history 2026-08-26. |
> | **CB-164 CLOSED 2026-08-26** | — | **FIXED: Fixup measured Off1/Off2 from AFTER the SET cycle; MC encodes from the SET parcel BASE** | Root cause: `HaydnFixupHwLoops::computeOffsets` measured StartOff/EndOff starting at `nextBundleBoundary(SetMI)`, but the encoder anchors HWLoopOff1/Off2 at the SET parcel's own base (`HaydnAsmBackend::evaluateFixup` seeds `Value = Abs % Parcel` so MCAssembler's PC-rel subtract lands on `align_down(fixup_loc, 12)`). Every encoded displacement was one parcel (12 B) larger than Fixup's accepted value, leaving a one-parcel blind slot at the uimm6 ceiling: pr51581-1 `f9`/`f11` at -O2 measured 252 B (accepted ≤ 252 cap) and encoded 264 B → `264>>2 = 66 > uimm6 63` → `error: relocation offset out of range` at object emission. Why fir_gate was caught but div-constant not: fir_gate's overflow was by whole parcels (measured value itself > 252 → demoted); the div-constant fat preheader landed measured exactly AT 252 — inside the blind slot. Fix (same commit as this row): `computeOffsets` charges the SET cycle's committed `EncodedBytes` (root via `haydn::hwloop::topLevelForLayout`) onto both offsets; the setup timing floor moves to `MinSetupIssueBytes` (= `MinSetupBytes` + one parcel, same accepted geometry); delta-based consumers (body-min-law, pads) are anchor-invariant. The two offending loops now demote to software loops (`startOff=264 > 252` → product demote-first); all other hwloops keep hardware forms. Law helper `haydn::hwloop::anchoredFromAfterSet` (HaydnHWLoopContracts.h) is the sole after-SET→encoded bridge, consumed by `computeOffsets` and sealed by gtest `HaydnHWLoopContractsTest.OffAnchorIsSetParcelBase` (bridge identity, −1 passthrough, CB-164 boundary 252→264→66>63, one-under=ceiling encodable, `MinSetupBytes→MinSetupIssueBytes` floor) plus compile-time static_asserts. MIR pin: `llvm/test/CodeGen/Haydn/cb164-hwloop-off1-set-parcel-anchor.mir` (21-parcel tail demotes; 20-parcel tail = encoded 252 exactly → hardware form SURVIVES, no over-demote); MC-side anchor boundary already sealed by `MC/Haydn/reloc-range-hwloop-div4-boundaries.s` (`.space 228`→field 60 pass / `.space 252`→264→OOR — the compiler-side instance of the same law). Verification: `clang -O2 -c pr51581-1.c` clean; torture `-O2 --filter 'pr51581'` → **pr51581-1 AND pr51581-2 both PASS** (compile+execute, guest_exit=0); full Haydn lit **1024 discovered: 1009 pass / 0 fail / 14 unsupported / 1 XFAIL** (`ar-unaligned-roundtrip.s`, pre-existing CB-151); all 14 hwloop-Fixup tests pass; HaydnTests gtest binary **595/595**; full BundleSim ctest 683/683. Original filing text in git history 2026-08-26. |
> | **CB-165 CLOSED 2026-08-27** | — | **GONE on `ead16580abae` (w68-new, post-W68.4 restamp)** | Retest on current HEAD: torture `-O2 --filter ^pr51581-2.c$` → **PASS** (guest clean, 1/1); sibling `pr51581-1.c` also PASS. Likely fixed by the W68.4 wave (identity-bake seats / commitLate wrap-only / restamp ead16580abae) or the CB-162/CB-164 lineage. Original filing text in git history 2026-08-26.
> | **CB-160 CLOSED 2026-08-26** | — | **FIXED: `tryFoldMove32DrToSw` double-scaled the ST32 offset** | Root cause: `HaydnPostSelectOptimize::tryFoldMove32DrToSw` (GISel post-select peephole, O1+ only — hence clean -O0) folded `MOVE32_DR_L/H + ST32` into `D_SW_L/H_WITH_IMM` but divided the ST32 offset by 4, assuming a byte offset. ST32 (golden `S_SW_WITH_IMM`) and `D_SW_L/H_WITH_IMM` share ONE word-scaled EA law `EA = rs + (imm6 << 2)` (ISS `ls_ea_with_imm(...,2)` both; NOTION-closed (a) probes confirm), so the already-word-scaled ST32 imm must pass through UNCHANGED. Effect in `fft_stage_inner_DFT4_16x16_ie`: first pair store lowered to `MOVE32_DR_L + ST32 %v, %y16, -4` (EA y+0) and the fold rewrote it to `d_sw_l_with_imm -1` (EA y+12): y[0] never stored, y[12] clobbered each iteration. Fix (same commit as this row): pass-through imm, keep `isInt<6>` gate. MIR pin: `llvm/test/CodeGen/Haydn/cb160-lane-store-fold-word-scaled-imm.mir` (imm out == imm in, ±). Verification: repro PASS -O0/-O1/-O2/-O3 (Y0=000004cc); full Haydn lit 1008 pass / 0 fail / 14 unsupported / 1 XFAIL (`ar-unaligned-roundtrip.s`, pre-existing CB-151) — identical to pre-fix baseline; ALL six fft-family dual-path gates PASS (g1 12, g2 33, g3 27, g4 4, g5 26, g6 float); picojpeg embench BOTH tests PASS (`bundlesim_embench_picojpeg`, `bundlesim_embench_v2_picojpeg`) — the same value-class as the CB-160-family picojpeg note. Original filing text in git history 2026-08-26. |
> | **NOTION-closed 2026-08-26 (a)** | — | "D_SW_L double-scaled imm" — NOT REPRODUCIBLE on `c6200933db5d` | 8 semantic probes on BundleSim, all match golden `instruction_type_index.json` exactly: d_sw_l imm=1→word 1 (`imm<<2` ✓), d_sdw imm=1→word 2 (`imm<<3` ✓), composed ptr+1&imm=2→word 3 ✓, d_sw_h imm=2 ✓, reg-form unscaled `rs+rs2` ✓, post/pre writeback forms ✓, imm sweep 0..3/−1/−2 ✓. Compiler obj bytes = hand-asm obj bytes (identical `87 43 08 11…`). If this notion came from a real failure it predates the current artifact; reopen only with a self-contained repro. |
> | **NOTION-closed 2026-08-26 (b)** | — | "picojpeg final-MIR-present-but-not-emitted" — FALSE ALARM (measurement artifact) | Full per-function, per-opcode compare of terminal-verify MIR vs emitted asm across all 11 picojpeg functions: **11/11 exact real-op match**. The apparent diffs were three parser artifacts: (1) MIR member names (`S_LBU_WITH_IMM_E2…`) vs public mnemonics (`ldu8`) — mapped via the generated td.inc table; (2) `nsw/nuw/disjoint` flags sit between `=` and the opcode; (3) late legal expansions `B`→`beqz`, `BR_JT`→`jalr` (member print), `RET`/`JALR_MSP`→`jalr_w`. NOP deltas (+47..+2649/parcel) are slot pad fill by design. Zero dropped instructions. |
> | **CB-158 CLOSED 2026-08-26** | — | **GONE on `c6200933db5d`** | `ctest -L gcc-compat`: **51/51 PASS incl. `vector-2` (3.40s), `vector-2a` (2.93s), `vector-2b` (3.05s)** — the exact triples that guest-ABORTed at filing. 64-byte generic float vector compat lowering now correct at the suite -O2. Full row (host-oracle acquittal, v16sf/v8sf/v4sf detail) in git history 2026-08-26. |
> | **CB-126 residual** | P3 | GISel legalize | any remaining non-pow2 / width MMO edge cases outside torture green set |
> | **CB-153** | P2→(a fixed) | wave scheduler performance | Residual half only: schedule-time HR never packs (open-cycle members + per-cycle advance leave pairing to leaveMBB) — the last ~9 multi-issue bundles and +0.76% CoreMark vs haydn-on-mhyang. Owner: post-RA scheduler cycle formation / HR admission (CB-147 exact re-solve density analog). Full history (a FIXED auction blowup; (b) density single-shot bridge + reversed re-bind recovery: core_matrix multi-issue 107→214 vs 223 baseline, CoreMark +8.8%→+0.76%) in git/file history 2026-08-15. |
> | **CB-154 UPDATED 2026-08-26** | P2 (scope shrunk) | unmodeled destination reads — residual 4 families | Generator pin now expects **`len(divergent_non_ls) == 4`** (generate_format_e_records.py:4627): the 2026-08-19 S2b waves migrated MULSS32/MULSA32, SMULA16/SMULS16, FMULS16/FMULAA16/FMULSS16, F2MULAS32/F2MULSA32, X4CLAMP16 etc. to generated tied defs (87 → 46 → 4). Remaining 4 = partial-write/conditional-move families (MOVEI_H/L, MOVF64/MOVT64, X2MOVF/T32, X4MOVF/T16) where golden reads old rtd to preserve the unwritten half; fix requires explicit old-destination operand through builtin→IR→GISel→member chain. Tracked under GOALS partial-write taxonomy. |
> | **CB-151** | P2 (sharpened 2026-08-15) | AR-ua encode | The D-side unaligned-window post ops encode through the bag-by-class member binding; golden's whole UA family carries NO rs2 and NO dir_sel (`D_LTWUA_POST rtd, ar_sel, rs`) — the five-operand LLVM logical shape is a fabrication against golden, and the wire member (3 fields) is the CORRECT shape. The public builtins (int64_t(void const*, int, int, int) in haydn_dsp.h) mirror the fat form, so the real fix reshapes the AE-compat LOWERING to expand the 4-arg public semantic onto golden-shaped ops — owner's API-intent territory (same layer as CB-150), not a generator patch. `ar-unaligned-roundtrip.s` XFAIL w/ OWNER note is the tracking signal (overlaps GE96-09 classification). |
> | **CB-150** | P3 | AE tier machinery | Tip mid-stream state, pre-existing at 1c740f0d5708: `ae-tier-audit.test` inventory counts drift (macros=600 surface=673 td_tiers=661), `ae-compat-tier-closure.c`, and `ae-compat-selp24-f24-satshift.c` expecting `llvm.smax`-shaped compat IR the current headers no longer produce. Needs the tier inventory regeneration workflow (owner's machine) — not guessed at in the merge. |
> | **CB-166 PARTIAL 2026-08-27** | P2 (perf) | VLIW scheduler: load-in-MAC-bundle enabled by SMS (033372428db8); strict {2MAC+LD} triple still absent | FIR-class 32x32 kernels stay at **avg 1.0 MAC/bundle / peak 2.0** (8 bundles per 4-out group) instead of the vendor HiFi3z **2.0 sustained** `{LD + LL_S2 + HH}` (2 MACs + load per cycle). Verified on toolchain `ead16580abae` with a 3-line repro (below): the scheduler emits each `ff2mula32r` in its own bundle and never places a `d_ldw_post_imm` in a MAC bundle. **Not a miscompile** — bit-exact everywhere; it is the perf ceiling for the whole FIR/dot triple. **RETEST-2 2026-08-27 on `e4c7077e7e2c` (fix 033372428db8): PARTIAL — harness `benchmarks/naturedsp_kernels/tools/cb166_retest.sh` (exit 3): loads NOW ride MAC bundles in the SMS-pipelined kernel ({MAC+LD}>0, core density 1.00→1.60 MAC/bundle, baseline-shape fn bundles 1.00M→954k), but the strict `{2MAC+LD}` triple is still 0 everywhere and straight-line T1 is untouched (SMS is loop-only). Side effects: -O3 baseline shape now beats the ldw.strand port (swizzle chain blocks the 5-bundle pipeline); -O2 unchanged ordering. Full numbers in the section below.** |
>
> **CB-162 FIXED (2026-08-24; row moved out of OPEN):** the "-O3 unroller
miscompile" was NOT the unroller — IR was proven innocent by a host-x86
oracle run of the -O3 IR (both arms bit-identical). Root cause (bisected
via MIR resume probes + ISS trace + bundle-limit memory dumps):
`demoteHardwareLoopToSoftware` (HaydnHardwareLoops.cpp, reached from
HaydnFixupHwLoops when hard Off1 was unencodable) used the hwloop trip
register as the software SUBI32/BNEZ_W countdown WITHOUT checking it was
dead after the loop. In main's inlined init the h-fill loop's trip
register still held the live descriptor M (and in a second instance
NBLK); the countdown zeroed it, the descriptor said M=0, and
bkfir16x16_process silently skipped its whole fast path (frozen ysum;
process body ~22k bundles instead of ~1.2M). Fix (three closed rules,
one mechanism):
1. `canUsePreferAsCounter` — Prefer may be the countdown only when dead
   at every loop exit (LivePhysRegs live-in of each exit successor).
2. `pickCounterReg::isUsable` — the SAME dead-after-loop law for the
   picked fallback counter (second instance clobbered NBLK via R10).
3. Value-preserve completion — when every GPR is live-out, the
   stack-counter demote saves Prefer to a save FI before the loop
   (shared PostRAScratchFI word when disjoint from the counter FI; rare
   dedicated HwLoopDemoteSaveFI otherwise) and exact-commit reloads it
   at the exit block entry; Prefer itself may then serve as the latch
   scratch. No frame growth in the normal case (lit 1084 green, zero
   CHECK rebaselines).
Pins: `llvm/test/CodeGen/Haydn/cb162-hwloop-demote-liveout-tripreg.ll`
(ZOL reads the trip reg; a later store of the same reg keeps its value;
no software countdown on it) + the end-to-end BundleSim repro
`benchmarks/naturedsp_kernels/tests/fir/bkfir16x16_o3_unroll_repro.c`
(ysum varies, matches -O2 arm) and real bench
`bench_bkfir16x16.c` @ -O3: sink=1093774 bit-exact, 1.24M bundles
(-70% vs -O2) — the -fno-unroll-loops workaround is no longer needed.
Full row in git history.

**CB-159 FIXED (2026-08-21; row moved out of OPEN 2026-08-23):** sha256
> -O2 legalizer hang (`G_BUILD_VECTOR` of `<N x s1>` retried forever) +
> wrong digest (SLP non-splat `lshr <2 x i32>` lane-0-only shift) — both
> fixed; s1 lanes pack for freeze/bitcast, non-splat constant vector
> shifts scalarize. `guest_exit=0` at -O2. Full row in git history.
>
> **CB-152 CLOSED (all three sub-items settled 2026-08-15; row moved out
> of OPEN 2026-08-23):** (a) bare-mnemonic pins re-pinned DONE; (b)
> singleton E2-preference SETTLED (policy artifacts win;
> exactSolveLateSingleton + finalize resettle swap lone e3_* to E2
> sibling); (c) MAC-accumulator member ties FIXED (generator derives
> ties from golden Write∩Read ports; AccFirst itineraries; tied-use
> late-read grace). Full row in git history.
>
> **CB-61 FIXED (minted 2026-08-17, W54):** `-O0` clang abort in
> `LegalizationArtifactCombiner::tryCombineTrunc` via
> `MachineIRBuilder::validateTruncExt` ("invalid widening trunc"). See
> dedicated section below. Not OPEN.
>
> **CB-161 FIXED (2026-08-21, commit b8c79a754d3b):** scheduler dual
> SFR-write co-issue (`x2mux32` + `x4mux16` unit-injective fatal). See
> dedicated section below. Not OPEN.
>

### Closed — CB-161 dual SFR-write mux packing (2026-08-21)

**Was:** at -O1/-O2 the `intrin_x2x4_sfr_cmp` case fataled with
`Haydn bundle invariant violated ... structural inverse: chosen Format E
members are not unit-injective` — the scheduler packed two SFR-writing
members (`x2mux32` + `x4mux16`) into one bundle, violating SFR 2R/**1W**.
CB-153's landed port-budget gate charged only *anonymous* implicit `$sfr`
defs; the generated mux members' SFR write was named through their
logical-desc surface and slipped past on both commit surfaces.

**Fix (commit b8c79a754d3b, `[Haydn][GE96-11] OCC-FLIP+TWIN-REMATCH +
CB-161 SFR counting`):**
- `HaydnPortModel.h` / `HaydnHazardRecognizer.cpp` — `countSFRPorts`
  extended to count SFR traffic of generated members via logical-desc
  naming.
- `MCTargetDesc/HaydnMCFormats.cpp` — `HaydnBundlePortBudget` SFR-writer
  gate now sees the member-level SFR write on every commit/pack surface.
- Commit-site unit-twin rematch: on same-golden-unit duplication, retry
  the golden unit-twin (`FormatEMembers` sibling with same
  logical/Mode/EntryIdx, different Unit) — fail closed when no twin
  exists.

**Pins:**
- `llvm/test/CodeGen/Haydn/cb161-dual-sfr-mux-separate.ll` (regression:
  two SFR-writing muxes must land in separate bundles).
- `unittests/Target/Haydn/HaydnPortModelSFRMemberTest.cpp` (188 lines
  covering the member-level SFR counting law).
- BundleSim `benchmarks/haydn_intrin/repro_2026_08_21_sfr_dual_mux_unit_injective.c`
  → `guest_exit=0` at -O0/-O1/-O2.
- Tracking ctest `bundlesim_reg_intrin_x2x4_sfr_cmp` PASS.

**Gate at commit:** full-gate green — HaydnTests 550/550, lit 1132+/0
Failed. Do not re-open unless the SFR member-level accounting is
removed.

### Closed same day — CB-155 FIXED; CB-156/CB-157 were wrong test goldens (2026-08-17)

**CB-155 FIXED**: `fieldSlotKeepOperands` failed closed on
`OldN == 0 || NewN == 0`, rejecting the one case with nothing to check —
an operand-less FieldSlot onto an operand-less member (ZERO_SFR and its
generated members). First corpus that bundles zero_sfr with a store
(intrin_simd_cmul, -O1/-O2) hit the ledger rebind with members standing at
entries contradicting their stamps, and the replanned 0↔0 rewrite tripped
the memberDescCompatible contract assert. Empty↔empty now returns the empty
keep map; asymmetric zero still fails closed. The sequential walk also
gained the same memberDescCompatible + ri6ImmFitsMember gate the rebind
path already had, so a genuinely incompatible resolution falls through to
the rebind instead of asserting. Pinned by
`finalize-zero-sfr-member-rebind.mir` (verified to assert without the fix).

**CB-156 / CB-157 WITHDRAWN — the compiler was innocent both times**: the
"failing" goldens contradicted arithmetic. multi_arg_abi expected
`a-b+c-d+e-f+g-h == 98` where every evaluation order gives 90; i64_divmod
expected `srem(INT64_MAX, 2^32) == 0x7FFFFFFF`, which is the QUOTIENT (the
remainder is 0xFFFFFFFF). Both test files abort on host x86-64 gcc -O2
exactly as on Haydn; both pass on both targets with corrected goldens (sim
commit d97da99). The forensics that briefly suggested "cross-call state
corruption" — same call correct once then aborting — were a misread of
which check aborted: the sdiv printed correct and the WRONG-golden srem on
the next line did the aborting. Lesson re-learned: HOST-ORACLE THE TEST
before filing the compiler; CB-158 survived that knife, these two did not.

## CB-61 — FIXED (minted 2026-08-17)

**Was:** clang `-O0 -global-isel-abort=1` abort on nested trunc/ext
cast chains. `LegalizationArtifactCombiner::tryCombineTrunc` folded
`trunc(trunc)` unconditionally; an earlier artifact combine could
narrow the inner source so `TruncSrc` was not strictly wider than
`DstTy`. `MachineIRBuilder::buildTrunc` then hit
`validateTruncExt` (`"invalid widening trunc"`) and SIGABRT'd llc.

**Fix (one guard, D1000 HC#0 exception):** skip the fold when
`MRI.getType(TruncSrc).getSizeInBits() <= DstTy.getSizeInBits()`.
Scoped to that one site in
`llvm/include/llvm/CodeGen/GlobalISel/LegalizationArtifactCombiner.h`.
Not a target hook.

**Pins:**
- `llvm/test/CodeGen/Haydn/cb61-o0-artifact-trunc-guard.ll`
- `llvm/test/CodeGen/Haydn/cb61-o0-postlegalizer-apint-width.ll`
  (follow-on APInt width hygiene after the guard landed)
- `llvm/test/CodeGen/Haydn/trunc-of-ext-identity-prelegalizer.ll`

**Ledger:** `contracts/pipeline.md` D1000 row. Do not re-open unless
the width guard is deleted.

### 2026-08-17 — provenance: mhyang's sim test corpus, first run against this toolchain

The batch above was exposed in one sweep: the BundleSim reunification (sim
`mhyang` @ b8cfe95) brought mhyang's new suites (haydn-codegen, haydn-abi,
gcc-compat, gcc-torture-struct/dg-struct, embench-iot-v2/dsp) onto this
toolchain (fork/haydn @ 583108abf5bb) for the first time — ctest went
245 → 641. Of the eleven reds that survived the first triage: one real
crash (CB-155, fixed), three real vector ABI reds (CB-158, open), two
wrong goldens (CB-156/157, withdrawn), four XPASS, one feature gap.
Two observations that are NOT bugs here:

- **XPASS ×4 — their byte-struct bug is fixed on this line**:
  `bundlesim_haydn_abi_s8/s9/s12/s16` are marked WILL_FAIL in mhyang's suite
  ("Haydn byte-struct codegen bug"); on this toolchain all four PASS
  (guest_exit 0), which ctest reports as failures. The WILL_FAIL flip belongs
  to whoever syncs their toolchain onto this line — flipping it from our side
  would break their own runs.
- **`__int128` is a feature gap, not a miscompile**: their
  `haydn_codegen_int128` expects `__int128` on haydn-unknown-elf; this clang
  rejects it ("not supported on this target"). Keep the test WILL_FAIL.

### Closed — CB-149 AR2/AR3 restored (2026-08-14, user decision)

The full 2-bit ar_sel domain is back: AR2/AR3 registers (64-bit, keeping
this line's width), the AR class, the five selector ArRegs tables and
their ArSel guards, the clang register-name list, and the seven ar_sel
ImmChecks (0_1 → 0_3; the dir_sel and setcbr checks are genuinely 1-bit
and stay). The interim retirement note claimed the architectural file
was AR0/AR1 "until golden classifies unused codes" — the classification
we have is the execution oracle: BundleSim's semantic model executes
`int64_t ar[4]`, the golden field is 2 bits, NatureDSP documents ar&=3,
and this line's own pre-retirement Sema tests expected [0, 3]. Verified:
ar_sel 2/3 round-trip assemble→objdump and compile→encode from the C
builtins (`pldwwua 2`, `flar 3`, `wbarwua 3`); Sema tests reopened to
[0, 3]. Execution coverage of ar2/3 UA streams lands when CB-151's
encode residual closes.

### Closed — CB-134 compile hang (verified fixed, 2026-08-14 merge audit)

The five hang files (`20001111-1`, `20170401-1`, `20180921-1`, `950809-1`,
`960312-1`) all compile cleanly at -O2 AND -O3 (`-std=gnu89
-Wno-everything -ffreestanding`, 90 s budget, exit 0 each). The fixes were
already on this line: the 2026-08-07 picks of the GISel legalizer
sub-byte-store livelock fix and the 64-bit vector MMO alignment fix are
exactly the pair that closed CB-134 on the haydn line. This entry had
simply never been re-verified after the picks.

### Merged from the haydn line (2026-08-14) — haydn-on-mhyang

See FORMAT-E-SWITCH-PLAN.md § 10 for the full account. Fix-relevant
deltas landed by the merge, all verified on this base:

| Item | What |
|------|------|
| Golden repin | Records regenerated from the repaired layout JSON (8465132c…). One live member changes: `X4SEL16_E3_E1_ALU1_RRR` had src1 hardwired 0 and a DR64 class on the GPR rs field. The same row's permuted field roles would encode rsd1/rsd2 swapped through the bag-by-class binding — generator now canonicalizes such members' (ins) order, pinned fail-closed. Test `x4sel16-e3-mapping-canonical.s` pins bytes for both states. |
| Golden placement law in the solver | The residual-slot solver accepted cycles with no (entry, unit) assignment and serialization fail-closed — the bf16mul "Format E one-parcel placement failed" crash. `haydnFormatEPlacementFeasible` (SDR over the generated catalog, same normalization as encode placement) refines every exact expand; `commitProduct` honors the refined row mask. Reproducer compiles at -O2 -filetype=obj: `two-store-placement-serialize.ll`. Unit law pins: two stores never co-issue; third MAC rejected; ld/ld/mac packs; SET_HWLOOP pairs with ADDI32, never ADD32. |
| Byte-scaled mem offsets + `areMemAccessesTriviallyDisjoint` | Plain LD/ST forms returned element indices; cross-width interval math was unsound both ways. All plain forms now scale; the disjointness hook (CB-148 on the haydn line) rides on top. Measured: bqriir32x32_df1 packs 131 ops in 115 bundles (was 118). |
| CB-144 ported (was live here) | `G_EXTRACT/INSERT_VECTOR_ELT` widen the element out of i1; the S1 `clampMaxNumElements` rows (assert-only, CB-130's lesson) removed. |
| pr28982 freeze hang ported (was live here) | `G_FREEZE` clamps its vector result; a `<16 x s32>` value no longer exists for consumers to chase in a loop. `freeze-wide-vector-no-hang.ll`. |
| Splice RAW fix | `spliceSkippablesForCycle` checked only defs; a skippable could be hoisted above its own producer. Symmetric predicate + range-exact hoist/sink ported. |
| DWARF line unit | `MinInstAlignment` 12 → 1: advances that are not whole parcels truncate and every later line address drifts (functions align to 4). `dwarf-line-bundle-addresses.s`. |
| `tryCSEConstantDR64` guards | The ADDI32/LOADI32 operand-kind aborts (frame index in the source, @global in the imm slot) now decline with a debug line. |
| Tip-stale tests aligned | 15 unit tests red against this line's own 08-10 entry-capacity/Option-A semantics; 2 lld tests still pinning `R_HAYDN_32` after typed `R_HAYDN_HWLoopOff1/2` returned. All aligned to measured law. |
| Tooling ported | `utils/haydn_encoding.py` (DB authority: --check 3686/126 self-consistent on this base), `haydn_vacuous_not.py` (reports 161 vacuous CHECK-NOTs here — corpus cleanup is follow-up), `haydn_ae_audit.py` (589 macros, 0 findings). `haydn_pack_probe.py` not ported (parses the retired encoding TD). |

### Closed — CB-137 / CB-138 / CB-140 SFR-class 2-op encoding (2026-08-10)

Same root: compiler emitted **3-DR** forms (`RR_DDD` / `x2seq32 d0, d0, d1`) for
ISA **2-op** instructions. BundleSim catalog pre-check →
`operand count disagrees with the golden catalog` / `ILLEGAL_INSTRUCTION`.

| ID | Scope | Evidence |
|----|--------|----------|
| **CB-137** | `x2slt32`, `x2movt32` (+ `x2cmplt32`/`x2mux32` lowers) | `bundlesim_reg_intrin_x2_cmpsel` **PASS** (2026-08-10) |
| **CB-138** | `x4slt16`, `x4movt16` (+ `x4cmplt16`/`x4mux16` lowers) | `bundlesim_reg_intrin_x4_cmpsel` **PASS** |
| **CB-140** | remaining 10: `x2seq/sle/movf`, `x4seq/sle/movf/movt` (+ siblings) | `bundlesim_reg_intrin_x2x4_sfr_cmp` **PASS** |

**Golden:** Format E `dest=rsd1,src=rsd2` / `rtd,rsd`; BundleSim `slot2_alu.h`
syntax `X2SEQ32 rsd1, rsd2`, `X2MOVT32 rtd, rsd`.  
**Fix:** residual `_S1/_S2` → `R_CMP` / `R_COND`; logical ops match; GISel 2-op
select. Lit pin: `llvm/test/CodeGen/Haydn/sfr-predication-intrinsics.ll`.

### Closed / fixed on lit-enabled gate (2026-07-24 wave)
>
> | Item | Evidence |
> |------|----------|
> | **CB-133** | `920501-8`, `930513-1` → **PASS** @ -O3 lit; baremetal `LIBC_CONF_PRINTF_DISABLE_FLOAT` overridden OFF in `libc/config/baremetal/haydn/config.json` (was writing raw `%f`/`%.0f` into buf) |
> | **CB-131 residual** | `struct-ret-1`, `va-arg-22` → **PASS** @ -O3 lit (retest 2026-07-24); no longer open |
> | **CB-135 di softfloat** | BSP `_SF_RELS` + `floatdidf`/`floatundisf`/`fix*di`… → `conversion`, `930622-2`, `pr49218` **PASS** |
> | **CB-135 residual complex** | `complex-5` → **PASS** @ -O3 full lit (GUEST_EXIT 0, `__divsc3` linked; was OPEN for `G_IS_FPCLASS` ICE) |
> | **CB-130 residual (torture)** | `pr60960`, `20050604-1`, `20060420-1`, `pr56866` → **PASS** (vector scalarize / FSHL s8 / UDIV / load) |
> | packed i72 post-inc | `pr57344-3` → **PASS** (no `D_LDW_POST_IMM` when align&lt;8) |
> | `%hhd` printf | `pr78622` → **PASS** @ -O3 full lit (GUEST_EXIT 0); optional `HAYDN_FREESTANDING_SKIP` if re-gated |
> | signed overflow harness | `950704-1` → **PASS** with `-fwrapv` (`TestRequiresFWrapV`) |
> | G_MERGE s16 | `20050316-1` → **PASS** |
> | G_FPTOUI s16 | `980605-1` → **PASS** |
>
> ### Confirmed FIXED earlier (2026-07-22 C+BundleSim)
>
> | ID | Pri | Evidence |
> |----|-----|----------|
> | **CB-126** (core) | P1 | `20040709-2/3`, `strct-pack-1`, `pr29006`, `pr53688`, `pr70903` → **PASS** |
> | **CB-127** | P2 | all `builtin-prefetch-1..6` → **PASS** |
> | **CB-128** | P2 | `20030323-1`, `20030811-1`, `pr17377` → **PASS** |
> | **CB-129** (core) | P2 | `comp-goto-1`, `20071210-1` → **PASS** |
> | **CB-131** (core) | P1 | `va-arg-1` + `va-arg-2` → **PASS** |
> | **CB-132** | P0 | clang SIGSEGV on va_arg cluster → **FIXED** |

## Lit-enabled fail inventory (2026-07-24)

Scope: llvm-testsuite `execute/*.c` **minus** upstream `TestsToSkip` (84).
Gate: `cd BundleSim && scripts/run_gcc_torture_lit.sh -O3 -j32`  
(with `LLVM_TESTSUITE` or `TORTURE_SRC` set; no hardcoded host paths in runner).

Upstream clang disables are **not** Haydn bugs — listed once in
`execute/CMakeLists.txt` (`UnsupportedTests` 76 + `FailingTests` 7 +
`990413-2` x86-only).

### A. ~~OPEN compiler — miscompile (CB-133)~~ FIXED

| Test | Result | Note |
|------|--------|------|
| `920501-8.c` | **PASS** | float printf enabled (haydn libc config) |
| `930513-1.c` | **PASS** | float printf enabled (haydn libc config) |

### B. OPEN compiler — compile hang (CB-134)

Lit marks these **UNSUPPORTED** (`HAYDN_COMPILE_HANG_SKIP`) so the suite
finishes; still **open compiler** until hangs are fixed or reduced.

| Test | Result |
|------|--------|
| `20001111-1.c` | compile hang (llc/GISel) |
| `20170401-1.c` | compile hang |
| `20180921-1.c` | compile hang |
| `950809-1.c` | compile hang |
| `960312-1.c` | compile hang |

### C. ~~Compiler-rt softfloat (CB-135)~~ FIXED

**Linked:** toolchain `libclang_rt.builtins.a` (+ legacy alias
`libclang_rt.builtins-haydn.a`) + `libm.a` when present.  
**Not libm** — int↔FP helpers live in LLVM `compiler-rt/lib/builtins`.

**Ownership (2026-08-07):** soft-int + soft-float are built by BundleSim
`scripts/build_haydn_compiler_rt.sh` (sources from `LLVM_SRC/compiler-rt`) and
installed with `scripts/install_haydn_sysroot.sh` next to llvm-libc. They are
**not** rebuilt by BundleSim `haydn_bsp` (board-only).

| Test | Was | Now |
|------|-----|-----|
| `conversion.c` | LINK missing `__floatundi*` / `__floatdi*` | **PASS** (di helpers in compiler-rt archive) |
| `930622-2.c` | LINK `__floatdidf` / `__fixdfdi` | **PASS** |
| `pr49218.c` | LINK `__fixsfdi` | **PASS** |
| `complex-5.c` | LINK `__divsc3` | **PASS** @ -O3 full lit (GUEST_EXIT 0, bundles=2842) |

### C2. ~~OPEN — soft-int div/mod miscompile at `-O2` (CB-1)~~ FIXED (2026-08-07)

| Field | Detail |
|-------|--------|
| **ID** | **CB-1** (closed) |
| **Was** | Soft-int div/mod TUs miscompiled at `-O2` (e.g. `6u/3u → 0`); product workaround was `-O1` for `{u,}div*`/`{u,}mod*` in `build_haydn_compiler_rt.sh`. Separately, `divdi3`/`moddi3` called missing **`__udivmoddi4`** (not in the curated archive) → link fail on 64-bit div cases. |
| **Fix** | (1) Backend: product RI Pats + SMS hard-BUNDLE header rebuild (`finalizeBundle` after tied two-addr) closed the densify miscompile path. (2) Runtime packaging: add `udivmoddi4.c` to `scripts/build_haydn_compiler_rt.sh`; build all soft-int at **`-O2`** (opt-out `CB1_FORCE_O1=1`). |
| **Rebuild / install (product)** | ```bash<br>export HAYDN_BIN=…/haydn-build/bin LLVM_SRC=…/llvm-head<br>export BUILD_DIR=…/llvm-libc-haydn-build   # tree with libc/lib/libc.a<br>scripts/build_haydn_compiler_rt.sh         # or full: build_haydn_llvm_libc.sh<br>scripts/install_haydn_sysroot.sh           # → $HAYDN_BIN/../sysroot + resource-dir<br>``` |
| **Evidence** | `cb1_div` / `cb1_div64` / `cb1_mod` / `cb62_seed8*` / `cb64_seed8*` / `cb74_min` ctest **PASS**; `cap_softint` / capability matrix **PASS**; nm gate includes `__udivmoddi4`. |
| **Related** | Do not conflate with CB-135 (di softfloat helpers — already fixed). |

### D. Timeout / budget (not miscompile)

| Test | Result | Note |
|------|--------|------|
| `920501-6.c` | TIMEOUT under default lit `--run-timeout 120` | **PASS** with `--bundle-limit 200000000 --run-timeout 300` (bundles=74241286) |

### E. NOT open compiler — freestanding / target feature / harness

| Test | Class | Why not CB |
|------|-------|------------|
| `built-in-setjmp.c` | COMPILE_TARGET | no `__builtin_setjmp`/`longjmp` on Haydn |
| `pr84521.c` | COMPILE_TARGET | same |
| `pr84748.c` | TARGET_SKIP | no `__int128` this phase — `HAYDN_TARGET_SKIP` |
| `loop-2f.c` | COMPILE_HOSTED | needs `sys/mman.h` (upstream skips only if `!HAVE_MMAP`) |
| `loop-2g.c` | COMPILE_HOSTED | same |
| `920302-1.c` | COMPILE | `-Wincompatible-pointer-types` as error; soft flags / Wno |
| `20030125-1.c` | FREESTANDING_SKIP | weak floor/sin; needs hosted builtins |
| `20020314-1.c` | **PASS** (BSP) | `plat/alloca.c` bump pool |
| `20021113-1.c` | **PASS** (BSP) | same |
| `20040223-1.c` | **PASS** (BSP) | same |
| `941202-1.c` | **PASS** (BSP) | same |
| `pr22061-1.c` | **PASS** (BSP) | same |
| `20020720-1.c` | LINK freestanding | `link_error` (test sentinel) |
| `fprintf-2.c` | **PASS** (BSP+lit) | `plat/stdio_extras.c` + lit `--bind guest_tmp:/tmp:rw` + baremetal fscanf ungetc |
| `printf-2.c` | **PASS** (BSP+lit) | same (`freopen` rebinds stdout cookie) |
| `user-printf.c` | **PASS** (BSP+lit) | same |

### F. Flat list — every lit-enabled Haydn non-PASS (reduced)

```
# CB-134 hang (HAYDN_COMPILE_HANG_SKIP → UNSUPPORTED) — only OPEN compiler residual
20001111-1.c
20170401-1.c
20180921-1.c
950809-1.c
960312-1.c
# budget (TIMEOUT default; PASS with higher budget)
920501-6.c
# not compiler (target / freestanding / harness) — SKIP
built-in-setjmp.c
pr84521.c
pr84748.c   # HAYDN_TARGET_SKIP — no __int128 this phase
loop-2f.c
loop-2g.c
920302-1.c
20030125-1.c
20020720-1.c
# FIXED via BundleSim BSP + lit VFS + baremetal fscanf ungetc:
#   alloca: 20020314-1, 20021113-1, 20040223-1, 941202-1, pr22061-1
#   stdio:  fprintf-2, printf-2, user-printf
# FIXED earlier this wave: conversion, 930622-2, pr49218, complex-5,
#   920501-8, 930513-1, struct-ret-1, va-arg-22, pr57344-3, pr78622, pr60960
```

### Repro (lit gate)

```bash
export PATH="$HAYDN_BIN:$PATH"   # Haydn clang on PATH
export LLVM_TESTSUITE=…/llvm-testsuite   # or TORTURE_SRC=…/execute
cd …/BundleSim
scripts/run_gcc_torture_lit.sh -O3 -j32
# focused:
scripts/run_gcc_torture_lit.sh --filter '920501-8|930513-1|conversion' -j8 -a
```
>
> ### Recently closed (hunt wave — not OPEN)
>
> | ID | Pri | Verdict | BundleSim gate | Compiler? | Harness? |
> |----|-----|---------|----------------|-----------|----------|
> | **CB-124** | P2 | **FIXED** — seed2@O1–O2; branch-relax scavenger `AllowSpill=false` | `yarpgen_seed2` / `yarpgen_seed2_O2` **-O0/-O2 prevent-reg** | **Yes** | No |
> | **CB-125** | P2 | **FIXED** — seed7@O2; same root as CB-124 branch-relax scavenger | `yarpgen_seed7` **-O2 prevent-reg** (WILL_FAIL removed) | **Yes** | No (ILP32 golden) |
>
> ### Archive reclass (2026-07-17) — `work/yarpgen_fail_archive_20260716`
>
> Unique seeds re-run with product BSP + `run_c --case` (27 packages):
>
> | Class | Seeds | Action |
> |-------|-------|--------|
> | **PASS** (F1/F2 fixed) | 1,5,8,10,12,14,24,39,45,48 (+ more) | Prevent-regression cases: `yarpgen_seed{8,10,12,14,24,45}` (+ existing 1/2/5) |
> | **FIXED (CB-124)** | **2@O1–O2** | branch-relax `AllowSpill=false`; `yarpgen_seed2`/`_O2` prevent-reg |
> | **FIXED (CB-125)** | **7@O2** | same root as CB-124; `yarpgen_seed7` prevent-reg |
> | **Stale widen golden** | seed2 archive `0xeabc…` | Ignore — host narrow is `0xfc46…` |
>
> A/B (same sources, ILP32 host golden):
>
> | Seed | -O0 | -O1 | -O2 | Host (narrow) | Guest@O2 |
> |------|-----|-----|-----|---------------|----------|
> | 2 | PASS | **PASS** | **PASS** | `0xfc460d2be25adb0b` | match after CB-124 AllowSpill |
> | 7 | PASS | PASS | **PASS** (was FAIL `0x60ee…`) | `0x1ab4548cf797a06f` | match after CB-124/125 AllowSpill |
>
> **CB-124 FIXED (2026-07-17):** seed2@O1–O2 was wrong `oracle_u64` (guest
> often `0xeb65…` / `0xe5f1…` vs host `0xfc46…`). Diagnostic class looked like
> RA/greedy (`-optimize-regalloc=false` / `-regalloc=basic` PASS; misched still
> FAIL), but root was **branch-relax trampoline scavenger** — see detailed
> section. Fix: `AllowSpill=false` in `insertIndirectBranch`. Gates:
> `yarpgen_seed2` + `yarpgen_seed2_O2` prevent-reg. Evidence:
> `BundleSim/work/cb124_parallel_20260717/`.
>
> **CB-125 FIXED (2026-07-17):** seed7@O2 was guest `0x60ee…` vs host
> `0x1ab4…` (CB-121 residual). **Same root as CB-124**:
> `HaydnInstrInfo::insertIndirectBranch` scavenger `AllowSpill=true` spilled a
> live-out and reloaded after `JALR_W` (dead). With `AllowSpill=false` (RISC-V
> peer), seed7@O2 matches host. Gate: `yarpgen_seed7` prevent-reg (no WILL_FAIL).
> Evidence: `BundleSim/work/cb125_parallel_20260717/`.
>
> **CoreMark CRC FIXED (2026-07-17):** first-bad **list_crc** via
> `matrix_mul_const`. Root was Stage-0 invent **ZOL exit→preheader hoist**
> (not in AIE). Interim guard rejected preheader IV uses; **later removed the
> invent entirely** (fallthrough pack only). Evidence:
> `BundleSim/work/coremark_parallel_20260717/FINDINGS.md`.
>
> **CB-123 CLOSED / CB-122 DROPPED:** unchanged (seed1@O2 green; seed6 not re-opened).
>
> Policy: AIE / Hexagon / RISC-V — no reserved MatInt AT.
>
> ### Closed / fixed (summary)
>
> | ID | Verdict | BundleSim? | Compiler? | Harness? |
> |----|---------|------------|-----------|----------|
> | **CoreMark CRC** | **FIXED** — ZOL exit→preheader invent removed (AIE does not do it); CRCs match i386 ILP32 | campaign | **Yes** | No |
> | **CB-123** | **CLOSED** — seed1@O2 PASS host golden | Yes (gate) | cleared on HEAD | No |
> | **CB-125** | **FIXED** — seed7@O2; same root as CB-124 branch-relax `AllowSpill=false` | Yes (prevent-reg) | **Yes** | No |
> | **CB-121** | **FIXED (O0/O1)** — seed7 ILP32 host + call hygiene; O2 residual closed as CB-125 | Yes | **Yes** | **Yes** |
> | **CB-119** | **FIXED** — Bundle128 `simm20` signed decode; F1 ILL@entry cleared | Yes | **Yes (MC)** | No |
> | **CB-120** | **FIXED** — unaligned/bitfield ISel; F2 seeds PASS | No | **Yes** | No |
> | **CB-113** | **CLOSED (harness ABI)** — LP64 host `long=8` vs Haydn ILP32 `long=4`; host oracle now `narrow_long` (ILP32-faithful) | No | No (ABI correct) | **Yes** |
> | **CB-115** | **FIXED (systematic)** — dead-end MBB → RET | No | Yes (landed) | No |
> | **CB-111 dual residual** | **CLOSED (obsoleted)** — dual `.s`≠`.elf` path gone | N/A | N/A | N/A |
> | **CB-117** | **FIXED** — GenMux (D490) + PEI/BSP (D492 series); full seed3148 host≡guest low8 **137** after clean BSP | No | Yes (landed) | stale-ELF only |
> | **CB-118** | **DROPPED (invalid test)** — yarpgen seed 2967 residual host≠sim is UB / not a Haydn gate; plumbing FIXED (D492/D494) | N/A | N/A (gate closed) | use `-m 32` + defined tests |
>
> **CB-108…112, 114 residual, 116 FIXED** earlier. Minimal ABI probe:
> `BundleSim/benchmarks/diff_sweep/abi_long/long_hi.c`.
>
> **Hunt evidence (2026-07-15):** `bs_yarpgen --from 6 --to 200` → 96 seeds,
> 25 FAIL (~26%). Family cluster collapses **25 seed FAILs → 3 unique roots**
> (this file: CB-119/120/121). Details:
> `BundleSim/work/yarpgen_hunt10/cluster/FAMILIES.md`.
>
> **This file holds currently OPEN bugs** (plus closed-with-evidence notes
> that reclassify bulk false-positives).
>
> **Repro convention (greenfield):**
>
> ```bash
> export PATH=/ssd2/mhyang/haydn-build/bin:$PATH
> export BUNDLESIM_HAYDN_TOOLCHAIN_BIN=/ssd2/mhyang/haydn-build/bin
> cd /ssd2/mhyang/BundleSim
> # single/multi .c:
> build/run_c a.c b.c -O2 --host
> # preferred yarpgen: linked pure-C CLI (archives FAIL only)
> build/bs_yarpgen --from 6 --verify --work work/yarpgen_sweep
> build/run_c --case bundlesim/tests/regression/cases/yarpgen_YYYYMMDD_HHMMSS_seedN/case.json
> ```
>
> `--host` compares exit **mod 256**; refuses plain `long`/`UL` unless
> `--force-host` (CB-113). Yarpgen campaign packages use host `oracle_u64`
> after widen_long (CB-113). Generator policy: always `-m 32` C11 integer-only.
## Fixed / reclassified history

- **CB-115 FIXED 2026-07-13 (systematic)** — empty / `unreachable` left
  succ-empty MBBs with **no terminator**; PEI only restores return blocks so
  mid-function dead-ends fell through past `.Lfunc_end`. Primary fix:
  **`HaydnEnsureTerminators`** (post-PEI `addPreSched2`): every non-EH
  succ-empty MBB without a terminator gets soft `RET` (`jalr_w r0, lr, 0`).
  Belts: FrameLowering empty-entry RET; ISel `G_TRAP*` → RET. Lit:
  `cb115-unreachable-terminator.ll` (whole-function + mid-function + O0/O2).
  Greenfield: seed 2927 `tf_4_init+tf_4_foo` MATCH host=0 sim=0.
  Lesson: `BundleSim/lessons/42-cb115-systematic-dead-end-terminators-2026-07-13.md`.
- **CB-111 dual residual CLOSED 2026-07-13 (obsoleted by greenfield)** —
  residual was `.s`≠`.elf` on large TUs (2967/3148 three-way disagree under
  legacy `link.sh -o` vs `-E`). Greenfield evaluates **ELF only**; dual-flow
  and legacy `.s` PC-stride model are archived. D493 LD-slot single-auth
  (seed 2256) remains FIXED. Seed3148/2967 plumbing FIXED (D490–D494);
  residual yarpgen host≠sim is **not** a dual-flow or open-compiler gate.
- **CB-113 RECLASSIFIED 2026-07-13 (not compiler, not sim)** — bulk host≠sim
  with same guest on both legacy flows is **LP64 host vs ILP32 Haydn
  `unsigned long`**. Minimal: `benchmarks/diff_sweep/abi_long/long_hi.c` —
  host high-byte of `0xE8C2…UL` ≠ 0; Haydn truncates. Greenfield:
  `build/run_c …/long_hi.c --force-host` → MISMATCH host=162 sim=1 (warns
  LP64). **Harness fix:** ILP32 host oracle, `yarpgen -m 32`, or widen to
  `unsigned long long` (`widen_long.py` / campaign prepare).
- **CB-116 FIXED 2026-07-13** — BranchRelaxation safety buffer default
  **256 → 1024** (`-haydn-branch-relax-safety-buffer`). Seed 3434 compiles.
- **CB-114 residual FIXED 2026-07-13** — GISel `G_TRUNC` non-pow2 + ZEXT/SEXT
  generalize. Lit: `cb114-trunc-nonpow2.ll`. Seed 2896 compiles.
- **CB-111 FIXED 2026-07-13 (LD-slot single-auth, D493)** — logical
  `LD32`/`LD64` only; slot in AltDescs/FlexMap. Lit:
  `cb111-ld-slot-single-auth.ll`.
- **CB-114 first-pass FIXED 2026-07-13** — trunc-to-s1 lattice. Lit:
  `cb114-trunc-to-s1.ll`.
- **CB-112 FIXED 2026-07-13** — lld WIDE_Call / WIDE branch thunk factory.
  Lit: `lld/test/ELF/haydn/thunk-wide-call-simm20.s`. Huge-function
  thunk-reach / map overflow are harness or size-limit edges, not this bug.
- **CB-108 / CB-109 / CB-110 FIXED 2026-07-13** — analyzeBranch mid-block
  call; CFGOptimizer fallthrough; G_ICMP non-pow2.
- **CB-104…107 / CB-2 / CB-22/50 residual FIXED 2026-07-13** →
  `BundleSim/lessons/38-fixed-cb104-106-2-22-50-wave-2026-07-13.md`
- Wave 2026-07-12 + CB-96…103: lessons 34 / 39.
- Historical FIXED: `BundleSim/lessons/19-fixed-historical-compiler-bugs-archive.md`

| ID | Pri | State | Notes |
|----|-----|-------|-------|
| **CB-132** | **P0** | **FIXED** | Mid-ISel CFG / pure-stack crash path removed. Thin ISel → `VAARG_I32/I64`; ExpandPseudos two-bank+overflow. Cluster no longer SIGSEGV. Repro was `va-arg-2.c` → now **PASS**. |
| **CB-126** | **P1** | **FIXED (core)** | C gate PASS; residual `G_ZEXTLOAD`/`G_STORE` width ICE on `pr57344*`, `pr52979*`, … |
| **CB-127** | **P2** | **FIXED** | C gate PASS all `builtin-prefetch-*`. |
| **CB-128** | **P2** | **FIXED** | C gate PASS returnaddress tests. |
| **CB-129** | **P2** | **FIXED (core)** | `comp-goto-1` PASS; residual MBP assert `20000815-1`. |
| **CB-130** | **P2** | **PARTIAL** | lit may pass; `simd-*`/`pr60960` still legalize ICE; `pr53645` → MEMORY_FAULT. |
| **CB-131** | **P1** | **FIXED (core)** | Systematic ExpandPseudos VAARG + Dst≠VaList + non-variadic consumers + no ZOL on va_arg + SP-bracket multi-spill. Cluster **PASS=42**/47; residual: `struct-ret-1`, `va-arg-22`. |
| **CB-119** | **P0** | **FIXED** | Bundle128 `simm20` signed DecoderMethod; lit `cb119-addi32-simm20-signed-decode.s`. F1 ILL@entry cleared. Prevent-reg: `yarpgen_seed{10,45,…}`. |
| **CB-120** | **P0** | **FIXED** | Misaligned load root: unaligned/bitfield ISel; Family F2. Prevent-reg: `yarpgen_seed{8,12,14,24,…}`. |
| **CB-121** | **P1** | **FIXED (O0/O1)** | Seed7 ILP32 host + call hygiene; O0/O1 match `0x1ab4…`. **O2 residual → CB-125 FIXED**. |
| **CB-124** | **P2** | **FIXED** | seed2@O1–O2; branch-relax `AllowSpill=false`; gates `yarpgen_seed2`/`_O2` prevent-reg. |
| **CB-125** | **P2** | **FIXED** | seed7@O2; **same root as CB-124** AllowSpill=false; gate `yarpgen_seed7` @ **-O2** prevent-reg. |
| **CoreMark CRC** | **P2** | **FIXED** | list_crc first; `matrix_mul_const` via ZOL exit→preheader IV hoist; see CoreMark section. |
| **CB-122** | — | **DROPPED (gate removed)** | seed6 unreducible / not debug-suitable; packages deleted; no regression case. |
| **CB-123** | — | **CLOSED** | seed1@O2 PASS `0x7b0a…` on HEAD; gate `yarpgen_seed1` @ **-O2**. Sticky `0xb863…` cannot-repro. |
| **CB-117** | — | **FIXED** | seed **3148**: D490+D492 series. Clean BSP: host≡guest low8 **137**. Decision: `docs/haydn/DECISION-D492-pei-scratch-no-livein-clobber.md`. |
| **CB-118** | — | **DROPPED (invalid test)** | seed **2967**: PEI/AT plumbing FIXED (D492/D494). Residual host≠sim after no-long strip is yarpgen UB (e.g. `tf_2_var_293` shift/`&&` mess) — **not a valid Haydn regression gate**. Decision: `docs/haydn/DECISION-D494-at-scratch-value-construct.md`. |
| **CB-113** | — | **CLOSED (harness ABI)** | LP64 host `long=8` vs ILP32 Haydn `long=4`. `run_c --force-host` on `long_hi.c`. |
| **CB-115** | — | **FIXED (systematic)** | `HaydnEnsureTerminators` + PEI/ISel belts. Lit: `cb115-unreachable-terminator.ll`. |
| **CB-111 dual** | — | **CLOSED (obsoleted)** | No `.s` gate; D493 slot-auth still FIXED. |

---

# Currently OPEN

## CB-167 — OPEN 2026-08-27: S2 convergence repack flips a legal same-cycle WAR into a wrong sequential order

**Class:** miscompile under `-haydn-sms2` (late-convergence driver).
**Toolchain:** w68-new @ `ad8bbc4ac8c9` (entry-qualified relocs + census fix landed).

**Reproducer:** gcc torture `pr85529-1.c` at -O2 (`run_gcc_torture_lit.sh -O2
--filter ^pr85529-1.c$` with `BUNDLESIM_TORTURE_EXTRA_CFLAGS="-mllvm -haydn-sms2"`):
OFF arm exits 0 (286 bundles); ON arm ABORT (165 bundles).

**Root cause (traced):** the short-circuit chain `s.a != (k < foo(k,2) && (c=k=g))`
lowers to `srai32 r9,r9,24` (foo result) → `slt32 r8,r8,r9` (use) → `ld32 r9,[r6]`
(volatile s.a read, WAR on r9). The DEFAULT scheduler packs
`{ slt32 r8,r8,r9 ; ld32 r9,[r6] }` in ONE parcel — same-cycle WAR, legal under
Haydn snapshot (read-old) semantics, executes correctly. The S2 invocation inside
HaydnLateConvergence SEQUENTIALIZES the pair with the LOAD FIRST (bundle trace:
ld r9 @pc+2 before slt @pc+3), so `slt` reads the LOADED value and the
short-circuit takes the wrong path → `c=k=g` executes → `c!=1` → abort.
The WAR anti-dependence between the same-cycle pair is lost/flipped during the
convergence repack (reopen → reschedule → recommit); the
`asIsGeneratedMembersFormLegalCycle` "Rematch can flip a legal WAR" note and the
archive F-series warnings point at the same rematch seam.

**Impact:** sole failure of the torture -O2 sms2 arm (1478/1479); full 659-suite
sms2 + all-flags arms green; CM -3.8% / DH -0.9% bundles. Blocks the G004
default-ON flip.

**Fix direction:** the S2/recommit path must preserve WAR order between a
member pair that was same-cycle-legal in S1: either keep such pairs coissued
(prefer the S1 cycle over re-formation) or re-derive the anti-dep from the
snapshot semantics (read-old) before emitting the sequential form. Owner:
HaydnPostRASchedStrategy / HaydnBundleMaterialize rematch.

**Precise DAG evidence (machine-scheduler dump, S1 region):**
`SU(8): dead $r9 = S_LW_WITH_IMM $r6, 0 (volatile s.a)` carries
`Successors: SU(9): Data Latency=2 Reg=$r9` into
`SU(9): $r8 = SLT32 killed $r8, killed $r9`.
A *Data* (RAW) edge from a `dead`-dest def into a use that also carries
`killed` is a mis-built dependence: SLT's reaching r9 def is the preceding
SRAI32 chain, and the LD→SLT relation is WAR (edge should be SU(9)→SU(8),
latency 0). Same-cycle packing in S1 masks the wrong edge (snapshot
read-old); the S2 latency-honoring schedule exposes it as a 2-cycle
LD-before-SLT serialization that reads the loaded value. Fix belongs in
the post-RA dependence construction for dead-def redefinitions of a
register whose prior def's last use carries the same register (the
dead/killed marker pair on the volatile-load materialization).


**Refined owner (bundle-interior dependence):** the S1 MIR commits
`BUNDLE { dead LD r9 ; SLT32 killed r8, killed r9 }` (legal: dead def, no
true intra-cycle RAW). When the scheduling DAG is (re)built over this
code — S1 bundle flattening or the S2 reschedule before repack — the
members are walked in order and the LD def of r9 meets SLT's use of r9 as
a *Data Latency=2* edge (SU(8) -> SU(9) in the dump), encoding a false
RAW that the snapshot hardware does not have. Any latency-honoring
schedule then serializes LD two cycles before SLT, which reads the loaded
value (the executed miscompile). Fix owner: the post-RA dependence
construction for bundle members must treat a DEAD-def member redefining a
register whose same-bundle sibling reads the PRIOR def as no edge (or an
Anti), matching HaydnIntraCycleRAW's dead-def skip; sites:
ScheduleDAGInstrs bundle walk / HaydnPostRASchedStrategy region build.
## CB-166 — PARTIAL FIX 2026-08-27 (033372428db8): ZOL runtime-trip SMS + grounded PHI chains; strict {LD+2MAC} co-issue tier remains OPEN

> **RETEST-2 2026-08-27 on `e4c7077e7e2c` (installed 17:10 build incl. 033372428db8) —
> PARTIAL CONFIRMED.** Three-tier harness (BundleSim `9461984`,
> `benchmarks/naturedsp_kernels/tools/cb166_retest.sh`; exit 0=full / 3=partial / 1=open):
> T1 straight-line repro `{MAC+LD}=0` (SMS is loop-only — by design). **T2 kernel:
> `{MAC+LD}>0 — loads now ride MAC bundles.** bkfir32x32 -O3 core density
> **1.00 → 1.60 MAC/bundle** (5-bundle SMS-pipelined body, `{ff2mula32r.ll d4; ld64 d9}`
> co-issue; fn bundles 1,001,160 → 954,000 on the baseline shape). Strict vendor
> `{LD + 2MAC}` triple: still 0 in every shape. Consequence: at -O3 the SMS-fixed
> **baseline shape now beats the ldw.strand port** (1,023,702 vs 1,067,879 — the port's
> x2sel32 swizzle/rotate chain blocks the tight 5-bundle pipeline), while at -O2 (SMS
> not engaged) the port still wins 1,063,918 vs 3,741,259. All gates re-pass on the new
> build (bkfir32x32-ab incl bench-shape, fir_gate_blk 19/75, dualtest-asc, ctest -L
> regression 190/190). Attribution: `-mllvm -haydn-sms2=false` changes totals <0.1%
> and the {MAC+LD} bundle persists — the enabling path is **033372428db8's generic
> ZOL-accept SMS**, not the G004 sms2 default.
> **RETEST 2026-08-27 — STILL OPEN on `554957730440`** (w68-new HEAD, post
> CB-167 fix + entry-qualified-reloc wave). Harness:
> `BundleSim/benchmarks/naturedsp_kernels/tools/cb166_retest.sh` (two tiers,
> exit 0 on any {2MAC+LD} bundle). Results: T1 minimal repro
> `{2MAC}=0 {2MAC+LD}=0` (identical to filing); T2 bkfir32x32 -O3 kernel body
> `{2MAC}=4 {2MAC+LD}=0` — the scheduler DOES pack 2 MACs into one bundle in
> the real hot loop, but NEVER rides a load into one. The CB-167 lineage
> (bundle-interior dead-def false RAW edge) was the right neighborhood and
> did not close this. Side observation: the new build regresses cold-path
> density on this kernel (prologue/drain nop-padding +266k bundles, -O3
> total 1,009,368 -> 1,275,519 while the hot core shape stays byte-identical);
> the ldw.strand PORT WIN still holds against the same-build baseline
> (1,338,267 -> 1,275,519, -4.7% -O3; -75.7% -O2).


**UPDATE 2026-08-27 — PARTIAL FIX LANDED (`033372428db8`, llvm-head):**
runtime-trip ZOL loops now reach the generic modulo scheduler with a
dynamic guard (J2_loop0r-style SLT32+BNEZ_W on the exact register
SET_HWLOOP_F2_W consumes), and grounded sliding-window PHI chains (the
FIR tap-delay shape) are no longer blanket-rejected. Measured on the
shared artifact: bkfir32x32 -O3 fn-attributed bundles 1,206,600 ->
998,960 (**-17.2%**, beats every recorded baseline; total 1,275,519 ->
1,067,879), sink bit-exact, {2MAC} parcels 4 -> 6, FIR A/B 9/9 bit-exact,
lit 1076/0F, units 599/599, BundleSim regression 218/218.
**Still open (this row stays tracked):** the strict tier — a load riding
INTO a dual-MAC parcel ({LD + MAC + MAC}) — is still 0 everywhere. Root
cause chain: (a) no-forwarding law forces load-fed MACs into separate
cycles; closing the last gap needs post-RA packer gap-filling, not SMS
acceptance; (b) the T1 straight-line probe additionally has a generic
register-coalescer false dep (not Haydn-owned). Retest harness verdict
line still reports STILL OPEN until the strict tier lands.

**Class:** perf / scheduling (NOT a miscompile). **Toolchain:** clang built from
`ead16580abae3aa52c2b19102dd63fa102db4121`; checked-out llvm-head `22cebc257e1b` (w68-new).

**Symptom:** FIR-class 32x32 kernels (4 independent Q31 accumulators, one
DR64 pair per 2 taps, `ff2mula32r_ll`/`_hh` per lane) run at **avg 1.0
MAC/bundle, peak 2.0** — an 8-bundle hot loop per 4-output group — instead
of the vendor HiFi3z **2.0 MAC/cycle sustained**: `{LD + LL_S2 + HH}`
(one load + two MACs per VLIW bundle). The vendor body is exactly:

```
cy:  { AE_LA32X2.IC;  MULAF32R.LL_S2 q;  MULAF32R.HH    }   # 2 MAC + 1 load
     { AE_L32X2.XC;   MULAF32R.LL_S2 q;  MULAF32R.HH    }   # every cycle
```

On Haydn, the same algorithm emits at best `{ ff2mula32r.ll ; ff2mula32r.ll }`
(two MACs, no load) in the densest bundles, with the loads (`d_ldw_post_imm`)
in separate bundles — 2 loads + 2 lane-swizzles + 2 register-rotates consume
the non-MAC bundles, so the 4-MAC-per-tap-pair loop takes 8 bundles (avg 1.0).

**Reproducer (3-line, self-contained):**

```bash
cat >/tmp/cb166.c <<'C'
#include <haydn.h>
#include <stdint.h>
static volatile int64_t sinkv;
__attribute__((noinline)) int64_t f(const int64_t *restrict w, int64_t a)
{
  haydn_cb_ld_t l0 = haydn_d_ldw_post_imm((const void *)w, 1);
  haydn_cb_ld_t l1 = haydn_d_ldw_post_imm(l0.new_ptr, 1);
  int64_t x0 = l0.data, x1 = l1.data;
  int64_t y0 = 0, y1 = 0;            /* independent accumulator targets */
  y0 = (int64_t)haydn_ff2mula32r_ll(y0, (haydn_dr64_t)x0, (haydn_dr64_t)a);
  y1 = (int64_t)haydn_ff2mula32r_ll(y1, (haydn_dr64_t)x1, (haydn_dr64_t)a);
  sinkv = y0 + y1;
  return y0 + y1;
}
C
clang --target=haydn-unknown-elf -O3 -mcpu=haydn -S -o /tmp/cb166.s /tmp/cb166.c \
  -I $HAYDN_BIN/../lib/clang/22/include
grep -cE '\{\s*[^}]*ff2mula32r[^}]*ff2mula32r[^}]*\}' /tmp/cb166.s    # = 0 (2-MAC alone never packs)
grep -cE '\{\s*[^}]*ff2mula32r[^}]*ff2mula32r[^}]*d_ldw[^}]*\}' /tmp/cb166.s  # = 0 (2-MAC+load never)
```

Result on the above toolchain: **both greps = 0** — the backend never packs
even two independent-accumulator `ff2mula32r` into one bundle, and never
co-issues `d_ldw_post_imm` with a MAC. Same negative across shapes: same-op
(ll,ll) and diff-op (ll,hh), 2/4 data pairs, the AE-structured inner loop
(`bkfir32x32_haydn_ae.c`) recompiled under these exact flags, and unroll
variants — all retain a separate-bundle body. (An earlier report showing
`{ff2mula32r.ll d3; ff2mula32r.ll d6; d_ldw_post_imm d9}` in the AE kernel
was from a DIFFERENT compile than the run_c `-O3 -mcpu=haydn` path.)

**Impact (quantified, BundleSim committed-bundle proxy):**
- `bkfir32x32` bench (-O3, N=256 M=64 x40 blocks): hot loop = 8 bundles /
  4 outputs (655,360 committed core bundles), `ourPeak 2.00` `ourAvg 0.65`
  over the loop body. Baseline 1,080,288 total bundles; ldw.strand
  sliding-window port (BundleSim `548558e`, bit-exact) lowers total to
  1,009,368 (−6.6%, all cold/prologue/drain) — the **hot-loop core is
  unchanged** because the load/dual-MAC co-issue is the blocker.
- Vendor report (`kernel_reports/ndsp/fir/bkfir32x32/report.md`): 8 cy / 16 MACs = **2.0**,
  22 real ops / 8 cy = 91.7% util, `loopnez`, `{LD + LL_S2 + HH}` every cycle.

**Ask (scheduler / bundling):** allow the post-RA (or wave) scheduler to form
`{ D_LDW_POST_IMM + ff2mula32r + ff2mula32r }` 3-op bundles for a load +
two independent-accumulator MACs — the HiFi3z `ae_format0` slot shape
(slot0 LD / slot1 MAC_S2 / slot2 primary MAC). This is the difference
between 1.0 and 2.0 MAC/cy for the entire FIR/dot 32x32 family.

## Retest log (2026-07-22)

```bash
# rebuild required after ba10662 (stale libLLVMHaydnCodeGen hid all fixes)
cd /ssd2/mhyang/haydn-build && ninja -j$(nproc) lib/libLLVMHaydnCodeGen.so.22.1 bin/llc bin/clang
export LD_LIBRARY_PATH=/ssd2/mhyang/haydn-build/lib
# full suite (runner):
TORTURE_WORK=/tmp/bundlesim-$UID/gcc-c-torture/run3 \
  python3 /tmp/bundlesim-672/gcc-c-torture/run_torture.py --opt=-O2 -j16 --timeout 20
# summary: …/run3/summary-O2.json
```

| Metric | pre-rebuild | post-rebuild |
|--------|-------------|--------------|
| PASS | 1312 | **1298** |
| COMPILE_BACKEND_ICE | 47 | **72** (see inventory below) |
| SIM_ABORT | 31 | **19** |
| FIXED→PASS | — | **20** (19 ICE + `va-arg-1`) |
| REGRESSED PASS→ICE | — | **34** (all **CB-132** SIGSEGV) |

### Why ICE went 47 → 72 (+25 net)

```text
  47  baseline ICE
− 19  ICE fixed → PASS (CB-126/127/128/129 cores)
+ 34  was PASS → SIGSEGV (CB-132 regression)
+ 12  was ABORT → SIGSEGV (same CB-132; no longer miscompile, now crash)
+  1  was MEMORY_FAULT → SIGSEGV (multi-ix)
+  0  new pure legalize ICE (none — residual legalize are STILL_ICE)
────
  72  post-rebuild ICE
```

Math: `47 − 19 + 34 + 12 + 1 − (va-arg-22 was STILL_ICE and still crash) = 72`.

### ICE inventory post-rebuild (**72** total) — by technical class

| Class | N | CB | Origin | What |
|-------|---|-----|--------|------|
| **A. SIGSEGV exit 139** | **48** | **CB-132** | 34 PASS + 12 ABORT + 1 MEMFAULT + 1 old ICE | clang frontend crash |
| **B. LEGALIZE_VECTOR** | **8** | CB-130 residual | all still-ICE | vector PHI/mul/FADD/LSHR/SDIV/BITCAST |
| **C. LEGALIZE_LOAD** | **7** | CB-126 residual | all still-ICE | `G_ZEXTLOAD` width mismatch |
| **D. TRANSLATE_CALL** | **4** | (new bucket) | all still-ICE | `unable to translate instruction: call` |
| **E. LEGALIZE_BITCAST_MISC** | **3** | (misc) | all still-ICE | `G_FPTOUI` / `G_PTRTOINT` / `G_FSHL` |
| **F. ASSERT_MBP** | **1** | CB-129 residual | still-ICE | `MachineBlockPlacement` (`20000815-1`) |
| **G. LEGALIZE_STORE** | **1** | CB-126 residual | still-ICE | `G_STORE` s32→s64 mem (`pr79737-2`) |
| **Total** | **72** | | | |

#### A. SIGSEGV_139 — **48** (CB-132) — *this is the +ICE*

**Regressed was-PASS (34):**  
`20000519-1`, `20041113-1`, `20041214-1`, `20071213-1`, `920625-1`, `920726-1`, `920908-1`, `931004-{2,4,6,8,10,12,14}`, `980205`, `980716-1`, `pr38151`, `pr44575`, `pr56205`, `stdarg-{1,2,4}`, `strct-stdarg-1`, `strct-varg-1`, `va-arg-{4,5,6,11,13,14,18,20,26,trap-1}`

**Was-ABORT → crash (12):**  
`920501-8`, `991216-2`, `pr64979`, `stdarg-3`, `va-arg-{2,9,10,15,16,17,19,24}`

**Other origin (2):** `multi-ix` (was MEMFAULT), `va-arg-22` (was legalize ICE)

```bash
# repro
$HAYDN/clang --target=haydn-unknown-elf -O2 -c $TORTURE/va-arg-2.c -o /tmp/t.o \
  -w -Wno-implicit-int -Wno-implicit-function-declaration
# exit 139
```

#### B. LEGALIZE_VECTOR — **8** (CB-130 residual)

`20050316-1` (G_BITCAST v2i16), `20050604-1` (G_FADD v4i32), `20060420-1` (G_PHI v4i32),  
`pr23135` (G_SDIV v2i32), `pr60960` (G_LSHR v4i8), `simd-1` (G_PHI v4i32),  
`simd-2` (G_PHI v8i16), `simd-6` (G_MUL v8i8)

#### C. LEGALIZE_LOAD — **7** (CB-126 residual)

`pr52979-1`, `pr52979-2`, `pr57344-1`, `pr57344-2`, `pr57344-3`, `pr57344-4`, `pr58570`  
→ `unable to legalize … G_ZEXTLOAD`

#### D. TRANSLATE_CALL — **4**

`pr65053-2`, `pr65956`, `pr88904`, `stkalign`  
→ `unable to translate instruction: call`

#### E. LEGALIZE_BITCAST_MISC — **3**

`980605-1` (G_FPTOUI), `pr17252` (G_PTRTOINT), `pr56866` (G_FSHL)

#### F. ASSERT_MBP — **1** (CB-129 residual)

`20000815-1` — `MachineBlockPlacement::buildCFGChains` assert

#### G. LEGALIZE_STORE — **1** (CB-126 residual)

`pr79737-2` — `G_STORE` s32 value to s64 memory

### ICE priority to fix

| Order | Class | N | Why |
|-------|-------|---|-----|
| 1 | **A SIGSEGV** | 48 | P0 regression; restore PASS + clear false ICE inflation |
| 2 | **C+G load/store legalize** | 8 | finish CB-126 |
| 3 | **B vector** | 8 | finish CB-130 |
| 4 | **D translate call** | 4 | independent backend hole |
| 5 | **E misc legalize** | 3 | small |
| 6 | **F MBP** | 1 | CB-129 leftover |

---

## CB-132 — FIXED (clang SIGSEGV on va_arg/stdarg after ba10662)

**Pri was P0. Fixed 2026-07-22 with CB-131 systematic ExpandPseudos VAARG.**

| Field | Value |
|-------|--------|
| Symptom (was) | `clang frontend command failed with exit code 139` (SIGSEGV) |
| Root | Post-`ba10662` pure-stack / mid-pipeline VAARG paths could not host bank→stack overflow without CFG; crash or wrong pure-stack. |
| Fix | Thin ISel `G_VAARG` → `VAARG_I32/I64`; `HaydnExpandPseudos::expandVAARG` implements two-bank + stack overflow with MBB split; spill restores land on Join (never after terminators). |
| Gate | `va-arg-2.c` @ -O2 BundleSim **PASS**; cluster filter no compile SIGSEGV |

### Reproduce (now expect PASS)

```bash
export LD_LIBRARY_PATH=/ssd2/mhyang/haydn-build/lib
export TORTURE_WORK=/tmp/bundlesim-$UID/gcc-c-torture/vaarg-cb132-gate
python3 /tmp/bundlesim-672/gcc-c-torture/run_torture.py --opt=-O2 -j4 \
  --filter '^(va-arg-1|va-arg-2)\.c$'
# expect: PASS=2
```

---

Source sweep: llvm-testsuite
`SingleSource/Regression/C/gcc-c-torture/execute` @ `-O2`, freestanding
Haydn link + BundleSim (not full lit yet). Evidence dirs:
`/tmp/bundlesim-672/gcc-c-torture/run/` (baseline), `run3/` (post-rebuild).

**Env (all repros):**

```bash
export HAYDN=/ssd2/mhyang/haydn-build/bin
export PATH="$HAYDN:$PATH"
export TORTURE=/ssd/mhyang/llvm/llvm-testsuite/SingleSource/Regression/C/gcc-c-torture/execute
export BSP=/ssd2/mhyang/BundleSim/build/bsp-stage
export SYSROOT=/ssd2/mhyang/haydn-build/sysroot/haydn-unknown-elf
export BSIM=/ssd2/mhyang/BundleSim/build/BundleSim
# compile-only ICE (no libc needed unless noted):
#   $HAYDN/clang --target=haydn-unknown-elf -O2 -c $TORTURE/FILE.c -o /tmp/t.o \
#     -w -Wno-implicit-int -Wno-implicit-function-declaration
```

**Runtime helper (exit 0 = PASS; ABORT/nonzero = FAIL):**

```bash
haydn_torture_run() {  # usage: haydn_torture_run FILE.c [-O2]
  local src="$TORTURE/$1" opt="${2:--O2}" base=/tmp/haydn-torture-$$
  mkdir -p "$base"
  "$HAYDN/clang" --target=haydn-unknown-elf "$opt" -ffreestanding \
    -isystem "$BSP/include" -isystem "$SYSROOT/include" \
    -w -Wno-implicit-int -Wno-implicit-function-declaration -Wno-int-conversion \
    -c "$src" -o "$base/t.o" || return 2
  "$HAYDN/ld.lld" -m elf32haydn -T "$BSP/lib/bundlesim/bundlesim.ld" \
    --gc-sections --build-id=none \
    "$BSP/lib/bundlesim/crt0.o" "$base/t.o" --start-group \
    "$BSP/lib/bundlesim/libbundlesim_crt.a" \
    "$SYSROOT/lib/libc.a" "$SYSROOT/lib/libm.a" \
    "$BSP/lib/bundlesim/libbundlesim_plat.a" \
    "$BSP/lib/bundlesim/libbundlesim_sys.a" \
    "$BSP/lib/bundlesim/libclang_rt.builtins-haydn.a" \
    --end-group -o "$base/t.elf" || return 3
  "$BSIM" --objdump "$HAYDN/llvm-objdump" "$base/t.elf" \
    --result-json "$base/r.json" --stdio null
  python3 -c "import json;d=json.load(open('$base/r.json'));print(d.get('status'),d.get('stop_reason'),d.get('guest_exit_code'))"
}
```

---

## CB-126 — FIXED core / residual OPEN (narrow load legalize)

**Pri was P1. Core FIXED 2026-07-22 retest (rebuilt tree). Residual legalize remains.**

| Field | Value |
|-------|--------|
| Class | GlobalISel legalizer |
| Symptom | `error in backend: unable to legalize instruction: %N:_(s64) = G_LOAD … (load (s8\|s16) from …)` or `G_STORE` s64 → narrow mem |
| Opt | `-O2` (also seen at lower opts for some) |
| Cluster size | ~16 compile ICEs in torture @ -O2 |

**Examples:** `20040709-2.c`, `20040709-3.c`, `pr57344-1..4.c`, `strct-pack-1.c`,
`va-arg-22.c`, `pr29006.c`, `pr53688.c`, `pr58570.c`, `pr70903.c`,
`pr52979-1/2.c`, `pr79737-2.c`, `20051113-1.c`.

### Reproduce

```bash
# minimal — expect: unable to legalize … G_LOAD … (load (s8) …)  or (s16)
$HAYDN/clang --target=haydn-unknown-elf -O2 -c \
  $TORTURE/20040709-2.c -o /tmp/t.o \
  -w -Wno-implicit-int -Wno-implicit-function-declaration

# packed struct variant
$HAYDN/clang --target=haydn-unknown-elf -O2 -c \
  $TORTURE/strct-pack-1.c -o /tmp/t.o \
  -w -Wno-implicit-int -Wno-implicit-function-declaration
```

**Expected after core fix:** exit 0 on `20040709-2` / `strct-pack-1`. Residual
still ICE: `pr57344-*`, `pr52979-*`, `pr58570`, `pr79737-2`, …

---

## CB-127 — FIXED (G_PREFETCH cannot select)

**Pri P2. Compiler. Not BundleSim.**

| Field | Value |
|-------|--------|
| Class | GISel instruction select |
| Symptom | `cannot select: G_PREFETCH %…:gpr32(p0), …` |
| Tests | `builtin-prefetch-1.c` … `builtin-prefetch-6.c` (all 6) |

### Reproduce

```bash
$HAYDN/clang --target=haydn-unknown-elf -O2 -c \
  $TORTURE/builtin-prefetch-1.c -o /tmp/t.o \
  -w -Wno-implicit-int -Wno-implicit-function-declaration
# expect: cannot select: G_PREFETCH …
```

**Fix direction:** lower `__builtin_prefetch` / `G_PREFETCH` to nop (or Haydn
prefetch op if ISA has one); do not ICE.

---

## CB-128 — FIXED (`llvm.returnaddress` cannot select)

**Pri P2. Compiler. Not BundleSim.**

| Field | Value |
|-------|--------|
| Class | GISel ISel of `@llvm.returnaddress` |
| Symptom | `cannot select: %…:gpr32(p0) = G_INTRINSIC intrinsic(@llvm.returnaddress), N` |
| Tests | `20030323-1.c`, `20030811-1.c`, `pr17377.c` |

### Reproduce

```bash
$HAYDN/clang --target=haydn-unknown-elf -O2 -c \
  $TORTURE/20030323-1.c -o /tmp/t.o \
  -w -Wno-implicit-int -Wno-implicit-function-declaration
# expect: cannot select: … intrinsic(@llvm.returnaddress)
```

**Fix direction:** select `LR`/frame walk for depth 0; undef or 0 for depth>0
(document as unsupported) — must not ICE.

---

## CB-129 — FIXED core / residual (MachineBlockPlacement / computed goto)

**Pri P2. Compiler. Not BundleSim.**

| Field | Value |
|-------|--------|
| Class | `MachineBlockPlacement::buildCFGChains` assert |
| Symptom | `Assertion '(!TII->analyzeBranch(*PrevBB, …) \|\| …)' failed` @ `MachineBlockPlacement.cpp:2914` |
| Tests | `comp-goto-1.c`, `20000815-1.c`, `20071210-1.c` |

### Reproduce

```bash
# needs sysroot headers (stdlib.h)
$HAYDN/clang --target=haydn-unknown-elf -O2 -c \
  $TORTURE/comp-goto-1.c -o /tmp/t.o \
  -isystem $SYSROOT/include -isystem $BSP/include \
  -w -Wno-implicit-int -Wno-implicit-function-declaration
# expect: Assertion … MachineBlockPlacement::buildCFGChains
```

**Note:** `analyzeBranch` / terminator modeling for indirectbr / computed-goto
edges — likely incomplete Haydn branch analysis rather than generic LLVM bug.

---

## CB-130 — PARTIAL (vector legalize: G_LSHR / G_XOR / extract / …)

**Pri P2. Compiler. Not BundleSim.**

| Field | Value |
|-------|--------|
| Class | GISel legalizer for scalable/fixed vectors |
| Symptom | `unable to legalize instruction: %…_(<N x sM>) = G_LSHR\|G_XOR\|G_MUL\|G_EXTRACT_VECTOR_ELT …` |
| Tests | `simd-1.c`, `simd-2.c`, `simd-6.c`, `pr53645.c`, `pr53645-2.c`, `pr60960.c`, `pr65427.c`, `pr85169.c`, plus related FP/vector ICEs |

### Reproduce

```bash
$HAYDN/clang --target=haydn-unknown-elf -O2 -c \
  $TORTURE/pr53645.c -o /tmp/t.o -w
# expect: unable to legalize … G_LSHR … <4 x s32>

$HAYDN/clang --target=haydn-unknown-elf -O2 -c \
  $TORTURE/simd-1.c -o /tmp/t.o \
  -w -Wno-implicit-int -Wno-implicit-function-declaration
# expect: unable to legalize … G_XOR … <4 x s32>
```

**Fix direction:** scalarize unsupported vector ops in Haydn legalizer; or
reject vectors in FE if out of product scope (still should not backend-ICE).

---

## CB-131 — FIXED (core); residual ABI miscompiles

**Pri P1. Core fixed 2026-07-22 (systematic ExpandPseudos VAARG).** Residual
runtime ABORT/MEMFAULT on a few cases still open (struct-return / edge ABI).

| Field | Value |
|-------|--------|
| Class | Calling convention / varargs / aggregate return |
| Core fix | `VAARG_I32/I64` + ExpandPseudos: reg bank (`top+offs`, step 4/8) or stack overflow (`__stack` step 8) |
| Core gates | `va-arg-1.c`, `va-arg-2.c` @ -O2 → **PASS** (`GUEST_EXIT 0`) |
| Residual | **CLOSED 2026-07-24** — `struct-ret-1` + `va-arg-22` **PASS** @ -O3 freestanding lit |

### Reproduce core (expect PASS)

```bash
export LD_LIBRARY_PATH=/ssd2/mhyang/haydn-build/lib
python3 /tmp/bundlesim-672/gcc-c-torture/run_torture.py --opt=-O2 -j4 \
  --filter '^(va-arg-1|va-arg-2)\.c$'
# PASS=2
```

### Residual gate

```bash
python3 /tmp/bundlesim-672/gcc-c-torture/run_torture.py --opt=-O2 -j8 \
  --filter '^(struct-ret-1|stdarg-3|va-arg-22)\.c$'
# still SIM_ABORT until residual ABI closed
```

**Not in this ticket (reclass later):** `strlen-*`/`memchr` exit 1 may be
llvm-libc; soft-float `__float*` / `alloca` / hosted `tmpnam` are **link /
sysroot** holes, not sim; `BUNDLE_LIMIT` long loops need infinite-loop vs
limit recheck before filing.

---

## CB-124 — FIXED (yarpgen seed2 @ -O1/-O2; branch-relax scavenger)

**Pri was P2. Fixed 2026-07-17. Not harness (ILP32 host golden stable).**

| Field | Value |
|-------|--------|
| Host (narrow) | `0xfc460d2be25adb0b` |
| Guest -O0 | match (PASS) |
| Guest -O1 / -O2 (pre-fix) | wrong (e.g. `0x193d…` / `0xeb65…` / `0xe5f1…`) |
| Guest -O1 / -O2 (post-fix) | match host (PASS) |
| Gate | `yarpgen_seed2` + `yarpgen_seed2_O2` prevent-reg |

```bash
build/run_c --case bundlesim/tests/regression/cases/yarpgen_seed2_O2/case.json
# PASS oracle=0xfc460d2be25adb0b
```

## CB-125 — FIXED (yarpgen seed7 @ -O2; same root as CB-124)

**Pri was P2. Fixed 2026-07-17. Same class as CB-124 (branch-relax scavenger).**

| Field | Value |
|-------|--------|
| Host (narrow) | `0x1ab4548cf797a06f` |
| Guest -O0 / -O1 | match (always, after CB-121) |
| Guest -O2 (pre-fix) | `0x60eea7826bef4382` (FAIL) |
| Guest -O2 (post-fix) | match host (PASS) |
| Gate | `bundlesim/tests/regression/cases/yarpgen_seed7` (`-O2` prevent-reg) |

### Root (shared with CB-124)

`HaydnInstrInfo::insertIndirectBranch` used
`RegScavenger::scavengeRegisterBackwards(..., AllowSpill=true)`. Under greedy
RA pressure the scavenger spilled a **live-out** GPR and inserted the reload
**after** the `JALR_W` terminator in the trampoline MBB (never executed). Dest
block saw a clobbered live-in → wrong `oracle_u64`.

**Fix:** `AllowSpill=false` + RestoreBB path when no free reg (RISC-V peer;
AIE model, no free-AT). Comment + change in `HaydnInstrInfo.cpp`. Lit owned by
CB-124: `llvm/test/CodeGen/Haydn/cb124-branch-relax-nospill-after-jalr.{ll,s}`.

### A/B (pre-fix O2; freestanding multi-TU; host `0x1ab4…`)

| Config | Result | oracle |
|--------|--------|--------|
| baseline | **FAIL** | `0x60eea7826bef4382` |
| `-mllvm -optimize-regalloc=false` | **PASS** | host |
| `-mllvm -regalloc=basic` | **PASS** | host |
| `-mllvm -enable-misched=false` | **PASS** | host |
| `-mllvm -enable-post-misched=false` | **PASS** | host |
| `-mllvm -haydn-enable-ldst-opt` | **PASS** | host |

Soft dual-escape (misched also PASSed) vs pure CB-124 seed2 (misched still
FAIL) — same **greedy/RA pressure → far-branch relax** class, not PreRA sticky
(CB-123).

Per-TU: **func.c** at O2 is the RA-sensitive side (`regalloc=basic` on func
alone PASSed; on driver alone still FAILed).

### Reduce / first-bad

| Surface | Signal |
|---------|--------|
| `tf0` alone / `tf1` alone | O0≡O2 |
| **`tf0+tf1` (upto1)** | O2 mismatch; basic RA fixes |
| empty `tf1_foo` keep `tf0_foo` | O2 mismatch RA-class (host `0x3884…` / wrong `0xc146…`) |
| full seed7 | sticky `0x60ee…` pre-fix |

Workdir: `BundleSim/work/cb125_parallel_20260717/`
(`NOTES.md`, `ab_results.tsv`, `strip/empty_tf1`, `reduce/func.O2.ll` +
`interest_func.sh`). llvm-reduce multi-TU/backend-only path sketched; full
minimal IR not required once root matched CB-124.

### Repro / gate

```bash
export PATH=/ssd2/mhyang/haydn-build/bin:$PATH
export BUNDLESIM_HAYDN_TOOLCHAIN_BIN=/ssd2/mhyang/haydn-build/bin
cd /ssd2/mhyang/BundleSim
build/run_c --case bundlesim/tests/regression/cases/yarpgen_seed7/case.json
# PASS oracle 0x1ab4548cf797a06f
# ctest --test-dir build -R 'bundlesim_reg_yarpgen_seed7$' --output-on-failure
```

Historical note: WIP `-haydn-enable-ldst-opt` default ON once re-broke seed7 to
`0x60ee…` (pre-AllowSpill era); keep ldst-opt default **off** as product
policy. Not required for CB-125 close.

## Hunt context (2026-07-15 → reclass 2026-07-17)

```bash
export PATH=/ssd2/mhyang/haydn-build/bin:$PATH
export BUNDLESIM_HAYDN_TOOLCHAIN_BIN=/ssd2/mhyang/haydn-build/bin
cd /ssd2/mhyang/BundleSim
build/bs_yarpgen --from 6 --to 200 --verify --work work/yarpgen_hunt10 --O -O2
# stopped ~seed 101: pass=71 fail=25  (~26% FAIL)
# 2026-07-17 reclass of work/yarpgen_fail_archive_20260716: only seed2@O2 + seed7@O2 residual
```

| View | Number |
|------|--------|
| Seeds tried (6–101) | 96 |
| FAIL archives | 25 |
| **Unique families** | **3** (CB-119 / CB-120 / CB-121 FIXED); seed6 residual **CB-122 DROPPED**; seed2 high-opt **CB-124 FIXED** |
| CLI class | all `BACKEND_MISCOMPILE` (clang built+linked; no ICE → no IR-only archive) |

Cluster artifacts (BundleSim repo):

| Path | Content |
|------|---------|
| `work/yarpgen_hunt10/cluster/FAMILIES.md` | family table + per-seed features |
| `work/yarpgen_hunt10/cluster/features.tsv` | machine-readable stop/opcode/addr |
| `work/yarpgen_hunt10/CLASSIFY_10.md` | first-10 triage notes |
| `work/yarpgen_hunt10/report.tsv` | full seed status log |

**Not BundleSim defects:** sim correctly faults illegal ADDI32 encoding and
misaligned loads; wrong-oracle seed exits cleanly with a bad checksum.

**ROI:** CB-119+CB-120+CB-121 FIXED; **CB-123 CLOSED** (seed1@O2 gate green);
**CB-122 DROPPED** (seed6 removed); **CoreMark CRC FIXED**; **CB-124 FIXED**
+ **CB-125 FIXED** (shared branch-relax `AllowSpill=false` root). **Currently
OPEN:** none from this hunt.

---

## CB-119 — FIXED (ADDI32 simm20 signed decode; was ILL@entry)

**Pri P0. Compiler (Haydn MC / large-imm materialize). Not BundleSim.**

### Symptom

At **first execute / catalog check** (`bundle_count=0`, always
`pc=0x00010010`), BundleSim rejects the image:

```text
stop_reason=ILLEGAL_INSTRUCTION
subsystem=catalog
message=ADDI32 operand 2 immediate <N> exceeds its 20-bit signed golden representation
```

Signed 20-bit max is \(2^{19}-1 = 524287\). Observed immediates are all larger
(examples: 590352 … 1015344). Haydn **clang still produces an ELF** — this is
not a frontend ICE; it is **illegal encoding** that the golden catalog rejects.

### Cluster (hunt family F1)

| Field | Value |
|-------|--------|
| n seeds | **7** |
| seeds | 6, 10, 45, 50, 52, 67, 84 |
| stop | `ILLEGAL_INSTRUCTION` @ bundles=0 |
| pc | always `0x00010010` |
| opcode in msg | `ADDI32` |

### Representative archives (BundleSim)

| Seed | Archive | Imm in message |
|------|---------|----------------|
| **6** (primary) | `bundlesim/tests/regression/cases/yarpgen_20260715_231820_seed6` | 902944 |
| 10 | `…/yarpgen_20260715_231853_seed10` | 737824 |
| 45 | `…/yarpgen_20260715_232135_seed45` | 769248 |
| 50 | `…/yarpgen_20260715_232216_seed50` | 777568 |
| 52 | `…/yarpgen_20260715_232234_seed52` | 725808 |
| 67 | `…/yarpgen_20260715_232336_seed67` | 590352 |
| 84 | `…/yarpgen_20260715_232526_seed84` | 1015344 |

### Repro

```bash
export PATH=/ssd2/mhyang/haydn-build/bin:$PATH
export BUNDLESIM_HAYDN_TOOLCHAIN_BIN=/ssd2/mhyang/haydn-build/bin
cd /ssd2/mhyang/BundleSim
build/run_c --case bundlesim/tests/regression/cases/yarpgen_20260715_231820_seed6/case.json --keep -v
# BundleSim JSON (stderr on ILL path):
#   ADDI32 operand 2 immediate 902944 exceeds its 20-bit signed golden representation
#   pc=0x00010010 bundle_count=0
```

Objdump entry of kept `program.elf` / `llvm-objdump -d --triple=haydn` around
`.text` start — look for `addi32` / large imm that should have been
MOVT+MOV / multi-instruction materialize.

### Suspected root

Haydn backend (or MC emit) places a constant that does not fit **simm20** into
an `ADDI32` encoding instead of splitting (e.g. `MOV`/`MOVT`, `LOADI*`, or
wider materialize sequence). BundleSim catalog is the **golden** gate — do not
widen the sim field to paper over encode.

### Fix direction

1. Audit `ADDI32` / `addi` patterns and constant materialization for i32.
2. Enforce simm20 in MC / AsmPrinter / InstrInfo (assert or expand).
3. Lit: assemble/encode a constant `> 524287` must not emit single ADDI32.
4. Gate: seed6 `run_c --case` → `GUEST_EXIT` (or at least no catalog ILL).

### Fix (2026-07-16)

**Root:** Bundle128 `simm20` (used by `ADDI32`/`ADDI32S`/`SUBI32`/`SUBI32S`) had no
`DecoderMethod`. Tblgen zero-extended the 20-bit field; `llvm-objdump` printed
e.g. `-132688` as `915888`. BundleSim ELF frontend parses objdump text and
catalog-rejects `|imm| > 524287` at first instruction (`pc=0x10010`, bundles=0).

LLD thunks already emitted the correct HI12/LO20 bit pattern (`HaydnThunks.cpp`);
encoding was legal. Failure was **unsigned decode/print** (same class as CB-99 /
CB-82 / CB-22).

**Change:** `HaydnInstrFormats.td` `simm20` →
`DecoderMethod = "decodeSImmOperandXStepWide<20,0,/*IsSigned*/1>"`.

**Lit:** `llvm/test/MC/Haydn/cb119-addi32-simm20-signed-decode.s`.

**Seed6 after fix:** `stop=GUEST_EXIT` bundles=47822 (no ILL@entry). Residual
oracle host≠guest is **not** CB-119 (separate). Seed45 F1 fully PASS oracle.

Research: `.omc/research/cb119-addi32-simm20.md`.

---

## CB-120 — FIXED (misaligned load: word + half, addr always %4 == 1)

**FIXED 2026-07-16.** Legalizer + ISel (not BundleSim). See
`.omc/research/cb120-misaligned-load.md`.

**Was Pri P0. Compiler (unaligned mem / bitfield lowering). Not BundleSim.**

### Symptom

Program builds and runs, then:

```text
stop_reason=MEMORY_FAULT
message=guest access is misaligned
memory_fault: { reason: ALIGNMENT, access: READ, address: 0x…, size: 4|2, alignment: 4|2 }
```

| Sub-family | Opcode | size | n | Seeds |
|------------|--------|------|--:|-------|
| **F2a** word | `S_LW_WITH_REG` | 4 | 12 | 8, 12, 14, 39, 48, 49, 55, 71, 77, 87, 92, 100 |
| **F2b** half | `S_LHWU_WITH_IMM` | 2 | 5 | 24, 51, 54, 56, 98 |

**Strong motif:** every fault address in the hunt has **`addr % 4 == 1`**
(low bits `…1/5/9/d`). Not random misalignment — systematic off-by-one / wrong
low-bit on the load effective address.

Treat F2a+F2b as **one root** (alignment of derived pointers) unless reduce
proves separate ISel paths.

### Representative archives

| Role | Seed | Archive | Opcode | bundles | fault addr |
|------|------|---------|--------|--------:|------------|
| **Earliest word (primary)** | **14** | `yarpgen_20260715_231921_seed14` | `S_LW_WITH_REG` | 111 | `0x00074efd` |
| Early word | 12 | `yarpgen_20260715_231902_seed12` | `S_LW_WITH_REG` | 143 | `0x0004f8c5` |
| Early half | 98 | `yarpgen_20260715_232607_seed98` | `S_LHWU_WITH_IMM` | 149 | `0x00027661` |
| Late half | 24 | `yarpgen_20260715_231957_seed24` | `S_LHWU_WITH_IMM` | 8130 | `0x00046671` |
| Known-seed related | 8 | `yarpgen_20260715_231831_seed8` | `S_LW_WITH_REG` | 7540 | `0x0005904d` |

(Paths under `bundlesim/tests/regression/cases/`.)

Historical seed8 packages `cb62_seed8` / `cb64_seed8` are **exit_code** style and
not the same oracle package; still useful as related prior art.

### Repro

```bash
export PATH=/ssd2/mhyang/haydn-build/bin:$PATH
export BUNDLESIM_HAYDN_TOOLCHAIN_BIN=/ssd2/mhyang/haydn-build/bin
cd /ssd2/mhyang/BundleSim
build/run_c --case bundlesim/tests/regression/cases/yarpgen_20260715_231921_seed14/case.json --keep
# stop=MEMORY_FAULT bundles=111
# BundleSim JSON: opcode=S_LW_WITH_REG address=0x00074efd ALIGNMENT READ size=4
```

### Suspected root

Codegen produces **word/half loads** from addresses that are not naturally
aligned — e.g. incorrect GEP scaling, treating `i8*` arithmetic as `i32*`
without adjustment, or stack/global layout vs load width. Sim alignment check
matches golden (correct to fault).

### Fix direction

1. Reduce seed14 / seed12 (C or IR) to minimal misaligned `lw`.
2. Check Haydn load/store ISel, address mode folding, ABI stack alignment.
3. Lit: load `i32` from known-aligned base must never encode EA with `ea&3==1`.
4. Gate: seed14 + seed98 → `GUEST_EXIT` with matching host oracle.

---

## CB-121 — FIXED (seed7 dual defect: widen golden + call modeling)

**Pri was P1. Fixed 2026-07-16. Not BundleSim catalog.**

### Split root (both landed)

1. **Harness (CB-113-adjacent):** filed `oracle_u64=0xbac946fec14bd7a4` used
   LP64 `gcc` + `widen_long`. That is **not** ILP32-faithful for
   signed→`unsigned long` (high-half skew). Correct ILP32 host (narrow
   `long`→`uint32_t`/`int32_t` + `UL`→`u`) is **`0x1ab4548cf797a06f`** — the
   hunt guest already matched this. `bs_yarpgen_host_oracle_u64` now uses
   `bs_yarpgen_narrow_long_file` instead of widen.
2. **Compiler hygiene (call modeling):** `PseudoCALL` was missing `isCall=1`
   (while `PseudoCALLIndirect` had it); `JAL_W_S0` Defs omitted **R12** vs
   `JAL_W`/legacy (CB-97 class). Fixed so pre-RA sched / RA see direct calls
   and the S0 commit form as full call clobbers.

### Gate

```text
build/run_c --case …/yarpgen_20260715_231825_seed7/case.json
# PASS oracle=0x1ab4548cf797a06f stop=GUEST_EXIT
# also yarpgen_20260716_130658_seed7
```

### Files

| Area | Path |
|------|------|
| Harness | `BundleSim/bundlesim/tools/bs_yarpgen/bs_yarpgen_narrow.c` (new) |
| Harness | `bs_yarpgen_package.c`, `bs_yarpgen.h`, docs |
| Cases | seed7 packages: `oracle_u64` + `host_oracle.txt` → `0x1ab4…` |
| Compiler | `llvm/lib/Target/Haydn/HaydnInstrInfo.td` (`PseudoCALL` `isCall`) |
| Compiler | `llvm/lib/Target/Haydn/HaydnFormatsALU32.td` (`JAL_W_S0`/`JALR_W_S0` R12) |

### Notes

- Enabling concurrent WIP `-haydn-enable-ldst-opt` default ON **re-breaks**
  seed7 (`0x60ee…`); keep ldst-opt default OFF until that residual is fixed
  separately (not required for CB-121 close on HEAD).
- Research: `.omc/research/cb121-seed7-oracle.md`.

---

## CB-122 — DROPPED (seed6 gate removed; not a debug target)

**Not OPEN. Not a BundleSim regression case. Packages deleted 2026-07-17.**

Historical class: greedy RA wrong `oracle_u64` vs ILP32 `narrow_long` on
yarpgen seed6 (~8k LOC). Multi-shot reduce never produced a fixable surface;
free-AT crutch rejected (AIE model landed `adcc325`). **Do not re-open seed6
as a gate.** Seed2 high-opt RA residual (same *diagnostic class*, different
seed) is tracked separately as **CB-124** — do not fold seed2 back into CB-122
or re-open seed6 packages.

---

## CB-124 — FIXED (seed2@O1–O2; branch-relax scavenger AllowSpill)

**Pri was P2. Fixed 2026-07-17. Compiler. Not BundleSim. Not free-AT.**

### Symptom (pre-fix)

yarpgen **seed 2** matched ILP32 host at **`-O0`** but wrong `oracle_u64` at
**`-O1` / `-O2`** (`BACKEND_MISCOMPILE`, clean `GUEST_EXIT 0`).

| Package | Opt | Pre-fix | Post-fix |
|---------|-----|----------|-----------|
| `yarpgen_seed2` | **-O0** | PASS `0xfc46…` | PASS |
| `yarpgen_seed2_O2` | **-O2** | FAIL (e.g. `0xeb65…`) | **PASS** `0xfc46…` |
| per-TU O1 IR (`func.bc` greedy) | **-O1** | FAIL `0xe5f1…` | **PASS** `0xfc46…` |

Host expected: `0xfc460d2be25adb0b` (`narrow_long` / CB-113/121).

### Flag A/B (diagnostic class looked like RA/greedy)

| Config | Result | Signal |
|--------|--------|--------|
| baseline greedy | **FAIL** | product path |
| `-optimize-regalloc=false` / `-regalloc=basic` / `-regalloc=fast` | **PASS** | escape via less pressure / fewer far branches |
| `-enable-misched=false` | **FAIL** | PreRA/misched **not** root |

TSV: `BundleSim/work/hp_matrix/cb124_seed2_ab_20260717.tsv`.

### Root cause (not free-AT; not greedy itself)

`HaydnInstrInfo::insertIndirectBranch` used
`RegScavenger::scavengeRegisterBackwards(..., AllowSpill=true)`.

Under greedy pressure on large foos (`func.c` / `tf_*_foo`), BranchRelaxation
builds empty trampoline MBBs with many live-outs. AllowSpill scavenger spilled a
**live-out** GPR and reinserted the reload **after** `JALR_W` (terminator) in
the trampoline:

```text
ST32  killed $r8, %stack.N     ; spill live-out
$r8 = LOADI32 %bb.dest
$r8 = JALR_W $r8, 0            ; terminator (indirect far jump)
$r8 = LD32 %stack.N            ; DEAD reload — never executed
; dest block live-in $r8 is clobbered → wrong oracle
```

`-verify-machineinstrs` reported 14×
`Non-terminator instruction after the first terminator` on `tf_0_foo` under
greedy; basic RA verified clean (different layout / free regs).

**Not** call-clobber of CSR across calls in `func.bc` (foos have no calls);
the "RA/greedy" A/B was a pressure proxy that changed which far branches needed
scavenging.

### Fix

```text
llvm/lib/Target/Haydn/HaydnInstrInfo.cpp  insertIndirectBranch
  AllowSpill=false  (match RISCVInstrInfo::insertIndirectBranch)
  no free reg → spill R11 + jump via RestoreBB (existing path)
```

AIE model: no free AT. Lit:
`llvm/test/CodeGen/Haydn/cb124-branch-relax-nospill-after-jalr.ll`
(`-verify-machineinstrs` + `jalr_w` CHECK).

### Reduce / evidence paths

| Path | Content |
|------|---------|
| `BundleSim/work/cb124_parallel_20260717/` | parallel lane workdir |
| `…/ir/per_tu/{driver,func,target_main}.bc` | per-TU `-O1` emit-llvm (linked LTO IR **does not** repro) |
| `…/interest_host.sh` / `interest_diff.sh` | greedy≠host & basic==host / differential |
| `…/reduce/reduced_diff.ll` | llvm-reduce differential (~1.1k lines mid-run) |
| `…/mir/` | greedy vs basic after virtregrewriter |
| `…/hp_matrix/cb124_seed2_ab_20260717.tsv` | O1/O2 A/B |

### Gate

```bash
export PATH=/ssd2/mhyang/haydn-build/bin:$PATH
export BUNDLESIM_HAYDN_TOOLCHAIN_BIN=/ssd2/mhyang/haydn-build/bin
export HAYDN_SYSROOT=/ssd2/mhyang/haydn-build/sysroot/haydn-unknown-elf
cd /ssd2/mhyang/BundleSim
build/run_c --case bundlesim/tests/regression/cases/yarpgen_seed2/case.json
build/run_c --case bundlesim/tests/regression/cases/yarpgen_seed2_O2/case.json
# both PASS oracle=0xfc460d2be25adb0b
# remove WILL_FAIL on bundlesim_reg_yarpgen_seed2_O2 (BundleSim CMakeLists)
```

---

## CoreMark CRC — FIXED (2026-07-17)

**Pri P2. No CB id.** Host oracle freestanding **i386 ILP32** (not CB-113).

### Symptom (pre-fix)

| field | host | guest (broken) |
|-------|------|----------------|
| `seed_crc` | `0xe9f5` | match |
| `list_crc` | `0xe714` | `0x4aba` **first reported bad** |
| `matrix_crc` / `state_crc` | known | also wrong (list `calc_func` side-effects) |
| `error_count` | 0 | 3 |

### Root (reduced)

1. All TUs `-O0` PASS; only `core_matrix.c` @ `-O1+` fails.
2. Function bisect: **`matrix_mul_const`**.
3. Micro N=4: O1 leaves C row0 as `0x5a5a5a5a`; products start at row1.
4. MIR correct after `haydn-hwloops`; **post-RA Stage-0 invent** ZOL
   exit→preheader hoist moved `r2 += stride` before preheader `r8 = MOVE r2`
   (row base). AIE InterBlock does **not** Stage-0-hoist exit MIs this way.

### Fix

1. **Interim:** reject hoist when a live def is still read by remaining
   post-SET preheader MIs (preheader-IV guard).
2. **Final (product):** **delete** `tryZOLExitToPreheader` entirely — reject
   contract (body clobber, preheader IV, uimm6 caps, sentinel safety, …)
   cost more than any measured densify win. Stage-0 IB is **acyclic
   fallthrough pack only**; FixupHwLoops remains sole SET→body deficit padder.
- Evidence: `BundleSim/work/coremark_parallel_20260717/FINDINGS.md`

Post-fix: full CoreMark qualification MATCH (stock opts) without the invent.

### Follow-on all-O2 (2026-07-17)

After invent delete, **matrix.c @ -O2** still failed (stock had matrix pinned
`-O1`). Two more bugs, both AIE-aligned:

1. **Role A `LoopStart` adj expand** clobbered trip GPR in-place
   (`ADDI TripReg, -1` then `SET TripReg`). AIE writes `LC = src + adj`
   preserving src. Fix: adj into a different free GPR. Lit:
   `hwloop-rolea-adj-preserve-trip.mir`. Symptom: `matrix_sum` MEMORY_FAULT
   past `.bss`.
2. **Role B multi-BB outer + nested ZOL** converted outer soft loop to
   `SET_HWLOOP_REG 0` around inner SET; postmisched/PostPipeliner rewrote
   outer trip (`N` → `N-3` in clobberable `r1` → 0). Fix: decline multi-BB
   Role B when a child already took a HWLR level; PostPipeliner skips
   multi-BB SET. Lit: `hwloop-multibb-nested-decline.mir`. Symptom:
   `matrix_mul_matrix_bitextract` ST ALIGNMENT @ `0xe`.

**Product:** CoreMark qualification all TUs `-O2` MATCH i386 ILP32 known CRCs
(iters=1). Matrix `-O1` pin retired.

---

## Suggested fix order

1. **CB-119 FIXED** / **CB-120 FIXED** / **CB-121 FIXED** (seed7 O0/O1).
2. **CB-122 DROPPED** (seed6 removed — do not re-open).
3. **CB-123 CLOSED** — seed1@O2 gate green (`yarpgen_seed1` @ `-O2`).
4. **CB-124 FIXED** — seed2@O1–O2; branch-relax `AllowSpill=false`; promote `yarpgen_seed2_O2`.
5. **CB-125 FIXED** — seed7@O2 prevent-reg; same AllowSpill root as CB-124.
6. **CoreMark CRC FIXED** — ZOL exit→preheader invent removed; guest CRCs match i386 host.

### CB-123 — CLOSED (seed1@O2 cannot-repro sticky; gate promoted)

**Was:** dual-sched PreRA sticky guest `0xb863764be3e55d80` / first-bad
`tf_0_var_94=0x3a`; cleared historically by `-enable-misched=false`.

**Now (2026-07-17, pure AIE model HEAD):** default `-O2` guest matches ILP32
host `0x7b0a94956fc95d3d` (`run_c` + `ctest -R bundlesim_reg_yarpgen_seed1`).
Curated case optimization raised **O1 → O2**. Do not rebaseline the golden.

Optional hygiene (not OPEN): AIE PreRA defaults still diverge
(`isAvailableNode` pressure delay off; `PropagateIncomingLatencies` PreRA on
vs AIE off; no force-schedule 1-MI regions).

---

## CB-113 — CLOSED (harness ABI: LP64 host vs ILP32 Haydn `long`)

**Not BundleSim. Not soft-div. Not a Haydn codegen defect.**

**Minimal repro** (`BundleSim/benchmarks/diff_sweep/abi_long/long_hi.c`):

```c
unsigned long g = 0xE8C2F1A29B3D4C59UL; /* > 2^32 */
int main(void) { return (int)((g >> 32) & 0xFF); }
```

| side | `sizeof(long)` | exit (mod 256) |
|------|----------------|----------------|
| host gcc (LP64) | 8 | 162 (`0xA2`) |
| Haydn greenfield ELF | 4 | 1 (truncated / shift overflow on width) |

Haydn DataLayout: `e-m:e-p:32:32-…-n32-S64`.

**Harness fix:** ILP32-matched host golden, `yarpgen -m 32`, or force 64-bit
types to `unsigned long long`. Do not “fix” by patching the sim.

## CB-111 dual residual — CLOSED (obsoleted by greenfield)

**Was:** large yarpgen TUs disagreed across legacy `link.sh -o` (`.s`) vs
`-E` (`.elf`) and host (seed 2967: host8=254 s8=106 e8=40). Diagnosis on the
archived ISS implicated D-format PC stride 8 vs Bundle128 16 on the `.s`
path; **not fixed in BundleSim code** (user ban) and later **removed as a
product path**.

**Now:** greenfield is ELF-only (`run_c` / campaign BSP). Dual-flow residual
cannot reproduce. Large-seed plumbing → **CB-117 / CB-118 FIXED/DROPPED**.

Simple control still holds: `path_div.c` → host≡sim (greenfield MATCH 52).

## CB-115 — FIXED (systematic dead-end terminators)

**Root:** IRTranslator leaves empty MBBs for `unreachable` when
`TrapUnreachable` is off (no `G_TRAP`). PEI restore points are **return
blocks only**, so mid-function dead-ends never get an epilogue. Empty
`.LBB` before `.Lfunc_end` falls into the next symbol.

**Fix (layered):**

1. **Primary:** `HaydnEnsureTerminators` — post-PEI, all opt levels: ∀ MBB
   (non-EH, `succ_empty`, no terminator) → `RET`.
2. **ISel:** `G_TRAP` / `G_DEBUGTRAP` / `G_UBSANTRAP` → `RET`.
3. **Belt:** FrameLowering empty-entry RET (if ensure pass disabled).

**Lit:** `llvm/test/CodeGen/Haydn/cb115-unreachable-terminator.ll`
(`all_unreach`, `maybe_unreach` mid-arm, `empty_ret`; O0+O2).

**Greenfield evidence (2026-07-13):**

| variant | result |
|---------|--------|
| seed 2927 `tf_4_init`+`tf_4_foo` only | **MATCH** host=0 sim=0 `GUEST_EXIT` |
| `maybe_unreach` mid-arm (pre-fix) | empty `.LBB` → fall-through |
| `maybe_unreach` mid-arm (post-fix) | `{ jalr_w r0, lr, 0 }` on both arms |

## CB-117 — FIXED (seed 3148; D490 + D492 series)

**Gate:** greenfield BundleSim ELF only. Always **relink with current BSP**
after PEI/libc changes (`run_c --rebuild-bsp`); frozen ELFs under `/tmp`
are not authoritative.

### Layers fixed

| Layer | Fix | Evidence |
|-------|-----|----------|
| GenMux MEMORY_FAULT | D490 EarlyIfConv / GenMux retirement | no more fault on seed path |
| Constant `guest_exit` / low8=100 | D492 PEI no live-in R1 | `exit(status)` preserves arg |
| Seed→0 after few hashes | D492b call-preserved PEI scratch | no unsaved R8 CSR base |
| `ST64 …, fp` MEMORY_FAULT | D492c never R14 PEI scratch + live FP setup | `9dd5046b`… |
| Reduced `tf_2_foo` @ **-O2** | not a CSE bug | host≡guest low8 **205** |
| **Full seed3148** | clean BSP after D492 | host **137** ≡ guest low8 **137** |

**Verify (2026-07-14):** `/tmp/cb117_seed3148_full/program.elf` after
`--rebuild-bsp` + clang `-O2` + current `haydn-build`:
`stop_reason=GUEST_EXIT`, `guest_exit_code` low8=137, host `seed^(seed>>32)` low8=137.

**Non-issues (false leads):**

- MachineCSE of MOVT/SEQ on reduced foo — value-preserving imm/AND pooling.
- Old `p_f_O2.elf` low8=72 — stale BSP `exit`, same `f_O2.o` relinked → 205.

## CB-118 — DROPPED (invalid test; plumbing FIXED)

**Not a Haydn open-compiler gate.** Yarpgen seed **2967** residual host≠sim
is undefined / non-portable source (LP64 `long`, huge shifts, `&&`+`<<` mess).
After no-long strip, first hash DIFF was `tf_2_var_293` (host=1 guest low8=0)
— still not a defined-behavior golden.

| Layer | Status |
|-------|--------|
| PEI exit / large guest_exit | **FIXED** (D492) |
| `LOADI64` clobber live R12 → UNMAPPED | **FIXED** (PostRAScratch free-reg scavenge; no fixed-R12 AT) |
| host≠sim content hash | **DROPPED** — invalid test, not a fix target |

**AIE policy (2026-07-17):** R12 = normal allocatable caller-saved GPR.
**No free AT** — `FeatureReserveR12AT` / `-mreserve-r12-at` **deleted**.
MatInt: LOADI32→Dst (RISCV `movImm` style); LOADI64/VASTART→`HaydnPostRAScratch`
(LivePhysRegs free-reg first, PreferNotR12; spill home `PostRAScratchFI` only
if none free). EFI large offset: `createVirtualRegister` (RISCV/AIE).

**P0 L2 — post-RA spill FI far offset / llvm-libc (2026-07-16 Track D + C):**
Large vararg frame + FP used to hard-fatal print-time fixed-R12 AT spill.
**FIXED:** VASTART uses pre-pack `withPostRAScratch`; far FI via R0+re-zero in
spill path. Lit: `llvm/test/CodeGen/Haydn/vastart-large-fp.ll`.
**llvm-libc policy unchanged (Track D):** omit-FP only in
`libc/cmake/caches/haydn-unknown-elf.cmake`.

**Valid gates going forward:** greenfield `run_c` cases with defined C,
`yarpgen -m 32` only when used as compile smoke, not as host-mod-256 oracle
without sanitizing UB.

## CB-163 — OPEN: MOVE32_DR_L/H emitted with GPR (rN) source; sim catalog aborts

**Minted 2026-08-26.** Toolchain: clang 22.1.8 `1d9f3c7f3175` (w68-new).
BundleSim catalog `529c0dcfa10364243a82941aa210743e2b646f4b21c3fe901c4c956c954ed432`;
ISA semantic baseline `e74b2911ac7c7d171cb488bf6277901ed48cf1c3cf044b42f651d681685a5ff5`.

**Was:** naturedsp FIR family gate `fir_gate_blk` fails at **every** opt level
(O0/O1/O2/O3), product AND C_REF-oracle arms. compile+link succeed; BundleSim
aborts on the first `move32_dr_*`:
`ILLEGAL_INSTRUCTION  "operand kind disagrees with the golden catalog"`
(O0: `pc=0x100f0 line=34`; O1+: `pc=0x10438 line=100`).
Not the HWLOOP far-form issue (CB-164; that is gone on `1d9f3c7f`).

**Root cause:** `move32_dr_l/h` is a DR→GPR move (ISA `MOVE32_DR_L rt, rsd`,
`rsd` = DR pair). The frozen golden catalog pins `MOVE32_DR_L/H` operand[1] =
`BS_OPERAND_DR`. The compiler emits **943 of 2049** `move32_dr_*` with a **plain
GPR source (`rN`)** instead of a **DR pair (`dN`)** (1106 are correct `dN`).
The frontend `bundlesim/src/frontend/haydn_dump_parser.c` parses `rN`→GPR and
`dN`→DR (lines 50-53), so every GPR-source `move32_dr_*` fails the catalog's
operand-kind check → run aborts at instruction #1.

**Why the compiler is at fault (not catalog / not kernel / not LP64):**
- The raw encoding is unchanged; bytes decode identically; only the emitted
  operand **token** is wrong (`rN` vs `dN`).
- The compiler **already has** the correct GPR→GPR op **`MOVE32`** (`{GPR,GPR}`,
  emitted 735× in the same ELF). A split 64-bit value should be consumed via
  `MOVE32` (or via a `dN`-source `move32_dr_*`), not via `move32_dr_l rN`.
- Each `dN` form passes the catalog (1106/2049), so the catalog row is correct.
- Repro is independent of my 10 uncommitted a-variant firblk fast-paths
  (fails on committed source) and of the kernels (fails on the C_REF arm which
  uses no intrinsics).

**Reproduce** (BundleSim repo root; wrapper scripts `run_fir_gate.sh` /
`run_fft_gates.sh` mangle groups and masked this — drive `run_c` directly):

```bash
cd /ssd2/mhyang/BundleSim
export BUNDLESIM_HAYDN_TOOLCHAIN_BIN=/ssd2/mhyang/haydn-build/bin
BS=benchmarks/naturedsp_kernels
INC="-I $BS -I $BS/kernel_v4/include"
./build/run_c -O0 --bundle-limit 60000000 $BS/tests/fir/fir_gate_blk.c $INC --cflag -Wno-macro-redefined
#   -> ILLEGAL_INSTRUCTION "operand kind disagrees with the golden catalog"
#      stop_reason=ILLEGAL_INSTRUCTION  pc=0x000100f0  line=34  elf_sha256=f1659574...
# same at -O1/-O2/-O3 (pc=0x10438 line=100), and with -D HAYDN_KERNEL_C_REF=1 (oracle).
```

**Evidence** (object built from the gate at -O0):

```text
100f0: 07 44 80 01 ...   { nop; move32_dr_l  r8, r1 }   <-- WRONG: source r1 is a GPR
100fc: 07 44 70 02 ...   { nop; move32_dr_l  r7, r2 }   <-- WRONG
10108: 07 44 50 03 ...   { nop; move32_dr_l  r5, r3 }   <-- WRONG
102dc: cf 6b a9 1b ...   { move32_dr_h r3, d4; move32_dr_l r2, d4; ... }  <-- correct dN forms
2049 total move32_dr_* in ELF:  1106 dN (DR, correct), 943 rN (GPR, malformed)
MOVE32 (GPR,GPR) in same ELF: 735 (the op that should have been used for GPR→GPR)
```

**Clean minimal repros (object-only) confirm `dN` is valid and common:**
`c_call64.c` / `d_sel64.c` (64-bit call/select) emit `move32_dr_l rN, d0` —
no GPR-source form — i.e. the malformed `rN`-source form needs the full
inlined-fast-path shape; the gate `fir_gate_blk.c` is the reliable reproducer.

**Fix direction (owner):** in the 64-bit lowering / `MOVE32` vs
`MOVE32_DR_L/H` selection + RA slicing: only select `MOVE32_DR_L/H` when the
source is a live DR pair (`dN`); otherwise lower to `MOVE32` (GPR→GPR). A
minimal gate run after the fix should pass `fir_gate_blk` bit-exact at all
opt levels (expect the `MOVE32` count to absorb the 943).

## CB-164 — closed/regression-guard (filed 2026-08-25; GONE on current HEAD)

**Was** (toolchain clang 22.1.8 `d9cba20c772c` W68.3): `fir_gate_blk` at
-O1/-O2 failed to **compile** with

```text
error: relocation offset out of range   (x2)
```

`clang -S` emits fine; `clang -c` (assembler) rejects. Both sites are
`set_hwloop_f2` (hardware-loop **far form**) whose loop-back distance exceeds
the immediate field:

```text
firblk.s:28982: set_hwloop_f2 0, .LLhwloop_start284, .LLhwloop_end284, r6; ...
firblk.s:31428: set_hwloop_f2 0, .LLhwloop_start315, .LLhwloop_end315, r1; ...
```

opt-gated: -O0 compiles+passes, -O1/-O2 fail; both sites inside inlined `main`
(19 blk kernels #included; -O2 inlines them → an inlined loop body far enough
back to overflow). **Workaround:** `-mllvm -haydn-enable-hwloops=false` → blk
compiles AND passes bit-exact (19 kernels / 75 run-checks, GUEST_EXIT 0).

**Status:** retested 08-26 on `1d9f3c7f3175` — **GONE**. The HWLOOP pass now
demotes to a software loop instead of emitting an unencodable far form.
Keep as a regression guard: far-form selection must never emit when the offset
can't fit. Distinct from CB-162 (software-*demoter* live-out trip-reg clobber);
this was far-form *emission* range.

