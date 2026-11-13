#!/bin/sh
# Local installation on a NEW supported target, no FPGA load and no receiver start.
set -eu
cd "$(dirname "$0")/.."
[ "${1:-}" = --new-target ] || { echo 'Usage: sh runtime/install.sh --new-target /absolute/stock-fpga.bin /absolute/credentials-directory'; exit 2; }
STOCK=${2:?Missing original compatible stock FPGA image};CREDS=${3:?Missing credentials directory}
sh runtime/preflight.sh
[ ! -e /mnt/jffs2/pluto_lab ] && [ ! -e /mnt/mmc/tetra-profile ] && [ ! -e /run/pluto_lab ] || { echo 'Existing installation: stop and prepare a migration separately'; exit 1; }
for f in "$STOCK" "$CREDS/web-auth" "$CREDS/server.crt" "$CREDS/server.key" "$CREDS/pluto-ca.crt"; do [ -s "$f" ] || { echo "Missing $f"; exit 1; }; done
sha256sum -c package.sha256
umask 077
P=/mnt/jffs2/pluto_lab;S=/mnt/mmc/tetra-profile
mkdir -p "$P" "$S"
cp backend/pluto_http backend/pluto_https "$P/"
cp runtime/*.sh "$P/"
cp -R web "$P/"
cp "$CREDS/web-auth" "$CREDS/server.crt" "$CREDS/server.key" "$CREDS/pluto-ca.crt" "$P/"
cp "$STOCK" "$S/stock.bin"
cp backend/pluto_tetra_profile "$S/"
cp fpga/fixed.bin.gz "$S/"
cp runtime/tetra_profile_control.sh "$S/control.sh"
cp runtime/dispatch.sh "$S/dispatch.sh"
cp runtime/stage.sh "$S/stage.sh"
chmod 755 "$P"/pluto_* "$P"/*.sh "$S"/*.sh "$S/pluto_tetra_profile"
printf '35\n' >"$S/gain.conf"; printf '0\n' >"$S/lo.conf"
# Authentication/TLS are installed, but nothing is started and boot hooks are untouched.
echo 'Installed. Set a network profile and run start-web.sh manually. No RF changed.'
