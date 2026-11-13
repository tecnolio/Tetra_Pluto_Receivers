"""Fetch pinned upstream source only after reviewing upstream licensing."""
import argparse, io, tarfile, urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCES=[('outerplane/tetra-codec','21d884064478d63306ec654378e666ae41503d00','tetra-codec'),('curlyboi/libtetradec','62479b9ba33a086c94187bab62e102590bb33d75','libtetradec')]
p=argparse.ArgumentParser();p.add_argument('--accept-upstream-terms',action='store_true');a=p.parse_args()
if not a.accept_upstream_terms:p.error('Review THIRD_PARTY.md and upstream terms; then use --accept-upstream-terms')
for repo,commit,name in SOURCES:
 target=ROOT/'deps'/name
 if target.exists():raise SystemExit(f'{target} exists; inspect it before replacing')
 url=f'https://codeload.github.com/{repo}/tar.gz/{commit}'
 with urllib.request.urlopen(url,timeout=90) as response: data=response.read(20_000_001)
 if len(data)>20_000_000:raise SystemExit('Unexpected archive size')
 with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive:
  for member in archive:
   parts=Path(member.name).parts[1:]
   if not parts or member.isdir():continue
   if not member.isfile() or any(v in ('..','') for v in parts):raise ValueError('Unsafe archive entry')
   dest=target.joinpath(*parts)
   if not dest.resolve().is_relative_to(target.resolve()):raise ValueError('Path escape')
   dest.parent.mkdir(parents=True,exist_ok=True)
   with archive.extractfile(member) as source:dest.write_bytes(source.read())
 (target/'UPSTREAM_COMMIT').write_text(commit+'\n')
 print(name,commit)
