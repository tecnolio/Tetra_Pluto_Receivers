"""Read target libraries via SSH. Does not change the target. Requires paramiko."""
import argparse, getpass, shlex
from pathlib import Path
import paramiko
p=argparse.ArgumentParser();p.add_argument('host');p.add_argument('--port',type=int,default=22);p.add_argument('--user',default='root');p.add_argument('--output',type=Path,default=Path('private/sysroot'));p.add_argument('--password',action='store_true');a=p.parse_args()
c=paramiko.SSHClient();c.load_system_host_keys();c.set_missing_host_key_policy(paramiko.RejectPolicy())
c.connect(a.host,port=a.port,username=a.user,password=getpass.getpass('SSH password: ') if a.password else None,timeout=15)
try:
 for name in ['usr/lib/libiio.so','lib/libc.so.6','lib/libm.so.6','lib/libpthread.so.0','lib/libgcc_s.so.1','lib/ld-linux-armhf.so.3','usr/lib/libz.so.1']:
  _,stdout,stderr=c.exec_command('cat '+shlex.quote('/'+name),timeout=30);data=stdout.read();error=stderr.read()
  if stdout.channel.recv_exit_status() or data[:4]!=b'\x7fELF':raise RuntimeError(f'Cannot read ELF {name}: {error.decode(errors="replace")}')
  target=a.output/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data);print(name,len(data))
finally:c.close()
