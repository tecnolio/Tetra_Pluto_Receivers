"""Convert a timing-validated XC7Z020 bitstream for Linux FPGA manager."""
import argparse, hashlib, json
from pathlib import Path
def convert(bit):
 def take(n):
  nonlocal pos
  value=bit[pos:pos+n];pos+=n
  if len(value)!=n:raise ValueError('Truncated bitstream')
  return value
 pos=0;n=int.from_bytes(take(2),'big');take(n)
 if take(2)!=b'\x00\x01':raise ValueError('Invalid bit header')
 headers={}
 while True:
  tag=take(1)
  if tag not in (b'a',b'b',b'c',b'd',b'e'):raise ValueError('Invalid bit tag')
  size=int.from_bytes(take(4 if tag==b'e' else 2),'big');value=take(size)
  if tag==b'e':payload=value;break
  headers[tag.decode()]=value.rstrip(b'\0').decode('ascii')
 if '7z020clg400' not in headers.get('b',''):raise ValueError('Unsupported FPGA part')
 if len(payload)%4:raise ValueError('Unaligned payload')
 image=b''.join(payload[i:i+4][::-1] for i in range(0,len(payload),4))
 while len(image)%16:image+=bytes.fromhex('00000020')
 if bytes.fromhex('665599aa') not in image[:4096]:raise ValueError('Missing FPGA sync word')
 return image,headers
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--directory',type=Path,default=Path('build/fpga'));a=p.parse_args();d=a.directory
 if 'PROFILE_UL_100MHZ_VALIDATED' not in (d/'vivado.log').read_text(errors='replace'):raise SystemExit('Missing successful timing gate')
 if 'All user specified timing constraints are met' not in (d/'timing_summary.rpt').read_text(errors='replace'):raise SystemExit('Timing did not pass')
 image,headers=convert((d/'system_top_100mhz.bit').read_bytes());(d/'profile_ul.bin').write_bytes(image)
 (d/'build-validated.json').write_text(json.dumps(dict(headers=headers,image_sha256=hashlib.sha256(image).hexdigest(),max_rx_clock_hz=100000000),indent=2))
 print('Validated image:',len(image),'bytes; no FPGA loaded')
