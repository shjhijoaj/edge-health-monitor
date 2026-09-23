<#
.SYNOPSIS
    Runs the Cortex-M3 firmware in QEMU and feeds the captured frames to the
    local dashboard.

.DESCRIPTION
    Full emulation pipeline:

      1. build the firmware images (unless -SkipBuild)
      2. copy the ELF to an ASCII staging directory (QEMU cannot open non-ASCII paths)
      3. run the firmware for the requested number of seconds
      4. convert the captured UART hex log into replayable protocol frames
      5. optionally replay those frames into the dashboard
      6. write a metrics JSON file for the record

.EXAMPLE
    pwsh -File tools/sim/run_emulation.ps1 -Image watchdog -Seconds 90
    pwsh -File tools/sim/run_emulation.ps1 -Image stable -Seconds 600 -NoReplay
#>

param(
    [ValidateSet('watchdog', 'stable')]
    [string]$Image = 'watchdog',
    [int]$Seconds = 60,
    [string]$Qemu = '',
    [string]$Machine = 'lm3s6965evb',
    [string]$Endpoint = 'http://127.0.0.1:8765/api/ingest',
    [string]$OutName = '',
    [switch]$SkipBuild,
    [switch]$NoReplay
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repoRoot 'tools\find_tools.ps1')
if ([string]::IsNullOrWhiteSpace($Qemu)) { $Qemu = Find-QemuArm }
$simDir = Join-Path $repoRoot 'tools\sim'
$evidenceDir = Join-Path $repoRoot 'docs\evidence'
New-Item -ItemType Directory -Force -Path $evidenceDir | Out-Null
if ([string]::IsNullOrWhiteSpace($OutName)) { $OutName = $Image }

if (-not $Qemu) {
    throw "找不到 qemu-system-arm。请先安装：pacman -S mingw-w64-ucrt-x86_64-qemu，或用 -Qemu 指定路径。"
}

# QEMU on Windows cannot open files under a non-ASCII path, so the images and
# the UART log live in an ASCII staging directory.
$stage = Join-Path $env:TEMP 'ehm-sim'
New-Item -ItemType Directory -Force -Path $stage | Out-Null

if (-not $SkipBuild) {
    & (Join-Path $simDir 'build_firmware.ps1') | Out-Host
    if ($LASTEXITCODE -ne 0) { throw '固件构建失败' }
}

$imageName = if ($Image -eq 'stable') { 'edge_health_firmware_stable' } else { 'edge_health_firmware' }
$elfSource = Join-Path $repoRoot "build-firmware\$imageName.elf"
if (-not (Test-Path $elfSource)) { throw "找不到固件：$elfSource" }

$elf = Join-Path $stage "$Image.elf"
Copy-Item $elfSource $elf -Force

$uartLog = Join-Path $stage "uart-$Image.log"
$qemuError = Join-Path $stage "qemu-$Image.err"
Remove-Item $uartLog, $qemuError -Force -ErrorAction SilentlyContinue

Write-Host "在 $Machine 上运行 $Image 固件，$Seconds 秒..."
$wallStart = Get-Date
$process = Start-Process -FilePath $Qemu `
    -ArgumentList @('-M', $Machine, '-cpu', 'cortex-m3', '-display', 'none',
                    '-serial', 'stdio', '-kernel', $elf) `
    -RedirectStandardOutput $uartLog -RedirectStandardError $qemuError `
    -WindowStyle Hidden -PassThru
try {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline -and -not $process.HasExited) {
        Start-Sleep -Milliseconds 500
    }
} finally {
    if (-not $process.HasExited) { $process.Kill() }
}
$wallSeconds = [math]::Round(((Get-Date) - $wallStart).TotalSeconds, 1)

$lines = Get-Content $uartLog -ErrorAction SilentlyContinue
if (-not $lines) {
    Get-Content $qemuError -ErrorAction SilentlyContinue | ForEach-Object { Write-Warning $_ }
    throw '固件没有产生任何串口输出'
}

$frameLines = ($lines | Select-String -Pattern '^FRAME ').Count
$bootLines = $lines | Select-String -Pattern '^BOOT boot_count='
$bootCount = 0
$watchdogResets = 0
if ($bootLines) {
    $lastBoot = $bootLines[-1].Line
    if ($lastBoot -match 'boot_count=(\d+)\s+wd_resets=(\d+)') {
        $bootCount = [int]$Matches[1]
        $watchdogResets = [int]$Matches[2]
    }
}
# The health task restarts its tick counter after a watchdog reset, so record
# both the largest uptime seen and the value from the final report.
$guestUptimeMs = 0
$guestUptimeLastMs = 0
foreach ($line in ($lines | Select-String -Pattern '^STAT uptime_ms=(\d+)')) {
    $value = [int]($line.Line -replace '^STAT uptime_ms=', '')
    if ($value -gt $guestUptimeMs) { $guestUptimeMs = $value }
    $guestUptimeLastMs = $value
}

$rawOut = Join-Path $evidenceDir "$OutName-frames.ehraw"
Write-Host '解析串口日志为协议帧...'
$parseOutput = & python (Join-Path $repoRoot 'tools\hexlog_to_raw.py') --input $uartLog --output $rawOut 2>&1
$parseOutput | ForEach-Object { Write-Host "  $_" }
$badFrames = 0
$recovered = 0
foreach ($line in $parseOutput) {
    if ($line -match 'bad frames\s*:\s*(\d+)') { $badFrames = [int]$Matches[1] }
    if ($line -match 'frames recovered\s*:\s*(\d+)') { $recovered = [int]$Matches[1] }
}

$replayed = 0
if (-not $NoReplay -and $recovered -gt 0) {
    Write-Host "回放到本地面板（$Endpoint）..."
    $replayOutput = & python (Join-Path $repoRoot 'tools\serial_bridge.py') --replay $rawOut --endpoint $Endpoint 2>&1
    $lastReplay = $replayOutput | Select-Object -Last 1
    $lastReplay | ForEach-Object { Write-Host "  $_" }
    if ($lastReplay -match 'replayed (\d+) frames') { $replayed = [int]$Matches[1] }
}

Copy-Item $uartLog (Join-Path $evidenceDir "$OutName-uart.log") -Force

$metrics = [ordered]@{
    image             = $Image
    machine           = $Machine
    wall_seconds      = $wallSeconds
    guest_uptime_ms_max = $guestUptimeMs
    guest_uptime_ms_last = $guestUptimeLastMs
    frame_lines       = $frameLines
    frames_recovered  = $recovered
    bad_frames        = $badFrames
    frames_replayed   = $replayed
    boot_count        = $bootCount
    watchdog_resets   = $watchdogResets
    generated_at      = (Get-Date).ToString('s')
}
$metricsPath = Join-Path $evidenceDir "metrics-$OutName.json"
$metrics | ConvertTo-Json | Set-Content -LiteralPath $metricsPath -Encoding UTF8

Write-Host ''
Write-Host "墙钟运行时间   : $wallSeconds s"
Write-Host "模拟运行时间   : $([math]::Round($guestUptimeMs / 1000.0, 1)) s"
Write-Host "输出帧         : $frameLines"
Write-Host "还原帧 / 错帧  : $recovered / $badFrames"
Write-Host "回放帧         : $replayed"
Write-Host "启动次数       : $bootCount"
Write-Host "看门狗复位     : $watchdogResets"
Write-Host "证据目录       : $evidenceDir"
