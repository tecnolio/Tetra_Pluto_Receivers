#!/bin/sh
set -eu
SRC=/mnt/mmc/tetra-profile
R=/run/pluto_lab
D=$R/fpga-profile
cd "$SRC";sha256sum -c package.sha256
mkdir -p "$D" /lib/firmware
cp "$SRC/pluto_tetra_profile" "$D/pluto_tetra_profile.new"
chmod 755 "$D/pluto_tetra_profile.new";mv "$D/pluto_tetra_profile.new" "$D/pluto_tetra_profile"
cp "$SRC/control.sh" "$D/control.sh"
cp "$SRC/manifest.sha256" "$D/manifest.sha256"
gzip -dc "$SRC/fixed.bin.gz" >/lib/firmware/tetra-profile-persistent.bin
cp "$SRC/dispatch.sh" "$R/fpga-web/control.sh"
chmod 755 "$D/control.sh" "$R/fpga-web/control.sh"
