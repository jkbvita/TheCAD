# Windows desktop launch and Release package

## Launch an existing build

Double-click **Launch TheCAD.cmd** in the repository root. It starts the packaged
application when available. Otherwise it starts the existing development build
using a process-local dependency environment and reports an error if the build
or dependencies are missing. It does not require a Codex session or a PowerShell
profile and does not change global PATH settings.

For a completely independent runtime, double-click:

`C:\Users\jakub\projects\TheCAD\build\package\TheCAD\TheCAD.exe`

Keep the whole `TheCAD` package folder together. The executable cannot be moved
away from its DLLs, Qt plugins, `qt.conf`, and OCCT resources.

## Create the package

Build Release first, then run from the repository root:

```powershell
.\scripts\build.ps1 -GuiTests
.\scripts\package-windows.ps1
```

If Windows blocks script execution, use a process-local policy:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1
```

The package script accepts the same `-DependencyPrefix` as the development
helpers. It recreates `build/package/TheCAD/`; custom `-OutputDirectory`
destinations must be children of `build/package/`. Do not put user documents
inside a generated package destination.

Packaging requires the existing SDK environment and Visual Studio C++ tools.
The resulting application folder does not require those tools at runtime.
No installer, signing, system registration, or global configuration is performed.

## Package contents and verification

The script uses Visual Studio's `dumpbin` through CMake's runtime dependency
scanner to follow the executable and selected Qt plugin imports recursively.
It copies only the required non-system DLLs, not an SDK directory. Windows API
and system DLLs are supplied by Windows.

The package includes:

- The Release executable and its imported OCCT, Qt and transitive runtime DLLs.
- The Microsoft C++ runtime DLLs from the pinned dependency environment.
- Qt's Windows platform and native style plugins, plus ICO, GIF and JPEG readers.
- An application-local `qt.conf` pointing Qt to `plugins/`.
- OCCT shader sources in `resources/Shaders/`, located by the application
  relative to its own executable.
- Dependency version/source metadata and supplied license notices in `licenses/`.

The script then repeats import resolution with only the package and Windows
system PATH. Missing DLLs or a dependency resolved outside the package cause a
failure. This static check does not replace launching the native desktop
application and checking its OpenGL viewport.

For a fresh-process launch check, use a new PowerShell process without a profile,
set PATH to the Windows system directories, clear Qt/OCCT development environment
variables, and start the packaged executable with a working directory outside the
repository. Check the actual window and viewport before reporting success.

## Limits

This is a local Windows x64 Release prototype package, not a signed installer.
It requires an OpenGL-capable graphics driver. Other Windows machines and clean
virtual machines have not been certified by the packaging script alone.

The dependency notices and source package URLs are retained with dynamically
linked, replaceable libraries. The repository's existing application LICENSE is
copied unchanged. Packaging does not make a new application licensing decision.
A public release's full source-offer and redistribution process remains future
release work.
