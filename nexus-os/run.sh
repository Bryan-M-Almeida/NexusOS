#!/bin/sh
# Run NEXUS.OS in QEMU (requires qemu-system-i386)
exec qemu-system-i386 -drive format=raw,file="$(dirname "$0")/nexus.img"
