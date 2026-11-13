"""Cross compile ARMv7 hard-float software. Never deploys or touches SDR."""
import argparse, os, shutil, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--sysroot',type=Path,required=True);p.add_argument('--codecs',type=Path,default=ROOT/'deps');p.add_argument('--clang',default='clang');p.add_argument('--lld',default='ld.lld');p.add_argument('--go',default='go');a=p.parse_args()
out=ROOT/'build/backend';out.mkdir(parents=True,exist_ok=True)
src=ROOT/'backend/src';shim=ROOT/'backend/codec-include';codec=a.codecs/'tetra-codec';dec=a.codecs/'libtetradec'
libs=['usr/lib/libiio.so','lib/libc.so.6','lib/libm.so.6','lib/libpthread.so.0','lib/libgcc_s.so.1','usr/lib/libz.so.1']
for f in [*(a.sysroot/v for v in libs),codec/'include/tetra-codec.h']:
 if not f.is_file():raise SystemExit(f'Missing {f}')
flags=['--target=arm-linux-gnueabihf','-mcpu=cortex-a9','-mfpu=neon-vfpv3','-mfloat-abi=hard','-O3','-ffreestanding','-fno-stack-protector','-fno-pie','-ffunction-sections','-fdata-sections','-I',str(src),'-I',str(codec/'include')]
def run(cmd,**kw):subprocess.run(list(map(str,cmd)),check=True,**kw)
def compile(source,name,extra=()):
 obj=out/(name+'.o');run([a.clang,*flags,*extra,'-c',source,'-o',obj]);return obj
objects={name:compile(src/name,name) for name in ['startup.S','entry.c','core.c','receiver_fpga_profile.c','tetra_voice.c','http.c','portable_web.c']}
channel=sorted(dec.glob('*.c'));speech=sorted((codec/'source').glob('*.c'))
if not channel or not speech:raise SystemExit('Missing codec sources: run fetch_codecs.py first')
codecs=[compile(f,'codec_'+f.stem,['-fwrapv','-I',str(shim)]) for f in channel]
codecs += [compile(f,'speech_'+f.stem,['-fwrapv','-I',str(shim),'-include',str(shim/'reentrant-prefix.h')]) for f in speech]
common=[objects['startup.S'],objects['entry.c']]
for name,objs in [('pluto_tetra_profile',[objects[n] for n in ['core.c','receiver_fpga_profile.c','tetra_voice.c']]+codecs),('pluto_http',[objects['http.c'],objects['portable_web.c']])]:
 target=out/name
 run([a.lld,'-m','armelf_linux_eabi','-e','_start','--dynamic-linker','/lib/ld-linux-armhf.so.3','--allow-shlib-undefined','--gc-sections','-o',target,*common,*objs,*(a.sysroot/f for f in libs)])
 header=target.read_bytes()[:20]
 if header[:4]!=b'\x7fELF' or header[4:6]!=b'\x01\x01' or header[18:20]!=b'\x28\x00':raise SystemExit('Expected ELF32 little-endian ARM')
 print(name,target.stat().st_size,'bytes, ELF32 ARM')
env=os.environ.copy();env.update(GOOS='linux',GOARCH='arm',GOARM='7',CGO_ENABLED='0')
run([a.go,'build','-trimpath','-o',out/'pluto_https',src/'https_proxy.go'],env=env)
print('Built HTTPS proxy, linux/arm/v7')
