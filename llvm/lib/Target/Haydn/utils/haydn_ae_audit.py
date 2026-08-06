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


TEN = ['AE_LA16X4_RIP', 'AE_LA16X4_RIC', 'AE_LA32X2_RIP', 'AE_LA32X2_RIC',
       'AE_SA16X4_RIP', 'AE_SA32X2_RIP', 'AE_SA32X2F24_RIP', 'AE_SA64NEG_FP',
       'AE_SA16X4_IP_X', 'AE_SA32X2_IP_X']

for name in TEN:
    chain = resolve(name)
    if not chain:
        print(f'{name:18} NOT DEFINED')
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
    verdict = ', '.join(sorted(needs)) if needs else 'CLEAN (no AR helper / all 8,0)'
    print(f'{name:18} {verdict:34} defined last at line {defs[name][-1][0]}')
    if where:
        print(f'{"":18} via {", ".join(sorted(set(where)))}')
