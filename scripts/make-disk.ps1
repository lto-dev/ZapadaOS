# Zapada - scripts/make-disk.ps1
#
# Creates the Zapada block device disk image (build/disk.img) using WSL.
#
# Requirements:
#   - WSL (Windows Subsystem for Linux) with Bash available
#   - Inside WSL: parted, dosfstools, mtools, e2fsprogs
#     (sudo apt install parted dosfstools mtools e2fsprogs)
#
# Usage:
#   .\scripts\make-disk.ps1
#   .\scripts\make-disk.ps1 -Force   # overwrite existing image
#
# The resulting disk image contains:
#   - GPT partition table
#   - Partition 1: ZAPADA_BOOT (ext4, 64 MiB, mounted as / by the managed boot flow)
#   - Partition 2: ZAPADA_DATA (FAT32, 32 MiB, mounted as /mnt/c by /etc/fstab)
#   - Signature "ZAPADASK" at byte offset 0-7 of sector 0
#
# Gate check: the kernel's Phase 3A Part 2 test reads LBA 0 and expects
# bytes 0-7 to equal 43 59 4C 49 58 44 53 4B (ASCII "ZAPADASK").

param(
    [switch]$Force   # Overwrite existing disk.img if present
)

$DISK_IMG = ".\build\disk.img"
$BASH_SCRIPT = ".\scripts\make-disk.sh"

# Check whether we can use WSL
function Test-WslAvailable {
    try {
        $null = wsl --status 2>&1
        return $true
    } catch {
        return $false
    }
}

# Skip if image already exists and -Force was not specified
if ((Test-Path $DISK_IMG) -and (-not $Force)) {
    Write-Host "  Disk image already exists: $DISK_IMG" -ForegroundColor Gray
    Write-Host "  Use -Force to recreate it." -ForegroundColor Gray
    exit 0
}

Write-Host ""
Write-Host "Creating Zapada disk image..." -ForegroundColor Cyan
Write-Host "  Output : $DISK_IMG"
Write-Host "  Script : $BASH_SCRIPT"
Write-Host ""

if (-not (Test-Path $BASH_SCRIPT)) {
    Write-Host "ERROR: $BASH_SCRIPT not found." -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path ".\build" | Out-Null

if (Test-WslAvailable) {
    # Convert Windows paths to WSL paths. Normalize to forward slashes first,
    # otherwise wslpath can receive backslash-stripped arguments like
    $scriptPathWin = ((Resolve-Path $BASH_SCRIPT).Path) -replace '\\', '/'
    $outputPathWin = ((Resolve-Path ".\build").Path) -replace '\\', '/'
    $wslScript = (wsl wslpath -u $scriptPathWin).Trim()
    $wslOutput = (wsl wslpath -u $outputPathWin).Trim()

    Write-Host "  Using WSL..." -ForegroundColor Gray
    wsl bash $wslScript "$wslOutput/disk.img"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: make-disk.sh failed (exit code $LASTEXITCODE)." -ForegroundColor Red
        Write-Host "  Make sure parted and dosfstools are installed in WSL:" -ForegroundColor Yellow
        Write-Host "    sudo apt install parted dosfstools" -ForegroundColor Yellow
        exit 1
    }
} else {
    Write-Host "WARNING: WSL is not available." -ForegroundColor Yellow
    Write-Host "  Falling back to creating a minimal 100 MiB blank disk image" -ForegroundColor Yellow
    Write-Host "  without partition tables (no GPT or FAT32 formatting)." -ForegroundColor Yellow
    Write-Host "  Install WSL and run this script again for a proper disk image." -ForegroundColor Yellow
    Write-Host ""

    # Create a 100 MiB zero-filled image as a fallback
    $buf = [byte[]]::new(100 * 1024 * 1024)

    # Write ZAPADASK signature at byte 0
    $sig = [byte[]]@(0x43, 0x59, 0x4C, 0x49, 0x58, 0x44, 0x53, 0x4B)
    [Array]::Copy($sig, $buf, 8)

    [System.IO.File]::WriteAllBytes((Resolve-Path ".\build").Path + "\disk.img", $buf)
    Write-Host "  Created minimal blank disk image with ZAPADASK signature." -ForegroundColor Gray
}

Write-Host ""
Write-Host "Disk image ready: $DISK_IMG" -ForegroundColor Green
Write-Host "  Gate check signature: 43594C495844534B (ZAPADASK at LBA 0 offset 0)" -ForegroundColor Gray
Write-Host ""



