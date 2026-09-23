param(
    [int]$Port = 8765
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dataDir = Join-Path $root 'data'
$csv = Join-Path $dataDir 'telemetry.csv'
$raw = Join-Path $dataDir 'capture.ehraw'

$probe = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, $Port)
$available = $false
try {
    $probe.Start()
    $available = $true
} catch {
    $available = $false
} finally {
    if ($available) { $probe.Stop() }
}
if (-not $available) {
    throw "端口 $Port 已被占用，可能已经有一个面板在运行。请先关闭它，或换个端口：tools\run_local.ps1 -Port 8766"
}

$candidates = @(
    (Join-Path $root 'build\Release\edge_health_sim.exe'),
    (Join-Path $root 'build\edge_health_sim.exe'),
    (Join-Path $root 'build-nmake\edge_health_sim.exe')
)
$simulator = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $simulator) {
    throw "找不到 edge_health_sim.exe。请先按 README 完成 CMake 编译。"
}

New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
Remove-Item -LiteralPath $csv -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $raw -Force -ErrorAction SilentlyContinue
Write-Host "运行 C++ 设备模拟器: $simulator"
$python = Get-Command python -ErrorAction Stop
$simulatorLog = Join-Path $dataDir 'simulator.log'
$simulatorError = Join-Path $dataDir 'simulator-error.log'
$process = Start-Process -FilePath $simulator `
    -ArgumentList @('--loop', '--interval-ms', '1000', '--raw', $raw,
                    '--config', (Join-Path $root 'config\thresholds.cfg'), $csv) `
    -RedirectStandardOutput $simulatorLog `
    -RedirectStandardError $simulatorError `
    -PassThru -NoNewWindow
try {
    Start-Sleep -Milliseconds 300
    if ($process.HasExited) {
        throw "设备模拟器启动失败，退出码为 $($process.ExitCode)"
    }
    Write-Host "启动本地面板: http://127.0.0.1:$Port/"
    & $python.Source (Join-Path $PSScriptRoot 'dashboard_server.py') --csv $csv --port $Port
}
finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        Write-Host "设备模拟器已停止"
    }
}
