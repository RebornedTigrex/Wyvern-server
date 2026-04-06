<#
.SYNOPSIS
Build helper for Wyvern foundation server (MSVC + vcpkg + CMake presets).

.DESCRIPTION
The script auto-detects Visual Studio C++ tools (via vswhere + VsDevCmd),
resolves VCPKG_ROOT (env or common fallback paths), then runs CMake configure/build.

.PARAMETER Preset
CMake configure preset name (default: x64-debug or $env:WYVERN_PRESET).

.PARAMETER BuildDir
Custom binary directory (default: out/build/<preset>-foundation or $env:WYVERN_BUILD_DIR).

.PARAMETER Target
Build target name (default: wyvern_server_foundation or $env:WYVERN_TARGET).

.PARAMETER ConfigureOnly
Run only CMake configure step.

.PARAMETER BuildOnly
Run only CMake build step.

.PARAMETER Run
Run built executable after successful build.

.EXAMPLE
pwsh -File .\build.ps1

.EXAMPLE
pwsh -File .\build.ps1 -Preset x64-release -Target wyvern_server_foundation

.EXAMPLE
$env:WYVERN_PRESET = "x64-debug"
$env:WYVERN_JOBS = "12"
pwsh -File .\build.ps1 -Run
#>
[CmdletBinding()]
param(
    [string]$Preset = "",
    [string]$SourceDir = "",
    [string]$BuildDir = "",
    [string]$Target = "",
    [ValidateSet("ON", "OFF")]
    [string]$BuildFoundation = "",
    [int]$Jobs = 0,
    [switch]$ConfigureOnly,
    [switch]$BuildOnly,
    [switch]$Run,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Preset)) {
    if ($env:WYVERN_PRESET) {
        $Preset = $env:WYVERN_PRESET
    } else {
        $Preset = "x64-debug"
    }
}
if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = if ($env:EDUSPACE_SOURCE_DIR) { $env:EDUSPACE_SOURCE_DIR } else { $PSScriptRoot }
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    if ($env:WYVERN_BUILD_DIR) {
        $BuildDir = $env:WYVERN_BUILD_DIR
    } else {
        $BuildDir = Join-Path $PSScriptRoot ("out/build/{0}-foundation" -f $Preset)
    }
}
if ([string]::IsNullOrWhiteSpace($Target)) {
    if ($env:WYVERN_TARGET) {
        $Target = $env:WYVERN_TARGET
    } else {
        $Target = "wyvern_server_foundation"
    }
}
if ([string]::IsNullOrWhiteSpace($BuildFoundation)) {
    if ($env:WYVERN_BUILD_FOUNDATION) {
        $BuildFoundation = $env:WYVERN_BUILD_FOUNDATION
    } else {
        $BuildFoundation = "ON"
    }
}
if ($Jobs -le 0) {
    if ($env:WYVERN_JOBS) {
        $Jobs = [int]$env:WYVERN_JOBS
    } else {
        $Jobs = 8
    }
}

function Resolve-VsWherePath {
    $defaultPath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $defaultPath) {
        return $defaultPath
    }

    $found = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($null -ne $found) {
        return $found.Source
    }

    throw "vswhere.exe not found. Install Visual Studio Installer components, or add vswhere.exe to PATH."
}

function Resolve-VcpkgRoot {
    if ($env:VCPKG_ROOT -and (Test-Path $env:VCPKG_ROOT)) {
        return (Resolve-Path $env:VCPKG_ROOT).Path
    }

    $candidates = @(
        (Join-Path $PSScriptRoot "vcpkg"),
        (Join-Path $env:USERPROFILE "vcpkg"),
        "C:\vcpkg"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            $resolved = (Resolve-Path $candidate).Path
            $env:VCPKG_ROOT = $resolved
            return $resolved
        }
    }

    throw "VCPKG_ROOT is not set and no fallback vcpkg path was found. Set VCPKG_ROOT to your vcpkg directory."
}

function Import-MsvcEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [string]$VsDevCmdPath
    )

    $setOutput = & cmd.exe /d /c "`"$VsDevCmdPath`" -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to initialize MSVC environment via VsDevCmd.bat."
    }

    foreach ($line in $setOutput) {
        if ($line -match "^([^=]+)=(.*)$") {
            $name = $matches[1]
            $value = $matches[2]
            Set-Item -Path "Env:$name" -Value $value
        }
    }
}

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Exe,
        [Parameter(Mandatory = $true)]
        [string[]]$Args
    )

    Write-Host ">> $Exe $($Args -join ' ')"
    & $Exe @Args
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $Exe (exit code $LASTEXITCODE)"
    }
}

$SourceDir = [System.IO.Path]::GetFullPath($SourceDir)
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host ">> Removing build directory: $BuildDir"
    Remove-Item -Path $BuildDir -Recurse -Force
}

$vswherePath = Resolve-VsWherePath
$vsInstallPath = (& $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($vsInstallPath)) {
    throw "No Visual Studio installation with C++ tools was found."
}

$vsDevCmd = Join-Path $vsInstallPath "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $vsDevCmd)) {
    throw "VsDevCmd.bat not found at expected path: $vsDevCmd"
}

$resolvedVcpkgRoot = Resolve-VcpkgRoot
Write-Host "Using Visual Studio: $vsInstallPath"
Write-Host "Using VCPKG_ROOT: $resolvedVcpkgRoot"

Import-MsvcEnvironment -VsDevCmdPath $vsDevCmd
$env:VCPKG_ROOT = $resolvedVcpkgRoot
Write-Host "Re-applied VCPKG_ROOT after VsDevCmd: $env:VCPKG_ROOT"

if (-not $BuildOnly) {
    $configureArgs = @(
        "-Wno-dev",
        "--preset", $Preset,
        "-S", $SourceDir,
        "-B", $BuildDir,
        "-DWYVERN_BUILD_FOUNDATION=$BuildFoundation"
    )
    Invoke-External -Exe "cmake" -Args $configureArgs
}

if (-not $ConfigureOnly) {
    $buildArgs = @(
        "--build", $BuildDir,
        "--target", $Target
    )
    if ($Jobs -gt 0) {
        $buildArgs += @("-j", "$Jobs")
    }

    Invoke-External -Exe "cmake" -Args $buildArgs
}

if ($Run) {
    $exeCandidates = @(
        (Join-Path $BuildDir "$Target.exe")
    )
    $exePath = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($exePath)) {
        throw "Built executable not found. Checked: $($exeCandidates -join ', ')"
    }

    Write-Host ">> $exePath"
    & $exePath
    if ($LASTEXITCODE -ne 0) {
        throw "Executable returned non-zero exit code: $LASTEXITCODE"
    }
}

Write-Host "Build script completed successfully."
