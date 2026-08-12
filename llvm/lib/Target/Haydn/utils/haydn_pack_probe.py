#!/usr/bin/env python3
"""Does the packer's greedy placement ever pack fewer ops than it could?

This is the measurement behind CB-147. HaydnBundleFormatSolver.h::tryAdd walks
an instruction's PlacementAlternatives in DESCENDING slot-bit order and commits
the first one that fits, with no backtracking. The two product formats have
disjoint slot sets ({P20,P21} vs {P30,P31,P32}) and HaydnBundlePlan.h says so
outright -- "no occupancy is covered by both rows" -- so the FIRST member placed
chooses the format for the whole cycle. Anything holding a P3x placement takes
the 3-entry format at once, and every instruction whose placements are all P2x
(a wide immediate only fits a 2-entry entry) is locked out for the rest of that
cycle.

The script models tryAdd exactly and compares it against an exhaustive matching
over the same alternatives. It reads the generated member table rather than
restating it, so a regenerated layout re-measures instead of going stale.

Reports, for sequences of 2 and 3 instructions, how many pack fewer ops under
greedy than a valid assignment admits. Zero would mean the greedy walk is
already optimal for the delivered layout.
"""

import argparse
import re
import sys
from itertools import combinations_with_replacement, permutations
from pathlib import Path

# Slot (entry position) and unit bit values, mirroring HaydnBaseInfo.h. They
# are separate bit spaces: SlotBits says WHERE in the bundle, UnitBits says
# WHICH hardware serves it.
SLOT_BIT = {"P20": 1 << 0, "P21": 1 << 1,
            "P30": 1 << 2, "P31": 1 << 3, "P32": 1 << 4}
UNIT_BIT = {"LOADSTORE0": 1 << 0, "LOAD1": 1 << 1, "ALU0": 1 << 2,
            "ALU1": 1 << 3, "ALU2": 1 << 4, "MAC0": 1 << 5, "MAC1": 1 << 6}

# The two ProductFormatRows slot sets (HaydnBundlePlan.h).
E2 = SLOT_BIT["P20"] | SLOT_BIT["P21"]
E3 = SLOT_BIT["P30"] | SLOT_BIT["P31"] | SLOT_BIT["P32"]
FAMILIES = (E2, E3)

# Members are named <LOGICAL>_P<pos>_<UNIT>. Anchor on the suffix: logical
# names contain underscores (S_SW_WITH_IMM_P30_LOADSTORE0). \s*: because the
# generated defs are column-aligned and some carry two spaces before the colon.
DEF_RE = re.compile(r"^def ([A-Za-z0-9_]+)_(P2[01]|P3[012])_"
                    r"(LOADSTORE0|LOAD1|ALU[012]|MAC[01])\s*:")

DEFAULT_TD = (Path(__file__).resolve().parent.parent /
              "HaydnFormatEEncoding.td")


def load_placements(td_path):
    """logical -> tuple of (slot bit, unit bit), descending by slot bit."""
    placements = {}
    with open(td_path) as f:
        for line in f:
            m = DEF_RE.match(line)
            if m:
                logical, pos, unit = m.groups()
                placements.setdefault(logical, set()).add(
                    (SLOT_BIT[pos], UNIT_BIT[unit]))
    if not placements:
        sys.exit(f"no members parsed from {td_path} -- has the naming or the "
                 f"alignment of the generated defs changed?")
    return {k: tuple(sorted(v, reverse=True)) for k, v in placements.items()}


def greedy(seq):
    """tryAdd applied in order; returns how many of seq it accepts."""
    occ_slots = occ_units = 0
    mask = E2 | E3
    accepted = 0
    for alts in seq:
        for slot, unit in alts:            # already descending by slot bit
            if occ_slots & slot or occ_units & unit:
                continue
            new_occ = occ_slots | slot
            # A family survives only while it covers every occupied slot.
            new_mask = 0
            for fam in FAMILIES:
                if (mask & fam) == fam and not (new_occ & ~fam):
                    new_mask |= fam
            if not new_mask:
                continue
            occ_slots, occ_units, mask = new_occ, occ_units | unit, new_mask
            accepted += 1
            break
        else:
            break                          # cycle closes here
    return accepted


def feasible(seq):
    """Largest prefix of seq admitting SOME valid assignment, any order."""
    best = 0

    def rec(fam, i, used_slots, used_units, n):
        nonlocal best
        best = max(best, n)
        if i == len(seq):
            return
        for slot, unit in seq[i]:
            if not (slot & fam) or used_slots & slot or used_units & unit:
                continue
            rec(fam, i + 1, used_slots | slot, used_units | unit, n + 1)

    for fam in FAMILIES:
        rec(fam, 0, 0, 0, 0)
    return best


def name_of(bit, table):
    return next(k for k, v in table.items() if v == bit)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.
                                 RawDescriptionHelpFormatter)
    ap.add_argument("--encoding-td", type=Path, default=DEFAULT_TD,
                    help="generated HaydnFormatEEncoding.td (default: beside "
                         "this script's target directory)")
    ap.add_argument("--show", type=int, default=8,
                    help="how many losing sequences to print (default 8)")
    args = ap.parse_args()

    placements = load_placements(args.encoding_td)
    members = sum(len(a) for a in placements.values())

    # Collapse to distinct placement signatures. Two logicals with the same
    # (position, unit) set behave identically here, so this is lossless and it
    # is what makes an all-pairs sweep tractable.
    sigs = {}
    for logical, alts in placements.items():
        sigs.setdefault(alts, []).append(logical)

    print(f"{len(placements)} logicals, {members} members, "
          f"{len(sigs)} distinct placement signatures")

    e2_only = [s for s in sigs if all(not (slot & E3) for slot, _ in s)]
    locked = sum(len(sigs[s]) for s in e2_only)
    print(f"{locked} logicals have no 3-entry placement at all, so they can "
          f"only ever share a bundle when placed first in their cycle")

    keys = list(sigs)
    gaps = []
    for size in (2, 3):
        for combo in combinations_with_replacement(keys, size):
            want = feasible(list(combo))
            worst = min(greedy(list(p)) for p in set(permutations(combo)))
            if worst < want:
                gaps.append((size, combo, worst, want))

    print(f"\n{len(gaps)} instruction sequences pack fewer ops under the "
          f"greedy walk than a valid assignment admits")
    for size, combo, got, want in gaps[:args.show]:
        print(f"  {size} instrs: greedy packs {got}, feasible {want}")
        for sig in combo:
            pretty = ",".join(f"{name_of(s, SLOT_BIT)}/{name_of(u, UNIT_BIT)}"
                              for s, u in sig)
            peers = len(sigs[sig]) - 1
            print(f"      {sigs[sig][0]:<22} {pretty}"
                  f"{f'  (+{peers} peers)' if peers else ''}")
    if len(gaps) > args.show:
        print(f"  ... and {len(gaps) - args.show} more")

    return 0


if __name__ == "__main__":
    sys.exit(main())
