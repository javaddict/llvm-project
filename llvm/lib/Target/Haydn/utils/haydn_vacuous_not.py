#!/usr/bin/env python3
"""Report CHECK-NOT assertions that forbid a spelling the target retired.

A negative assertion naming a retired mnemonic always passes. That is worse
than deleting it: it reads as coverage and is not. FORMAT-E-SWITCH-PLAN.md
5.4 records the whole class -- `cb96-simm6-ls-offset.ll` guarded the
migration's most consequential semantic change (the scaled LS immediate) and
every one of its assertions was inert, the positives matching a function label
and the negatives naming spellings the retarget had removed.

Same shape as 5.6's memberless-logical sweep: the answer should only ever
shrink, and any new row is a new place a test claims to check something it
does not.

    python3 llvm/lib/Target/Haydn/utils/haydn_vacuous_not.py
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), *[os.pardir] * 4))
TESTS = ("llvm/test/CodeGen/Haydn", "llvm/test/MC/Haydn")

# Closed set: what the format E switch retired. Deliberately a whitelist of
# DEAD spellings rather than a check against the live opcode table -- a
# CHECK-NOT legitimately names plenty of non-Haydn text (libcall symbols,
# `<unknown>`, generic MIR opcodes, pass debug output), and flagging those
# produces noise that buries the real rows.
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

DIRECTIVE = re.compile(r"^\s*[;#]\s*[A-Za-z0-9_-]+-NOT:\s*(.*)$")


def main() -> int:
    rows = []
    for rel in TESTS:
        for dirpath, _, names in os.walk(os.path.join(ROOT, rel)):
            for name in names:
                if not name.endswith((".ll", ".mir", ".s")):
                    continue
                path = os.path.join(dirpath, name)
                for lineno, line in enumerate(open(path, errors="replace"), 1):
                    m = DIRECTIVE.match(line.rstrip("\n"))
                    if not m:
                        continue
                    body = m.group(1).strip()
                    if not body or KEEP.search(body):
                        continue
                    if RETIRED.search(body):
                        rows.append((os.path.relpath(path, ROOT), lineno, body))

    for path, lineno, body in rows:
        print(f"{path}:{lineno}: forbids a retired spelling: {body}")
    print(f"\n{len(rows)} vacuous CHECK-NOT")
    return 1 if rows else 0


if __name__ == "__main__":
    sys.exit(main())
