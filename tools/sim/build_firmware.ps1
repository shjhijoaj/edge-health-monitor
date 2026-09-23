<#
.SYNOPSIS
    Builds the Cortex-M3 firmware images with the ARM cross compiler.

.DESCRIPTION
    Produces two ELF images in build-firmware/:

      edge_health_firmware.elf         watchdog demonstration build
      edge_health_firmware_stable.elf  same firmware without the injected stall

    The script calls arm-none-eabi-gcc directly so it works on Windows without
    needing a make program or a CMake generator that understands cross builds.
#>

param(
    [string]$ToolchainRoot = '',
    [string]$FreeRtosRoot = ''
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repoRoot 'tools\find_tools.ps1')
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) {
    $ToolchainRoot = Find-ArmToolchainRoot
}
if (-not $ToolchainRoot) {
    throw "找不到 arm-none-eabi-gcc。请安装：pacman -S mingw-w64-ucrt-x86_64-arm-none-eabi-gcc，或用 -ToolchainRoot 指定目录。"
}
if ([string]::IsNullOrWhiteSpace($FreeRtosRoot)) {
    $FreeRtosRoot = Join-Path $repoRoot 'firmware\third_party\FreeRTOS-Kernel'
}

$gcc = Join-Path $ToolchainRoot 'arm-none-eabi-gcc.exe'
$size = Join-Path $ToolchainRoot 'arm-none-eabi-size.exe'
foreach ($tool in @($gcc, $size)) {
    if (-not (Test-Path $tool)) {
        throw "找不到 $tool。请先安装 ARM 工具链：pacman -S mingw-w64-ucrt-x86_64-arm-none-eabi-gcc"
    }
}

$port = Join-Path $FreeRtosRoot 'portable\GCC\ARM_CM3'
if (-not (Test-Path (Join-Path $FreeRtosRoot 'tasks.c'))) {
    throw "找不到 FreeRTOS 内核：$FreeRtosRoot。请按 docs\emulation.md 下载并解压。"
}

$buildDir = Join-Path $repoRoot 'build-firmware'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$sources = @(
    (Join-Path $repoRoot 'firmware\target\app_main.c'),
    (Join-Path $repoRoot 'firmware\target\board.c'),
    (Join-Path $repoRoot 'firmware\target\startup.c'),
    (Join-Path $repoRoot 'firmware\src\eh_protocol.c'),
    (Join-Path $repoRoot 'firmware\src\eh_measurement.c'),
    (Join-Path $repoRoot 'firmware\target\sensors\i2c_bus_sim.c'),
    (Join-Path $repoRoot 'firmware\target\sensors\i2c_bus_hal.c'),
    (Join-Path $repoRoot 'firmware\target\sensors\sensor.c'),
    (Join-Path $repoRoot 'firmware\target\sensors\sensor_i2c.c'),
    (Join-Path $repoRoot 'firmware\target\sensors\sensor_sim.c'),
    (Join-Path $FreeRtosRoot 'tasks.c'),
    (Join-Path $FreeRtosRoot 'queue.c'),
    (Join-Path $FreeRtosRoot 'list.c'),
    (Join-Path $FreeRtosRoot 'portable\MemMang\heap_4.c'),
    (Join-Path $port 'port.c')
)

$includes = @(
    (Join-Path $repoRoot 'firmware\target'),
    (Join-Path $repoRoot 'firmware\target\sensors'),
    (Join-Path $repoRoot 'firmware\include'),
    (Join-Path $FreeRtosRoot 'include'),
    $port
)

$commonFlags = @(
    '-mcpu=cortex-m3', '-mthumb', '-std=gnu99', '-Os',
    '-ffunction-sections', '-fdata-sections',
    '-fno-common', '-Wall'
)
foreach ($include in $includes) { $commonFlags += "-I$include" }

$linkFlags = @(
    '-mcpu=cortex-m3', '-mthumb', '-nostartfiles',
    "-T$(Join-Path $repoRoot 'firmware\target\linker.ld')",
    '-Wl,--gc-sections'
)

function Build-Image {
    param([string]$Name, [string[]]$ExtraFlags, [string]$MapSuffix)

    $objects = @()
    foreach ($source in $sources) {
        $object = Join-Path $buildDir ("$Name-" + [System.IO.Path]::GetFileNameWithoutExtension($source) + '.o')
        $arguments = $commonFlags + $ExtraFlags + @('-c', $source, '-o', $object)
        Write-Host "  编译 $([System.IO.Path]::GetFileName($source))"
        & $gcc @arguments
        if ($LASTEXITCODE -ne 0) { throw "编译失败：$source" }
        $objects += $object
    }

    $elf = Join-Path $buildDir "$Name.elf"
    $map = Join-Path $buildDir "$Name$MapSuffix.map"
    $arguments = $objects + $linkFlags + @("-Wl,-Map=$map", '-o', $elf)
    Write-Host "  链接 $Name.elf"
    & $gcc @arguments
    if ($LASTEXITCODE -ne 0) { throw "链接失败：$Name" }
    & $size $elf
    return $elf
}

Write-Host '构建看门狗演示固件（含故意的采样卡死注入）'
$watchdogImage = Build-Image -Name 'edge_health_firmware' -ExtraFlags @('-DEHM_WATCHDOG_DEMO=1') -MapSuffix ''

Write-Host '构建稳定性测试固件（无故障注入）'
$stableImage = Build-Image -Name 'edge_health_firmware_stable' -ExtraFlags @('-DEHM_WATCHDOG_DEMO=0') -MapSuffix ''

Write-Host ''
Write-Host "看门狗演示固件: $watchdogImage"
Write-Host "稳定性测试固件: $stableImage"
