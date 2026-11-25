import re
TOK = re.compile(r'\s*(\(|\)|"(?:[^"\\]|\\.)*"|[^\s()"]+)')
def parse(text):
    pos = 0; stack = [[]]
    for m in TOK.finditer(text):
        t = m.group(1)
        if t == '(':
            stack.append([])
        elif t == ')':
            x = stack.pop(); stack[-1].append(x)
        else:
            stack[-1].append(t)
    return stack[0]
def dump(x, ind=0):
    if isinstance(x, list):
        simple = all(not isinstance(e, list) for e in x)
        if simple: return '(' + ' '.join(x) + ')'
        out = '(' + ' '.join(e for e in x if not isinstance(e, list) and x.index(e) < 3 and False)  # placeholder
        parts = []; head = []
        for e in x:
            if isinstance(e, list): parts.append(dump(e, ind+1))
            else:
                if parts: parts.append(e)
                else: head.append(e)
        return '(' + ' '.join(head) + '\n' + '\n'.join('  '*(ind+1) + p for p in parts) + ')'
    return x
def find(x, key):
    return [e for e in x if isinstance(e, list) and e and e[0] == key]
def get_symbol(lib, name):
    tree = parse(open(f'/usr/share/kicad/symbols/{lib}.kicad_sym').read())[0]
    for s in find(tree, 'symbol'):
        if s[1] == f'"{name}"': return s
    raise KeyError(name)
def pins(sym):
    out = []
    def walk(x):
        for e in x:
            if isinstance(e, list):
                if e[0] == 'pin':
                    at = find(e, 'at')[0]; nm = find(e, 'name')[0][1].strip('"'); num = find(e, 'number')[0][1].strip('"')
                    ln = float(find(e, 'length')[0][1])
                    out.append(dict(num=num, name=nm, x=float(at[1]), y=float(at[2]), ang=float(at[3]) if len(at) > 3 else 0, len=ln, type=e[1]))
                else: walk(e)
    walk(sym); return out
