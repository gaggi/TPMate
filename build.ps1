$ErrorActionPreference = 'Stop'

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmake = $cmakeCommand.Source
} else {
    $bundledCMake = Join-Path $PSScriptRoot '..\LaunchMate\.tools\cmake-3.30.5-windows-x86_64\bin\cmake.exe'
    if (-not (Test-Path -LiteralPath $bundledCMake)) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($vsPath) {
                $bundledCMake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            }
        }
    }
    if (-not (Test-Path -LiteralPath $bundledCMake)) {
        throw 'CMake was not found. Install CMake 3.21 or newer and add it to PATH.'
    }
    $cmake = (Resolve-Path -LiteralPath $bundledCMake).Path
}

function Invoke-CMake([string]$arguments) {
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $cmake
    $start.Arguments = $arguments
    $start.WorkingDirectory = $PSScriptRoot
    $start.UseShellExecute = $false
    # Some launchers supply both PATH and Path; MSBuild rejects that environment.
    $environment = [System.Collections.Generic.Dictionary[string,string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        $environment[$entry.Key] = $entry.Value
    }
    $start.Environment.Clear()
    foreach ($entry in $environment.GetEnumerator()) { $start.Environment[$entry.Key] = $entry.Value }
    $process = [System.Diagnostics.Process]::Start($start)
    $process.WaitForExit()
    $code = $process.ExitCode
    $process.Dispose()
    if ($code -ne 0) { exit $code }
}

Invoke-CMake '--preset x64'
Invoke-CMake '--build --preset release-x64'
Invoke-CMake '--preset x86'
Invoke-CMake '--build --preset release-x86'
