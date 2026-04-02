#!/usr/bin/env bash
# Zapada - scripts/make-disk.sh
#
# Creates the Zapada disk image (build/disk.img) in WSL.
#
# Disk layout:
#   Sector 0 (LBA 0):    Protective MBR + ZAPADASK signature at bytes 0-7
#   Sector 1 (LBA 1):    GPT header
#   Sectors 2-33:        GPT partition entries
#   Sectors 2048-:       ZAPADA_BOOT partition  (FAT32, 32 MiB)
#   Sectors 67584-:      ZAPADA_SYS  partition  (FAT32, 48 MiB)
#   Sectors 165888-:     ZAPADA_DATA partition  (ext4, 16 MiB)
#   Last 33 sectors:     GPT backup header
#
# Signature at LBA 0 offset 0-7:
#   "ZAPADASK" = 0x43 0x59 0x4C 0x49 0x58 0x44 0x53 0x4B
#
# The Phase 3A gate check for Part 2 reads LBA 0 and verifies the first 8
# bytes equal 0x43594C495844534B (big-endian packed).
#
# TEST.DLL is written to the ZAPADA_BOOT FAT32 partition for the Phase 3A
# Part 3 gate check.  The managed BootLoader.cs reads this file and prints
# "[Boot] found: TEST.DLL" and "[Gate] GateD" to the serial log.
#
# Prerequisites (install once):
#   sudo apt install parted dosfstools mtools e2fsprogs
#
# Usage from WSL:
#   bash scripts/make-disk.sh [OUTPUT_PATH]
#   (defaults to build/disk.img relative to workspace root)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(dirname "$SCRIPT_DIR")"
DISK_IMG="${1:-$WORKSPACE_DIR/build/disk.img}"
DISK_IMG_WIN="$(wslpath -w "$DISK_IMG" 2>/dev/null || echo "$DISK_IMG")"

echo "Zapada disk image creation"
echo "  Output: $DISK_IMG"

# Ensure build directory exists
mkdir -p "$(dirname "$DISK_IMG")"

# Create a raw disk image of 100 MiB (204800 512-byte sectors)
echo "  Creating blank 100 MiB image..."
dd if=/dev/zero of="$DISK_IMG" bs=1M count=100 status=none

# Create GPT partition table
echo "  Creating GPT partition table..."
parted -s "$DISK_IMG" mklabel gpt

# Partition 1: ZAPADA_BOOT (FAT32, 1 MiB – 33 MiB)
echo "  Creating ZAPADA_BOOT partition (FAT32, 32 MiB)..."
parted -s "$DISK_IMG" mkpart ZAPADA_BOOT fat32 1MiB 33MiB

# Partition 2: ZAPADA_SYS (FAT32, 33 MiB – 81 MiB)
echo "  Creating ZAPADA_SYS partition (FAT32, 48 MiB)..."
parted -s "$DISK_IMG" mkpart ZAPADA_SYS fat32 33MiB 81MiB

# Partition 3: ZAPADA_DATA (ext4, 81 MiB – 97 MiB)
echo "  Creating ZAPADA_DATA partition (ext4, 16 MiB)..."
parted -s "$DISK_IMG" mkpart ZAPADA_DATA ext4 81MiB 97MiB

# Format both partitions in-place inside the raw image. Avoid loop devices so the
# script works in unprivileged WSL environments where losetup is not permitted.
echo "  Formatting partitions as FAT32..."
BOOT_PART_LBA=2048
SYS_PART_LBA=67584
DATA_PART_LBA=165888
DATA_PART_SECTORS=32768
mkfs.vfat -F 32 --offset="$BOOT_PART_LBA" -h "$BOOT_PART_LBA" -n ZAPADA_BOOT "$DISK_IMG" > /dev/null 2>&1
mkfs.vfat -F 32 --offset="$SYS_PART_LBA"  -h "$SYS_PART_LBA"  -n ZAPADA_SYS  "$DISK_IMG" > /dev/null 2>&1

echo "  Formatting ZAPADA_DATA as ext4 with a dummy file..."
TMP_EXT4_IMG="$(mktemp)"
TMP_DUMMY_FILE="$(mktemp)"
cleanup() {
    rm -f "$TMP_EXT4_IMG" "$TMP_DUMMY_FILE"
}
trap cleanup EXIT
dd if=/dev/zero of="$TMP_EXT4_IMG" bs=512 count="$DATA_PART_SECTORS" status=none
mkfs.ext4 -F -L ZAPADA_DATA "$TMP_EXT4_IMG" > /dev/null 2>&1
printf 'Zapada ext4 dummy payload\n' > "$TMP_DUMMY_FILE"
debugfs -w -R "write $TMP_DUMMY_FILE /dummy.txt" "$TMP_EXT4_IMG" > /dev/null 2>&1
dd if="$TMP_EXT4_IMG" of="$DISK_IMG" bs=512 seek="$DATA_PART_LBA" conv=notrunc status=none

# Add TEST.DLL to ZAPADA_BOOT for the Phase 3A Part 3 + Phase 3B Step 4 gate checks.
# Uses mtools mcopy with the @@offset syntax to write directly to the image file
# without re-mounting.  ZAPADA_BOOT starts at LBA 2048 = byte offset 1048576.
#
# Priority:
#   1. If build/hello.dll exists (built by build.ps1 or test-all.ps1 after make), copy it
#      as TEST.DLL so the Phase 3B Step 4 end-to-end gate can execute Run() via the
#      managed runtime and emit [Boot] TEST.DLL loaded, [Boot] Zapada.Test.Hello loaded,
#      and [Gate] Phase3B.
#   2. Fallback: src/managed/Zapada.Test.Hello/bin/Release/net9.0/Zapada.Test.Hello.dll
#      exists when dotnet build/publish has run at least once.  This path is outside
#      build/ so it survives make clean.
#   3. Final fallback: 8-byte TESTDATA stub, which satisfies only the Phase 3A Part 3
#      gate check ([Boot] found: TEST.DLL / [Gate] GateD).
BOOT_PART_OFFSET=$((BOOT_PART_LBA * 512))
HELLO_DLL="$WORKSPACE_DIR/build/hello.dll"
HELLO_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Test.Hello/bin/Release/net9.0/Zapada.Test.Hello.dll"
# Resolve: prefer staged build/hello.dll, then bin/ output, then stub
if [ ! -f "$HELLO_DLL" ] && [ -f "$HELLO_DLL_BIN" ]; then
    HELLO_DLL="$HELLO_DLL_BIN"
fi
echo "  Adding TEST.DLL to ZAPADA_BOOT partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$HELLO_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${BOOT_PART_OFFSET}" "$HELLO_DLL" "::TEST.DLL"
        echo "  TEST.DLL written from $(basename "$HELLO_DLL") (Phase 3B Step 4 payload)"
    else
        printf 'TESTDATA' | MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${BOOT_PART_OFFSET}" - "::TEST.DLL"
        echo "  TEST.DLL written (8 bytes: TESTDATA stub — Zapada.Test.Hello.dll not found)"
    fi
else
    echo "  WARNING: mtools not found; TEST.DLL not written to ZAPADA_BOOT."
    echo "  Phase 3A Part 3 gate check [Gate] GateD will not pass until mtools is installed."
    echo "  Install mtools: sudo apt install mtools"
fi

# Add VBLK.DLL to ZAPADA_BOOT for the Phase 3.1 D.1 gate check.
# Priority:
#   1. build/vblk.dll (staged by build.ps1 dotnet publish)
#   2. src/managed/Zapada.Drivers.VirtioBlock/bin/Release/net9.0/Zapada.Drivers.VirtioBlock.dll
#   3. If neither exists, skip with a warning (Phase 3.1 gate will not pass).
VBLK_DLL="$WORKSPACE_DIR/build/vblk.dll"
VBLK_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Drivers.VirtioBlock/bin/Release/net9.0/Zapada.Drivers.VirtioBlock.dll"
if [ ! -f "$VBLK_DLL" ] && [ -f "$VBLK_DLL_BIN" ]; then
    VBLK_DLL="$VBLK_DLL_BIN"
fi
echo "  Adding VBLK.DLL to ZAPADA_BOOT partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$VBLK_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${BOOT_PART_OFFSET}" "$VBLK_DLL" "::VBLK.DLL"
        echo "  VBLK.DLL written from $(basename "$VBLK_DLL") (Phase 3.1 D.1 payload)"
    else
        echo "  WARNING: VBLK.DLL not found; Phase31-D1 gate will not pass."
        echo "  Run: dotnet publish src/managed/Zapada.Drivers.VirtioBlock/..."
    fi
else
    echo "  WARNING: mtools not found; VBLK.DLL not written to ZAPADA_BOOT."
fi

# Add GPT.DLL to ZAPADA_BOOT for the Phase 3.1 D.2 gate check.
# Priority:
#   1. build/gpt.dll (staged by build.ps1 dotnet publish)
#   2. src/managed/Zapada.Fs.Gpt/bin/Release/net9.0/Zapada.Fs.Gpt.dll
#   3. If neither exists, skip with a warning (Phase 3.1 D.2 gate will not pass).
GPT_DLL="$WORKSPACE_DIR/build/gpt.dll"
GPT_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Fs.Gpt/bin/Release/net9.0/Zapada.Fs.Gpt.dll"
if [ ! -f "$GPT_DLL" ] && [ -f "$GPT_DLL_BIN" ]; then
    GPT_DLL="$GPT_DLL_BIN"
fi
echo "  Adding GPT.DLL to ZAPADA_BOOT partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$GPT_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${BOOT_PART_OFFSET}" "$GPT_DLL" "::GPT.DLL"
        echo "  GPT.DLL written from $(basename "$GPT_DLL") (Phase 3.1 D.2 payload)"
    else
        echo "  WARNING: GPT.DLL not found; Phase31-D2 gate will not pass."
        echo "  Run: dotnet publish src/managed/Zapada.Fs.Gpt/..."
    fi
else
    echo "  WARNING: mtools not found; GPT.DLL not written to ZAPADA_BOOT."
fi

# Add STORAGE.DLL to ZAPADA_BOOT for the Phase 2B storage abstractions + RamFs driver.
# Priority:
#   1. build/storage.dll (staged by build.ps1 dotnet publish)
#   2. src/managed/Zapada.Storage/bin/Release/net10.0/Zapada.Storage.dll
#   3. If neither exists, skip with a warning.
STORAGE_DLL="$WORKSPACE_DIR/build/storage.dll"
STORAGE_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Storage/bin/Release/net10.0/Zapada.Storage.dll"
if [ ! -f "$STORAGE_DLL" ] && [ -f "$STORAGE_DLL_BIN" ]; then
    STORAGE_DLL="$STORAGE_DLL_BIN"
fi
echo "  Adding STORAGE.DLL to ZAPADA_BOOT partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$STORAGE_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${BOOT_PART_OFFSET}" "$STORAGE_DLL" "::STORAGE.DLL"
        echo "  STORAGE.DLL written from $(basename "$STORAGE_DLL") (Phase 2B storage payload)"
    else
        echo "  WARNING: STORAGE.DLL not found; Phase-Storage gate will not pass."
        echo "  Run: dotnet publish src/managed/Zapada.Storage/..."
    fi
else
    echo "  WARNING: mtools not found; STORAGE.DLL not written to ZAPADA_BOOT."
fi

# Add FAT32.DLL to ZAPADA_BOOT for the Phase 3.1 D.3 gate check.
# Priority:
#   1. build/fat32.dll (staged by build.ps1 dotnet publish)
#   2. src/managed/Zapada.Fs.Fat32/bin/Release/net9.0/Zapada.Fs.Fat32.dll
#   3. If neither exists, skip with a warning (Phase 3.1 D.3 gate will not pass).
FAT32_DLL="$WORKSPACE_DIR/build/fat32.dll"
FAT32_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Fs.Fat32/bin/Release/net9.0/Zapada.Fs.Fat32.dll"
if [ ! -f "$FAT32_DLL" ] && [ -f "$FAT32_DLL_BIN" ]; then
    FAT32_DLL="$FAT32_DLL_BIN"
fi
echo "  Adding FAT32.DLL to ZAPADA_BOOT partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$FAT32_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${BOOT_PART_OFFSET}" "$FAT32_DLL" "::FAT32.DLL"
        echo "  FAT32.DLL written from $(basename "$FAT32_DLL") (Phase 3.1 D.3 payload)"
    else
        echo "  WARNING: FAT32.DLL not found; Phase31-D3 gate will not pass."
        echo "  Run: dotnet publish src/managed/Zapada.Fs.Fat32/..."
    fi
else
    echo "  WARNING: mtools not found; FAT32.DLL not written to ZAPADA_BOOT."
fi

# Add VFS.DLL to ZAPADA_BOOT for the Phase 3.1 D.4 gate check.
# Priority:
#   1. build/vfs.dll (staged by build.ps1 dotnet publish)
#   2. src/managed/Zapada.Fs.Vfs/bin/Release/net9.0/Zapada.Fs.Vfs.dll
#   3. If neither exists, skip with a warning (Phase 3.1 D.4 gate will not pass).
VFS_DLL="$WORKSPACE_DIR/build/vfs.dll"
VFS_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Fs.Vfs/bin/Release/net9.0/Zapada.Fs.Vfs.dll"
if [ ! -f "$VFS_DLL" ] && [ -f "$VFS_DLL_BIN" ]; then
    VFS_DLL="$VFS_DLL_BIN"
fi
echo "  Adding VFS.DLL to ZAPADA_BOOT partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$VFS_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${BOOT_PART_OFFSET}" "$VFS_DLL" "::VFS.DLL"
        echo "  VFS.DLL written from $(basename "$VFS_DLL") (Phase 3.1 D.4 payload)"
    else
        echo "  WARNING: VFS.DLL not found; Phase31-D4 gate will not pass."
        echo "  Run: dotnet publish src/managed/Zapada.Fs.Vfs/..."
    fi
else
    echo "  WARNING: mtools not found; VFS.DLL not written to ZAPADA_BOOT."
fi

# Add CONF.DLL to ZAPADA_BOOT for the Phase 3.2 conformance gate check.
# Priority:
#   1. build/conf.dll (staged by build.ps1 dotnet publish)
#   2. src/managed/Zapada.Conformance/bin/Release/net10.0/Zapada.Conformance.dll
#   3. If neither exists, skip with a warning (Phase 3.2 gate will not pass).
CONF_DLL="$WORKSPACE_DIR/build/conf.dll"
CONF_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Conformance/bin/Release/net10.0/Zapada.Conformance.dll"
if [ ! -f "$CONF_DLL" ] && [ -f "$CONF_DLL_BIN" ]; then
    CONF_DLL="$CONF_DLL_BIN"
fi
echo "  Adding CONF.DLL to ZAPADA_BOOT partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$CONF_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${BOOT_PART_OFFSET}" "$CONF_DLL" "::CONF.DLL"
        MTOOLS_SKIP_CHECK=1 mmd -i "${DISK_IMG}@@${BOOT_PART_OFFSET}" "::SYS" 2>/dev/null || true
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${BOOT_PART_OFFSET}" "$CONF_DLL" "::SYS/CONF.DLL"
        echo "  CONF.DLL written from $(basename "$CONF_DLL") (Phase 3.2 conformance payload)"
    else
        echo "  WARNING: CONF.DLL not found; Phase3.2-Conf gate will not pass."
        echo "  Run: dotnet publish src/managed/Zapada.Conformance/..."
    fi
else
    echo "  WARNING: mtools not found; CONF.DLL not written to ZAPADA_BOOT."
fi

# Add the cross-assembly conformance dependency so disk-based loading paths can
# exercise the same two-assembly CLR behavior as initramfs-based bring-up.
CROSSASM_DLL="$WORKSPACE_DIR/build/conf-crossasm.dll"
CROSSASM_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Conformance.CrossAsm/bin/Release/net10.0/Zapada.Conformance.CrossAsm.dll"
if [ ! -f "$CROSSASM_DLL" ] && [ -f "$CROSSASM_DLL_BIN" ]; then
    CROSSASM_DLL="$CROSSASM_DLL_BIN"
fi
echo "  Adding XCONF.DLL to ZAPADA_BOOT partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$CROSSASM_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${BOOT_PART_OFFSET}" "$CROSSASM_DLL" "::XCONF.DLL"
        echo "  XCONF.DLL written from $(basename "$CROSSASM_DLL") (cross-assembly conformance payload)"
    else
        echo "  WARNING: XCONF.DLL not found; cross-assembly disk payload will be absent."
        echo "  Run: dotnet publish src/managed/Zapada.Conformance.CrossAsm/..."
    fi
else
    echo "  WARNING: mtools not found; XCONF.DLL not written to ZAPADA_BOOT."
fi

# Write ZAPADASK signature at bytes 0-7 of sector 0.
# The protective MBR bootstrap code area (bytes 0-439) is unused by UEFI.
# Writing our signature here does not invalidate the GPT or MBR boot record.
echo "  Writing ZAPADASK signature at sector 0 offset 0..."
printf '\x43\x59\x4C\x49\x58\x44\x53\x4B' | dd of="$DISK_IMG" bs=1 count=8 conv=notrunc status=none

echo "  Disk image created: $DISK_IMG"
echo "  Signature: 43594C495844534B (ZAPADASK)"
echo "  Done."

