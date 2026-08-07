# Format E encoding switch — working plan

Haydn is moving from **128-bit Bundle128** to **96-bit format E**. This file is
the handoff: it records where the work stands, what is already decided, what is
left, and the traps that have already cost time. It is written so a session
starting cold can continue without re-deriving anything.

Companion documents:

| Where | What |
|---|---|
| `simulator/TODO.md` | The migration's own ledger, incl. why the BundleSim catalog is deliberately frozen |
| `OPEN-COMPILER-BUGS.md` § B4 | CB-143, the compiler-side ledger entry |
| `~/haydn/haydn-ar-intrinsics-change.html` | Handout for users of the AR intrinsics whose prototypes change |

---

## 1. State right now

### Branches

| Repo | Branch | Head | Builds? |
|---|---|---|---|
| `llvm-project` | `haydn` | `ef5c1b1971de` | **yes, fully green** |
| `llvm-project` | `haydn-formate-switch-mc` | `9e631c69a630` | **compiles; 424/430 CodeGen produce output; `HaydnTests` builds and runs 142/253. Not green: see § 5.2, § 5.6, § 5.7.** |
| `llvm-project` | `haydn-formate-switch-wip` | `6f0d97cf0e10` | rebased; now subsumed by `-mc` |
| `simulator` | `master` | `bdf14d7` | yes, green except CB-130 |

`haydn` is the trunk. Everything on it is green and committed; work from it.

All three branches are on the `github` remote. The pre-rebase states of the two
WIP branches are preserved as the tags `backup/wip-preformate-20260807` and
`backup/mc-preformate-20260807`, also pushed — the rebases were force-pushed
over the branch names, so those tags are the only copies of what was there
before.

### The two WIP branches were COMPLEMENTARY — and are now joined

An earlier revision of this file said `haydn-formate-switch-wip` "is gone with
the host that held it". **That was wrong — the host was still there and so was
the branch.** It has since been pushed, rebased and merged into
`haydn-formate-switch-mc`, which now carries both halves. The table is kept as
the record of what each contributed:

| | `haydn-formate-switch-wip` | `haydn-formate-switch-mc` |
|---|---|---|
| both root `.td` include switches | yes | yes |
| AR reshape — 7 logicals into `HaydnInstrInfo.td` | **yes** | no |
| `BuiltinsHaydn.td` AR prototypes | **yes** | no |
| GISel AR lowering | **yes** | no |
| the five `HaydnFormats*.td` + composite deleted | no | **yes** |
| Bundle128 `InstSlot`s deleted from `HaydnSlots.td` | no | **yes** |
| slot-model correction (alternates index ≠ slot) | no | **yes** |
| composites / emitter / printer / parser rework | no | **yes** |

**Neither was a superset of the other**, and the join confirmed it precisely:
the AR half closed exactly the 7 GISel errors it carried and moved nothing
else. How it was done, since the shape is worth keeping:

1. `git rebase haydn haydn-formate-switch-wip` — **no conflicts at all**. The
   trunk had not touched `Haydn.td`, `HaydnAsmMatcher.td` or
   `BuiltinsHaydn.td` since the WIP branch's base, so the six-line include
   switch replayed clean. Its second commit (the `--emit report` one-liner) was
   auto-dropped as already applied — trunk's `595218b8a264` is the same patch.
2. `git rebase haydn haydn-formate-switch-mc` — **required, and not optional**.
   `-mc` was based five commits below the trunk, two of them code:
   `f9ed0ff6365f` (`HaydnBundlePlan.h`) and `d7e0d13b534f` (the unit axis).
   The unit axis touches the same four files `-mc` reworks. Only
   `HaydnPlacementAlternative.h` actually conflicted; the other three
   auto-merged, and all three were checked by hand afterwards rather than
   trusted.
3. `git cherry-pick` the AR reshape onto `-mc`. Only the two root `.td` files
   conflicted, and **both conflicts were pure comment text** — the `include`
   lines were byte-identical on the two sides.

The conflict in `HaydnPlacementAlternative.h` was the interesting one and it
resolved cleanly, because the two sides were written for each other without
knowing it: trunk's comment on `fieldSlotsForAltIndex` said "at the switch this
must become *ask the member for its own slot kind*", which is exactly what
`-mc`'s `fieldSlotsForMember` does; and `-mc`'s note saying "nothing currently
enforces format E's rule that no two entries may name the same unit" is exactly
what the unit axis had since implemented. Both stale halves of that
conversation were rewritten rather than carried forward.

The branch here was originally created under the name
`haydn-formate-switch-wip` too, which was a bad call precisely because this
file documented that name as belonging to something lost. Renamed to
`haydn-formate-switch-mc` for what it actually holds.

Earlier revisions of this file named `haydn` heads `9802930e5fde` and
`135c38e2f1bc`. Neither hash exists in the repo; the branch was rebased before
it was pushed. The commit *subjects* in § 4 are the durable reference, and the
hashes there are the ones on the pushed branch.

### Green baselines — verify these before and after every step

```sh
# llvm-project
cmake --build build -j"$(nproc)" -- -k 0             # 0 errors; -k 0, see § 5.2
build/bin/llvm-lit -s llvm/test/CodeGen/Haydn llvm/test/MC/Haydn
#   589 discovered: 573 pass, 8 XFAIL, 8 unsupported, 0 fail
cmake --build build -j"$(nproc)" --target HaydnTests  # REQUIRED — see § 6.12
build/unittests/Target/Haydn/HaydnTests               # 253/253 (248 + 5 unit-axis)
build/bin/llvm-lit -s lld/test/ELF/haydn \
    lld/test/ELF/haydn-relocations.s lld/test/ELF/haydn-linker-script.s   # 24/24

# the encoding's own gates
python3 llvm/lib/Target/Haydn/utils/haydn_encoding.py --database ~/haydn --check
#   "format E: 3686 placements over 126 (entry, unit, type) shapes"
python3 llvm/lib/Target/Haydn/utils/haydn_encoding.py --database ~/haydn --emit roundtrip
#   "round-trip over 3686 placements / every placement encodes and decodes back to itself"

# simulator (needs the sysroot installed first — see § 2)
ctest --test-dir build -j"$(nproc)"
#   221 tests: 220 pass, golden_pin skips, cb44_o2_stale_cond_max_reduce fails (CB-130)
```

**Run only one full `ctest` at a time.** Two concurrent runs starve
`lldb_feature_matrix` (§ 6.7) and time out four yarpgen cases. This has produced
false failures twice.

---

## 2. Environment

```sh
export HAYDN_BIN=/home/ckchen/haydn/llvm-project/build/bin
export BUILD_DIR=/home/ckchen/haydn/llvm-libc-haydn-build
export LLVM_SRC=/home/ckchen/haydn/llvm-project
export BUNDLESIM_HAYDN_TOOLCHAIN_BIN=$HAYDN_BIN   # run_c needs this outside ctest
```

The ISA database is `~/haydn/` — `instruction_type_index.json`,
`format_e_bit_layout_v2.json`, `VLIW_Engine_Compiler_Constraints.md` and
friends. It is pinned by `simulator/bundlesim/isa/database/generated/GOLDEN_INPUTS.sha256`.

After any clang rebuild, the baremetal libc **and the BSP stage** must be
rebuilt before the simulator suite means anything. Both are stale-prone for the
same reason (§ 6.6), and a stale BSP is the more confusing of the two because it
links fine and then faults deep inside libc:

```sh
ninja -C "$BUILD_DIR" -t clean          # REQUIRED — see § 6.6
simulator/scripts/build_haydn_llvm_libc.sh
simulator/scripts/install_haydn_sysroot.sh

cd simulator
rm -rf build/bsp-stage                  # REQUIRED — see § 6.6
cmake --build build --target haydn_bsp
```

---

## 3. What format E is

* Bundle is **96 bits / 12 bytes**. Payload starts at bit 6.
* Header: `Inst{2-0} = 0b111` format indicator, **`Inst{3}` = entry count**
  (0 → 2 entries, 1 → 3 entries), `Inst{5-4}` reserved.
* Two composites, both 12 bytes: **`BUNDLE_E2`** (2 entries) and
  **`BUNDLE_E3`** (3 entries). Bundle128 had exactly one, `BUNDLE128_FULL`.
  This is the single biggest structural change (§ 5.2).
* Entry windows are **not byte-aligned**: 2-entry = 45 and 41 bits; 3-entry =
  31, 31 and 27 bits. There is **no one-entry form**, so a single-op bundle must
  be NOP-padded to two entries.
* Seven units: `LOADSTORE0 LOAD1 ALU0 ALU1 ALU2 MAC0 MAC1`. An entry maps to
  exactly one; no two entries in a bundle may share a unit. Slots carry no
  capability any more.
* Members are named **`<LOGICAL>_P<form><pos>_<UNIT>`** — e.g. `JAL_P20_ALU0`,
  `D_LDW_BREV_IMM_P31_LOAD1`. 3578 members + 2 composites = 3580 defs.
  Bundle128's spelling was `<LOGICAL>_S<k>`.

Generated by `llvm/lib/Target/Haydn/utils/haydn_encoding.py`:

```sh
# --emit td needs the logicals' instruction properties; see § 6.9
build/bin/llvm-tblgen --dump-json -I llvm/lib/Target/Haydn -I llvm/include \
    llvm/lib/Target/Haydn/Haydn.td -o /tmp/haydn-records.json

python3 .../haydn_encoding.py --database ~/haydn --emit td --flags-from /tmp/haydn-records.json \
                                                                   -o .../HaydnFormatEEncoding.td
python3 .../haydn_encoding.py --database ~/haydn --emit composites -o .../HaydnFormatEComposites.td
python3 .../haydn_encoding.py --database ~/haydn --emit schedule   -o .../HaydnFormatESchedule.td
python3 .../haydn_encoding.py --database ~/haydn --emit report --target-dir llvm/lib/Target/Haydn
```

### Already live on `haydn`

Scheduling has **already** moved to the unit model and is in the build:
`HaydnGeneric.td:33` includes `HaydnFormatESchedule.td`; 7 `U_*` FuncUnits, 25
`Unit_*_L*` itinerary classes, 114 `let Itinerary = Unit_*` in
`HaydnInstrInfoAuto.td`. Encoding, packing and execution are all still
Bundle128. 15 `Slot*` itinerary classes and `SLOT0/1/2` FuncUnits remain,
referenced by 195 member definitions; they retire with the member files.

---

## 4. Done so far, and why each mattered

All on `haydn`, each verified green before commit.

| Commit | What | Why it is not cosmetic |
|---|---|---|
| `14754e31453b` | Fixup kinds decided from the logical, not the member spelling | Was 78 member names in three switches. Format E turns one `_S0` member into up to seven `_P..` ones, so the table would have grown, not moved. Now 8 references remain. **This is the change that was previously tried and reverted** — see § 6.1. |
| `c263468226eb` | `stripHaydnMemberSuffix` understands both spellings, with unit tests | The member→logical fold is one pure function now, and format E's spelling is testable while Bundle128 is still live. |
| `4b429621712c` | Bundle decoder takes its parcel width from `getFormatSize()` | Was a literal 128-bit APInt, a 16-byte length check and `Size = 16` on four exits. A wrong stride does not fail cleanly, it desyncs the parcel stream. |
| `8e2b16552a6f` | The generated encoding is actually includable | Three defects that could only surface by trying: `Inst` narrower than `Size * 8`; composite AsmStrings written in the ISA document's backtick notation; members and composites in one file. |
| `595218b8a264` | `--emit report` no longer crashes | A stray line referencing undefined names. It is the only view comparing the database against TableGen, so it should not be the unrunnable mode. |
| `d67473b945df` | **Cross-check the two database files** | Found 16 instructions silently losing an operand. See § 5.3 — this is the most consequential finding so far. |
| `8c3a9a9d241c` | Regenerate after correcting 76 mapping rows | The correction is forced by the database's own alias declarations, not chosen. |
| `9cc0a9687653` | `ORI32_W` retired, first of the `_W` fold | Establishes the template *and* records the trap that the wrong approach passes lit but breaks `HaydnTests` (§ 6.2). |
| `961ccef2c648` | The ten branch `_W` forms retired | Found § 6.9 and § 6.10 — the narrow members were never wired for CodeGen and neither defect shows up in a lit run. Forced the BundleSim fix below. |
| `837f8e079dce` | `JAL_W` / `JALR_W` retired | Same two gaps plus CB-129's `isBarrier`. `jal sym` now emits `R_HAYDN_WIDE_CallSImm20`; `FIXUP_HAYDN_CallSImm20` and `FIXUP_HAYDN_BranchSImm16` are no longer selected by anything. |
| `74aa4d24f9eb` | `--fix-operand-mapping`, and the plan's own corrections | The 76-row correction did not travel with the repos and this host had the corrected `.td` against an uncorrected database. Repair is now reproducible and forced, not remembered. See § 5.3. |
| `6e35b4124346` | `CSRW_W` retired | First fold that was not a rename in shape: dropped the dead `$rd` after showing the decoder-parity reason for it was false (`CSRW` is `isCodeGenOnly`, so it was never in the decoder table), and took the wide def's `hasSideEffects = 0, Defs = [SFR]`, which `csrw-hwloop-hazard.mir` exercises through postmisched. |
| `318248c8d1aa` | The three `SET_HWLOOP` `_W` forms retired | The only fold where the base names were occupied — by pseudos, which move to `_PSEUDO`. Forced deleting the three narrow members: pairing is by name, so the renamed logical would otherwise have picked up a member whose shape the database contradicts. |
| *(this session)* | `MOVEI_H`/`MOVEI_L` and the four `X4CMUL16` forms promoted to real logicals; GISel selects them instead of `_S0`/`_S1` members | Removes 6 of § 5.2's 23 errors **without** the switch, because the defect is not encoding-dependent: a bare `HaydnInst` with no `Inst` bits is inferred `MCID::Pseudo` and AsmPrinter drops it, so GISel had to name a member, which pins every `x4cmul16` to slot 1 and every `movei` to slot 0. Same promotion `X4ABS16`/`ABS64`/`X2ABS32S` already had. `Fmt48_MOVEI` is restored for this (retired earlier for having no live consumer). Found § 6.12. |

On `simulator/master`: `4b65727` (LLDB port TOCTOU), `84d545d` + `29ac239`
(docs), `786d7c4` (cb99 wired up as an executed case), `b8da0eb` (golden re-pin),
`bdf14d7` (branch range scale read from the catalog — § 6.11).

---

## 5. What is left

Recommended order. A, B and D can each be done on the green trunk; C is the
atomic step.

### 5.1 `_W` family retirement — 16 of 17 done, 1 left

Format E has **no `_W` member for any of them**; the database has one
instruction (`ADDI32 rt, rs, imm20`, `BEQ rs1, rs2, imm12`, …) whose member
carries the wide shape. So the wide form survives under the base name and the
narrow legacy declaration goes.

Done: `ORI32_W` (`9cc0a9687653`); the ten branches `BEQ_W`…`BLTU_W`,
`BEQZ_W`…`BLTZ_W` (`961ccef2c648`); `JAL_W` / `JALR_W` (`837f8e079dce`);
`CSRW_W` (`6e35b4124346`); `SET_HWLOOP_W` / `_F2_W` / `_REG_W`
(`318248c8d1aa`).

**Left: `ADDI32_W` only** (82 C++ references). It is the one the plan always
said to do last, and it is not a rename — see below, where the attempt is
written up.

`SLLI64` / `SRLI64` / `SRAI64` are `Fmt48_Wide*` but carry no `_W` suffix and
match the database already. Leave them.

#### The template, corrected

The original template said "C++ only, never touch the `.td`". **That was wrong,
and believing it is how the branch fold shipped two silent defects before they
were caught.** It held for `ORI32` only by luck: `ORI32`'s narrow declaration
already carried `uimm20`, so nothing about it needed fixing. In general the
narrow declaration and its `_S0` member were *never wired for CodeGen* — nothing
selected them — so they are missing whatever the `_W` pair had to be given.
§ 6.9 and § 6.10 are the two axes found so far.

What § 6.2 actually forbids is **moving a logical to a different format class**.
Editing flags or an operand class *within* the class a def already has is fine,
and `HaydnTests` is the gate that proves it.

1. `sed -i 's/Haydn::<X>_W\b/Haydn::<X>/g'` across the referencing `.cpp`/`.h`.
2. Fix the name→opcode map in `HaydnInstrInfo.cpp` (~line 194, `KnownBases`):
   the entry keys off the *member* name with its suffix stripped, so
   `{"BEQ_W", Haydn::BEQ_W}` becomes `{"BEQ", Haydn::BEQ}` — it must name the
   member that is now live, not the one that was.
3. Build. Duplicate `case` labels are a compile error and a useful signal; the
   degenerate `IsWide ? Haydn::X : Haydn::X` ternaries and `||`-chains that
   compare the same enum twice are **not**, so grep for them.
4. **Diff the retiring def against the one that survives, field by field.** For
   each of `let` flags, `Defs`/`Uses`, and every operand class, on *both* the
   logical and its `_S0` member. Any difference is either a defect in the narrow
   def or a deliberate correction in the wide one, and either way you have to
   decide which. Do not skip the member: after `materializeMultiOpcodeInstrs`
   the member's `MCInstrDesc` is the only one AsmPrinter sees.
5. Assemble the two spellings side by side and compare bytes:
   ```sh
   printf '.text\n{ beq r1, r2, 32; nop; nop }\n{ beq_w r1, r2, 32; nop; nop }\n' > /tmp/cmp.s
   build/bin/llvm-mc -triple=haydn-unknown-elf -filetype=obj /tmp/cmp.s -o /tmp/cmp.o
   build/bin/llvm-objdump -d -z --triple=haydn-unknown-elf /tmp/cmp.o
   ```
   They must differ **only in the opcode discriminator**. This is what caught
   § 6.10: `beq` encoded 32 where `beq_w` encoded 16.
6. Prove the fold is a rename rather than asserting it — see below.
7. Full baseline (§ 1) **including `HaydnTests`** (§ 6.2), **`lld/test/ELF/haydn`**
   (§ 6.1) and the simulator suite (§ 6.11 is why).

#### Proving the fold is a rename

Regenerating expectations only records what the new encoder did. The check that
actually has teeth is to diff `llc` output across the change and require that
every difference vanish when the `_w` suffix is put back:

```sh
# dump all 430 CodeGen tests into $1/ using one fixed llc invocation
for src in $(find llvm/test/CodeGen/Haydn -name '*.ll' | sort); do
  key=$(echo "${src#llvm/test/CodeGen/Haydn/}" | tr '/' '_')
  timeout 60 build/bin/llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
      -o - "$src" > "$1/$key.txt" 2>/dev/null
done
```

Run it before the change, `git stash` / rebuild / run it after, then for each
pair apply `s/\b(jalr|jal)_w\b/\1/` to the *before* side and require equality.
The JAL/JALR fold came back 423 files differing, **0 residual** — that is the
result to insist on. The branch fold came back with 7 residuals, and every one
of them was a real bug (§ 6.9, § 6.10, and the stale `Defs = [SFR]`).

Two caveats: `.mir` tests that spell the opcodes literally will always show up as
residuals (their *input* names `BNEZ_W`), and a residual that is a genuine
improvement still has to be understood before it is accepted.

#### The three that are not pure renames

* **`CSRW_W` — analysis done, not yet implemented.** The shapes:

  | Def | Where | Shape |
  |---|---|---|
  | `CSRW` (logical) | `HaydnInstrInfo.td:911`, `FmtCSR` | `(outs GPR32:$rd), (ins uimm8:$csr_addr, GPR32:$rs)` — 3 ops, `$rd` dead |
  | `CSRW_W` (logical) | `HaydnInstrInfo.td:1866`, `Fmt48_WideCSR` | `(outs), (ins uimm8_csr:$uimm8, GPR32:$rt)` — 2 ops, `hasSideEffects = 0, Defs = [SFR]` |
  | `CSRW_S0` | `HaydnFormatsALU32.td:1164` | `(outs), (ins uimm8:$csr, GPR32:$r)` — 2 ops |
  | `CSRW_W_S0` | `HaydnFormatsALU32.td:1170` | identical 2 ops |

  Both members and all 7 format E members are two-operand, so the target shape
  is the two-operand one. The dead `$rd` exists "only for MC operand-count
  parity with the shared `FmtCSR` decoder" — `FmtCSR` is shared with `CSRR`
  (0x39, real `$rd`) and its decoder case 25 decodes `rd + csr_addr + rs` for
  both. So dropping `$rd` from `CSRW` stays inside `FmtCSR` (§ 6.2-safe) but
  **the shared decoder has to be checked**: `CSRW` is 0x3A and `CSRR` 0x39, so
  they are distinguishable, but the generated table has not been inspected.

  The four C++ references: `HaydnInstrInfo.cpp:205` (`KnownBases`);
  `HaydnHazardRecognizer.cpp:118-120` (`getHwloopCsrAddr` already branches on
  the shape to pick operand index 1 vs 0 — it collapses to index 0);
  `HaydnAsmPrinter.cpp:601` and `:1034` (both *create* `CSRW_W`, so both already
  build the two-operand shape). Retiring the narrow form deletes the
  dead-def-stripping hack at `HaydnMCCodeEmitter.cpp:556-560`, which is 2 of the
  23 errors in § 5.2.

* **`SET_HWLOOP_W` / `_F2_W` / `_REG_W`** — the database names these
  `SET_HWLOOP`, `SET_HWLOOP_F2`, `SET_HWLOOP_REG`, and **`SET_HWLOOP` and
  `SET_HWLOOP_REG` already exist in the tree as `HaydnPseudo` defs** (the
  pre-expansion forms with `brtarget` MBB operands). The pseudos must be renamed
  before the real instructions can take those names. `SET_HWLOOP_F2` is free.
  Note `SET_HWLOOP` fits only the 45-bit `P20` entry (35 bits of operands).
  Watch `HaydnRelocLayout`'s `HWLoopOff1`/`Off2` rows and § 6.8.

* **`ADDI32_W` — attempted, analysed, and reverted deliberately. Read this
  before trying again; the mechanical part is easy and is not the problem.**

  The shapes. `ADDI32` is `FmtI<0x20>` with `simm16`; `ADDI32_W` is
  `Fmt48_WideGPRImm<0x01>` with `simm20_wide_abs`. The members
  `ADDI32_S0/_S1/_S2` (`0b0001001`) **already carry `simm20`** and already
  match the database (`ADDI32 rt, rs, imm20`, `Available: ALU0, ALU1, ALU2`).
  So only the logical is narrow.

  Three findings, all of which hold up and are worth keeping:

  1. **`FmtI` cannot hold the immediate.** It is a 32-bit format whose imm
     field is `Inst{15-0}`, with `Inst{31,30}=0b01`, opcode at `29-24`, rd at
     `23-20`, rs at `19-16`. There is no room for 20 bits, and § 6.2 forbids
     re-parenting to `Fmt48_WideGPRImm`. The way out is that the logical's own
     encoding is vestigial — every `ADDI32` is bundled and committed to a
     member by `materializeMultiOpcodeInstrs` before it is encoded — so widen
     the operand to `simm20` and add `isCodeGenOnly = 1`, which keeps the
     unencodable 32-bit form out of the AsmMatcher and the decoder tables
     where a 20-bit operand over a 16-bit field would silently truncate.
     `addi32` still parses and decodes through the members. This is the same
     shape the `CSRW` fold ended up in.
  2. **The fixup path already works.** `simm20` carries no `EncoderMethod`, so
     symbolic operands fall back to `getMachineOpValue` and land in
     `getExprFixupKind` — which already answers `FIXUP_HAYDN_LO20` for
     `ADDI32` and folds members through `getHaydnLogicalBaseOpcode`. That is
     the same fixup `simm20_wide_abs`'s `EncoderMethod` emitted directly, so
     § 6.1's trap does not bite here. Confirmation: folding the C++ produces a
     **duplicate `case Haydn::ADDI32:`** in `getExprFixupKind`, because the
     retired `ADDI32_W` case a few lines down returned `LO20` too. Merge them.
  3. **It is not a rename, and that is the whole problem.** `ADDI32_W` had one
     member, `ADDI32_W_S0`, so every wide add was pinned to slot 0. `ADDI32`
     has three. Placement opens up, bundles pack denser, live ranges move and
     regalloc follows. Widening `simm16` → `simm20` separately changes
     *selection*: 17–20-bit constants that used to force a wide form or
     LUI+ADDI32 now fit the base form.

  Measured over the 430-test CodeGen corpus, with `s/addi32_w/addi32/` applied
  to the before side: **423 files differ, 0 of them a pure rename** — 242 are
  the same instruction stream repacked, 177 are the same multiset reordered,
  and 4 have a genuinely different stream (`c-e2e-bundle-dump`,
  `dsp-intrinsic-e2e`, `post-inc-offset-preserve`,
  `swpipeline-load-mac-schedule-found`), differing in allocated registers and
  in frame adjustments. 53 lit tests fail, on spelling *and* slot position.

  So § 5.1's "prove the fold is a rename" gate cannot apply, and regenerating
  53 expectations would only record what the new encoder did — precisely what
  § 5.4 warns against. **The gate for this one is execution: the simulator
  suite, after the full § 2 libc + BSP rebuild (§ 6.6).** Do it as its own
  session: fold the C++, merge the duplicate fixup case, run ctest first, and
  only then regenerate the lit expectations. Budget for the 4 changed-stream
  files needing individual review — denser packing is the *expected* win here
  (the database says three ALUs), but § 5.1's rule stands that a residual
  which is a genuine improvement still has to be understood before it is
  accepted.

### 5.2 The 23 C++ errors — the atomic step

Reproduce by switching both roots (this is what the WIP branch holds):

* `Haydn.td` — replace the five `HaydnFormats{ALU32,ALU64,LS,LD,MAC}.td`
  includes with `HaydnFormatEEncoding.td`; replace `HaydnCompositeFormats.td`
  with `HaydnFormatEComposites.td`.
* `HaydnAsmMatcher.td` — **the same change**, except it must NOT gain the
  composites: § 6.4's reason still holds, and the matcher root never included
  `HaydnCompositeFormats.td` either. This is a second TableGen root
  (`CMakeLists.txt` sets `LLVM_TARGET_DEFINITIONS` for `-gen-asm-matcher`
  only). Missing it is what produced 1739 errors instead of 30.
* `HaydnSlots.td` — **delete the three `InstSlot` defs and the three
  `HaydnSlotS*` classes**, leaving only `HaydnFormatInst`. Not optional and not
  cosmetic; see "The 23-error count was measuring the wrong thing" below.

**The switch has been dry-run. TableGen parses it cleanly** — no tblgen
errors at all, so all 3578 members and the two composites are includable in
both roots, property flags included. The C++ fallout was then measured with
`cmake --build build -- -k 0` (plain `--build` stops early and shows only 7):

| n | Category | Status |
|---:|---|---|
| 5 | `BUNDLE128_FULL` | open — the real design work |
| 4 | Decoder tables | open |
| 6 | GISel selects members directly | open |
| ~~6~~ | ~~hwloop `_S0` predicates~~ | **done on trunk, `c290615e3cb0`** |
| ~~2~~ | `CSRW_S0` normalization → 1 left | halved by the `CSRW_W` fold |
| 7 | The AR logicals in `HaydnFormatsLS.td` | open — § 7, what the lost WIP branch held |

That was **29** before the hwloop fold, **23** after. The category counts
match this section's original table exactly; the 7 AR ones are extra because
they were already relocated on the WIP branch.

Note the two root edits do not survive a branch switch on their own — they
are uncommitted working-tree changes and `git checkout` carries them along.
Commit them before switching branches, or redo them; they are six lines.

Then, by category:

| n | What | Notes |
|---|---|---|
| ~~5~~ | ~~`BUNDLE128_FULL` → `BUNDLE_E2` / `BUNDLE_E3`~~ | **Done on `haydn-formate-switch-mc`.** It was billed as "the real design work" and turned out not to be — the generated packet-format table already makes the choice. See "What the composite choice actually is" below for what it really cost. |
| 4 | Decoder tables | `DecoderTableS048/S140/S240` → `DecoderTableP2048/P2148/P30../P31../P32..`; `DecoderTableBundle128128` → the two `FormatE2`/`FormatE3` tables. Pick the composite from the header: `Inst{3}`. `HaydnDisassembler.cpp:252,267,282,477`. Its `SlotGeo Slots[3]` must become 2-or-3. |
| ~~6~~ | ~~GISel selects members directly~~ | **Done on the trunk, before the switch** — see § 4. Not encoding-dependent: the promotion is verifiable while Bundle128 is still live, and the C++ then names only logicals. |
| ~~6~~ | ~~hwloop predicates naming `_S0`~~ | **Done on the trunk in `c290615e3cb0`**, before the switch. Folded through `getHaydnLogicalBaseOpcode`, which resolves the base by name search rather than a table, so it works for either spelling. |
| ~~1~~ | ~~`CSRW_S0` normalization~~ | **Done on `haydn-formate-switch-mc`, by deletion.** The hardcoded retarget to `CSRW_S0` was exactly the "residual logical, find its member for this entry" case, so it is subsumed by the general `findMemberForSlot` path the alternates-index correction introduced. Naming one member by hand could not survive format E anyway — `CSRW` has seven. |
| 7 | The seven AR logicals | `PLDWWUA`, `FLAR`, `WBARWUA` and the four `D_*UA_POST` live in `HaydnFormatsLS.td` and vanish with it, so GISel loses them (`HaydnInstructionSelector.cpp:6031-6130`). This is § 7's reshape, which the lost WIP branch had already done. Database shapes confirmed: `PLDWWUA_POST ar_sel, rs`; `WBARWUA ar_sel, rs`; `FLAR ar_sel`; the four `D_*UA_POST rtd, ar_sel, rs`. Format E has members for all seven. |

#### The 23-error count was measuring the wrong thing

**It counts C++ compile errors, and most of this step is not a compile error.**
The switch has now been carried far enough to see the real shape; the work is
parked on `haydn-formate-switch-mc` (`f7e173347bb4`), which builds down to 27
errors and is where the numbers below come from. Read that commit message
before re-deriving.

Three things the count missed:

1. **The dead Bundle128 InstSlots hid 15 errors.** `HaydnSlots.td` declares
   `s0_slot`/`s1_slot`/`s2_slot`. Switching the roots leaves them declared, so
   `MCSlotKind::Haydn_SLOT_S0` still *compiles* — while meaning something
   different. Every `InstSlot` becomes an enumerator and a row in the generated
   `HaydnSlots` table, so the three dead ones push format E's five kinds off
   0..4 and each carries a `ConflictBits` of "everything" (they appear in no
   live composite). **Delete them in the same commit.** Doing so takes the
   count from 23 to 32 and makes the slot-model surface visible, which is the
   honest number.

2. **The alternates index is no longer the slot, and that is silent.**
   Bundle128's `AlternateInsts` rows were sparse size-3 with index == slot, so
   `1 << AltIndex` was the occupancy bit and `Alts[SlotIdx]` was the member for
   a composite operand. Format E indexes the same rows by **placement** — the
   (entry position, unit) pair, 0..17 — so index 6 and index 10 are both ALU0
   in different entries, and index 6 and index 9 are both entry P30 on
   different units. Neither the index nor its low bits are a slot. Three places
   assumed otherwise and **none of them fails to compile**:
   `HaydnMCFormats::getLegalSlots`, `fieldSlotsForAltIndex`
   (`HaydnPlacementAlternative.h`), and the emitter's residual retarget
   `Alts[SlotIdx]`. All three must ask the member for its own slot kind
   (`getSlotKind(MemberOpc)`). This is the single most load-bearing correction
   in the step.

3. **~~The unit-exclusion rule is not modelled anywhere.~~ Now a second axis in
   the packer — see § 7.1.** § 3 says no two entries of a bundle may share a
   unit. The slot model cannot express it: the generated `ConflictBits`
   correctly let P30/P31/P32 co-occur, and *which unit* each entry uses is a
   property of the member chosen, not of the slot. Nothing stopped the packer
   putting ALU0 in P30 (placement 6) and ALU0 in P31 (placement 10). Note
   `--emit roundtrip` cannot see this: it is symmetric across encoder and
   decoder, the same blind spot as § 5.3 and § 6.10.

#### What the composite choice actually is

**It is not new code, and this was the wrong thing to worry about.** The
generated packet-format table already carries both rows:

| Row | SlotSet | Size | `getSlots()` (AsmString order) |
|---|---|---|---|
| `BUNDLE_E2` | `0x3` = {P20,P21} | 12 | P21, P20 |
| `BUNDLE_E3` | `0x1c` = {P30,P31,P32} | 12 | P32, P31, P30 |

`Bundle::getFormatOrNull()` → `PacketFormats::getFormat(OccupiedSlots)` returns
the first row covering the occupancy, and because the generated `ConflictBits`
make the two slot sets mutually exclusive, an occupancy can only ever be
covered by one of them. **That lookup is the entry-count decision.** The only
work is deleting `assert(Format->Opcode == BUNDLE128_FULL)` and replacing the
`for K in 0..ISSUE_SLOT_COUNT { Haydn_SLOT_S0 + K }` loops with iteration over
`Format->getSlots()` — **reversed**, because `getSlots()` is in AsmString order
(high entry first, `"$e1; $e0"`) while the operand dag is
`(ins p20_entry:$e0, p21_entry:$e1)`, low entry first. Both AsmPrinter and
AsmParser already carried a comment saying the table would select the opcode
when a second row landed; it does.

Four consequences that are real behaviour changes, not renames:

* **The all-NOP bundle is no longer an all-zero word.** Bundle128 had no header
  and a zero slot window *was* the NOP. Format E's bits[2:0] are the `0b111`
  format indicator and bit 3 the entry count, so a zero 12-byte word is a
  different format's bundle. Build it through the normal path so it gets a
  header and NOP-padded entries.
* **12 bytes is not two whole limbs.** `emitBundle128Word` wrote two `uint64`s;
  96 bits needs `uint64` + `uint32` or it runs 4 bytes into the next bundle.
* **The "prefer slot 0 for a solitary instruction" hint has to go**, in both
  `HaydnAsmParser` and the emitter. It was safe under Bundle128 because every
  instruction had an `_S0`. Format E's 2-entry entry0 admits only
  `ALU0`/`LOADSTORE0`/`MAC0`, so an ALU2-only op (`ARCTAN`, `RECIP`, …) has no
  placement there at all and must land in a 3-entry bundle. Give no hint and
  let the solver pick.
* **Entry windows are not byte-aligned** (45/41 and 31/31/27, payload from bit
  6), so the emitter's fixup translation `(95 - RightOffset) / 8` truncates and
  a fixup can start mid-byte. Bundle128's 48/40/40-from-bit-0 windows divided
  exactly. The byte base is still the right anchor; the sub-byte part lives in
  the fixup kind's field geometry in `HaydnRelocLayout.cpp`, **which is still
  Bundle128's and has to be re-derived** — this is the same work § 6.10 needs
  for the immediate scaling, and it is why that item is not a pure generator
  change.

#### What is left after all that — measured, not estimated

**0 C++ errors.** `haydn-formate-switch-mc` compiles, and the toolchain
assembles a 96-bit bundle and reads it back:

```
{ nop; add32 r1, r2, r3 }                      -> 07 ...  12 bytes, BUNDLE_E2
{ add32 r1,r2,r3; add32 r4,r5,r6; add32 ... }  -> 8f ...  12 bytes, BUNDLE_E3
```

both re-disassembling to exactly what went in. **This is much less than it
sounds** — see "what the round trip does not prove" below.

| n | Where | What | State |
|---:|---|---|---|
| ~~7~~ | ~~`HaydnInstructionSelector.cpp`~~ | ~~The AR logicals — § 7's reshape.~~ | closed by the branch join |
| ~~9~~ | ~~`HaydnBundlePlan.h` + downstream~~ | ~~The product-format model.~~ | closed by `c970a7ef3dea` |
| ~~8~~ | ~~`HaydnDisassembler.cpp`~~ | ~~Decoder tables + `SlotGeo Slots[3]`.~~ | closed by `ab459e49b08a` |

The trajectory was 27 → 24 (rebase onto trunk) → 17 (AR half joined) → 8
(product table filled in) → 0 (disassembler).

##### The disassembler's content gate was wrong, and did not show up as an error

`isValidFlexSlotWindow` read the unit from the entry window's **top three
bits** — where Bundle128 put its FU and where format E puts reserved zeros.
Format E's unit (`mapping`) is in the **bottom two bits**, and its value
depends on the entry position: ALU0 is `0b00` at P20 but `0b10` at P30. Fed a
valid `ADD32_P20_ALU0` it read FU=0 and opcode=0 out of the reserved field and
returned false, so **every non-NOP bundle would have failed to disassemble**
while the file compiled perfectly. The eight compile errors were the visible
part of the port; this was the load-bearing part.

It was deleted rather than ported, because the reason it existed is gone:
Bundle128's generated sub-tries were catch-all defaults that accepted any
window, so a hand-written FU + opcode-range table was the only content check.
Format E's sub-tries are real tries — they switch on `mapping`+`type` and
`OPC_CheckField` the reserved bits — so the generated table **is** the content
authority and a second hand-maintained copy could only drift from it.

Two smaller consequences, both of which change behaviour:

* **No "all-zero window is an exempt NOP" case.** Format E has real NOP members
  at every entry position and an all-zero window decodes to whichever has
  `mapping == 0b00` there (`NOP_P20_ALU0`, `NOP_P30_MAC0`, …), so a NOP is a
  successful decode, not an empty sub-MCInst. An all-zero **word** is still not
  a bundle — its header is not `0b111` — the same fact that makes
  `writeNopData`'s all-zero pad wrong.
* **The trailing 2-byte `0x0000` → `Haydn::NOP` fallback is gone.** It rested
  on Bundle128's "all-zero word is the NOP", and there is no 2-byte format E
  encoding at all, so synthesising a NOP from two zero bytes invents an
  instruction that cannot exist. Short tails render `<unknown>`. Revisit when
  § 5.4's expectations are regenerated.

##### Text order in a bundle is AsmString order, high entry first

Worth knowing before reading a surprising assembly result. `{ a; b }` means
`a` in the **high** entry, because the composite AsmStrings are `"$e1; $e0"`
and `"$e2; $e1; $e0"` and the parser indexes `getSlots()` by text position.

So `{ add32 r1,r2,r3; nop }` does **not** produce a 2-entry bundle: it asks for
`ADD32` in P21, which admits only ALU1/LOAD1/MAC1, and `ADD32` has no P21
member. The solver correctly falls back to a 3-entry bundle where `ADD32` does
have a placement. Writing it the other way round, `{ nop; add32 r1,r2,r3 }`,
gives `BUNDLE_E2`. Both composites are reachable; a run of E3-only results
means the test inputs were written in the wrong order, not that E2 is dead.

##### What the round trip does not prove

The hostile-input hard bar was re-checked and holds: 30 random 12-byte words
give 30 `<unknown>`, `llvm-objdump` exits 0, and each advances exactly 12 bytes
with no desync.

But an assemble/disassemble round trip is **symmetric across encoder and
decoder**, so it is blind to exactly the class of defect § 5.3, § 6.10 and the
`--emit roundtrip` gate are blind to. It proves nothing about fixup geometry,
and `HaydnRelocLayout` is **still Bundle128's** while format E's entry windows
are not byte-aligned. `lld/test/ELF/haydn` and the simulator are the only
things standing outside the encoder's own opinion.

Still open and not close to green: `HaydnTests` does not compile (the 12-file
three-slot port below), the 589 lit expectations are Bundle128's, and neither
lld nor the simulator has been run on this branch. Note the earlier estimate of 12 for the
product-format item was measured before `f9ed0ff6365f`, which took it to 9 —
it did **not** take it to 0, and reading its subject ("a table, not a
singleton") as though it had is an easy mistake to make.

##### The product table fill-in was not a pure data change

`f9ed0ff6365f` said the switch owed the table "one row becomes two, 16 bytes
becomes 12, `FormatID`'s enumerators, the slot-window widths and their
`static_assert`", and that every lookup would be unchanged. The lookups were
indeed unchanged. Three other things were not, and all three are silent:

* **The entry windows do not tile the word.** Bundle128's 48/40/40 summed to
  128 exactly, so the assert was `sum == width`. Format E's are 45+41 (4 bits
  unused) and 31+31+27 (1 unused) over a 90-bit payload, so that assert cannot
  be rescaled — it became two per-composite asserts of
  `header + windows + unused == 96`. Widths verified against the generated
  `HaydnFormatEComposites.td`, not the prose.
* **`makeBundle128Plan` stamped the format instead of deriving it.** With one
  row that was right. With two, the occupancy is what says which — a
  `{P30,P31}` occupancy is a 3-entry bundle — and the FormatID is stamped on
  the BUNDLE MIR root, so a wrong one names a composite whose `SlotSet` does
  not contain the slots in use. Now `makeProductPlan`, deriving by table scan.
  `HaydnPostRASchedStrategy`'s multi-MI finalize had the same constant with
  the chosen `VLIWFormat` already in scope.
* **`HaydnAsmBackend::writeNopData` held a second parcel-size oracle** — its
  own `constexpr uint64_t Bundle128Bytes = 16`, exactly what
  `HaydnBundlePlan.h`'s header comment forbids, and invisible to a grep for
  the plan's symbols. It would have kept padding in 16-byte units after the
  switch. **Its all-zero payload is still wrong** and is marked FIXME rather
  than quietly resized: format E puts `0b111` in `Inst{2-0}`, so twelve zero
  bytes are not a NOP bundle but a different format's. Needs a real
  `BUNDLE_E2` built through the encoder.

One known-wrong site remains, deliberately: `HaydnFinalizeBundle`'s
**singleton** path stamps the default row because it has no chosen format to
derive from. That is right for most singletons — there is no 1-entry form, so
a lone instruction NOP-pads to two entries — but wrong for an ALU2-only op
(`ARCTAN`, `RECIP`, …), which has no 2-entry placement at all (§ 7.1: ALU2
never appears in the 2-entry form) and must be `BundleE3`.

**And the compiler is the easy half.** Nothing below shows up as an error:

* **12 unit-test files assert the three-slot geometry** — ~150 references to
  `Haydn::SLOT0/1/2` and `SLOT_ALL`, nearly all of them in tests.
  `getLegalSlots(ADD32)` becomes `0x1F` not `0x7`; `SLOT_ALL` stops having a
  single meaning (a full bundle is `0x3` *or* `0x1c`); `makeBundle128Plan`,
  `fieldSlotsToIndex`, and the `ExhaustiveThreeSlotFillRejectsFourth` /
  `DualLoadPlanOccupancyIsS0S1` family all encode Bundle128's shape. This is a
  port of the packing expectations to a different bundle geometry, and § 6.2
  makes `HaydnTests` the gate that says whether the `.td` work was safe — so it
  cannot be skipped or deferred.
* § 5.4's 589 lit expectations.
* § 5.5's BundleSim catalog.

**Two generator gaps block this step, and neither shows up as a C++ error.**
Both were found the hard way during the `_W` fold, on Bundle128's narrow members
— the generated format E members have the same shape, so they will reproduce
them across all 3578:

* ~~No instruction-property flags (§ 6.9).~~ **CLOSED.** `--emit td` now takes
  `--flags-from`, the `llvm-tblgen --dump-json` output, and copies each
  logical's `isBranch` / `isTerminator` / `isCall` / `isBarrier` /
  `isIndirectBranch` / `isNotDuplicable` and its `Defs` onto every member it
  expands; it refuses to emit without it. Full reasoning in § 6.9.
* **No immediate scaling** (§ 6.10) — **still open, and it is not a pure
  generator change.** Members take plain `simm12` / `simm20` where the offset
  is stored in 2-byte units. The scale is derivable from the database (see
  § 6.10 — the claim that a `branch_scale` field exists there is wrong), but
  expressing it needs operand classes carrying
  `getSImmOpValueXStepWide<N,/*Shift*/1,…>`, and those need format E fixup
  geometry, which lands with this step rather than before it.

Both are silent: `--emit roundtrip` cannot see either, because it only checks
that the encoder agrees with the decoder, and both defects are symmetric.

Also delete, in the same commit: `HaydnFormats{ALU32,ALU64,LS,LD,MAC}.td`
(7615 lines) and `HaydnCompositeFormats.td`. **`HaydnFormatsLS.td` is the only
one that mixes 7 logical defs in with its members** (`PLDWWUA`, `FLAR`,
`WBARWUA`, and the four `D_*UA_POST`); the other four files are pure members.
Those seven have already been relocated to `HaydnInstrInfo.td` on the WIP branch,
with the new database shapes. The old copies still sit in `HaydnFormatsLS.td`
there — harmless only because nothing includes that file any more, so it must go
in the same commit or the defs collide. That relocation was never tied to the
encoding and could have been done at any point.

### 5.3 Database correction — done, but the `.xlsx` disagrees

`format_e_bit_layout_v2.json` left an operand unspelled for the four-register
MAC family. 76 mapping rows were corrected on this host (commit `8c3a9a9d241c`
regenerated from it; `simulator` re-pinned in `b8da0eb`).

Why nothing else caught it, which is worth understanding before trusting the
other gates:

* `verify_shape` only asks that every bit of an entry window be claimed. A blank
  field still claims its bits — as a constant zero.
* `--emit roundtrip` encodes and decodes, so it only ever sees what the encoder
  already believed. It agrees with itself.

So `X2MULA32` — a core MAC accumulate — was emitted with `rsd1` pinned to zero,
and every gate passed. The new check in `haydn_encoding.py::verify_operand_sets`
compares against the `Syntax` in `instruction_type_index.json` and refuses to
emit on mismatch.

**Outstanding:** `format_e_bit_layout_v2.xlsx` was not touched and now disagrees
with the JSON on those 76 rows. If the spreadsheet generates the JSON, the fix
must be made there and redelivered, or the next delivery silently reverts it.

#### The correction does not travel — check the pin first on any new host

The database lives *beside* the two repos and is version-controlled by neither,
so cloning both repos onto a fresh host gets you the corrected `.td` and the
re-pinned `GOLDEN_INPUTS.sha256` **paired with an uncorrected database**. This
happened: on this host `--check` refused to emit and named 16 instructions,
while `HaydnFormatEEncoding.td` in the tree was already the corrected one.

Diagnose it in one command — every other database file will match and only the
bit layout will not:

```sh
cd ~/haydn && sha256sum -c --ignore-missing \
    simulator/bundlesim/isa/database/generated/GOLDEN_INPUTS.sha256
```

Repair is now mechanical, and the assignment is forced rather than guessed:

```sh
python3 llvm/lib/Target/Haydn/utils/haydn_encoding.py \
    --database ~/haydn --fix-operand-mapping --write
#   "76 mapping row(s) repaired, 77 field(s) rewritten"
```

It solves, per row, the matching between the operands the Syntax names and the
fields whose alias list admits them, and refuses to write unless every row's
matching is unique. Verified: run against the uncorrected layout it reproduces
the pinned `8465132c…` **byte for byte**, and the three generated `.td` files
then come back identical to the committed ones. Re-running it is a no-op.

Note the alias lists differ **per (entry, unit, type)**. `X4SEL16` is the
example — six of its seven placements declare `src3(rs, rtd2)` and the seventh
declares `src3(rsd1, rtd2)`, so the seventh needs a *different* assignment
(`src1=rsd2, src2=rs, src3=rsd1`) than the other six. Correcting these rows by
copying a good row over a bad one produces a layout that `--check` rejects.

### 5.4 Regenerate the 589 lit expectations

Every encoding expectation changes at the switch. Regenerating from the new
toolchain only records what the new encoder did, so use `--emit roundtrip` as
the independent judgement: where a regenerated expectation and the round-trip
disagree, **the encoder is wrong**.

Be aware of what `--emit roundtrip` cannot see, though: it checks the encoder
against the decoder, so any defect symmetric across the pair passes. That is how
§ 5.3's blank MAC operand survived, and how § 6.10's missing ÷2 survived. For
anything with an outside reference — a scale the database states, a field the
linker patches, a flag the generic CodeGen layer reads — the round trip is not
evidence. The before/after `llc` diff in § 5.1 and the byte-level A/B against
the retiring spelling are, and at the switch itself `lld/test/ELF/haydn` and the
simulator suite are the only things standing outside the encoder's own opinion.

### 5.6 The load/store family has no format E encoding — BLOCKER

**Found after the C++ reached 0 errors. `llc` cannot compile a load or a
store.** This is the largest remaining item in the whole migration and § 5.2's
"and the compiler is the easy half" list did not contain it.

```
define void @copy(ptr %d, ptr %s) {
  %v = load i32, ptr %s
  store i32 %v, ptr %d
  ret void
}
```

```
LLVM ERROR: HaydnAsmPrinter: standalone unsupported BUNDLE child
(no getSlotKind / format) — refuse silent drop (B3.4). Bundle MIR:
BUNDLE 0, implicit-def $r2, implicit killed $r2 :: (load (s32) from %ir.s)
```

The B3.4 defence is doing its job — it refuses to drop an instruction it
cannot place — but ISel selects `LD32` and format E has **no member for it**.

#### How much of the ISA this is

Of 729 non-pseudo logicals, **684 have format E members and 47 do not**:

| n | Group | Status |
|---:|---|---|
| 15 | `_W` forms (`BEQ_W`, `JAL_W`, `ADDI32_W`, …) | Harmless. § 5.1 folded the C++ onto the base names but left the `.td` defs; nothing selects them. Delete with the switch. |
| 11 | `PseudoLong*` | Correct — pseudos are expanded before encode. |
| 3 | `ASR32`, `LSR32`, `SHL32` | No C++ or GISel references found; likely dead, **not confirmed**. |
| **18** | **load/store** | **The blocker.** |

The 18: `LD8 LDU8 LD16 LDU16 LD32 LD64` and their `_POST` / `_REG_M0S0LS`
variants, plus `ST8 ST16 ST32 ST64` and theirs. `LD32` has 35 C++/GISel
references, `ST32` 47, `LD64` 17, `ST64` 18.

#### It is a rename plus a range collapse, not a redesign

The database renamed the family by width class — `S_` for a GPR32 result, `D_`
for DR64 — and the correspondence is one to one:

| Bundle128 | format E |
|---|---|
| `LD32 rt, rs, simm16` | `S_LW_WITH_IMM rt, rs, simm6:$scaled_imm` |
| `ST32 rt, rs, simm16` | `S_SW_WITH_IMM rt, rs, simm6:$scaled_imm` |
| `LDU8` / `LD8` | `S_LBU_*` / `S_LBS_*` |
| `LDU16` / `LD16` | `S_LHWU_*` / `S_LHWS_*` |
| `LD64` / `ST64` | `D_LW_*` / `D_SW_*` (DR64) |

and each addressing mode is a separate logical — `_WITH_IMM`, `_WITH_REG`,
`_PRE_IMM`, `_PRE_REG`, `_POST_IMM`, `_POST_REG`, plus `_BREV_*` and `_CB_*`.
69 of the 110 LOADSTORE0/LOAD1 logicals produce a GPR32.

**The hard part is not the rename, it is `simm16` → `simm6` scaled.** Any
offset outside the scaled 6-bit range stops being an addressing mode and
becomes an address materialization plus a `_REG` form. That is a
frame-index-elimination and address-mode-selection change, not a table edit,
and it will move stack layout for every function with more than a handful of
locals. Expect § 5.4's expectations to move far more than a re-encode would
explain.

This is the same shape as § 7's AR reshape — the database reshaped a family and
ISel has to be retargeted — but roughly three times the size, and unlike the AR
work it is on the path of every compiled function rather than a DSP intrinsic
surface.

#### Status: retargeted, and llc compiles again

Done in `404226033703` (load/store + `ADDI32_W`) and `10514c6918f0` (the
member→logical fold). `llc` went from crashing on any function with a load to
**372 of the 430 CodeGen tests producing output**, via 271 at the halfway
point.

The opcode retarget was mechanical — the operand shapes are identical, and the
`_POST` / `_REG_M0S0LS` forms already carried `simm6:$scaled_imm`, so those
were pure renames. Two things were not:

* **The immediate changed units.** Bundle128's `simm16` held BYTES; format E's
  `simm6:$scaled_imm` holds ELEMENTS, because the hardware does
  `EA = rs + (imm6 << log2(width))`. A byte offset written into it compiles,
  encodes, disassembles and round-trips — and addresses `width` times too far.
  Demonstrated: `getelementptr i32, i32 5` (byte 20) emitted
  `s_lw_with_imm r2, r1, 20`, which addresses byte 80.

  The audit that bounded it: every `BuildMI`/`buildInstr` of a `*_WITH_IMM`
  form whose last `.addImm` is not literally `0`. 66 construction sites, 24
  already safe, 42 converted — 2 in GISel, 1 restructuring of
  `eliminateFrameIndex` so its arithmetic stays in bytes end to end, and 38
  through a new `haydnScaledLSImm(ByteOff, Width)` helper that **asserts
  divisibility rather than truncating**. Verified: `[8 x i32]` at `sp+8` with
  elements 0/3/7 emits immediates 0/3/7.

* **`ADDI32_W` had to go too**, because frame setup/destroy emits it and it has
  no member either. That is § 5.1's last `_W` fold, done by its own template.
  The duplicate `case Haydn::ADDI32` in `getExprFixupKind` is a compile error
  and is what surfaced it; both arms returned `FIXUP_HAYDN_LO20`, so there was
  no decision to make. **Only the mechanical half of § 5.1's checklist is
  done** — the llc-diff rename proof, the operand widening to `imm20`, and the
  libc + BSP chain are not.

##### `getHaydnFlexBaseOpcode` was folding on the spelling

Worth its own note because it is the third instance of one mistake. It
stripped only `_S0/_S1/_S2` and then consulted a hand-maintained `KnownBases`
table. Format E members are `<logical>_P<form><pos>_<UNIT>`, so the suffix
never matched, the member opcode came back **unchanged**, and every caller's
`Opc == Haydn::BEQ` compare silently failed.

Silently, except in `getBranchDestBlock`, which ends in `llvm_unreachable` —
branch relaxation aborted on **112 of the 430 tests**. The other ten callers
(analyzeBranch, the SMS loop recognizer, the hwloop bump matchers) just took
the wrong path quietly. It now delegates to `getHaydnLogicalBaseOpcode`, which
resolves by name search and works for either spelling, and the `KnownBases`
table is gone — it was a second copy of a fact the names already carry, and it
had already drifted (both the JAL and ADDI32 folds left a degenerate `X || X`
in it).

**The rule this keeps re-teaching: fold through the logical, never the
spelling.** § 5.2 applied it to the hwloop predicates in `c290615e3cb0`, § 4
applied it to the fixup kinds in `14754e31453b`, and this is the third site.
Grep for anything else that strips `_S` by hand.

##### Two defs described `sext32t64`, and the wrong one had the members

The last 52 failures. `SEXT_GPR32_TO_DR64` (`HaydnInstrInfo.td:601`,
`FmtALU64Unary<0x67,0x197>`, asm `sext32t64`) is what CodeGen emitted and has
**no** format E member; `SEXT32T64` (`HaydnInstrInfoAuto.td`, `isCodeGenOnly`
stub) is the database name and has seven. The stub was declared
`(outs GPR32:$rd)`, which is wrong against the database (`rtd =
SEXT32->64(rs)`) **and against its own generated members**
(`(outs DR64:$rtd), (ins GPR32:$rs)`) — harmless only while nothing selected
it. Corrected and retargeted.

The MIR blamed `MOV_GPR_TO_DR64`, which was a red herring: it expands fine,
*into* `SEXT_GPR32_TO_DR64`.

**Note how the last two instances escaped a systematic sweep.** Set-differencing
the `.td` logicals against the generated members and intersecting with
`grep 'Haydn::[A-Z]'` over the target reported **zero** CodeGen-reachable
memberless opcodes while two tests still failed:
`HaydnInstructionSelector` builds this one as a bare
`buildInstr(SEXT_GPR32_TO_DR64)` inside a `using namespace llvm::Haydn` scope,
so the `Haydn::` qualifier the sweep keyed on is simply absent. **Match the
bare name too**, or the sweep quietly under-reports.

#### Still open
* 3 shifts (`ASR32`, `LSR32`, `SHL32`) have no members and no references found;
  likely dead, unconfirmed.
* 15 `_W` and 11 `PseudoLong*` defs are memberless and harmless; delete with
  the switch.
* **"Compiles" is not "correct".** `HaydnTests` does not build, the 589 lit
  expectations are Bundle128's, and neither lld nor the simulator has been run.

#### Why nothing caught it earlier

Every gate that ran was blind to it, each for its own reason, and this is the
same list as § 5.3 and § 6.10:

* `--emit roundtrip` and `--check` only ever see the database's own members, so
  a logical the database never mentions is invisible to them.
* TableGen is happy: `LD32` is a well-formed def, it simply has no member.
* The C++ compiles: nothing references a member that does not exist.
* The assemble/disassemble round trip in § 5.2 used `add32`, which does have
  members. **`{ ld32 r1, r2, 0 }` fails to assemble** and would have shown this
  immediately.

The lesson for the rest of the migration: a green encoder gate says the
database is self-consistent, never that the compiler can reach it. The cheap
standing check is the set difference between the `.td` logicals and the
generated members — 47 today, and it should only ever shrink.

### 5.7 The unit axis is not threaded — first finding from the restored gate

`HaydnTests` compiles again as of `9e631c69a630` (578 errors to 0) and runs
**142 of 253**. The 111 failures are deliberately NOT regenerated: an
expectation rewritten to match the new code records only what the new code
did, and the whole value of this gate is that it disagrees. They need triage
one at a time — and the first one triaged found this.

```
HaydnBundleTest.DualLoadCanShareCycle
  packs two S_LW_WITH_IMM, gets occupancy P31|P32
  those members are S_LW_WITH_IMM_P31_LOAD1 and S_LW_WITH_IMM_P32_LOAD1
  -> the SAME UNIT, which § 3 forbids
```

The axis itself is correct. The test builds `Bundle<MCInst> B(&Fmts)` with
`MII = nullptr`, and § 7.1 already records the consequence:

> *`MCInstrInfo` is optional on `Bundle` / `tryAdd` /
> `enumeratePlacementAlternatives`. Without it no unit is claimed. Pre-RA
> scheduler paths that have no `MCInstrInfo` therefore keep slot-only
> behaviour — which is right today and **is a gap once format E is live**.*

**Format E is now live on this branch, so the gap is real**, and this is its
first concrete instance: a slot-only packer builds a bundle the hardware
cannot issue. It is silent in every other gate — the bundle is well-formed,
it encodes, it round-trips, and only the unit assignment is illegal.

The fix is § 7's own decided follow-up: thread `MCInstrInfo` through those
paths, or move the unit onto a generated per-member table that needs no name
lookup. The second is better and is the natural companion to § 6.9's
`--flags-from` work, since it would also remove the name-parsing dependency.

Note the shape of this: § 7.1 predicted the gap in prose and could not
demonstrate it, because under Bundle128 the axis was inert and no test could
reach it. Restoring `HaydnTests` after the switch is what turned a documented
risk into a reproducible failure. That is the argument for not deferring the
gate any further.

### 5.5 BundleSim side

Must land in the **same commit** as § 5.2, because it is what keeps the two
trees agreeing. See `simulator/TODO.md` for the full reasoning.

* `bundlesim/isa/database/generate_catalog.py` still reads the seven retired
  `slot*_instruction_list.json`. Port it to `instruction_type_index.json`, which
  carries the same `Syntax`/`Behavior` fields; its 682 instructions line up with
  the committed `.inc` one for one. `legal_slots` has no source in the new
  database — it came from the filename — and retires with the slot model.
* The AR family's six changed entries need their hand-written model and dispatch
  updated to the new arity: `isa/model/Slot0/slot0_ls.h`,
  `isa/model/Slot1/slot1_load.h`, `isa/dispatch/dispatch_slot0_ls.c`,
  `dispatch_slot1_load.c`. The dispatchers match on the literal mnemonic, so the
  `PLDWWUA` → `PLDWWUA_POST` rename alone would drop it on the floor.
* Regenerate `isa/dispatch/semantic_family_map.inc`, re-pin
  `isa/SEMANTIC_SNAPSHOT.sha256`.
* `generate_catalog.py` cannot run today, so do **not** unfreeze the catalog
  early: it would desync BundleSim from the compiler that exists.

---

## 6. Traps — read this section before touching anything

### 6.1 Why the earlier mechanical rewrite was reverted

Not a logical-vs-member semantics problem. It is **which operand class carries
an `EncoderMethod`**.

Logical `JAL` takes `calltarget`, whose `EncoderMethod` sends the target to
`getCallTargetOpValue`; it never reaches `getExprFixupKind`. The member
`JAL_S0` takes `calltarget_s0`, which has only a `DecoderMethod`, so it falls
back to `getMachineOpValue` and does land there. The previous attempt dropped
the member cases without adding the logicals, so `JAL` fell to `default` and six
call/branch relocation tests broke.

The fix (in `14754e31453b`) folds to the logical **and** adds `JAL`/`JALR`
explicitly, with a comment saying they are only reachable as members. Adding
them is safe precisely because the logical cannot arrive.

**`lld/test/ELF/haydn` is the suite that catches this class of mistake.** Run it.

### 6.2 Do not move a logical between formats

Retiring `ORI32_W` by declaring `ORI32` over `Fmt48_WideGPRImm` and deleting
`ORI32_W` **builds cleanly and passes all 589 lit tests** — and breaks eight
`HaydnBundleTest` cases that never mention `ORI32` (`ADD64`, `SLL64`, the
dual-load ones), plus trips the `canAdd` assertion in `Bundle::add`.

Changing which format a logical belongs to moves it in `CodeGenFormat`'s index
space and the packing tables shift underneath everything else. **Always run
`HaydnTests`, not just lit.**

Note the scope: this forbids **re-parenting a def to a different format class**.
It does not forbid editing the `.td` at all, and the stronger reading — "the
fold has to leave the `.td` alone and rename in C++" — is what let § 6.9 and
§ 6.10 through. Changing a `let` flag, a `Defs` list, or an operand class
*within* the class a def already has does not move anything in `CodeGenFormat`'s
index space; the branch and JAL folds did all three and `HaydnTests` stayed at
248/248 throughout. Run it after every such edit and let it answer the question,
rather than avoiding the edit.

### 6.3 Two TableGen roots

`Haydn.td` for everything, `HaydnAsmMatcher.td` for `-gen-asm-matcher` alone.
They must name the same member set. Forgetting the second one turns 30 errors
into 1739.

### 6.4 Composites need their own file

`-gen-asm-matcher` rejects a def whose `AsmString` opens with an operand, and a
composite has no mnemonic. Bundle128 solved this by keeping `BUNDLE128_FULL` in
a file the matcher root does not include; format E does the same with
`HaydnFormatEComposites.td`. `--emit composites` produces it.

### 6.5 Entry `Inst` must be padded to `Size * 8`

Entry windows are 45/41/31/31/27 bits. `Size` is a byte count and cannot express
that, and `InstructionEncoding` rejects an `Inst` narrower than `Size * 8`. The
generator zero-pads above the window so every field keeps its layout position.

### 6.6 `ninja` does not track the compiler binary — and this bites the BSP too

After rebuilding clang, `simulator/scripts/build_haydn_llvm_libc.sh` prints
`ninja: no work to do`, keeps the `libc.a` the **old** clang produced, then
reports `OK:` and installs a stale sysroot. Following the documented change gate
is not sufficient. Force it: `ninja -C "$BUILD_DIR" -t clean` first, then expect
~723 targets and ~1.5 min.

**The same applies to `simulator/build/bsp-stage`, and it is worse there.** The
BSP holds `crt0.o` and `libbundlesim_{crt,plat,sys}.a`; `cmake --build build`
will not rebuild them just because the compiler changed. Nothing warns you. The
stale objects link cleanly against freshly-compiled code — the two halves simply
carry different instruction spellings — and the program then dies 18 bundles in
with `MEMORY_FAULT / ALIGNMENT` at an address like `0x7fffff9d`, with the fault
PC pointing deep inside libc and no hint that the BSP is involved. It looks
exactly like a miscompile.

The tell is in the disassembly: `llvm-objdump -d program.elf` shows
`__bundlesim_start` and `__bundlesim_run_fini_array` using the old spellings
(`jal_w`, `jalr_w`) while `main` and `abort` use the new ones. Fix:

```sh
rm -rf build/bsp-stage && cmake --build build --target haydn_bsp
```

This cost most of an hour during the JAL fold and mimicked a real regression
convincingly enough to be worth checking *first* whenever the simulator suite
goes from green to broadly red.

### 6.7 `lldb_feature_matrix` fails under load

`simulator/scripts/lldb_feature_matrix.sh` uses a hardcoded port 23601 and waits
only 5 seconds for BundleSim's listen announcement. Under a loaded machine it
reports `timeout listen`. It has produced false failures three times; it always
passes on an idle machine.

`lldb_rsp_smoke` and `lldb_rsp_interrupt` had the same class of bug — a
`bind(0)`/`close`/hope-we-win-the-race port pick, measured at a **19% collision
rate** across concurrent processes — and were fixed in simulator `4b65727` by
passing `--lldb-port 0` and reading the port BundleSim announces on stderr.
`lldb_feature_matrix.sh` has not had that treatment and should get it.

### 6.12 `cmake --build build` does not build `HaydnTests`

The default target does not include the unit tests, so the § 1 sequence
"build, then run `build/unittests/Target/Haydn/HaydnTests`" happily runs a
**binary from a previous session** against tablegen output that has since been
regenerated. Nothing warns you; the timestamps are the only tell.

This produced **32 false failures** during the GISel promotion
(`HaydnBundleTest`, `HaydnBundleMaterializeTest`, `HaydnBundleFormatSolver`,
`HaydnAIEParityBundleTest`) — a spread that looks exactly like § 6.2's
"lit passes, HaydnTests breaks broadly on cases that never mention the changed
instruction", which is the most alarming signature in this document. The
proximate symptom was `enumeratePlacementAlternatives(Fmts, Haydn::ADD32, …)`
returning false while the regenerated `HaydnGenFormats.inc` plainly contained
`case Haydn::ADD32: return &AlternateInsts[53];` — the stale binary was reading
the old enum numbering.

Diagnose it before believing any broad `HaydnTests` failure:

```sh
ls -la --time-style=+%H:%M:%S build/unittests/Target/Haydn/HaydnTests \
                              build/lib/Target/Haydn/HaydnGenFormats.inc
```

If the `.inc` is newer, the run means nothing. Always:

```sh
cmake --build build -j"$(nproc)" --target HaydnTests
```

§ 6.2 makes `HaydnTests` the gate that decides whether a `.td` edit was safe.
That gate is only as good as the binary, and this is the second time in this
migration that a build system silently kept a stale artifact (§ 6.6 is the
first). Assume nothing rebuilds itself.

### 6.13 Editing `haydn_dsp.h` does not change what the tests see

`clang/test/Headers/*.c` say `#include <haydn_dsp.h>`, which resolves to the
**resource-dir copy** at `build/lib/clang/22/include/haydn_dsp.h`, not the
source file you just edited. Withdrawing eleven `AE_*` macros and re-running
`clang/test/Headers` reported **143/143 passing** — against the old header.
The four tests that should have failed only did so after:

```sh
cmake --build build -j"$(nproc)" --target clang-resource-headers
```

This is the third member of the same family: § 6.6 (ninja keeps a stale
`libc.a` and a stale BSP), § 6.12 (`cmake --build build` does not build
`HaydnTests`), and now this. In every case a build system silently served a
previous artifact and the test result was meaningless — twice in the
reassuring direction, which is worse. **Whenever a change to generated or
copied inputs produces no test movement at all, check the artifact's timestamp
before believing it.**

`-I clang/lib/Headers` on a manual `clang` invocation *does* pick up the source
copy, so a hand check and the test suite can disagree — that is the tell.

### 6.14 `grep -c 'error:'` is not the error count

Every number in § 5.2 is **distinct source locations**. A raw count of `error:`
lines is inflated by translation-unit multiplicity, and the inflation is
concentrated exactly where the switch does its damage — in headers.

Measuring `-mc` before the join gave **118** `error:` lines but **24** distinct
locations. `HaydnBundlePlan.h` alone accounted for 90 of the 118: 6 distinct
errors, each reported by all 15 translation units that include it. Read
carelessly, that looks like the switch is four times worse than documented and
that `f9ed0ff6365f` made things worse rather than better. It did not.

```sh
cmake --build build -j"$(nproc)" -- -k 0 > /tmp/build.log 2>&1
grep -oE '^/home/[^ ]+:[0-9]+:[0-9]+: error:' /tmp/build.log | sort -u | wc -l
```

Two further cautions on the same measurement:

* **`-k 0` is required** and is in § 1's baseline for this reason. Plain
  `cmake --build` stops at the first failing target and showed 7 where there
  were 23.
* Even with `-k 0`, ninja stops when it *cannot make progress*, so errors
  hidden behind a failed target are still not counted. The number is a lower
  bound on a broken tree, and it only becomes exact at zero.

This is the fourth member of the family in § 6.6 / § 6.12 / § 6.13: a tool
answered a slightly different question than the one being asked, and the answer
looked plausible.

### 6.8 Adding a regression case trips the manifest gate

`scripts/check_regression_manifest.sh --write` regenerates it. It also rewrites
`YARPGEN_MANIFEST.json`'s timestamp with no content change — revert that to keep
the diff meaningful.

---

### 6.9 The narrow `_S0` members carry no instruction-property flags

Bundle128's `_W_S0` members were wrapped in `let isBranch = 1, isTerminator = 1`
(and `isCall`/`isBarrier`/`isIndirectBranch` plus the caller-saved `Defs` for
the call forms). **The narrow `_S0` members had none of it**, because nothing
ever selected them.

This matters because `materializeMultiOpcodeInstrs` commits the logical to its
member before AsmPrinter runs, so the member's `MCInstrDesc` is the one the
generic CodeGen layer reads. With no `isTerminator`,
`MachineBasicBlock::terminators()` comes back empty,
`AsmPrinter::isBlockOnlyReachableByFallthrough` concludes a block that is in
fact a branch target is reachable only by fallthrough, and the label is emitted
as a `// %bb.1:` comment while the branch still references `.LBB0_1`. The
assembler then rejects its own compiler's output:

```
<unknown>:0: error: Undefined temporary symbol .LBB0_1
```

Only two of 589 lit tests caught it, and only because those functions happened
to have a block with exactly one predecessor that was also its layout
predecessor.

**CLOSED.** `--emit td` now takes `--flags-from`, the output of
`llvm-tblgen --dump-json`, and copies each logical's properties onto every
member it expands. It refuses to emit without it rather than producing
flagless members, because flagless is silently wrong rather than incomplete.

Reading TableGen's own output beats the two alternatives. A text scan of the
`.td` would have to re-implement `let ... in` block resolution; deriving from
the database's `Behavior` would find the ten branches and JAL/JALR but miss
what turned out to be most of the set — of the **27 database instructions
carrying properties**, twelve are comparisons whose only property is
`Defs = [SFR]` (`SEQ64`, `X2SEQ32`, `X4SLE16`, …), plus `CSRW`, `ZERO_SFR`,
`MOVEGPR2SFR` and the three `SET_HWLOOP` forms. Those are LLVM-side modelling
facts, not database facts.

`hasSideEffects` is deliberately not copied: TableGen infers it when a def
leaves it unset, so writing the resolved value onto every member would freeze
the inference rather than reproduce it.

Verified end to end. The generated file went from 0 property lines to 189
(34 `isBranch`, 37 `isTerminator`, 6 `isCall`, 3 `isBarrier`, 3
`isIndirectBranch`, 8 `isNotDuplicable`, 98 `Defs`) with the diff being pure
insertion. Dry-running § 5.2's include switch on a throwaway root parses
through `-gen-instr-info` cleanly, and the emitted table has
`BEQ_P20_ALU0` as `MCID::Branch|MCID::Terminator` and `JAL_P20_ALU0` as
`MCID::Call` with 17 implicit defs.

### 6.10 The narrow `_S0` members had the wrong immediate scaling

§ 5.14 D1 puts branch offsets in 2-byte units. The `_W_S0` members took
`brtarget_wide_{ri12,i12,i20}`, whose encoder/decoder pair does the ÷2. The
narrow `_S0` members took plain `simm12` / `calltarget_s0`, which store the byte
offset **raw**: `beq r1, r2, 32` encoded 32 where `beq_w r1, r2, 32` encoded 16,
and `jal` had half the reach of `jal_w` (±512KB against ±1MB).

Nothing caught it because the defect is symmetric. `simm12` decodes raw too, so
the round trip agrees with itself, and symbolic targets were unaffected — the
fixup carries `ValueShift = 1` regardless of the operand class, so only literal
offsets in hand-written asm and the *disassembly* were wrong. The byte-level A/B
in § 5.1 step 5 is the only thing that surfaces it.

Format E's generated members take plain `simm12` / `simm20` as well, so the
same question is open there — **still open; § 6.9 is done, this is not.**

**It is wider than branches.** The same "generated members take the plain
operand class" pattern costs an `EncoderMethod` wherever the Bundle128 member
had one. `MOVEI_H`/`MOVEI_L` are the second instance found: the Bundle128
member `MOVEI_H_S0` takes `simm32_movei`, which carries
`getSImmOpValueXStepWide<32,0,…,FIXUP_HAYDN_32>`, while the generated
`MOVEI_H_P20_ALU0` takes plain `simm32`, which has none. A symbolic operand
therefore falls back to `getMachineOpValue` instead of emitting the fixup —
§ 6.1's trap, in the other direction. **Audit every generated member whose
Bundle128 peer had an `EncoderMethod` before trusting the switch**; grep the
retired `HaydnFormats*.td` (they are in git history, deleted by the switch
commit) for the operand classes that carry one, rather than assuming branches
were the only family affected.

**Correction: the database has no `branch_scale` field.** Earlier revisions of
this file said it did and that the generator merely had to consume it. It does
not — grep `instruction_type_index.json` and it returns nothing. `branch_scale`
is BundleSim's, set in `generate_catalog.py` from a **hardcoded** Python set
(`SCALED_CONTROL_FLOW` → 2, `JALR` → 1, `HWLOOP` → 4).

The distinction is still derivable from the database, just not by reading a
field. `Behavior` states the addressing mode, and the constraints document
(line 55) states that bundle addresses are 2-byte aligned:

| `Behavior` shape | addressing | scale | matches |
|---|---|---|---|
| `PC = … PC + imm …` | PC-relative, bundle units | 2 | **exactly 11**: the ten branches + `JAL` |
| `PC = rs + imm` | register-relative, byte units | 1 | **exactly 1**: `JALR` |
| `PC + (uimm6_offset1 << 2)` | stated outright | 4 | the `SET_HWLOOP` family |

That reproduces BundleSim's hardcoded table row for row, which is worth doing
for its own sake: it derives the answer instead of remembering it, and it
independently confirms the hardcoded set is right.

Implementation note: the ÷2 belongs in an operand class carrying
`getSImmOpValueXStepWide<N,/*Shift*/1,…>`, the way `brtarget_wide_i12` does it
for Bundle128. Those existing classes cannot simply be reused — their fixup
kinds carry Bundle128 field geometry — so format E needs its own, which ties
this to the fixup-geometry work in § 5.2 rather than making it a pure
generator change like § 6.9 was.

### 6.11 BundleSim reads mnemonics, so retiring a spelling changes its behaviour

BundleSim consumes `llvm-objdump` text, not bytes. Branch immediates reach it as
**byte offsets**, and the halfword range check divides before comparing against
the 12- or 20-bit field. That divisor used to be keyed off a hardcoded list of
eleven `_W` mnemonics in `bs_instruction_normalize`.

The moment the compiler stopped emitting `bnez_w`, the list stopped matching,
every branch range-checked its byte offset against 12 bits, and anything past
±2048 came back `ILLEGAL_INSTRUCTION`: **180 of 221 ctest cases**, mostly
yarpgen, with a message that names the immediate and the field width and gives
no clue that a spelling changed.

Fixed in simulator `bdf14d7` by reading `desc->branch_scale` from the golden
catalog instead. Note it was already correct there — 2 for the ten branches and
JAL, 1 for rs-relative JALR — so this was one hardcoded list disagreeing with a
generated table that already knew the answer. **Grep the simulator for other
mnemonic literals before retiring the next spelling**; `dispatch_*.c` matching
on literal mnemonics is called out in § 5.5 for exactly this reason.

## 7.1 The unit model — and why the implementation must stay data-driven

**This is a hardware model that is expected to move again. Nothing in C++ may
encode where it currently sits.**

| Model | unit → slot | Consequence |
|---|---|---|
| Bundle128 (old) | each unit pinned to ONE slot | unit and slot are the same fact; the migration elided units entirely and said "this instruction only goes in slot N" |
| format E (now) | many-to-many, but **partial** | an instruction goes anywhere its unit is still free |
| next (expected) | some units 1 slot, some 2, a few all 3 | full flexibility costs too much hardware; the balance point moves |

**The delivered layout is already at a balance point, not the flexible
extreme.** 7 units × 5 entry positions = 35 pairs; the bit layout admits **18**:

| unit | positions | which |
|---|---:|---|
| ALU0 | 4 | 2e-e0, 3e-e0, 3e-e1, 3e-e2 |
| MAC0 | 3 | 2e-e0, 3e-e0, 3e-e1 |
| LOAD1 | 3 | 2e-e1, 3e-e1, 3e-e2 |
| ALU2 | 2 | 3e-e0, 3e-e2 |
| MAC1 | 2 | 2e-e1, 3e-e2 |
| LOADSTORE0 | 2 | 2e-e0, 3e-e0 |
| ALU1 | 2 | 2e-e1, 3e-e1 |

ALU2 never appears in the 2-entry form at all. So **the (unit, position)
relation is already data in `format_e_bit_layout_v2.json`**, surfaced as the
placement-index enumeration the generator emits. Re-delivering a layout and
regenerating is the whole of "customising the balance point" — provided C++
never hardcodes a unit, a position, or a relation between them.

### What is implemented

Two independent occupancy axes on the packer. **Slot** says where in the
bundle; **unit** says which hardware serves it; neither implies the other:

* same unit, different entries — `ADD32_P30_ALU0` / `ADD32_P31_ALU0` — a bundle
  may hold **one**, and the slot check alone would take both;
* same entry, different units — `ADD32_P30_ALU0` / `LD32_P30_LOADSTORE0` — also
  only one, but for the *other* reason, and the unit check alone would take
  both.

`PlacementAlternative::Units` and `CycleMember::Units` carry the member's unit;
`CycleState::OccupiedUnits` and `Bundle::OccupiedUnits` accumulate it.
`tryAdd` rejects an alternative whose unit is taken **and tries the next one** —
that is what makes "put it wherever its unit is free" the placement rule rather
than a failure. A committed member has no such choice and is rejected outright.

The unit is read off the member NAME (`haydnMemberUnitFromName`), sharing the
closed unit table with `stripHaydnMemberSuffix`. A generated per-member unit
table would be a better source and is the natural follow-up to § 6.9's
`--flags-from` work; the name is used because it is available today and is
already the tested carrier of the member→logical fold.

### What is NOT yet true, and must not be assumed

* **The axis is inert under Bundle128** and therefore **not exercised by any
  live path**. Bundle128 members carry no unit — correctly: its slot model
  pinned one unit per slot, so the spelling never had to say — and
  `Units == 0` conflicts with nothing. The mechanism is covered only by
  `HaydnMemberUnit.*` in `HaydnMCFormatsTest.cpp`, which feeds it format E
  spellings directly. **Treat it as untested against real packing until the
  switch lands**, and expect the first real bundles to be where it earns or
  loses trust.
* **`MCInstrInfo` is optional** on `Bundle` / `tryAdd` / `enumeratePlacementAlternatives`.
  Without it no unit is claimed. Pre-RA scheduler paths that have no
  `MCInstrInfo` therefore keep slot-only behaviour — which is right today and
  **is a gap once format E is live**: a pre-RA cycle could be declared feasible
  on slots that post-RA then rejects on units. Either thread `MCInstrInfo`
  through those paths or move the unit to a generated table that needs no name
  lookup.
* **The itinerary model already reserves units too — but it is no longer the
  authority.** `HaydnFormatESchedule.td` defines 7 `U_*` FuncUnits and
  `InstrItinData<Unit_MAC0_L2, [InstrStage<1, [U_MAC0]>], [2]>`, and the
  itinerary class names the *set* of units that can serve it (`Unit_ALU0_L1` vs
  `Unit_ALU0ALU1ALU2_L1`) — the same fixed↔flexible spectrum, expressed on the
  logical. Two mechanisms describe one constraint; **placement wins** (§ 7).
  The itinerary is on the logical and therefore cannot express a logical whose
  members sit on different units, which is the ordinary case here, so it is the
  less expressive of the two. What still has to happen: the itinerary's unit set
  should become *derived* from the members rather than independently authored,
  or the two will drift silently — and until it is, do not read a `U_*`
  reservation as a legality statement.

  A live instance is already in the tree: the seven AR logicals cherry-picked
  onto `haydn-formate-switch-mc` carry `let Itinerary = Slot012_ALU`, a
  Bundle128 *slot* itinerary, while their format E members carry units in their
  names. It compiles because `HaydnSchedule.td` is untouched. Picking the
  replacement class is exactly the derivation above and should not be done by
  hand.

## 7. Decided, do not relitigate

* **Unit exclusion: the PLACEMENT is authoritative, not the itinerary.** The
  member's unit — `PlacementAlternative::Units`, `Bundle::OccupiedUnits`,
  `tryAdd` — decides whether a bundle is legal. The itinerary's `U_*` FuncUnit
  reservation is derived and advisory, and must not be read as a legality
  statement. The reason is expressiveness, not preference: the exclusion rule is
  about *which member was chosen*, and only the member can say — a logical
  carries one itinerary but has members on several units, so the itinerary
  cannot state the constraint at all. Follow-ups this decides, both open:
  derive the itinerary's unit set from the members instead of authoring it
  separately, and close § 7.1's `MCInstrInfo`-optional gap so pre-RA paths stop
  being slot-only.
* **The AR intrinsic prototypes change; the source break is accepted.** The
  re-delivered database dropped the second base register and the direction
  select and made the post-increment a fixed +8. `pldwwua` → `PLDWWUA_POST`
  (prototype unchanged), `wbarwua` loses `dir_sel`, the four `d_*ua_post` forms
  lose `stride` and `dir_sel`. Done on the WIP branch: `.td` logicals, seven
  relocated to `HaydnInstrInfo.td`; `BuiltinsHaydn.td` prototypes; GISel
  lowering. Losing the writeback costs nothing — ISel already marked that def
  `Dead`.
* **The BundleSim catalog stays frozen until § 5.2 lands.** It currently matches
  the compiler that exists; unfreezing early desyncs them.
* **Slot placement is not a product gate.** Do not enable `--enforce-slots`.
* **When a `_W` fold finds the narrow and wide defs disagreeing, the wide one
  wins.** This is not a preference, it is what § 5.1's premise means: the
  database keeps one instruction and it is the wide shape. Applied so far to the
  branch offset scaling, JAL's reach, `hasSideEffects`/`Defs = [SFR]` on the
  conditional branches, and JALR's `isBarrier`. The narrow defs were never
  selected by CodeGen, so where they differ they are simply unmaintained.
* **`jal` emits `R_HAYDN_WIDE_CallSImm20`, and the two narrow relocations are
  retired.** `FIXUP_HAYDN_CallSImm20` and `FIXUP_HAYDN_BranchSImm16` are no
  longer selected by anything as of `837f8e079dce`. The fixup kinds, their
  geometry rows in `HaydnRelocLayout.cpp` and their `R_HAYDN_*` ELF mappings all
  stay, so objects built by an older toolchain still link — but nothing produces
  them any more, and `reloc-callsimm20.s` / `haydn-relocations.s` /
  `thunk-addend.s` now assert the WIDE name.

---

## 8. Open questions needing a human

1. **`haydn_dsp.h` has not been updated.** The builtin prototypes changed on the
   WIP branch but the header's wrappers still pass four arguments
   (`haydn_ae_la64_step(__ar, __p, 8, 0)` → `haydn_d_ltwua_post(p, ar, stride,
   dir)`). It will not compile until this is done, and how to do it depends on
   question 2.
2. **~~Ten~~ ELEVEN public `AE_*` macros have no format E equivalent —
   ANSWERED: withdraw for now.** They pass either `dir = 1` or a runtime
   stride, neither of which the new hardware has. Every other `AE_*` already
   passes `stride = 8, dir = 0` and is unaffected.

   **Decision: withdrawn, deliberately reversibly.** Software emulation with
   ordinary loads and pointer arithmetic remains the open alternative; nothing
   about it is foreclosed. Each macro now expands to
   `__HAYDN_AE_WITHDRAWN_STMT(sym, needs)`, a `_Static_assert(0)` naming the
   symbol and the missing capability.

   That idiom is deliberately NOT the existing `__HAYDN_AE_UNSUPPORTED`:
   that one is the opt-out for *inexact mappings* and is disarmed by
   `__HAYDN_ALLOW_INEXACT_AE`. This is *missing hardware*, so it has no escape
   hatch, and `haydn-compat-la-ric.c` has a RUN line asserting that
   `__HAYDN_ALLOW_INEXACT_AE` does not re-enable it.

   **It was eleven, not ten.** `AE_LA32X2F24_RIP` delegates to
   `__AE_LA32X2_RIP_{3A,4A}` and needs `dir = 1` exactly like its peers.
   `haydn_ae_audit.py` missed it because **the candidate list was hardcoded to
   ten names** — the delegation-following logic was fine, it simply never ran
   on a name nobody had typed in. The script now derives its candidates from
   the header (588 macros scanned) and reports the 11 as withdrawn.

   Ground truth for this class of question is the preprocessor, not a script:
   expand every `AE_*` at every arity with `clang -E` and look at what actually
   reaches an AR helper with `dir = 1` or a non-literal stride. That is how the
   eleventh was found, and it now agrees with the audit exactly.

   Both `HAYDN_COMPAT_TIER_AE_LA16X4_RIC` and `_AE_LA32X2_RIC` moved from
   `HAYDN_COMPAT_EXACT` to `HAYDN_COMPAT_UNSUPPORTED`; the other nine never
   carried a tier tag. Note the contrast the taxonomy test now pins:
   **`AE_L16X4_RIC` stays EXACT** — it reverses via a negative `D_LDW_CB`
   stride, not the direction select, so it is unaffected.

   Four clang tests asserted the old lowering
   (`haydn-compat-la-ric.c`, `haydn-compat-exact-value-ref.c`,
   `haydn-compat-tier-taxonomy.c`, `haydn_dsp.c`). They now assert the
   withdrawal instead, which is stronger than deleting them: the property the
   old tests existed to protect was "RIC must not silent-alias forward IC",
   and failing to compile is the strongest possible form of that. The forward
   `_IC` probes are kept as the live contrast.

   The handout at `~/haydn/haydn-ar-intrinsics-change.html` still asks users
   which they depend on; that answer decides whether the withdrawal becomes
   emulation or becomes permanent.

   Audited against the header as it stands (the *last* `#define` wins, and
   several of these are redefined two or three times, so the early definitions
   are misleading — `AE_SA32X2_RIP` is plain C at line 5245 and then goes back
   onto the AR helper at 7089):

   | Macro | Needs | Final `#define` |
   |---|---|---|
   | `AE_LA16X4_RIC` | `dir = 1` | 3718 |
   | `AE_LA32X2_RIC` | `dir = 1` | 3734 |
   | `AE_SA32X2F24_RIP` | `dir = 1` | 7081 |
   | `AE_SA32X2_RIP` | `dir = 1` | 7089 |
   | `AE_SA64NEG_FP` | `dir = 1` | 683 |
   | `AE_SA16X4_IP_X` | runtime stride | 735 |
   | `AE_SA32X2_IP_X` | runtime stride | 743 |
   | `AE_LA16X4_RIP` | `dir = 1` **and** runtime stride | 3752 |
   | `AE_LA32X2_RIP` | `dir = 1` **and** runtime stride | 3775 |
   | `AE_SA16X4_RIP` | `dir = 1` **and** runtime stride | 4507 |

   Note the split inside the last three: their **3-arg** overload passes
   `stride = 8, dir = 1`, so only the **4-arg** spelling needs a runtime
   stride. If the answer is "emulate", the 3-arg forms collapse into the same
   shape as the five `dir = 1` ones and only five macros need a real software
   loop. Re-run the audit after any edit — it resolves the overload chains and
   reads the final definition, which grepping does not:

   ```sh
   # the cheap check — derives its candidates from the header now
   python3 llvm/lib/Target/Haydn/utils/haydn_ae_audit.py
   #   588 AE_* macros scanned; 11 need attention.  (all WITHDRAWN)

   # ground truth, when the answer matters: expand everything and look
   cmake --build build --target clang-resource-headers   # REQUIRED — see § 6.13
   ```
3. **The `.xlsx` twin of the bit layout** — see § 5.3.
4. **CB-130** (`bundlesim_reg_cb44_o2_stale_cond_max_reduce`) is the sole
   simulator ctest failure, a GISel legalizer assert on
   `<2 x s1> = G_BUILD_VECTOR` at `LegalizerHelper.cpp:5246`. Unrelated to this
   migration; tracked separately.
5. **Only format E's bit layout exists on this host.** The constraints document
   lists five bundle sizes (96/64/48/32/16-bit); the other four have no layout
   and cannot be invented.

---

## 9. File map

| Path | Role |
|---|---|
| `llvm/lib/Target/Haydn/Haydn.td` | Main TableGen root |
| `llvm/lib/Target/Haydn/HaydnAsmMatcher.td` | Second root, `-gen-asm-matcher` only |
| `llvm/lib/Target/Haydn/HaydnFormatEEncoding.td` | Generated members (3578) |
| `llvm/lib/Target/Haydn/HaydnFormatEComposites.td` | Generated `BUNDLE_E2` / `BUNDLE_E3` |
| `llvm/lib/Target/Haydn/HaydnFormatESchedule.td` | Generated unit itineraries — **already live** |
| `llvm/lib/Target/Haydn/HaydnFormats{ALU32,ALU64,LS,LD,MAC}.td` | Bundle128 members, to delete (7615 lines) |
| `llvm/lib/Target/Haydn/HaydnCompositeFormats.td` | `BUNDLE128_FULL`, to delete |
| `llvm/lib/Target/Haydn/utils/haydn_encoding.py` | The generator, its gates, and `--fix-operand-mapping` |
| `llvm/lib/Target/Haydn/utils/haydn_ae_audit.py` | Which `AE_*` macros still need the AR args format E drops (§ 8 Q2) |
| `llvm/lib/Target/Haydn/MCTargetDesc/HaydnMCFormats.{h,cpp}` | `stripHaydnMemberSuffix`, `getHaydnLogicalBaseOpcode`, slot geometry |
| `llvm/lib/Target/Haydn/MCTargetDesc/HaydnMCCodeEmitter.cpp` | Fixup kinds, composite encode |
| `llvm/lib/Target/Haydn/Disassembler/HaydnDisassembler.cpp` | Parcel decode, `SlotGeo` |
| `llvm/unittests/Target/Haydn/HaydnMCFormatsTest.cpp` | Member-suffix tests, both spellings |
| `clang/include/clang/Basic/BuiltinsHaydn.td` | AR prototypes (changed on WIP) |
| `clang/lib/Headers/haydn_dsp.h` | `AE_*` surface — **not yet updated** |
| `simulator/bundlesim/isa/database/generate_catalog.py` | Cannot run; port to the new database |
| `simulator/bundlesim/isa/database/generated/GOLDEN_INPUTS.sha256` | Database pin |
