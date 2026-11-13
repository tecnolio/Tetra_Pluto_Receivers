#!/bin/sh
# Permanent profile-driven 1..8 UL / one-primary-DL receiver; stop restores stock FPGA.
set -eu
D=/run/pluto_lab/fpga-profile
A=/run/pluto_lab/runtime/fpga-web-active
R=/run/pluto_lab
S=$R/runtime/fpga-web.json
TOKEN=${2:-}
UL=0;DL=0;ULS=0
if [ "${1:-}" = run ]; then
 read UL DL PROFILE </mnt/mmc/tetra-active-pair.conf
 ULS=$(awk -F '\t' -v id="$PROFILE" '$1==id {print $5;exit}' /mnt/mmc/tetra-networks.tsv)
fi
LO=0
[ ! -f /mnt/mmc/tetra-profile/lo.conf ] || read LO </mnt/mmc/tetra-profile/lo.conf
GAIN=35
[ ! -f /mnt/mmc/tetra-profile/gain.conf ] || read GAIN </mnt/mmc/tetra-profile/gain.conf
if [ "${1:-}" = run ] && [ -f "$R/fpga-web/next-gain" ]; then read GAIN <"$R/fpga-web/next-gain";rm -f "$R/fpga-web/next-gain";fi
case "$GAIN" in ''|*[!0-9]*) exit 2;;esac
[ "$GAIN" -ge 0 ] && [ "$GAIN" -le 73 ]
state(){
 active=false;[ ! -d "$A" ] || active=true
 printf '{"available":true,"active":%s,"persistent":true,"state":"%s","ends_at":0,"ul_hz":%s,"dl_hz":%s,"ul_frequencies_hz":[%s],"rx_port":1,"gain_db":%s}\n' "$active" "$1" "$UL" "$DL" "$ULS" "$GAIN" >"$S.$$.new";mv "$S.$$.new" "$S"
}
stop_native(){
 [ -f "$A/test.pid" ] || return 0
 pid=$(cat "$A/test.pid")
 [ -r "/proc/$pid/cmdline" ] && grep -F -q "$D/pluto_tetra_profile" "/proc/$pid/cmdline" || return 0
 kill "$pid" 2>/dev/null || true
 for i in 1 2 3 4 5; do [ -r "/proc/$pid/cmdline" ] && grep -F -q "$D/pluto_tetra_profile" "/proc/$pid/cmdline" || return 0;sleep 1;done
 kill -9 "$pid" 2>/dev/null || true;sleep 1
}
unbind(){
 for spec in cf_axi_adc:79020000.cf-ad9361-lpc cf_axi_dds:79024000.cf-ad9361-dds-core-lpc dma-axi-dmac:7c400000.dma dma-axi-dmac:7c420000.dma; do
  driver=${spec%%:*};device=${spec#*:};[ ! -L "/sys/bus/platform/drivers/$driver/$device" ] || echo "$device" >"/sys/bus/platform/drivers/$driver/unbind"
 done
}
rebind(){
 for spec in dma-axi-dmac:7c400000.dma dma-axi-dmac:7c420000.dma cf_axi_dds:79024000.cf-ad9361-dds-core-lpc cf_axi_adc:79020000.cf-ad9361-lpc; do
  driver=${spec%%:*};device=${spec#*:};[ -L "/sys/bus/platform/drivers/$driver/$device" ] || echo "$device" >"/sys/bus/platform/drivers/$driver/bind"
 done
}
restore(){
 [ -f "$A/session" ] && [ "$(cat "$A/session")" = "$TOKEN" ] || return 0
 mkdir "$A/restoring" 2>/dev/null || return 0
 state restoring
 stop_native
 unbind
 echo 0 >/sys/class/fpga_manager/fpga0/flags
 echo tetra-fpga-web-restore.bin >/sys/class/fpga_manager/fpga0/firmware
 rebind
 [ "$(cat /sys/class/fpga_manager/fpga0/state)" = operating ]
 cd "$R";rm -f runtime/tetra.pid tetra.pid
 printf '{"receiver":"stopped","mode":"idle","frequency_hz":%s,"crc_ok":0}\n' "$DL" >status.json
 rm -f "$A/session" "$A/multichannel" "$A/test.pid" "$A/request" "$A/profile"
 rmdir "$A/restoring";rmdir "$A"
 state ready
}
case ${1:-} in
 restore) restore;;
 watchdog)
  parent=${3:-0}
  while [ -f "$A/session" ] && [ "$(cat "$A/session")" = "$TOKEN" ]; do
   if ! kill -0 "$parent" 2>/dev/null; then "$D/control.sh" restore "$TOKEN";exit;fi
   sleep 5
  done;;
 run|adopt)
  action=$1
  exec >>"$D/controller.log" 2>&1
  cd "$R"
  sha256sum -c "$D/manifest.sha256"
  [ "$(devmem 0xF8000530 32)" = 0x23727093 ]
  TOKEN="$(date +%s).$$";echo "$TOKEN" >"$A/session";touch "$A/multichannel" "$A/profile"
  trap 'rc=$?;trap - 0 2 15;restore;exit "$rc"' 0
  trap 'exit 1' 2 15
  if [ "$action" = run ]; then
   state validating_profile
   count=$(printf '%s' "$ULS" | awk -F, '{print NF}')
   [ "$count" -ge 1 ] && [ "$count" -le 8 ]
   printf '%s' "$ULS" | awk -F, -v dl="$DL" -v ul="$UL" 'BEGIN{lo=2000000000;hi=0;ok=0} {for(i=1;i<=NF;i++){v=$i+0;if($i!~/^[0-9]+$/||v<70000000||v>1000000000||v%25000||seen[v]++)exit 1;if(v<lo)lo=v;if(v+10000000>hi)hi=v+10000000;if(v==ul&&dl==v+10000000)ok=1}if(!ok||hi-lo>11800000)exit 1}'
   state loading_multi_ul
   echo 12500000 >/sys/bus/iio/devices/iio:device0/in_voltage_sampling_frequency
   echo 12500000 >/sys/bus/iio/devices/iio:device0/out_voltage_sampling_frequency
   interface_clock=$(cat /sys/kernel/debug/clk/spi0.0-pl_intf_clk/clk_rate)
   [ "$interface_clock" -gt 0 ] && [ "$interface_clock" -le 100000000 ]
   unbind
   echo 0 >/sys/class/fpga_manager/fpga0/flags
   echo tetra-profile-persistent.bin >/sys/class/fpga_manager/fpga0/firmware
   [ "$(cat /sys/class/fpga_manager/fpga0/state)" = operating ]
   rebind
   nohup "$D/pluto_tetra_profile" --fpga-dual --seconds 2147483647 --gain "$GAIN" --rx 1 --channels "$ULS" --ul-hz "$UL" --dl-hz "$DL" --lo "$LO" >"$D/receiver.log" 2>&1 </dev/null &
   pid=$!;echo "$pid" >"$A/test.pid";echo "$pid" >runtime/tetra.pid;echo "$pid" >tetra.pid
  else
   pid=$(cat runtime/tetra.pid)
   grep -F -q "$D/pluto_tetra_profile" "/proc/$pid/cmdline"
   [ "$(cat /sys/bus/iio/devices/iio:device0/in_voltage_sampling_frequency)" = 12500000 ]
   echo "$pid" >"$A/test.pid"
  fi
  nohup "$D/control.sh" watchdog "$TOKEN" "$$" >"$D/watchdog.log" 2>&1 </dev/null &
  state receiving_multi_ul
  while :;do
   [ ! -f "$A/request" ] || break
   [ -r "/proc/$pid/cmdline" ] && grep -F -q "$D/pluto_tetra_profile" "/proc/$pid/cmdline" || break
   sleep 1
  done;;
 *) exit 2;;
esac
