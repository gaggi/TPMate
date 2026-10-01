$ErrorActionPreference = 'Stop'

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmake = $cmakeCommand.Source
} else {
    $bundledCMake = Join-Path $PSScriptRoot '..\LaunchMate\.tools\cmake-3.30.5-windows-x86_64\bin\cmake.exe'
    if (-not (Test-Path -LiteralPath $bundledCMake)) {
        throw 'CMake was not found. Install CMake 3.21 or newer and add it to PATH.'
    }
    $cmake = (Resolve-Path -LiteralPath $bundledCMake).Path
}

& $cmake --preset x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build --preset release-x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --preset x86
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build --preset release-x86
exit $LASTEXITCODE
