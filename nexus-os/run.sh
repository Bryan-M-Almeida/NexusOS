#!/bin/sh
# Roda o NEXUS.OS no QEMU (precisa de qemu-system-i386)
exec qemu-system-i386 -drive format=raw,file="$(dirname "$0")/nexus.img"
