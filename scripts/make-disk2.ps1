# Zapada - scripts/make-disk2.ps1
# Creates build/disk2.img for the multi-device storage smoke.

param(
    [switch]$Force
)

$DISK_IMG = ".\build\disk2.img"
$BASH_SCRIPT = ".\scripts\make-disk2.sh"

function Test-WslAvailable {
    try {
        $null = wsl --status 2>&1
        return $true
    } catch {
        return $false
    }
}

if ((Test-Path $DISK_IMG) -and (-not $Force)) {
    Write-Host "  Second disk image already exists: $DISK_IMG" -ForegroundColor Gray
    Write-Host "  Use -Force to recreate it." -ForegroundColor Gray
    exit 0
}

Write-Host ""
Write-Host "Creating Zapada second disk image..." -ForegroundColor Cyan
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
    wsl bash $wslScript "$wslOutput/disk2.img"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: make-disk2.sh failed (exit code $LASTEXITCODE)." -ForegroundColor Red
        Write-Host "  Make sure parted, dosfstools, and mtools are installed in WSL." -ForegroundColor Yellow
        exit 1
    }
} else {
    Write-Host "WARNING: WSL is not available." -ForegroundColor Yellow
    Write-Host "  Creating a blank 64 MiB fallback image without GPT/FAT32." -ForegroundColor Yellow
    $buf = [byte[]]::new(64 * 1024 * 1024)
    [System.IO.File]::WriteAllBytes((Resolve-Path ".\build").Path + "\disk2.img", $buf)
}

Write-Host ""
Write-Host "Second disk image ready: $DISK_IMG" -ForegroundColor Green
Write-Host ""
