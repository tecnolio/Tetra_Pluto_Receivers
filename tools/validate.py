"""Validate source closure and publication exclusions, without hardware access."""
from pathlib import Path
import xml.etree.ElementTree as ET
import re, hashlib, zipfile
ROOT=Path(__file__).resolve().parents[1]
def require(path):
 if not path.is_file():raise AssertionError(f'Missing source: {path}')
xpr=ROOT/'fpga/base/vivado/multi_ul_trial.xpr';base=xpr.parent
for node in ET.parse(xpr).iter('File'):
 name=node.get('Path','').replace('$PPRDIR',str(base)).replace('$PSRCDIR',str(base/'multi_ul_trial.srcs'))
 if name:require(Path(name))
for component in (ROOT/'fpga/ip_repo').rglob('component.xml'):
 for node in ET.parse(component).iter():
  if node.tag.endswith('}file'):
   name=next((v.text for v in node if v.tag.endswith('}name')),None)
   if name:require(component.parent/name)
excluded={'build','deps','private','.git','__pycache__'}
files=[p for p in ROOT.rglob('*') if p.is_file() and not excluded.intersection(p.relative_to(ROOT).parts)]
for p in files:
 rel=p.relative_to(ROOT)
 if p.suffix.lower()=='.apk' and rel.parent.as_posix()=='downloads' and re.fullmatch(r'PlutoPocket-[0-9]+\.[0-9]+\.apk',p.name):
  checksum=p.with_name(p.name+'.sha256')
  require(checksum)
  if checksum.read_text().split()[0]!=hashlib.sha256(p.read_bytes()).hexdigest():raise AssertionError(f'APK checksum mismatch: {rel}')
  with zipfile.ZipFile(p) as package:
   for name in package.namelist():
    if 'pluto_ca' in name:raise AssertionError('Personal CA in APK')
    data=package.read(name)
    if re.search(rb'[0-9a-f]+\.sn\.mynetname\.net',data):raise AssertionError('Personal endpoint in APK')
  continue
 if p.suffix.lower() in {'.apk','.jks','.key','.pem','.bin','.bit','.dcp','.pyc'}:raise AssertionError(f'Private/binary artifact: {rel}')
 raw=p.read_bytes()
 if p.resolve()==Path(__file__).resolve():continue
 for pattern in [rb'BEGIN (?:RSA )?PRIVATE KEY',rb'[0-9a-f]+\.sn\.mynetname\.net',rb'192\.168\.2\.[0-9]+',rb'/home/entrar/',rb'PL_VALLADOLID']:
  if re.search(pattern,raw):raise AssertionError(f'Unexpected private environment marker in {rel}')

print(f'PASS: {len(files)} publishable files, FPGA and ADI IP source closure complete')
