# Zapada - build.ps1
# Build the Zapada kernel for x86_64 or AArch64.
#
# This script delegates compilation to WSL (Windows Subsystem for Linux)
# because the required tools are Linux-native.
#
# Usage:
#   .\build.ps1 -Arch x86_64            # x86_64 build (PCs, VMs)
#   .\build.ps1 -Arch aarch64           # AArch64 build (QEMU virt)
#   .\build.ps1 -Arch aarch64 -Board rpi3  # AArch64 for RPi 3/3+/3A+
#   .\build.ps1 -Arch aarch64 -Board rpi4  # AArch64 for RPi 4B / CM4
#   .\build.ps1 -Arch x86_64 -Run       # build then run in QEMU (x86_64)
#   .\build.ps1 -Arch aarch64 -Run      # build then run in QEMU (AArch64)
#   .\build.ps1 -Arch x86_64 -SkipDotnet # skip all managed `dotnet publish` steps and reuse staged DLLs
#   .\build.ps1 -Arch x86_64 -NativeClean # run `make clean` before native build
#   .\build.ps1 -Clean                  # clean ALL build artifacts
#
# Requirements in WSL:
#   x86_64: sudo apt install build-essential nasm grub-pc-bin grub-common xorriso
#   aarch64: sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("x86_64", "aarch64")]
    [string]$Arch,

    [ValidateSet("virt", "rpi3", "rpi4")]
    [string]$Board = "virt",

    [switch]$Run,    # build then immediately run in QEMU
    [switch]$Clean,  # clean all build artifacts

    [switch]$SkipDotnet, # skip managed `dotnet publish` steps and reuse existing staged DLLs

    [switch]$NativeClean # run `make clean` before the native make step
)

$dotnetBuildEnabled = -not $SkipDotnet
$managedCacheRoot = Join-Path (Get-Location).Path ".build-cache\managed"

function Stop-DotnetBuildServers {
    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        return
    }

    dotnet build-server shutdown 2>&1 | Out-Null
}

# --------------------------------------------------------------------------
# Detect WSL availability
# --------------------------------------------------------------------------
function Test-Wsl {
    try {
        $null = & wsl --status 2>&1
        return $true
    } catch {
        return $false
    }
}

if (-not (Test-Wsl)) {
    Write-Host "ERROR: WSL is not available." -ForegroundColor Red
    Write-Host "Install WSL: wsl --install" -ForegroundColor Yellow
    exit 1
}

# --------------------------------------------------------------------------
# Convert Windows path to WSL path
# --------------------------------------------------------------------------
function ConvertTo-WslPath([string]$winPath) {
    $abs = Resolve-Path $winPath -ErrorAction SilentlyContinue
    if ($abs) {
        $winPath = $abs.Path
    }
    $winPath = $winPath -replace '\\', '/'
    if ($winPath -match '^([A-Za-z]):(.*)') {
        $drive = $Matches[1].ToLower()
        $rest  = $Matches[2]
        return "/mnt/$drive$rest"
    }
    return $winPath
}

$wslProject = ConvertTo-WslPath (Get-Location).Path

# --------------------------------------------------------------------------
# Mount the Windows drive in WSL if needed (for BitLocker drives like S:)
# --------------------------------------------------------------------------
if ($wslProject -match '^/mnt/([a-z])(?:/|$)') {
    $driveLetter = $Matches[1]
    $wslDrive    = "/mnt/$driveLetter"

    $mountCheck = wsl bash -c "mountpoint -q $wslDrive && echo mounted || echo not_mounted" 2>&1
    if ($mountCheck -eq "not_mounted") {
        Write-Host "Mounting $wslDrive in WSL..." -ForegroundColor Yellow
        wsl bash -c "sudo mkdir -p $wslDrive && sudo mount -t drvfs $($driveLetter.ToUpper()): $wslDrive 2>&1"
        if ($LASTEXITCODE -ne 0) {
            Write-Host "WARNING: Could not mount $wslDrive. Build may fail." -ForegroundColor Yellow
        }
    }
}

# --------------------------------------------------------------------------
# Clean
# --------------------------------------------------------------------------
if ($Clean) {
    Write-Host "Cleaning all build artifacts..." -ForegroundColor Cyan
    wsl bash -c "cd '$wslProject' && make clean"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Clean failed." -ForegroundColor Red
        exit 1
    }
    # Remove Zapada.Boot publish output.
    $bootPublishDir = Join-Path (Get-Location).Path "src\managed\Zapada.Boot\publish"
    if (Test-Path $bootPublishDir) {
        Remove-Item $bootPublishDir -Recurse -Force
        Write-Host "  Removed src/managed/Zapada.Boot/publish/" -ForegroundColor Gray
    }
    Write-Host "Clean complete." -ForegroundColor Green
    exit 0
}

function Publish-ManagedAssembly {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DisplayName,

        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [Parameter(Mandatory = $true)]
        [string]$PublishOut,

        [Parameter(Mandatory = $true)]
        [string]$BuiltDllPath,

        [Parameter(Mandatory = $true)]
        [string]$StagedDllPath,

        [Parameter(Mandatory = $true)]
        [string]$CacheDllPath,

        [switch]$Required,

        [string]$MissingDotnetMessage,

        [string]$MissingProjectMessage,

        [string]$PublishFailureMessage
    )

    Write-Host "Building $DisplayName..." -ForegroundColor Cyan

    $stagedDir = Split-Path $StagedDllPath -Parent
    $cacheDir = Split-Path $CacheDllPath -Parent
    if (-not (Test-Path $stagedDir)) {
        New-Item -ItemType Directory -Force -Path $stagedDir | Out-Null
    }
    if (-not (Test-Path $cacheDir)) {
        New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null
    }

    if (-not $dotnetBuildEnabled) {
        $recoveryDllPath = $null

        if (Test-Path $CacheDllPath) {
            Copy-Item $CacheDllPath $StagedDllPath -Force
            $existingSize = (Get-Item $StagedDllPath).Length
            Write-Host "  Skipped dotnet: restored cache $CacheDllPath -> $StagedDllPath ($existingSize bytes)" -ForegroundColor Yellow
        } elseif (Test-Path $StagedDllPath) {
            $existingSize = (Get-Item $StagedDllPath).Length
            Write-Host "  Skipped dotnet: reusing $StagedDllPath ($existingSize bytes)" -ForegroundColor Yellow
        } elseif (Test-Path $BuiltDllPath) {
            Copy-Item $BuiltDllPath $StagedDllPath -Force
            Copy-Item $BuiltDllPath $CacheDllPath -Force
            $existingSize = (Get-Item $StagedDllPath).Length
            Write-Host "  Skipped dotnet: restaged from $BuiltDllPath to $StagedDllPath ($existingSize bytes)" -ForegroundColor Yellow
        } else {
            $projectDir = Split-Path $ProjectPath -Parent
            $dllName = Split-Path $StagedDllPath -Leaf
            $recoveryDllPath = Get-ChildItem -Path $projectDir -Filter $dllName -Recurse -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending |
                Select-Object -First 1 -ExpandProperty FullName

            if ($recoveryDllPath) {
                Copy-Item $recoveryDllPath $StagedDllPath -Force
                Copy-Item $recoveryDllPath $CacheDllPath -Force
                $existingSize = (Get-Item $StagedDllPath).Length
                Write-Host "  Skipped dotnet: recovered $recoveryDllPath -> $StagedDllPath ($existingSize bytes)" -ForegroundColor Yellow
            } elseif ($Required) {
                Write-Host "ERROR: build.ps1 was run with -SkipDotnet but required staged DLL is missing: $StagedDllPath" -ForegroundColor Red
                exit 1
            } else {
                Write-Host "  WARNING: build.ps1 was run with -SkipDotnet and no staged DLL exists at $StagedDllPath" -ForegroundColor Yellow
            }
        }

        Write-Host ""
        return
    }

    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        if ($Required) {
            Write-Host $MissingDotnetMessage -ForegroundColor Red
            exit 1
        }

        Write-Host $MissingDotnetMessage -ForegroundColor Yellow
        Write-Host ""
        return
    }

    if (-not (Test-Path $ProjectPath)) {
        if ($Required) {
            Write-Host $MissingProjectMessage -ForegroundColor Red
            exit 1
        }

        Write-Host $MissingProjectMessage -ForegroundColor Yellow
        Write-Host ""
        return
    }

    Stop-DotnetBuildServers
    dotnet publish $ProjectPath -c Release -o $PublishOut --nologo --disable-build-servers /nodeReuse:false /p:UseSharedCompilation=false 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host $PublishFailureMessage -ForegroundColor Red
        exit 1
    }
    Stop-DotnetBuildServers

    if (-not (Test-Path $BuiltDllPath)) {
        Write-Host "ERROR: Expected output not found: $BuiltDllPath" -ForegroundColor Red
        exit 1
    }

    Copy-Item $BuiltDllPath $StagedDllPath -Force
    Copy-Item $BuiltDllPath $CacheDllPath -Force
    $builtSize = (Get-Item $StagedDllPath).Length
    Write-Host "  Staged: $StagedDllPath ($builtSize bytes)" -ForegroundColor Gray
    Write-Host ""
}

function Restore-ManagedCacheToBuild {
    $cacheFiles = @(
        @{ Cache = (Join-Path $managedCacheRoot "Zapada.Boot.dll"); Target = (Join-Path (Get-Location).Path "build\boot.dll"); Required = $true },
        @{ Cache = (Join-Path $managedCacheRoot "Zapada.Test.Hello.dll"); Target = (Join-Path (Get-Location).Path "build\hello.dll"); Required = $false },
        @{ Cache = (Join-Path $managedCacheRoot "Zapada.Drivers.VirtioBlock.dll"); Target = (Join-Path (Get-Location).Path "build\vblk.dll"); Required = $false },
        @{ Cache = (Join-Path $managedCacheRoot "Zapada.Fs.Gpt.dll"); Target = (Join-Path (Get-Location).Path "build\gpt.dll"); Required = $false },
        @{ Cache = (Join-Path $managedCacheRoot "Zapada.Fs.Fat32.dll"); Target = (Join-Path (Get-Location).Path "build\fat32.dll"); Required = $false },
        @{ Cache = (Join-Path $managedCacheRoot "Zapada.Storage.dll"); Target = (Join-Path (Get-Location).Path "build\storage.dll"); Required = $false },
        @{ Cache = (Join-Path $managedCacheRoot "Zapada.Fs.Vfs.dll"); Target = (Join-Path (Get-Location).Path "build\vfs.dll"); Required = $false },
        @{ Cache = (Join-Path $managedCacheRoot "Zapada.Conformance.CrossAsm.dll"); Target = (Join-Path (Get-Location).Path "build\conf-crossasm.dll"); Required = $false },
        @{ Cache = (Join-Path $managedCacheRoot "Zapada.Conformance.dll"); Target = (Join-Path (Get-Location).Path "build\conf.dll"); Required = $false }
    )

    foreach ($entry in $cacheFiles) {
        $targetDir = Split-Path $entry.Target -Parent
        if (-not (Test-Path $targetDir)) {
            New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
        }

        if (Test-Path $entry.Cache) {
            Copy-Item $entry.Cache $entry.Target -Force
        } elseif ($entry.Required) {
            Write-Host "ERROR: required managed cache entry missing: $($entry.Cache)" -ForegroundColor Red
            exit 1
        }
    }
}

function Save-ExistingManagedArtifactsToCache {
    $optionalEntries = @(
        @{ Source = (Join-Path (Get-Location).Path "src\managed\Zapada.Conformance.CrossAsm\bin\Release\net10.0\Zapada.Conformance.CrossAsm.dll"); Cache = (Join-Path $managedCacheRoot "Zapada.Conformance.CrossAsm.dll") },
        @{ Source = (Join-Path (Get-Location).Path "src\managed\Zapada.Conformance.CrossAsm\obj\Release\net10.0\Zapada.Conformance.CrossAsm.dll"); Cache = (Join-Path $managedCacheRoot "Zapada.Conformance.CrossAsm.dll") },
        @{ Source = (Join-Path (Get-Location).Path "src\managed\Zapada.Conformance\bin\Release\net10.0\Zapada.Conformance.dll"); Cache = (Join-Path $managedCacheRoot "Zapada.Conformance.dll") },
        @{ Source = (Join-Path (Get-Location).Path "src\managed\Zapada.Conformance\obj\Release\net10.0\Zapada.Conformance.dll"); Cache = (Join-Path $managedCacheRoot "Zapada.Conformance.dll") }
    )

    foreach ($entry in $optionalEntries) {
        $cacheDir = Split-Path $entry.Cache -Parent
        if (-not (Test-Path $cacheDir)) {
            New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null
        }

        if ((-not (Test-Path $entry.Cache)) -and (Test-Path $entry.Source)) {
            Copy-Item $entry.Source $entry.Cache -Force
        }
    }
}

if ($SkipDotnet) {
    Save-ExistingManagedArtifactsToCache
    Write-Host "Skipping all managed dotnet publish steps; reusing staged DLLs from build/." -ForegroundColor Yellow
    Write-Host ""
} else {
    Stop-DotnetBuildServers
}

Publish-ManagedAssembly `
    -DisplayName "managed core library (System.Console)" `
    -ProjectPath (Join-Path (Get-Location).Path "src\managed\System.Console\System.Console.csproj") `
    -PublishOut (Join-Path (Get-Location).Path "build\console-out") `
    -BuiltDllPath (Join-Path (Join-Path (Get-Location).Path "build\console-out") "System.Console.dll") `
    -StagedDllPath (Join-Path (Get-Location).Path "build\System.Console.dll") `
    -CacheDllPath (Join-Path $managedCacheRoot "System.Console.dll") `
    -Required `
    -MissingDotnetMessage "ERROR: 'dotnet' not found. System.Console cannot be built." `
    -MissingProjectMessage "ERROR: System.Console.csproj not found at $(Join-Path (Get-Location).Path "src\managed\System.Console\System.Console.csproj")" `
    -PublishFailureMessage "ERROR: dotnet publish System.Console failed."

Publish-ManagedAssembly `
    -DisplayName "managed assembly (Zapada.Boot)" `
    -ProjectPath (Join-Path (Get-Location).Path "src\managed\Zapada.Boot\Zapada.Boot.csproj") `
    -PublishOut (Join-Path (Get-Location).Path "src\managed\Zapada.Boot\publish") `
    -BuiltDllPath (Join-Path (Join-Path (Get-Location).Path "src\managed\Zapada.Boot\publish") "Zapada.Boot.dll") `
    -StagedDllPath (Join-Path (Get-Location).Path "build\boot.dll") `
    -CacheDllPath (Join-Path $managedCacheRoot "Zapada.Boot.dll") `
    -Required `
    -MissingDotnetMessage "ERROR: 'dotnet' not found. Install .NET SDK from https://dot.net" `
    -MissingProjectMessage "ERROR: Zapada.Boot.csproj not found at $(Join-Path (Get-Location).Path "src\managed\Zapada.Boot\Zapada.Boot.csproj")" `
    -PublishFailureMessage "ERROR: dotnet publish Zapada.Boot failed."

Publish-ManagedAssembly `
    -DisplayName "managed test fixture (Zapada.Test.Hello)" `
    -ProjectPath (Join-Path (Get-Location).Path "src\managed\Zapada.Test.Hello\Zapada.Test.Hello.csproj") `
    -PublishOut (Join-Path (Get-Location).Path "build\hello-out") `
    -BuiltDllPath (Join-Path (Join-Path (Get-Location).Path "build\hello-out") "Zapada.Test.Hello.dll") `
    -StagedDllPath (Join-Path (Get-Location).Path "build\hello.dll") `
    -CacheDllPath (Join-Path $managedCacheRoot "Zapada.Test.Hello.dll") `
    -MissingDotnetMessage "  WARNING: 'dotnet' not found. Zapada.Test.Hello not built; Zapada.Test.Hello.dll will be a stub." `
    -MissingProjectMessage "  WARNING: Zapada.Test.Hello.csproj not found at $(Join-Path (Get-Location).Path "src\managed\Zapada.Test.Hello\Zapada.Test.Hello.csproj"); skipping." `
    -PublishFailureMessage "ERROR: dotnet publish Zapada.Test.Hello failed."

Publish-ManagedAssembly `
    -DisplayName "VirtioBlock driver (Zapada.Drivers.VirtioBlock)" `
    -ProjectPath (Join-Path (Get-Location).Path "src\managed\Zapada.Drivers.VirtioBlock\Zapada.Drivers.VirtioBlock.csproj") `
    -PublishOut (Join-Path (Get-Location).Path "build\vblk-out") `
    -BuiltDllPath (Join-Path (Join-Path (Get-Location).Path "build\vblk-out") "Zapada.Drivers.VirtioBlock.dll") `
    -StagedDllPath (Join-Path (Get-Location).Path "build\vblk.dll") `
    -CacheDllPath (Join-Path $managedCacheRoot "Zapada.Drivers.VirtioBlock.dll") `
    -MissingDotnetMessage "  WARNING: 'dotnet' not found. Zapada.Drivers.VirtioBlock not built; Zapada.Drivers.VirtioBlock.dll will be missing." `
    -MissingProjectMessage "  WARNING: Zapada.Drivers.VirtioBlock.csproj not found at $(Join-Path (Get-Location).Path "src\managed\Zapada.Drivers.VirtioBlock\Zapada.Drivers.VirtioBlock.csproj"); skipping." `
    -PublishFailureMessage "ERROR: dotnet publish Zapada.Drivers.VirtioBlock failed."

Publish-ManagedAssembly `
    -DisplayName "GPT reader (Zapada.Fs.Gpt)" `
    -ProjectPath (Join-Path (Get-Location).Path "src\managed\Zapada.Fs.Gpt\Zapada.Fs.Gpt.csproj") `
    -PublishOut (Join-Path (Get-Location).Path "build\gpt-out") `
    -BuiltDllPath (Join-Path (Join-Path (Get-Location).Path "build\gpt-out") "Zapada.Fs.Gpt.dll") `
    -StagedDllPath (Join-Path (Get-Location).Path "build\gpt.dll") `
    -CacheDllPath (Join-Path $managedCacheRoot "Zapada.Fs.Gpt.dll") `
    -MissingDotnetMessage "  WARNING: 'dotnet' not found. Zapada.Fs.Gpt not built; Zapada.Fs.Gpt.dll will be missing." `
    -MissingProjectMessage "  WARNING: Zapada.Fs.Gpt.csproj not found at $(Join-Path (Get-Location).Path "src\managed\Zapada.Fs.Gpt\Zapada.Fs.Gpt.csproj"); skipping." `
    -PublishFailureMessage "ERROR: dotnet publish Zapada.Fs.Gpt failed."

Publish-ManagedAssembly `
    -DisplayName "FAT32 reader (Zapada.Fs.Fat32)" `
    -ProjectPath (Join-Path (Get-Location).Path "src\managed\Zapada.Fs.Fat32\Zapada.Fs.Fat32.csproj") `
    -PublishOut (Join-Path (Get-Location).Path "build\fat32-out") `
    -BuiltDllPath (Join-Path (Join-Path (Get-Location).Path "build\fat32-out") "Zapada.Fs.Fat32.dll") `
    -StagedDllPath (Join-Path (Get-Location).Path "build\fat32.dll") `
    -CacheDllPath (Join-Path $managedCacheRoot "Zapada.Fs.Fat32.dll") `
    -MissingDotnetMessage "  WARNING: 'dotnet' not found. Zapada.Fs.Fat32 not built; Zapada.Fs.Fat32.dll will be missing." `
    -MissingProjectMessage "  WARNING: Zapada.Fs.Fat32.csproj not found at $(Join-Path (Get-Location).Path "src\managed\Zapada.Fs.Fat32\Zapada.Fs.Fat32.csproj"); skipping." `
    -PublishFailureMessage "ERROR: dotnet publish Zapada.Fs.Fat32 failed."

Publish-ManagedAssembly `
    -DisplayName "Storage abstractions (Zapada.Storage)" `
    -ProjectPath (Join-Path (Get-Location).Path "src\managed\Zapada.Storage\Zapada.Storage.csproj") `
    -PublishOut (Join-Path (Get-Location).Path "build\storage-out") `
    -BuiltDllPath (Join-Path (Join-Path (Get-Location).Path "build\storage-out") "Zapada.Storage.dll") `
    -StagedDllPath (Join-Path (Get-Location).Path "build\storage.dll") `
    -CacheDllPath (Join-Path $managedCacheRoot "Zapada.Storage.dll") `
    -MissingDotnetMessage "  WARNING: 'dotnet' not found. Zapada.Storage not built; Zapada.Storage.dll will be missing." `
    -MissingProjectMessage "  WARNING: Zapada.Storage.csproj not found at $(Join-Path (Get-Location).Path "src\managed\Zapada.Storage\Zapada.Storage.csproj"); skipping." `
    -PublishFailureMessage "ERROR: dotnet publish Zapada.Storage failed."

Publish-ManagedAssembly `
    -DisplayName "VFS layer (Zapada.Fs.Vfs)" `
    -ProjectPath (Join-Path (Get-Location).Path "src\managed\Zapada.Fs.Vfs\Zapada.Fs.Vfs.csproj") `
    -PublishOut (Join-Path (Get-Location).Path "build\vfs-out") `
    -BuiltDllPath (Join-Path (Join-Path (Get-Location).Path "build\vfs-out") "Zapada.Fs.Vfs.dll") `
    -StagedDllPath (Join-Path (Get-Location).Path "build\vfs.dll") `
    -CacheDllPath (Join-Path $managedCacheRoot "Zapada.Fs.Vfs.dll") `
    -MissingDotnetMessage "  WARNING: 'dotnet' not found. Zapada.Fs.Vfs not built; Zapada.Fs.Vfs.dll will be missing." `
    -MissingProjectMessage "  WARNING: Zapada.Fs.Vfs.csproj not found at $(Join-Path (Get-Location).Path "src\managed\Zapada.Fs.Vfs\Zapada.Fs.Vfs.csproj"); skipping." `
    -PublishFailureMessage "ERROR: dotnet publish Zapada.Fs.Vfs failed."

Publish-ManagedAssembly `
    -DisplayName "cross-assembly conformance fixture (Zapada.Conformance.CrossAsm)" `
    -ProjectPath (Join-Path (Get-Location).Path "src\managed\Zapada.Conformance.CrossAsm\Zapada.Conformance.CrossAsm.csproj") `
    -PublishOut (Join-Path (Get-Location).Path "build\conf-crossasm-out") `
    -BuiltDllPath (Join-Path (Join-Path (Get-Location).Path "build\conf-crossasm-out") "Zapada.Conformance.CrossAsm.dll") `
    -StagedDllPath (Join-Path (Get-Location).Path "build\conf-crossasm.dll") `
    -CacheDllPath (Join-Path $managedCacheRoot "Zapada.Conformance.CrossAsm.dll") `
    -MissingDotnetMessage "  WARNING: 'dotnet' not found. Zapada.Conformance.CrossAsm not built; Zapada.Conformance.CrossAsm.dll will be missing." `
    -MissingProjectMessage "  WARNING: Zapada.Conformance.CrossAsm.csproj not found at $(Join-Path (Get-Location).Path "src\managed\Zapada.Conformance.CrossAsm\Zapada.Conformance.CrossAsm.csproj"); skipping." `
    -PublishFailureMessage "ERROR: dotnet publish Zapada.Conformance.CrossAsm failed."

Publish-ManagedAssembly `
    -DisplayName "IL conformance tests (Zapada.Conformance)" `
    -ProjectPath (Join-Path (Get-Location).Path "src\managed\Zapada.Conformance\Zapada.Conformance.csproj") `
    -PublishOut (Join-Path (Get-Location).Path "build\conf-out") `
    -BuiltDllPath (Join-Path (Join-Path (Get-Location).Path "build\conf-out") "Zapada.Conformance.dll") `
    -StagedDllPath (Join-Path (Get-Location).Path "build\conf.dll") `
    -CacheDllPath (Join-Path $managedCacheRoot "Zapada.Conformance.dll") `
    -MissingDotnetMessage "  WARNING: 'dotnet' not found. Zapada.Conformance not built; Zapada.Conformance.dll will be missing." `
    -MissingProjectMessage "  WARNING: Zapada.Conformance.csproj not found at $(Join-Path (Get-Location).Path "src\managed\Zapada.Conformance\Zapada.Conformance.csproj"); skipping." `
    -PublishFailureMessage "ERROR: dotnet publish Zapada.Conformance failed."

if ($NativeClean) {
    Write-Host "Running native clean before make..." -ForegroundColor Yellow
    wsl bash -c "cd '$wslProject' && make clean 2>&1"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Native clean failed." -ForegroundColor Red
        exit 1
    }

    Restore-ManagedCacheToBuild
}

# --------------------------------------------------------------------------
# Build via WSL make
# --------------------------------------------------------------------------
if ($Arch -eq "aarch64") {
    $makeCmd = "make ARCH=aarch64 BOARD=$Board all"
    Write-Host ""
    Write-Host "Building Zapada - AArch64 (BOARD=$Board)..." -ForegroundColor Cyan
    Write-Host "  Project : $wslProject" -ForegroundColor Gray
    Write-Host ""

    wsl bash -c "cd '$wslProject' && make ENABLE_ZACLR=1 zaclr-generated 2>&1 && $makeCmd 2>&1"

    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "AArch64 build FAILED." -ForegroundColor Red
        Write-Host ""
        Write-Host "Troubleshooting:" -ForegroundColor Yellow
        Write-Host "  - Missing cross-compiler? Run: sudo apt install gcc-aarch64-linux-gnu" -ForegroundColor Yellow
        exit 1
    }

    Write-Host ""
    Write-Host "AArch64 build complete." -ForegroundColor Green
    Write-Host "  Kernel ELF : .\build\aarch64\Zapada.elf" -ForegroundColor Gray
    Write-Host "  Kernel image: .\build\aarch64\kernel8.img" -ForegroundColor Gray
    Write-Host ""
} else {
    Write-Host ""
    Write-Host "Building Zapada - x86_64..." -ForegroundColor Cyan
    Write-Host "  Project : $wslProject" -ForegroundColor Gray
    Write-Host ""

    wsl bash -c "cd '$wslProject' && make ENABLE_ZACLR=1 zaclr-generated 2>&1 && make ENABLE_ZACLR=1 all 2>&1"

    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "x86_64 build FAILED." -ForegroundColor Red
        Write-Host ""
        Write-Host "Troubleshooting:" -ForegroundColor Yellow
        Write-Host "  - Missing NASM?           Run: sudo apt install nasm" -ForegroundColor Yellow
        Write-Host "  - Missing grub-mkrescue?  Run: sudo apt install grub-pc-bin grub-common xorriso" -ForegroundColor Yellow
        exit 1
    }

    Write-Host ""
    Write-Host "x86_64 build complete." -ForegroundColor Green
    Write-Host "  Kernel ELF  : .\build\Zapada.elf" -ForegroundColor Gray
    Write-Host "  Bootable ISO: .\build\Zapada.iso" -ForegroundColor Gray
    Write-Host ""
}

# --------------------------------------------------------------------------
# Run in QEMU if requested
# --------------------------------------------------------------------------
if ($Run) {
    Write-Host "Launching QEMU..." -ForegroundColor Cyan
    & .\run.ps1 -Arch $Arch -TimeoutSec 300
}

