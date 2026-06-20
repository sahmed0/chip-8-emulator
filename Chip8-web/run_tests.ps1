# Build and run the Chip-8 unit tests without depending on the toolchain being
# on PATH (works even from a terminal opened before MSYS2 was added to PATH).
#
# Usage:
#   .\run_tests.ps1            # build + run
#   .\run_tests.ps1 -v         # verbose, per-test output (passed through to greatest)
#
# Any arguments are forwarded to the test binary (greatest flags: -v, -f, -l, ...).

$ErrorActionPreference = "Stop"

# Run from this script's own directory so relative source paths resolve.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $here
try {
    # Locate the MSYS2 toolchain. Honor a custom root via $env:MSYS2_ROOT.
    $binDirs = @()
    if ($env:MSYS2_ROOT) { $binDirs += (Join-Path $env:MSYS2_ROOT "ucrt64\bin") }
    $binDirs += @("C:\msys64\ucrt64\bin", "C:\msys64\mingw64\bin")

    $toolchainBin = $binDirs | Where-Object { Test-Path (Join-Path $_ "gcc.exe") } | Select-Object -First 1
    if (-not $toolchainBin) {
        throw "gcc.exe not found. Looked in: $($binDirs -join ', '). Set `$env:MSYS2_ROOT if MSYS2 is installed elsewhere."
    }

    # Prepend to PATH for THIS process only so gcc's cc1 subprocess finds its DLLs.
    $env:Path = "$toolchainBin;$env:Path"

    $src = @("tests/test_chip8.c", "chip8.c")
    $bin = "test_chip8.exe"

    Write-Host "Building $bin with $(Join-Path $toolchainBin 'gcc.exe')..." -ForegroundColor Cyan
    & gcc -g -O0 -Wall -Wextra -std=c11 @src -o $bin
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)." }

    Write-Host "Running tests..." -ForegroundColor Cyan
    & ".\$bin" @args
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
