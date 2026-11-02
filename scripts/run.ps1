param([string]$DependencyPrefix)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'DevEnvironment.ps1')
$null = Initialize-TheCadEnvironment $DependencyPrefix
$executable = Join-Path (Split-Path $PSScriptRoot) 'build\windows\Release\TheCAD.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw 'TheCAD has not been built. Run scripts/build.ps1 first.'
}
$applicationProcess = Start-Process -FilePath $executable -PassThru -Wait -WindowStyle Hidden
if ($applicationProcess.ExitCode -ne 0) { throw "TheCAD exited with code $($applicationProcess.ExitCode)." }
