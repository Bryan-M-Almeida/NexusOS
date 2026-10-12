set -e
DEV="$1"
[ -z "$DEV" ] && { echo "uso: sudo ./flash.sh /dev/sdX   (veja o nome certo com: lsblk)"; exit 1; }
[ -b "$DEV" ] || { echo "$DEV nao e um dispositivo de bloco"; exit 1; }
lsblk "$DEV"
printf "Isso APAGA TUDO em %s. Digite SIM para continuar: " "$DEV"; read R
[ "$R" = "SIM" ] || { echo cancelado; exit 1; }
dd if="$(dirname "$0")/nexus.img" of="$DEV" bs=1M conv=fsync status=progress
sync; echo "Pronto. Dê boot pelo pendrive em modo Legacy/CSM (BIOS)."
