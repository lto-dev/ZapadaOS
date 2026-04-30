# Zapada - scripts/make-disk-usb.ps1
# Creates build/disk-usb.img for the xHCI/USB mass-storage smoke.

param(
    [switch]$Force
)

$DISK_IMG = ".\build\disk-usb.img"
$BASH_SCRIPT = ".\scripts\make-disk-usb.sh"

function Test-WslAvailable {
    try {
        $null = wsl --status 2>&1
        return $true
    } catch {
        return $false
    }
}

if ((Test-Path $DISK_IMG) -and (-not $Force)) {
    Write-Host "  USB storage image already exists: $DISK_IMG" -ForegroundColor Gray
    Write-Host "  Use -Force to recreate it." -ForegroundColor Gray
    exit 0
}

Write-Host ""
Write-Host "Creating Zapada USB storage image..." -ForegroundColor Cyan
Write-Host "  Output : $DISK_IMG"
Write-Host "  Script : $BASH_SCRIPT"
Write-Host ""

if (-not (Test-Path $BASH_SCRIPT)) {
    Write-Host "ERROR: $BASH_SCRIPT not found." -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path ".\build" | Out-Null

if (Test-WslAvailable) {
    $scriptPathWin = ((Resolve-Path $BASH_SCRIPT).Path) -replace '\\', '/'
    $outputPathWin = ((Resolve-Path ".\build").Path) -replace '\\', '/'
    $wslScript = (wsl wslpath -u $scriptPathWin).Trim()
    $wslOutput = (wsl wslpath -u $outputPathWin).Trim()

    Write-Host "  Using WSL..." -ForegroundColor Gray
    wsl bash $wslScript "$wslOutput/disk-usb.img"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: make-disk-usb.sh failed (exit code $LASTEXITCODE)." -ForegroundColor Red
        Write-Host "  Make sure parted, dosfstools, and mtools are installed in WSL." -ForegroundColor Yellow
        exit 1
    }
} else {
    Write-Host "WARNING: WSL is not available." -ForegroundColor Yellow
    Write-Host "  Creating a blank 64 MiB fallback image without GPT/FAT32." -ForegroundColor Yellow
    $buf = [byte[]]::new(64 * 1024 * 1024)
    [System.IO.File]::WriteAllBytes((Resolve-Path ".\build").Path + "\disk-usb.img", $buf)
}

Write-Host ""
Write-Host "USB storage image ready: $DISK_IMG" -ForegroundColor Green
Write-Host ""
