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
| `llvm-project` | `haydn` | `318248c8d1aa` | **yes, fully green** |
| `simulator` | `master` | `bdf14d7` | yes, green except CB-130 |

Nothing is pushed. `fork/haydn` is still at `837f8e079dce`, four commits behind.

`haydn` is the trunk. Everything on it is green and committed; work from it.

**`haydn-formate-switch-wip` no longer exists** — not locally and not on the
`fork` remote, so it was never pushed and is gone with the host that held it.
It held only the two root `.td` include switches plus the AR reshape, and this
document was written to make it re-derivable: the include switches are § 5.2's
first two bullets and the AR reshape is § 7's first bullet. Re-derive rather
than go looking for it.

Earlier revisions of this file named `haydn` heads `9802930e5fde` and
`135c38e2f1bc`. Neither hash exists in the repo; the branch was rebased before
it was pushed. The commit *subjects* in § 4 are the durable reference, and the
hashes there are the ones on the pushed branch.

### Green baselines — verify these before and after every step

```sh
# llvm-project
cmake --build build -j"$(nproc)"                     # 0 errors
build/bin/llvm-lit -s llvm/test/CodeGen/Haydn llvm/test/MC/Haydn
#   589 discovered: 573 pass, 8 XFAIL, 8 unsupported, 0 fail
build/unittests/Target/Haydn/HaydnTests               # 248/248
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
python3 .../haydn_encoding.py --database ~/haydn --emit td         -o .../HaydnFormatEEncoding.td
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
* `HaydnAsmMatcher.td` — **the same change**. This is a second TableGen root
  (`CMakeLists.txt` sets `LLVM_TARGET_DEFINITIONS` for `-gen-asm-matcher`
  only). Missing it is what produced 1739 errors instead of 30.

Then, by category:

| n | What | Notes |
|---|---|---|
| 5 | `BUNDLE128_FULL` → `BUNDLE_E2` / `BUNDLE_E3` | `HaydnAsmParser.cpp:878`, `HaydnAsmPrinter.cpp:696`, `HaydnMCCodeEmitter.cpp:316,501`, `HaydnMCFormats.cpp:333`. **The real design work**: the encoder must now *choose* a composite by entry count, and `HaydnAsmPrinter` fills the composite operand dag by slot index. |
| 4 | Decoder tables | `DecoderTableS048/S140/S240` → `DecoderTableP2048/P2148/P30../P31../P32..`; `DecoderTableBundle128128` → the two `FormatE2`/`FormatE3` tables. Pick the composite from the header: `Inst{3}`. `HaydnDisassembler.cpp:252,267,282,477`. Its `SlotGeo Slots[3]` must become 2-or-3. |
| 6 | GISel selects members directly | `MOVEI_H_S0`/`MOVEI_L_S0` at `HaydnInstructionSelector.cpp:3826`; `X4CMUL16{,S,_F2,S_F2}_S1` at 4808-4811. The comment says the Auto.td logicals are `HaydnInst` stubs that AsmPrinter drops as `MCID::Pseudo`. Format E has members for all of them (`MOVEI_H` 2, `X4CMUL16` 5), so the clean fix is to make the logicals real and let the alternates auction place them. |
| 6 | hwloop predicates naming `_S0` | `HaydnFixupHwLoops.cpp:125,129,133` and `HaydnMCInstLower.cpp:31`. Fold through `getHaydnLogicalBaseOpcode` — `HaydnFixupHwLoops` already has `const HaydnInstrInfo &TII`; `HaydnMCInstLower::Lower` can reach MII via the `MachineInstr`. |
| 2 | `CSRW_S0` normalization | `HaydnMCCodeEmitter.cpp:556-560`. Should disappear with § 5.1's `CSRW_W` work. |

**Two generator gaps block this step, and neither shows up as a C++ error.**
Both were found the hard way during the `_W` fold, on Bundle128's narrow members
— the generated format E members have the same shape, so they will reproduce
them across all 3578:

* **No instruction-property flags** (§ 6.9). `haydn_encoding.py` emits no
  `isBranch` / `isTerminator` / `isCall` / `isBarrier` / `isIndirectBranch` and
  no caller-saved `Defs`. Without them AsmPrinter suppresses branch-target
  labels and the compiler's own output stops assembling. The flags are a
  property of the logical, so the generator can copy them from the logical it is
  expanding rather than inventing a table.
* **No immediate scaling** (§ 6.10). Members take plain `simm12` / `simm20`
  where the offset is stored in 2-byte units. The database already distinguishes
  the cases — `branch_scale` is 2 for the ten branches and JAL, 1 for
  rs-relative JALR — so this is available, just not consumed.

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

**The format E generator emits no property flags at all** — grep
`haydn_encoding.py` for `isBranch|isTerminator|isCall|isBarrier` and it returns
nothing, and `BEQ_P20_ALU0` in the generated `.td` has none. All 3578 members
need them before § 5.2, or every function with a conditional branch produces
output that does not assemble. This is the single largest known gap in the
generator.

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

Format E's generated members take plain `simm12` / `simm20` as well, so the same
question is open there: the database gives `BEQ` and `JAL` `branch_scale = 2`
and `JALR` `branch_scale = 1`, and the generator does not currently express that
distinction.

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

## 7. Decided, do not relitigate

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
2. **Ten public `AE_*` macros have no format E equivalent.** They pass either
   `dir = 1` or a runtime stride, neither of which the new hardware has. Every
   other `AE_*` already passes `stride = 8, dir = 0` and is unaffected. Emulate
   in software with ordinary loads and pointer arithmetic, or drop them? The
   handout at `~/haydn/haydn-ar-intrinsics-change.html` asks users which they
   depend on. **Awaiting a decision — nothing here has been changed.**

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
   python3 llvm/lib/Target/Haydn/utils/haydn_ae_audit.py
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
