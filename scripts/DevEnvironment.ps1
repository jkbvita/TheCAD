function Initialize-TheCadEnvironment {
    param([string]$DependencyPrefix)
    if (-not $DependencyPrefix) {
        $DependencyPrefix = $env:THECAD_DEPS_PREFIX
    }
    if (-not $DependencyPrefix) {
        $DependencyPrefix = Join-Path $env:LOCALAPPDATA 'TheCAD\dev\prototype'
    }
    $library = Join-Path $DependencyPrefix 'Library'
    $cmake = Join-Path $library 'bin\cmake.exe'
    if (-not (Test-Path -LiteralPath $cmake)) {
        throw "Dependencies not found at $DependencyPrefix. See docs/build-windows.md."
    }
    $env:PATH = "$library\bin;$DependencyPrefix;$DependencyPrefix\Scripts;$env:PATH"
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $library 'lib\qt6\plugins\platforms'
    $env:CSF_ShadersDirectory = Join-Path $library 'share\opencascade\resources\Shaders'
    $env:CSF_XSMessage = Join-Path $library 'share\opencascade\resources\XSMessage'
    $env:CSF_SHMessage = Join-Path $library 'share\opencascade\resources\SHMessage'
    $env:CSF_STEPDefaults = Join-Path $library 'share\opencascade\resources\XSTEPResource'
    $env:CSF_UnitsDefinition = Join-Path $library 'share\opencascade\resources\UnitsAPI\Units.dat'
    return $library
}
