"""For each AE_* macro, find its FINAL definition and whether it still needs
the AR arguments format E drops (a stride other than 8, or dir = 1)."""
import re

H = '/home/ckchen/haydn/llvm-project/clang/lib/Headers/haydn_dsp.h'
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

# the AR helpers whose prototypes change; last arg is dir, second-to-last stride
STEP = re.compile(r'haydn_ae_(la16x4|la64|sa16x4|sa64)_step\s*\(')
POS = re.compile(r'haydn_ae_sa64pos\s*\(')


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
        for call in re.finditer(STEP, text):
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
            if len(parts) >= 2:
                stride, dr = parts[-2].strip(), parts[-1].strip()
                if stride != '8':
                    needs.add(f'stride={stride}')
                if dr != '0':
                    needs.add(f'dir={dr}')
                where.append(f'{who}:{line}')
        if POS.search(text):
            tail = text.split('haydn_ae_sa64pos')[1]
            dr = tail.rsplit(',', 1)[1].split(')')[0].strip()
            if dr != '0':
                needs.add(f'dir={dr}')
            where.append(f'{who}:{line}')
    if needs:
        flagged.append((name, ', '.join(sorted(needs)), sorted(set(where))))

print(f'{len(CANDIDATES)} AE_* macros scanned; {len(flagged)} need attention.\n')
for name, verdict, where in flagged:
    print(f'{name:18} {verdict:34} defined last at line {defs[name][-1][0]}')
    if where:
        print(f'{"":18} via {", ".join(where)}')
if not flagged:
    print('  (none — every macro passes stride = 8, dir = 0)')
