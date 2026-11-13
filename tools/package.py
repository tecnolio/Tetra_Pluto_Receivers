"""Build a source-derived tarball, excluding credentials and codecs' sources."""
import gzip, hashlib, io, tarfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];out=ROOT/'build';files={}
for name in ['pluto_tetra_profile','pluto_http','pluto_https']:files['backend/'+name]=(out/'backend'/name).read_bytes()
files['fpga/fixed.bin.gz']=gzip.compress((out/'fpga/profile_ul.bin').read_bytes(),mtime=0)
for folder in ['runtime','web']:
 for p in (ROOT/folder).rglob('*'):
  if p.is_file():files[p.relative_to(ROOT).as_posix()]=p.read_bytes()
files['package.sha256']=''.join(hashlib.sha256(data).hexdigest()+'  '+name+'\n' for name,data in sorted(files.items())).encode()
with tarfile.open(out/'tetra-pluto-reference.tar.gz','w:gz') as archive:
 for name,data in sorted(files.items()):
  info=tarfile.TarInfo('tetra-pluto-reference/'+name);info.size=len(data);info.mode=0o755 if name.startswith(('backend/','runtime/')) else 0o644;info.mtime=0
  archive.addfile(info,io.BytesIO(data))
print(out/'tetra-pluto-reference.tar.gz')
