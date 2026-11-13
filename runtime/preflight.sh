#!/bin/sh
# Read-only checks for the supported reference hardware. Does not claim ownership.
set -eu
[ "$(id -u)" = 0 ] || { echo 'Run as root'; exit 1; }
[ "$(uname -m)" = armv7l ] || { echo 'Expected ARMv7'; exit 1; }
case "$(uname -r)" in 5.15*) ;; *) echo 'Only the reference Linux 5.15 driver layout is supported'; exit 1;; esac
[ "$(devmem 0xF8000530 32)" = 0x23727093 ] || { echo 'Unsupported chip ID: requires reference XC7Z020 revision'; exit 1; }
for path in /lib/ld-linux-armhf.so.3 /sys/class/fpga_manager/fpga0/firmware /sys/bus/platform/drivers/cf_axi_adc/79020000.cf-ad9361-lpc /sys/bus/platform/drivers/cf_axi_dds/79024000.cf-ad9361-dds-core-lpc /sys/bus/platform/drivers/dma-axi-dmac/7c400000.dma /sys/bus/platform/drivers/dma-axi-dmac/7c420000.dma /sys/bus/iio/devices/iio:device0/in_voltage_sampling_frequency /sys/kernel/debug/clk/spi0.0-pl_intf_clk/clk_rate; do
 [ -e "$path" ] || { echo "Missing reference path: $path"; exit 1; }
done
awk '$2=="/mnt/mmc" {ok=1} END{exit !ok}' /proc/mounts || { echo 'Mount the SD card at /mnt/mmc first'; exit 1; }
# Refuse a target whose IIO device is currently open. Never terminate its owner.
for f in /proc/[0-9]*/fd/*; do
 target=$(readlink "$f" 2>/dev/null || true)
 case "$target" in /dev/iio:device*) echo "SDR busy: $f -> $target"; exit 1;; esac
done
printf 'Reference checks passed; kernel=%s, FPGA=%s. This does not verify the board wiring or stock image.\n' "$(uname -r)" "$(cat /sys/class/fpga_manager/fpga0/state)"
