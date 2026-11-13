import importlib.util, subprocess, sys, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('bitstream',ROOT/'tools/convert_bitstream.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
def bit(part=b'7z020clg400-2\0',payload=bytes.fromhex('aa995566')*4):
 out=b'\0\x01x\0\x01'
 for tag,value in [(b'a',b'test\0'),(b'b',part),(b'c',b'2026\0'),(b'd',b'12:00\0'),(b'e',payload)]:out+=tag+len(value).to_bytes(4 if tag==b'e' else 2,'big')+value
 return out
class Tools(unittest.TestCase):
 def test_endianness_and_alignment(self):
  image,headers=module.convert(bit());self.assertEqual(image,bytes.fromhex('665599aa')*4);self.assertEqual(headers['b'],'7z020clg400-2')
 def test_wrong_part_rejected(self):
  with self.assertRaises(ValueError):module.convert(bit(part=b'7z010clg400\0'))
 def test_truncation_rejected(self):
  with self.assertRaises(ValueError):module.convert(bit()[:-1])
 def test_missing_sync_rejected(self):
  with self.assertRaises(ValueError):module.convert(bit(payload=b'\0'*16))
 def test_valid_profile_and_invalid_span(self):
  with tempfile.TemporaryDirectory() as folder:
   cmd=[sys.executable,str(ROOT/'tools/configure_network.py'),'--ul','430000000','--dl','440000000','--output',folder]
   subprocess.run(cmd,check=True,capture_output=True)
   self.assertEqual((Path(folder)/'tetra-active-pair.conf').read_text(),'430000000 440000000 network\n')
   self.assertNotEqual(subprocess.run(cmd+['--uls','430000000,432000000'],capture_output=True).returncode,0)
if __name__=='__main__':unittest.main()
