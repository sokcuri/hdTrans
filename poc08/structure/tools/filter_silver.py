"""Drop noisy silver heads (interjection/filler dependents and heads, fragment lines) from a gold file.

usage: filter_silver.py IR.jsonl GOLD.jsonl OUT.jsonl
"""

import json,sys,re
irp,gp,outp=sys.argv[1:4]
ir={(json.loads(l)['doc'],json.loads(l)['line']):json.loads(l) for l in open(irp)}
n=d=0
with open(outp,'w') as f:
    for l in open(gp):
        g=json.loads(l); o=ir[(g['doc'],g['line'])]; b=o['buns']
        nI=sum(x['type']=='I' for x in b)
        frag = nI>=2 or o['src'].count('…')+o['src'].count('・・')>=3 or len(b)<2
        h=g['heads']
        for i,x in enumerate(b):
            if h[i]<0: continue
            n+=1
            if frag or x['type']=='I' or x.get('cwc') in ('filler','intj') or (h[i]<len(b) and b[h[i]]['type']=='I'):
                h[i]=-2; d+=1
        f.write(json.dumps({'doc':g['doc'],'line':g['line'],'heads':h},ensure_ascii=False)+'\n')
print('dropped',d,'of',n)
