# OPEN Haydn compiler bugs blocking BundleSim

> **STATUS (2026-07-24)** — live tools: `$HAYDN_BIN` / BundleSim `build/`.
> Product path: `build/BundleSim` + `build/run_c` + `haydn_bsp`.
> Torture gate: **`scripts/run_gcc_torture_lit.sh`** (llvm-lit + freestanding
> link + BundleSim). Upstream clang disables = llvm-testsuite
> `execute/CMakeLists.txt` `TestsToSkip` (**84**). Lit-enabled = **1430**.
>
> **gcc-c-torture/execute lit-enabled @ -O3** — latest run, after CB-137
> (`scripts/run_gcc_torture_lit.sh -O3 -- -j16`, upstream llvm-test-suite HEAD):
>
> | Result | Count | Note |
> |--------|------:|------|
> | **PASS** | **1417** | GUEST_EXIT 0 — **no FAIL, no TIMEOUT** |
> | FAIL | **0** | was 29 before CB-137 (all "guest access is misaligned") |
> | TIMEOUT | 0 | |
> | UNSUPPORTED | 97 | upstream `TestsToSkip` only; Haydn hang-skip list is now empty |
> | Total `.c` | 1514 | |
>
> Previous recorded run (2026-07-24, older test-suite checkout): PASS 1408 /
> FAIL 14 / TIMEOUT 1 / UNSUPPORTED 91. Absolute counts are not directly
> comparable — the upstream skip list grew (84 → 97).
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
> | **CB-145** | P2 | process | **gcc-c-torture has not run since the format E switch.** The last run is 2026-08-05: 1417 tests, -O3, 0 FAIL. Everything since — PEI displacement scaling, the lld thunk addend, 48 `isPseudo` opcodes, DWARF line addresses, the itinerary retarget, the store/load overlap rule, three legalizer fixes — is unmeasured against it, and it is the largest independent corpus here (CB-137 was found by it, 29 failures). The `pr28982a` hang is the one file that happened to be compiled by hand, and it turned out to be a regression the suite would have caught. A run is in progress |
> | **CB-143** | P3 | unit model migration | **The slot model is gone and the packer axes are settled.** `SLOT0/1/2` deleted, `HAYDN_NUM_FU_BITS` = 7, 512 logicals retargeted per def, 31 unencodable defs removed and the 21 tests that assembled them moved to the live mnemonics (`6f37a306084f`, `83959145aafd`, `3dd56bf31023`). Of B4's three remaining axes, **two were already modelled and the entry was wrong about them** (`2cddbe292469`): per-entry immediate width is enforced by construction — the generator emits a member only where the instruction fits, so `ADDI32` has two placements and `ADD32` seven — and the register-port budget is bundle-wide already, because `HaydnFuncUnitWrapper::operator|=` accumulates. The third, **store/load overlap, was genuinely missing and is now enforced**: the ISA requires the compiler to prove disjointness or split the bundle, the hardware raises an exception otherwise, and nothing checked it. ~2% bundles. **Residual**: unit exclusivity is still pairwise in one direction (three instructions each needing {ALU1, ALU2} pass and cannot all issue) — a wasted pack attempt, not a wrong bundle, since the placement search decides. |
> | **CB-126 residual** | — | GISel legalize | **No observed failure.** Every case the ledger attributes to CB-126 (`pr79737-2`, `20040709-2/3`, `strct-pack-1`, `pr29006`, `pr53688`, `pr70903`, `pr57344-3`) PASSes at -O3 with 0 FAIL across all 1514 torture tests. Keep closed unless a new reproducer appears |
>
> ### Closed / fixed
>
> | Item | Evidence |
> |------|----------|
> | **CB-130 residual** — vector predicate legalization | `.clampMaxNumElements(0, S1, 1)` was the whole of it, and it could not have worked: `clampMaxNumElements` builds its target with `LLT::scalarOrVector()`, which returns a **scalar** for a count of one, so it asked `fewerElementsVector` to narrow `<2 x s1>` to plain `s1` and `fewerElementsVectorMerge` asserted "Expected vector types". **One element is not a smaller vector.** The rule is deleted rather than repaired: a `<N x s1>` build is an ARTIFACT of scalarizing a vector `G_ICMP`, the matching unmerge arrives when its `G_ZEXT`/`G_SELECT` users are scalarized in turn, and Haydn has no vector-of-i1 for it to become — falling to `.lower()` reports UnableToLegalize, the legalizer defers to the artifact combiner and the pair cancels. The comment claiming a bare `.lower()` hangs on `pr28982a` does not apply: **pr28982a hangs with the clamp present too**, in `G_EXTRACT_VECTOR_ELT` on `<16 x s32>` with a variable index (register numbers past %300000), which is a separate open bug. `bundlesim_reg_cb44_o2_stale_cond_max_reduce` now exits 145 and MATCHes the host oracle, so the original CB-44 wrong-answer symptom is gone with it. **BundleSim ctest is 225/225.** Test: `CodeGen/Haydn/gisel/cb130-v2i1-predicate.ll`  The `pr28982a` hang recorded alongside this is a **REGRESSION**, not the long-standing bug it was written up as: the 2026-08-05 torture run compiled and ran it clean at -O3 (`GUEST_EXIT` 0, 17102 bundles, artifact under `gcc-c-torture-lit/artifacts/pr28982a`), and reverting the fix shows it hangs at **-O3 as well as -O2** — so "the harness only runs -O3 and I found it at -O2" does not explain it. Introduced somewhere in the format E work between 2026-08-05 and 2026-08-12; not bisected, which would need a full LLVM build per step. See CB-145. |
> | **CB-144 i1 vector element access** | `extractelement`/`insertelement` on a `<N x i1>` with a VARIABLE index was unlegalizable — Haydn has no vector-of-i1, and the generic lowering spills to a stack slot and gives up on an element that is not byte-sized. Fixed by widening the ELEMENT out of i1 rather than teaching anything to hold one: `widenScalar` on type index 0 anyexts the source vector and truncates the result back, so it becomes an s32 extract from a `<N x s32>`. Verified by execution in the simulator, both lanes both ways. Test `CodeGen/Haydn/gisel/cb144-i1-vector-variable-index.ll` |
> | **CB-136 unguarded `haydn.h`** | `#include <haydn.h>` alone emitted 159 "needs target feature" errors — the default set is `-bit-reversed,-circular-buffer,-simd` and 168 wrapper BODIES call builtins needing one of them, so the header failed before the user wrote anything. The per-op expression was already in `Entry::Features` and simply unused at emission; it is now an `__attribute__((target(...)))` per wrapper, immintrin.h-style, so the diagnostic lands on the CALL where a user can act on it. Every Haydn op names exactly one feature — audited, not assumed (`-gen-haydn-op-feature-audit`: simd 159, bit-reversed 9, agu 7, circular-buffer 6, no ANDs, no ORs) — and the emitter rejects an OR rather than dropping it. Six hand-written wrappers name their feature literally. `6197e624fce4`; test `Headers/haydn-bare-include-no-feature-errors.c` |
> | **CB-141 slot model vs ISA database** — **premise superseded, not a bug** | CB-141 asked an ISA owner to rule on (1) may a 32-bit GPR ALU op issue outside Slot0 and (2) may a store issue outside Slot0. Both questions were built on a **retired** revision of `VLIW_Engine_Compiler_Constraints.md` (the `VLIW_Engine_Database_20260701/` copy, since removed). The current document describes a **unit model**, not per-slot capability, and `instruction_type_index.json` answers both directly: `ADD32`/`ADDI32`/`XOR32`/`MOVE32` are `Available: [ALU0, ALU1, ALU2]`, so `Slot012_ALU` was **correct**; all 34 stores are `Available: LOADSTORE0` and the machine has exactly **one** such unit, so the real rule is "at most one store per bundle", independent of position. The measured "6483/19327 (34%) ALU32 outside S0" and "989 (5%) store outside S0" are therefore **not violations**, and the projected density drop from 1.15 to ~1.00 ops/bundle does not follow. **One finding survives**: the 22 bundles holding two stores are genuine — two entries cannot both map to LOADSTORE0. Migration tracked as CB-143 |
> | **CB-142 bundle text did not name the slot** | `clang -S x.c && clang -c x.s` did not reproduce `clang -c x.c`: hard `error: incorrect bundle` at -O2/-O3/-Os on dhry_1.c, silent slot migration at -O1 (112 bundles). Two causes. (1) The AsmParser ignored the textual position entirely, and the `_S0/_S1/_S2` members of a multi-slot logical share one AsmString, so the matcher pinned every bundle mnemonic to the FIRST member and its fixed `getSlotKind` forced it into that member's slot. (2) `ST32_POST_S1`/`ST64_POST_S1` were `isCodeGenOnly`, which drops a mnemonic from the asm matcher as well as the decoder trie (d463 class), so `st32_post` printed but would not parse. Fix: bundle text is now slot-positional in ISA order — **`{ slot2; slot1; slot0 }`, right-aligned on s0** (`VLIW_Engine_Compiler_Constraints.md` "Bundle format: ``G:{`slot2`, `slot1`, `slot0`}``"). `BUNDLE128_FULL`'s AsmString became `"$s2; $s1; $s0"` (printer + objdump), the parser hints `Bundle::add` with slot `N-1-i` and de-materializes the matched member to its logical base so `encodeSlotSubInst` materializes `Alts[SlotIdx]`, and the post-inc aliases moved to `DecoderNamespace = "FlexEmitterOnly"`. An illegal hint still falls back to the solver; a single-entry `{ op }` keeps encoder-chosen placement. Encoding is untouched (`Inst = {s2, s1, s0}` and the operand dag are unchanged). BundleSim's `haydn_dump_parser.c` walks the same order. **294 (file, -O0/-O1/-O2/-O3/-Os) pairs across dhrystone + BSP + regression cases are byte-identical**; dhry_1 direct vs two-step ELFs are byte-identical; `c-e2e-bundle-dump.ll` lost its `XFAIL`. Test: `MC/Haydn/cb142-bundle-slot-position-roundtrip.s` |
> | **CB-139 exposed-pipeline latency** | Haydn has no interlock but nothing enforced `Data_Latency = 2`: `adjustSchedDependency` deliberately softened load→use to 1, and the 14 `_S2` LS formats claimed `Slot2_ALU` (latency 1). Dhrystone -O0 had 90 violating sites, -O2 had 3. New `HaydnLatencyStalls` pass (all opt levels) + new `Slot2_LS` itinerary → **0 violations across a 50-file corpus**. Cost +13% bundles at -O0, +1% at -O2 |
> | **CB-140 hwloop body < 3 bundles** | `MinBodyBundles` was hard-coded to 0 and annotated "deprecated as a legality floor", but the spec says "Loop Body: It must contain at least 3 instruction bundles". Tiny ZOL bodies (1–2 bundles) reached the assembler. Now 3, padded in `HaydnFixupHwLoops` before the inclusive END |
> | **CB-138 vector lane unmerge miscompile** | `G_UNMERGE_VALUES` had a fail-OPEN fallback that COPY'd the whole source into every def, so unhandled shapes silently read lane 0 for every lane (`s64 → 8 × s8` produced the illegal `MOVE32 <GPR>, $d0`). Added the s64 → 8 × s8 and GPR32 → 2×s16 / 4×s8 cases (`MOVE32_DR_L/H` + shift/mask) and made the fallback **fail closed**. Test: `CodeGen/Haydn/gisel/unmerge-v8i8-lanes.ll` |
> | **CB-137 under-aligned vector mem** | 64-bit SIMD load/store was type-only legal, so an `align 1` `<8 x i8>` became two 4-byte `D_SW_L/D_SW_H` halves → `MEMORY_FAULT`. Now bitcast to s64 for align < 32. **gcc-c-torture -O3: 1388 PASS / 29 FAIL → 1417 PASS / 0 FAIL**; ctest 216/219 → 218/219; unmodified Dhrystone runs (`dhry_oracle` exit 7). Test: `CodeGen/Haydn/gisel/legalizer-underaligned-vector-mem.ll` |
> | **CB-134 compile hang** | All 5 (`20001111-1`, `20170401-1`, `20180921-1`, `950809-1`, `960312-1`) compile in 38–66 ms and **PASS** end-to-end; removed from BundleSim `HAYDN_COMPILE_HANG_SKIP`. Two of them (`20170401-1`, `20180921-1`) carry `store i1` and are attributable to CB-134a; the other three were already fixed by earlier commits |
> | **CB-134a sub-byte store hang** | `store i1` livelocked the GISel legalizer (unbounded memory, no diagnostic). `HaydnLegalizerInfo` value/mem-mismatch `customIf` now requires whole-byte mem (`>= 8`); sub-byte mem goes to `lowerIfMemSizeNotByteSizePow2()`. Test: `CodeGen/Haydn/gisel/legalizer-subbyte-store.ll`. Was blocking the BundleSim BSP (`bsp/plat/time_llvm_libc.c`) |
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

Not re-tested after **CB-134a** (torture sources are not on every host). One
concrete hang of this class **is** fixed — see below.

#### CB-134a — `store i1` legalizer livelock (FIXED)

```llvm
; llc -O0 -mtriple=haydn-unknown-elf  → never returns, RSS grows without bound
define void @s1(ptr %p) { store i1 true, ptr %p  ret void }
```

`G_STORE s1 :: (store s1)` cycled forever inside the Legalizer:

1. `.minScalar(0, S8)` widened the value → `G_STORE s8 :: (store s1)`
2. the value/mem-mismatch `.customIf` matched (`isPowerOf2_32(1)` is true)
3. the custom handler truncated back to `s1` **and rewrote the MMO to `s1`**
4. → identical query again, forever (new vregs + MMOs each round, no
   diagnostic, no abort)

Fix: `customIf` now also requires
`MMODescrs[0].MemoryTy.getSizeInBits() >= 8`, so sub-byte mem falls through to
`lowerIfMemSizeNotByteSizePow2()` and becomes a zero-extended byte store.

Not synthetic: InstCombine turns a plain `bool` flag store into `store i1` at
`-O2`, which is how BundleSim's `bsp/plat/time_llvm_libc.c` hit it (the BSP
could not be built at all). Regression: `CodeGen/Haydn/gisel/legalizer-subbyte-store.ll`.

Byte-identical `libc.a` / `libm.a` before vs after a clean rebuild — the guard
only affects shapes that previously hung.

### B2. ~~Under-aligned 64-bit vector mem (CB-137)~~ FIXED

Guest died with `MEMORY_FAULT` / "guest access is misaligned" inside
llvm-libc: 27 of the 29 gcc-c-torture failures were in `memcpy` (11),
`memset` (9), `printf_core` (4), `strcpy` (2) and `strncpy` (1).

**Root cause.** `HaydnLegalizerInfo` gave 64-bit SIMD mem *type-only*
legality:

```cpp
.legalFor({{V2I32, P0}, {V4I16, P0}, {V8I8, P0}})   // no MemDesc => any align
```

while every scalar row carried an explicit `AlignInBits`. ISel splits an
under-8-aligned 64-bit access into `D_SW_L`/`D_SW_H` (or `LD32`) halves, which
are 4-byte ops, so an `align 1` vector access became two 4-byte accesses:

| declared align | before | after |
|----------------|--------|-------|
| 1 | `d_sw_l` + `d_sw_h` | `8 x st8` |
| 2 | `d_sw_l` + `d_sw_h` | `4 x st16` |
| 4 | `d_sw_l` + `d_sw_h` | unchanged (correct) |
| 8 | `st64` | unchanged |

**Why libc.** llvm-libc has no Haydn `memcpy`/`memset`, so
`inline_memcpy.h` falls to the `#else` branch → `generic/byte_per_byte.h`, a
byte-at-a-time loop. The loop vectorizer turns that into honest
`<8 x i8> … align 1` mem ops; the backend then treated them as legal. An 8-line
C byte-copy loop reproduces it without libc.

**Fix.** `bitcastIf` under-aligned 64-bit vector mem to `s64` so it takes the
scalar under-aligned lower path. Deliberately **not** `.scalarize`: scalarizing
a DR-resident vector store drops the per-lane extract and writes lane 0 into
every byte (verified — `vdr.c` returned `0xFF` with a scalarize-based first
attempt, `0` with the bitcast).

**Evidence.** gcc-c-torture `-O3`: **1388 PASS / 29 FAIL → 1417 PASS / 0 FAIL**
(1514 discovered, 97 upstream-skipped). BundleSim ctest 216/219 → 218/219
(`yarpgen_seed1` + `yarpgen_seed67` fixed). Unmodified Dhrystone now runs to
completion (`dhry_oracle.c` exit 7; `dhry_1.c` prints `Int_Glob: 9`,
`Ch_1_Glob: A`, `Ptr_Glob->Int_Comp: 5`) — it previously faulted in `strcpy`
before the first line of output.

The earlier "format-string alignment" description of this bug was a symptom,
not the cause: `printf_main`'s format copy is one of the vectorized byte loops.

### B2b. ~~Vector lane unmerge miscompile (CB-138)~~ FIXED

Wrong result (no fault, no diagnostic) with **aligned** vector mem, so it was
independent of CB-137 and reproduced identically with and without that fix.

**Root cause.** `HaydnInstructionSelector`'s `G_UNMERGE_VALUES` case handled
`s64 → 2 × s32` (`MOVE32_DR_L`/`MOVE32_DR_H`) and `s64 → 4 × s16`, then ended
in a **fail-open** fallback:

```cpp
// Generic fallback: emit COPYs from source to each def.
for (unsigned Idx = 0; Idx < NumDefs; ++Idx)
  MIB.buildCopy(Dst, Src);          // whole source into EVERY def
```

So any shape without an explicit case became "every lane = lane 0". For
`s64 → 8 × s8` it also emitted `MOVE32 <GPR>, $d0`, an illegal cross-bank move
that `-verify-machineinstrs` rejects outright — invisible by default because
the machine verifier is not in the pipeline.

Two producers reach that shape: `extractelement <8 x i8>` (v8i8 was missing
from the `G_EXTRACT_VECTOR_ELT` `customFor` list) and vector `icmp`
scalarization, which builds the unmerge directly without any extract — that is
how a `memcmp`-style byte compare returned "not equal" for identical buffers.

**Fix.**
* selector: added `s64 → 8 × s8` and `GPR32 → 2 × s16 / 4 × s8`
  (32-bit residual packs live in one GPR, so shift+mask with no half move)
* selector: fallback is now **fail closed** — a missing unmerge shape is a
  selection error, not wrong code
* legalizer: `{S8, V8I8}` added to `G_EXTRACT_VECTOR_ELT` `customFor`, and the
  custom expansion always goes through `s64 → 2 × s32` + shift/trunc instead of
  unmerging straight to `NumElems × ElemTy`

Making the fallback fail closed immediately exposed a second silent
miscompile: `legalize-v2i16-scalarize.ll` was passing while relying on it
(`<2 x s16>` in one GPR32, lane 0 accidentally right, lane 1 wrong). That test
only CHECKs `jalr_w`, so it never noticed.

Repro that used to fail (`vpre.c`, host 0 / guest 1):

```c
typedef unsigned char v8 __attribute__((vector_size(8)));
static unsigned char buf[32] __attribute__((aligned(8)));
__attribute__((noinline)) static void st_a(unsigned char *p, v8 v) { *(v8 *)p = v; }
int main(void) {
    v8 v = {1,2,3,4,5,6,7,8};
    int fails = 0;
    for (int i = 0; i < 32; i++) buf[i] = 0xEE;
    st_a(buf, v);
    for (int i = 0; i < 8; i++) if (buf[i] != (unsigned char)(i+1)) fails |= 1;
    for (int i = 0; i < 32; i++) buf[i] = 0xEE;
    st_a(buf + 8, v);
    for (int i = 0; i < 8; i++) if (buf[8+i] != (unsigned char)(i+1)) fails |= 2;
    if (buf[0] != 0xEE || buf[16] != 0xEE) fails |= 4;
    return fails;                       /* host 0, guest 1 */
}
```

Each store and each check passed in isolation; only the combined shape failed,
because only there did the checks get vectorized into `icmp ne <8 x i8>` and
reduced through the broken unmerge. Now host and guest both return 0.

gcc-c-torture never covered this (0 FAIL before and after), which is worth
noting: a silent lane miscompile can sit under a fully green torture suite.
`-verify-machineinstrs` would have caught the illegal move — it is not part of
the default pipeline.

### B2c. ~~Exposed-pipeline latency + hwloop body (CB-139 / CB-140)~~ FIXED

Haydn has **no interlock**. The spec rule is

> "If instruction A at bundle t has Data_Latency = N, no instruction in bundles
>  t+1 through t+N-1 may read A's destination register."

Loads, `CSRR` and the MAC family are `Data_Latency = 2`. Nothing enforced it:

1. `HaydnSubtarget::adjustSchedDependency` **deliberately** softened load→use
   to latency 1 for non-accumulator consumers, to help SMS reach a lower II.
2. The 14 `_S2` forms of the LS formats declared `Itinerary = Slot2_ALU`
   (latency **1**) — the comment "single-slot, mirrors encoder commit" was
   about slot resources, but it set the latency too. S0 used `Slot0_LS` and S1
   `Slot1_LD`, both latency 2; only S2 was wrong.
3. At `-O0` functions are `optnone`, so `PostMachineScheduler` **and**
   `HaydnFinalizeBundle` both `skipFunction` — nothing schedules at all.

BundleSim can never catch this: it is a functional bundle simulator with no
timing model (its own staff-readiness review records "no timing or
memory-performance model exists" as P0), so a violating program returns the
right answer in simulation and the wrong one on hardware.

| | before | after |
|---|---|---|
| Dhrystone -O0 / -O1 / -O2 / -O3 | 90 / — / 3 / — | **0 / 0 / 0 / 0** |
| 50-file corpus (BSP + yarpgen + Dhrystone) | 90+ | **0** |

**Fix:** new `Slot2_LS` itinerary (SLOT2 resource, latency 2) plus a new
`HaydnLatencyStalls` pass at the head of `addPreEmitPass`, so the two
`BranchRelaxation` runs and `HaydnFixupHwLoops` absorb the size growth. It runs
at every optimization level.

The soften in (1) is **kept** — it is now safe because the pass is the backstop,
and feeding the true latency to the scheduler shifts 12 golden schedules
(hwloop / SMS / II contracts) whose intent is not recoverable from CHECK lines.
Do not remove the pass while the soften stays; the comment says so at the site.

Cost: +13% bundles at -O0, +1% at -O2 (Dhrystone 872967 → 976058).

Subtlety worth keeping: a **post/pre-increment load writes two destinations**,
the loaded value and the base writeback. `s_lbu_post_imm r5, r1, 1` followed by
a read of `r1` is a violation. The itinerary's `OperandCycles` has one entry
(operand 0), so the writeback fell back to latency 1. The spec gives ONE
`Data_Latency` per instruction and lists both `rt` and `rs` as write ports, so
the pass takes the class maximum for any def without an explicit cycle. If
hardware forwards the address writeback earlier, encode that as a real
per-operand cycle rather than assuming it.

**CB-140**, found while validating the above: `MinBodyBundles` was 0 with the
note "deprecated as a legality floor", but the spec says "Loop Body: It must
contain at least 3 instruction bundles". Tiny ZOL bodies (1–2 bundles) were
reaching the assembler. `HaydnFixupHwLoops` now pads short bodies with NOPs
before the inclusive END, next to the existing t-3 setup-gap padding. A survey
of the 50-file corpus found 20 hardware loops, all already compliant on
body ≥ 3 / setup ≥ 3 / END > BEGIN — only synthetic lit loops were short.

Still unchecked from the spec's HW Loop section: nested-loop boundary overlap
and the flow-control restrictions (no branch into or out of an active loop).

### B2d. OPEN compiler — vector predicate legalization (CB-130 residual)

`bundlesim_reg_cb44_o2_stale_cond_max_reduce` is the only remaining ctest
failure. It had **two** layers.

**Fixed:** `G_ABS` had no vector rule at all (only `legalFor({s32, s64})` plus
min/maxScalar), so an SLP-formed `<2 x s32> = G_ABS` aborted with "unable to
legalize instruction". The rule now ends in `.lower()` like RISCV's, and vector
abs lowers to the branchless `x2sra32` / `x2add32` / `xor64` sequence. Test:
`CodeGen/Haydn/gisel/legalize-vector-abs.ll`. Note `.scalarize(0)` is NOT usable
here — it reaches `fewerElementsVectorMerge`, which asserts for a unary op.

**Still open:** with the abs gap closed, the same test now stops one step later
on a vector **predicate**:

```
Legalizing: %124:_(<2 x s1>) = G_BUILD_VECTOR %211:_(s1), %212:_(s1)
LegalizerHelper.cpp:5246: Assertion `DstTy.isVector() && NarrowTy.isVector() &&
  "Expected vector types"' failed.
```

The trigger is `G_BUILD_VECTOR`'s `.clampMaxNumElements(0, S1, 1)`: LLT
normalizes a 1-element vector to a **scalar**, so `NarrowTy` is scalar `s1` and
`fewerElementsVectorMerge` asserts. The `<2 x s1>` itself is an artifact — a
vector `icmp` whose lanes were already scalarized, rebuilt only to feed
`G_ZEXT <2 x s32>` (which does have `.scalarize(0)`). Ideally the artifact
combiner folds `unmerge(build_vector)` and the build dies, but it is legalized
first.

**Next step — pick a representation for `<N x s1>` first.** Haydn has no mask
register, so one of:

| Option | Cost |
|--------|------|
| Widen every `<N x s1>` to `<N x s32>` (one GPR per lane) | simple and uniform; burns GPRs, and GPR has only 2 write ports per bundle |
| Pack lanes into one GPR bitmask | compact; every extract/insert becomes shift+mask, and vector `select` gets ugly |
| Never form `<N x s1>` — force the vector `icmp`/`select`/`zext` chain to scalarize together | no new representation, but needs the whole chain gated consistently or artifacts reappear |

Reproduce with:
`benchmarks/compiler_bugs/cb44_o2_stale_cond_max_reduce.c` at `-O2`, or
`llc -O2 -mtriple=haydn-unknown-elf -mcpu=haydn -debug-only=legalizer` on its
IR and read the last `Legalizing:` line before the assertion.

### B3. OPEN compiler — `haydn.h` unguarded DSP bodies (CB-136)

```console
$ clang --target=haydn-unknown-elf -c t.c   # t.c: #include <haydn.h>
haydn.h:165:10: error: '__builtin_haydn_brev32' needs target feature bit-reversed
haydn.h:3487:10: error: '__builtin_haydn_x2abs32' needs target feature simd
… (159 errors total with -ferror-limit=0)
```

Default Haydn features are `+agu,+hwloop,-bit-reversed,-circular-buffer,-simd`,
but the public API header defines every DSP wrapper body unconditionally, so the
include fails before the user writes any code. Needs `#if`/`__attribute__((target))`
guards (or feature-gated sections).

**No longer blocking:** BundleSim `run_c` now compiles with **`-mcpu=haydn`**,
which turns the features on, so `intrin_*`, `mac_mul*64_all`,
`ls_brev_addbrba`, `ls_move_dr` and `cbr_wrap_csr` pass. That is a consumer-side
workaround — the header is unchanged and a bare
`clang --target=haydn-unknown-elf -c` on a TU that includes `haydn.h` still
emits `needs target feature`. Demoted to P3 (ergonomics).

**Fix plan.** `haydn.h` is generated — do not edit it. The generator is
`clang/utils/TableGen/HaydnIntrinEmitter.cpp` (`-gen-haydn-intrin-header` from
`clang/include/clang/Basic/BuiltinsHaydn.td`). The per-op feature expression is
**already parsed** into `Entry::Features` (member at ~line 88, filled at
~line 356) and simply not used when emitting the wrapper. Emit
`__attribute__((target("<features>")))` on each wrapper, the way x86's
`immintrin.h` does with `__DEFAULT_FN_ATTRS`, converting the TableGen
comma=AND / pipe=OR expression to a target-attribute string.

The mechanical part is the reason this was not rushed: `emitFn` has **34 call
sites** and its last parameter (`Doc`) is defaulted, so adding a `Features`
parameter touches every positional call. Add it *before* `Doc` and update all
34, or thread an `Entry &` through instead of loose strings. Verify with

```console
$ printf '#include <haydn.h>\nint main(void){return 0;}\n' > t.c
$ clang --target=haydn-unknown-elf -O2 -ffreestanding -c t.c -o /dev/null
```

which must produce no `needs target feature` diagnostics with **no** `-mcpu`.

### B4. OPEN — unit model migration (CB-143, supersedes CB-141)

CB-141 recorded a disagreement between LLVM's slot model and the ISA database
and asked for an owner's ruling. No ruling is needed: the database it quoted has
been **retired**. `VLIW_Engine_Database_20260701/` is gone, and the current
`VLIW_Engine_Compiler_Constraints.md` describes a different machine.

**Old model** — each slot has a fixed set of instructions it can execute.
**New model** — slots carry no capability. Every instruction declares the
hardware **units** it can issue on; an entry may join a bundle whenever a unit
it needs is still free, and it records which unit it took.

> § Constraints: "Each entry in a bundle may be assigned to any available unit;
> unit assignment is not bound to a fixed slot." … "Each entry in a bundle maps
> to exactly one unit. Multiple entries in the same bundle must not map to the
> same unit."

Seven shared units: `LOADSTORE0`, `LOAD1`, `ALU0`, `ALU1`, `ALU2`, `MAC0`,
`MAC1`. `Available` per instruction (`instruction_type_index.json`, 683 entries):

| Count | Available |
|------:|-----------|
| 357 | `[MAC0, MAC1]` |
| 195 | `[ALU0, ALU1, ALU2]` |
| 56 | `[LOADSTORE0, LOAD1]` |
| 53 | `[LOADSTORE0]` |
| 16 | `[ALU0]` — all control flow + `SET_HWLOOP*` + `WFI` |
| 6 | `[ALU1, ALU2]` — `LOG2`/`EXP2`/`RECIP`/`SQRT`/`SIN_COS`/`ARCTAN` |

CB-141's two questions are answered by that table: `ADD32`/`ADDI32`/`XOR32`/
`MOVE32` are `[ALU0, ALU1, ALU2]`, so `Slot012_ALU` was right; stores are
`LOADSTORE0` and there is one of them, so the rule is **at most one store per
bundle**, not "stores live in Slot0". Of the three measurements CB-141 cited,
only the **22 bundles holding two stores** remain violations.

#### Encoding: 96-bit format E

`format_e_bit_layout_v2.json` carries the unit model in the encoding —
`bundle_bits = 96` (not 128), payload `bit[95:6]`, budget 90b, header
`bit[2:0] = 0b111` + `bit[3]` entry_num (0 → 2 entries, 1 → 3). Each entry has a
2-bit `mapping` field naming its unit:

| Form | entry0 | entry1 | entry2 |
|------|--------|--------|--------|
| 2-entry | 45b · ALU0/MAC0/**LOADSTORE0** | 41b · ALU1/MAC1/**LOAD1** | — |
| 3-entry | 31b · MAC0/ALU2/ALU0/**LOADSTORE0** | 31b · MAC0/ALU1/ALU0/**LOAD1** | 27b · MAC1/ALU2/ALU0/**LOAD1** |

Two consequences the slot model never had. `LOADSTORE0` is encodable **only in
entry0**, so store placement is still positional — but as an encoding property,
not a capability one. And **entry widths differ**: an `ALU0/I32` form needs 45b,
which only the 2-entry entry0 provides, so a wide immediate forces the bundle
down to two entries.

#### What LLVM does not model yet

`HaydnSchedule.td` still defines `SLOT0`/`SLOT1`/`SLOT2` FuncUnits with
`Slot0_ALU`/`Slot1_LD`/`Slot2_LS`-style itinerary classes; 51 files under
`lib/Target/Haydn` mention `Bundle128`/16-byte/128-bit-only. Beyond renaming,
the packer gains constraints it has never had:

1. bundle legality is a **matching** problem — instruction `Available` × entry
   encodable set × all-distinct units
2. at most one branch per bundle, and it consumes `ALU0`
3. per-entry immediate width limits which entry an instruction may occupy
4. per-bundle register port budget — GPR 4r/2w, DR 7r/3w, AR 2r/2w, SFR 2r/1w
5. a store and a load in one bundle must be provably non-overlapping, else the
   hardware raises an exception

**Stage 0 landed** (BundleSim side): `isa/database/generate_unit_model.py` reads
both JSONs into `generated/unit_model_generated.inc`, exposed by
`include/bundlesim/unit_model.h`, with `bundlesim_new_unit_model` pinning the
invariants above and `bundlesim_new_unit_model_generated` gating freshness.
Nothing consumes it on the execute path yet — the live path is still the
slot-shaped catalog.

**Open gap**: the document lists five bundle sizes (96/64/48/32/16-bit) but only
format E's layout exists on this host, and format E encodes 2 or 3 entries only.
A single-op bundle must be a 2-entry form with a NOP; NOP is encodable in every
entry position and unit, so padding never blocks.

Related, also unverified against the spec's HW Loop section: nested-loop
boundary overlap, and the flow-control rule that no branch/jump/call may enter
or leave an active hardware loop.

### B5. Why these stayed hidden — two observability gaps

Worth fixing before hunting more Haydn bugs, because both let wrong code pass a
fully green suite.

**1. `-verify-machineinstrs` is not in any gate.** CB-138 emitted
`MOVE32 <GPR>, $d0`, an illegal cross-bank move. The machine verifier rejects it
on sight, but it is opt-in, so 1514 green torture tests and 219 green ctest
cases said nothing. Cheapest improvement available: run the Haydn lit subset
with `-verify-machineinstrs` in CI. It would have caught CB-138 immediately, and
it is how the second silent miscompile (`legalize-v2i16-scalarize.ll`) surfaced.

**2. BundleSim has no timing model, by design.** Its own staff-readiness review
records as P0: *"'Cycles' are committed bundles; no timing or memory-performance
model exists"* and *"the instruction catalog has no latency, initiation
interval, bypass..."*. Haydn's pipeline is **exposed** (no interlock), so every
`Data_Latency` violation (CB-139: 90 sites in Dhrystone -O0) produced the right
answer in simulation and would read stale registers on hardware. Nothing in the
BundleSim gate can detect this class; it has to be checked structurally on the
generated code. The scanner used for CB-139 walks bundles in order, tracks
`Data_Latency` from the itinerary, and flags a read inside a producer's window —
worth keeping as a real tool rather than a throwaway script.

Same shape applies to slot legality (B4): BundleSim deliberately abstains
(`--enforce-slots` is debug-only), so nothing checks it either.

### C. ~~Compiler-rt softfloat (CB-135)~~ FIXED

**Linked:** `libclang_rt.builtins-haydn.a` + `libm.a` when present.  
**Not libm** — int↔FP helpers live in compiler-rt / BSP RT.

| Test | Was | Now |
|------|-----|-----|
| `conversion.c` | LINK missing `__floatundi*` / `__floatdi*` | **PASS** (di helpers in BSP RT) |
| `930622-2.c` | LINK `__floatdidf` / `__fixdfdi` | **PASS** |
| `pr49218.c` | LINK `__fixsfdi` | **PASS** |
| `complex-5.c` | LINK `__divsc3` | **PASS** @ -O3 full lit (GUEST_EXIT 0, bundles=2842) |

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
