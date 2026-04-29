# Zapada - run.ps1
# Launch Zapada in QEMU for x86_64 or AArch64.
#
# Usage:
#   .\run.ps1 -Arch x86_64             # x86_64, display + serial log
#   .\run.ps1 -Arch aarch64            # AArch64 (QEMU virt), serial log
#   .\run.ps1 -Arch x86_64 -WaitForGdb # x86_64, pause at entry, wait for GDB on :1234
#   .\run.ps1 -Arch x86_64 -Nographic  # x86_64, headless, serial to stdout
#   .\run.ps1 -Arch aarch64 -Nographic # AArch64, headless, serial to stdout
#   .\run.ps1 -Arch x86_64 -TimeoutSec 5 # auto-stop QEMU after timeout, like test-all
#
# Requires:
#   - qemu-system-x86_64 on PATH for x86_64   (winget install QEMU.QEMU)
#   - qemu-system-aarch64 on PATH for aarch64  (included in the same QEMU package)

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("x86_64", "aarch64")]
    [string]$Arch,

    [switch]$WaitForGdb, # x86_64 only: freeze CPU at entry, open GDB stub on tcp::1234
    [switch]$Nographic,  # headless; serial goes to stdio

    [int]$TimeoutSec = 15 # 0 = interactive; >0 = auto-kill QEMU after timeout
)

$QEMU_DIR  = "C:\Program Files\qemu"
$MEM_X86   = "512M"
$MEM_AA64  = "1024M"

function Invoke-QemuWithTimeout {
    param(
        [string]   $QemuExe,
        [string[]] $QemuArgs,
        [string]   $LogPath,
        [int]      $TimeoutMs
    )

    $stderrPath = ($LogPath -replace '\.log$', '') + "-qemu-stderr.log"
    if (Test-Path $stderrPath) { Remove-Item $stderrPath -Force }

    $proc = Start-Process -FilePath $QemuExe -ArgumentList $QemuArgs `
                          -PassThru -NoNewWindow `
                          -RedirectStandardOutput "NUL" `
                          -RedirectStandardError  $stderrPath `
                          -ErrorAction SilentlyContinue

    if ($null -eq $proc) {
        Write-Host "ERROR: Could not launch $QemuExe" -ForegroundColor Red
        return -2
    }

    $exited = $proc.WaitForExit($TimeoutMs)
    $exitCode = if ($exited) { $proc.ExitCode } else { -1 }

    if (-not $exited) {
        try { $proc.Kill() } catch {}
        Start-Sleep -Milliseconds 400
    }

    return $exitCode
}

# --------------------------------------------------------------------------
# x86_64
# --------------------------------------------------------------------------
if ($Arch -eq "x86_64") {
    $QEMU = if (Test-Path "$QEMU_DIR\qemu-system-x86_64.exe") {
        "$QEMU_DIR\qemu-system-x86_64.exe"
    } else { "qemu-system-x86_64" }

    $ISO = ".\build\Zapada.iso"
    $LOG = ".\build\serial.log"

    if (-not (Test-Path $ISO)) {
        Write-Host "ERROR: $ISO not found. Run .\build.ps1 -Arch x86_64 first." -ForegroundColor Red
        exit 1
    }

    New-Item -ItemType Directory -Force -Path ".\build" | Out-Null

    $DISK = ".\build\disk.img"

    $qemuArgs = @(
        "-machine", "pc",
        "-cpu",     "qemu64",
        "-m",       $MEM_X86,
        "-cdrom",   $ISO,
        "-boot",    "d",
        "-serial",  "file:$LOG",
        "-serial",  "mon:stdio",
        "-vga",     "std",
        "-smp",     "2",
        "-net",     "none",
        "-no-reboot",
        "-no-shutdown"
    )

    # Attach VirtIO block disk image if present (Phase 3A Part 2+).
    # x86_64 currently uses the legacy/transitional I/O-port transport path.
    if (Test-Path $DISK) {
        $qemuArgs += "-drive"
        $qemuArgs += "file=$DISK,format=raw,if=none,id=d0"
        $qemuArgs += "-device"
        $qemuArgs += "virtio-blk-pci,drive=d0,disable-modern=on,disable-legacy=off,addr=0x4"
        Write-Host "  Disk image  : $DISK (VirtIO PCI legacy/transitional)" -ForegroundColor Gray
    }

    if ($WaitForGdb) {
        $qemuArgs += "-s"
        $qemuArgs += "-S"
        Write-Host ""
        Write-Host "Debug mode: CPU frozen at entry point." -ForegroundColor Yellow
        Write-Host "Attach GDB in a second terminal:" -ForegroundColor Yellow
        Write-Host "  gdb ./build/Zapada.elf" -ForegroundColor Cyan
        Write-Host "  (gdb) target remote :1234" -ForegroundColor Cyan
        Write-Host "  (gdb) break kernel_main" -ForegroundColor Cyan
        Write-Host "  (gdb) continue" -ForegroundColor Cyan
        Write-Host ""
    }

    if ($Nographic) {
        $qemuArgs = $qemuArgs | Where-Object { $_ -ne "std" -and $_ -ne "-vga" }
        $qemuArgs += "-nographic"
    }

    Write-Host ""
    Write-Host "Booting Zapada (x86_64)..." -ForegroundColor Green
    Write-Host "  ISO         : $ISO" -ForegroundColor Gray
    Write-Host "  Serial log  : $LOG" -ForegroundColor Gray
    Write-Host "  QEMU monitor: Ctrl+Alt+2" -ForegroundColor Gray
    Write-Host "  Exit QEMU   : Ctrl+Alt+Q (window) or type 'quit' in monitor" -ForegroundColor Gray
    Write-Host ""

    if ($TimeoutSec -gt 0) {
        Write-Host "  Auto-timeout : ${TimeoutSec}s" -ForegroundColor Gray
        Invoke-QemuWithTimeout -QemuExe $QEMU -QemuArgs $qemuArgs -LogPath $LOG -TimeoutMs ($TimeoutSec * 1000) | Out-Null
    } else {
        & $QEMU @qemuArgs
    }
}

# --------------------------------------------------------------------------
# AArch64
# --------------------------------------------------------------------------
elseif ($Arch -eq "aarch64") {
    $QEMU = if (Test-Path "$QEMU_DIR\qemu-system-aarch64.exe") {
        "$QEMU_DIR\qemu-system-aarch64.exe"
    } else { "qemu-system-aarch64" }

    $IMG = ".\build\aarch64\kernel8.img"
    $LOG = ".\build\aarch64\serial.log"
    $INITRD = ".\build\initramfs.cpio.gz"

    if (-not (Test-Path $IMG)) {
        Write-Host "ERROR: $IMG not found. Run .\build.ps1 -Arch aarch64 first." -ForegroundColor Red
        exit 1
    }

    New-Item -ItemType Directory -Force -Path ".\build\aarch64" | Out-Null

    # QEMU virt: generic ARMv8 platform with PL011 UART at 0x09000000.
    #
    # For now AArch64 test/run parity uses the built-in virtio-mmio transport
    # exposed by the virt machine itself. The PCIe/ECAM probe code remains in
    # the kernel tree for future RPi4-class work, but QEMU -kernel does not
    # perform the BAR/resource assignment needed for our PCI path yet.
    $DISK = ".\build\disk.img"

    $qemuArgs = @(
        "-M",       "virt",
        "-cpu",     "cortex-a72",
        "-kernel",  $IMG,
        "-initrd",  $INITRD,
        "-m",       $MEM_AA64,
        "-global",  "virtio-mmio.force-legacy=false",
        "-serial",  "file:$LOG",
        "-display", "none",
        "-no-reboot"
    )

    # Attach disk image via the built-in QEMU virtio-mmio transport.
    if (Test-Path $DISK) {
        $qemuArgs += "-drive"
        $qemuArgs += "if=none,file=$DISK,format=raw,id=d0"
        $qemuArgs += "-device"
        $qemuArgs += "virtio-blk-device,drive=d0"
    }

    if ($Nographic) {
        # Replace file log with stdio so output is visible in the terminal
        $baseArgs = @("-M", "virt", "-cpu", "cortex-a72", "-kernel", $IMG, "-initrd", $INITRD, "-m", $MEM_AA64, "-global", "virtio-mmio.force-legacy=false", "-nographic", "-no-reboot")
        if (Test-Path $DISK) {
            $baseArgs += "-drive"
            $baseArgs += "if=none,file=$DISK,format=raw,id=d0"
            $baseArgs += "-device"
            $baseArgs += "virtio-blk-device,drive=d0"
        }
        $qemuArgs = $baseArgs
    }

    Write-Host ""
    Write-Host "Booting Zapada (AArch64 / QEMU virt)..." -ForegroundColor Green
    Write-Host "  Kernel image: $IMG" -ForegroundColor Gray
    Write-Host "  Serial log  : $LOG" -ForegroundColor Gray
    Write-Host "  Exit QEMU   : Ctrl+A then X (nographic) or kill the process" -ForegroundColor Gray
    Write-Host ""

    if ($TimeoutSec -gt 0) {
        Write-Host "  Auto-timeout : ${TimeoutSec}s" -ForegroundColor Gray
        Invoke-QemuWithTimeout -QemuExe $QEMU -QemuArgs $qemuArgs -LogPath $LOG -TimeoutMs ($TimeoutSec * 1000) | Out-Null
    } else {
        & $QEMU @qemuArgs
    }
}

