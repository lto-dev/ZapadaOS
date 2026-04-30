# Zapada - run-shell.ps1
# Launch the x86_64 Zapada image in QEMU with the serial console attached
# directly to this terminal so the managed interactive shell can be used.
#
# Usage:
#   .\run-shell.ps1
#   .\run-shell.ps1 -Build
#   .\run-shell.ps1 -Build -RecreateDisks
#   .\run-shell.ps1 -NoUsb
#   .\run-shell.ps1 -WaitForGdb
#
# Important:
#   This script intentionally does not pass -Smoke to build.ps1. A non-smoke
#   kernel command line makes Zapada.Boot select the interactive shell.

param(
    [switch]$Build,
    [switch]$NativeClean,
    [switch]$RecreateDisks,
    [switch]$NoSecondDisk,
    [switch]$NoUsb,
    [switch]$WaitForGdb,
    [string]$QemuDir = "C:\Program Files\qemu"
)

$ErrorActionPreference = "Stop"

function Get-QemuPath {
    param([string]$QemuDir)

    $localQemu = Join-Path $QemuDir "qemu-system-x86_64.exe"
    if (Test-Path $localQemu) {
        return $localQemu
    }

    return "qemu-system-x86_64"
}

if ($Build) {
    $buildArgs = @("-Arch", "x86_64")
    if ($NativeClean) {
        $buildArgs += "-NativeClean"
    }

    Write-Host "Building x86_64 Zapada without smoke mode..." -ForegroundColor Cyan
    & .\build.ps1 @buildArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if ($RecreateDisks) {
    Write-Host "Recreating storage images..." -ForegroundColor Cyan
    & .\scripts\make-disk.ps1 -Force
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    if (-not $NoSecondDisk) {
        & .\scripts\make-disk2.ps1 -Force
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    if (-not $NoUsb) {
        & .\scripts\make-disk-usb.ps1 -Force
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}

$iso = ".\build\Zapada.iso"
$disk = ".\build\disk.img"
$disk2 = ".\build\disk2.img"
$usbDisk = ".\build\disk-usb.img"

if (-not (Test-Path $iso)) {
    Write-Host "ERROR: $iso not found. Run .\build.ps1 -Arch x86_64 first, or use .\run-shell.ps1 -Build." -ForegroundColor Red
    exit 1
}

$qemu = Get-QemuPath -QemuDir $QemuDir

$qemuArgs = @(
    "-machine", "pc",
    "-cpu", "qemu64",
    "-m", "512M",
    "-cdrom", $iso,
    "-boot", "d",
    "-serial", "mon:stdio",
    "-display", "none",
    "-smp", "2",
    "-net", "none",
    "-no-reboot",
    "-no-shutdown"
)

if (Test-Path $disk) {
    $qemuArgs += "-drive"
    $qemuArgs += "file=$disk,format=raw,if=none,id=d0"
    $qemuArgs += "-device"
    $qemuArgs += "virtio-blk-pci,drive=d0,disable-modern=off,disable-legacy=on,addr=0x4"
}

if ((-not $NoSecondDisk) -and (Test-Path $disk2)) {
    $qemuArgs += "-drive"
    $qemuArgs += "file=$disk2,format=raw,if=none,id=d1"
    $qemuArgs += "-device"
    $qemuArgs += "virtio-blk-pci,drive=d1,disable-modern=off,disable-legacy=on,addr=0x5"
}

if ((-not $NoUsb) -and (Test-Path $usbDisk)) {
    $qemuArgs += "-drive"
    $qemuArgs += "file=$usbDisk,format=raw,if=none,id=usb0"
    $qemuArgs += "-device"
    $qemuArgs += "qemu-xhci,id=xhci,msi=off,msix=off,addr=0x6"
    $qemuArgs += "-device"
    $qemuArgs += "usb-storage,drive=usb0,bus=xhci.0"
}

if ($WaitForGdb) {
    $qemuArgs += "-s"
    $qemuArgs += "-S"
}

Write-Host "Booting Zapada interactive shell (x86_64)..." -ForegroundColor Green
Write-Host "  ISO       : $iso" -ForegroundColor Gray
Write-Host "  Serial    : stdio" -ForegroundColor Gray
Write-Host "  Smoke     : disabled" -ForegroundColor Gray
Write-Host "  Exit      : press Ctrl+A then X, or type 'quit' in the QEMU monitor" -ForegroundColor Gray
Write-Host ""

& $qemu @qemuArgs
exit $LASTEXITCODE
