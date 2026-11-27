param(
    [string]$DependencyPrefix,
    [switch]$GuiTests
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'DevEnvironment.ps1')
$library = Initialize-TheCadEnvironment $DependencyPrefix
$repoRoot = Split-Path $PSScriptRoot
$buildDirectory = Join-Path $repoRoot 'build\windows'
$guiTestsValue = if ($GuiTests) { 'ON' } else { 'OFF' }
& "$library\bin\cmake.exe" -S $repoRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_PREFIX_PATH=$library" -DBUILD_TESTING=ON "-DTHECAD_GUI_TESTS=$guiTestsValue"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& "$library\bin\cmake.exe" --build $buildDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
& "$library\bin\ctest.exe" --test-dir $buildDirectory -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
