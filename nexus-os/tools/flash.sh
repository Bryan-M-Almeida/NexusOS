#!/bin/sh
# Write nexus.iso to a USB drive (Linux). Usage: sudo tools/flash.sh /dev/sdX
set -e
DEV="$1"
[ -z "$DEV" ] && { echo "usage: sudo tools/flash.sh /dev/sdX   (find the device with: lsblk)"; exit 1; }
[ -b "$DEV" ] || { echo "$DEV is not a block device"; exit 1; }
lsblk "$DEV"
printf "This ERASES everything on %s. Type YES to continue: " "$DEV"; read R
[ "$R" = "YES" ] || { echo cancelled; exit 1; }
dd if="$(dirname "$0")/../nexus.iso" of="$DEV" bs=1M conv=fsync status=progress
sync; echo "Done. Boot from the USB drive in Legacy/CSM (BIOS) mode."
