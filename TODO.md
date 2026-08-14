# Haydn backend TODO — codegen quality

Non-correctness items. Real bugs go in
[`OPEN-COMPILER-BUGS.md`](OPEN-COMPILER-BUGS.md); this file is for things that
produce *valid but worse* code.

---

## T1 — Constant materialized into a register when an immediate form exists

**Status: DONE.** Fixed by adding the missing RI20/RI5 Pats to `HaydnGISel.td`
plus the `HaydnSImm`/`HaydnUImm` `ImmLeaf` `return` fix described below.
Measured on Dhrystone `-O2`: 431 → 421 bundles (-2.3%), constant
materializations 54 → 46, single-op materialization bundles 34 → 29, and the
four wasted sites down to one (see **T1b** for that residual). `-O0`
1311 → 1306. Gate A green at 570 pass / 0 fail.

The original analysis is kept below because the boundary reasoning (which
immediate class per opcode, and which materializations are *mandatory*) is what
makes the fix safe.

---

## T1 (original analysis) — Constant materialized when an immediate form exists

**Impact:** small and measured (4 bundles out of 431 in
Dhrystone `-O2`) · **Where:** ISel / post-ISel combine

### Symptom

A small constant is materialized with `addi32_w rX, r0, imm` and then consumed
by the register-register form of an ALU op, in a bundle where the immediate form
of that op would have done the whole thing:

```asm
{ addi32_w r1, r0, -1; nop; nop }        ; bundle N   — only op in the bundle
{ nop; nop; add32 r10, r10, r1 }         ; bundle N+1
```

should be one bundle:

```asm
{ ...; nop; addi32 r10, r10, -1 }
```

### Confirmed sites (Dhrystone `-O2`)

Reproduce the assembly with the same flags `run_c` uses:

```bash
export PATH=~/haydn/llvm-project/build/bin:$PATH
SY=~/haydn/llvm-project/build/sysroot/haydn-unknown-elf
cd ~/haydn/simulator
clang --target=haydn-unknown-elf -mcpu=haydn -std=c11 -O2 -ffreestanding \
      -fno-builtin -fno-stack-protector -ffunction-sections -fdata-sections \
      -fomit-frame-pointer -isystem $SY/include -I build/bsp-stage/include \
      -S benchmarks/dhrystone_haydn/dhry_1.c -o dhry_1.O2.s
```

| Function | asm line | current | should be |
|----------|---------:|---------|-----------|
| `main`   | 127 | `addi32_w r1, r0, -1` + `add32 r10, r10, r1` | `addi32 r10, r10, -1` |
| `main`   | 169 | `addi32_w r4, r0, 12` + `add32 r2, r2, r4`    | `addi32 r2, r2, 12` |
| `Proc_4` | 298 | `addi32_w r5, r0, 1` + `and32 r1, r1, r5`     | `andi32 r1, r1, 1` |
| `Proc_6` | 438 | `addi32_w r5, r0, 255` + `and32 r5, r1, r5`   | `andi32 r5, r1, 255` |

In all four the materialization is the **only** op in its bundle, so folding it
removes a whole bundle each.

### Do NOT "fix" these — they are not the same thing

Most constant materialization in this file is **mandatory**, because the ISA has
no immediate form for the consumer. Verified against
`VLIW_Engine_Database_20260701` `instruction_type_index.json`:

| Immediate form | ISA | Haydn backend |
|----------------|-----|---------------|
| `ADDI32`, `SUBI32`, `ANDI32`, `ORI32`, `XORI32`, `SLLI32`, `SRLI32`, `SRAI32` | exists | defined |
| `SEQI32`, `SLTI32`, `SLTUI32` | **does not exist** | — |

So compare-against-constant *must* go through a register:

```asm
{ addi32_w r4, r0, 65; ... }
{ ...; sltu32 r4, r9, r4 }       ; SLTU32 is RR-only — this is correct
```

`Func_3` (`return Enum_Par_Val == Ident_3`) looks like the same waste but is
not: `SEQ32` is RR-only too.

### Also not waste: materialization that fills an idle slot

`Proc_7` (`*ref = val2 + (val1 + 2)`) does:

```asm
{ addi32_w r4, r0, 2; nop; add32 r1, r1, r2 }
{ nop; nop; add32 r1, r1, r4 }
```

This looks foldable to `addi32 r1, r1, 2`, but the dependency chain
`(r1+r2)` → `+2` is two deep either way, so both shapes take **two bundles**.
The materialization rides along in an otherwise-empty slot0 and costs zero
cycles — only one instruction and one register. Do not count these when
measuring.

### Measured scale

Dhrystone `-O2`, 431 bundles total:

| | count |
|---|---:|
| `addi32* rX, r0, imm` materializations | 54 |
| … of which occupy a bundle alone | 34 |
| … **and** whose consumer has an immediate form (real waste) | **4** |

So the fix is worth ~1% of bundles on Dhrystone. Do it for the codegen-quality
reason, not because it is hot.

### Root cause (diagnosed) — there is no immediate-folding path at all

This is not a pattern losing to the RR form. **No pattern exists.**

Immediate-form opcodes actually emitted in `dhry_1.O2.s`:

| opcode | uses | why it appears |
|--------|-----:|----------------|
| `subi32` | 15 | hand-written: stack adjust / spill sequence |
| `addi32` | 7 | hand-written: frame index, global address, `G_PTR_ADD` |
| `xori32` | 5 | hand-written: logical-not idiom only |
| `andi32`, `ori32`, `slli32`, `srli32`, `srai32` | **0** | no emission path whatsoever |

Three findings that together explain it:

1. **Every RI20 def has an empty pattern list.** All 15 immediate-form defs in
   `HaydnFormatsALU32.td` (`ANDI32_S*`, `ORI32_S*`, `XORI32_S*`, `ADDI32_S*`,
   `SUBI32_S*`) end in `[]>;` — zero have a non-empty pattern. The logical
   identities in `HaydnInstrInfo.td:311-322` (`ANDI32`, `ORI32`, `XORI32`) are
   **also** `[]`.
2. **The selector has no case for these opcodes.** `HaydnInstructionSelector.cpp`
   has no `case TargetOpcode::G_AND` / `G_OR` / `G_XOR` / `G_ADD` (only `G_SHL`,
   at line 1556). So they fall through to the TableGen-generated matcher — which
   has nothing to match, per (1).
3. **`HaydnGISel.td` contains no immediate patterns at all.**

So the only reason any `*I32` form ever appears is a hand-written special case in
the selector: `XORI32` exclusively as the logical-not idiom
(`HaydnInstructionSelector.cpp:806`, `:904`, `:937`), `ADDI32_W` for
address materialization, `SUBI32` for frame setup. The `xori32 r1, r1, 1` at
`dhry_1.O2.s:130` — immediately after one of the four failing sites — is that
idiom, **not** evidence that a general fold works.

Note the comment at `HaydnInstrInfo.td:304-310` is misleading: it says the
`uimm20` `ImmLeaf` is "the ISel contract that matches peers' ZEXT imm20", but
with an empty pattern list nothing is generated, so there is no contract in
force. Worth correcting that comment either way.

### Suggested approach

Add the missing selection, so the RR form is never chosen when one operand is a
fitting constant: `G_ADD` / `G_SUB` / `G_AND` / `G_OR` / `G_XOR` / `G_SHL` /
`G_LSHR` / `G_ASHR` with a `G_CONSTANT` operand should select the `*I32` form.

Two options:

* **Patterns** — give the logical defs in `HaydnInstrInfo.td` real pattern lists
  (e.g. `[(set GPR32:$rd, (and GPR32:$rs, uimm20:$imm))]`) and let the generated
  matcher do it. Cleanest, and matches the intent the existing comment already
  claims. Verify the post-RA `setDesc` → `*_S*` slot-member path still fires.
* **Selector cases** — follow the shape of the existing hand-written code. More
  code, but consistent with how `G_SHL` is already handled.

Prefer patterns unless the slot-member rewrite forces otherwise.

Watch out for:

* **signedness, not width** — all five are RI20 (20-bit), but arithmetic and
  logical differ:

  | op | imm type |
  |----|----------|
  | `ADDI32`, `SUBI32` | `simm20` (signed, `HaydnSImm<20>`) |
  | `ANDI32`, `ORI32`, `XORI32` | `uimm20` (**zero-extended**, `HaydnUImm<20>`) |

  So the logical forms cannot take a negative constant. All four sites here are
  fine (`-1` and `12` go to `addi32`/`simm20`; `1` and `255` to
  `andi32`/`uimm20`), but a general fold must range-check per opcode class, and
  must not fold e.g. `and x, -1` into `andi32`.
* the constant having **more than one use** — folding then duplicates it, which
  may be worse; only fold single-use
* `r0` is the soft-zero register, so `addi32_w rX, r0, imm` is also the canonical
  "load small constant" idiom. Do not break the case where that is the intent

### Verification

Correctness gate is unchanged (this is a quality change, but it touches ISel):

```bash
# A: Haydn lit
build/bin/llvm-lit -s -j16 llvm/test/CodeGen/Haydn llvm/test/MC/Haydn
# B: rebuild libc + install sysroot next to that clang
# C: BundleSim ctest + gcc-c-torture
```

Then re-measure with the scanner logic above and confirm the 4 sites are gone
and the `-O2` bundle count drops from 431 to ~427.

---

## T1b — Hand-written selector idioms still materialize their constants

**Status: DONE.** One `emitALUImm` helper (companion to `emitInvert01`) plus nine
call sites in `GISel/HaydnInstructionSelector.cpp`; the `SrcBits <= 16` mask
cutoff is now `<= 20` (the real `ANDI32` `uimm20` limit). Dhrystone `-O2`
`Proc_4` 16 → 14 bundles — the wasted site below is gone — and total ops
487 → 480; whole-file `-O2` bundles stay 421 because `main` re-packed 226 → 228
bundles for 3 fewer ops (scheduling, spills unchanged at 11/12). `-O0`
1173 → 1171 bundles. New test `gisel/ext-imm-idioms.mir`; six goldens moved.

Gates: Haydn lit 571 pass / 0 fail (588 discovered, +1 new test), clang 33/33,
lld 24/24, `HaydnTests` 244/244. llvm-libc rebuilt from scratch with this clang
and the sysroot reinstalled (`libc.a` 7,396,836 → 7,375,892 bytes). BundleSim
ctest 218/219 — the one failure is the already-documented CB-130 residual
(`bundlesim_reg_cb44_o2_stale_cond_max_reduce`), identical to the pre-change
baseline. gcc-c-torture -O3: **1417 PASS / 0 FAIL** of 1514 (97 unsupported);
`920501-6.c` false-times-out at `-j16` and passes in 76 s at `-j1`.

The `<= 20` relaxation was verified beyond lit: `andi32 rX, rY, 1048575`
round-trips through `llvm-mc` → `llvm-objdump`, and a guest run confirms
BundleSim zero-extends the imm20 (`0xDEADBEEF & 0xFFFFF` → `0x000dbeef`), so a
mask with bit 19 set is safe.

The original analysis is kept below. **One correction:** the last row of the site
table was wrong — lines 792/815/851/886/918/963 are the `AND32 hi_eq, lo_eq` of
the s64 icmp lowering, i.e. register-register ANDs with no constant at all, and
nothing to fold. The real remaining sites were the two `G_SEXT` shift branches
(`LOADI32 32-SrcBits` + `SLL32`/`SRA32`, not listed originally) and the
`G_TRUNC`-to-s1 bit-0 isolation.

---

## T1b (original analysis) — Hand-written selector idioms materialize constants

**Impact:** 1 remaining wasted bundle in Dhrystone `-O2`, but
the idiom (`x & 1` bool normalisation, sub-32 zero-extend masks) is very common
· **Where:** `GISel/HaydnInstructionSelector.cpp`

T1 fixed the *pattern* path. It does not reach AND32/SLL32/SRL32 that the C++
selector builds by hand, because those never go through `selectImpl`. The
residual Dhrystone site is exactly this:

```asm
; Proc_4  --  int Bool_Loc = Ch_Loc == Ch_1_Glob;
{ addi32_w r5, r0, 1; nop; nop }
{ nop; nop; and32 r1, r1, r5 }        ; should be andi32 r1, r1, 1
```

The same `& 1` in `cb14-cond-opt-fold-liveout.ll` *does* fold, because there it
comes from a `G_AND` and hits the new Pat. Only the hand-built path is left.

Sites that emit `LOADI32 <reg>, imm` followed by an RR op:

| line(s) | idiom | mask / amount |
|---------|-------|---------------|
| 1920-1924 | i1 → i8 zero-extend | `1` |
| 1934-1942 | sub-32 → i32 zext, `SrcBits <= 16` | `(1<<SrcBits)-1` |
| 1943-1959 | sub-32 → i32 zext, wider | shift amount `32-SrcBits` |
| 1967-1977 | sub-32 → i64 zext, `SrcBits <= 16` | `(1<<SrcBits)-1` |
| 1978-1989 | sub-32 → i64 zext, wider | shift amount `32-SrcBits` |
| 792, 815, 851, 886, 918, 963 | icmp / select bool normalisation | `1` |

Each can become a single `ANDI32` / `SLLI32` / `SRLI32`. Constraints:

* `ANDI32` is `uimm20`, so masks for `SrcBits <= 20` fit directly — this is
  strictly better than the current `SrcBits <= 16` cutoff, and the comment at
  1927-1930 ("we never need a wide immediate") can be relaxed accordingly
* `SrcBits` of 24 gives mask `16777215`, which does **not** fit `uimm20`; that
  case must keep the shift pair
* shift amounts are `uimm5`, and `32-SrcBits` is always in 1..31, so the
  `SLLI32`/`SRLI32` conversion is unconditional

Deliberately **not** bundled into T1: T1 already re-baselined 43 lit goldens, and
mixing a second codegen change in would make the diff unreviewable and hard to
bisect. Land separately, re-run the same gates.

### Not in scope (checked)

`G_PTRMASK` (selector ~2339) also does `AND32 ptr, mask` with the mask in a
register, but its mask arrives as an already-selected vreg and the va_start
alignment masks are negative (`-8`), which `uimm20` cannot hold. Leave on the RR
path.

---

## T2 — Low bundle density overall

**Status:** open, needs a decision before any work · **Impact:** large but
partly blocked by CB-141

Dhrystone `-O2` packs **1.15 ops/bundle** on a 3-issue machine — 38.3% slot
occupancy, 77% of bundles holding a single op, and only 4 bundles filling all
three slots. `-O0` is 0.88 ops/bundle with 12% completely empty bundles and
**never uses slot1 or slot2 at all**.

| | bundles | ops | ops/bundle | slot occupancy | slot0 / slot1 / slot2 |
|---|---:|---:|---:|---:|---|
| `-O0` | 1311 | 1157 | 0.88 | 29.4% | 1157 / 0 / 0 |
| `-O2` | 431 | 495 | 1.15 | 38.3% | 293 / 63 / 139 |

Per-bundle op counts at `-O2`: 20 empty (5%), 331 single (77%), 76 double (18%),
4 full (1%).

Dhrystone is pointer-chasing, control-heavy, low-ILP code, so a low number is
expected — but not this low.

> When counting bundles, note that bundle lines can carry a trailing comment
> (`{ st32 r11, fp, -24 }          // 4-byte Folded Spill`). A regex anchored on
> `\}\s*$` silently drops 270 of the 1311 `-O0` bundles. Match
> `^\s*\{(.*?)\}(\s*//.*)?\s*$`.

**Blocked on CB-141.** Much of the packing that *does* happen puts 32-bit GPR
ALU ops in slot1/slot2 and stores outside slot0, which the ISA database says
those slots cannot do. If the database is authoritative, the honest density is
close to 1.00 and this item becomes "there is no ILP to get without new
scheduling", not "the packer is weak". Settle CB-141 first.

Other contributors visible in the assembly, independent of CB-141:

* `xor32 r0, r0, r0` (soft-zero R0 rematerialization) — 43 occurrences across
  11 functions. Leaf functions use the expected 2 (entry + before return), but
  `main` has **20** and `Proc_1` has 4, i.e. it is re-zeroed around calls, not
  just at the prologue/epilogue. Worth checking whether R0 really needs
  rematerializing after every call or whether it can be treated as
  callee-preserved
* leaf functions with `spills=0` still open an 8-byte frame
  (`subi32 sp, sp, 8` / `addi32_w sp, sp, 8`) — see `Proc_7`, `Func_3`
* taking a global address always costs two bundles (`lui` + `addi32_w`) and is
  rarely packed with anything

---

## T3 — Keep the latency scanner as a real tool

**Status:** open · see `OPEN-COMPILER-BUGS.md` § B5

The scanner written to find the CB-139 exposed-pipeline violations lives only in
a scratch directory. It walks bundles in order, tracks `Data_Latency` from the
itinerary, and flags a read inside a producer's window. BundleSim structurally
cannot detect this class (no timing model), so this is the only check that can.
The same script shape also produced the T1 and T2 numbers above. Worth landing
somewhere maintained rather than being rewritten each time.
