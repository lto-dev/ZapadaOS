# Zapada - tests/x86_64/gate_test.ps1
#
# Stage 1.2 gate test for Windows.
#
# Launches the kernel in QEMU, captures serial output, and checks that all
# Stage 1.2 gate conditions are present in the output.
#
# Usage:
#   .\tests\x86_64\gate_test.ps1
#   .\tests\x86_64\gate_test.ps1 -Build        # build before testing
#   .\tests\x86_64\gate_test.ps1 -QemuPath "C:\other\qemu\qemu-system-x86_64.exe"
#
# Exit code:
#   0 - all gate checks passed
#   1 - one or more gate checks failed or QEMU did not produce output
#
# To add checks for a new stage, append entries to $GateChecks or $ForbidChecks.

param(
    [switch]$Build,
    [string]$QemuPath = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$Iso      = "build\Zapada.iso",
    [string]$Log      = "build\serial.log",
    [int]$TimeoutSecs = 10
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# --------------------------------------------------------------------------
# Gate conditions
#
# $GateChecks : substrings that MUST appear somewhere in the serial log.
# $ForbidChecks: substrings that must NOT appear in the serial log.
#
# Extend these arrays as new stages are implemented.
# --------------------------------------------------------------------------

$GateChecks = @(
    # Stage 1.1 conditions (always required)
    "Multiboot2 magic   : OK",
    "Memory map         :",

    # Stage 1.2 conditions
    "GDT                : loaded",
    "IDT                : loaded (vectors 0-255 installed)",
    "PMM free frames    :",
    "Heap probe         : 0x",
    "Stage 1.2 check    : early scaffolding initialized",
    "System halted after successful Stage 1.2 bring-up."
)

$ForbidChecks = @(
    "KERNEL PANIC"
)

# --------------------------------------------------------------------------
# Optional build step
# --------------------------------------------------------------------------

if ($Build) {
    Write-Host "Building kernel..."
    $result = wsl bash -c "cd /mnt/s/lto-dev/Zapada && make all 2>&1"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "BUILD FAILED"
        exit 1
    }
    Write-Host "Build complete."
}

# --------------------------------------------------------------------------
# Check prerequisites
# --------------------------------------------------------------------------

if (-not (Test-Path $QemuPath)) {
    Write-Host "FAIL: QEMU not found at: $QemuPath"
    exit 1
}

if (-not (Test-Path $Iso)) {
    Write-Host "FAIL: ISO not found at: $Iso"
    Write-Host "      Run 'make all' or use -Build to build first."
    exit 1
}

# --------------------------------------------------------------------------
# Run QEMU
# --------------------------------------------------------------------------

Write-Host ""
Write-Host "Zapada Gate Test - x86_64 Stage 1.2"
Write-Host "------------------------------------"
Write-Host "ISO     : $Iso"
Write-Host "Timeout : $TimeoutSecs seconds"
Write-Host ""
Write-Host "Launching QEMU..."

# Clear any previous log before the run.
if (Test-Path $Log) {
    Remove-Item $Log -Force
}

$proc = Start-Process `
    -FilePath $QemuPath `
    -ArgumentList "-cdrom `"$Iso`" -serial file:`"$Log`" -display none -no-reboot" `
    -PassThru

Start-Sleep -Seconds $TimeoutSecs

if (-not $proc.HasExited) {
    $proc.Kill()
}

Start-Sleep -Milliseconds 500

# --------------------------------------------------------------------------
# Check output
# --------------------------------------------------------------------------

if (-not (Test-Path $Log)) {
    Write-Host "FAIL: serial.log was not created. QEMU may not have started correctly."
    exit 1
}

$content = Get-Content $Log -Raw
if ([string]::IsNullOrWhiteSpace($content)) {
    Write-Host "FAIL: serial.log is empty. Kernel produced no output."
    exit 1
}

$passed    = 0
$failed    = 0
$forbidden = 0

Write-Host "Gate check results:"
Write-Host ""

foreach ($check in $GateChecks) {
    if ($content -like "*$check*") {
        Write-Host "  PASS  $check"
        $passed++
    } else {
        Write-Host "  FAIL  $check"
        $failed++
    }
}

foreach ($check in $ForbidChecks) {
    if ($content -like "*$check*") {
        Write-Host "  FAIL  (forbidden) $check"
        $forbidden++
    } else {
        Write-Host "  PASS  (not present) $check"
        $passed++
    }
}

Write-Host ""
Write-Host "Results: $passed passed, $($failed + $forbidden) failed"
Write-Host ""

if ($failed -gt 0 -or $forbidden -gt 0) {
    Write-Host "GATE TEST FAILED"
    Write-Host ""
    Write-Host "Full serial output:"
    Write-Host "------------------------------------"
    Get-Content $Log
    exit 1
} else {
    Write-Host "GATE TEST PASSED"
    exit 0
}

