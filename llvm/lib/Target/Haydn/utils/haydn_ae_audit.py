"""For each AE_* macro, find its FINAL definition and whether it still needs
the AR arguments format E drops (a stride, or a direction select).

Those two arguments are gone from the helpers rather than defaulted, so the
question is now one of arity: a macro that still needs them is one that hands
an AR helper more arguments than the helper takes. The expected arity is read
back out of haydn_dsp.h's own `static inline` definitions — hardcoding it here
would be a second copy of a fact the header already states, which is the shape
of drift that let the builtins and the emitter's specialPublicShape() disagree
until the header stopped compiling."""
import re
import sys

import pathlib
H = sys.argv[1] if len(sys.argv) > 1 else str(
    pathlib.Path(__file__).resolve().parents[5] / "clang/lib/Headers/haydn_dsp.h")
src = open(H).read().split('\n')

# join line continuations into logical definitions
defs = {}          # macro name -> list of (line_no, full text)
i = 0
while i < len(src):
    m = re.match(r'#define\s+(\w+)', src[i])
    if m:
        text, start = src[i], i
        while text.rstrip().endswith('\\'):
            i += 1
            text = text.rstrip()[:-1] + ' ' + src[i].strip()
        defs.setdefault(m.group(1), []).append((start + 1, ' '.join(text.split())))
    i += 1

# The AR helpers whose prototypes changed, with the arity each one actually
# has, read from its definition in the header.
HELPER_DEF = re.compile(
    r'^static inline \S+ (haydn_ae_(?:la16x4_step|la64_step|sa16x4_step|'
    r'sa64_step|sa64pos))\((.*?)\)\s*\{', re.M | re.S)
HELPERS = {}
_joined = '\n'.join(src)
for m in HELPER_DEF.finditer(_joined):
    HELPERS[m.group(1)] = len([p for p in m.group(2).split(',') if p.strip()])
if len(HELPERS) != 5:
    raise SystemExit('haydn_ae_audit: expected 5 AR helper definitions in '
                     f'{H}, found {sorted(HELPERS)}')

CALL = re.compile(r'(' + '|'.join(sorted(HELPERS)) + r')\s*\(')


def resolve(name, depth=0, seen=None):
    """Text of the final definition, following _OVERLOAD/_3A/_4A delegation."""
    seen = seen or set()
    if name in seen or depth > 6 or name not in defs:
        return []
    seen.add(name)
    _, text = defs[name][-1]           # last #define wins
    out = [(name, defs[name][-1][0], text)]
    for callee in re.findall(r'(__AE_\w+)', text):
        out += resolve(callee, depth + 1, seen)
    return out


# EVERY public AE_* macro, derived from the header — not a hardcoded list.
#
# It used to be a hardcoded list of ten, and that is exactly how it under-
# reported: AE_LA32X2F24_RIP delegates to __AE_LA32X2_RIP_{3A,4A} and needs
# dir = 1 like its peers, but it was not one of the ten names, so `resolve`
# never ran on it. The resolution logic was fine; the candidate set was the
# bug. Ground truth is the preprocessor — expand every macro and look at what
# actually reaches an AR helper — and this scan is the cheap approximation of
# that, so keep it derived.
CANDIDATES = sorted(n for n in defs if re.fullmatch(r'AE_[A-Z0-9_]+', n))

flagged = []
for name in CANDIDATES:
    chain = resolve(name)
    if not chain:
        continue
    # Already withdrawn (§ 8 Q2) — report separately so "clean" keeps meaning
    # "needs nothing", not "no longer expands to anything".
    if any('__HAYDN_AE_WITHDRAWN_STMT' in text for _, _, text in chain):
        flagged.append((name, 'WITHDRAWN (§ 8 Q2)', []))
        continue
    needs = set()
    where = []
    for who, line, text in chain:
        for call in re.finditer(CALL, text):
            helper = call.group(1)
            args = text[call.end():]
            depth, cur, parts = 1, '', []
            for ch in args:
                if ch == '(':
                    depth += 1
                elif ch == ')':
                    depth -= 1
                    if depth == 0:
                        parts.append(cur)
                        break
                if depth == 1 and ch == ',':
                    parts.append(cur)
                    cur = ''
                else:
                    cur += ch
            want = HELPERS[helper]
            if len(parts) > want:
                extra = ', '.join(p.strip() for p in parts[want:])
                needs.add(f'{helper} takes {want}, given {len(parts)}: {extra}')
                where.append(f'{who}:{line}')
    if needs:
        flagged.append((name, '; '.join(sorted(needs)), sorted(set(where))))

print(f'{len(CANDIDATES)} AE_* macros scanned; {len(flagged)} need attention.\n')
for name, verdict, where in flagged:
    print(f'{name:18} {verdict:34} defined last at line {defs[name][-1][0]}')
    if where:
        print(f'{"":18} via {", ".join(where)}')
if not flagged:
    print('  (none — every macro calls the AR helpers at their real arity)')

# Exit status, and the distinction it draws. WITHDRAWN is the ACCEPTED state
# (§ 8 Q2 decided it for eleven macros), so those rows report without failing.
# An arity row is a defect and fails. Until now everything exited 0, so a real
# fault -- a macro handing an AR helper a stride again -- was reported to a
# human and passed to a script; that is the same shape as the vacuous-CHECK-NOT
# gate reading zero files (plan § 6.15).
defects = [r for r in flagged if 'WITHDRAWN' not in r[1]]
if defects:
    print(f'\n{len(defects)} of them are arity defects, not withdrawals.')
sys.exit(1 if defects else 0)
