#!/bin/sh
# Prepare RAM and start the API/TLS only. Never starts reception automatically.
set -eu
P=/mnt/jffs2/pluto_lab;S=/mnt/mmc/tetra-profile;R=/run/pluto_lab
[ ! -e "$R" ] || { echo 'Runtime already exists; inspect its owners first'; exit 1; }
[ -s /mnt/mmc/tetra-networks.tsv ] && [ -s /mnt/mmc/tetra-active-pair.conf ] || { echo 'Configure UL/DL first'; exit 1; }
sh "$P/preflight.sh"
umask 077
mkdir -p "$R/runtime" "$R/fpga-web" /lib/firmware
cp "$S/stock.bin" /lib/firmware/tetra-fpga-web-restore.bin
# Hash manifest is local to this target, including its exact stock rollback image.
gzip -dc "$S/fixed.bin.gz" >/lib/firmware/tetra-profile-persistent.bin
sha256sum "$S/pluto_tetra_profile" | sed "s|$S/|/run/pluto_lab/fpga-profile/|" >"$S/manifest.sha256"
sha256sum /lib/firmware/tetra-profile-persistent.bin /lib/firmware/tetra-fpga-web-restore.bin >>"$S/manifest.sha256"
(cd "$S"; sha256sum pluto_tetra_profile control.sh dispatch.sh manifest.sha256 fixed.bin.gz >package.sha256)
sh "$S/stage.sh"
touch "$R/fpga-web/persistent"
cp "$P/tetra_network_apply.sh" "$R/tetra-network-apply.sh";chmod 755 "$R/tetra-network-apply.sh"
cp -R "$P/web" "$R/web"
cd "$R"
nohup "$P/pluto_http" 8080 >runtime/http.log 2>&1 </dev/null &
echo $! >runtime/http.pid
nohup "$P/pluto_https" -listen :8443 -backend http://127.0.0.1:8080 -cert "$P/server.crt" -key "$P/server.key" -ca "$P/pluto-ca.crt" -extra-services=false -dual-protocol=false >runtime/https.log 2>&1 </dev/null &
echo $! >runtime/https.pid
sleep 1
kill -0 "$(cat runtime/http.pid)";kill -0 "$(cat runtime/https.pid)"
echo 'API available on HTTPS :8443; reception remains stopped.'
