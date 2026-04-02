# Zapada - test-all.ps1
# Build both architectures and verify serial output via QEMU.
#
# For each architecture:
#   1. Build with make (via WSL for x86_64; direct for aarch64)
#   2. Run QEMU in headless mode with serial captured to a log file
#   3. Wait up to $TimeoutSec seconds for the kernel to halt
#   4. Kill QEMU if it has not exited
#   5. Read the log and assert expected output lines are present
#   6. Report PASS / FAIL with individual check results
#
# Usage:
#   .\test-all.ps1                    # build both, test both
#   .\test-all.ps1 -Arch x86_64       # build + test only x86_64
#   .\test-all.ps1 -Arch aarch64      # build + test only aarch64
#   .\test-all.ps1 -NoBuild           # skip build step (use existing artifacts)
#   .\test-all.ps1 -DebugBuild        # kernel: -O0 -g -DDEBUG; managed: -c Debug
#   .\test-all.ps1 -TimeoutSec 10     # increase per-arch QEMU timeout
#
# Requirements:
#   - WSL with x86_64-elf cross-toolchain, nasm, grub-mkrescue
#   - WSL with aarch64-linux-gnu cross-toolchain
#   - qemu-system-x86_64 + qemu-system-aarch64 on PATH or in C:\Program Files\qemu\

param(
    [ValidateSet("x86_64", "aarch64", "all")]
    [string]$Arch       = "all",

    [switch]$NoBuild,             # skip the build step, use existing artifacts

    [switch]$DebugBuild,          # kernel: DEBUG=1 (-O0 -g -DDEBUG); managed: -c Debug

    [int]$TimeoutSec    = 5      # seconds to wait for QEMU to finish;
)

# Build configuration derived from -DebugBuild switch.
$BuildConfig = if ($DebugBuild) { "Debug" } else { "Release" }

$QEMU_DIR       = "C:\Program Files\qemu"
$EXIT_PASS      = 0
$EXIT_FAIL      = 1
$overallResult  = $EXIT_PASS
$ManagedCacheRoot = Join-Path (Get-Location).Path ".build-cache\managed"

# --------------------------------------------------------------------------
# Helper: print a check result line
# --------------------------------------------------------------------------
function Write-Check {
    param([string]$Label, [bool]$Pass, [string]$Detail = "")
    if ($Pass) {
        Write-Host ("  [PASS] " + $Label) -ForegroundColor Green
    } else {
        Write-Host ("  [FAIL] " + $Label) -ForegroundColor Red
        if ($Detail) {
            Write-Host ("         Expected: $Detail") -ForegroundColor DarkRed
        }
    }
    return $Pass
}

# --------------------------------------------------------------------------
# Helper: find a QEMU binary
# --------------------------------------------------------------------------
function Get-Qemu([string]$Exe) {
    $path = Join-Path $QEMU_DIR $Exe
    if (Test-Path $path) { return $path }
    return $Exe
}

# --------------------------------------------------------------------------
# Helper: run QEMU with a timeout and capture serial log
# Returns exit code (or -1 if killed by timeout, -2 if failed to launch)
# --------------------------------------------------------------------------
function Invoke-QemuWithTimeout {
    param(
        [string]   $QemuExe,
        [string[]] $QemuArgs,
        [string]   $LogPath,
        [int]      $TimeoutMs
    )

    # Remove stale log and ensure directory exists
    if (Test-Path $LogPath) { Remove-Item $LogPath -Force }
    New-Item -ItemType Directory -Force -Path (Split-Path $LogPath) | Out-Null

    # Capture QEMU stderr separately to diagnose early QEMU startup failures.
    $stderrPath = ($LogPath -replace '\.log$', '') + "-qemu-stderr.log"
    if (Test-Path $stderrPath) { Remove-Item $stderrPath -Force }

    # Start-Process -ArgumentList with an array passes each element as a separate
    # argv token; no manual quoting is needed for paths without embedded spaces.
    $proc = Start-Process -FilePath $QemuExe -ArgumentList $QemuArgs `
                          -PassThru -NoNewWindow `
                          -RedirectStandardOutput "NUL" `
                          -RedirectStandardError  $stderrPath `
                          -ErrorAction SilentlyContinue

    if ($null -eq $proc) {
        Write-Host "  ERROR: Could not launch $QemuExe" -ForegroundColor Red
        return -2
    }

    $exited = $proc.WaitForExit($TimeoutMs)
    $exitCode = if ($exited) { $proc.ExitCode } else { -1 }

    if (-not $exited) {
        try { $proc.Kill() } catch {}
        # Give the OS a moment to flush the serial file
        Start-Sleep -Milliseconds 400
    }

    # Diagnostic: report QEMU exit status and any stderr output.
    if ($exited) {
        Write-Host "  QEMU exited early (code=$exitCode) — checking stderr..." -ForegroundColor Yellow
        if (Test-Path $stderrPath) {
            $errContent = Get-Content $stderrPath -Raw -ErrorAction SilentlyContinue
            if ($errContent -and $errContent.Trim().Length -gt 0) {
                Write-Host "  [QEMU stderr]:" -ForegroundColor DarkRed
                Write-Host ($errContent.Trim() | Select-Object -First 20) -ForegroundColor DarkRed
            } else {
                Write-Host "  [QEMU stderr]: (empty)" -ForegroundColor Gray
            }
        }
    }

    return $exitCode
}

# --------------------------------------------------------------------------
# Helper: assert a set of patterns against a log file
# --------------------------------------------------------------------------
function Test-SerialLog {
    param(
        [string]   $LogPath,
        [string[]] $Checks,  # human-readable description for each check
        [string[]] $Patterns # corresponding substring to look for in the log
    )

    if (-not (Test-Path $LogPath)) {
        Write-Host "  ERROR: Serial log not found: $LogPath" -ForegroundColor Red
        return $false
    }

    $content = Get-Content $LogPath -Raw
    $allPass = $true

    for ($i = 0; $i -lt $Checks.Length; $i++) {
        # Use .Contains() — always returns a [bool]; avoids -like returning Object[]
        # when $content is $null (empty serial log from a failed QEMU start).
        $found = ($null -ne $content) -and $content.Contains($Patterns[$i])
        $ok    = Write-Check -Label $Checks[$i] -Pass ([bool]$found) -Detail $Patterns[$i]
        if (-not $ok) { $allPass = $false }
    }

    return $allPass
}

# --------------------------------------------------------------------------
# Helper: dump in-guest managed/runtime FAIL lines from serial output
# --------------------------------------------------------------------------
function Show-SerialFailLines {
    param([string]$LogPath)

    if (-not (Test-Path $LogPath)) {
        return
    }

    $failLines = Get-Content $LogPath -ErrorAction SilentlyContinue | Where-Object { $_.Contains("FAIL") }
    if ($null -ne $failLines -and $failLines.Count -gt 0) {
        Write-Host "  [Serial FAIL lines]" -ForegroundColor Yellow
        foreach ($line in $failLines) {
            Write-Host ("    " + $line) -ForegroundColor Yellow
        }
    }
}

# --------------------------------------------------------------------------
# Helper: detect WSL
# --------------------------------------------------------------------------
function Test-Wsl {
    try { $null = & wsl --status 2>&1; return $true } catch { return $false }
}

function ConvertTo-WslPath([string]$p) {
    $abs = Resolve-Path $p -ErrorAction SilentlyContinue
    if ($abs) { $p = $abs.Path }
    $p = $p -replace '\\', '/'
    if ($p -match '^([A-Za-z]):(.*)') {
        return "/mnt/$($Matches[1].ToLower())$($Matches[2])"
    }
    return $p
}

# --------------------------------------------------------------------------
# Build helper
# --------------------------------------------------------------------------
function Invoke-ManagedBootBuild {
    # Build Zapada.Boot and stage it for the initramfs.
    $bootProject    = Join-Path (Get-Location).Path "src\managed\Zapada.Boot\Zapada.Boot.csproj"
    $bootPublishOut = Join-Path (Get-Location).Path "src\managed\Zapada.Boot\publish"
    $bootDll        = Join-Path $bootPublishOut "Zapada.Boot.dll"
    $bootDllDest    = Join-Path (Get-Location).Path "build\boot.dll"
    $bootDllCache   = Join-Path $ManagedCacheRoot "Zapada.Boot.dll"

    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        Write-Host "  ERROR: 'dotnet' not found; Zapada.Boot cannot be staged into initramfs." -ForegroundColor Red
        return $false
    }

    if (-not (Test-Path $bootProject)) {
        Write-Host "  ERROR: Zapada.Boot.csproj not found; cannot build initramfs boot entry." -ForegroundColor Red
        return $false
    }

    Write-Host "  Building managed assembly (Zapada.Boot)..." -ForegroundColor Cyan
    dotnet publish $bootProject -c $BuildConfig -o $bootPublishOut --nologo 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: dotnet publish Zapada.Boot failed." -ForegroundColor Red
        return $false
    }

    if (-not (Test-Path $bootDll)) {
        Write-Host "  ERROR: $bootDll not found after publish." -ForegroundColor Red
        return $false
    }

    if (-not (Test-Path $ManagedCacheRoot)) {
        New-Item -ItemType Directory -Force -Path $ManagedCacheRoot | Out-Null
    }

    Copy-Item $bootDll $bootDllDest -Force
    Copy-Item $bootDll $bootDllCache -Force
    $bootSize = (Get-Item $bootDllDest).Length
    Write-Host "  Staged: $bootDllDest ($bootSize bytes)." -ForegroundColor Gray
    return $true
}

function Restore-ManagedCacheToBuild {
    $entries = @(
        @{ Cache = (Join-Path $ManagedCacheRoot "Zapada.Boot.dll"); Target = (Join-Path (Get-Location).Path "build\boot.dll") },
        @{ Cache = (Join-Path $ManagedCacheRoot "Zapada.Test.Hello.dll"); Target = (Join-Path (Get-Location).Path "build\hello.dll") },
        @{ Cache = (Join-Path $ManagedCacheRoot "Zapada.Drivers.VirtioBlock.dll"); Target = (Join-Path (Get-Location).Path "build\vblk.dll") },
        @{ Cache = (Join-Path $ManagedCacheRoot "Zapada.Fs.Gpt.dll"); Target = (Join-Path (Get-Location).Path "build\gpt.dll") },
        @{ Cache = (Join-Path $ManagedCacheRoot "Zapada.Storage.dll"); Target = (Join-Path (Get-Location).Path "build\storage.dll") },
        @{ Cache = (Join-Path $ManagedCacheRoot "Zapada.Fs.Fat32.dll"); Target = (Join-Path (Get-Location).Path "build\fat32.dll") },
        @{ Cache = (Join-Path $ManagedCacheRoot "Zapada.Fs.Vfs.dll"); Target = (Join-Path (Get-Location).Path "build\vfs.dll") },
        @{ Cache = (Join-Path $ManagedCacheRoot "Zapada.Conformance.CrossAsm.dll"); Target = (Join-Path (Get-Location).Path "build\conf-crossasm.dll") },
        @{ Cache = (Join-Path $ManagedCacheRoot "Zapada.Conformance.dll"); Target = (Join-Path (Get-Location).Path "build\conf.dll") }
    )

    foreach ($entry in $entries) {
        $targetDir = Split-Path $entry.Target -Parent
        if (-not (Test-Path $targetDir)) {
            New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
        }

        if (Test-Path $entry.Cache) {
            Copy-Item $entry.Cache $entry.Target -Force
        }
    }
}

function Invoke-ManagedHelloBuild {
    # Build Zapada.Test.Hello and stage build/hello.dll for the Phase 3B Step 4 end-to-end gate.
    # Must be called AFTER the native clean/build sequence because build artifacts can be recreated.
    # Returns true even if the project is absent (make-disk falls back to TESTDATA stub).
    $helloProject = Join-Path (Get-Location).Path "src\managed\Zapada.Test.Hello\Zapada.Test.Hello.csproj"
    $helloBinDll  = Join-Path (Get-Location).Path "src\managed\Zapada.Test.Hello\bin\$BuildConfig\net10.0\Zapada.Test.Hello.dll"
    $helloDest    = Join-Path (Get-Location).Path "build\hello.dll"

    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        Write-Host "  WARNING: 'dotnet' not found; Zapada.Test.Hello not built; Phase 3B gate may not pass." -ForegroundColor Yellow
        return $true
    }

    if (-not (Test-Path $helloProject)) {
        Write-Host "  WARNING: Zapada.Test.Hello.csproj not found; skipping Phase 3B test DLL build." -ForegroundColor Yellow
        return $true
    }

    Write-Host "  Building Phase 3B test fixture (Zapada.Test.Hello)..." -ForegroundColor Cyan
    dotnet build $helloProject -c $BuildConfig --nologo 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: dotnet build Zapada.Test.Hello failed." -ForegroundColor Red
        return $false
    }

    if (-not (Test-Path $helloBinDll)) {
        Write-Host "  ERROR: $helloBinDll not found after build." -ForegroundColor Red
        return $false
    }

    # Ensure build/ exists (make all creates it, but guard in case of partial clean)
    $buildDir = Join-Path (Get-Location).Path "build"
    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir | Out-Null
    }

    Copy-Item $helloBinDll $helloDest -Force
    $sz = (Get-Item $helloDest).Length
    Write-Host "  Staged: $helloDest ($sz bytes)." -ForegroundColor Gray
    return $true
}

function Invoke-ConformanceCrossAsmBuild {
    $crossAsmProject = Join-Path (Get-Location).Path "src\managed\Zapada.Conformance.CrossAsm\Zapada.Conformance.CrossAsm.csproj"
    $crossAsmBinDll  = Join-Path (Get-Location).Path "src\managed\Zapada.Conformance.CrossAsm\bin\$BuildConfig\net10.0\Zapada.Conformance.CrossAsm.dll"
    $crossAsmDest    = Join-Path (Get-Location).Path "build\conf-crossasm.dll"

    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        Write-Host "  WARNING: 'dotnet' not found; Zapada.Conformance.CrossAsm not built; cross-assembly conformance gates may not pass." -ForegroundColor Yellow
        return $true
    }

    if (-not (Test-Path $crossAsmProject)) {
        Write-Host "  WARNING: Zapada.Conformance.CrossAsm.csproj not found; skipping cross-assembly conformance build." -ForegroundColor Yellow
        return $true
    }

    Write-Host "  Building cross-assembly conformance fixture (Zapada.Conformance.CrossAsm)..." -ForegroundColor Cyan
    dotnet publish $crossAsmProject -c $BuildConfig -o (Join-Path (Get-Location).Path "build\conf-crossasm-out") --nologo 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: dotnet publish Zapada.Conformance.CrossAsm failed." -ForegroundColor Red
        return $false
    }

    if (-not (Test-Path $crossAsmBinDll)) {
        Write-Host "  ERROR: $crossAsmBinDll not found after publish." -ForegroundColor Red
        return $false
    }

    Copy-Item $crossAsmBinDll $crossAsmDest -Force
    Write-Host "  Staged: $crossAsmDest" -ForegroundColor Gray
    return $true
}

function Invoke-ConformanceBuild {
    $confProject = Join-Path (Get-Location).Path "src\managed\Zapada.Conformance\Zapada.Conformance.csproj"
    $confBinDll  = Join-Path (Get-Location).Path "src\managed\Zapada.Conformance\bin\$BuildConfig\net10.0\Zapada.Conformance.dll"
    $confDest    = Join-Path (Get-Location).Path "build\conf.dll"

    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        Write-Host "  WARNING: 'dotnet' not found; Zapada.Conformance not built; conformance gates may not pass." -ForegroundColor Yellow
        return $true
    }

    if (-not (Test-Path $confProject)) {
        Write-Host "  WARNING: Zapada.Conformance.csproj not found; skipping conformance build." -ForegroundColor Yellow
        return $true
    }

    Write-Host "  Building IL conformance tests (Zapada.Conformance)..." -ForegroundColor Cyan
    dotnet build $confProject -c $BuildConfig --nologo 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: dotnet build Zapada.Conformance failed." -ForegroundColor Red
        return $false
    }

    if (-not (Test-Path $confBinDll)) {
        Write-Host "  ERROR: $confBinDll not found after build." -ForegroundColor Red
        return $false
    }

    Copy-Item $confBinDll $confDest -Force
    Write-Host "  Staged: $confDest" -ForegroundColor Gray
    return $true
}

function Invoke-VblkBuild {
    # Build Zapada.Drivers.VirtioBlock and stage build/vblk.dll for the Phase 3.1 D.1 gate.
    # Must be called AFTER the native clean/build sequence because build artifacts can be recreated.
    # Returns true even if the project is absent (make-disk warns and skips VBLK.DLL).
    $vblkProject = Join-Path (Get-Location).Path "src\managed\Zapada.Drivers.VirtioBlock\Zapada.Drivers.VirtioBlock.csproj"
    $vblkBinDll  = Join-Path (Get-Location).Path "src\managed\Zapada.Drivers.VirtioBlock\bin\$BuildConfig\net10.0\Zapada.Drivers.VirtioBlock.dll"
    $vblkDest    = Join-Path (Get-Location).Path "build\vblk.dll"

    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        Write-Host "  WARNING: 'dotnet' not found; Zapada.Drivers.VirtioBlock not built; Phase 3.1 gate will not pass." -ForegroundColor Yellow
        return $true
    }

    if (-not (Test-Path $vblkProject)) {
        Write-Host "  WARNING: Zapada.Drivers.VirtioBlock.csproj not found; skipping Phase 3.1 D.1 driver build." -ForegroundColor Yellow
        return $true
    }

    Write-Host "  Building Phase 3.1 D.1 driver (Zapada.Drivers.VirtioBlock)..." -ForegroundColor Cyan
    dotnet build $vblkProject -c $BuildConfig --nologo 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: dotnet build Zapada.Drivers.VirtioBlock failed." -ForegroundColor Red
        return $false
    }

    if (-not (Test-Path $vblkBinDll)) {
        Write-Host "  ERROR: $vblkBinDll not found after build." -ForegroundColor Red
        return $false
    }

    # Ensure build/ exists (make all creates it, but guard in case of partial clean)
    $buildDir = Join-Path (Get-Location).Path "build"
    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir | Out-Null
    }

    Copy-Item $vblkBinDll $vblkDest -Force
    $sz = (Get-Item $vblkDest).Length
    Write-Host "  Staged: $vblkDest ($sz bytes)." -ForegroundColor Gray
    return $true
}

function Invoke-GptBuild {
    # Build Zapada.Fs.Gpt and stage build/gpt.dll for the Phase 3.1 D.2 gate.
    # Must be called AFTER the native clean/build sequence because build artifacts can be recreated.
    # Returns true even if the project is absent (make-disk warns and skips GPT.DLL).
    $gptProject = Join-Path (Get-Location).Path "src\managed\Zapada.Fs.Gpt\Zapada.Fs.Gpt.csproj"
    $gptBinDll  = Join-Path (Get-Location).Path "src\managed\Zapada.Fs.Gpt\bin\$BuildConfig\net10.0\Zapada.Fs.Gpt.dll"
    $gptDest    = Join-Path (Get-Location).Path "build\gpt.dll"

    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        Write-Host "  WARNING: 'dotnet' not found; Zapada.Fs.Gpt not built; Phase 3.1 D.2 gate will not pass." -ForegroundColor Yellow
        return $true
    }

    if (-not (Test-Path $gptProject)) {
        Write-Host "  WARNING: Zapada.Fs.Gpt.csproj not found; skipping Phase 3.1 D.2 GPT build." -ForegroundColor Yellow
        return $true
    }

    Write-Host "  Building Phase 3.1 D.2 GPT reader (Zapada.Fs.Gpt)..." -ForegroundColor Cyan
    dotnet build $gptProject -c $BuildConfig --nologo 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: dotnet build Zapada.Fs.Gpt failed." -ForegroundColor Red
        return $false
    }

    if (-not (Test-Path $gptBinDll)) {
        Write-Host "  ERROR: $gptBinDll not found after build." -ForegroundColor Red
        return $false
    }

    # Ensure build/ exists (make all creates it, but guard in case of partial clean)
    $buildDir = Join-Path (Get-Location).Path "build"
    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir | Out-Null
    }

    Copy-Item $gptBinDll $gptDest -Force
    $sz = (Get-Item $gptDest).Length
    Write-Host "  Staged: $gptDest ($sz bytes)." -ForegroundColor Gray
    return $true
}

function Invoke-Fat32Build {
    # Build Zapada.Fs.Fat32 and stage build/fat32.dll for the Phase 3.1 D.3 gate.
    # Must be called AFTER the native clean/build sequence because build artifacts can be recreated.
    # Returns true even if the project is absent (make-disk warns and skips FAT32.DLL).
    $fat32Project = Join-Path (Get-Location).Path "src\managed\Zapada.Fs.Fat32\Zapada.Fs.Fat32.csproj"
    $fat32BinDll  = Join-Path (Get-Location).Path "src\managed\Zapada.Fs.Fat32\bin\$BuildConfig\net10.0\Zapada.Fs.Fat32.dll"
    $fat32Dest    = Join-Path (Get-Location).Path "build\fat32.dll"

    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        Write-Host "  WARNING: 'dotnet' not found; Zapada.Fs.Fat32 not built; Phase 3.1 D.3 gate will not pass." -ForegroundColor Yellow
        return $true
    }

    if (-not (Test-Path $fat32Project)) {
        Write-Host "  WARNING: Zapada.Fs.Fat32.csproj not found; skipping Phase 3.1 D.3 FAT32 build." -ForegroundColor Yellow
        return $true
    }

    Write-Host "  Building Phase 3.1 D.3 FAT32 reader (Zapada.Fs.Fat32)..." -ForegroundColor Cyan
    dotnet build $fat32Project -c $BuildConfig --nologo 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: dotnet build Zapada.Fs.Fat32 failed." -ForegroundColor Red
        return $false
    }

    if (-not (Test-Path $fat32BinDll)) {
        Write-Host "  ERROR: $fat32BinDll not found after build." -ForegroundColor Red
        return $false
    }

    # Ensure build/ exists (make all creates it, but guard in case of partial clean)
    $buildDir = Join-Path (Get-Location).Path "build"
    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir | Out-Null
    }

    Copy-Item $fat32BinDll $fat32Dest -Force
    $sz = (Get-Item $fat32Dest).Length
    Write-Host "  Staged: $fat32Dest ($sz bytes)." -ForegroundColor Gray
    return $true
}

function Invoke-VfsBuild {
    # Build Zapada.Fs.Vfs and stage build/vfs.dll for the Phase 3.1 D.4 gate.
    # Must be called AFTER the native clean/build sequence because build artifacts can be recreated.
    # Returns true even if the project is absent (make-disk warns and skips VFS.DLL).
    $vfsProject = Join-Path (Get-Location).Path "src\managed\Zapada.Fs.Vfs\Zapada.Fs.Vfs.csproj"
    $vfsBinDll  = Join-Path (Get-Location).Path "src\managed\Zapada.Fs.Vfs\bin\$BuildConfig\net10.0\Zapada.Fs.Vfs.dll"
    $vfsDest    = Join-Path (Get-Location).Path "build\vfs.dll"

    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        Write-Host "  WARNING: 'dotnet' not found; Zapada.Fs.Vfs not built; Phase 3.1 D.4 gate will not pass." -ForegroundColor Yellow
        return $true
    }

    if (-not (Test-Path $vfsProject)) {
        Write-Host "  WARNING: Zapada.Fs.Vfs.csproj not found; skipping Phase 3.1 D.4 VFS build." -ForegroundColor Yellow
        return $true
    }

    Write-Host "  Building Phase 3.1 D.4 VFS layer (Zapada.Fs.Vfs)..." -ForegroundColor Cyan
    dotnet build $vfsProject -c $BuildConfig --nologo 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: dotnet build Zapada.Fs.Vfs failed." -ForegroundColor Red
        return $false
    }

    if (-not (Test-Path $vfsBinDll)) {
        Write-Host "  ERROR: $vfsBinDll not found after build." -ForegroundColor Red
        return $false
    }

    # Ensure build/ exists (make all creates it, but guard in case of partial clean)
    $buildDir = Join-Path (Get-Location).Path "build"
    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir | Out-Null
    }

    Copy-Item $vfsBinDll $vfsDest -Force
    $sz = (Get-Item $vfsDest).Length
    Write-Host "  Staged: $vfsDest ($sz bytes)." -ForegroundColor Gray
    return $true
}

function Invoke-StorageBuild {
    # Build Zapada.Storage and stage build/storage.dll for the Phase 2B storage gate.
    # Must be called AFTER the native clean/build sequence because build artifacts can be recreated.
    # Returns true even if the project is absent (make-disk warns and skips STORAGE.DLL).
    $storageProject = Join-Path (Get-Location).Path "src\managed\Zapada.Storage\Zapada.Storage.csproj"
    $storageBinDll  = Join-Path (Get-Location).Path "src\managed\Zapada.Storage\bin\$BuildConfig\net10.0\Zapada.Storage.dll"
    $storageDest    = Join-Path (Get-Location).Path "build\storage.dll"

    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        Write-Host "  WARNING: 'dotnet' not found; Zapada.Storage not built; Phase-Storage gate will not pass." -ForegroundColor Yellow
        return $true
    }

    if (-not (Test-Path $storageProject)) {
        Write-Host "  WARNING: Zapada.Storage.csproj not found; skipping Phase 2B Storage build." -ForegroundColor Yellow
        return $true
    }

    Write-Host "  Building Phase 2B Storage abstractions (Zapada.Storage)..." -ForegroundColor Cyan
    dotnet build $storageProject -c $BuildConfig --nologo 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: dotnet build Zapada.Storage failed." -ForegroundColor Red
        return $false
    }

    if (-not (Test-Path $storageBinDll)) {
        Write-Host "  ERROR: $storageBinDll not found after build." -ForegroundColor Red
        return $false
    }

    # Ensure build/ exists (make all creates it, but guard in case of partial clean)
    $buildDir = Join-Path (Get-Location).Path "build"
    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir | Out-Null
    }

    Copy-Item $storageBinDll $storageDest -Force
    $sz = (Get-Item $storageDest).Length
    Write-Host "  Staged: $storageDest ($sz bytes)." -ForegroundColor Gray
    return $true
}

function Invoke-Build {
    param([string]$ArchName, [string]$Board = "rpi3")

    if (-not (Test-Wsl)) {
        Write-Host "ERROR: WSL is required for building." -ForegroundColor Red
        exit $EXIT_FAIL
    }

    # Build the single embedded managed boot payload.
    if (-not (Invoke-ManagedBootBuild)) {
        return $false
    }

    $wslProject = ConvertTo-WslPath (Get-Location).Path

    # Ensure the drive is mounted in WSL
    if ($wslProject -match '^/mnt/([a-z])') {
        $dl    = $Matches[1]
        $mnt   = "/mnt/$dl"
        $mc    = wsl bash -c "mountpoint -q $mnt && echo mounted || echo not_mounted" 2>&1
        if ($mc -eq "not_mounted") {
            wsl bash -c "sudo mkdir -p $mnt && sudo mount -t drvfs $($dl.ToUpper()): $mnt" 2>&1 | Out-Null
        }
    }

    $debugMakeFlag = if ($DebugBuild) { " DEBUG=1" } else { "" }
    if ($ArchName -eq "x86_64") {
        $makeCmd = "make clean >/dev/null 2>&1 ; make all$debugMakeFlag"
    } else {
        $makeCmd = "make clean >/dev/null 2>&1 ; make ARCH=aarch64 BOARD=$Board all$debugMakeFlag"
    }

    Write-Host "  Building $ArchName..." -ForegroundColor Cyan
    $result = wsl bash -c "cd '$wslProject' && $makeCmd 2>&1"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  Build FAILED for $ArchName." -ForegroundColor Red
        Write-Host $result
        return $false
    }
    Write-Host "  Build OK." -ForegroundColor DarkGreen

    Restore-ManagedCacheToBuild

    # Stage build/hello.dll AFTER the native clean/build sequence.
    # make-disk uses this file as the Phase 3B Step 4 TEST.DLL payload written to FAT32.
    if (-not (Invoke-ManagedHelloBuild)) {
        return $false
    }

    # Stage build/vblk.dll AFTER the native clean/build sequence for Phase 3.1 D.1 VBLK.DLL driver gate.
    # make-disk uses this file as the VBLK.DLL payload written to ZAPADA_BOOT FAT32.
    if (-not (Invoke-VblkBuild)) {
        return $false
    }

    # Stage build/gpt.dll AFTER the native clean/build sequence for Phase 3.1 D.2 GPT.DLL driver gate.
    # make-disk uses this file as the GPT.DLL payload written to ZAPADA_BOOT FAT32.
    if (-not (Invoke-GptBuild)) {
        return $false
    }

    # Stage build/storage.dll AFTER the native clean/build sequence for Phase 2B STORAGE.DLL driver gate.
    # make-disk uses this file as the STORAGE.DLL payload written to ZAPADA_BOOT FAT32.
    if (-not (Invoke-StorageBuild)) {
        return $false
    }

    # Stage build/fat32.dll AFTER the native clean/build sequence for Phase 3.1 D.3 FAT32.DLL driver gate.
    # make-disk uses this file as the FAT32.DLL payload written to ZAPADA_BOOT FAT32.
    if (-not (Invoke-Fat32Build)) {
        return $false
    }

    # Stage build/vfs.dll AFTER the native clean/build sequence for Phase 3.1 D.4 VFS.DLL driver gate.
    # make-disk uses this file as the VFS.DLL payload written to ZAPADA_BOOT FAT32.
    if (-not (Invoke-VfsBuild)) {
        return $false
    }

    # Stage build/conf-crossasm.dll AFTER the native clean/build sequence so initramfs includes the
    # cross-assembly conformance dependency used by the new CLR gap-closure gates.
    if (-not (Invoke-ConformanceCrossAsmBuild)) {
        return $false
    }

    if (-not (Invoke-ConformanceBuild)) {
        return $false
    }

    return $true
}

# ==========================================================================
# x86_64 gate
# ==========================================================================
function Test-X86_64 {
    Write-Host ""
    Write-Host "=====================================================" -ForegroundColor White
    Write-Host " x86_64 (Phase 3A)  -  QEMU q35                    " -ForegroundColor White
    Write-Host "=====================================================" -ForegroundColor White

    if (-not $NoBuild) {
        if (-not (Invoke-Build -ArchName "x86_64")) {
            return $false
        }
    }

    # Recreate the disk image every run so Phase 3A Part 2/3 always test against
    # a known-good GPT/FAT32 image with the expected ZAPADASK signature and TEST.DLL.
    $DISK = ".\build\disk.img"
    Write-Host "  Creating VirtIO disk image for gate check..." -ForegroundColor Cyan
    & .\scripts\make-disk.ps1 -Force | Out-Null

    $ISO = ".\build\Zapada.iso"
    $LOG = ".\build\serial.log"
    $QemuStderrLog = ".\build\serial-qemu-stderr.log"

    if (Test-Path $LOG) { Remove-Item $LOG -Force }
    if (Test-Path $QemuStderrLog) { Remove-Item $QemuStderrLog -Force }

    if (-not (Test-Path $ISO)) {
        Write-Host "  ERROR: $ISO not found. Build first." -ForegroundColor Red
        return $false
    }

    $qemu    = Get-Qemu "qemu-system-x86_64.exe"
    $isoAbs  = (Resolve-Path $ISO).Path
    $logAbs  = (Resolve-Path ".\build").Path + "\serial.log"
    $diskAbs = (Resolve-Path ".\build").Path + "\disk.img"
    $args = @(
        "-machine", "q35",
        "-cpu",     "qemu64",
        "-m",       "256M",
        "-cdrom",   $isoAbs,
        "-boot",    "d",
        "-serial",  "file:$logAbs",
        "-nographic",
        "-no-reboot"
    )
    # Attach VirtIO block disk image for Phase 3A Part 2 gate check
    if (Test-Path $DISK) {
        $args += "-drive"
        $args += "file=$diskAbs,format=raw,if=none,id=d0"
        $args += "-device"
        $args += "virtio-blk-pci,drive=d0"
    }

    Write-Host "  Running QEMU (timeout: ${TimeoutSec}s)..." -ForegroundColor Gray
    $null = Invoke-QemuWithTimeout -QemuExe $qemu -QemuArgs $args `
                                   -LogPath $LOG -TimeoutMs ($TimeoutSec * 1000)

    # 53 array checks + 8 standalone checks = 61 content checks: Phase 2A/2B/2C + Phase 3.2 S1 + managed boot gates (x86_64)
    $checks  = @(
        "Phase 3A banner",
        "GDT loaded",
        "IDT loaded",
        "PMM initialized",
        "Heap probe returned",
        "Phase 2A check passed",
        "Verifier accepted assembly",
        "Phase 2B subsystems initialized",
        "Phase 2B scheduler initialized",
        "Phase 2B timer initializing",
        "Phase 2C x86_64 PIC+PIT programmed",
        "Phase 2B syscall table initialized",
        "Phase 2B IPC initialized",
        "Phase 2B process lifecycle tests passed",
        "Phase 2B scheduler tests passed",
        "Phase 2B IPC tests passed",
        "Phase 2B syscall dispatch tests passed",
        "Phase 2B all self-tests passed",
        "Phase 2B completion marker",
        "Phase 2C kstack pool initialized",
        "Phase 2C self-test count (10 pass 0 fail)",
        "Phase 2C all self-tests PASSED",
        "Phase 3.2 S1 interpreter gap self-tests gate",
        "Phase-Storage Storage found",
        "Phase-Storage Storage loaded",
        "Phase-Storage Storage initialized",
        "Phase-Storage gate",
        "Phase 3.2 Conformance found",
        "Phase 3.2 Conformance loaded",
        "Phase 3.2 Conformance OK",
        "Phase 3.2 Conformance gate",
        "Phase CrossAsm field gate",
        "Phase CrossAsm conformance gate",
        "Phase Cctor widen gate",
        "Phase 3B Zapada.Test.Hello loaded",
        "Phase 3B end-to-end gate",
        "Phase 3.1 D.1 VirtioBlock found",
        "Phase 3.1 D.1 VirtioBlock loaded",
        "Phase 3.1 D.1 VirtioBlock initialized",
        "Phase 3.1 D.1 VirtioBlock gate",
        "Phase 3.1 D.2 GPT found",
        "Phase 3.1 D.2 GPT loaded",
        "Phase 3.1 D.2 GPT initialized",
        "Phase 3.1 D.2 GPT gate",
        "Phase 3.1 D.3 FAT32 found",
        "Phase 3.1 D.3 FAT32 loaded",
        "Phase 3.1 D.3 FAT32 initialized",
        "Phase 3.1 D.4 VFS found",
        "Phase 3.1 D.4 VFS loaded",
        "Phase 3.1 D.4 VFS initialized",
        "Phase 3.1 D.4 VFS gate",
        "Boot complete gate GateD",
        "System halted after Phase 3A bring-up"
    )
    $patterns = @(
        "Zapada - Phase 3A bring-up",
        "GDT                : loaded",
        "IDT                : loaded",
        "PMM free frames    : ",
        "Heap probe         : ",
        "Phase 2A check     : native scaffolding initialized",
        "Verifier            : assembly OK",
        "Phase 2B        : initializing subsystems",
        "Scheduler       : initialized",
        "Timer           : initializing hz=",
        "Timer           : x86_64 PIC + PIT programmed",
        "Syscall         : dispatch table initialized",
        "IPC             : initialized",
        "Phase 2B: process lifecycle test",
        "Phase 2B: scheduler test",
        "Phase 2B: IPC channel test",
        "Phase 2B: syscall dispatch test",
        "Phase 2B        : all self-tests passed",
        "Phase 2B complete.",
        "KStack pool     : ",
        "Phase 2C tests: pass=10 fail=0",
        "Phase 2C        : all self-tests PASSED",
        "[Gate] Phase3.2-S1",
        "[Boot] found: Zapada.Storage",
        "[Boot] Zapada.Storage loaded",
        "[Storage] Zapada.Storage initialized",
        "[Gate] Phase-Storage",
        "[Boot] found: Zapada.Conformance",
        "[Boot] Zapada.Conformance loaded",
        "[Boot] Conformance OK",
        "[Gate] Phase3.2-Conf",
        "[Gate] Phase-CrossAsmField",
        "[Gate] Phase-CrossAsmConf",
        "[Gate] Phase-CctorWiden",
        "[Boot] invoking: Zapada.Test.Hello",
        "[Gate] Phase3B",
        "[Boot] found: Zapada.Drivers.VirtioBlock",
        "[Boot] Zapada.Drivers.VirtioBlock loaded",
        "[Boot] VirtioBlock driver initialized",
        "[Gate] Phase31-D1",
        "[Boot] found: Zapada.Fs.Gpt",
        "[Boot] Zapada.Fs.Gpt loaded",
        "[Boot] GPT driver initialized",
        "[Gate] Phase31-D2",
        "[Boot] found: Zapada.Fs.Fat32",
        "[Boot] Zapada.Fs.Fat32 loaded",
        "[Boot] FAT32 driver initialized",
        "[Boot] found: Zapada.Fs.Vfs",
        "[Boot] Zapada.Fs.Vfs loaded",
        "[Boot] VFS initialized",
        "[Gate] Phase31-D4",
        "[Gate] GateD",
        "System halted after successful Phase 3A bring-up."
    )

    # Negative check: ensure no test failures in the log
    $content = if (Test-Path $LOG) { Get-Content $LOG -Raw } else { "" }
    $allPass = $true

    $noFail = ($null -ne $content) -and (-not $content.Contains("[FAIL]"))
    if (-not (Write-Check -Label "No self-test failures (FAIL marker absent)" -Pass ([bool]$noFail))) { $allPass = $false }
    if (-not $noFail) { Show-SerialFailLines -LogPath $LOG }

    # Phase 2B test count must still pass (58 tests)
    $p2bCount = ($null -ne $content) -and $content.Contains("pass=58 fail=0")
    if (-not (Write-Check -Label "Phase 2B self-test count (58 pass, 0 fail)" -Pass ([bool]$p2bCount))) { $allPass = $false }

    # Phase-Storage gate line check (Zapada.Storage initialized)
    $pStorageGate = ($null -ne $content) -and $content.Contains("[Gate] Phase-Storage")
    if (-not (Write-Check -Label "Phase-Storage gate line present" -Pass ([bool]$pStorageGate))) { $allPass = $false }

    # Phase 3.1 D.1 gate line check (VirtioBlock DllMain.Initialize executed)
    $p31d1Gate = ($null -ne $content) -and $content.Contains("[Gate] Phase31-D1")
    if (-not (Write-Check -Label "Phase 3.1 D.1 VirtioBlock gate line present" -Pass ([bool]$p31d1Gate))) { $allPass = $false }

    # Phase 3.1 D.2 gate line check (GPT DllMain.Initialize executed)
    $p31d2Gate = ($null -ne $content) -and $content.Contains("[Gate] Phase31-D2")
    if (-not (Write-Check -Label "Phase 3.1 D.2 GPT gate line present" -Pass ([bool]$p31d2Gate))) { $allPass = $false }

    # Phase 3.1 D.4 gate line check (VFS DllMain.Initialize executed)
    $p31d4Gate = ($null -ne $content) -and $content.Contains("[Gate] Phase31-D4")
    if (-not (Write-Check -Label "Phase 3.1 D.4 VFS gate line present" -Pass ([bool]$p31d4Gate))) { $allPass = $false }

    # VFS bootstrap must not also report an initialization failure.
    $vfsInitFailed = ($null -ne $content) -and $content.Contains("[Boot] VFS Initialize failed")
    if (-not (Write-Check -Label "Phase 3.1 D.4 VFS bootstrap succeeded" -Pass (-not $vfsInitFailed) -Detail "Log contains: [Boot] VFS Initialize failed")) { $allPass = $false }

    # Phase 3.2 S1 gate line check (interpreter gap self-tests passed)
    $p32s1Gate = ($null -ne $content) -and $content.Contains("[Gate] Phase3.2-S1")
    if (-not (Write-Check -Label "Phase 3.2 S1 interpreter gap tests gate" -Pass ([bool]$p32s1Gate))) { $allPass = $false }

    # Phase 3.2 Conf gate line check (IL conformance tests all passed)
    $p32confGate = ($null -ne $content) -and $content.Contains("[Gate] Phase3.2-Conf")
    if (-not (Write-Check -Label "Phase 3.2 Conformance gate line present" -Pass ([bool]$p32confGate))) { $allPass = $false }

    $serialPass = Test-SerialLog -LogPath $LOG -Checks $checks -Patterns $patterns
    return ($allPass -and $serialPass)
}

# ==========================================================================
# AArch64 gate
# ==========================================================================
function Test-AArch64 {
    Write-Host ""
    Write-Host "=====================================================" -ForegroundColor White
    Write-Host " AArch64 (Phase 3A)  -  QEMU virt                   " -ForegroundColor White
    Write-Host "=====================================================" -ForegroundColor White

    if (-not $NoBuild) {
        if (-not (Invoke-Build -ArchName "aarch64" -Board "virt")) {
            return $false
        }
    }

    # Recreate the disk image every run so Phase 3A Part 2/3 always test against
    # a known-good GPT/FAT32 image with the expected ZAPADASK signature and TEST.DLL.
    $DISK = ".\build\disk.img"
    Write-Host "  Creating VirtIO disk image for gate check..." -ForegroundColor Cyan
    & .\scripts\make-disk.ps1 -Force | Out-Null

    $IMG = ".\build\aarch64\kernel8.img"
    $LOG = ".\build\aarch64\serial.log"
    $INITRD = ".\build\initramfs.cpio.gz"
    $ProbeLog = ".\build\aarch64\serial-probe.log"
    $QemuStderrLog = ".\build\aarch64\serial-qemu-stderr.log"
    $ProbeQemuStderrLog = ".\build\aarch64\serial-probe-qemu-stderr.log"

    if (Test-Path $LOG) { Remove-Item $LOG -Force }
    if (Test-Path $ProbeLog) { Remove-Item $ProbeLog -Force }
    if (Test-Path $QemuStderrLog) { Remove-Item $QemuStderrLog -Force }
    if (Test-Path $ProbeQemuStderrLog) { Remove-Item $ProbeQemuStderrLog -Force }

    if (-not (Test-Path $IMG)) {
        Write-Host "  ERROR: $IMG not found. Build first." -ForegroundColor Red
        return $false
    }

    $qemu    = Get-Qemu "qemu-system-aarch64.exe"
    $imgAbs  = (Resolve-Path $IMG).Path
    $logAbs  = (Resolve-Path ".\build\aarch64").Path + "\serial.log"
    $diskAbs = (Resolve-Path ".\build").Path + "\disk.img"
    $initrdAbs = if (Test-Path $INITRD) { (Resolve-Path $INITRD).Path } else { $null }

    # -----------------------------------------------------------------
    # DIAGNOSTIC: bare-boot probe — run WITHOUT disk to test whether
    # the kernel itself boots and produces serial output.
    # This isolates whether the empty serial log is caused by a QEMU
    # PCIe device configuration issue, or by the kernel crashing early.
    # -----------------------------------------------------------------
    Write-Host "  [Diag] Bare-boot probe (no disk, no PCIe device)..." -ForegroundColor Cyan
    $probeLogAbs = (Resolve-Path ".\build\aarch64").Path + "\serial-probe.log"
    $probeArgs = @(
        "-M",       "virt",
        "-cpu",     "cortex-a72",
        "-kernel",  $imgAbs,
        "-m",       "1024M",
        "-global",  "virtio-mmio.force-legacy=false",
        "-serial",  "file:$probeLogAbs",
        "-display", "none",
        "-no-reboot"
    )
    if ($null -ne $initrdAbs) {
        $probeArgs += "-initrd"
        $probeArgs += $initrdAbs
    }
    $probeExit = Invoke-QemuWithTimeout -QemuExe $qemu -QemuArgs $probeArgs `
                                        -LogPath $probeLogAbs -TimeoutMs ($TimeoutSec * 1000)
    $probeContent = if (Test-Path $probeLogAbs) { Get-Content $probeLogAbs -Raw } else { $null }
    $probeHasOutput = ($null -ne $probeContent) -and ($probeContent.Trim().Length -gt 0)
    if ($probeHasOutput) {
        Write-Host "  [Diag] PASS: kernel produces serial output without PCIe device." -ForegroundColor DarkGreen
        Write-Host "         => Root cause: virtio-blk-pci attachment is crashing the QEMU virt machine." -ForegroundColor Yellow
    } else {
        Write-Host "  [Diag] FAIL: kernel produces NO serial output even without PCIe device." -ForegroundColor DarkRed
        Write-Host "         => Root cause: kernel binary or QEMU boot issue (NOT the disk/PCIe device)." -ForegroundColor Yellow
    }
    # Show first line of probe output for quick confirmation
    if ($probeContent) {
        $firstLine = ($probeContent -split "`n" | Where-Object { $_.Trim().Length -gt 0 } | Select-Object -First 1)
        if ($firstLine) {
            Write-Host "  [Diag] First serial line: $firstLine" -ForegroundColor Gray
        }
    }

    $args = @(
        "-M",       "virt",
        "-cpu",     "cortex-a72",
        "-kernel",  $imgAbs,
        "-m",       "1024M",
        "-global",  "virtio-mmio.force-legacy=false",
        "-serial",  "file:$logAbs",
        "-display", "none",
        "-no-reboot"
    )
    if ($null -ne $initrdAbs) {
        $args += "-initrd"
        $args += $initrdAbs
    }
    # Attach disk via QEMU virt's built-in virtio-mmio transport.
    if (Test-Path $DISK) {
        $args += "-drive"
        $args += "if=none,file=$diskAbs,format=raw,id=d0"
        $args += "-device"
        $args += "virtio-blk-device,drive=d0"
    }

    Write-Host "  Running QEMU (timeout: ${TimeoutSec}s)..." -ForegroundColor Gray
    $null = Invoke-QemuWithTimeout -QemuExe $qemu -QemuArgs $args `
                                   -LogPath $LOG -TimeoutMs ($TimeoutSec * 1000)

    # 53 array checks + 8 standalone checks = 61 content checks: Phase 2A/2B/2C + Phase 3.2 S1 + managed boot gates (AArch64)
    $checks  = @(
        "Phase 3A AArch64 banner",
        "Running at EL1",
        "Exception vectors installed",
        "PMM initialized",
        "Heap initialized",
        "Phase 2A AArch64 check passed",
        "Verifier accepted assembly",
        "Phase 2B subsystems initialized",
        "Phase 2B scheduler initialized",
        "Phase 2B timer initializing",
        "Phase 2C AArch64 generic timer programmed",
        "Phase 2B syscall table initialized",
        "Phase 2B IPC initialized",
        "Phase 2B process lifecycle tests",
        "Phase 2B scheduler tests",
        "Phase 2B IPC tests",
        "Phase 2B syscall dispatch tests",
        "Phase 2B all self-tests passed",
        "Phase 2B completion marker",
        "Phase 2C kstack pool initialized",
        "Phase 2C self-test count (10 pass 0 fail)",
        "Phase 2C all self-tests PASSED",
        "Phase 3.2 S1 interpreter gap self-tests gate",
        "Phase-Storage Storage found",
        "Phase-Storage Storage loaded",
        "Phase-Storage Storage initialized",
        "Phase-Storage gate",
        "Phase 3.2 Conformance found",
        "Phase 3.2 Conformance loaded",
        "Phase 3.2 Conformance OK",
        "Phase 3.2 Conformance gate",
        "Phase CrossAsm field gate",
        "Phase CrossAsm conformance gate",
        "Phase Cctor widen gate",
        "Phase 3B Zapada.Test.Hello loaded",
        "Phase 3B end-to-end gate",
        "Phase 3.1 D.1 VirtioBlock found",
        "Phase 3.1 D.1 VirtioBlock loaded",
        "Phase 3.1 D.1 VirtioBlock initialized",
        "Phase 3.1 D.1 VirtioBlock gate",
        "Phase 3.1 D.2 GPT found",
        "Phase 3.1 D.2 GPT loaded",
        "Phase 3.1 D.2 GPT initialized",
        "Phase 3.1 D.2 GPT gate",
        "Phase 3.1 D.3 FAT32 found",
        "Phase 3.1 D.3 FAT32 loaded",
        "Phase 3.1 D.3 FAT32 initialized",
        "Phase 3.1 D.4 VFS found",
        "Phase 3.1 D.4 VFS loaded",
        "Phase 3.1 D.4 VFS initialized",
        "Phase 3.1 D.4 VFS gate",
        "Boot complete gate GateD",
        "System halted after Phase 3A bring-up"
    )
    $patterns = @(
        "Zapada - Phase 3A AArch64 bring-up",
        "Exception level    : EL1",
        "VBAR_EL1           : exception vectors installed",
        "PMM free frames    : ",
        "Heap init          : OK",
        "Phase 2A check     :",
        "Verifier            : assembly OK",
        "Phase 2B        : initializing subsystems",
        "Scheduler       : initialized",
        "Timer           : initializing hz=",
        "Timer           : AArch64 generic timer programmed",
        "Syscall         : dispatch table initialized",
        "IPC             : initialized",
        "Phase 2B: process lifecycle test",
        "Phase 2B: scheduler test",
        "Phase 2B: IPC channel test",
        "Phase 2B: syscall dispatch test",
        "Phase 2B        : all self-tests passed",
        "Phase 2B complete.",
        "KStack pool     : ",
        "Phase 2C tests: pass=10 fail=0",
        "Phase 2C        : all self-tests PASSED",
        "[Gate] Phase3.2-S1",
        "[Boot] found: Zapada.Storage",
        "[Boot] Zapada.Storage loaded",
        "[Storage] Zapada.Storage initialized",
        "[Gate] Phase-Storage",
        "[Boot] found: Zapada.Conformance",
        "[Boot] Zapada.Conformance loaded",
        "[Boot] Conformance OK",
        "[Gate] Phase3.2-Conf",
        "[Gate] Phase-CrossAsmField",
        "[Gate] Phase-CrossAsmConf",
        "[Gate] Phase-CctorWiden",
        "[Boot] invoking: Zapada.Test.Hello",
        "[Gate] Phase3B",
        "[Boot] found: Zapada.Drivers.VirtioBlock",
        "[Boot] Zapada.Drivers.VirtioBlock loaded",
        "[Boot] VirtioBlock driver initialized",
        "[Gate] Phase31-D1",
        "[Boot] found: Zapada.Fs.Gpt",
        "[Boot] Zapada.Fs.Gpt loaded",
        "[Boot] GPT driver initialized",
        "[Gate] Phase31-D2",
        "[Boot] found: Zapada.Fs.Fat32",
        "[Boot] Zapada.Fs.Fat32 loaded",
        "[Boot] FAT32 driver initialized",
        "[Boot] found: Zapada.Fs.Vfs",
        "[Boot] Zapada.Fs.Vfs loaded",
        "[Boot] VFS initialized",
        "[Gate] Phase31-D4",
        "[Gate] GateD",
        "System halted after successful Phase 3A bring-up."
    )

    # Negative check: ensure no test failures in the log
    $content = if (Test-Path $LOG) { Get-Content $LOG -Raw } else { "" }
    $allPass = $true

    $noFail = ($null -ne $content) -and (-not $content.Contains("[FAIL]"))
    if (-not (Write-Check -Label "No self-test failures (FAIL marker absent)" -Pass ([bool]$noFail))) { $allPass = $false }
    if (-not $noFail) { Show-SerialFailLines -LogPath $LOG }

    # Phase 2B test count must still pass (58 tests)
    $p2bCount = ($null -ne $content) -and $content.Contains("pass=58 fail=0")
    if (-not (Write-Check -Label "Phase 2B self-test count (58 pass, 0 fail)" -Pass ([bool]$p2bCount))) { $allPass = $false }

    # Phase-Storage gate line check (Zapada.Storage initialized)
    $pStorageGate = ($null -ne $content) -and $content.Contains("[Gate] Phase-Storage")
    if (-not (Write-Check -Label "Phase-Storage gate line present" -Pass ([bool]$pStorageGate))) { $allPass = $false }

    # Phase 3.1 D.1 gate line check (VirtioBlock DllMain.Initialize executed)
    $p31d1Gate = ($null -ne $content) -and $content.Contains("[Gate] Phase31-D1")
    if (-not (Write-Check -Label "Phase 3.1 D.1 VirtioBlock gate line present" -Pass ([bool]$p31d1Gate))) { $allPass = $false }

    # Phase 3.1 D.2 gate line check (GPT DllMain.Initialize executed)
    $p31d2Gate = ($null -ne $content) -and $content.Contains("[Gate] Phase31-D2")
    if (-not (Write-Check -Label "Phase 3.1 D.2 GPT gate line present" -Pass ([bool]$p31d2Gate))) { $allPass = $false }

    # Phase 3.1 D.4 gate line check (VFS DllMain.Initialize executed)
    $p31d4Gate = ($null -ne $content) -and $content.Contains("[Gate] Phase31-D4")
    if (-not (Write-Check -Label "Phase 3.1 D.4 VFS gate line present" -Pass ([bool]$p31d4Gate))) { $allPass = $false }

    # VFS bootstrap must not also report an initialization failure.
    $vfsInitFailed = ($null -ne $content) -and $content.Contains("[Boot] VFS Initialize failed")
    if (-not (Write-Check -Label "Phase 3.1 D.4 VFS bootstrap succeeded" -Pass (-not $vfsInitFailed) -Detail "Log contains: [Boot] VFS Initialize failed")) { $allPass = $false }

    # Phase 3.2 S1 gate line check (interpreter gap self-tests passed)
    $p32s1Gate = ($null -ne $content) -and $content.Contains("[Gate] Phase3.2-S1")
    if (-not (Write-Check -Label "Phase 3.2 S1 interpreter gap tests gate" -Pass ([bool]$p32s1Gate))) { $allPass = $false }

    # Phase 3.2 Conf gate line check (IL conformance tests all passed)
    $p32confGate = ($null -ne $content) -and $content.Contains("[Gate] Phase3.2-Conf")
    if (-not (Write-Check -Label "Phase 3.2 Conformance gate line present" -Pass ([bool]$p32confGate))) { $allPass = $false }

    $serialPass = Test-SerialLog -LogPath $LOG -Checks $checks -Patterns $patterns
    return ($allPass -and $serialPass)
}

# ==========================================================================
# Main
# ==========================================================================

Write-Host ""
Write-Host "Zapada test-all.ps1" -ForegroundColor Cyan
Write-Host "------------------" -ForegroundColor Cyan

$DiagRoot = ".\diag"

$runX86  = ($Arch -eq "all") -or ($Arch -eq "x86_64")
$runAA64 = ($Arch -eq "all") -or ($Arch -eq "aarch64")

if ($runX86) {
    $ok = Test-X86_64
    if (-not $ok) { $overallResult = $EXIT_FAIL }

    if (Test-Path ".\build\serial.log") {
        New-Item -ItemType Directory -Force -Path $DiagRoot | Out-Null
        Copy-Item ".\build\serial.log" (Join-Path $DiagRoot "x86_64-serial.log") -Force
    }
    if (Test-Path ".\build\serial-qemu-stderr.log") {
        New-Item -ItemType Directory -Force -Path $DiagRoot | Out-Null
        Copy-Item ".\build\serial-qemu-stderr.log" (Join-Path $DiagRoot "x86_64-serial-qemu-stderr.log") -Force
    }

    Write-Host ""
    if ($ok) {
        Write-Host "  x86_64 : PASS" -ForegroundColor Green
    } else {
        Write-Host "  x86_64 : FAIL" -ForegroundColor Red
    }
}

if ($runAA64) {
    $ok = Test-AArch64
    if (-not $ok) { $overallResult = $EXIT_FAIL }

    if (Test-Path ".\build\aarch64\serial.log") {
        New-Item -ItemType Directory -Force -Path $DiagRoot | Out-Null
        Copy-Item ".\build\aarch64\serial.log" (Join-Path $DiagRoot "aarch64-serial.log") -Force
    }
    if (Test-Path ".\build\aarch64\serial-probe.log") {
        New-Item -ItemType Directory -Force -Path $DiagRoot | Out-Null
        Copy-Item ".\build\aarch64\serial-probe.log" (Join-Path $DiagRoot "aarch64-serial-probe.log") -Force
    }
    if (Test-Path ".\build\aarch64\serial-qemu-stderr.log") {
        New-Item -ItemType Directory -Force -Path $DiagRoot | Out-Null
        Copy-Item ".\build\aarch64\serial-qemu-stderr.log" (Join-Path $DiagRoot "aarch64-serial-qemu-stderr.log") -Force
    }
    if (Test-Path ".\build\aarch64\serial-probe-qemu-stderr.log") {
        New-Item -ItemType Directory -Force -Path $DiagRoot | Out-Null
        Copy-Item ".\build\aarch64\serial-probe-qemu-stderr.log" (Join-Path $DiagRoot "aarch64-serial-probe-qemu-stderr.log") -Force
    }

    Write-Host ""
    if ($ok) {
        Write-Host "  AArch64: PASS" -ForegroundColor Green
    } else {
        Write-Host "  AArch64: FAIL" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "=====================================================" -ForegroundColor White
if ($overallResult -eq $EXIT_PASS) {
    Write-Host " ALL TESTS PASSED" -ForegroundColor Green
} else {
    Write-Host " SOME TESTS FAILED" -ForegroundColor Red
}
Write-Host "=====================================================" -ForegroundColor White
Write-Host ""

exit $overallResult


