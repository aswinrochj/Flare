import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
KICAD = os.path.normpath(os.path.join(HERE, '..', '..', 'hardware', 'kicad'))
import sys
sys.path.insert(0, HERE)
from sexp import parse, find
from netlist import nets
t=parse(open(os.path.join(KICAD, 'safe-node.net')).read())[0]
got={}
for n in find(find(t,'nets')[0],'net'):
    name=find(n,'name')[0][1].strip('"').lstrip('/')
    nodes=set((find(x,'ref')[0][1].strip('"'),find(x,'pin')[0][1].strip('"')) for x in find(n,'node'))
    got[name]=nodes
want={k:set(v) for k,v in nets().items()}
ok=True
for k,v in want.items():
    g=got.get(k)
    if g is None:
        # find by members
        cand=[n for n,m in got.items() if m==v]
        print('MISSING NAME',k,'->',cand); ok=ok and bool(cand); continue
    if g!=v: print('DIFF',k,'missing',v-g,'extra',g-v); ok=False
extra=[n for n,m in got.items() if n not in want and len(m)>1 and not n.startswith('unconnected')]
print('extra multi-pin nets:',extra)
single=[(n,m) for n,m in got.items() if len(m)==1 and not n.startswith('unconnected') and not n.startswith('Net-')]
print('single-node named nets:',single)
print('nets want',len(want),'got',len([n for n in got if not n.startswith('unconnected')]),'OK' if ok and not extra else 'MISMATCH')
