# sms-multistage/ — Kernel-scale SMS corpus + II/NS update policy (G006)

Ultragoal story G006 (2026-08-23). This directory is the kernel-scale
corpus for the post-RA multi-stage SMS engine (`HaydnMultiStageSMS`),
adapted from the AIE `aie2/schedule/postpipeliner/` corpus + README
policy (peer tree `/ssd2/mhyang/llvm-aie`), re-expressed for the Haydn
ISA. The fixtures lock **QoR (II/NS), not schedules**.

## The canonical remark (G005 KPI layer)

Every fixture has a remark arm:

```
llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 \
    -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
    -pass-remarks-analysis=haydn-multistage-sms
```

and pins the ONE canonical per-loop line emitted at finalize
(`emitHaydnSMSLoopRemarks`, deterministic layout order):

```
HaydnMultiStageSMS: Schedule found II=<n> NS=<n> prologue=<n> parcels
epilogue=<n> parcels kind=<k> [seat=<reason>] loop=bb.<N>.<name>
```

- `kind=accepted` — the engine committed a pipelined kernel (commit
  path). `kind=accepted-analysis` appears only under
  `-haydn-multistage-sms-analysis-only` (not used by this corpus: these
  fixtures pin the committed object).
- `kind=declined` — the engine candidated the loop but refused it
  (fail-closed); `seat=` names the rejecting seat.
- `kind=not-candidate` — the shape never entered the engine (no ZOL /
  soft-countdown single-BB loop).
- II semantics (AIE-compatible): accepted = the searched II (== realized
  parcel count per the G002 realized-parcel certificate); declined /
  not-candidate = the honest realized body parcel count ("body length as
  II" for tooling compatibility).

Fixtures CAPTURE II/NS as FileCheck variables (`[[II]]`, `[[NS]]`) and
pin `kind=` (and `seat=` for decline fixtures). Absolute II/NS values are
deliberately NOT pinned — the README policy below is the guard against
regression.

## II and NS definitions

- **II** = number of parcel lines from the kernel loop block's label
  (`.LLhwloop_start` / the loop MBB head) up to and including the parcel
  carrying the loop-end (HWLR_END addresses the last body parcel —
  inclusive; G004 rebind). Equivalently: the `#<swps> II=` AsmPrinter
  stamp, which the G002 certificate keeps equal to the realized kernel
  parcel count (`AchievedII`).
- **NS** = the stage count. Read it off the canonical remark's `NS=`
  field; in the committed asm, derive it from the
  HWLR_COUNT-setting immediate / peel geometry (`stage-mbb prolog=…`
  remark; `NS = prologue-peel depth + 1` for staged kernels, `NS=1` for
  kernel-only). `NS=0` on `not-candidate` lines means "no stage
  structure exists" (body length is the II field).

## AUTO-UPDATE POLICY (AIE postpipeliner README, adapted verbatim)

The detailed schedule of these tests may change over time, and the CHECK
lines may be automatically updated (e.g. re-capture with
`llvm-lit -sv`, then hand-check the diff) provided that **neither the II
nor the NS grows**. If the stage count grows, automatic update is allowed
only if the II shrinks.

- The II is the number of lines between the loop block's label up to and
  including the parcel headed by the loop-end label (equivalently the
  `II=` field of the canonical remark, which the G002 certificate keeps
  equal to the realized kernel parcel count).
- The stage count (NS) is determined by the canonical remark's `NS=`
  field (equivalently the immediate operand of the HWLR_COUNT-setting
  instruction / the peel geometry).

A diff that grows II, or grows NS without shrinking II, is a QoR
regression: do NOT auto-accept it — file it against
`topics/scheduling` (owner of the SMS engine) with the fixture name.

### G007 SEF-peel addendum (2026-08-23)

`sef-*` fixtures pin the side-effect-free boundary relaxation
(`HaydnMultiStageSMS::peelSideEffectFree`, AIE
`AIEPostPipeliner.cpp:1630-1676` port). When the trip gate would refuse
an II whose stage-0 is entirely SEF under the closed Haydn condition
(`isSEFPeelableMI`: no stores/calls/branches/implicit ops; loads only
with loop-INVARIANT addresses — golden grants no OOB exemption; no def
overlapping a live-in or live-out), the engine drops that stage:
NS decreases by 1, the accept remark gains `sef-peel=1`, and the kernel
accepts at the LOWER II with `HWLR_COUNT = trip - (NS-1)` using the
POST-PEEL NS. For SEF kernels the prologue retains the PRE-drop peel
DEPTH (stage groups; `NS = prologue-depth`, one more than the non-SEF
`NS = prologue-depth + 1` law — the parcel COUNT is content-dependent:
an all-SEF stage-0 peels its clones, an empty stage-0 peels nothing)
and the epilogue completions start at stage 2 (`EpiBase=2`, AIE
`visitPipelineSchedule:1736-1740`).

**Round-4 verdict (2026-08-23): the SEF acceptance window is EMPTY for
the current i32-latency body space — no fixture can yet pin
`sef-peel=1`.** Verified across q-rooted chains (k=2..9), load-rooted
chains, mixed-ALU shapes, and delay-3 FIR: every NS=3 schedule dies on
a NON-trip seat — `scheduleOtherIterations` (modulo infeasibility:
second-copy Earliest > Insert in the II-tight zone) or
`certificateExactCommitPlan` (the II-tight modulo-cycle groups cannot
coissue as E96 parcels) or the lifetime certificate. The peel can only
rescue trip-only refusals, and there are none. Structural cause: AIE's
live SEF window comes from its 4-6-cycle load latencies; Haydn's i32
load/MAC dest latency is [2], so NS=3 schedules only exist where II is
too tight to be feasible, and i64 MAC bodies that would span stages at
feasible IIs softexpand past the 96-instruction body cap. The port is
correct-but-inert (fail-closed at every seat; the deferred restore
keeps the peel from leaking into the next II attempt — pinned by
`sef-peel-window-empty.ll`, which must keep its plain no-peel accept).

**OPEN ITEM (owner: topics/scheduling residual): the `sef-peel=1`
accept pin.** No validating shape exists today. Unblocks when a
deeper-latency itinerary lands or a sub-96-instr i64 shape spans
stages at a feasible II; then re-derive `sef-peel-window-empty.ll`
into the accept pin — the engine side needs no further change. The
probed-family negative evidence above also feeds W60 (inter-block) and
M2 (IPC/KPI) planning: any QoR model that assumes SEF-peel rescues on
current i32 bodies over-counts — the window is empty, and every
boundary refusal observed is a non-trip seat the peel cannot touch.

## Running / updating

```bash
# All fixtures (from the monorepo root):
/ssd2/mhyang/haydn-build/bin/llvm-lit -sv \
  llvm/test/CodeGen/Haydn/sms-multistage

# One fixture, verbose (shows the captured II/NS):
/ssd2/mhyang/haydn-build/bin/llvm-lit -sv \
  llvm/test/CodeGen/Haydn/sms-multistage/<fixture>.ll
```

There is no `update_llvm_test_checks` automation for the remark arms —
the RMK CHECK lines are short and hand-maintained on purpose (the
policy, not the tooling, is the guard). After any engine change:

1. Run the corpus. Captured-variable failures = structural (kind/seat
   changed): review the engine change.
2. Green but different II/NS? Re-read the captured values against the
   policy before updating any comment that quotes them.

## Fixture inventory (15)

| File | Class | Expected kind |
|------|-------|---------------|
| `dot-basic.ll` | load-MAC-store dot (II ladder bottom) | accepted |
| `dot-chain-2.ll` | dot + 2-op dependent chain | accepted |
| `dot-chain-4.ll` | dot + 4-op dependent chain (deep) | accepted |
| `fir-delay2.ll` | FIR tap-delay, accumulate-only (LCD recurrence) | accepted |
| `fir-store-decline.ll` | FIR delay-line + per-iter store | declined (canonical-remark pin) |
| `memcpy-var-trip.ll` | memcpy, unproven runtime trip | declined |
| `trip2-boundary.ll` | const trip=2 + honest MD floor | accepted |
| `trip3-boundary.ll` | const trip=3 + honest MD floor | accepted |
| `sef-peel-window-empty.ll` | G007 window-EMPTY pin: peel fires at II=5/NS=3, downstream seats still refuse, plain no-peel accept (no `sef-peel`) | accepted (no SEF) |
| `sef-decline-nonsef-stage0.ll` | G007 control: phi-writing stage-0 never peels (no `sef-peel` field) | accepted (no SEF) |
| `large-ii.ll` | 8 serial MACs (II well above 7) | accepted |
| `single-stage.ll` | parallel adds, NS=1 resource-conflict | accepted |
| `unpack-extract-war.ll` | load, extract halves, recombine, store | declined |
| `deep-chain-store-observe.ll` | AchievedII-vs-II OBSERVATION (open classification) | accepted |
| `fir-circular-cb.ll` | circular-buffer (CBR intrinsic) FIR, lean | accepted-analysis (commit blocked by open verifier bug — see fixture header) |

Decline fixtures are part of the contract: they pin fail-closed refusal
(specific `kind=declined seat=…`, never a sequential fallback) while the
hardware loop still arms — the negative-arm discipline from the
2026-08-22 qualification.

## G002 follow-ups folded in (owner notes)

- `deep-chain-store-observe.ll` pins the current AchievedII(10) vs
  canonical II(9) divergence on the deep mul-chain + store body — open
  classification (legitimate dest-window stall accounting vs certificate
  gap), owner `topics/scheduling`. The fixture documents the observation
  without judging it; whichever way the classification lands, the file
  gets a REGRESSION TEST comment and the values updated under this
  README's policy.
- `fir-circular-cb.ll` is the accepting (analysis) lean form of the
  circular-AR probe. TWO findings pinned in its header: (1) the SMS
  COMMIT path on a CB-member kernel fails `-verify-machineinstrs` with
  tied-operand errors (4x, D_LDW_CB_IMM rs_wb tie) — isolated to
  commit-only (0 errors hwloop-only and analysis-only), owner
  `topics/scheduling`; the fixture runs analysis-only until fixed, then
  flips to a commit arm. (2) The full i64-accumulating CB FIR
  softexpands past the engine's 96-instruction body cap and stays
  not-candidate — a body-cap consequence, not an engine defect; do not
  raise MaxBodyInstrs without a plan-tree change.
