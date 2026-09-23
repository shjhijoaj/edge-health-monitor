# Shared tool discovery for the build and verification scripts.
#
# Every function returns $null when the tool cannot be found, so callers can
# decide whether the step is skipped or has to fail.

function Find-GccRoot {
    <# Returns a directory that contains gcc.exe / g++.exe. #>
    param([string]$Explicit = '')

    if ($Explicit -and (Test-Path (Join-Path $Explicit 'gcc.exe'))) { return $Explicit }
    foreach ($candidate in @('C:\msys64\ucrt64\bin', 'C:\msys64\mingw64\bin', 'C:\msys64\clang64\bin')) {
        if (Test-Path (Join-Path $candidate 'gcc.exe')) { return $candidate }
    }
    $command = Get-Command gcc -ErrorAction SilentlyContinue
    if ($command) { return (Split-Path -Parent $command.Source) }
    return $null
}

function Find-ArmToolchainRoot {
    <# Returns a directory that contains arm-none-eabi-gcc.exe. #>
    param([string]$Explicit = '')

    if ($Explicit -and (Test-Path (Join-Path $Explicit 'arm-none-eabi-gcc.exe'))) { return $Explicit }
    foreach ($candidate in @('C:\msys64\ucrt64\bin', 'C:\msys64\mingw64\bin')) {
        if (Test-Path (Join-Path $candidate 'arm-none-eabi-gcc.exe')) { return $candidate }
    }
    $command = Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue
    if ($command) { return (Split-Path -Parent $command.Source) }
    return $null
}

function Find-QemuArm {
    param([string]$Explicit = '')

    if ($Explicit -and (Test-Path $Explicit)) { return $Explicit }
    foreach ($candidate in @('C:\msys64\ucrt64\bin\qemu-system-arm.exe', 'C:\msys64\mingw64\bin\qemu-system-arm.exe')) {
        if (Test-Path $candidate) { return $candidate }
    }
    $command = Get-Command qemu-system-arm -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    return $null
}

function Find-MsvcDevCmd {
    param([string]$Explicit = '')

    if ($Explicit -and (Test-Path $Explicit)) { return $Explicit }
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * -property installationPath 2>$null
        if ($installPath) {
            $devCmd = Join-Path $installPath.Trim() 'Common7\Tools\VsDevCmd.bat'
            if (Test-Path $devCmd) { return $devCmd }
        }
    }
    return $null
}
