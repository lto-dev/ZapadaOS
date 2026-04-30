#!/usr/bin/env bash
# Zapada - scripts/make-disk.sh
#
# Creates the Zapada disk image (build/disk.img) in WSL.
#
# Disk layout:
#   Sector 0 (LBA 0):    Protective MBR + ZAPADASK signature at bytes 0-7
#   Sector 1 (LBA 1):    GPT header
#   Sectors 2-33:        GPT partition entries
#   Sectors 2048-:       ZAPADA_BOOT partition  (ext4, 64 MiB, root/boot)
#   Sectors 133120-:     ZAPADA_DATA partition  (FAT32, 32 MiB, compatibility)
#   Last 33 sectors:     GPT backup header
#
# Signature at LBA 0 offset 0-7:
#   "ZAPADASK" = 0x43 0x59 0x4C 0x49 0x58 0x44 0x53 0x4B
#
# The Phase 3A gate check for Part 2 reads LBA 0 and verifies the first 8
# bytes equal 0x43594C495844534B (big-endian packed).
#
# Managed payloads are written to the ZAPADA_BOOT Ext4 root/boot partition.  The
# FAT32 ZAPADA_DATA partition remains populated for compatibility and is mounted at
# /mnt/c by /etc/fstab.
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

# Partition 1: ZAPADA_BOOT (ext4, 1 MiB – 65 MiB)
echo "  Creating ZAPADA_BOOT partition (ext4, 64 MiB)..."
parted -s "$DISK_IMG" mkpart ZAPADA_BOOT ext4 1MiB 65MiB

# Partition 2: ZAPADA_DATA (FAT32, 65 MiB – 97 MiB)
echo "  Creating ZAPADA_DATA partition (FAT32, 32 MiB)..."
parted -s "$DISK_IMG" mkpart ZAPADA_DATA fat32 65MiB 97MiB

# Format both partitions in-place inside the raw image. Avoid loop devices so the
# script works in unprivileged WSL environments where losetup is not permitted.
echo "  Formatting compatibility FAT32 partition..."
ROOT_PART_LBA=2048
ROOT_PART_SECTORS=131072
DATA_PART_LBA=133120
DATA_PART_SECTORS=65536
mkfs.vfat -F 32 --offset="$DATA_PART_LBA" -h "$DATA_PART_LBA" -n ZAPADA_DATA "$DISK_IMG" > /dev/null 2>&1

DATA_PART_OFFSET=$((DATA_PART_LBA * 512))

HELLO_DLL="$WORKSPACE_DIR/build/hello.dll"
HELLO_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Test.Hello/bin/Release/net10.0/Zapada.Test.Hello.dll"
if [ ! -f "$HELLO_DLL" ] && [ -f "$HELLO_DLL_BIN" ]; then
    HELLO_DLL="$HELLO_DLL_BIN"
fi

VBLK_DLL="$WORKSPACE_DIR/build/vblk.dll"
VBLK_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Drivers.VirtioBlock/bin/Release/net10.0/Zapada.Drivers.VirtioBlock.dll"
if [ ! -f "$VBLK_DLL" ] && [ -f "$VBLK_DLL_BIN" ]; then
    VBLK_DLL="$VBLK_DLL_BIN"
fi

DRIVERS_DLL="$WORKSPACE_DIR/build/drivers.dll"
DRIVERS_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Drivers/bin/Release/net10.0/Zapada.Drivers.dll"
if [ ! -f "$DRIVERS_DLL" ] && [ -f "$DRIVERS_DLL_BIN" ]; then
    DRIVERS_DLL="$DRIVERS_DLL_BIN"
fi

GPT_DLL="$WORKSPACE_DIR/build/gpt.dll"
GPT_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Fs.Gpt/bin/Release/net10.0/Zapada.Fs.Gpt.dll"
if [ ! -f "$GPT_DLL" ] && [ -f "$GPT_DLL_BIN" ]; then
    GPT_DLL="$GPT_DLL_BIN"
fi

STORAGE_DLL="$WORKSPACE_DIR/build/storage.dll"
STORAGE_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Storage/bin/Release/net10.0/Zapada.Storage.dll"
if [ ! -f "$STORAGE_DLL" ] && [ -f "$STORAGE_DLL_BIN" ]; then
    STORAGE_DLL="$STORAGE_DLL_BIN"
fi

FAT32_DLL="$WORKSPACE_DIR/build/fat32.dll"
FAT32_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Fs.Fat32/bin/Release/net10.0/Zapada.Fs.Fat32.dll"
if [ ! -f "$FAT32_DLL" ] && [ -f "$FAT32_DLL_BIN" ]; then
    FAT32_DLL="$FAT32_DLL_BIN"
fi

EXT_DLL="$WORKSPACE_DIR/build/ext.dll"
EXT_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Fs.Ext/bin/Release/net10.0/Zapada.Fs.Ext.dll"
if [ ! -f "$EXT_DLL" ] && [ -f "$EXT_DLL_BIN" ]; then
    EXT_DLL="$EXT_DLL_BIN"
fi

EXT4_DLL="$WORKSPACE_DIR/build/ext4.dll"
EXT4_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Fs.Ext4/bin/Release/net10.0/Zapada.Fs.Ext4.dll"
if [ ! -f "$EXT4_DLL" ] && [ -f "$EXT4_DLL_BIN" ]; then
    EXT4_DLL="$EXT4_DLL_BIN"
fi

VFS_DLL="$WORKSPACE_DIR/build/vfs.dll"
VFS_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Fs.Vfs/bin/Release/net10.0/Zapada.Fs.Vfs.dll"
if [ ! -f "$VFS_DLL" ] && [ -f "$VFS_DLL_BIN" ]; then
    VFS_DLL="$VFS_DLL_BIN"
fi

SHELL_DLL="$WORKSPACE_DIR/build/shell.dll"
SHELL_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Shell/bin/Release/net10.0/Zapada.Shell.dll"
if [ ! -f "$SHELL_DLL" ] && [ -f "$SHELL_DLL_BIN" ]; then
    SHELL_DLL="$SHELL_DLL_BIN"
fi

BOOT_DLL="$WORKSPACE_DIR/build/boot.dll"
BOOT_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Boot/bin/Release/net10.0/Zapada.Boot.dll"
if [ ! -f "$BOOT_DLL" ] && [ -f "$BOOT_DLL_BIN" ]; then
    BOOT_DLL="$BOOT_DLL_BIN"
fi

CONSOLE_DLL="$WORKSPACE_DIR/build/System.Console.dll"
CONSOLE_DLL_BIN="$WORKSPACE_DIR/src/managed/System.Console/bin/Release/net10.0/System.Console.dll"
if [ ! -f "$CONSOLE_DLL" ] && [ -f "$CONSOLE_DLL_BIN" ]; then
    CONSOLE_DLL="$CONSOLE_DLL_BIN"
fi

CONF_DLL="$WORKSPACE_DIR/build/conf.dll"
CONF_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Conformance/bin/Release/net10.0/Zapada.Conformance.dll"
if [ ! -f "$CONF_DLL" ] && [ -f "$CONF_DLL_BIN" ]; then
    CONF_DLL="$CONF_DLL_BIN"
fi

CROSSASM_DLL="$WORKSPACE_DIR/build/conf-crossasm.dll"
CROSSASM_DLL_BIN="$WORKSPACE_DIR/src/managed/Zapada.Conformance.CrossAsm/bin/Release/net10.0/Zapada.Conformance.CrossAsm.dll"
if [ ! -f "$CROSSASM_DLL" ] && [ -f "$CROSSASM_DLL_BIN" ]; then
    CROSSASM_DLL="$CROSSASM_DLL_BIN"
fi

echo "  Formatting ZAPADA_BOOT as ext4 with Linux-style root files..."
TMP_EXT4_IMG="$(mktemp)"
TMP_DUMMY_FILE="$(mktemp)"
TMP_FSTAB_FILE="$(mktemp)"
TMP_MOTD_FILE="$(mktemp)"
cleanup() {
    rm -f "$TMP_EXT4_IMG" "$TMP_DUMMY_FILE" "$TMP_FSTAB_FILE" "$TMP_MOTD_FILE"
}
trap cleanup EXIT
dd if=/dev/zero of="$TMP_EXT4_IMG" bs=512 count="$ROOT_PART_SECTORS" status=none
mkfs.ext4 -F -L ZAPADA_BOOT "$TMP_EXT4_IMG" > /dev/null 2>&1
printf 'Zapada ext4 dummy payload\n' > "$TMP_DUMMY_FILE"
cat > "$TMP_FSTAB_FILE" <<'FSTAB'
# Zapada filesystem table
# <device>      <mountpoint> <type>  <options>
LABEL=ZAPADA_BOOT /           ext4    ro
LABEL=ZAPADA_DATA /mnt/c      vfat    ro
LABEL=ZAPADA_SMOKE /mnt/d     vfat    ro
FSTAB
cat > "$TMP_MOTD_FILE" <<'MOTD'
Welcome to Zapada.
Ext4 is mounted as /, and FAT32 compatibility is mounted at /mnt/c.
Type help for shell commands.
MOTD
debugfs -w -R "mkdir /etc" "$TMP_EXT4_IMG" > /dev/null 2>&1 || true
debugfs -w -R "mkdir /mnt" "$TMP_EXT4_IMG" > /dev/null 2>&1 || true
debugfs -w -R "mkdir /mnt/c" "$TMP_EXT4_IMG" > /dev/null 2>&1 || true
debugfs -w -R "write $TMP_DUMMY_FILE /dummy.txt" "$TMP_EXT4_IMG" > /dev/null 2>&1
debugfs -w -R "write $TMP_FSTAB_FILE /etc/fstab" "$TMP_EXT4_IMG" > /dev/null 2>&1
debugfs -w -R "write $TMP_MOTD_FILE /etc/motd" "$TMP_EXT4_IMG" > /dev/null 2>&1

write_ext4_payload() {
    local source_path="$1"
    local target_path="$2"
    local label="$3"
    if [ -f "$source_path" ]; then
        debugfs -w -R "write $source_path $target_path" "$TMP_EXT4_IMG" > /dev/null 2>&1
        echo "  $label written to Ext4 root as $target_path"
    else
        echo "  WARNING: $label not found; $target_path absent from Ext4 root."
    fi
}

echo "  Adding managed payloads to ZAPADA_BOOT Ext4 root partition..."
write_ext4_payload "$BOOT_DLL" "/Zapada.Boot.dll" "Zapada.Boot.dll"
write_ext4_payload "$HELLO_DLL" "/Zapada.Test.Hello.dll" "Zapada.Test.Hello.dll"
write_ext4_payload "$DRIVERS_DLL" "/Zapada.Drivers.dll" "Zapada.Drivers.dll"
write_ext4_payload "$VBLK_DLL" "/Zapada.Drivers.VirtioBlock.dll" "Zapada.Drivers.VirtioBlock.dll"
write_ext4_payload "$GPT_DLL" "/Zapada.Fs.Gpt.dll" "Zapada.Fs.Gpt.dll"
write_ext4_payload "$FAT32_DLL" "/Zapada.Fs.Fat32.dll" "Zapada.Fs.Fat32.dll"
write_ext4_payload "$EXT_DLL" "/Zapada.Fs.Ext.dll" "Zapada.Fs.Ext.dll"
write_ext4_payload "$EXT4_DLL" "/Zapada.Fs.Ext4.dll" "Zapada.Fs.Ext4.dll"
write_ext4_payload "$STORAGE_DLL" "/Zapada.Storage.dll" "Zapada.Storage.dll"
write_ext4_payload "$VFS_DLL" "/Zapada.Fs.Vfs.dll" "Zapada.Fs.Vfs.dll"
write_ext4_payload "$SHELL_DLL" "/Zapada.Shell.dll" "Zapada.Shell.dll"
write_ext4_payload "$CONSOLE_DLL" "/System.Console.dll" "System.Console.dll"
write_ext4_payload "$CONF_DLL" "/Zapada.Conformance.dll" "Zapada.Conformance.dll"
write_ext4_payload "$CROSSASM_DLL" "/Zapada.Conformance.CrossAsm.dll" "Zapada.Conformance.CrossAsm.dll"
dd if="$TMP_EXT4_IMG" of="$DISK_IMG" bs=512 seek="$ROOT_PART_LBA" conv=notrunc status=none

# Add TEST.DLL to ZAPADA_DATA for compatibility checks.
# Uses mtools mcopy with the @@offset syntax to write directly to the image file
# without re-mounting.  ZAPADA_DATA starts at LBA 133120 = byte offset 68157440.
#
# Priority:
#   1. If build/hello.dll exists (built by build.ps1 or test-all.ps1 after make), copy it
#      as TEST.DLL so the Phase 3B Step 4 end-to-end gate can execute Run() via the
#      managed runtime and emit [Boot] TEST.DLL loaded, [Boot] Zapada.Test.Hello loaded,
#      and [Gate] Phase3B.
#   2. Fallback: src/managed/Zapada.Test.Hello/bin/Release/net10.0/Zapada.Test.Hello.dll
#      exists when dotnet build/publish has run at least once.  This path is outside
#      build/ so it survives make clean.
#   3. Final fallback: 8-byte TESTDATA stub, which satisfies only the Phase 3A Part 3
#      gate check ([Boot] found: TEST.DLL / [Gate] GateD).
echo "  Adding TEST.DLL to ZAPADA_DATA partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$HELLO_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${DATA_PART_OFFSET}" "$HELLO_DLL" "::TEST.DLL"
        echo "  TEST.DLL written from $(basename "$HELLO_DLL") (Phase 3B Step 4 payload)"
    else
        printf 'TESTDATA' | MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${DATA_PART_OFFSET}" - "::TEST.DLL"
        echo "  TEST.DLL written (8 bytes: TESTDATA stub — Zapada.Test.Hello.dll not found)"
    fi
else
    echo "  WARNING: mtools not found; TEST.DLL not written to ZAPADA_DATA."
    echo "  Phase 3A Part 3 gate check [Gate] GateD will not pass until mtools is installed."
    echo "  Install mtools: sudo apt install mtools"
fi

echo "  Adding DRIVERS.DLL to ZAPADA_DATA partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$DRIVERS_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${DATA_PART_OFFSET}" "$DRIVERS_DLL" "::DRIVERS.DLL"
        echo "  DRIVERS.DLL written from $(basename "$DRIVERS_DLL") (shared driver HAL payload)"
    else
        echo "  WARNING: DRIVERS.DLL not found; managed driver HAL dependencies may not load from /mnt/c."
    fi
else
    echo "  WARNING: mtools not found; DRIVERS.DLL not written to ZAPADA_DATA."
fi

# Add VBLK.DLL to ZAPADA_DATA compatibility partition.
# Priority:
#   1. build/vblk.dll (staged by build.ps1 dotnet publish)
#   2. src/managed/Zapada.Drivers.VirtioBlock/bin/Release/net10.0/Zapada.Drivers.VirtioBlock.dll
#   3. If neither exists, skip with a warning (Phase 3.1 gate will not pass).
echo "  Adding VBLK.DLL to ZAPADA_DATA partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$VBLK_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${DATA_PART_OFFSET}" "$VBLK_DLL" "::VBLK.DLL"
        echo "  VBLK.DLL written from $(basename "$VBLK_DLL") (Phase 3.1 D.1 payload)"
    else
        echo "  WARNING: VBLK.DLL not found; Phase31-D1 gate will not pass."
        echo "  Run: dotnet publish src/managed/Zapada.Drivers.VirtioBlock/..."
    fi
else
    echo "  WARNING: mtools not found; VBLK.DLL not written to ZAPADA_DATA."
fi

# Add GPT.DLL to ZAPADA_DATA compatibility partition.
# Priority:
#   1. build/gpt.dll (staged by build.ps1 dotnet publish)
#   2. src/managed/Zapada.Fs.Gpt/bin/Release/net10.0/Zapada.Fs.Gpt.dll
#   3. If neither exists, skip with a warning (Phase 3.1 D.2 gate will not pass).
echo "  Adding GPT.DLL to ZAPADA_DATA partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$GPT_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${DATA_PART_OFFSET}" "$GPT_DLL" "::GPT.DLL"
        echo "  GPT.DLL written from $(basename "$GPT_DLL") (Phase 3.1 D.2 payload)"
    else
        echo "  WARNING: GPT.DLL not found; Phase31-D2 gate will not pass."
        echo "  Run: dotnet publish src/managed/Zapada.Fs.Gpt/..."
    fi
else
    echo "  WARNING: mtools not found; GPT.DLL not written to ZAPADA_DATA."
fi

# Add STORAGE.DLL to ZAPADA_DATA compatibility partition.
# Priority:
#   1. build/storage.dll (staged by build.ps1 dotnet publish)
#   2. src/managed/Zapada.Storage/bin/Release/net10.0/Zapada.Storage.dll
#   3. If neither exists, skip with a warning.
echo "  Adding STORAGE.DLL to ZAPADA_DATA partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$STORAGE_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${DATA_PART_OFFSET}" "$STORAGE_DLL" "::STORAGE.DLL"
        echo "  STORAGE.DLL written from $(basename "$STORAGE_DLL") (Phase 2B storage payload)"
    else
        echo "  WARNING: STORAGE.DLL not found; Phase-Storage gate will not pass."
        echo "  Run: dotnet publish src/managed/Zapada.Storage/..."
    fi
else
    echo "  WARNING: mtools not found; STORAGE.DLL not written to ZAPADA_DATA."
fi

# Add FAT32.DLL to ZAPADA_DATA compatibility partition.
# Priority:
#   1. build/fat32.dll (staged by build.ps1 dotnet publish)
#   2. src/managed/Zapada.Fs.Fat32/bin/Release/net10.0/Zapada.Fs.Fat32.dll
#   3. If neither exists, skip with a warning (Phase 3.1 D.3 gate will not pass).
echo "  Adding FAT32.DLL to ZAPADA_DATA partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$FAT32_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${DATA_PART_OFFSET}" "$FAT32_DLL" "::FAT32.DLL"
        echo "  FAT32.DLL written from $(basename "$FAT32_DLL") (Phase 3.1 D.3 payload)"
    else
        echo "  WARNING: FAT32.DLL not found; Phase31-D3 gate will not pass."
        echo "  Run: dotnet publish src/managed/Zapada.Fs.Fat32/..."
    fi
else
    echo "  WARNING: mtools not found; FAT32.DLL not written to ZAPADA_DATA."
fi

echo "  Adding EXT.DLL to ZAPADA_DATA partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$EXT_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${DATA_PART_OFFSET}" "$EXT_DLL" "::EXT.DLL"
        echo "  EXT.DLL written from $(basename "$EXT_DLL") (Ext shared payload)"
    else
        echo "  WARNING: EXT.DLL not found; Ext4 dependency will be absent."
        echo "  Run: dotnet publish src/managed/Zapada.Fs.Ext/..."
    fi
else
    echo "  WARNING: mtools not found; EXT.DLL not written to ZAPADA_DATA."
fi

echo "  Adding EXT4.DLL to ZAPADA_DATA partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$EXT4_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${DATA_PART_OFFSET}" "$EXT4_DLL" "::EXT4.DLL"
        echo "  EXT4.DLL written from $(basename "$EXT4_DLL") (Ext4 driver payload)"
    else
        echo "  WARNING: EXT4.DLL not found; Phase-Ext4Driver gate will not pass."
        echo "  Run: dotnet publish src/managed/Zapada.Fs.Ext4/..."
    fi
else
    echo "  WARNING: mtools not found; EXT4.DLL not written to ZAPADA_DATA."
fi

# Add VFS.DLL to ZAPADA_DATA compatibility partition.
# Priority:
#   1. build/vfs.dll (staged by build.ps1 dotnet publish)
#   2. src/managed/Zapada.Fs.Vfs/bin/Release/net10.0/Zapada.Fs.Vfs.dll
#   3. If neither exists, skip with a warning (Phase 3.1 D.4 gate will not pass).
echo "  Adding VFS.DLL to ZAPADA_DATA partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$VFS_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${DATA_PART_OFFSET}" "$VFS_DLL" "::VFS.DLL"
        echo "  VFS.DLL written from $(basename "$VFS_DLL") (Phase 3.1 D.4 payload)"
    else
        echo "  WARNING: VFS.DLL not found; Phase31-D4 gate will not pass."
        echo "  Run: dotnet publish src/managed/Zapada.Fs.Vfs/..."
    fi
else
    echo "  WARNING: mtools not found; VFS.DLL not written to ZAPADA_DATA."
fi

echo "  Adding SHELL.DLL to ZAPADA_DATA partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$SHELL_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${DATA_PART_OFFSET}" "$SHELL_DLL" "::SHELL.DLL"
        echo "  SHELL.DLL written from $(basename "$SHELL_DLL") (Zapada.Shell payload)"
    else
        echo "  WARNING: SHELL.DLL not found; Zapada.Shell payload will be absent."
        echo "  Run: dotnet publish src/managed/Zapada.Shell/..."
    fi
else
    echo "  WARNING: mtools not found; SHELL.DLL not written to ZAPADA_DATA."
fi

# Add CONF.DLL to ZAPADA_DATA compatibility partition.
# Priority:
#   1. build/conf.dll (staged by build.ps1 dotnet publish)
#   2. src/managed/Zapada.Conformance/bin/Release/net10.0/Zapada.Conformance.dll
#   3. If neither exists, skip with a warning (Phase 3.2 gate will not pass).
echo "  Adding CONF.DLL to ZAPADA_DATA partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$CONF_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${DATA_PART_OFFSET}" "$CONF_DLL" "::CONF.DLL"
        MTOOLS_SKIP_CHECK=1 mmd -i "${DISK_IMG}@@${DATA_PART_OFFSET}" "::SYS" 2>/dev/null || true
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${DATA_PART_OFFSET}" "$CONF_DLL" "::SYS/CONF.DLL"
        echo "  CONF.DLL written from $(basename "$CONF_DLL") (Phase 3.2 conformance payload)"
    else
        echo "  WARNING: CONF.DLL not found; Phase3.2-Conf gate will not pass."
        echo "  Run: dotnet publish src/managed/Zapada.Conformance/..."
    fi
else
    echo "  WARNING: mtools not found; CONF.DLL not written to ZAPADA_DATA."
fi

# Add the cross-assembly conformance dependency so disk-based loading paths can
# exercise the same two-assembly CLR behavior as initramfs-based bring-up.
echo "  Adding XCONF.DLL to ZAPADA_DATA partition..."
if command -v mcopy > /dev/null 2>&1; then
    if [ -f "$CROSSASM_DLL" ]; then
        MTOOLS_SKIP_CHECK=1 mcopy -i "${DISK_IMG}@@${DATA_PART_OFFSET}" "$CROSSASM_DLL" "::XCONF.DLL"
        echo "  XCONF.DLL written from $(basename "$CROSSASM_DLL") (cross-assembly conformance payload)"
    else
        echo "  WARNING: XCONF.DLL not found; cross-assembly disk payload will be absent."
        echo "  Run: dotnet publish src/managed/Zapada.Conformance.CrossAsm/..."
    fi
else
    echo "  WARNING: mtools not found; XCONF.DLL not written to ZAPADA_DATA."
fi

# Write ZAPADASK signature at bytes 0-7 of sector 0.
# The protective MBR bootstrap code area (bytes 0-439) is unused by UEFI.
# Writing our signature here does not invalidate the GPT or MBR boot record.
echo "  Writing ZAPADASK signature at sector 0 offset 0..."
printf '\x43\x59\x4C\x49\x58\x44\x53\x4B' | dd of="$DISK_IMG" bs=1 count=8 conv=notrunc status=none

echo "  Disk image created: $DISK_IMG"
echo "  Signature: 43594C495844534B (ZAPADASK)"
echo "  Done."
