param(
    [string]$DependencyPrefix,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'DevEnvironment.ps1')
$library = Initialize-TheCadEnvironment $DependencyPrefix
$dependencyRoot = Split-Path $library
$repoRoot = Split-Path $PSScriptRoot
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
$executable = Join-Path $buildRoot 'windows\Release\TheCAD.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build Release first with scripts/build.ps1.' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $buildRoot 'package\TheCAD' }
$packageRoot = [IO.Path]::GetFullPath($OutputDirectory)
$packageParent = [IO.Path]::GetFullPath((Join-Path $buildRoot 'package'))
# Constrain cleanup to generated build outputs, including custom destinations.
if (-not $packageRoot.StartsWith($packageParent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "The package destination must be a child of $packageParent."
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'The Visual Studio C++ toolchain is needed to inspect runtime imports.' }
$compilerDirectory = Get-ChildItem -LiteralPath (Join-Path $visualStudio 'VC\Tools\MSVC') -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$dumpbin = Join-Path $compilerDirectory.FullName 'bin\Hostx64\x64\dumpbin.exe'
if (-not (Test-Path -LiteralPath $dumpbin)) { throw "Runtime dependency scanner not found: $dumpbin" }
if (Test-Path -LiteralPath $packageRoot) { Remove-Item -LiteralPath $packageRoot -Recurse -Force }
New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
Copy-Item -LiteralPath $executable -Destination $packageRoot

$pluginRoot = Join-Path $library 'lib\qt6\plugins'
$pluginNames = @('platforms\qwindows.dll', 'styles\qmodernwindowsstyle.dll',
    'imageformats\qico.dll', 'imageformats\qjpeg.dll', 'imageformats\qgif.dll')
$plugins = @()
foreach ($relative in $pluginNames) {
    $source = Join-Path $pluginRoot $relative
    if (-not (Test-Path -LiteralPath $source)) { throw "Required Qt plugin missing: $source" }
    $plugins += $source
    $destination = Join-Path (Join-Path $packageRoot 'plugins') $relative
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}
$dependencyList = Join-Path $buildRoot 'runtime-dependencies.txt'
$cmake = Join-Path $library 'bin\cmake.exe'
$scanArguments = @("-DAPP=$executable", "-DLIBRARY_BIN=$library\bin",
    "-DEXTRA_LIBRARIES=$($plugins -join ';')", "-DDUMPBIN=$dumpbin",
    "-DOUTPUT_FILE=$dependencyList", '-P', (Join-Path $PSScriptRoot 'RuntimeDependencies.cmake'))
& $cmake @scanArguments
if ($LASTEXITCODE -ne 0) { throw 'Runtime import discovery failed.' }
$dependencies = @(Get-Content -LiteralPath $dependencyList | Where-Object { $_ })
foreach ($source in $dependencies) { Copy-Item -LiteralPath $source -Destination $packageRoot }
# OCCT reads shader sources at runtime; main.cpp locates this relative directory.
$shaderSource = Join-Path $library 'share\opencascade\resources\Shaders'
$resourceDirectory = Join-Path $packageRoot 'resources'
New-Item -ItemType Directory -Path $resourceDirectory -Force | Out-Null
Copy-Item -LiteralPath $shaderSource -Destination $resourceDirectory -Recurse
@"
[Paths]
Prefix=.
Plugins=plugins
"@ | Set-Content -LiteralPath (Join-Path $packageRoot 'qt.conf') -Encoding ascii

# Retain the package licenses of every copied binary and the OCCT shader owner.
$shippedPaths = @($dependencies) + $plugins + @((Join-Path $shaderSource 'Declarations.glsl'))
$relativePaths = @{}
foreach ($source in $shippedPaths) {
    $relative = $source.Substring($dependencyRoot.Length + 1).Replace('\', '/').ToLowerInvariant()
    $relativePaths[$relative] = $true
}
$noticesRoot = Join-Path $packageRoot 'licenses'
New-Item -ItemType Directory -Path $noticesRoot -Force | Out-Null
$inventory = @()
foreach ($metadataFile in Get-ChildItem -LiteralPath (Join-Path $dependencyRoot 'conda-meta') -Filter '*.json') {
    $metadata = Get-Content -LiteralPath $metadataFile.FullName -Raw | ConvertFrom-Json
    $ownsFile = $false
    foreach ($file in $metadata.files) {
        if ($relativePaths.ContainsKey($file.Replace('\', '/').ToLowerInvariant())) { $ownsFile = $true; break }
    }
    if (-not $ownsFile) { continue }
    $noticeSource = Join-Path $metadata.link.source 'info\licenses'
    # The conda freetype split stores both license texts in its same-version header package.
    if ($metadata.name -eq 'libfreetype6' -and -not (Test-Path -LiteralPath $noticeSource)) {
        $ownerFile = Get-ChildItem -LiteralPath (Join-Path $dependencyRoot 'conda-meta') -Filter "freetype-$($metadata.version)-*.json" | Select-Object -First 1
        if ($ownerFile) {
            $owner = Get-Content -LiteralPath $ownerFile.FullName -Raw | ConvertFrom-Json
            $noticeSource = Join-Path $owner.link.source 'info\licenses'
        }
    }
    if (-not (Test-Path -LiteralPath $noticeSource)) { throw "License notices missing for $($metadata.name): $noticeSource" }
    $noticeDestination = Join-Path $noticesRoot $metadata.name
    Copy-Item -LiteralPath $noticeSource -Destination $noticeDestination -Recurse
    $inventory += [ordered]@{ name=$metadata.name; version=$metadata.version; build=$metadata.build; license=$metadata.license; source=$metadata.url }
}
$inventory | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $noticesRoot 'packages.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination (Join-Path $noticesRoot 'TheCAD-LICENSE.txt')
# OCCT stores its exception alongside the installed documentation, not info/licenses.
Copy-Item -LiteralPath (Join-Path $library 'share\doc\opencascade\OCCT_LGPL_EXCEPTION.txt') -Destination (Join-Path $noticesRoot 'occt\OCCT_LGPL_EXCEPTION.txt')
# LGPL v3 incorporates GPL v3; retain the existing full GPL v3 text as well.
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination (Join-Path $noticesRoot 'qt6-main\GPL-3.0.txt')
@'
TheCAD local Release package

Double-click TheCAD.exe. No Codex session, development shell, compiler or
modified system PATH is required. Keep all DLLs, plugins, resources and qt.conf
together. This folder may be moved as a unit on Windows x64.

An OpenGL-capable Windows graphics driver is required. Runtime DLLs remain
separate and replaceable. Third-party package versions, source package URLs
and supplied license notices are in licenses/. The repository's existing
application license text is retained; this does not select a new license.

TheCAD uses Qt 6 for its desktop interface and Open CASCADE Technology for
precise geometry and visualization. Their license notices are retained in
licenses/qt6-main/ and licenses/occt/, including the OCCT LGPL exception.

This is a local prototype package, not an installer or signed public release.
The build source and reproducible dependency lock are in the TheCAD repository:
https://github.com/jkbvita/TheCAD
'@ | Set-Content -LiteralPath (Join-Path $packageRoot 'README.txt') -Encoding utf8

# Repeat the import scan using only packaged DLLs and the Windows system PATH.
# This detects accidental dependencies on the development environment.
$packagedPlugins = @($pluginNames | ForEach-Object { Join-Path (Join-Path $packageRoot 'plugins') $_ })
$verificationList = Join-Path $buildRoot 'package-runtime-dependencies.txt'
$savedPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot;$(Split-Path $dumpbin)"
    $verifyArguments = @("-DAPP=$packageRoot\TheCAD.exe", "-DLIBRARY_BIN=$packageRoot",
        "-DEXTRA_LIBRARIES=$($packagedPlugins -join ';')", "-DDUMPBIN=$dumpbin",
        "-DOUTPUT_FILE=$verificationList", '-P', (Join-Path $PSScriptRoot 'RuntimeDependencies.cmake'))
    & $cmake @verifyArguments
    if ($LASTEXITCODE -ne 0) { throw 'Packaged runtime dependency verification failed.' }
    foreach ($resolved in Get-Content -LiteralPath $verificationList) {
        if ($resolved -and -not [IO.Path]::GetFullPath($resolved).StartsWith($packageRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Packaged dependency escaped the package: $resolved"
        }
    }
} finally { $env:PATH = $savedPath }
$size = (Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Measure-Object Length -Sum).Sum
Write-Output "Created $packageRoot ($([math]::Round($size / 1MB, 1)) MiB; $($dependencies.Count) imported runtime DLLs)."
Write-Output "Double-click: $(Join-Path $packageRoot 'TheCAD.exe')"
