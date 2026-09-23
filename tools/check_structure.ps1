param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$required = @(
    'CMakeLists.txt',
    'README.md',
    'firmware/include/eh_protocol.h',
    'firmware/src/eh_protocol.c',
    'gateway/include/gateway.hpp',
    'gateway/src/gateway.cpp',
    'tests/test_protocol.c',
    'tests/test_gateway.cpp',
    'docs/architecture.md'
)

$missing = @($required | Where-Object { -not (Test-Path (Join-Path $Root $_)) })
if ($missing.Count -gt 0) {
    $missing | ForEach-Object { Write-Error "missing: $_" }
    exit 1
}

Write-Output "project structure OK: $Root"
