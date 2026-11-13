"""Generate SD profile files locally. Does not contact a Pluto or start RF."""
import argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--ul',type=int,required=True);p.add_argument('--dl',type=int,required=True);p.add_argument('--uls',default='');p.add_argument('--name',default='Mi red');p.add_argument('--output',type=Path,default=Path('private/config'));a=p.parse_args()
uls=sorted(set(map(int,a.uls.split(',')))) if a.uls else [a.ul]
if len(uls)>8 or not uls or a.ul not in uls or a.dl-a.ul!=10000000 or max(uls)+10000000-min(uls)>11800000 or any(v<70000000 or v>990000000 or v%25000 for v in uls):p.error('Requires 1..8 UL, 25kHz raster, DL=UL+10MHz, total span <=11.8MHz')
if not a.name or len(a.name)>100 or any(ord(c)<32 or ord(c)==127 for c in a.name):p.error('Invalid name')
a.output.mkdir(parents=True,exist_ok=True)
(a.output/'tetra-networks.tsv').write_text(f'network\t{a.name}\t{a.ul}\t{a.dl}\t'+','.join(map(str,uls))+'\t'+','.join(str(v+10000000) for v in uls)+'\n',encoding='utf-8')
(a.output/'tetra-active-pair.conf').write_text(f'{a.ul} {a.dl} network\n')
print('Configuration generated locally:',a.output)
