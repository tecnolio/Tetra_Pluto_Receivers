#!/bin/sh
set -eu
cd /run/pluto_lab
LOCK=runtime/tetra-network-apply
PAIR=/mnt/mmc/tetra-active-pair.conf
STATE=runtime/tetra-network-apply.json
exec >>runtime/tetra-network-apply.log 2>&1
state() { printf '{"state":"%s"}\n' "$1" >"$STATE.new"; mv "$STATE.new" "$STATE"; }
get() {
 auth=$(cat /mnt/jffs2/pluto_lab/web-auth) || return 1
 [ -n "$auth" ] || return 1
 wget -q -T 15 --header "Authorization: Basic $auth" -O "$2" "http://127.0.0.1:8080$1"
}
release() { rm -f "$LOCK/previous.conf" "$LOCK/gain" "$LOCK/cancel" "$LOCK/status.json" "$LOCK/response.json"; rmdir "$LOCK"; }
cancelled() { [ -f "$LOCK/cancel" ]; }
idle() { get /api/system/status "$LOCK/status.json"; grep -q '"rf_owner":"idle"' "$LOCK/status.json"; }
finish() {
 rc=$?
 trap - 0 2 15
 if [ "$rc" -ne 0 ]; then
  if cancelled; then state cancelled
  else
   state failed_restoring_previous
   cp "$LOCK/previous.conf" "$PAIR.new" && mv "$PAIR.new" "$PAIR"
   # Only stop this helper's failed attempt, never a different receiver.
   if [ -n "${started_pid:-}" ] && [ "$(cat runtime/tetra.pid 2>/dev/null)" = "$started_pid" ]; then
    get '/api/tetra/stop?profile_apply=1' "$LOCK/response.json" || true
    for i in $(seq 1 30); do [ -d runtime/fpga-web-active ] || break; sleep 1; done
   fi
   if idle; then
    cp "$LOCK/gain" fpga-web/next-gain
    get '/api/tetra/start?profile_apply=1' "$LOCK/response.json" || true
   fi
   state failed_previous_configuration_restored
  fi
 fi
 release
 exit "$rc"
}
trap finish 0
trap 'exit 1' 2 15
read ul dl profile <"$PAIR"
state stopping
get '/api/tetra/stop?profile_apply=1' "$LOCK/response.json"
for i in $(seq 1 45); do
 cancelled && exit 1
 [ -d runtime/fpga-web-active ] || break
 sleep 1
done
[ ! -d runtime/fpga-web-active ]
cancelled && exit 1
idle
cp "$LOCK/gain" fpga-web/next-gain
state starting
get '/api/tetra/start?profile_apply=1' "$LOCK/response.json"
started_pid=$(cat runtime/tetra.pid 2>/dev/null || true)
for i in $(seq 1 45); do
 cancelled && exit 1
 if [ -f runtime/tetra.pid ]; then started_pid=$(cat runtime/tetra.pid); fi
 if grep -q '"receiver":"receiving"' status.json && grep -q '"mode":"live-ul-dl-fpga"' status.json && grep -q "\"channels_hz\":\[$ul,$dl\]" status.json; then
  state applied
  exit 0
 fi
 sleep 1
done
exit 1
