# Build and run on Windows

## Prerequisites

- Windows x64 and a working desktop OpenGL graphics driver.
- Visual Studio 2022 Build Tools, the C++ desktop workload, and a Windows SDK.
- PowerShell and micromamba for the documented prebuilt dependency environment.
- Qt 6.8.4, OCCT 7.9.3, CMake 3.28 or newer (3.x), supplied below.

The tested path uses Release builds and dynamically linked, MSVC-compatible
conda-forge packages. This avoids building Qt and OCCT from source. It is a local
prototype development setup, not a standalone distributable installer.

## Install the compiler

From PowerShell (elevation may be requested by the installer):

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --source winget --accept-source-agreements --accept-package-agreements --silent --override '--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
```

## Install the dependencies

Use an existing micromamba installation or bootstrap it into a user-local folder
using the [official Windows package endpoint](https://mamba.readthedocs.io/en/latest/installation/micromamba-installation.html):

```powershell
$devRoot = Join-Path $env:LOCALAPPDATA 'TheCAD\dev'
New-Item -ItemType Directory -Force -Path $devRoot | Out-Null
Invoke-WebRequest -Uri 'https://micro.mamba.pm/api/micromamba/win-64/latest' -OutFile (Join-Path $devRoot 'micromamba.tar.bz2')
tar -xjf (Join-Path $devRoot 'micromamba.tar.bz2') -C $devRoot Library/bin/micromamba.exe
$micromamba = Join-Path $devRoot 'Library\bin\micromamba.exe'
$deps = Join-Path $devRoot 'prototype'
& $micromamba create --yes --root-prefix (Join-Path $devRoot 'mamba') --prefix $deps --file environment-win-64.lock
```

Run the final command from the repository root. The explicit lock pins all
packages and hashes from the tested Windows environment. To resolve a fresh
compatible environment instead, use `--file environment.yml` with
`--override-channels --channel conda-forge`. The manifest selects OCCT's `novtk`
variant; VTK is not required by this application.

Dependencies remain outside the repository. No global PATH changes or shell
activation are needed for the helper scripts. The downloaded bootstrap package
manager itself is not pinned by the environment lock.

## Build and test

From the repository root:

```powershell
.\scripts\build.ps1
```

This configures a Visual Studio x64 build in `build/windows/`, compiles Release,
and runs the geometry and part-document tests. To also test the actual native viewport and camera
navigation on a desktop with OpenGL:

```powershell
.\scripts\build.ps1 -GuiTests
```

The GUI test briefly opens a window and writes
`build/windows/test-artifacts/viewport.png`. It verifies initialization,
orbit, pan, wheel zoom, native render-surface dimensions across maximize/restore/
resize, and rendered image content. A second run uses 150% DPI. The document UI
suite checks File actions, Save/Save As, dirty prompts, and error handling. It is opt-in
because unattended machines may have no graphics context.

If script execution is blocked, use a process-local policy when permitted:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -GuiTests
```

## Run

```powershell
.\scripts\run.ps1
```

The executable is `build/windows/Release/TheCAD.exe`. The run script supplies DLL,
Qt platform-plugin, and OCCT shader paths. Running the executable directly from
Explorer requires equivalent runtime paths; deployment packaging is not included.

Controls for this prototype:

| Action | Control |
| --- | --- |
| Orbit | Left mouse drag |
| Pan | Middle or right mouse drag |
| Zoom | Mouse wheel / vertical trackpad scroll |
| Fit solid | F or Fit all toolbar action |
| Reset isometric view and fit | Home or Isometric toolbar action |

These bindings are provisional; configurable workflows are outside this task.

For another dependency location, pass `-DependencyPrefix 'C:\path\to\environment'`
to either script, or set `THECAD_DEPS_PREFIX`. The helpers expect the documented
conda-style `Library/` layout. For a different SDK layout, configure CMake with
`CMAKE_PREFIX_PATH` pointing to the Qt and OCCT SDK roots and supply their runtime
DLL/plugin/resource paths yourself. Use MSVC-compatible x64 Release libraries.

## Troubleshooting

- Missing Visual Studio generator: install the C++ workload and Windows SDK.
- Missing Qt/OCCT at configure time: check the dependency prefix.
- Qt platform-plugin or DLL errors: use the run script with the correct prefix.
- Viewport initialization failure: check the graphics driver and availability of
  desktop OpenGL. The application reports an initialization error dialog.
- Debug builds are not covered by this prebuilt Release SDK setup.

## Native part documents

The application starts maximized with standard Windows controls. Use File > New,
Open, Save, Save As, or Exit and the standard shortcuts. Native Windows dialogs
accept .tcpart files. Read the [file format and lifecycle guide](file-formats.md).
Only parts are implemented; .tcasmb and .tcdrw are reserved names.
