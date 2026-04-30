#!/usr/bin/env bash
# Zapada - scripts/make-disk2.sh
#
# Creates a second raw VirtIO disk image used by the multi-device storage smoke.
# Layout:
#   GPT
#   Partition 1: ZAPADA_SMOKE (FAT32, mounted at /mnt/d by /etc/fstab)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(dirname "$SCRIPT_DIR")"
DISK_IMG="${1:-$WORKSPACE_DIR/build/disk2.img}"

echo "Zapada second disk image creation"
echo "  Output: $DISK_IMG"

mkdir -p "$(dirname "$DISK_IMG")"

echo "  Creating blank 64 MiB image..."
dd if=/dev/zero of="$DISK_IMG" bs=1M count=64 status=none

echo "  Creating GPT partition table..."
parted -s "$DISK_IMG" mklabel gpt
parted -s "$DISK_IMG" mkpart ZAPADA_SMOKE fat32 1MiB 63MiB

SMOKE_PART_LBA=2048
SMOKE_PART_SECTORS=126976
SMOKE_FAT_LABEL=ZSMOKE

echo "  Formatting ZAPADA_SMOKE partition (FAT32)..."
TMP_FAT_IMG="$(mktemp)"
cleanup() {
    rm -f "$TMP_FAT_IMG"
}
trap cleanup EXIT
dd if=/dev/zero of="$TMP_FAT_IMG" bs=512 count="$SMOKE_PART_SECTORS" status=none
# FAT volume labels are limited to 11 bytes. The GPT partition name remains
# ZAPADA_SMOKE and is the label used by the managed partition scanner.
mkfs.vfat -F 32 -h "$SMOKE_PART_LBA" -n "$SMOKE_FAT_LABEL" "$TMP_FAT_IMG" > /dev/null 2>&1

HELLO_DLL="$WORKSPACE_DIR/build/hello.dll"
HELLO_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Test.Hello/bin/Release/net10.0/Zapada.Test.Hello.dll"
if [ ! -f "$HELLO_DLL" ] && [ -f "$HELLO_DLL_BIN" ]; then
    HELLO_DLL="$HELLO_DLL_BIN"
fi

echo "  Adding TEST.DLL to ZAPADA_SMOKE partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$HELLO_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "$TMP_FAT_IMG" "$HELLO_DLL" "::TEST.DLL"
        echo "  TEST.DLL written from $(basename "$HELLO_DLL")"
    else
        printf 'TESTDATA' | MTOOLS_SKIP_CHECK=1 mcopy -i "$TMP_FAT_IMG" - "::TEST.DLL"
        echo "  TEST.DLL written as fallback TESTDATA stub"
    fi
else
    echo "  WARNING: mtools not found; TEST.DLL not written to ZAPADA_SMOKE."
fi

dd if="$TMP_FAT_IMG" of="$DISK_IMG" bs=512 seek="$SMOKE_PART_LBA" conv=notrunc status=none

echo "  Second disk image created: $DISK_IMG"
echo "  Done."
