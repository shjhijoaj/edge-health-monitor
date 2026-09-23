<#
.SYNOPSIS
    Runs every verification step of the project and prints a summary.

.DESCRIPTION
    Steps:
      1. Python syntax check for all tools
      2. Host build + CTest with MSVC           (skipped when MSVC is missing)
      3. Host tests with GCC                     (skipped when GCC is missing)
      4. Cortex-M3 firmware cross build
      5. Watchdog demo in QEMU + frame replay    (skipped with -SkipEmulation)

    MSVC needs an ASCII working directory because its linker cannot write PDB
    files under a path with non-ASCII characters, so the host builds are done
    in a mirrored copy under %TEMP%.

.EXAMPLE
    pwsh -File tools/verify_all.ps1 -SkipEmulation
#>

param(
    [switch]$SkipEmulation,
    [string]$MsvcDevCmd = '',
    [string]$GccRoot = '',
    [string]$Qemu = '',
    [int]$EmulationSeconds = 120
)

$ErrorActionPreference = 'Continue'

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $repoRoot 'tools\find_tools.ps1')
if ([string]::IsNullOrWhiteSpace($MsvcDevCmd)) { $MsvcDevCmd = Find-MsvcDevCmd }
if ([string]::IsNullOrWhiteSpace($GccRoot)) { $GccRoot = Find-GccRoot }
if ([string]::IsNullOrWhiteSpace($Qemu)) { $Qemu = Find-QemuArm }
$mirror = Join-Path $env:TEMP 'ehm-verify'
$results = [System.Collections.Generic.List[object]]::new()

function Add-Result {
    param([string]$Step, [string]$Status, [string]$Detail = '')
    $results.Add([PSCustomObject]@{ Step = $Step; Status = $Status; Detail = $Detail })
    Write-Host ("[{0}] {1} {2}" -f $Status, $Step, $Detail)
}

function Sync-Sources {
    Remove-Item -LiteralPath $mirror -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $mirror | Out-Null
    foreach ($name in @('firmware', 'gateway', 'tests', 'tools', 'config', 'CMakeLists.txt')) {
        Copy-Item (Join-Path $repoRoot $name) (Join-Path $mirror $name) -Recurse -Force
    }
}

# ---------------------------------------------------------------- 1. Python
Write-Host '=== 1. Python 工具语法检查 ==='
$pyFiles = Get-ChildItem (Join-Path $repoRoot 'tools') -Filter '*.py' -File
$pyFailed = @()
foreach ($file in $pyFiles) {
    & python -m py_compile $file.FullName 2>$null
    if ($LASTEXITCODE -ne 0) { $pyFailed += $file.Name }
}
Get-ChildItem (Join-Path $repoRoot 'tools') -Recurse -Directory -Filter '__pycache__' |
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
if ($pyFailed.Count -eq 0) {
    Add-Result 'Python 语法检查' 'PASS' "$($pyFiles.Count) 个文件"
} else {
    Add-Result 'Python 语法检查' 'FAIL' ($pyFailed -join ', ')
}

# ------------------------------------------------------------ 2/3. Host builds
Write-Host '=== 2. 主机测试（MSVC） ==='
Sync-Sources
if ($MsvcDevCmd -and (Test-Path $MsvcDevCmd)) {
    $command = 'call "{0}" -arch=x64 && cd /d "{1}" && cmake -S . -B build -G "NMake Makefiles" && cmake --build build && ctest --test-dir build --output-on-failure' -f $MsvcDevCmd, $mirror
    $output = cmd.exe /d /c $command 2>&1
    $summary = ($output | Select-String 'tests passed').Line
    if ($LASTEXITCODE -eq 0) {
        Add-Result '主机测试（MSVC + CTest）' 'PASS' $summary
    } else {
        Add-Result '主机测试（MSVC + CTest）' 'FAIL' (($output | Select-Object -Last 3) -join ' ')
    }
} else {
    Add-Result '主机测试（MSVC + CTest）' 'SKIP' '未找到 Visual Studio 开发环境'
}

Write-Host '=== 3. 主机测试（GCC） ==='
$gcc = if ($GccRoot) { Join-Path $GccRoot 'gcc.exe' } else { $null }
$gpp = if ($GccRoot) { Join-Path $GccRoot 'g++.exe' } else { $null }
if ((Test-Path $gcc) -and (Test-Path $gpp)) {
    $obj = Join-Path $mirror 'gcc-obj'
    New-Item -ItemType Directory -Force -Path $obj | Out-Null
    $steps = @(
        @($gcc, '-std=c99', '-O2', '-Wall', '-Wextra', "-I$mirror\firmware\include", '-c', "$mirror\firmware\src\eh_protocol.c", '-o', "$obj\eh_protocol.o"),
        @($gcc, '-std=c99', '-O2', '-Wall', '-Wextra', "-I$mirror\firmware\include", '-c', "$mirror\firmware\src\eh_measurement.c", '-o', "$obj\eh_measurement.o"),
        @($gpp, '-std=c++17', '-O2', '-Wall', '-Wextra', "-I$mirror\firmware\include", "-I$mirror\gateway\include", '-c', "$mirror\gateway\src\gateway.cpp", '-o', "$obj\gateway.o"),
        @($gcc, '-std=c99', '-O2', "-I$mirror\firmware\include", "$mirror\tests\test_protocol.c", "$obj\eh_protocol.o", "$obj\eh_measurement.o", '-o', "$obj\test_protocol.exe"),
        @($gpp, '-std=c++17', '-O2', "-I$mirror\firmware\include", "-I$mirror\gateway\include", "$mirror\tests\test_gateway.cpp", "$obj\gateway.o", "$obj\eh_protocol.o", "$obj\eh_measurement.o", '-o', "$obj\test_gateway.exe")
    )
    $gccFailed = $null
    foreach ($step in $steps) {
        & $step[0] @($step[1..($step.Count - 1)]) 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { $gccFailed = ($step[1..($step.Count - 1)] -join ' '); break }
    }
    if ($gccFailed) {
        Add-Result '主机测试（GCC）' 'FAIL' $gccFailed
    } else {
        Push-Location $obj
        $protocolOutput = & '.\test_protocol.exe' 2>&1
        $protocolOk = $LASTEXITCODE -eq 0
        $gatewayOutput = & '.\test_gateway.exe' 2>&1
        $gatewayOk = $LASTEXITCODE -eq 0
        Pop-Location
        if ($protocolOk -and $gatewayOk) {
            Add-Result '主机测试（GCC）' 'PASS' "$protocolOutput / $gatewayOutput".Trim()
        } else {
            Add-Result '主机测试（GCC）' 'FAIL' "protocol=$protocolOk gateway=$gatewayOk"
        }
    }
} else {
    Add-Result '主机测试（GCC）' 'SKIP' '未找到 gcc'
}

# ----------------------------------------------------------- 4. Firmware build
Write-Host '=== 4. Cortex-M3 固件交叉编译 ==='
$firmwareOutput = & (Join-Path $repoRoot 'tools\sim\build_firmware.ps1') 2>&1
$firmwareElf = Join-Path $repoRoot 'build-firmware\edge_health_firmware.elf'
if (Test-Path $firmwareElf) {
    $size = ($firmwareOutput | Select-String 'text\s+data\s+bss').Line
    Add-Result 'Cortex-M3 固件构建' 'PASS' ($firmwareOutput | Select-String 'edge_health_firmware.elf\s*$' | Select-Object -Last 1).Line
} else {
    Add-Result 'Cortex-M3 固件构建' 'FAIL' (($firmwareOutput | Select-Object -Last 2) -join ' ')
}

# -------------------------------------------------------------- 5. Emulation
Write-Host '=== 5. QEMU 仿真运行 ==='
if ($SkipEmulation) {
    Add-Result 'QEMU 看门狗演示' 'SKIP' '按参数跳过'
} elseif (-not $Qemu) {
    Add-Result 'QEMU 看门狗演示' 'SKIP' '未找到 qemu-system-arm'
} else {
    # The guest runs slower than the wall clock, so the run has to be long enough
    # for the deliberate stall (8 s of guest time) and the watchdog to be reached.
    $emulation = & (Join-Path $repoRoot 'tools\sim\run_emulation.ps1') `
        -Image watchdog -Seconds $EmulationSeconds -SkipBuild -NoReplay -OutName 'watchdog-verify' 2>&1
    $metricsPath = Join-Path $repoRoot 'docs\evidence\metrics-watchdog-verify.json'
    if (Test-Path $metricsPath) {
        $metrics = Get-Content $metricsPath -Raw | ConvertFrom-Json
        $ok = ($metrics.bad_frames -eq 0) -and ($metrics.frames_recovered -ge 30) -and
              ($metrics.watchdog_resets -ge 1)
        Add-Result 'QEMU 看门狗演示' ($(if ($ok) { 'PASS' } else { 'FAIL' })) `
            "帧 $($metrics.frames_recovered)，错帧 $($metrics.bad_frames)，看门狗复位 $($metrics.watchdog_resets)，模拟 $([math]::Round($metrics.guest_uptime_ms_max / 1000.0, 1)) s"
    } else {
        Add-Result 'QEMU 看门狗演示' 'FAIL' (($emulation | Select-Object -Last 2) -join ' ')
    }
}

# ------------------------------------------------------------------ summary
Write-Host ''
Write-Host '=== 汇总 ==='
$results | Format-Table -AutoSize

$summaryPath = Join-Path $repoRoot 'docs\evidence\verify-summary.json'
$results | ConvertTo-Json | Set-Content -LiteralPath $summaryPath -Encoding UTF8
Write-Host "结果已写入 $summaryPath"

if ($results | Where-Object { $_.Status -eq 'FAIL' }) {
    exit 1
}
exit 0
