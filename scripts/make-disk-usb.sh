#!/usr/bin/env bash
# Zapada - scripts/make-disk-usb.sh
#
# Creates a raw USB mass-storage image used by the xHCI/USB storage smoke.
# Layout:
#   GPT
#   Partition 1: ZAPADA_USB (FAT32, mounted at /mnt/u by /etc/fstab)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(dirname "$SCRIPT_DIR")"
DISK_IMG="${1:-$WORKSPACE_DIR/build/disk-usb.img}"

echo "Zapada USB storage image creation"
echo "  Output: $DISK_IMG"

mkdir -p "$(dirname "$DISK_IMG")"

echo "  Creating blank 64 MiB image..."
dd if=/dev/zero of="$DISK_IMG" bs=1M count=64 status=none

echo "  Creating GPT partition table..."
parted -s "$DISK_IMG" mklabel gpt
parted -s "$DISK_IMG" mkpart ZAPADA_USB fat32 1MiB 63MiB

USB_PART_LBA=2048
USB_PART_SECTORS=126976
USB_FAT_LABEL=ZAPADA_USB

echo "  Formatting ZAPADA_USB partition (FAT32)..."
TMP_FAT_IMG="$(mktemp)"
cleanup() {
    rm -f "$TMP_FAT_IMG"
}
trap cleanup EXIT
dd if=/dev/zero of="$TMP_FAT_IMG" bs=512 count="$USB_PART_SECTORS" status=none
mkfs.vfat -F 32 -h "$USB_PART_LBA" -n "$USB_FAT_LABEL" "$TMP_FAT_IMG" > /dev/null 2>&1

HELLO_DLL="$WORKSPACE_DIR/build/hello.dll"
HELLO_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Test.Hello/bin/Release/net10.0/Zapada.Test.Hello.dll"
if [ ! -f "$HELLO_DLL" ] && [ -f "$HELLO_DLL_BIN" ]; then
    HELLO_DLL="$HELLO_DLL_BIN"
fi

echo "  Adding TEST.DLL to ZAPADA_USB partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$HELLO_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "$TMP_FAT_IMG" "$HELLO_DLL" "::TEST.DLL"
        echo "  TEST.DLL written from $(basename "$HELLO_DLL")"
    else
        printf 'USBTEST' | MTOOLS_SKIP_CHECK=1 mcopy -i "$TMP_FAT_IMG" - "::TEST.DLL"
        echo "  TEST.DLL written as fallback USBTEST stub"
    fi
else
    echo "  WARNING: mtools not found; TEST.DLL not written to ZAPADA_USB."
fi

dd if="$TMP_FAT_IMG" of="$DISK_IMG" bs=512 seek="$USB_PART_LBA" conv=notrunc status=none

echo "  USB storage image created: $DISK_IMG"
echo "  Done."
