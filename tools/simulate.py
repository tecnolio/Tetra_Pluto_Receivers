"""Run channelizer regression using Icarus Verilog; no SDR access."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1];rtl=root/'fpga/rtl'
with tempfile.TemporaryDirectory() as td:
 output=Path(td)/'channelizer.vvp'
 subprocess.run(['iverilog','-g2012','-s','tb','-o',str(output),*(str(rtl/f) for f in ['tb.v','tetra_channelizer.v','tetra_ddc_cic_lane.v','tetra_fir_dec2.v'])],check=True)
 for n in [1,2,4,8]:
  for mode in [[],['+single'],['+dl_single','+primary=5']]:
   subprocess.run(['vvp',str(output),f'+count={n}',*mode],cwd=rtl,check=True)
