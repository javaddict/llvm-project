#!/usr/bin/env python3
"""Report negative assertions that forbid something the target cannot emit.

A negative assertion naming a spelling that does not exist always passes. That
is worse than deleting it: it reads as coverage and is not.
FORMAT-E-SWITCH-PLAN.md 5.4 records the class -- `cb96-simm6-ls-offset.ll`
guarded the migration's most consequential semantic change and every one of its
assertions was inert -- and 6.16 records that this is only one of four ways a
FileCheck line can assert nothing.

Same shape as 5.6's memberless-logical sweep: the answer should only ever
shrink, and any new row is a new place a test claims to check something it
does not.

    python3 llvm/lib/Target/Haydn/utils/haydn_vacuous_not.py

Two checks run. The first needs no build; the second is the one with teeth.

1. A hand-written list of spellings the switch RETIRED. Cheap, and it works on
   a tree that has never been built.

2. **Liveness against the generated tables.** Every token in a negative
   assertion that is shaped like a mnemonic, an opcode or an intrinsic is
   looked up in what the target can actually emit. This is what the whitelist
   could not do: `CHECK-NOT: HWLOOP_END` named a spelling that was never on
   any list, in a file whose own header said the regression signature was a
   BLT back-edge -- so the test asserted nothing AND the thing it meant to
   assert was missing. Substring containment, because that is how FileCheck
   matches: `CHECK-NOT: mul32` is live, since `x2mul32` contains it.

Both llvm and clang tests are scanned; clang was out of scope entirely until
now (6.15). clang/test/Sema is deliberately NOT in the list: it is thousands of
tests that have nothing to do with this target, and one row from
attr-availability-swift.c is one row too many for a gate that is meant to be
read every time.

WHAT IS DELIBERATELY NOT REPORTED, having been looked at once:

* `llvm.haydn.*` names that never existed. `x2cmula-isqrt-probe.c` forbids
  `llvm.haydn.x2cmula32` to say the composed op must not survive as a single
  intrinsic -- but it never was one, so the NOT cannot fire. The positives
  beside it (`x2cmul32` plus two `add64`) already assert the decomposition,
  so this is belt over braces rather than a hole, and it is a different
  category from a spelling that USED to exist.
* assembler directives (`.cfi_def_cfa_register`) and pass titles
  (`HaydnPostLegalizerCombiner`). Both are emitted by tools whose vocabulary
  is not the instruction tables, so their absence there proves nothing.

Both exclusions are shape-based below rather than a name list, so they do not
need maintaining.
"""
import os
import re
import sys

# Five levels: utils -> Haydn -> Target -> lib -> llvm -> repo root.
#
# This was FOUR, which lands on <repo>/llvm. os.walk of a path that does not
# exist yields nothing, so the gate reported "0 vacuous CHECK-NOT" without
# opening a single file, and had done since it was written. A gate whose whole
# purpose is to catch assertions that pass by not checking anything was passing
# by not checking anything. Verified after the fix by the count moving off 0.
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), *[os.pardir] * 5))
TESTS = (
    "llvm/test/CodeGen/Haydn",
    "llvm/test/MC/Haydn",
    "clang/test/Headers",
    "clang/test/CodeGen/Haydn",
)
SUFFIXES = (".ll", ".mir", ".s", ".c")

# What the target can emit, as generated. Whole-file containment on purpose:
# a token absent from all three cannot be printed by any tool, and matching a
# stray substring only makes this MORE conservative, which is the right
# direction for a gate whose every row has to be worth acting on.
VOCAB = (
    "build/lib/Target/Haydn/HaydnGenInstrInfo.inc",   # opcode enum names
    "build/lib/Target/Haydn/HaydnGenAsmWriter.inc",   # asm mnemonics
    "build/include/llvm/IR/IntrinsicImpl.inc",        # llvm.haydn.* IR names
)

# Closed set: what the format E switch retired. Deliberately a whitelist of
# DEAD spellings rather than a check against the live opcode table -- a
# negative assertion legitimately names plenty of non-Haydn text (libcall
# symbols, `<unknown>`, generic MIR opcodes, pass debug output), and flagging
# those produces noise that buries the real rows.
RETIRED = re.compile(
    r"\b("
    r"ld8|ldu8|ld16|ldu16|ld32|ld64|st8|st16|st32|st64"     # 5.6 load/store
    r"|LD8|LDU8|LD16|LDU16|LD32|LD64|ST8|ST16|ST32|ST64"
    r"|\w+_[wW]\b"                                          # 5.1 _W forms
    r"|\w+_[sS][012]\b|\w+_[sS][012]_FLEX"                  # Bundle128 members
    r"|\w+_M0S0\w*|BUNDLE128\w*"
    r"|c\.[a-z]"                                            # compressed, retired
    r")"
)

# Still live despite matching the pattern above.
KEEP = re.compile(r"(LD32|ST32|LD64|ST64)_POST_INC|ld32_reg|st32_reg")

# `//` as well as `;` and `#`: several .s files use it, and the gate could not
# see any of their negatives (6.16).
DIRECTIVE = re.compile(r"^\s*(?:[;#]|//)\s*[A-Za-z0-9_-]+-NOT:\s*(.*)$")

# A token worth looking up: mnemonic-, opcode-, pseudo- or intrinsic-shaped.
# Plain English words and bare identifiers are excluded, because negatives name
# pass titles ("Haydn Load/Store Optimizer") and debug text as a matter of
# course.
SHAPED = re.compile(
    r"^(?:"
    r"[a-z][a-z0-9]*(?:_[a-z0-9]+)*\d[a-z0-9_]*"  # add32, s_lw_with_imm, x2mul32
    r"|[A-Z][A-Z0-9]*(?:_[A-Z0-9]+)+"             # ADDI32_P21_ALU1, HWLOOP_END
    r")$"
)
# Emitted by tools whose vocabulary is not the instruction tables: assembler
# directives and dotted intrinsic paths. Their absence from those tables says
# nothing, so shape them out rather than list them.
OTHER_VOCABULARY = re.compile(r"^(?:llvm\.|\.)|\.")
TOKEN = re.compile(r"\b[A-Za-z][A-Za-z0-9]{2,}(?:[._][A-Za-z0-9]+)*\b")
# Registers and block labels are neither, and appear in negatives constantly.
NOT_A_SPELLING = re.compile(r"^([rd]\d+|sp|fp|lr|LBB\d+_\d+|\.?[Ll]?\.?LBB.*)$")


def load_vocab():
    """The union of what the generated tables contain, or None if unbuilt."""
    parts, missing = [], []
    for rel in VOCAB:
        path = os.path.join(ROOT, rel)
        if os.path.exists(path):
            parts.append(open(path, errors="replace").read())
        else:
            missing.append(rel)
    return ("\n".join(parts) if parts else None), missing


def directives():
    for rel in TESTS:
        base = os.path.join(ROOT, rel)
        if not os.path.isdir(base):
            continue
        for dirpath, _, names in os.walk(base):
            for name in names:
                if not name.endswith(SUFFIXES):
                    continue
                path = os.path.join(dirpath, name)
                for lineno, line in enumerate(open(path, errors="replace"), 1):
                    m = DIRECTIVE.match(line.rstrip("\n"))
                    if m and m.group(1).strip():
                        yield (os.path.relpath(path, ROOT), lineno,
                               m.group(1).strip())


def main() -> int:
    vocab, missing = load_vocab()
    rows = []
    for path, lineno, body in directives():
        if KEEP.search(body):
            continue
        if RETIRED.search(body):
            rows.append((path, lineno, f"forbids a retired spelling: {body}"))
            continue
        if vocab is None:
            continue
        # Regex blocks match whatever they match; only literal text can be dead.
        literal = re.sub(r"\{\{.*?\}\}", " ", body)
        for tok in TOKEN.findall(literal):
            if NOT_A_SPELLING.match(tok) or OTHER_VOCABULARY.search(tok):
                continue
            if not SHAPED.match(tok):
                continue
            if tok not in vocab:
                rows.append((path, lineno,
                             f"forbids {tok!r}, which nothing can emit: {body}"))
                break

    for path, lineno, why in rows:
        print(f"{path}:{lineno}: {why}")

    if missing:
        # Never silently degrade: a gate that reads 0 because it could not look
        # is the § 6.6 family of failure, and this file exists to stop exactly
        # that kind of reassurance.
        print(f"\nLIVENESS CHECK SKIPPED — missing {', '.join(missing)}."
              f"\nBuild the target first; the count below is the whitelist only.")
    print(f"\n{len(rows)} vacuous CHECK-NOT")
    return 1 if rows else 0


if __name__ == "__main__":
    sys.exit(main())
