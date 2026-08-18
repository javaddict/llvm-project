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

| Repo | Branch | Head | State |
|---|---|---|---|
| `llvm-project` | `haydn` | *the tip — do not trust a hash here* | **The only branch, and a single linear line — no merge commits.** The format E switch landed here on 2026-08-14. Fully green: llvm lit 604/620 zero failures, `HaydnTests` 256/256, clang 1428/1475 zero failures, `--check` + round-trip 3686/3686, BundleSim ctest 226/226, gcc-c-torture 1417 PASS / 0 FAIL at -O3, CoreMark e2e PASS. |
| `simulator` | `master` | `417b0c2` **pushed** (`origin` IS javaddict/bundlesim here — unlike `llvm-project`, where `origin` is upstream and only `fork` may be pushed) | ctest **226/226**. The § 5.5 executor port is done; the old "41/221, the rest failing in the un-ported executor" is retired. |
| `llvm-project` | ~~`haydn-formate-switch-mc`~~ | was `6107f7edec50` | **DELETED 2026-08-14.** Nothing was lost: every commit it carried is now part of `haydn`'s single line (it was first merged, and the merge was then linearized away). Proven before deleting — `git rev-list --count fork/haydn-formate-switch-mc ^fork/haydn` was **0**. Its old SHAs no longer name commits in this history; they live in `backup/haydn-premerge-linearize-20260814`. |
| `llvm-project` | ~~`haydn-formate-switch-wip`~~ | was `6f0d97cf0e10` | **DELETED 2026-08-14**, but it was NOT simply "subsumed" — see below. 20 of its 21 commits past `backup/wip-preformate-20260807` came in with `-mc`; its **tip did not**, and is preserved as the tag `backup/wip-parked-20260814`. |

**`haydn` is the trunk, and since 2026-08-14 that is finally true of the code
as well as the prose.** Work from it.

#### The split this closed, because the shape of it is worth knowing

Before they were joined the two branches had diverged **by file, not by feature**: all
58 of `haydn`'s commits since the merge base were `[docs]` touching only this
file and `TODO.md`, while all 92 on `-mc` carried the code, the tests and the
ledger — 418 files. So the trunk held the narrative and the branch held the
work, and "work from `haydn`" would have built a compiler with none of the
format E switch in it. An earlier revision of this section said "**this file
only exists on `haydn`**; the copy on `-mc` is 500 lines behind" — which was
true, and was the visible half of a split that also ran the other way.

That accident is also why joining them was clean: `-mc` never touched this file
or `TODO.md` after the merge base and `haydn` never touched
`OPEN-COMPILER-BUGS.md`, so each file's authoritative version won on its own and
nothing conflicted. **Both documents now live on `haydn`.** There is no longer a
branch to cross-check before editing either one.

#### The history is linear, and what that cost

The join was first recorded as a merge commit and then **linearized**: all 153
commits replayed in author-date order onto the merge base, so the history is one
line with **no merge commits**. Two things make that safe to have done and worth
knowing if it is ever done again:

* **The content did not move.** The replayed tree is byte-identical to the tree
  the gate had already passed — verified by comparing tree hashes, not by
  re-reading files. The gate was then re-run on the linear history anyway.
* **Sorting purely by date does not work.** One commit on the code side
  (`[Haydn] Absorb the database's AR shapes onto the format E switch`) is
  authored a day *before* its own parent, because it was replayed off the WIP
  branch and kept its original author date. A global date sort put it ahead of
  its parent and the first cherry-pick conflicted immediately. The order that
  works is a two-pointer merge of the two sides that **never reorders within a
  side** — dependency order wins wherever a rebase left a date out of sequence.

**Every abbreviated commit hash was renumbered** — 41 in this file, 5 in
`OPEN-COMPILER-BUGS.md`, and **18 quoted inside commit messages**. The
pre-linearization history is `backup/haydn-premerge-linearize-20260814` and the
state before the message repair is `backup/haydn-premsgfix-20260814`.

**The message citations were first written off as unrepairable, and that was
wrong.** The argument was that fixing them changes those commits' SHAs, which
invalidates the fix — but it does not, because **a message can only cite an
ancestor**: nothing can quote a hash that did not exist when it was written. So
rebuilding oldest-first terminates. By the time a commit is rebuilt, every hash
it names already has its final SHA, and nothing later has been written yet.
Checked before doing it rather than argued: **18 repairable, 0 circular.**

That rewrite is also much safer than the linearization it followed. Only
*messages* change, so every tree is reused verbatim, `git commit-tree` needs no
worktree, and a conflict is not possible. Verified after: tree identical, the
author-date sequence identical, and the only subject that changed is § 5.16's,
which literally contains a hash.

**One citation is deliberately left dangling.** `6f0d97cf0e10` is the parked WIP
tip that was never merged; it is correct for a message to name it, and it lives
in `backup/wip-parked-20260814`.

**Done again on 2026-08-17, for the two reunification merges, and it cost even
less.** § 10's and § 11's closing merges were both `-s ours` in effect — each
merge tree is identical to its first parent, because the ports had already
landed as ordinary commits ahead of it — so neither carried content that
dropping it could lose. Only § 11's merge was on the first-parent line (§ 10's
sat on its second-parent side), so `git rebase --onto` past that one merge
removed both and replayed just the 11 commits above it. After: `haydn` is **41**
commits on `origin/haydn-dev`, first-parent count equal to the total, no merge
in the range, tree identical to the pre-rewrite tip. The **226** commits that
were reachable only through the second parents keep their SHAs and stay
reachable from `haydn-on-mhyang`, `haydn` and `haydn-on-mhyang-2` on the fork,
plus the tag `backup/haydn-pre-linearize-20260817`. Citations: 8 renumbered in
`HANDOFF-MHYANG.md` and the dropped merge's own hash retired; **0 inside commit
messages** — nothing quoted a rewritten hash, so the message repair above had no
sequel this time. The rebase ran with `core.hooksPath` pointed at nothing, so
Gerrit's `commit-msg` hook could not slip a Change-Id into a replayed message;
all 11 are byte-identical. Nothing was rebuilt or re-gated — identical tree, so
§ 11's numbers still describe this history.


The pre-rebase states of the two WIP branches are preserved as the tags
`backup/wip-preformate-20260807` and `backup/mc-preformate-20260807`, also
pushed — the rebases were force-pushed over the branch names, so those tags are
the only copies of what was there before.

**Those tags do NOT cover the branch tips, and this section used to read as if
they did.** A tag records where a branch was *before* its rebase; the branch
then moved on. Checked when the branches were deleted:

* `-mc`'s tip needed no tag — it is the **second parent** of the merge, so
  every commit on it stays reachable from `haydn`. `git rev-list --count
  fork/haydn-formate-switch-mc ^fork/haydn` was **0**.
* `-wip`'s tip **was not covered by anything**. `6f0d97cf0e10` is in neither
  `haydn` nor `backup/wip-preformate-20260807`, and it was reachable from
  exactly one ref — the branch about to be deleted. It is now the tag
  `backup/wip-parked-20260814`. Its own message says *"Parked, not for merge"*:
  it absorbs the re-delivered database's AR shapes with 23 C++ errors still
  open, and that AR work was finished differently on the trunk (§ 8 Q1).

**The rule this leaves:** before deleting a branch, prove the loss is zero
against every ref that could hold it — `git rev-list --count <branch> ^haydn
^<tags>` — rather than trusting a sentence in this file that says it is
subsumed. One of the two was, and one was not.

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
#   591 discovered: 573 pass, 8 XFAIL, 8 unsupported, 2 fail — the 2 are the
#   nothing — zero failures
cmake --build build -j"$(nproc)" --target HaydnTests  # REQUIRED — see § 6.12
build/unittests/Target/Haydn/HaydnTests               # 253/253 (248 + 5 unit-axis)
build/bin/llvm-lit -s lld/test/ELF/haydn \
    lld/test/ELF/haydn-relocations.s lld/test/ELF/haydn-linker-script.s   # 24/24

build/bin/llvm-lit -s clang/test/Headers clang/test/Sema clang/test/CodeGen/Haydn
#   1474 discovered: 1420 pass, 47 unsupported, 7 fail — clang/test/Headers is
#   clean; the 7 are CodeGen/Haydn + Sema and none are AR (see § 8 Q1)

# the encoding's own gates
python3 llvm/lib/Target/Haydn/utils/haydn_encoding.py --database ~/haydn --check
#   "format E: 3686 placements over 126 (entry, unit, type) shapes"
python3 llvm/lib/Target/Haydn/utils/haydn_encoding.py --database ~/haydn --emit roundtrip
#   "round-trip over 3686 placements / every placement encodes and decodes back to itself"
python3 llvm/lib/Target/Haydn/utils/haydn_ae_audit.py
#   "588 AE_* macros scanned; 11 need attention" — all 11 WITHDRAWN (§ 8 Q2)

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
rm -rf build/bsp-obj build/bsp-stage    # REQUIRED — see § 6.6, § 6.6.1
mkdir -p build/bsp-obj/{compiler_rt,libc,plat,softfloat,sys} \
         build/bsp-stage/lib/bundlesim
cmake --build build --target haydn_bsp
```

`bsp-obj` is not a typo for `bsp-stage`: the stage is an archive of the objects,
and removing only the stage re-archives the OLD objects (§ 6.6.1). The `mkdir`
is required — neither the compile rule nor `llvm-ar` creates its output
directory, and the failure arrives minutes later as `unable to open output
file`.

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
`HaydnInstrInfoManual.td`. Encoding, packing and execution are all still
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
* **`lld/ELF/Arch/HaydnThunks.cpp` held a THIRD one, and it is the one that
  hid longest** — fixed in `6bdabe8d803d`. `writeBundle128LE` wrote two
  `uint64`s, `size()` returned 48, parcels went at 0/16/32, and the three
  instructions were hand-written 64-bit constants. It lives in lld, so a grep
  for `HaydnBundlePlan.h`'s symbols never reaches it and **no compiler-side
  gate can see it at all**: every far call and far branch jumped into a veneer
  that `llvm-objdump` renders as three `<unknown>` lines.

  Rewritten as generated data, on § 5.8's precedent rather than as a second
  set of constants: `haydn_encoding.py --emit thunk-encoding` writes
  `HaydnThunkEncoding.inc`, which lld already has on its include path for
  `HaydnRelocLayout.h`. The veneer is three `BUNDLE_E2` words — instruction at
  entry 0 on ALU0, NOP at entry 1 on ALU1, the one shape all three share since
  `ADDI32` has no 3-entry placement — and every register field is zero because
  the veneer is R0 throughout, so the immediate is the only thing link time
  varies. The generated words were checked against an independent oracle
  before being trusted: `llvm-mc` assembles `{ nop; lui r0, 0 }` to
  `07 0a 02 00 …`, byte for byte what the table says. `alignment` drops from
  16 to 4 for § 5.9's reason.

  **The lesson is the oracle count, not the fix.** Three copies of "a parcel
  is 16 bytes" existed, in three files, and the third was invisible to every
  search that found the first two. Before believing the parcel size is
  single-sourced, grep for the *number* across `lld/` as well as `llvm/`.

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

### 5.3 Database correction — done; provenance is what is still open

`format_e_bit_layout_v2.json` left an operand unspelled for the four-register
MAC family. 76 mapping rows were corrected on this host (commit `8c3a9a9d241c`
regenerated from it; `simulator` re-pinned in `b8da0eb`).

**Two database files diverge from the delivery, not one**, and this section said
otherwise for a long time. The second is `instruction_type_index.json`, where six
accumulating MACs had `rtd` added to `DR_Read_Port`. Both divergences are
mechanically reproducible from the as-delivered files — see *the correction does
not travel* below for the two commands and the byte-for-byte verification.

**And note where the error actually was: this document already knew.** § 5.11
documents the six read ports in detail, down to "+42 bytes, six lines" — which is
exactly the size difference between the delivered index and the pinned one. What
was wrong was **this** section, the procedural one that tells you how to repair a
fresh host, and it is the one people follow. **Two sections of the same document
disagreeing is worse than neither knowing**, because the wrong one carried the
commands.

#### The row accounting, verified against the delivery

The scale of defect 1, computed against the as-delivered file rather than quoted
from notes — an earlier write-up of this said 3556 rows and 3480 agreeing, and
both numbers were wrong:

| | rows |
|---|---:|
| `mapping` rows in the file | **3686** |
| of those, the per-shape `NOP` row | 126 |
| real instruction rows | **3560** |
| operand set already agreed with `Syntax` | 3484 |
| **disagreed** | **76** |

126 is not a coincidence: there is exactly one `NOP` row per (entry, unit, type)
shape, and there are 126 shapes — the same 126 `--check` reports. Every one of
the 3560 real rows names an instruction the index knows, so nothing is skipped
for being unrecognised.

The 76 were **every** placement of the 15 affected mnemonics (15 × 5), not a
subset — which is why an intra-layout self-consistency check finds nothing, and
why the comparison against `Syntax` is the only thing that can catch this class.

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

**ANSWERED by the ISA owner, 2026-08-12.** The provenance chain is:

```
instruction_type_index.json  ──┐
                               ├─(+ instruction_type_operands.json + prompts)──> format_e_bit_layout_v2.json
                               │                                                          │
                               │                                                          └──> format_e_bit_layout_v2.xlsx
                               └─(generate_instruction_to_entry.py)──> instruction_to_entry.xlsx
```

Both spreadsheets are **renderings**; neither is a master. That confirms the
measurement below — the `.xlsx` is the JSON's geometry with the instruction
tables left out — and it settles § 8 Q3.

**The answer is worse news than "the JSON is the master", and better news than
"there is an upstream table we cannot see".** There is no upstream table at all:
`format_e_bit_layout_v2.json` is **generated by prompt** from two files we
already hold. So the 76-row defect was not a transcription slip from some master
— it was a *generation* error, and its own input (`Syntax` in
`instruction_type_index.json`) states the right answer. Two consequences:

* **A redelivery is likely to reintroduce this class of defect, not merely at
  risk of it.** A prompt is not deterministic and there is nothing upstream to
  repair, so "fix it at the source and redeliver" — what § 5 of the question
  document asked for — is not actionable as written.
* **Our repair is therefore the right long-term shape, not a patch.**
  `--fix-operand-mapping` re-derives the mapping rows from `Syntax`, i.e. from
  the generator's own master input; `verify_operand_sets` refuses to emit on
  mismatch. Treat the `mapping` arrays as a **derived cache we re-derive on
  every delivery**, not as data to be trusted. That turns a silent revert into
  a loud refusal plus a mechanical, idempotent repair.

#### Why the `Syntax` cross-check cannot be replaced by a self-check

Worth having measured, because the obvious alternative — give the bit layout a
self-consistency gate and stop depending on a second file — **cannot work**, and
the numbers say so rather than an argument.

An intra-layout agreement check has nothing to report: the same instruction's
operand *set* is identical across every one of its placements for all **684**
instructions in the file. And the original defect was not partial — the 75
blank-`src1` rows were **exactly** the 15 affected mnemonics × 5 placements
each, **75 of 75, every placement, uniformly**.

**The file was internally consistent while being wrong.** So
`verify_operand_sets` — each mapping row against the index's `Syntax` — is the
only check that has ever caught a defect here, and there is no self-contained
substitute for it. That is the argument for the bullets above, and it is why the
refusal is a refusal and not a warning.

This was tried the other way round on 2026-08-13 — treat the layout as the sole
source of truth and ignore the other files — and withdrawn the same day. Two
things it ran into, kept because they are facts about the files rather than about
the decision:

* **`instruction_type_index.json` cannot be ignored even in principle.** The
  layout carries bit geometry and `mapping` rows and nothing else. `Available`
  (every unit itinerary), `Behavior` (immediate signedness and the load/store
  scale — § 5.6), `Syntax` **operand order** (the layout's fields are in bit
  order, which does not give it — § 5.10) and the four
  `*_Read_Port`/`*_Write_Port` lists all live only in the index.
* **`--fix-operand-mapping --write` is direction-sensitive.** It rewrites the
  layout *from* the index, which is the repair under the posture above and would
  have been a footgun under the inverted one.

**How much of the rest of the file should we distrust?** Measured rather than
assumed, because "it is LLM-generated" is an argument for checking, not for
alarm. `instruction_type_operands.json` — the second generator input — gives
each type's operand shapes *and their bit widths*, and nothing in the toolchain
had ever read it (`haydn_encoding.py` does not mention it, though
`GOLDEN_INPUTS.sha256` pins it). Cross-checking it against the delivered
geometry: over **337 (row, shape) pairs across all 126 (entry, unit, type)
rows, every operand field width agrees — zero mismatches.** The prompt-generated
geometry is consistent with its own second input.

**It cannot be made into a gate, and the reason is worth keeping.** Two pairs
have no valid assignment — `entry_num_1/entry{0,2}/ALU2/I12` against the shape
`rs, imm12`, because those placements declare `dest(rt)` where the ALU1 peer
declares `dest(rs, rt)`. That is **correct**: the `rs` form of I12 is
`BEQZ`/`BGEZ`/`BLTZ`/`BNEZ`, all control flow is `Available: [ALU0]`, and so an
ALU2 placement legitimately hosts only `LUI`. The shape table is **type**-scoped
while a placement is **unit**-scoped, so it lists shapes a given row correctly
never hosts, and a gate built on it would report false defects. The existing
per-instruction `Syntax` check is strictly stronger and already covers this.

Two of the eight apparent failures in the first run of that cross-check were
also false: the assignment of operands to fields is a matching problem, and the
first pass solved it **greedily** — the same first-fit error this document
records as CB-147 (§ 5.23), made again, in the tool written to check for
defects. An exhaustive matching dissolved six of the eight. **Greedy assignment
does not become correct because the thing being assigned is small.**

**What the earlier statement of this section got wrong.** This section
used to say the untouched `format_e_bit_layout_v2.xlsx` "now disagrees with the
JSON on those 76 rows". **It cannot.** The `.xlsx` holds bit geometry only —
three sheets, 346 distinct strings, and not one instruction mnemonic or register
alias in the file — so it does not carry mapping rows at all, and it was never a
place the fix could have been made. Measured, not assumed: all 446 field
geometries it *does* describe were compared against the corrected JSON and none
disagreed, so the spreadsheet reads as a rendering of the JSON's geometry with
the instruction tables left out.

The real risk is unchanged but the question is different: **nothing we hold
produces the `mapping` arrays**, so we cannot tell whether the next delivery
reverts them. One data point points at the JSON being the master —
`generate_instruction_to_entry.py`, delivered alongside, reads
`instruction_type_index.json` and *writes* `instruction_to_entry.xlsx`, so for
that pair the JSON is the source and the spreadsheet the rendering. That is
suggestive, not an answer, and it is the ISA owner's to give. **Answered on
2026-08-12 — see the § 8 Q3 entry above for the chain.**

`~/haydn/ISA-QUESTION-format-e-mapping-provenance.md` has since been rewritten as
an **outgoing** document rather than a question, and it is what the owner is being
sent: both defects as body sections (76 mapping rows, six read ports), the
provenance answer folded in as context, and **one** ask — make each pair of
self-agreement checks part of the generation step (`mapping` vs `Syntax`,
`*_Read_Port` vs what `Behavior` reads). Redelivery is explicitly optional there,
because both repairs are one command each. Every figure and quote in it was
re-verified against the as-delivered files rather than carried over from these
notes, which is how the 3556/3480 error was found.

**Verifying a claim about an `.xlsx` needs a tool this host does not have.**
`openpyxl` is not installed, so the row-and-column claims in that document could
not be checked the obvious way. Read the sheet as what it is — a zip of XML:

```python
import zipfile, re
z = zipfile.ZipFile("instruction_to_entry.xlsx")
vals = re.findall(r"<t[^>]*>(.*?)</t>",
                  z.read("xl/sharedStrings.xml").decode("utf-8", "replace"), re.S)
```

That confirmed the *content* claim (`X2MULA32` is a cell, and
`rtd1, rtd2, rsd1, rsd2` occurs exactly once among the 773 shared strings) but
not the row number the document used to cite, so **the row number was removed
rather than shipped unverified**. A specific that cannot be checked is worth less
than the general claim it decorates.

Also worth knowing: the delivery was **internally inconsistent**, and the half we
did not have to touch is the correct one. `instruction_type_index.json`'s
`Syntax` and `instruction_to_entry.xlsx`'s *Operands* column both spell all four
operands of `X2MULA32`; only the bit layout's mapping row named three.

#### The correction does not travel — check the pin first on any new host

The database lives *beside* the two repos and is version-controlled by neither,
so cloning both repos onto a fresh host gets you the corrected `.td` and the
re-pinned `GOLDEN_INPUTS.sha256` **paired with an uncorrected database**. This
happened: on this host `--check` refused to emit and named 16 instructions,
while `HaydnFormatEEncoding.td` in the tree was already the corrected one.

Diagnose it in one command. **TWO files will fail, not one** — this block used to
say "every other database file will match and only the bit layout will not",
which was wrong and would have left whoever followed it stuck after the first
repair:

```sh
cd ~/haydn && sha256sum -c --ignore-missing \
    simulator/bundlesim/isa/database/generated/GOLDEN_INPUTS.sha256
#   format_e_bit_layout_v2.json: FAILED
#   instruction_type_index.json: FAILED      <-- the one that used to go unmentioned
```

Repair is mechanical for both, and each assignment is forced rather than
guessed. **Run both; either alone leaves the pin failing:**

```sh
python3 llvm/lib/Target/Haydn/utils/haydn_encoding.py \
    --database ~/haydn --fix-operand-mapping --write
#   "76 mapping row(s) repaired, 77 field(s) rewritten"

python3 llvm/lib/Target/Haydn/utils/haydn_encoding.py \
    --database ~/haydn --fix-read-ports --write
#   "6 read port(s) repaired over 6 row(s)"
#   FMULA32S_{HH,LH,LL} / FMULS32S_{HH,LH,LL}: DR_Read_Port += rtd
```

The first solves, per row, the matching between the operands the Syntax names
and the fields whose alias list admits them, and refuses to write unless every
row's matching is unique. The second adds the accumulator that six accumulating
MACs read and their `DR_Read_Port` omitted — the § 5.3 class again, a
disagreement between two things the database says about itself (`Behavior` reads
`rtdQ1.63`; the port list stopped at `rsd1, rsd2`), and it matters because that
list is the DR read-port budget the packer enforces.

**Verified against the pristine delivery, not from memory.** Copying the
as-delivered eight files and applying both fixers reproduces **both** pinned
files byte for byte — `format_e_bit_layout_v2.json` → `8465132c…` and
`instruction_type_index.json` → `e77908e9…` — and the generated `.td` files then
come back identical to the committed ones. Re-running either is a no-op. So the
entire divergence between the delivered database and the pinned one is exactly
these two repairs; nothing was hand-edited.

A structural diff of the layout against the delivery confirms the blast radius:
**77 leaf changes, every one of them a `src1`/`src2` value inside a `mapping`
row** — 75 rows filling one blank, plus the one transposed `X4SEL16` row that
takes two — and **no** change to any bit range, opcode, `type_code_bin`,
`operand_fields` or reserved statement.

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

**Do not regenerate while § 5.11's first three axes are non-zero.** They are
exactly the places the encoder reads an operand the logical did not put there,
so regenerating would freeze the wrong bytes into 589 expectations and the
round trip would agree with all of them. Six attempts were made before the gate
was met and five found code to fix instead — the relocation kind for
`SET_HWLOOP`, branch scaling, `LUI`'s operand shape, the tied writeback family,
and § 5.11's own generator half.

`arity`, `defs` and `kinds` reached zero and **62 tests have been regenerated
against that**; lit went 373 → 435 of 589. `ties` is a weaker gate and did not
block: it is a register-allocation defect, not an encoding one, and the three
left on it are held by § 5.2 rather than by work.

#### It is not one script run

Of the 200 that were failing when the gate opened:

| | n | |
|---|---:|---|
| hand-written | 132 | no script owns them |
| `REBASELINED (auto)` | 35 | an earlier ad-hoc pass, no owner either |
| `utils/update_*_test_checks.py` | 33 | of which 6 have RUN lines the scripts cannot drive |

**And running the script is not enough on its own.** It replaces only blocks in
its own format, so on a file that also carries a stale hand-written block it
APPENDS: `bitfield.ll` came out holding the format E assertions *and* the
Bundle128 ones they were meant to replace. Strip every assertion first, then
regenerate whole. That is what the 27 script-owned and the 35 rebaselined tests
got, and the 35 now name the script as their owner so the next encoding change
does not become 35 more hand-edits.

#### Read the diff; the numbers move for two different reasons

Regenerating records the encoder, so the review is the check. Two things worth
looking for, both seen:

* **A changed value that is right.** `s64-loadstore.ll` spills the halves of an
  i64 at offsets 0 and **1**, not 0 and 4 — § 5.11's ÷4 LS scaling, the same
  fact as `s_lw_pre_imm r3, r1, 1`. An expectation that "corrects" it back to 4
  is the defect.
* **A changed value that is a placement.** Relocation offsets are
  `bundle_start + the entry's byte base`, so they move whenever the packer puts
  an instruction in a different entry. `cb76-reloc-hi20-lo16.s` went 0x0/0x10 →
  0x8/0x12 and both are correct. Assert the invariant in a comment, because the
  number is not one.

#### The prose goes stale too, and nothing catches it

25 comment lines in the regenerated tests still described Bundle128. They are
not assertions so nothing failed, but they are what tells the next reader why a
test expects what it expects. Two kinds:

* **The slot model** — "the second LD32 is promoted to LD32_S1 so both loads
  pack", "slot 1 + slot 2". The behaviour survives, the reason does not:
  LOADSTORE0 and LOAD1 are two units and an entry may name each (§ 7.1), where
  Bundle128 had one unit per slot so packing meant promoting to another slot.
* **A previous bulk rename's wreckage** — two tests read "both ld32 and ld32
  are semantically identical loads", a sentence that once named two spellings
  and had them collapsed onto one. Left in place and labelled rather than
  guessed at: its claim was about slot promotion and does not survive, and
  inventing a replacement would be § 5.6's mistake at one remove.

#### What is left: 81, and 15 of the 138 were never assertions at all

**Correction: this section used to say all 138 were hand-written assertions.
Fifteen were not, and they had to be done first — regenerating on top of them
would have frozen a miscompile into the expectations.** Four were live code
defects and eleven were stale test INPUT, failing before FileCheck ran because
their source text named a retired logical. Categorising before touching
anything is what separated them; the split was:

| n | | |
|---:|---|---|
| 4 | live code defects | `MOVE32` arity, a packer hoisting a use above its def, the generated `haydn.h` (twice) |
| 11 | stale INPUT | nine MC `.s` spelling `LD32`/`LD64`/`LD16`/`ADDI32_W`, one `.mir` spelling `ZERO_DR 0`, one Bundle128-only pad test |
| 6 | script-owned, RUN lines the scripts cannot drive | as this section already recorded |
| 117 | assertions | 53 a retired spelling, 64 a bundle shape |
| **138** | | |

Of the 117, **52 are regenerated and script-owned**. The remainder split into
two problems that are NOT more of the same work:

* **43 carried 162 `CHECK-NOT` assertions and were held — RESOLVED, and the
  framing was wrong.** The reasoning was that the update scripts cannot
  reproduce negative checks, which is true and beside the point: nothing
  required a script. They were rewritten by hand along with everything else in
  this section, and all 43 pass. The scripts do still take over any prefix they
  find on a RUN line, so **do not point one at a file carrying negatives** —
  that would silently delete, for example, `hwloop-remat-freereg-dep.mir`'s
  `CHECK-NOT: $r1 = ADDI32_W $r1, -1`, which is the entire property that test
  exists to protect.

  The half of that note that mattered — "many of those NOTs name retired
  spellings and are already vacuous" — could not be checked at the time,
  because the gate that was supposed to answer it was reading zero files. See
  § 6.15.
* **26 cannot be driven at all.** `update_mc_test_checks.py` rewrites `%s` into
  an `echo | llvm-mc` pipeline and mangles multi-line RUN continuations; two
  more hit an `output_type` UnboundLocalError in the llc script. Regenerating
  them mangles the file, so they were restored.

Ten more regenerate cleanly and still disagree — real differences, to be read
one at a time — and two were the `.c` tests blocked on § 8 Q1, now unblocked.

**Update after § 8 Q1, § 5.13, § 5.12 and the stale-input pass: 25, not 38.**
`ar-unaligned-roundtrip.s` was one of the byte-level group and is regenerated;
the two `.ll` tests the intrinsic arity change broke were fixed in the same
pass. The tooling problem under the eight `.s` files
(`update_mc_test_checks.py` cannot drive their RUN lines) is untouched — that
one was regenerated by hand against `llvm-objdump` and `-show-encoding`
output, which works for a file this size and does not scale to eight.

**The stale-INPUT category is now empty — seven of them, and none was a
one-line rename.** What they had in common is that the source text named
something retired, so they failed before FileCheck had anything to judge:
`mac-instructions.s` carried three spellings of the same instruction across
its input, its `CHECK:` and its `ROUNDTRIP:` lines; the `.mir` spelled
`ADDI32_S2` in already-bundled input; `packetizer-promote-ld32-to-ld32-s1.ll`
asserted `LD32_S0`/`LD32_S1`, a retired spelling and a retired model at once;
and the four `flex-*` files fed hand-computed **Bundle128** parcels.

#### A byte oracle that came out of the encoder is not an oracle

The `flex-*` four exist to hold bytes derived from the ISA document, which is
the one thing `--emit roundtrip` structurally cannot be: the round trip checks
the encoder against the decoder and passes on any defect symmetric across the
pair (§ 5.3's blank MAC operand, § 6.10's missing ÷2, § 5.13's sign extension
came close). Their bytes are re-derived by hand from
`format_e_bit_layout_v2.json`, field by field and written out in each file.

`flex-bytes-mac.s` had already lost that property and said so in its own
comment — *"the live `llvm-mc -show-encoding` for … produces"* — which turns
an independent oracle into a second copy of the encoder's opinion. **If one of
these files is ever "updated" by pasting `-show-encoding` output, it stops
being evidence and nothing will report that it has.**

`flex-nop.s` is the one whose rule actually changed. Bundle128's was "all 128
bits zero = NOP". Format E cannot state it that way at all: `bit[2:0] = 0b111`
is the format indicator, so an all-zero parcel is not a bundle and
disassembles as `<unknown>`. What survives is one step weaker — every payload
bit zero, only the indicator set, `07 00 …` — and that is what it pins now.

#### The two "deliberate reds" were stale INPUT too

They are green, and the diagnosis they carried was wrong in a way worth
recording, because it was believed for several sessions and it reads
convincingly.

`f2mulzaa32rs-binary-mac.ll` and `mac-acc-tied-def-encode-roundtrip.ll`
declared and called `@llvm.haydn.f2mulzaa32rs_hhll` and
`@llvm.haydn.ff2mula32rs_lh` — with **underscores**. The intrinsics are
`llvm.haydn.f2mulzaa32rs.hhll` and `llvm.haydn.ff2mula32rs.lh`, with dots, and
each file spells a sibling correctly two lines away
(`llvm.haydn.f2mulzaa32r.hhll`). An unrecognised `llvm.*` name is not an
intrinsic at all, so it became an ordinary external call:

```
{ nop; jal lr, llvm.haydn.f2mulzaa32rs_hhll; nop }
```

That is the "falls through to a call to a nonexistent symbol" this section
recorded — and it is what a misspelt intrinsic ALWAYS does, whether or not the
target can select the real one.

The conclusion drawn from it was that `HaydnInstructionSelector.cpp` and
`HaydnGISel.td` never mention the intrinsic, which is true, and that this is
why it is not selected, which is not. **Neither file mentions it because
neither needs to**: the generic matcher selects it from the instruction's own
definition. Measured rather than argued — a probe was built for every
intrinsic with no selector mention and compiled: **56 of 56 select**. The grep
was the evidence and the evidence was wrong.

The other half of the old note — *"the test asserted a spelling this
instruction never had"* — was correct, and about the ASM checks
(`f2mulzaa32r.hhll` where members use `_`). Two true observations about
spelling, one about the IR name and one about the mnemonic, were fused into a
missing-feature story. **When a symptom has a mundane explanation and an
alarming one, the mundane one needs excluding first, and a name is cheap to
check.**

#### Closing it out: what the 138 actually were

The backlog is empty apart from the two deliberate reds, so the final split is
worth recording against the categorisation this section started with. Almost
nothing was "regenerate the expectation":

| | n | |
|---|---:|---|
| the property was never placement / bytes | 6 | asserted the packer's choice; rewritten to assert the contract |
| stale INPUT | 7 | source text named something retired |
| assertions that could not fail | 2 | nested `{{...}}`, an alternation with a dead arm |
| numbers that are consequences | 4 | relocation offsets, parcel addresses |
| immediate spelling | 4 + 1 | element index, not bytes |
| mnemonic spelling | 2 | member `_` against logical `.` |
| genuinely regenerate the bytes | 4 | `encoding.s` and the gformat/mode0 three |
| stale premise that was a live defect | 1 | `d486` — § 5.14 |
| ~~deliberate reds~~ | 2 | not a GISel gap at all — a misspelt intrinsic name, see below |

**Four of the failures were compiler defects, not expectations**: § 5.13's
`lui`, § 5.12's unit axis, and § 5.14's two. Each was found by taking a red
test's assertion seriously rather than regenerating it. That ratio is the
argument for the rule at the top of this section — categorise before touching
anything — and the sharper form of it is: *a test that is red because the
compiler changed and a test that is red because the compiler broke look
identical from the failure output.*

#### The scale is not observable at MC level

Four failures were the load/store immediate. The category name is misleading:
the INPUTS had already been converted to the element index and only the CHECKs
still spelled bytes, so `s_lw_with_imm R2, R3, 4` was asserted to print back as
`r2, r3, 16`. The database settles it without regenerating anything —
`S_LW_WITH_IMM rt, rs, imm6` / `rt = mem32[rs + (imm6 << 2)]`.

| instruction | address | scale |
|---|---|---:|
| `S_LBS` / `S_LBU` / `S_SB` | `rs + imm6` | 1 |
| `S_LHWS` / `S_LHWU` / `S_SHW` | `rs + (imm6 << 1)` | 2 |
| `S_LW` / `S_SW` | `rs + (imm6 << 2)` | 4 |
| `D_LDW` / `D_SDW` | `rs + (imm6 << 3)` | 8 |

**And no MC test can check the scale itself.** The encoding holds `imm6` and
nothing else; the shift is what the hardware does with it. An encoder and
decoder that drifted to the same wrong shift round-trip perfectly, and a
`-show-encoding` or objdump check just echoes the operand. This was nearly
recorded wrongly: a "scale ladder" was written into
`ld16-ld8-bundle128-roundtrip.s` — four widths reaching one byte offset, so
the immediates are forced to 8/4/2/1 — with a comment claiming it catches
symmetric drift. It does not. The comment now says what it does catch: an
operand that quietly goes back to meaning bytes, which would need all four to
read 8.

The scale is checked where a byte offset must BECOME an element index, which
is CodeGen. `s64-loadstore.ll` spilling an i64's halves at 0 and 1 rather than
0 and 4 is the assertion that has teeth, and past that the simulator is the
only judge — the same shape as § 5.13, where the defect lived between the
parser and the decoder and the round trip could not see it either.

#### Placement is almost never the property

Six of § 5.4's failures were bundle-order differences, and in every one the
test's own header said placement was not what it was testing — "must parse
inside a bundle", "round-trip cleanly", "ONE grouped line, NOT N split lines",
"DISTINCT slots, zero collisions", "we assert only that BOTH ops appear with
their original operands". Then each pinned the arrangement anyway, so the
switch broke them all on something none of them was for.

Rewritten to assert the contract: mnemonic and operands present, one line per
parcel where that is the point, either print order where two ops must share a
bundle. That last is the legitimate use of an alternation under § 6.16 — two
spellings of ONE fact — as against `{{beqz|set_hwloop}}`, which was two facts
and asserted neither.

**They now survive § 5.12 as well.** Pinning today's arrangement would have
meant redoing all six the moment the unit axis starts firing and NOP padding
stops landing on ALU0. If a placement test has to be regenerated, first ask
whether it should be asserting placement at all.

**A substitution will not do the retired spellings**: § 5.6's load/store rename
is "a rename plus a range collapse", so `LD32` did not become one name — it
became `s_lw_with_imm rt, rs, imm`, with an operand the old spelling did not
have and a ÷4 scale on it. Read the scale from the database's own `Behavior`
(`rs + (imm6 << N)`, with byte accesses stating the degenerate `rs + imm6`)
rather than writing a table: doing that is what caught § 5.6's correspondence
table naming the DR64 pair wrongly.

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
| `LD64` / `ST64` | `D_LDW_*` / `D_SDW_*` (DR64) |

and each addressing mode is a separate logical — `_WITH_IMM`, `_WITH_REG`,
`_PRE_IMM`, `_PRE_REG`, `_POST_IMM`, `_POST_REG`, plus `_BREV_*` and `_CB_*`.
69 of the 110 LOADSTORE0/LOAD1 logicals produce a GPR32.

**Correction: the DR64 row above used to read `D_LW_*` / `D_SW_*`, and both
halves were wrong in a way that assembles.** `d_lw_with_imm` is a real logical
— it loads a WORD into a DR64, scaled by 4 — so writing it where a doubleword
load belongs produces a test that passes and addresses half as far.
`d_sw_with_imm` does not exist at all; the doubleword store is
`d_sdw_with_imm`. The names are `D_LDW_*` (load doubleword, `imm6 << 3`) and
`D_SDW_*`. Confirmed against the database's own `Behavior`
(`rtd = mem64[rs + (imm6 << 3)]`) rather than against this table, which is how
the error surfaced; the compiler had it right all along — `llc` selects
`d_ldw_with_imm` for an `i64` load.

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
**no** format E member; `SEXT32T64` (`HaydnInstrInfoManual.td`, `isCodeGenOnly`
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

`HaydnTests` compiles again as of `b897d75ea33d` (578 errors to 0) and runs
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

**Fixed in `597801192e9d`**, verified on real codegen rather than in
principle:

```
before  $r1 = S_LW_WITH_IMM_P32_LOAD1
        $r2 = S_LW_WITH_IMM_P31_LOAD1        <- both LOAD1, one bundle
after   $r1 = S_LW_WITH_IMM_P32_LOAD1
        $r2 = S_LW_WITH_IMM_P30_LOADSTORE0
```

and the two loads still dual-issue, so it costs no ILP.

Fixed at the root, not per call site. **The formats object now carries the
MII** (`HaydnBaseMCFormats::getMCInstrInfo()` — null on the plain class, the
real one on `HaydnMCFormatsWithMII`) and the two chokepoints fall back to it:
`Bundle`'s constructor and `enumeratePlacementAlternatives`. `tryAdd` and
`tryAddProduct` both funnel through the latter, so one change covers the whole
solver and **a new caller cannot lose the unit check by forgetting an
argument** — which is exactly how it was lost. `HaydnMCCodeEmitter` had even
built a `HaydnMCFormatsWithMII` and then dropped it when constructing its
`Bundle`.

`HaydnHazardRecognizer` mattered most: it is what *chooses* which member an
instruction becomes, and it held a plain `HaydnMCFormats` while owning a
`TargetInstrInfo *`. Fixing the finalize paths alone did nothing, because the
member was already chosen by then — worth remembering when reasoning about
this layer.

**Correction: this section used to say `HaydnTests` "cannot currently construct
an `MCInstrInfo`", and therefore that unit-aware packing had no unit-test
coverage. It can, and it now does.** `HaydnDesc` was already in the suite's
`LLVM_LINK_COMPONENTS` and registers one; `HaydnTestMCInstrInfo.h` is that
constructor, going through the `TargetRegistry` and failing loudly if the
target is not registered — a null MII would silently revert every such test to
slot-only, which is the failure this closes. All eleven suites now build
`HaydnMCFormatsWithMII`, which is what real packing does.

Turning it on paid for itself immediately, and none of it was visible to a
slot-only object:

* **`DualLoadThenRejectThirdLoad` and `DualMacFillsS1S2` were passing
  vacuously** — a third load has no unit, since there are exactly two.
* **Eight tests were asserting bundles the hardware cannot issue**, built from
  members that all named ALU0. Slot-only packing accepted them: the illegal
  bundle above, reproduced inside the gate that exists to catch it.
* **A test that had never checked anything.** `EncodedBytesAlwaysSixteenOnSuccess`
  iterated over `{ArrayRef<unsigned>{Haydn::NOP}, …}`; `ArrayRef`'s
  single-element constructor stores a POINTER to a temporary that dies at the
  end of the full expression, so every opcode it read was stack garbage. It
  stayed invisible precisely because a formats object with no `MCInstrInfo`
  never reads an opcode's NAME — garbage simply missed the alternates table.
* **Two name helpers aborted on an opcode they had never heard of.**
  `getLogicalBaseOpcode` and `getMemberSlotFromNameLocal` called
  `MCInstrInfo::getName` unguarded while backing `isSupportedInstruction`,
  which is a PREDICATE: an unknown opcode has an answer, and it is no.

**Still uncovered:** a caller that passes no MII packs slot-only. That remains
the documented behaviour of an object that was never told which `MCInstrInfo`
is in play, rather than an accident — the difference is that the tests no
longer silently inherit it.

The remaining § 7.1 option — moving the unit onto a generated per-member table
— is still the better end state, and is the natural companion to § 6.9's
`--flags-from` work since it removes the name-parsing dependency entirely.

Note the shape of this: § 7.1 predicted the gap in prose and could not
demonstrate it, because under Bundle128 the axis was inert and no test could
reach it. Restoring `HaydnTests` after the switch is what turned a documented
risk into a reproducible failure. That is the argument for not deferring the
gate any further.

### 5.8 Fixup geometry corrupts the bundle header — BLOCKER, and no gate sees it

**Demonstrated, not deduced.** Assembling the same branch with a literal
offset and with a symbolic one:

```
{ nop; nop; beqz r1, -12 }    ->  8f ...   bits[5:0] = 0b001111   correct
{ nop; nop; beqz r1, tgt  }   ->  af ...   bits[5:0] = 0b101111   CORRUPT
                                             ^^ reserved bits, must be 00
```

The encoder produces a correct bundle in both cases. **Applying the
relocation is what breaks it**, and the result does not decode at all —
`llvm-objdump` renders `<unknown>`.

#### Why

The emitter anchors an entry's fixups at
`SlotWindowLSBByteBase = (BUNDLE_E_BITS - 1 - RightOffset) / 8`. Format E's
entry windows do not start on byte boundaries, so that division **truncates**:

| entry | window | LSB bit | byte base | lost sub-byte shift |
|---|---|---:|---:|---:|
| P20 | `Inst{50-6}` | 6 | 0 | **6** |
| P21 | `Inst{91-51}` | 51 | 6 | **3** |
| P30 | `Inst{36-6}` | 6 | 0 | **6** |
| P31 | `Inst{67-37}` | 37 | 4 | **5** |
| P32 | `Inst{94-68}` | 68 | 8 | **4** |

Bundle128's windows were 48/40/40 from bit 0 — LSBs at 0, 48, 88, every one
byte-aligned — so the shift was always 0 and no code ever had to carry it.
Above, `beqz` landed in P30, the shift of 6 was dropped, and the field was
written six bits low, on top of the header.

#### Why a static table cannot express it

`RelocFieldInfo` is keyed by `RelocKind` alone and carries one `FieldLsb`. The
shift is a property of the **placement**, and every relocation-bearing
instruction has members in several entries:

```
JAL JALR BEQ BNE     P20, P30, P31           -> shifts 6, 6, 5
BEQZ BNEZ LUI        P20, P30, P31, P32      -> shifts 6, 6, 5, 4
ADDI32               P20, P21                -> shifts 6, 3
SET_HWLOOP           P20                     -> shift 6
```

So one `RelocKind` needs up to four different `FieldLsb` values. This is a
genuine design decision, and it is **linker-visible** — `HaydnRelocLayout` is
the single source of truth that MC *and* lld both read, and lld sees only an
ELF relocation (offset + type), never the placement.

#### The sub-byte shift is only half of it — attempted and reverted

A first attempt derived the lost sub-byte shift from the image (bundle byte +
header bit 3 identify the entry) and applied the field at
`FieldLsb + shift`. **It stopped the header corruption and still produced
wrong code**, because it assumed `FieldLsb` was already correct per entry and
only the byte truncation had been lost. Both terms vary:

| member | `imm12` at entry bit | entry LSB in bundle | **bundle bit** |
|---|---:|---:|---:|
| `BEQZ_P20` | 26 | 6 | 32 |
| `BEQZ_P30` | 17 | 6 | 23 |
| `BEQZ_P31` | 17 | 37 | 54 |

and the table's `FieldLsb` for `WIDE_BranchSImm12` is **4** — Bundle128's
position, from `s0={FU,opc,reserved22,imm12,rs}`. So the field's bundle-bit
position is `entryLSB(placement) + fieldLsbWithinEntry(kind, placement)`, a
genuine **2-D** relation over (RelocKind, placement). Neither term is a
constant per kind.

**Reverted deliberately, and this is the interesting part.** The half-fix was
strictly worse than the broken state: writing into the header is what makes
this bug *loud* — the generated decoder tables check the header, so
`llvm-objdump` says `<unknown>` and you cannot miss it. Correct the header
overlap while leaving the field misplaced and the bundle decodes fine with a
wrong branch target, which nothing in this project can see. A loud wrong is
worth more than a quiet wrong until the real fix lands.

#### What the real fix has to be

The geometry must come from the **generated encoding**, not a hand-written
table: `haydn_encoding.py` already knows every member's field positions,
because it emits them. A per-(RelocKind, placement) geometry table generated
alongside `HaydnFormatEEncoding.td` is the only source that cannot drift from
the members it describes — the same argument that retired the `KnownBases`
table in § 5.6 and the `isValidFlexSlotWindow` FU table in § 5.2.

The image-derivation decision still holds and is still needed: it is what
recovers the *placement* at relocation time, in both MC and lld. It is simply
an input to the table lookup rather than a correction applied on top of one.

#### Fixed in `de19bca4f3b5` — a generated table, shared with lld

Option 2 was taken: the placement is derived from the image, so no new
`R_HAYDN_*` types and a re-delivered layout keeps working. But the geometry
itself is **generated**, not corrected by hand — the key is
`(FieldSize, entry count, entry index, mapping)` and every part of it is
readable at relocation time:

| part | where it comes from |
|---|---|
| `FieldSize` | the relocation's own `RelocFieldInfo` |
| entry count | header bit 3 |
| entry index | the relocation's offset within its bundle |
| mapping | read out of that entry — distinguishes `LUI` on ALU2 from a branch on ALU0 at the same entry |

47 rows from `haydn_encoding.py --emit reloc-geometry`, because that script
already knows every field's position: it is what places them. Ambiguous keys
are **dropped rather than guessed**, so a relocation that ever needs one
misses the table and the caller errors instead of patching the wrong bits.
Only narrow fields collide (`NOP`'s 4-bit imm); every 12- and 20-bit
immediate resolves uniquely.

`patchRelocFieldInBundle` is the **whole operation**, not a helper, and MC and
lld both call it. Each of its three steps produced a silent wrong answer at
least once, so splitting them across two callers was not worth the risk.

Result: all three entry positions patch correctly
(`-6`, `-14`, `-22` for branches at `0xc`, `0x18`, `0x24` to a target at 0),
and `lld/test/ELF/haydn` goes **5/24 → 8/24**. No CodeGen or `HaydnTests`
regression.

**This paragraph used to end "the remaining 16 are Bundle128 byte expectations
(§ 5.4)", and that guess was half wrong.** Triaging all twelve that were left
later split them evenly: six were stale — the 16-byte address stride, and one
that could not be ported at all because format E has neither an all-zero NOP
nor a whole-parcel `.p2align` — and six were **one defect**, the Bundle128
thunk emitter (§ 5.2) plus the addend correction below. Assuming a red gate is
stale is exactly the assumption § 5.4 warns about, and it was made here about
the one gate that stands outside the encoder's opinion.

##### Three silent traps on the way, all worth knowing

* **The sub-byte shift alone is not enough** — see above. Correcting only the
  byte truncation is strictly worse than leaving it broken.
* **`patchField`'s image reader only knows widths 1, 2, 4 and 6** and silently
  does *nothing* for anything else. A 3-byte window meant the P30 and P31
  branches wrote **no bits at all** while P32, which happened to need 2,
  patched correctly — a failure that looks like "the fix works for one case".
  Widths are now rounded up to a supported one.
* **Data relocations must not go near this.** `R_HAYDN_32` in `.rodata` has no
  bundle and no header; applying the geometry broke every data relocation in
  lld. It failed *safely* — non-bundle bytes do not carry the `0b111` format
  indicator — but it should never have been asked.
  `isInstructionFieldReloc` gates it.

#### Why nothing caught it, again

`--emit roundtrip` never applies a fixup, so it cannot see this at all. The
assemble/disassemble round trip caught it here **only by luck**: the field
happened to land on the header, and the header is the one thing the generated
decoder tables check (§ 5.2). A fixup that lands six bits low inside a
*payload* field is completely silent — it round-trips, because the encoder and
decoder still agree with each other, and only the linked program is wrong.

`lld/test/ELF/haydn` and the simulator remain the only gates that stand
outside this, which is exactly what § 5.4 said and is now demonstrated rather
than predicted.

#### The byte base is in the ADDEND too, and a veneer does not cancel it

**Found in `6bdabe8d803d`, after the thunk emitter was rewritten (§ 5.2) and
its veneer finally decoded. The target was still four bytes past the symbol.**

This is the other half of "a branch resolves from the bundle, not from the
entry it sits in" (`c14078c3339d`), and it is the half that only bites a
consumer computing an absolute address:

* the emitter puts the entry's byte base into the **addend**, and the
  relocation's offset points at the **entry** — the offset has to, because
  § 5.8's geometry lookup recovers the entry index from it;
* a PC-relative patch is `S + A - P`, and the base appears in both `A` and
  `P`, so **it cancels** and the branch resolves from the bundle. That is the
  whole trick;
* a **veneer cancels nothing**. It materializes `S + A` into a register with
  HI12/LO20 and jumps there, so the base stays in and every far call lands
  that many bytes into the middle of a bundle.

`jal lr, callee + 16` therefore produced a veneer targeting `callee + 20`.
Recovered the same way § 5.8 recovers a placement — from the image, as the
relocation's offset within its own bundle — which keeps the fix in one
vocabulary instead of adding a second convention.

Two things worth carrying forward:

* **The addend is no longer just the source addend.** Any new consumer of a
  Haydn instruction-field relocation has to decide whether it cancels the byte
  base or removes it, and the answer is not the same for all of them.
  `reloc-callsimm20.s` now states the invariant — offset and addend are equal
  for a bare call, and the two cancel — rather than pinning the numbers.
* **Nothing but the linker could have found it.** The object is identical
  either way; only the linked image differs, and only for symbols far enough
  away to need a veneer. `--emit roundtrip` never applies a fixup, and MC's
  own tests never link.

### 5.9 Function alignment: a 12-byte parcel cannot align to 16

**Fixed in `ca005be89073`.** Object emission went from **134 of 430** CodeGen
tests to **424** — the same 424 that produce assembly.

#### The measurement that hid it

Every earlier count in this document ran `llc` to **assembly**. Running
`llc -filetype=obj` on the same inputs gave 134, and 47 of the first 49
failures were `unable to write nop sequence of N bytes`.

**"llc produces output" was never the same claim as "the output assembles",
and the gap was 290 tests.** Any future coverage number here should say which
one it measured.

#### Why

A callee entry and an LR return PC must land on an exact bundle boundary.
Under Bundle128 that meant `Align(16)`, the parcel size. Format E's parcel is
**12 bytes — not a power of two**, so it cannot be requested at all, and
asking for 16 is actively wrong: padding a stream of 12-byte bundles up to a
16-byte boundary needs 4, 8 or 12 bytes depending on the function's length,
and only 12 is a whole bundle. `writeNopData` correctly refuses a partial
parcel, so **whether a function assembled depended on its size**:

| function | pad to 16 | |
|---|---:|---|
| 1 bundle (12 B) | 4 | **abort** |
| 2 bundles (24 B) | 8 | **abort** |
| 3 bundles (36 B) | 12 | ok |
| 4 bundles (48 B) | 0 | ok |
| 5 bundles (60 B) | 4 | **abort** |

That is why the first two functions tried by hand both assembled — they
happened to be sizes where the padding was 0 or 12. A size-dependent failure
is worse than a total one: it looks like the feature works.

`Align(4)` is the right request. Every bundle boundary is at
`section_start + 12k`, which is always 4-aligned, so the contract is already
satisfied and the padding is **always zero**. It cannot ask for a partial
bundle because it never asks for anything.

This subsumes part of the `writeNopData` item: its all-zero payload is still
wrong (§ 5.2), but nothing now asks it for a non-bundle length.

### 5.10 The members lost the operand classes that carry EncoderMethods

**§ 6.1, exactly, and it is still open.** Found while regenerating the lld
expectations: two of those tests are not stale, they are reporting real
defects.

```
set_hwloop 0, loop_body, loop_end, 3
  expected  0x0 R_HAYDN_HWLoopOff1 / 0x0 R_HAYDN_HWLoopOff2
  actual    0x0 R_HAYDN_32         / 0x0 R_HAYDN_32
lui R1, target_data
  expected  R_HAYDN_HI12
  actual    no relocation emitted at all
```

The mechanism is the one § 6.1 already describes for `JAL`:

* The **logical** `SET_HWLOOP` takes `hwloop_off1` / `hwloop_off2`, whose
  `EncoderMethod` is
  `getSImmOpValueXStepWide<6,/*Shift*/2,…,FIXUP_HAYDN_HWLoopOff1>`. The fixup
  kind is attached **by the operand class**, and the value never reaches
  `getExprFixupKind`.
* The generated format E **member** takes a plain immediate with no
  `EncoderMethod`, so it falls through to `getMachineOpValue` →
  `getExprFixupKind`, which has **no `SET_HWLOOP` case at all** (`grep` finds
  the name zero times in `HaydnMCCodeEmitter.cpp`) and returns the data
  default `FIXUP_HAYDN_32`.

So a hardware-loop setup emits two *data* relocations pointing at an
instruction. `isInstructionFieldReloc` then correctly treats them as data and
patches at the raw byte offset (§ 5.8) — every layer behaves consistently with
a premise that is wrong three steps upstream.

#### This is the same gap as § 6.9 and § 6.10, on a third axis

§ 6.9 closed the **instruction property flags** for members by teaching
`--emit td` to copy them from the logical (`--flags-from`). § 6.10 records the
**immediate scaling** as still open, and says the fix is "operand classes
carrying `getSImmOpValueXStepWide<N,/*Shift*/1,…>`". This is the **fixup
kind**, carried by the same `EncoderMethod`, in the same operand classes.

They are one item: **the generator emits plain `simmN` where the logical had a
purpose-built operand class, and everything that class carried is lost.**
Scaling and fixup kind are two symptoms; there may be more.

**Fixed in `b358e14e9259`, but only partly, and the boundary matters.** The
member now inherits the logical's operand class, with two restrictions,
because a wrong inherit mis-encodes silently:

* **Only classes with both an explicit width and an `EncoderMethod`** — six of
  them: `hwloop_off1/2`, `uimm16_hwloop_cnt`, `uimm20_wide_abs`, `uimm6_dr`,
  `uimm8_csr`. The width lets the member's field be checked against the class.
  The width-agnostic `Operand<OtherVT>` classes (`brtarget`, `calltarget`) are
  excluded **on purpose**: their encoders dispatch on the *opcode*, branches
  already reach the right fixup kind through `getExprFixupKind`, and rerouting
  a working path is not worth the risk.
* **Matched by name, not position.** 21 logicals still disagree with their
  members on arity (§ 5.11), and a positional match is wrong for every one of
  them. The member's alias carries the logical's operand name as a suffix
  (`uimm6_offset1` for `offset1`), which is unambiguous wherever it matches.
  Note this is the *operand class*; the tie's position is matched positionally
  and deliberately, because there the two sides' names never line up at all.

Net effect is four member definitions, all `SET_HWLOOP` / `SET_HWLOOP_F2`, and
`set_hwloop` now emits `R_HAYDN_HWLoopOff1/Off2` instead of two
`R_HAYDN_32`. lld 10/24 → 11/24.

#### § 6.10 is CLOSED — `c884e1ce7eea`

The earlier expectation that this would close § 6.10 was wrong. Branch
immediates still take a plain `simm12` and store the byte offset **raw**, so
the same offset encodes two different ways depending on how it was written:

```
{ nop; nop; beqz r1, -12 }   -> field -12   literal, no scale
{ nop; nop; beqz r1, tgt }   -> field  -6   symbolic, RelocFieldInfo
                                            ValueShift = 1 applied
```

**Fixed.** The encoders already did the right thing and the members simply
never reached them: `getBranchTargetOpValue` divides by two, checks 2-byte
alignment, and dispatches the fixup kind on the opcode. The members now take
`brtarget_e12` / `calltarget_e20` — width-matched so the decoder can shift
back, encoder-shared so one class covers every placement and every branch
shape. 37 member definitions: 34 branches, 3 calls.

Matched by **uniqueness**, not name: the logical calls it `offset` or `target`
and the database calls the field `imm12`, so there is nothing to match on, but
a branch logical carries exactly one `brtarget`/`calltarget`.

A second defect fell out, and it is § 5.6's mistake again:
`getCallTargetOpValue` tested `Opcode == Haydn::JAL` — the **logical** opcode
— so a member named `JAL_P30_ALU0` never matched and the `>>1` was skipped for
every placed call. **Fold through the logical, never the spelling** now has
four instances in this document (§ 4's fixup kinds, § 5.2's hwloop predicates,
§ 5.6's `getHaydnFlexBaseOpcode`, and this).

**Both of § 5.2's original generator gaps are now closed** — § 6.9's property
flags and § 6.10's immediate scaling.

##### And it immediately caught a bad expectation — mine

`reloc-wide-branch-call` had been updated two commits earlier to assert
`jal lr, 10` / `beq r1, r2, 4`. Those were the **raw field values**; the byte
offsets are 20 and 8. The encoded bytes did not change when § 6.10 was
closed — only the disassembly, which now reads in bytes (§ 6.11's convention).

The expectation had been written by reading what the disassembler printed at a
moment when the disassembler was wrong. That is exactly what § 5.4 means by
"regenerating only records what the new encoder did", and it happened here
despite knowing the rule — worth remembering before regenerating 589 more.

#### Why it took until now to see

Nothing before this point applied a relocation to a hand-written hwloop or
`lui`. `--emit roundtrip` does not do fixups; the CodeGen sweep measured
whether `llc` produced output; and the § 5.8 work used branches, whose members
*do* land in `getExprFixupKind` and so happened to work. `lld/test/ELF/haydn`
is the first gate that exercises the others — which is what § 5.4 meant by
"the round trip is not evidence".

### 5.11 Logical and member operand lists must agree, or the encoder reads the wrong one

**`lui r1, sym` emitted no relocation at all** and encoded its immediate as 0.
Fixed in `b1b950547b32`; lld 11/24 → 12/24.

The database says `LUI rt, imm12` — two operands. The logical said
`(outs GPR32:$rd), (ins GPR32:$rs, uimm12:$imm)` — three, with every caller
passing `R0` for the extra one. Its format E members have two, matching the
database.

**That one-operand difference is enough to mis-encode silently.** After
`materializeMultiOpcodeInstrs` the MEMBER's `MCInstrDesc` is what the encoder
reads, but the MCInst still carries the operands the LOGICAL was built with.
So the encoder took operand 1 — the dummy `R0` — where the immediate should
be. A register operand produces no fixup and encodes as its register number,
so the field read 0 and the relocation vanished, with no diagnostic anywhere.

Fixed by giving the logical the database's shape and dropping the `R0`
argument at all ten call sites. `FmtI`'s `Inst{19-16}` still wants an `rs`
field, so it is bound to a constant rather than kept as an operand — that
encoding is dead on this branch anyway.

#### This is a class, not one bug

Hundreds of logicals disagreed with their members, and every one is a place
the encoder can read the wrong operand — the failure mode `LUI` showed: no
diagnostic, a plausible encoding, a missing or wrong relocation.

The cheap standing check is the same shape as § 5.6's memberless sweep:
compare each logical's operand list against its members'. It is a `.td` fact
on both sides, so it can be checked without running anything. **Comparing the
counts is not enough** — see "the measurement was crediting a tie that never
happened" below, and `SEQ64`, which has two operands on both sides and still
mis-encodes because they mean different things.

#### The tied-writeback half, fixed in `9ed26f2cc23d`

The largest single group. A logical with `Constraints = "$rs = $rs_wb"`
presents **four** operands — the tied register once as an out and once as an
in — while the database describes the member with three, naming the writeback
out `rs` and giving it no in. llc emitted a register where the offset belongs:

```
before  s_lw_pre_imm r3, r1, r1
after   s_lw_pre_imm r3, r1, 1      getelementptr i32, i32 1 -> byte 4,
                                    scaled by 4 -> 1
```

The generator now restores the tie — renames the written-back out, inserts the
tied in, copies the `Constraints`. 202 member definitions.

**Note the asymmetry that hid it.** Hand-written asm was *fine*: the parser
builds the MCInst against the member, so the two agreed. Only **compiled** code
was wrong, and only in the operands after the tie.

#### The generator was deciding defs by spelling — fixed in `26370cf02c39`

**`X2SEQ32` was the thread that unravelled it, and the suspicion recorded here
was right: the dest-by-field-name rule was wrong.** It called an operand an out
when the bit-layout field it landed in was named `dest`. That is a *layout*
fact. An entry's field set depends on its (entry, unit):

```
2-entry entry0/ALU0   {"dest": "rsd1", "src": "rsd2"}
3-entry entry0/ALU2   {"dest": "   ", "src1": "rsd1", "src2": "rsd2"}
```

Both rows are `X2SEQ32 rsd1, rsd2`. The narrow row has nowhere to put a second
source, so `rsd1` sat in the field called `dest`; the wide row leaves `dest`
**blank**, which is the database saying the instruction has no register
destination at all. Its only write is `SFR_Write_Port`.

Three things follow, and each is worth more than the bug itself:

1. **The rule made the same instruction a def on one unit and not on another.**
   14 logicals disagreed with themselves that way. A destination cannot depend
   on which ALU executes the instruction, so no semantic reading of the field
   name was ever going to hold.
2. **It failed in both directions.** It invented a destination for the SFR
   compares, and it *lost* the real one for `LUI`, `ZERO_GPR`, `CSRR` and
   `MOVESFR2GPR` on six placements of seven — including on the branch that had
   just fixed `LUI`'s logical. Fixing the logical did not fix the members, and
   nothing said so.
3. **The database had stated the answer all along and nothing read it.** Every
   instruction carries `GPR/DR/AR/SFR_Read_Port` and `_Write_Port` over the
   same operand names its Syntax uses. A `grep` for those field names in the
   generator returned **zero**. They are the authority now.

This is the fifth instance of the same rule — § 4, § 5.2, § 5.6, § 6.10 and now
here. **Fold through the meaning, never through the spelling.** The spelling
was a field name this time rather than a mnemonic suffix, which is why it
survived four previous applications of the rule.

#### The measurement was crediting a tie that never happened

`--emit operand-agreement` did not ask the emitter what it had emitted; it
*modelled* it, and the model was wrong. It added one operand for a tied
writeback whenever the logical carried a `Constraints` — including for every
tie the emitter had silently skipped.

The emitter matched the tie **by name**, and the two sides do not use the same
names for one register: `F2MULAA32RS_HHLL` ties `$rd = $rd_in` where the
database calls it `rtd`, so nothing matched, no tie was restored, and the
member came out one operand short. The check added its optimistic `+1` anyway
and reported agreement.

**823 placements over 164 logicals were reported as agreeing while their
member really was one operand short.** The `968` this section previously
dismissed was closer to the truth than the `171` that replaced it. The check
now shares `member_operand_shape` with the emitter and cannot drift again.

The tie itself is read differently now. **Whether there is a tie, and where its
in sits, are CodeGen-side presentation choices that only the logical can
state** — the database says no more than that the register is read and written.
So both come from the logical, but **positionally**, which needs no agreement
about names. Two things forced that reading:

* the logicals do not agree with each other about where the tied in goes —
  `D_SDW_POST_IMM rtd, rs, imm6` puts it second, `PLDWWUA_POST ar_sel, rs`
  puts it first — and either is legal, because the AsmString names operands;
* restoring a tie the logical does not declare makes the member demand an
  operand CodeGen never builds, and `llc` then aborts inside
  `MCOperand::operator[]`. Object emission fell 424 → 408 on that alone. The
  member has to match its logical operand for operand; where the logical then
  contradicts the database, that is reported rather than papered over.

#### What is left: four axes, all on the logical side

```sh
python3 .../haydn_encoding.py --database ~/haydn \
    --emit operand-agreement --flags-from haydn-records.json
#   operand agreement: 3 logicals, 21 member placements disagree
#     arity 0   defs 0   kinds 0   ties 21 / 3
```

The generator no longer contributes a single disagreement. The first three axes
are mutually exclusive and are all places the **encoder** reads the wrong
operand; `ties` is judged independently, overlaps them, and is a **model**
defect rather than an encoding one.

| Axis | What it means |
|---|---|
| `arity` | different operand *count*: everything after the difference is read from the wrong position |
| `defs` | counts agree, `NumDefs` does not — a member that calls a source a def describes an instruction writing a register it does not write |
| `kinds` | counts and defs agree, but a position is a register on one side and an immediate on the other |
| `ties` | the database says a register is read *and* written and the logical does not present it that way |

**Three of the four axes are empty.** What each one caught before it emptied is
worth keeping, because none of them was visible to the count the section used
to carry:

* **`defs`** caught `SEQ64`/`SLE64`/`SLT64` — two operands on both sides, so
  every count-based check passed them, while the logical called the first a
  destination and the hardware reads it as the second source. They were never
  in the old list of 27 at all.
* **`kinds`** caught `D_SDW_CB_IMM`/`_REG` and `WBARWUA`, whose logicals put
  the selector immediate after the registers while the database Syntax and the
  members put it first. Counts and def counts agreed; only comparing what each
  position *means* found it.
* **`arity`** ended up naming six instructions where the *database* was wrong,
  not the logical — see below.

The four shapes, and what each cost:

| Shape | n | Fixed in | Cost |
|---|---:|---|---|
| SFR compares with no destination | 9 | `d3b8af57efac` | intrinsics and builtins become void (§ 7) |
| an extra dead operand | 5 | `d3b8af57efac` | **NOT `.td` only — see below** |
| `Behavior` reads a port the database omits | 6 | `ad03d0b4a86d` | a database repair, not a `.td` one |
| an accumulator the logical never declared | 91 | `f87120340f6e`, `358739d19303`, `c5004f82c0a0` | 87 intrinsic prototypes gain the accumulator |
| operand order against the Syntax | 3 | `a64cd1a686ec` | `.td` plus four GISel construction sites |

**The first two changed the generated encoding by not one byte.** The members
already had the right shape — the generator takes an operand's role from the
database — so it was the logicals that moved to meet them. That is the shape of
this whole section: the encoder was reading operands the logical had put
somewhere else.

**Correction: "an extra dead operand" was recorded as `.td` only, and it was
not.** `MOVE32` lost its `$rs2` in that commit and has ELEVEN C++ construction
sites — `copyPhysReg` plus ten in `HaydnBitSimplify`. Three were updated and
seven were not, so every OR32 / XOR32 / ORI32 / XORI32 identity fold and the
`AND32 rd, rs, rs` fold kept building a three-operand `MOVE32` against a
two-operand descriptor. That is not silent — the machine verifier rejects it
with "Extra explicit operand on non-variadic instruction" and the pass aborts
— but it survived because the one test that caught it was itself filed under
§ 5.4 as a stale expectation. Its assertions had been correct all along.

The other four of the five (`ABS32`, `ABS32S`, `CSRR`, `ZERO_DR`) have no C++
construction sites, which is the only reason the row looked true.

**The rule this adds: when a logical's operand list changes, the sweep is every
`BuildMI`/`buildInstr` of that opcode, not just the `.td`.** A `.mir` test that
spells the opcode literally counts too — `ZERO_DR 0` outlived the operand it
passed.

**The 85 accumulates were a live miscompile, not a modelling gap.** `FMULS16_HS00`
is `rtd = SAT(rtd - rsd1*rsd2)`; its intrinsic took two arguments, so GISel gave
the instruction a fresh vreg to accumulate into and the hardware added to
whatever the register allocator had left there. The target shape was already in
the tree — `X4CJMULA16S_H` has carried `FmtALU64Acc` + `$rd = $rd_in` + a
ternary intrinsic + `selectAccMAC` all along — so this was a rename onto an
existing vocabulary rather than a design.

`MOVEI_H`/`MOVEI_L` turned out to be the same shape and are fixed in
`c5004f82c0a0`. They write one half of the destination and preserve the other,
and with a one-argument intrinsic the preserved half was undefined — so the
pair could not do the one thing their builtin documentation describes, because
two independent defs cannot be chained onto one register:

```c
int64_t v = haydn_movei_l(acc, lo);   /* acc supplies the high half */
v          = haydn_movei_h(v,   hi);  /* chains onto it            */
```

**The three left on `ties` are a reason, not work.** `SLLI64`/`SRLI64`/`SRAI64`
are tied **only** because Bundle128's 48-bit `Fmt48_WideDR_RI6` has room for
one register, so an untied `$rsd` has no field to encode into. The database and
the format E members both give the two registers independent homes. It comes
off with the Bundle128 formats (§ 5.2); until then it costs a register copy and
no correctness.

Treat every count as a standing hazard, not a fixed list: they should only ever
shrink, and any new mismatch is a new place the encoder can read the wrong
operand.

#### Six of them are the database contradicting itself

`FMULA32S_{HH,LH,LL}` and `FMULS32S_{HH,LH,LL}` have a Behavior that plainly
accumulates —

```
rtd = SATQ1.63(rtdQ1.63 + SATQ1.63(rsd1[63:32]Q1.31 * rsd2[63:32]Q1.31))
```

— while their `DR_Read_Port` lists only `rsd1, rsd2`. **The port list is
wrong, not the logical**, which declares the tie correctly.

Same class as § 5.3's 76 mapping rows: a disagreement *between* two statements
the database makes about itself, which no gate that checks the database against
the *encoder* can see. `--check` now refuses on it, and the repair is forced
rather than chosen — the Behavior assigns the register from an expression
containing itself, so it is read, and the register file follows from the alias:

**And it is an inconsistency inside the port data, not a convention we misread.**
`X2MULA32` — the § 5.3 instruction — *does* list its accumulators in
`DR_Read_Port` (`rtd1, rtd2, rsd1, rsd2`), and that is partly how the layout was
shown wrong. So the database states the accumulator-is-also-read rule correctly
for the four-register MACs and breaks it for these six. Worth saying out loud to
whoever owns the database, because the obvious first reply is "the ports only
list explicit source operands", and the delivery itself refutes that.

```sh
python3 .../haydn_encoding.py --database ~/haydn --fix-read-ports --write
#   6 read port(s) repaired over 6 row(s)
```

The file is CRLF and one field per line, so the edit is byte-level: +42 bytes,
six lines, every line ending intact. Re-running is a no-op. The database is
version-controlled by neither repo, so this is a mode rather than a note — see
§ 5.3's account of a correction that did not travel. `GOLDEN_INPUTS.sha256` is
re-pinned in `simulator` (`dfd2078`).

Note what did **not** catch this: `--emit roundtrip` never sees the logical at
all, `--check` only validates the database against itself, and the encoder and
decoder agreed with each other because both were reading the member. Only a
relocation — an outside reference — could expose it, which is § 5.4's point
again.

### 5.13 Immediate signedness was read off the field's NAME — CLOSED

A fifth axis of § 5.11's shape, found by triaging § 5.4's remaining failures.

```
llvm-mc:      { lui r3, 4095 }   ->  a0 66 fe 1f
llvm-objdump: a0 66 fe 1f        ->  { lui r3, -1 }
```

Same twelve bytes both ways, so **`--emit roundtrip` was green** — it checks
the encoder against the decoder and the bits were never in dispute. The
disagreement was between the parser, which kept what was written, and the
decoder, which sign-extended a field the database defines as a bit pattern.
`operand-agreement` was green too: its `kinds` axis compares operand KIND, and
`simm20` and `uimm20` are the same kind.

The generator chose the class from the field's NAME — `uimm...` unsigned,
anything starting `imm` signed. A spelling convention standing in for a
semantic fact. It got `ADDI32 rt, rs, imm20` right by luck
(`rt = rs + SEXT32(imm20)`) and four instructions wrong, and the LOGICAL had
the right class all along, so the `.td` disagreed with itself.

**The Behavior states it.** Read it there, the same way § 5.4 says to read the
load/store scale there:

| Behavior | signedness |
|---|---|
| `SEXT32(imm20)`, `$signed(imm8)` | signed |
| `ZEXT32(imm20)` | unsigned |
| the field inside a `{...}` concatenation | unsigned — a pattern, not a number |
| a shift amount, `rs << uimm5` | unsigned |
| an address offset, `rs + (imm6 << 3)` | signed |

Anything the Behavior does not place is a hard error rather than a default;
defaulting is what produced this. Over 683 instructions it classifies 77
immediate fields with nothing left over.

Fixed in `7cac01fb0b97`: `LUI` (`{imm12, 20'b0}`) and `ANDI32` / `ORI32` /
`XORI32` (`ZEXT32(imm20)`), 13 member defs. `andi32 r1, r2, 1048575` printed
as `-1`, which reads as `rs & 0xFFFFFFFF` rather than `rs & 0xFFFFF`.

**`MOVEI_H` / `MOVEI_L` are deliberately NOT changed** although their Behavior
splices `imm32` the same way. At 32 bits `simm` and `uimm` cover the same bit
patterns and the printer's sign extension is the identity — `movei_h d0, -1`
already round-trips exactly — so the only thing the class would change is which
spellings the parser accepts, and negative literals are what existing code
writes. The carve-out is on width and is stated where the width is known.

`hi20-fixup-compensation.s` and `move-instructions.s` went green **without
their assertions being touched**. They asserted `lui r3, 4095` and were right;
relaxing them would have frozen the defect in. That is the § 5.4 rule in its
sharpest form: a red gate is not evidence that the expectation is stale.

### 5.14 SET_HWLOOP_F2's label fixups wrote outside their fields — CLOSED

`bqriir32x32_df1_process` compiles to an object containing one bundle that
disassembles as `<unknown>`. It is the `set_hwloop_f2` that programs the
hardware loop: llc emits it in the `.s`, and the same bundle is undecodable in
the `.o`. **The binary contains a hardware loop whose trip count is never
programmed.**

`SET_HWLOOP_F2_P31_ALU0` lays entry1 out as

```
e1{29-18} = uimm12_offset2      e1{17-12} = uimm6_offset1
e1{11}    = hwlr_sel            e1{30}    = reserved, must be 0
```

and the two label fixups spill across all three. Holding the loop body at 69
bundles and varying only `N`, the filler bundles between the instruction and
the START label:

| N | uimm6_offset1 | uimm12_offset2 | reserved e1{30} |
|---:|---:|---:|---:|
| 0 | 0 | 105 | 0 |
| 4 | 0 | 111 | 0 |
| 5 | 32 | **2160** | 0 |
| 11 | 32 | 121 | **1** |

The body never changes size, so `offset2` must not move; `offset1` must track
the start distance and does not. **Below the threshold this is a silent wrong
value** — a hardware loop with the wrong bounds, which executes. Only when the
reserved bit finally sets does the disassembler refuse the bundle, and that
refusal is the entire reason any of it was visible.

The numeric-operand path range-checks correctly — `set_hwloop_f2 1, 0, 4096,
r4` is rejected — so only the label path is unguarded. Same class as § 5.8, and
for the same reason no gate saw it: **`--emit roundtrip` never applies a
fixup**. It round-trips the placement's own bits, and a fixup writes over them
afterwards.

#### Fixed in `aed9c9de841d` — the key could not tell two fields apart

§ 5.8's table is keyed on `(FieldSize, entry count, entry index, mapping)`.
**That does not identify a field.** `SET_HWLOOP_F2` carries a 6-bit and a
12-bit offset in ONE entry, and another instruction has a 6-bit immediate at
the same `(entry, mapping)`, so the lookup answered with whichever the
generator had seen:

| field | needs bundle bit | table said |
|---|---:|---:|
| off1, 6-bit | **49** | 62 |
| off2, 12-bit | **55** | 54 |

One mis-key explains both halves. off1 written at 62 lands **inside** off2's
field (55..66) — off2 reads back carrying off1's distance and off1 reads back
zero, the silent form. And 62 + 5 = **67**, the reserved bit, which is why a
large enough start distance made the disassembler refuse the bundle.

Two omissions, both the same one. The generator scanned only operands literally
named `imm`; `SET_HWLOOP_F2`'s are **`imm1` and `imm2`**, so they were never in
the table and the lookup fell through to someone else's row. And the key was
missing the **type code** — which the generator's own docstring already said
"would" be needed, for the narrow fields it was dropping. It was needed for
more than that.

The key is now `(FieldSize, entry count, entry index, mapping, type code)`, and
because the type code's own position depends on `(entry, mapping)`, the `.inc`
emits a second table to locate it: read the mapping, find the type code, read
it, match a row. Every part still comes from the bundle image alone — neither
MC nor lld knows the member, which was § 5.8's constraint and is kept.

Measured before writing code: adding `imm1`/`imm2`/`imm3` under the old key
gives **8 colliding keys**; adding the type code gives **68 rows and zero**.
The generator now refuses outright if a type code ever sits at two positions
for one `(entry, mapping)`.

**`FieldLsb` was not what was wrong, and did not change.** For an instruction
field it is unused — `patchRelocFieldInBundle` resolves the position from the
image — and its only reader is `readRelocAddend`, which is REL-only and so dead
for Haydn (RELA). Both places say so in comments now, because "fix the
FieldLsb" is the wrong instinct here and is exactly what RISK-6 was.

`hwloop-fixup-reserved-bit.s` and `d486-hwloop-fieldlsb-bundle128.s` are
regression tests rather than XFAILs now; keep both, because one shows the loud
symptom and the other the silent one. `bqriir32x32_df1-e2e.ll` gets its
`set_hwloop` check back — asserted alone, never alternated.

#### The second instance, and why it is the one to keep

`d486-hwloop-fieldlsb-bundle128.s` was in § 5.4's stale-premise pile because
it asserted a Bundle128 byte position. Its QUESTION was never stale: "do the
hardware-loop offset fixups land in the fields the instruction declares." The
Bundle128 answer was no, because `FieldLsb` had been transcribed from the
legacy 48-bit parcel. The format E answer is also no, for a different reason,
and the numbers are worse than the loud case:

```
.Lbody at +12, .Lend at +24   ->   off1 = 0, off2 = 3
                                   printed: set_hwloop_f2 0, 0, 12, r1
```

`off1` is zero under any choice of units, and `off2` is carrying the distance
that belongs to `off1`. **No reserved bit is set**, so nothing refuses the
bundle and no disassembly shows `<unknown>`. The only reason this is visible
at all is that the test asks about the fields rather than about a byte.

It asserts `0, 12, 24, r1` and needs no byte pattern: the printed operands say
the same thing and survive a layout change, which `byte0 == 0x08` did not.

**This is the half that would have found the defect on its own.** The loud one
depends on a distance large enough to reach the reserved bit; this one shows
the spill for any distance at all, because it asks what the fields contain
rather than whether the bundle decodes.

`bqriir32x32_df1-e2e.ll` is deliberately kept **green** rather than left red
like § 5.4's `f2mulzaa32rs` pair: it carries twenty assertions and a red e2e
test stops being a signal for the other nineteen. Its comment records what is
not asserted there and why.

#### How it stayed hidden: an alternation with a dead arm

The check was `BUNDLE-DAG: {{beqz|set_hwloop}}`. An alternation passes on
EITHER arm. The loop became a hardware loop and the guard's condition got
inverted to `bnez`, so the `beqz` arm went stale — and the arm that mattered
was never verified in the first place, because `beqz` had been matching all
along. The same file had `{{slt32|set_hwloop}}` two lines up, where `slt32`
still matches and so still hides it.

This is § 5.4's `f2mulzaa32rs` shape exactly — *"a wrong expectation and a
missing feature covering for each other"* — and it suggests a rule:
**an alternation over things that are not alternatives is a hiding place.**
`{{beqz|bnez}}` is fine, they are two spellings of one fact. `{{beqz|set_hwloop}}`
is two different facts, and it asserts neither.

### 5.12 The unit axis did not fire on the AsmParser's hinted path — CLOSED

`{ beq r1, r2, 8; bnez r3, 16; beqz r4, 24 }` assembles. Three ALU0-only
branches, three control transfers on one branch unit, and llvm-mc emits a
parcel: `BEQ_P30_ALU0` / `BNEZ_P31_ALU0` / `BEQZ_P32_ALU0`, three positions and
one unit. § 3's rule is unqualified — "an entry maps to exactly one unit; no
two entries in a bundle may share a unit" — and 16 logicals are ALU0-only, all
of them branches, `JAL`/`JALR` and the `SET_HWLOOP` family.

**Every rejection the assembler currently produces is placement exhaustion,
not a unit conflict.** That is what makes this easy to miss: the cases that
look like the unit check working are all explained without it.

| bundle | rejected? | why |
|---|---|---|
| four `add32` | yes | no four-entry composite exists |
| `beq` + `bne` + `blt` | yes | all three exist only at P30/P31 — no third **position** |
| two `d_*wua_post` stores | yes | both exist only at position 0 |
| `beq` + `bnez` + `beqz` | **no** | all three positions exist; only the unit is left, and it is not checked |

#### Mechanism

`HaydnBundle.h`'s `add(I *Instr, MCSlotKind HintSlot)` resolves the unit with
`unitBitsForMember(MII, Opcode)`, which derives it from the member NAME. On the
hinted path the AsmParser has already folded the opcode to its LOGICAL
(`getLogicalBaseOpcode`), and a logical name carries no
`_P<form><pos>_<UNIT>` suffix, so the call returns 0, the hint is taken, and
nothing is claimed. `OccupiedUnits` therefore never accumulates, and the
`pickSlot` fallback — which *does* seed `Probe.OccupiedUnits` correctly — is
never reached for well-formed positional text.

The code says so, and was right when it was written:

> for a logical the member is picked later, so nothing is claimed here and the
> axis stays permissive. Under Bundle128 both are 0.

§ 7.1 predicted exactly this: *"treat it as untested against real packing until
the switch lands, and expect the first real bundles to be where it earns or
loses trust."* This is that moment, and the hinted path loses.

#### Fixed in `08a144aa56cc`, on the answer it was held for

**A NOP occupies no unit.** That was the open hardware question and it is
decided. It is also what makes the fix cheap: padding stays wherever it lands,
so **no NOP-padded bundle's bytes change** and § 5.4's expectations did not
need re-basing.

Two causes, and either one alone leaves the axis silent — fixing one and
stopping would have looked like progress and changed nothing.

1. **The AsmParser built a plain `HaydnMCFormats`, with no `MCInstrInfo`.** A
   unit is read off a member's name and the name comes from `MII`, so nothing
   was ever claimed regardless of what the rest of the code did. § 7.1 named
   this hazard before the switch landed; it was live on the one path that
   matters for hand-written asm.
2. **`add(I*, MCSlotKind HintSlot)` asked `unitBitsForMember`, which answers 0
   for a LOGICAL** — it reads a `_P<form><pos>_<UNIT>` suffix a logical does
   not have. The hinted path holds logicals, because the parser folds them
   deliberately, so the hint was taken while claiming nothing. It now asks the
   placement alternatives AT THAT SLOT for a free unit — the same question
   `isHintSlotLegal` already asked about slots.

`opcodeClaimsUnit` carries the NOP decision, read off the name like the unit
itself so a logical and its members answer alike. **It has to be asked rather
than inferred**: every NOP member still NAMES a unit — there is one at all 18
(unit, position) pairs — so `haydnMemberUnitBits` answers for a NOP exactly as
it does for a real op. The exemption applies on both sides: a NOP claims
nothing AND is never excluded, including inside the solver, where seeding it
with the occupied units would have moved it to another alternative and
re-based every padded bundle for no reason.

Rejected now, each for the unit and nothing else:

```
{ beq r1, r2, 8; bnez r3, 16; beqz r4, 24 }   three ALU0, three positions
{ beq r1, r2, 8; bne r3, r4, 16 }             two ALU0
```

**The CodeGen path was already correct**, and that is verified rather than
assumed: `encodeBundleE` builds its bundle `WithMII` and uses the no-hint
`add`, which goes through the solver where the units were seeded properly. A
scan of 159 bundles from a compiled `bqriir` object found zero unit collisions
among non-NOP entries.

`bundle-unit-collision.s` is a regression test now. `bundle-canadd-reject.s`
keeps its warning the other way round: the unit half IS enforced, but none of
ITS cases needs it — each is explained by placement exhaustion alone, which is
exactly why the axis being inert went unnoticed.

### 5.15 The sysroot rebuild — how far the simulator gets, and where it stops

**Run before § 5.5 is done, deliberately**, because § 2's rebuild is the only
thing that compiles libc and links a real image, and it found defects no lit
test reaches. The suite does not pass and cannot until § 5.5 lands; what it
gives is a list of things that were broken before the executor ever mattered.

#### Compiler defects it found (fixed, `c2a7f4d92d5a`)

* **`HaydnPostSelectOptimize::tryCSEConstantDR64` aborted on a frame index.**
  It matches an OPCODE and then reads operand kinds, and `ADDI32
  %stack.7.new_char.addr.i, 0` is a perfectly ordinary `ADDI32` whose source
  is a frame index until PEI rewrites it. Ten crashes, every printf
  translation unit. **Opcode-matching says nothing about operand kinds** —
  the guards are on every `getReg`/`getImm` on that path now.
* **libc's `setjmp`/`longjmp` inline asm** used `ld32`/`st32`/`ld64`/`st64`
  with BYTE offsets and the `_w` suffixes. § 5.6's rename is "a rename plus a
  range collapse": the immediate is an element index, so the frame's byte
  offsets divide by 4 and 8. This is the case § 5.4 warned assembles and
  addresses the wrong slot.

#### BSP defects it found (fixed, `simulator 06616dd`)

* `crt0.s` used `ld32`, which still PARSES (the logical survives) and then
  fails at emit with "no Bundle128 form".
* **The linker script padded the instruction stream with zeros.** It aligned
  the END of `.text` to 16 and asserted `__text_end & 15 == 0`. A parcel is 12
  bytes; 12 does not divide 16; the fill is zeros; and **an all-zero parcel is
  not a bundle** (bit[2:0] must be `0b111`). Every image ended with one
  `<unknown>` and the harness failed the run before execution started —
  **182 of 221 tests, from one twelve-byte hole**. The end alignment is
  `ALIGN(4)` now and the assertion says what it always meant: a whole number
  of 12-byte parcels.
* `.p2align 4` in `crt0.s` and `hostcall_haydn.S`, same arithmetic as § 5.9.

#### Where it stops

```
run_c: FAIL stop expected GUEST_EXIT got BAD_PC
stop=BAD_PC guest_exit=0 bundles=73
```

The image links, disassembles clean and **executes** — 73 bundles before a bad
PC. That is the executor: the model, the dispatchers and the catalog, which is
exactly § 5.5's list and which § 7 freezes until § 5.2 lands. 180 of 221 fail
there. **Do not read that number as a regression** — the suite has never run
against format E, and its previous 220/221 was against a Bundle128 toolchain.

The golden database pin was verified intact before and after: all eight inputs
match `GOLDEN_INPUTS.sha256`.

### 5.16 The BAD_PC was never the executor — four compiler defects were

§ 5.15 stopped at `stop=BAD_PC after 73 bundles` and repeated the plan's own
attribution: the un-ported executor. **That was wrong, and the wrongness has a
shape worth keeping.** BundleSim reports a wrong return address as BAD_PC, so a
compiler that corrupts `lr` and a simulator that cannot execute look identical
from the outside. The suite went 41/221 → **220/221** without touching the
executor. The one remaining failure is CB-130, which is what failed before the
switch too.

**Diagnose the BAD_PC before believing what it is attributed to.** All four
defects below were reachable only by running a program; every one of them was
invisible to lit, and two were invisible because the test that covered them
checked a shape instead of a number.

#### 1. PEI put byte offsets in a scaled field — `1207787d2b88`

`emitCSRStore` / `emitCSRLoad` handed the raw byte offset to a
`simm6:$scaled_imm`, whose field holds ELEMENTS. `emitCSRLoad` is the one that
shows how: its range test had already been migrated to `isInt<6>(Offset >>
Shift)` and the value below it had not. On a 40-byte frame, 12..28 encoded as
elements (four times too far) and 32/36 truncated to −32/−28. `lr` came back
garbage and the program died at its first return — 73 bundles into `return 7`.

`haydnScaledLSImm()` existed the whole time and its comment predicted exactly
this. A sweep of every `BuildMI` of a scaled LS opcode says these two were the
last unconverted sites.

#### 2. lld dropped the entry base at a thunk — `3c47276ece84`

A branch resolves from the BUNDLE; the relocation points at the ENTRY. The
emitter puts the entry's byte base in the addend so `S + A − P` cancels it.
lld clears the addend on thunk redirection (`rel.addend = -getPCBias(...)`),
and `getPCBias` returned 0 for Haydn, so far calls landed `base` bytes short:
0 for entry 2, **4 for entries 0 and 1**. BundleSim refuses such an image
outright, which is the only reason it surfaced.

Hexagon already uses `getPCBias` for the identical problem — packets, not
parcels — so the fix is the seam that exists rather than a new one.
`thunk-addend.s` should have caught this and did not: `lui{{.*}}r0,` matches
any immediate, and the file never compares an address to anything.

#### 3. Every DWARF line address rounded down to 16 — `7476a026f969`

`MinInstAlignment = 16` reaches exactly one place: the line program's
`minimum_instruction_length`, the unit MCDwarf DIVIDES address advances by.
A `.loc` three bundles in reported 0x20 for an instruction at 0x24. It is 1
now, not 12 — functions align to 4, so not every advance is a whole parcel and
12 would bring the truncation back for exactly those cases.

#### 4. 48 real instructions were still `isPseudo` — `48c963aa7901`

`let isPseudo = 1 in {` at `HaydnInstrInfoManual.td:2925` covers everything after
it that does not opt out. Format E generates members for 48 of those logicals,
so they are real instructions and the flag is the last thing saying otherwise.

The consequence is worse than a missed optimization, and **intermittent**,
which is why it survived. The packer skips pseudos, so each issued alone. The
AsmPrinter's BUNDLE-child loop has `else if (I->isPseudo()) continue;` and
**drops** it — but only for an instruction that ended up inside a bundle. A
standalone one is lowered normally. The same opcode was therefore emitted
correctly in one function and deleted silently in the next.

`$d0 = SEXT32T64 $r7` vanished, leaving `{ nop }`, and the `D_SDW` below it
stored whatever `d0` held before. Nothing warned — not the verifier, not the
bundle checker, not `-verify-machineinstrs`.

Keep the audit; it is a one-line invariant. Intersect the `MCID::Pseudo` bit in
`HaydnGenInstrInfo.inc` with the logicals having `_P<form><pos>_<UNIT>` members
in `HaydnFormatEEncoding.td`. **48 before, 0 after, and it must stay 0.**

#### What actually found it

Nothing in the compiler. yarpgen seed 2 returned a wrong checksum at −O0 and
only at −O0; bisecting five test functions to one, then its 38 globals to
five, gave five `signed char`s whose widening was wrong, and the minimal case
was three adjacent one-byte globals all reading back the same byte. § 5.15's
lesson generalises: **run a program.**

### 5.5 BundleSim side — DONE, `simulator 0767a9b`

Landed after § 5.2, not with it. The "same commit" requirement was about the
two trees agreeing; § 5.2 had already landed and the catalog was the half left
behind, so the freeze had inverted — see § 5.16.

* **`generate_catalog.py` is ported.** `instruction_type_index.json` carries the
  same `Syntax`/`Behavior`, so most of it was a change of container: 682
  mnemonics, and **the only rows that move are the six predicted below**.
  Nothing else in 682 differs, which is what says the port is faithful rather
  than merely green. `operands_info.md` had also changed shape (the leading
  index column is gone) and its own emptiness check is what turned that into a
  stop rather than a catalog of zero-width immediates.
* **`legal_slots` and `cfg_only_s0` are gone**, and the catalog has no slot
  column at all. The first came from the FILENAME of the per-slot list; the
  second was a hardcoded mnemonic set. Format E gives a slot no capability, and
  the rule that replaced them — no two entries of one bundle map to the same
  unit — is a property of a BUNDLE, so it cannot live in a per-instruction row.
  `--enforce-slots` therefore decides nothing per instruction. **The units are
  real data and already have an owner**, `generate_unit_model.py`; do not
  re-derive them in the catalog.
* **The six AR entries were transcribed, not adapted.** The operands did not
  merely disappear — the behaviour changed. Every UA form post-increments `rs`
  by a fixed +8, so there is no reverse funnel and no second memory line: a
  load step reads ONE aligned line, hands out a window over `{line, ar}` and
  leaves the line in `ar`, with the offset coming from the address (`rs[2]` for
  TW, `rs[2:1]` for QHW). `WBARWUA` writes 2, 4 or 6 bytes by `rs[2:1]` and
  **nothing** when those bits are `00`; it used to write the whole aligned line
  over the top of whatever followed the stream.
* `semantic_family_map.inc` regenerated, `SEMANTIC_SNAPSHOT.sha256` re-pinned
  (two model bodies), documented in `SEMANTIC_BASELINE.md` the way the previous
  re-pins were.
* **`BUNDLESIM_SEMANTIC_BASELINE_SHA256` moves, and gains a definition**
  (`simulator 2ede2a6`). It is what a saved `.bsci` is keyed on, and it was a
  hand-picked value unchanged since the repository's second commit — through
  two model re-pins, one of them behavioural. Nothing checked it, so it sat
  still while the eighteen bodies it identifies moved underneath. It is now the
  SHA-256 **of `SEMANTIC_SNAPSHOT.sha256`** and `verify_baseline.sh` asserts
  the equality, so re-pinning is: regenerate the snapshot, copy its digest.
  A value with no derivation is one nobody can be wrong about.

#### `cb100_ar_unaligned` — what the freeze was protecting

The AR family is the one shape that touches both trees at once, and **neither
tree's own tests can see them disagree**: the assembler will happily encode a
spelling BundleSim will not bind, and BundleSim will happily bind one the
assembler will never emit. There was no test that ran a real AR sequence
through the compiler and executed it. There is now; perturbing its cursor by
four bytes moves it from exit 42 to exit 2.

#### Three freshness gates were not running

Found while arming the catalog check, and the more useful half of this step.

* `bundlesim_new_catalog_generated` was conditioned on
  `slot0_alu_instruction_list.json`, which stopped being shipped. The guard did
  its job — no check with missing inputs — and then the check never armed again
  after the generator was ported to the file that replaced it.
* Worse: `_BUNDLESIM_GOLDEN_DIR` was recomputed from the environment on **every**
  configure, including the implicit re-run cmake does when a `CMakeLists.txt`
  changes and has no environment. Editing an unrelated build rule silently
  disarmed the golden pin, the catalog check and the unit-model check together.
  They report that as `Skipped`, or by not existing. It is cached now.

`ctest` goes 220/221 → **224/225**: three gates that were absent now run, the
golden pin no longer skips, and `cb100` is new. The one failure is CB-130.

---

### 5.17 CB-130 — the last failure, and it was one line

`bundlesim_reg_cb44_o2_stale_cond_max_reduce` was the sole remaining ctest
failure and predated the switch. **`simulator ctest is now 225/225.**

`.clampMaxNumElements(0, S1, 1)` on `G_BUILD_VECTOR` could not have worked.
`clampMaxNumElements` builds its target with `LLT::scalarOrVector()`, which
returns a **scalar** for a count of one, so the rule asked
`fewerElementsVector` to narrow `<2 x s1>` to plain `s1` and
`fewerElementsVectorMerge` asserted. **One element is not a smaller vector.**

Deleted rather than repaired, because there is nothing for a `<N x s1>` build
to legalize INTO. It is an artifact — a scalarized vector `G_ICMP` builds it,
and the matching unmerge arrives when its `G_ZEXT`/`G_SELECT` users are
scalarized in turn. Falling to `.lower()` reports UnableToLegalize, the
legalizer defers to the artifact combiner, and the pair cancels.

**The comment defending the clamps was wrong about this one.** It says a bare
`.lower()` hangs the legalizer on `pr28982a` at -O2. Measured: **pr28982a
hangs with the clamp present too**, and its loop is in `G_EXTRACT_VECTOR_ELT`
on `<16 x s32>` with a variable index, growing a chain past register `%300000`
— a separate open bug, and not evidence for anything here. Checking that cost
two rebuilds and was worth both: without it the fix looks like a regression
trade.

The original CB-44 symptom — a stale condition register leaving `mxIdx` at 1
instead of 14 — is gone **with** the crash rather than merely uncovered by it:
the case exits 145 and MATCHes the host oracle.

---

### 5.18 CB-143 — two pieces landed, and one of them had been inert for weeks

**The logicals were still on the slot machine.** The members were retargeted
when format E landed; the logicals inherit an itinerary from their format
class, and every one of those still named `SLOT0/1/2` — 543 of 4677 defs, and
the ones the pre-RA scheduler reads. It cannot be done by editing the format
classes: **a format is an encoding shape and `Available` is a machine fact, and
they are not the same partition.** `Slot12_ALU` alone covered defs belonging to
`Unit_ALU0ALU1ALU2_L1` (104), `Unit_MAC0MAC1_L2` (70) and `Unit_MAC0MAC1_L1`
(38). Derived per def from the database, 512 retargeted (`51c24ca3e2d7`).

One fact the database cannot state, and it is load-bearing:
`Slot12_MAC_AccFirst` carries per-operand cycles `[2, 2, 1, 1]` — the
`FmtALU64Acc` order with the accumulator at index 1. The generated classes
carry a single `Data_Latency`, so retargeting the 225 acc-MACs onto plain
`Unit_MAC0MAC1_L2` dropped it, and `postmisched-stall-idle-nop.mir` caught the
loss. Hence a hand-written `Unit_MAC0MAC1_L2_AccFirst`. **That is the division
the two schedule files should keep**: generated classes carry what the database
states, hand-written ones carry what it does not.

#### The post-RA scheduler could not see a single unit

`HAYDN_NUM_FU_BITS` was **3**, for the retired slots. `HaydnItineraries`
declares those three *and* the seven units, in that order, so a `Unit_*`
itinerary sets bits 3..9 and `HaydnFuncUnitWrapper`'s loop over 0..2 recorded
**nothing**. Every instruction reached the scoreboard with an empty Required
set from the day the members were retargeted.

It produced no wrong answers, and that is why it lasted: legality comes from
the placement search (§ 7), so the packer kept rejecting what the hardware
rejects. What was lost is the scheduler's ability to stop proposing those
cycles — B4's "at most one store per bundle" was invisible to it.
`144a622c6f8f`.

#### The tests were pinning an instruction set that does not exist

Five MC files assembled the retired `ld32`/`st32`/`ld8`/`ldu16`/… and CHECKed
for them by name; 27 `.mir` tests named the opcodes. **They passed because the
dead defs survived as parse-only shells with no format E member** — they cannot
be encoded, so nothing those lines tested was reachable.

Moved to the live spellings (`080486f5b000`), and **the immediate had to be
rechosen rather than converted**: it is an element index in a `simm6` field, so
`ld32 r6, r7, 1024` is 256 elements and does not fit at all. The values walk 0,
±1, ±2 and the ends of the field. Both load/store MC files now say plainly what
they canNOT see — **the scale**, because the encoding holds the six bits and
nothing else, so a swapped shift between two widths round-trips clean. That is
checked by executing, in the simulator's `cb99`.

The `.mir` rewrite is a rename plus a rescale **except for the POST forms**:
their defs already declared `simm6:$scaled_imm`, so those immediates were
already element indices and dividing again would have moved the address. Three
lines failed to convert and said so, which is how it was caught.

#### 31 defs deleted, and the two that stayed

Every def with no format E member and no database entry: fifteen `_W` peers,
thirteen retired load/store spellings, `ASR32`/`LSR32`/`SHL32` and
`SEXT_GPR32_TO_DR64`.

**`LD32` and `LD64` are deliberately not among them.** Deleting those two
breaks `HaydnBundle::canAdd` — a store followed by an ALU op stops packing —
and `HaydnInstrInfo.cpp` decides things by name substring
(`N.contains("LD64")`). Recorded rather than guessed at; they are load-bearing
in a way the other 31 are not.

#### An accidental directive, found in passing

`vliw-packetizer-waw-hazard.mir` explained itself with the sentence *"a
CHECK-NOT: BUNDLE line fails"*. FileCheck reads directives out of comments, so
that WAS a `CHECK-NOT` — before the first `CHECK-LABEL`, matching nothing,
passing vacuously. § 6.16 again, and this time in a file whose whole subject is
a negative.

#### The three packer axes — two were already modelled

**Measured before building, and B4 was wrong about two of its three.**

*Per-entry immediate width — modelled by construction.* The generator emits a
member only where the instruction FITS, so `ADDI32` has `P20_ALU0`/`P21_ALU1`
and nothing else while `ADD32` has all seven, and the packer searches over
members. B4's own example is what it does: a 3-entry bundle holding `addi32`
is rejected, the 2-entry form assembles.

*Register port budget — bundle-wide already.* `HaydnFuncUnitWrapper::operator|=`
accumulates (`GPRReads += …`) and `conflict()` is called with the running
cycle, so the sum IS the bundle's demand. **§ 5.18's own commit message called
this a pairwise approximation and was wrong** — worth correcting rather than
leaving, because that sentence would send someone to rewrite something that
works. **And then this paragraph did the same thing again**, one sentence later:
it said "what *is* pairwise is unit exclusivity, in one direction only: three
instructions each needing `{ALU1, ALU2}` pass and cannot all issue". Measured,
and that mechanism is not there either — see § 5.23. Twice in one paragraph is
the pattern worth naming: **a claim about which check is approximate is not
cheaper to guess than to measure**, and both guesses here pointed at code that
was already right.

*Store/load overlap — genuinely missing.* `3f5f9b21dc6f`.

> § Constraints: "Within the same bundle, a store and a load must not target
> overlapping memory addresses. … the hardware detects the conflict and raises
> an exception."

Nothing enforced it, and LOADSTORE0/LOAD1 are different units — so the packer
could emit a bundle that **faults on real silicon**, with neither lit nor the
simulator saying so, because BundleSim does not model the check either.

The rule records FACTS about each access, not the `MachineInstr`. Instructions
are replaced during a region (`setDesc` for the chosen member), and
dereferencing a dead one's memory operands is a **segfault inside
`MemOperandsHaveAlias`**, not a wrong answer — that was the first version, and
it crashed 36 tests. Cost is ~2% bundles.

#### And a pre-existing bug underneath it

`HaydnInstrInfo::getMemOperandsWithOffsetWidth` returned the **element index as
a byte Offset** — § 5.6's scale trap again, in the routine that everything asks
about addresses: memory clustering, `areMemAccessesTriviallyDisjoint`, and now
this. Every distance came out `width` times too small, so two adjacent words
looked overlapping. The `PreImm` path directly above it already had the shift.
The byte and halfword forms were absent entirely, so they answered "no
information" and every caller assumed the worst.

---

### 5.19 The wide vector had to stop existing at its producer

`clang` **hung** at -O2 on gcc-c-torture `pr28982a`/`pr28982b`: 356505
legalizations, register numbers past `%300000`, no end. Recorded in § 5.17 as a
separate open bug when it turned out not to be evidence for keeping CB-130's
clamp; this is it. `f905e7be8c03`.

`G_FREEZE` was **`alwaysLegal()`**, so a `<16 x i32>` was allowed to exist as a
VALUE — and nothing else in the target can hold one. Every consumer narrowed it
locally and something re-merged the pieces to feed the next. **That is a loop
the legalizer has no reason to leave.**

#### Two attempts at the consumers, both reverted

Worth recording because each looked like the answer and each only moved the
loop:

* The custom vector→vector unmerge lowers by extracting elements **from the
  source**, which is a reduction only while the source is one step above legal
  — a `<16 x s32>` extract is itself illegal and fewer-elements it back into an
  unmerge of that same vector. Capping the rule at a 128-bit source moved the
  loop one level down, to `<4 x s32>`.
* Lowering that unmerge through a scalar bitcast instead — which the code's own
  comment had rejected as "bitcast-to-s512 thrash" — moved it again, to
  `G_CONCAT_VECTORS` re-forming the `<16 x s32>`.

**Neither is kept.** Narrowing `G_FREEZE` alone is sufficient, and churning a
deliberate lowering choice for no gain would have been the worse commit. The
lesson generalises past this bug: **when a type keeps reappearing, the fix is
at whatever is allowed to produce it, not at the consumers that keep meeting
it.**

Reduced to one function — load a wide vector, freeze it, index it with a
variable — and the difference is not subtle: without the fix `llc` does not
terminate, with it the whole thing legalizes in 119 steps.
`gisel/freeze-wide-vector-no-hang.ll`.

#### And the three CB-130s still sitting there

`clampMaxNumElements(…, S1, 1)` was still in `G_EXTRACT_VECTOR_ELT`,
`G_INSERT_VECTOR_ELT` and `G_CONCAT_VECTORS` — the construct that **can only
assert**, unreached rather than working. Removed (`2ede04445ecb`); no test
moves, which is the expected result and also the reason not to have left them.

Behind them is a real gap, visible now instead of disguised:
`extractelement <2 x i1> %c, i32 %i` reports *"unable to legalize"*. Opened as
CB-144.

---

### 5.20 CB-136 — a header you cannot include is not an API

`#include <haydn.h>` alone emitted **159** *"needs target feature"* errors and
stopped. The default set is `-bit-reversed,-circular-buffer,-simd`, and 168
wrapper BODIES call builtins needing one of the absent three, so the include
failed before the user had written any code. It had been worked around
consumer-side: BundleSim's `run_c` passes `-mcpu=haydn`, which turns the
features on and hides it. `6696e07b98e6`.

**The per-op feature expression was already parsed into `Entry::Features` and
simply not used at emission.** It is now an `__attribute__((target(...)))` per
wrapper, the way `immintrin.h` does it, so the diagnostic lands on the CALL —
the only place a user can act on it:

```
error: always_inline function 'haydn_x2abs32' requires target feature 'simd',
but would be inlined into function 'f' that is compiled without support for
'simd'
```

Every Haydn op names exactly ONE feature. **Checked rather than assumed**, via
the emitter's own `-gen-haydn-op-feature-audit`: `simd` 159, `bit-reversed` 9,
`agu` 7, `circular-buffer` 6, no ANDs and no ORs. The emitter now rejects an OR
instead of dropping it — a target attribute cannot express one, and a silently
weaker guard is worse than none.

The count went 159 → 25 → 6 → 0, not straight to zero, because each round found
another emission path: the generic wrapper builder, then the SIMD one, then the
pair/load-writeback ones, and finally six wrappers that are hand-written rather
than built from an `Entry` and name their feature literally.

**And the § 6.16 trap, in the test written to explain the fix.** The file
carried a sentence saying it deliberately does not use a blanket
"no diagnostics" marker — and `-verify` reads directives out of comments, so
writing the marker's name in a sentence ABOUT it made it real. Third instance
in this document; the rule is simply **never spell a directive inside prose**.

---

### 5.21 CB-144 — widen the element out of i1, do not teach anything to hold one

`extractelement <2 x i1> %c, i32 %i` reported *"unable to legalize"*. Haydn has
no vector-of-i1, and the generic lowering could not help: with a variable index
it spills the vector to a stack slot, and `lowerExtractInsertVectorElt` gives
up on an element that is **not byte-sized**. Reachable from ordinary C — a
vector `icmp` feeding a variable-indexed read is all it takes.

**It had been hidden behind `clampMaxNumElements(…, S1, 1)`**, which could only
ever have asserted (§ 5.19). What looked like coverage was a different crash
waiting.

Fixed by moving the type, not by representing it: `widenScalar` on type index 0
anyexts the source vector to match and truncates the result back, so an `s1`
extract from a `<N x s1>` becomes an `s32` extract from a `<N x s32>` — a shape
the existing rules already handle, and byte-sized, so the stack lowering
applies. `G_INSERT_VECTOR_ELT` takes the same treatment, where type index 0 is
the result VECTOR and carries the source and the inserted value with it.
`beeab09f3522`.

**Verified by execution, not by reading the schedule.** The simulator runs both
lanes of both operand orders through the lowered extract and the answers are
exact. Reading `andi32 r1, r1, 1; slli32 r1, r1, 2` and believing it is not the
same thing, and this document has enough entries that begin with someone
believing an assembly listing.

---

### 5.22 The torture run — 1416/1, and the one failure is mine

`gcc-c-torture` had **not** run since the switch. The last one is 2026-08-05:
1417 PASS, 0 FAIL, -O3. Everything since — PEI displacement scaling, the lld
thunk addend, 48 `isPseudo` opcodes, DWARF line addresses, the itinerary
retarget, the store/load overlap rule, three legalizer fixes — was unmeasured
against the largest independent corpus here, the one that found CB-137.

Run: **1514 discovered, 97 unsupported, 1416 PASS, 1 FAIL.** Exactly one
regression, and it is § 5.15's own linker-script assertion.

#### First, a correction to § 5.19

That section presented the `pr28982a` hang as a long-standing bug found by
probing. **It is a regression.** The 2026-08-05 artifact for it is a clean
`GUEST_EXIT` 0 at -O3, 17102 bundles. The obvious defence — "the harness runs
-O3 and I found it at -O2" — does not hold either: reverting the fix shows it
hangs at **both**. It broke somewhere in the format E work; not bisected,
which would need a full LLVM build per step.

#### `align-3.c`, and why relaxing the assertion does not help

```c
void func(void) __attribute__((aligned(256)));
```

The linker pads BETWEEN functions, and that padding is not a whole number of
parcels, so the assertion fires with a message about a case it was not written
for. Relaxing it is the instructive part: the image then links and **BundleSim
rejects it with `DISASSEMBLER_FAILURE` over 13 `<unknown>` bundles**, because
the fill is zeros and an all-zero parcel is not a bundle.

**Two Bundle128 properties made this work before, and format E has neither:**

* a 16-byte parcel divides every power-of-two alignment; **12 divides none**
* **all-zero WAS a valid Bundle128 NOP** — `MC/Haydn/flex-nop.s` pinned exactly
  that, and § 5.9's note that the rule "is one step weaker" in format E is
  where the consequence was already written down without anyone following it
  this far

So fill padding cannot be made decodable at an arbitrary offset — which means
the only place it can be handled is the loader. **BundleSim accepts it now**
(`simulator 417b0c2`), and the way it does is the point.

#### Four refusals, one assumption

```
objdump_runner       strstr(stdout, "<unknown>")
haydn_dump_parser    reject on the first <unknown> line
code_image_builder   records must tile the section exactly
bundlesim.ld         section size must be a whole number of parcels
```

Every one was true under Bundle128 for the same two reasons, and neither
survived: a 16-byte parcel **divides every power-of-two alignment**, so there
was never padding to tile around, and **all-zero was a valid Bundle128 NOP**,
so fill decoded cleanly.

**The replacement looks at the bytes rather than at an arithmetic consequence
of them**: an uncovered gap passes only when every byte of it is ZERO. That is
*stronger* than what it replaced, not a relaxation — a decoder that has lost
the frame leaves REAL INSTRUCTION BYTES uncovered, and those are not zero.
Nothing is given up, because an all-zero parcel could not have been a bundle
anyway. And a branch INTO the gap is still refused by "direct control target is
not an exact code record": **coverage and reachability are different questions,
and only the first one moved.**

Checked in both directions, which is the part worth insisting on: `align-3.c`
runs (`GUEST_EXIT` 0), and corrupting one format-indicator byte in a working
image is refused with *"uncovered bytes that are not alignment fill"*.

`gcc-c-torture -O3`: **1417 PASS / 0 FAIL**, the 2026-08-05 baseline exactly.

---

### 5.23 CB-143's residual was not there; the real one is greedy, not permissive

Going to fix the residual § 5.18 recorded found no such gap, and a larger one
next to it pointing the opposite way. **Both of § 5.18's guesses about which
check was approximate were wrong, and both named code that already worked** —
the port budget (already corrected there) and then unit exclusivity.

**Where unit exclusivity actually lives.** Not in
`HaydnFuncUnitWrapper::conflict`. `buildCandidate` fills `Required` from
`PlacementAlternative::FieldSlots`, and that is an **entry position**, not a
unit — `SlotBits` says WHERE in the bundle and `UnitBits` says WHICH hardware
serves it, separate spaces by construction (`HaydnBaseInfo.h`). The
itinerary-unit path in `buildCandidate` is the **no-alts fallback only**, so the
two meanings share one bitset and only the position one is normally live. The
real check is `CycleState::OccupiedUnits`, one unit per member, reached from
`getHazardType` via `canTryAddProduct` — and it is **exact**, so the "three
instructions each needing two units pass and cannot all issue" case does not
exist. Pinned by execution as `TwoMACUnitsRejectThird`: three `X2MUL32` are
refused, because its five placements offer MAC0/MAC1 and nothing else.

What is genuinely unmodelled is much narrower than the old wording claimed:
`conflict()` **alone** decides lookahead cycles (`DeltaCycles != 0`) and
`checkConflict` for the PostPipeliner, and neither of those sees units.

#### CB-147 — the first instruction placed chooses the format for the whole cycle

The two `ProductFormatRows` have **disjoint** slot sets (`{P20,P21}` vs
`{P30,P31,P32}`), and `HaydnBundlePlan.h` states it already: *"no occupancy is
covered by both rows"*. `tryAdd` walks alternatives by **descending slot bit**
(P32>P31>P30>P21>P20) and commits the first that fits, with no backtracking. So
whichever instruction lands first decides the format, and anything holding a P3x
placement takes the 3-entry form immediately.

**10 logicals have no P3x placement at all** — `ADDI32`, `ADDI32S`, `ANDI32`,
`MOVEI_H`, `MOVEI_L`, `ORI32`, `SET_HWLOOP`, `SUBI32`, `SUBI32S`, `XORI32`,
because a wide immediate only fits a 2-entry entry. That is the
immediate-arithmetic family and both halves of a constant materialisation. They
can share a bundle **only when placed first in their cycle**; otherwise they get
one of their own. `{addi32, add32}` is a legal 2-entry bundle that the packer
emits as two bundles if `add32` is offered first — `CB147_E3FirstLocksOutE2OnlyADDI32`
asserts both orders.

Measured, not reasoned about: **150 instruction sequences pack fewer ops than a
valid assignment admits**, over 3578 members / 684 logicals / 11 distinct
placement signatures.

```sh
python3 llvm/lib/Target/Haydn/utils/haydn_pack_probe.py
#   684 logicals, 3578 members, 11 distinct placement signatures
#   10 logicals have no 3-entry placement at all
#   150 instruction sequences pack fewer ops under the greedy walk
```

It re-reads the generated members rather than restating them, so regenerating
the layout re-measures instead of going stale. It also caught an error in the
first draft of the ledger entry, which had named `D_LDW_CB_IMM` among the
locked-out families — it has a `P30` placement. **The probe was written to check
a claim and immediately earned itself.**

Emitted density for scale, 250 torture files: **1.1126 ops/bundle**, 10784
3-entry vs 3838 2-entry bundles, and **11988 of 14622 bundles hold exactly one
real op**. Most of that is dependency-bound rather than packing-bound — Haydn
has no intra-bundle forwarding, so a reader cannot share a bundle with its
writer — so do not read the whole gap as recoverable.

**Every bundle emitted is legal.** The cost is density and the accuracy of the
scheduler's cost model, which is why this is P3 and not a correctness item.

#### FIXED — `tryAdd` re-solves the cycle, and the trap was real but payable

The fix is the one this section called unsafe, done safely. **`tryAdd` now
re-solves the cycle over its existing members plus the newcomer before
rejecting.** Greedy is kept as the fast path and runs first, so every cycle it
already packed is packed **identically**; the search only runs where there used
to be a rejection, and it walks each member's alternatives in the same
descending-slot order, so the first solution it finds *is* the greedy one
wherever greedy worked. The change can only add acceptances.

Measured before building anything, and again after:

| | before | after |
|---|---:|---:|
| solver rejections | 2507 | **518** |
| of those, greedy-only (an assignment existed) | 1961 | **0** |
| bundles over 250 torture files | 14622 | **14355** |
| ops/bundle | 1.1126 | **1.1333** |
| real ops emitted | 16269 | 16269 |

Same work, 267 fewer parcels — **−1.83%**. What is left rejecting is genuinely
infeasible.

**The trap was real, and the way past it was to pay it rather than avoid it.**
`tryAdd` accepting instruction N is not the end of the transaction:
`commitPlacementForEmit` stamps
`AltDescs->setAlternateDescriptor(MI, Member.MemberOpcode, *TII)` for **each
instruction as it is accepted**, so a re-solve that relocates an earlier member
leaves that member's stamp naming the slot it vacated, and
`materializeMultiOpcodeInstrs` would `setDesc` it there — a genuinely wrong
bundle, strictly worse than the density it buys. So the recognizer keeps
`CurrentCycleMIs` parallel to the solver's member list and **re-stamps every
mover**. That path is hot, not a corner case: **683 relocations** over the same
corpus, which is also why a mistake there would have been loud rather than
subtle.

Holding those `MachineInstr *` is safe for exactly the reason
`HaydnAlternateDescriptors` already holds the same pointers for the whole
region — nothing replaces a `MachineInstr` between the stamp and
`materializeMultiOpcodeInstrs` — and they are dropped with the cycle.
**Facts-not-pointers still stands for anything that outlives a cycle** (see
`CurrentCycleMemOps`, which stores facts precisely because it does).

`CycleState` gained `SeedFormatMask` because a re-solve re-decides every
placement and must seed from the frontier the cycle *started* with;
`FeasibleFormatMask` has already been narrowed by the assignment being thrown
away.

**Verified by execution before any expectation was regenerated** — which is the
whole point of § 5.4's warning. BundleSim `ctest` **226/226** and
`gcc-c-torture -O3` **1417 PASS / 0 FAIL**, both after the § 2 libc + BSP
rebuild. `NumScheduledCyclesSplit` is **0**, so the recognizer and the finalizer
agree on every cycle it packed. Only then were the 54 autogenerated lit tests
regenerated.

The scheduler-preference alternative this section proposed
(`HaydnPostRASchedStrategy` picking the most format-constrained ready
instruction first) was **not needed** and is not implemented: the solver change
subsumes it and costs nothing at schedule level.

#### Three hand-written tests needed judgement, and one found a new trap

The 54 autogenerated tests are bookkeeping. The three hand-written ones were
not, and one of them is a trap worth its own entry (§ 6.17):

* **`pei-scratch-caller-saved-only.ll`** asserted that no callee-saved GPR is
  used as a stack base, and fired on
  `{ addi32 r8, r0, 0; s_sw_with_imm r1, sp, 0 }`. **Not a regression** — that
  is `r8 = r0 + 0`, and the `sp` the pattern matched belongs to the *other
  instruction in the bundle*. `{{.*}}` spanned the `;`. The property it cares
  about still holds (the base is `r1`). Patterns there now use `[^;]` so they
  cannot leave the instruction they are about.
* **The two GISel i1-vector tests** pinned both lane compares to one bundle with
  a same-line directive. Which bundle each lands in is a scheduling detail and
  not what those tests are about; they now assert the two compares separately.
* **`bqriir32x32_df1-e2e.ll`**'s hwloop distance moved 708 → 696, exactly one
  parcel. That test already anticipated this and states the invariant it wants —
  real distances, neither zero — which still holds.

---

### 5.24 CB-148 — the pipeliner asserted because a hook was never implemented

Found by running `scripts/run_full_gate.sh` (ctest + CoreMark + Dhrystone) for
the first time. **clang aborted compiling CoreMark**, and nothing in the
recorded baseline covered it: lit, ctest and gcc-c-torture all pass with the bug
present.

```
MachinePipeliner.cpp:1117, hasLoopCarriedMemDep:
  Assertion `TII->areMemAccessesTriviallyDisjoint(SrcMI, DstMI) &&
             "What happened to the chain edge?"' failed.
```

**It was not a scale bug**, which is where § 5.18 would have sent you.
`TargetInstrInfo::areMemAccessesTriviallyDisjoint` is a virtual whose **default
returns false for every pair**, and Haydn never overrode it while enabling
`MachinePipeliner` at O2+. The pipeliner's cheap path reasons "same base, lower
offset first, therefore disjoint" and then asserts the target agrees — so an
unimplemented hook is not a loss of precision here, it is an abort. Every target
that enables the swing pipeliner implements it (AArch64, Hexagon, RISCV, AMDGPU,
Lanai).

Implemented in the standard shape: same single base operand, non-scalable
offsets, and `LowOffset + LowWidth <= HighOffset`. Haydn's
`getMemOperandsWithOffsetWidth` already returns **byte** offsets (§ 5.18 fixed
that), so the comparison is like with like.

**It is a real precision win, not just an assert silencer.**
`MachineInstr::mayAlias` is built on this hook, so every consumer of it — memory
clustering, scheduling, dead-store elimination — was being told "may alias" for
every pair. Measured over 250 torture files: bundles 14355 → 14341 and the real
op count 16269 → **16268**, one instruction fewer, which is the more precise
alias answer letting a store go.

#### The first regression test for it was vacuous

A hand-written `i32` loop over one base at two constant offsets — the shape the
assertion describes — **compiled fine without the fix**. Checking that a new
test fails without its fix is the rule, and it caught this one.

What actually reaches the path is narrower: `matrix_add_const` is a **halfword**
read-modify-write in a nested loop, and the vectorizer turns the inner loop into
`<4 x i16>`, which legalizes into several halfword accesses off one base at
different constant offsets. Both the element type and the vectorization are
load-bearing. The test is that IR, reduced, and it aborts the compiler without
the override.

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
rm -rf build/bsp-obj build/bsp-stage && cmake --build build --target haydn_bsp
```

**That command is corrected — the original said `bsp-stage` alone, which
rebuilds nothing (§ 6.6.1).** It appeared to work during the JAL fold because
`crt0.s` *is* rebuilt by its own rule, and crt0 was where the JAL spelling
showed.

This cost most of an hour during the JAL fold and mimicked a real regression
convincingly enough to be worth checking *first* whenever the simulator suite
goes from green to broadly red.

### 6.6.1 `rm -rf build/bsp-stage` rebuilds nothing

§ 2 says to remove `build/bsp-stage` before rebuilding the BSP. That is the
STAGE. The objects live in **`build/bsp-obj`**, which survives it, so the
stage is re-archived from whatever was compiled last time and only `crt0.s`
— which the rule happens to rebuild — is current.

The symptom is not a stale-object message. It is:

```
ld.lld: error: ...libbundlesim_plat.a(exit_llvm_libc.o):
  relocation R_HAYDN_WIDE_CallSImm20: no format E geometry for this placement
```

a complaint about the relocation, pointing at nothing wrong with the
relocation: those objects were Bundle128, ten days old, and the geometry lookup
was being asked about a bundle that was not one. **Check the object's section
alignment before believing a relocation error** — `Al 16` on a `.text.*` is a
Bundle128 build.

`rm -rf build/bsp-obj build/bsp-stage` recompiles all 72. Its subdirectories
(`compiler_rt libc plat softfloat sys`) then have to be recreated by hand:
neither the compile rule nor `llvm-ar` creates its output directory, and the
failure for that is `unable to open output file`, several minutes later.

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

### 6.15 `haydn_vacuous_not.py` was reading ZERO files

Its `ROOT` went up four levels from `llvm/lib/Target/Haydn/utils`, which lands
on `<repo>/llvm`. `os.walk` of a path that does not exist yields nothing, so
every run printed **"0 vacuous CHECK-NOT" without opening a file**, and had
done since the script was written. Every green reading of it — in this
document, in the session handoffs, in the "gates" block of § 1 — meant nothing.

**A gate whose entire purpose is to catch assertions that pass by not checking
anything was passing by not checking anything.** It is the § 6.6 / § 6.12 /
§ 6.13 / § 6.14 family again, and the sharpest instance of it: a tool answered
a question nobody had asked it, and the answer was the reassuring one.

The tell was available and unread: the number never moved. Not when 62 tests
were regenerated, not when the load/store family was renamed, not when nine
`.s` files had their assertions rewritten. **A gate that never moves is either
perfect or blind, and the second is far more likely.**

#### What it checks now

Two halves. The whitelist of RETIRED spellings survives — it needs no build and
it is what catches a `_S1` member coming back. Added to it: every mnemonic- or
opcode-shaped token in a negative assertion is looked up in what the target can
actually emit (the generated opcode enum, the asm mnemonic table, the intrinsic
table), by substring, because that is how FileCheck matches — `CHECK-NOT:
mul32` is live, since `x2mul32` contains it.

That found what the whitelist structurally could not: `HWLOOP_END`, a spelling
nobody had thought to list. See § 5.4's entry on the 43 held tests.

Scope, decided by measuring rather than guessing:

* `//` directives are seen now. **21 of them, in 6 llvm files, were invisible
  to the old regex**, which only accepted `;` and `#`.
* `clang/test/{Headers,CodeGen/Haydn}` are in scope. `clang/test/Sema` is not:
  thousands of unrelated tests, and one row out of `attr-availability-swift.c`
  is one row too many for a gate meant to be read every time.
* `llvm.haydn.*` names that never existed, assembler directives and pass titles
  are shaped out, with the reasons in the docstring. `x2cmula-isqrt-probe.c`
  forbids `llvm.haydn.x2cmula32` to say the composed op must not survive as one
  intrinsic — but it never was one, and the positives beside it already assert
  the decomposition. A spelling that never existed is a different category from
  one that was retired.

Verified by putting a defect back rather than by the count being zero: restore
`CHECK-NOT: HWLOOP_END` and the liveness half reports it; plant
`CHECK-NOT: ADD32_S1` and the whitelist half does. **The count is 0 because the
tree is clean, not because the gate cannot look** — which is a sentence that
now needs saying every time it is quoted.

### 6.15.1 The clang tests were out of scope (subsumed above)

`TESTS = ("llvm/test/CodeGen/Haydn", "llvm/test/MC/Haydn")`. The gate reading
zero means zero **there**, and that is exactly where it was reading zero while
`haydn-compat-exact-value-ref.c` and `haydn-compat-mulzaafd16ss-33-22.c` each
carried `ASM-NOT: fmulaa16.hs.11.00` — dotted, against an assembler that
prints `fmulaa16_hs_11_00`. Six such lines across the two files, all of them
unable to match anything, all of them counted by nobody.

They are corrected to underscores, and the correction was checked the only way
that means anything: put a string the window *does* contain into the NOT and
confirm it fires. The first attempt at that control used a spelling outside the
directive's window and passed, which looks identical to a dead assertion —
**when a `CHECK-NOT` control passes, check the window before concluding the
assertion is dead.**

Whether to widen the script's scope is a real decision, not an oversight to
fix in passing: the clang tests use different prefixes and a different notion
of a retired spelling, and widening it will surface rows that then have to be
triaged rather than left. It is the same shape as § 6.6 / § 6.12 / § 6.13 /
§ 6.14 — a tool answering a slightly narrower question than the one being
asked — and it is the fifth member of that family.

### 6.16 Four ways a FileCheck line asserts nothing

All four were found in one pass, and none of them fails in a way that says so.

* **Nested `{{...}}`.** FileCheck closes the regex at the FIRST `}}`, so
  `{{S_LW_{{[A-Z_]*}}|D_LDW_POST_IMM|...}}` is one broken regex followed by
  literal text. It cannot match anything. It was red, which is the lucky case
  — the same construction with a matchable prefix would be green and empty.
* **An alternation over things that are not alternatives.**
  `{{beqz|set_hwloop}}` passes on either arm, so it asserts neither. § 5.14 is
  what that cost: the `set_hwloop` arm was never verified, and when the
  `beqz` arm went stale the line failed for the wrong reason.
  `{{beqz|bnez}}` is fine — two spellings of one fact. Two different facts in
  one alternation is a hiding place.
* **A directive prefix inside a comment.** Already in this file's own history
  (§ 5.4), and hit again while WRITING the fix for the first item: quoting the
  broken line verbatim in a comment made FileCheck adopt it. Prose about a
  directive has to break the prefix.
* **`--implicit-check-not=<unknown>` unquoted.** `<` is a stdin redirect, so
  the RUN line silently becomes "read from a file named `unknown`". lit reports
  **UNRESOLVED**, which does not appear in the failure count and reads like a
  harness hiccup. Quote it.

The standing gate `utils/haydn_vacuous_not.py` catches only the fourth kind: a
negative assertion naming something nothing can emit. Nothing catches these
four. And note what § 6.15 turned out to be — the gate was reading zero files,
so for most of this migration nothing caught the fourth kind either.

### 6.17 A line is a BUNDLE, so `{{.*}}` matches across instructions

On most targets one assembly line is one instruction, and a pattern like

```
addi32{{.*}}r8,{{.*}}sp
```

reads as "an `addi32` whose destination is `r8` and whose base is `sp`". **Here
it does not.** A line is a bundle of up to three independent instructions
separated by `;`, and `.` matches the separator, so that pattern also matches

```
{ addi32 r8, r0, 0; s_sw_with_imm r1, sp, 0 }
```

where the `r8` comes from one instruction and the `sp` from another that has
nothing to do with it. `pei-scratch-caller-saved-only.ll` asserted "no
callee-saved GPR is used as a stack base" this way and reported a violation on
CB-147's density improvement — a **false alarm**: the base was still `r1`, and
two unrelated instructions had merely become co-issued.

**Use `{{[^;]*}}` whenever the parts of a pattern must belong to the same
instruction.** The negative forms are the dangerous ones: a positive assertion
that spans members usually just stops matching and you investigate, while a
negative one starts matching and reports a bug that is not there. Either way the
answer is the same — a pattern about one instruction must not be able to leave
it.

This is the sibling of § 6.16: there, prose became a directive; here, one
instruction's operands became another's. Both come from reading a bundle as if
it were a line.

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

  **It lost, then earned it — see § 5.12.** The first real bundles showed the
  axis never fired on the AsmParser's hinted path: that path had no
  `MCInstrInfo` at all, and even with one it read the unit off a member-name
  suffix that a LOGICAL does not have. `{ beq; bnez; beqz }` assembled — three
  control transfers on ALU0. The unit test passing and the assembler accepting
  that bundle were both true at once, which is exactly what the sentence above
  was warning about. Both halves are fixed and the axis is enforced now; the
  CodeGen path never had the defect, because it builds its bundle `WithMII`
  and goes through the solver rather than the hint.

  **NOP occupies no unit** (§ 5.12). The hardware answer arrived and is now
  encoded in `opcodeClaimsUnit`. If a re-delivered model changes it, that
  function is the one place to change — but note the consequence it currently
  buys: NOP padding is unconstrained, so no padded bundle's encoding depends
  on the unit axis at all.
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
* **The SFR compares are void; the source break is accepted.** `X2SEQ32`,
  `X2SLT32`, `X2SLE32`, `X4SEQ16`, `X4SLT16`, `X4SLE16`, `SEQ64`, `SLT64`,
  `SLE64` write SFR and no register, so their intrinsics and builtins return
  nothing and the three scalars take the second source they always had. Read
  the result with `haydn_movesfr2gpr()`. The old return value was never a value
  at all: GISel allocated a dead vreg for it, and every caller in `haydn_dsp.h`
  already discarded it and read the flags back. Same call as the AR prototypes
  below, and for a stronger reason — those changed shape, this one never had
  the shape it advertised. Done in `d3b8af57efac`.
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

1. **~~`haydn_dsp.h` has not been updated~~ — DONE in `1d26e4382186` and
   `badd67cd03a9`, and it was not one file.**
   The five AR helpers now take `(ar, p)` / `(data, ar, p)` / `sa64pos(ar, p)`,
   the 15 live call sites lost their `8, 0`, and `haydn_ae_sa64pos`'s 8-way
   `switch ((ar << 1) | dir)` is a 4-way `switch (ar & 3)`. The four dead
   `*_RIP` bodies that still passed `__s, 1` are withdrawn at their definition
   site rather than left: they were unreachable (each is `#undef`'d and
   withdrawn again further down), so nothing failed, and a 4-argument call to a
   2-argument helper sitting in the tree is the kind of thing that only looks
   fine until someone reorders the file.

   **The header was the visible third of it.** Retargeting it exposed two more
   copies of the same arity, neither of which the header's own compile would
   have caught:

   * **`IntrinsicsHaydn.td` still declared the old shapes** — `d_lqhwua_post`
     and `d_ltwua_post` as `(ptr, i32, i32, i32)`, the stores as five operands,
     `wbarwua` as three. The clang builtin was already two, so `haydn.h`'s own
     wrapper body **crashed clang** on `CallInst::init`'s "Calling a function
     with bad signature!" — not a diagnostic, an assert. The GISel selector had
     already been moved to the new arity (`op(2)=ptr, op(3)=ar_sel`), so the
     `.td` was the only layer left behind.
   * **`specialPublicShape()` in `HaydnIntrinEmitter.cpp` had its own table**,
     still handing out 4- and 5-argument shapes. It wins *before*
     `PublicPrototype` is parsed, so `checkPublicProtoArity` — added in
     `ecbb96c630d0` for exactly this failure mode — never saw it. The only
     thing that noticed was `capi-op-closure-matrix.c` failing to compile the
     probe it generates. That hole is now closed: `resolvePublicShape` applies
     the same rule to the special table, and reintroducing a wrong arity there
     is a tblgen fatal error (verified by doing it).

   Note what the sequence says about measurement. The header stopped compiling,
   which is loud; the intrinsic mismatch was a compiler crash, which is louder;
   and the emitter's copy was silent everywhere except one test in a directory
   nobody reads first. **Arity lives in four places** — `BuiltinsHaydn.td`,
   `IntrinsicsHaydn.td`, `specialPublicShape()`, and `haydn_dsp.h` — and only
   the first two now check each other by construction.

   `haydn_ae_audit.py` needed retargeting too and became a better check for it.
   It used to read `parts[-2]`/`parts[-1]` of each helper call as stride/dir,
   which against the new arity reported 13 false positives with bodies like
   `dir=(ptr`. It now asks the question the change actually leaves: **does any
   macro hand an AR helper more arguments than the helper takes**, with the
   expected arity read back out of `haydn_dsp.h`'s own `static inline`
   definitions rather than restated. Back to exactly the 11 withdrawn.

   Tests retargeted with it: `ar-unaligned-intrinsics.ll`,
   `capi-constarg-imm-probe.ll`, `ar-unaligned-roundtrip.s` (regenerated whole
   — its assertions still spelled 16-byte parcels, Bundle128 slot order and a
   third `wbarwua` operand), `haydn-ar-unaligned-intrinsics.c`,
   `capi-pointer-api-probe.c`, `haydn-compat-la-ric.c`,
   `haydn-compat-exact-value-ref.c`, `haydn-immarg-const.c`,
   `haydn-builtins.c`. Where a Sema case tested that `dir_sel` had to be a
   constant or in range, it was moved onto `ar_sel` rather than deleted with
   the operand — the edge is the same one, and deleting it would have quietly
   dropped coverage.

   clang lit went 15 failures → 0 in `clang/test/Headers`, and
   `llvm/test/{CodeGen,MC}/Haydn` went 535 → 537 of 589.
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
3. **~~What produces the `mapping` arrays in `format_e_bit_layout_v2.json`?~~ —
   ANSWERED by the ISA owner, 2026-08-12. Nothing we do not already hold.**
   `format_e_bit_layout_v2.json` is generated from `instruction_type_index.json`
   and `instruction_type_operands.json` **by prompt**;
   `format_e_bit_layout_v2.xlsx` is generated from that JSON; and
   `instruction_to_entry.xlsx` is `generate_instruction_to_entry.py` over the
   index. **Both spreadsheets are renderings and neither is a master.**

   The operational consequence, in one line: **the `mapping` arrays are a
   derived cache, so re-derive them on every delivery rather than trusting
   them** — `--fix-operand-mapping` recomputes them from the same `Syntax` the
   generator was given, and `verify_operand_sets` refuses to emit on mismatch.
   There is no upstream table to ask anyone to repair. Full reasoning, and the
   measurement showing why no self-contained check can replace that comparison,
   in § 5.3.

   **With this answered, § 8 has nothing left waiting on a human.**
4. **~~CB-130~~ — DONE, and it was never a question for a human.**
   `bundlesim_reg_cb44_o2_stale_cond_max_reduce` was the sole remaining
   simulator ctest failure, a GISel legalizer assert on
   `<2 x s1> = G_BUILD_VECTOR`. Closed by deleting a `clampMaxNumElements(0,
   S1, 1)` that could only ever assert — § 5.17. **Standing rule, because that
   construct was in four rules and not one:** the other three
   (`G_EXTRACT_VECTOR_ELT`, `G_INSERT_VECTOR_ELT`, `G_CONCAT_VECTORS`) went in
   § 5.20, and `clampMaxNumElements(…, S1, 1)` is not to be reintroduced
   anywhere — `LLT::scalarOrVector(1, T)` returns a **scalar**, and
   `fewerElementsVectorMerge` asserts that NarrowTy is a vector. One element is
   not a smaller vector. Both of the last two entries it hid (CB-130, CB-144)
   were real bugs it disguised as coverage.
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
| `llvm/lib/Target/Haydn/utils/haydn_ae_audit.py` | Which `AE_*` macros still need the AR args format E drops (§ 8 Q2). Since § 8 Q1 the test is arity, read from `haydn_dsp.h`'s own helper definitions |
| `llvm/lib/Target/Haydn/utils/haydn_vacuous_not.py` | Standing gate: negative assertions that can never match, by retired-spelling whitelist AND liveness against the generated tables. llvm + the two clang Haydn dirs — see § 6.15 |
| `llvm/lib/Target/Haydn/utils/haydn_pack_probe.py` | Whether the packer's greedy placement packs fewer ops than a valid assignment admits — the measurement behind CB-147 (§ 5.23). Reads the generated members, so a regenerated layout re-measures |
| `llvm/lib/Target/Haydn/MCTargetDesc/HaydnMCFormats.{h,cpp}` | `stripHaydnMemberSuffix`, `getHaydnLogicalBaseOpcode`, slot geometry |
| `llvm/lib/Target/Haydn/MCTargetDesc/HaydnMCCodeEmitter.cpp` | Fixup kinds, composite encode |
| `llvm/lib/Target/Haydn/MCTargetDesc/HaydnRelocGeometry.inc` | Generated `--emit reloc-geometry` (§ 5.8); checked in, shared with lld |
| `llvm/lib/Target/Haydn/MCTargetDesc/HaydnThunkEncoding.inc` | Generated `--emit thunk-encoding` (§ 5.2); the veneer's three bundle words |
| `lld/ELF/Arch/HaydnThunks.cpp` | Long-branch veneer. lld sees these two `.inc`s via the include path `lld/ELF/CMakeLists.txt` adds for `HaydnRelocLayout.h` |
| `llvm/lib/Target/Haydn/Disassembler/HaydnDisassembler.cpp` | Parcel decode, `SlotGeo` |
| `llvm/unittests/Target/Haydn/HaydnMCFormatsTest.cpp` | Member-suffix tests, both spellings |
| `clang/include/clang/Basic/BuiltinsHaydn.td` | AR prototypes (changed on WIP). One of the four places arity lives — § 8 Q1 |
| `llvm/include/llvm/IR/IntrinsicsHaydn.td` | The intrinsic signatures the builtins lower to. Must match `BuiltinsHaydn.td` or clang asserts rather than diagnoses — § 8 Q1 |
| `clang/utils/TableGen/HaydnIntrinEmitter.cpp` | Generates `haydn.h` and the op-closure probe. `specialPublicShape()` is a hand-written arity table, now cross-checked — § 8 Q1 |
| `clang/lib/Headers/haydn_dsp.h` | `AE_*` surface, and the five AR helpers every `AE_LA/SA_*` routes through (§ 8 Q1) |
| `simulator/bundlesim/isa/database/generate_catalog.py` | Cannot run; port to the new database |
| `simulator/bundlesim/isa/database/generated/GOLDEN_INPUTS.sha256` | Database pin |

## 10. The reunification onto the mhyang base (2026-08-14)

This document's history through § 9 describes the `haydn` line. On
2026-08-14 that line was merged ONTO mhyang's Format E96 line (worktree
`llvm-project-mhyang`, tip `1c740f0d5708`, 15 commits since the common
base `33e468768f4f` of 2026-07-27 — six of them adapted picks of our own
fixes). The user's instruction fixed the direction: HIS tree is the
base; our unique work is ported on top. Branch: `haydn-on-mhyang`; its
closing merge commit carried `haydn`@6e3b2cc5a46c as second parent, and
since the 2026-08-17 linearization (§ 1) it is that branch name, not this
history, that keeps every commit this document cites reachable.

### What the two lines were

Same product, two implementations. His: E96 hard cutover as one squash,
generated records checked in (FormatE/generate_format_e_records.py →
HaydnGen*.inc + LIVE members td.inc), residual S0/S1/S2 solver evolved
into a pure nondominated CycleCandidateSet (no eager stamping — his
architecture solves CB-147's dead-end AND CB-147's danger at once),
productized multi-stage SMS, typed HWLoopOff relocs, serialize-only
fail-closed MC. Ours: the § 1–§ 9 journey — database-driven generation
via haydn_encoding.py, the unit model (CB-143), the repaired golden,
CB-144..148, and the validation culture (execution gates, fail-first
tests, vacuous-NOT).

### Policy applied to the overlap

Where both implemented the same thing, his implementation stays (it is
the packing superset). Our line's contributions were re-expressed in his
architecture rather than transplanted:

* The repaired golden (8465132c…) replaces the delivery pin his records
  were generated from. His generator's dual-dest recovery had absorbed
  all of the 76-row mapping defect except X4SEL16_E3_E1_ALU1_RRR (src1
  hardwired 0, wrong src2 class). One member changed. The same row is
  the only member whose SAME-CLASS operand order permutes against its
  logical's majority signature, which his bag-by-class
  fillFormatEMemberInst would encode swapped — invisible to every
  round-trip, visible only to the golden/simulator. The generator now
  emits such members' (ins)/asm in canonical alias order, pinned
  fail-closed.
* The unit model became golden-placement SDR refinement: his solver
  worked in residual slots, which are labels, not entries — a slot-legal
  cycle could have no (entry, unit) assignment and serialization
  fail-closed ("one-parcel placement failed": two LOADSTORE0-only
  stores, a third MAC, SET_HWLOOP against any plain-ALU partner — the
  bf16mul crash). haydnFormatEPlacementFeasible mirrors the emit DFS
  law inside every solver expand, and commitProduct selects the row
  honoring the refined mask. E2's geometry follows: e0 hosts the plain
  ops, e1 the RI20/I32, LOAD1 and MAC1 families, so plain pairs are
  E3-only.
* CB-148's hook landed with the § 5.18-class fix his accessor still
  needed (plain LD/ST offsets returned element indices; cross-width
  interval math was unsound both ways). The CoreMark abort shape is not
  currently reachable on this base (his vector path keeps <4 x i16> in
  DRs), and the test stays as the guard.
* Ported because absent and verified live: CB-144 (exact original
  error), the G_FREEZE producer clamp (pr28982a/b hang reproduced), the
  splice RAW/directional fix, DWARF MinInstAlignment 12→1 (drift on any
  non-parcel advance; functions align to 4), tryCSEConstantDR64 operand
  guards.
* Verified already present and NOT ported: CB-134's two fixes (all five
  hang files compile at O2/O3 — his ledger was stale), G_ABS, the
  accumulator read-port charge (operand-derived, immune to the
  index-json defect), the isPseudo-48 class (his pack law is meta+alts),
  the lld thunk entry base, element-index setjmp/longjmp.
* His tip was mid-stream: 15 unit tests red against his own 08-10
  entry-capacity/Option-A semantics, and the two hwloop reloc lld tests
  still pinning R_HAYDN_32 after his own FieldLsb fix restored the typed
  relocs. All aligned to the measured law in this merge.

### Verification state at merge (final, 2026-08-14)

All after the § 2 libc+BSP rebuild with the merged compiler
(llvm-libc-merge-build; sysroot installed beside the merged clang;
simulator/build-merge configured on the merged toolchain):

```
llvm+clang+lld Haydn lit   770 discovered / 3 failed — all three CB-150
                           (this base's AE tier machinery, mid-stream at
                           its own tip; ledgered, not guessed at)
HaydnTests                 437/437   (tip: 417/432)
BundleSim ctest            222/223   (the one red is cb100_ar_unaligned,
                           the deliberate CB-151 tracking signal)
gcc-c-torture -O3          1417 PASS / 0 FAIL (97 lit-unsupported)
CoreMark e2e               PASS
encoding --check           3686 placements / 126 shapes, self-consistent
encoding roundtrip         3686/3686, operands included
Dhrystone                  not run — its qualification source path
                           (/home/ckchen/benchmark/…) does not exist on
                           this machine; it was never part of the
                           recorded baseline either
```

The second validation pass also fixed on this base: ARCTAN (and every
golden-placeable pseudo) now emits at -O0 as a product singleton; the
Bundle<MCInst> fixed-slot paths keep member history so
syncSlotMapFromPreferred cannot underflow; aligned(N) on functions is
honored end-to-end (AsmPrinter consults F.getAlign, the lld
sh_addralign clamp is gone — its premise died when the simulator
started accepting zero-fill gaps); libc's fscanf entrypoint is back
(the BundleSim plat override it pointed to was retired on the
simulator side). Simulator-side (local commit 841a570): the ELF
validator accepts EF_HAYDN_E96, and cb100's hand asm moved to this
toolchain's AGU surface.

### Known-open on this base

* haydn_vacuous_not.py reports 161 vacuous CHECK-NOTs across his test
  corpus (our line drove this gate to 0). Tool ported; corpus cleanup is
  follow-up work, not merge scope.
* haydn_pack_probe.py was NOT ported — it parses our retired
  HaydnFormatEEncoding.td; his exact candidate-set solver covers the
  greedy-lockout class it measured. Re-deriving the density probe over
  his FormatEMembers tables is follow-up work.
* AE_* policy: our line withdrew macros the AR hardware cannot serve;
  his line tags them with fail-closed tiers. Both are fail-closed; the
  surface difference is a product decision to settle with the ISA/API
  owner, not a merge decision.
* (Settled post-merge, same day: CB-149 — the user decided, and AR2/AR3
  are restored with the full 2-bit ar_sel domain, 636042bb7cd5; gates
  resealed at torture 1417/0, CoreMark PASS, ctest 222/223. And
  fork/mhyang moved during the merge: 1c740f0d5708 → 11b1d70b4111,
  five commits, 654 files — FieldSlot retirement, a generated shared
  resource model with hosted multi-stage SMS, hwloop Role-A, runtime
  fail-closed ABI/residual pseudos, AE oracles. A second reconciliation
  onto that tip is merge-sized work awaiting a direction decision; his
  new tip still has only AR0/AR1, so CB-149 rides along.)
* His plan documents live at /ssd2/mhyang/haydn-plans (his machine) and
  are cited by header comments as "plan §…"; they are not in this repo.

## 11. Onto the second wave (2026-08-15)

mhyang's second wave (`1c740f0d5708` → `11b1d70b4111`, five commits, 654
files) landed while `haydn-on-mhyang` was being validated, and the
direction was confirmed with him: his line absorbs ours. Branch
`haydn-on-mhyang-2` re-expresses the merged state on the wave tip; its
closing merge carried `haydn-on-mhyang` as second parent, and since the
2026-08-17 linearization (§ 1) both prior lines stay reachable through
that branch name instead of through this history.

### What the wave absorbed on its own (verified, adopted, not re-applied)

The wave independently converged on most of `haydn-on-mhyang`: the
unit-cover law (`opcodesHaveFormatEUnitCover` — its own comment names
the two-LOADSTORE0-stores case; the bf16mul crash class is structurally
gone), byte-scaled memory offsets with FI awareness plus the
disjointness hook, the DWARF line unit as the golden 2-byte min
bundle-address alignment (more principled than that line's 1 — adopted),
the splice RAW direction ("a COPY that only reads a member-def'd reg
used to pass the def-only check"), the Bundle member-history assert, and
ARCTAN-class -O0 emission. Convergence this systematic is the strongest
evidence both lines are reading the same ISA truth.

### What was still missing (re-applied, each verified live first)

The repaired golden (his pin was still the delivery hash; the delta is
again exactly X4SEL16_E3_E1_ALU1_RRR, and his new typed-MemberId
serialization still binds bag-by-class — the probe reproduced the
rsd1/rsd2 swap byte-for-byte until the canonical-order emission was
re-applied, landing on the same …52 d9… bytes haydn-on-mhyang derived
independently). CB-144 and the G_FREEZE hang. aligned(N) end-to-end.
tryCSEConstantDR64 guards. The fscanf entrypoint. CB-149 (the wave
still modeled AR0/AR1; restoring AR2/AR3 exposed a hand-written
`RegNo >= 39` bounds literal in the InstPrinter that silently printed
`<?>` for every register whose enum value moved — now
`Haydn::NUM_TARGET_REGS`; his own Sema tests expected [0, 3] and went
green). Golden discovery falls back to ~/haydn in both importers and
the CMake check wiring; the canonical-vector ledger exists only on the
plans machine and embeds the golden hash in its oracle block, so the
--check gates are feature-gated (`haydn-golden-canonical`) and the
ledger must be regenerated there against the repaired golden.

### Baseline honesty

The wave tip itself was red: 63 lit failures at baseline. After the
ports: zero NEW failures against that baseline, four baseline reds
fixed outright, 46 autogenerated expectations regenerated under the
op-multiset-preservation check (all 46 preserved — pure repack from his
scheduler evolution), the two hwloop reloc lld pins moved to his
GE96-03 byte window (ValueShift=0 halves the old ÷2 branch reach — the
+4080 thunk the old pin rejected is correct now), reloc-ls-imm re-pinned
to logical-mnemonic printing. The residue is ledgered: CB-152 (eight
finalize-mid-stream hand tests, including a BUNDLE-row-vs-
ProductDefaultRowID contradiction and the tied-MAC member Desc verifier
trip — owner's calls, deliberately not re-pinned to current behavior),
CB-150 (one AE compat test), CB-151 unchanged.

### Wave-tip gates (addendum)

Execution gates against the wave-tip toolchain, after two convention fixes
on the simulator side (dump parser accepts objdump's new `<sym>` branch
annotations; the legacy ld/st mnemonic aliases stop dividing an
immediate that is already an element index) and one on the toolchain
side (LLD's fixed 4-byte trap filler back to ISA-inert zero — the
idle-parcel seed planted 0b111 indicators in sub-parcel residues that
cannot hold a bundle, and aligned(N) functions then produced dumps with
overlapping PCs; gcc-torture align-3 was red end-to-end and is green
again; `trap-fill-not-zero.s` is re-pinned as `trap-fill-zero-inert.s`
with both directions of the history in its header):

* BundleSim ctest: 219/223 — red are cb100 (CB-151 tracking, by
  design) and three yarpgen seeds that now exceed their 300 s budget
  (CB-153b; seed8 solo-passes given time).
* gcc-torture -O3: 1416 pass / 0 fail / 1 timeout (the timeout is the
  CB-153 compile/runtime class; haydn-on-mhyang scored 1417/0).
* CoreMark: PASS, 563,224 committed bundles vs haydn-on-mhyang's 517,586
  (+8.8%, CB-153b).
* Haydn lit battery: 880 discovered, 9 red — the eight CB-152
  finalize-cohort tests plus CB-150. HaydnTests: recorded as 437/437 at
  the time, which was a MISCOUNT — the grep pattern matched gtest's
  plural "FAILED TESTS" but its singular "1 FAILED TEST" summary, so
  GoldenHashPins (the wave unittest still pinning the DELIVERY golden
  hash b0b477e5... that this line had repointed to the repaired
  8465132c... everywhere else) sat red unnoticed. Pin re-pointed with
  the repair rationale; suite now 459/459 (count from the gtest tail,
  not a grep). Read the tail, don't pattern-match victory.

### Wave-tip follow-up sweep (2026-08-15, same session)

CB-153a fixed (post-RA auction ranking memoized three ways, decision-
identical, 30.5s -> 1.38s llc on strlen-5; corpus byte-identical, twin
pinned by unit test). CB-152c fixed (members mirror the logical's tied
accumulator; tie set = golden Write∩Read ports ∩ TD-modeled ties; the
87 golden-accumulating-but-TD-untied logicals are CB-154). CB-152b
settled for the documented E2 singleton default (doc + owner MIR test
outvote two mechanism-derived unit pins); CB-152a done — five cohort
tests self-healed, three re-pinned to committed member names. CB-153b
diagnosed: the seed timeouts were 153a's compile time in disguise; the
real +8.8% is co-issue density halved in the wave's post-RA (same op
counts, half the multi-issue bundles; auction not the lever) — handed
to the owner with per-object numbers and a minimal example.

Battery: 880 discovered / ONE red (CB-150, plans machine). HaydnTests
459/459. torture -O3 1417/0/0 — full house, back to the haydn-on-mhyang
mark. ctest 222/223 (cb100 = CB-151 by design; the bsp_smoke 2000-bundle
leash moved to 2500 after CB-152c's hazard-correct stalls +72 bundles).
CoreMark PASS, 563,224 bundles (unchanged by this sweep).
