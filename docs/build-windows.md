# Build and run on Windows

## Prerequisites

- Windows x64 and a working desktop OpenGL graphics driver.
- Visual Studio 2022 Build Tools, the C++ desktop workload, and a Windows SDK.
- PowerShell and micromamba for the documented prebuilt dependency environment.
- Qt 6.8.4, OCCT 7.9.3, CMake 3.28 or newer (3.x), supplied below.

The tested path uses Release builds and dynamically linked, MSVC-compatible
conda-forge packages. This avoids building Qt and OCCT from source. The development SDK remains separate from the application runtime package.

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
& $micromamba create --yes --root-prefix (Join-Path $devRoot 'mamba') --prefix (Join-Path $devRoot 'sketch-solver') --file environment-sketch-solver-win-64.lock
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

For Explorer startup, double-click **Launch TheCAD.cmd** at the repository root.
Create an independent runtime folder with `.\scripts\package-windows.ps1`, then
launch `build/package/TheCAD/TheCAD.exe`. Keep the whole package folder together.
The development executable at `build/windows/Release/TheCAD.exe` still uses the
run script for its runtime paths. See [Windows deployment](windows-deployment.md)
for the exact local path, package contents and verification procedure.

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
save `.thepart` files and can open legacy `.tcpart` input. Saving a legacy part
requests a separate `.thepart` destination and leaves its original file unchanged.
Only parts are implemented; `.theasmb` and `.thedrw` are reserved. See the
[file format and compatibility guide](file-formats.md).

## Graphical sketch creation

Start from an empty part or keep the current part open. In the SKETCH tab, click
**Sketch**. Front, Top, and Right appear as translucent reference planes in the
viewport. Move the pointer to highlight a plane, then click it. The camera turns
normal to that plane and sketch editing begins without another dialog. The
selection graphics disappear; the sketch origin and local axes remain visible.

For the plane-first workflow, select a visible plane in the viewport or one of
the Origin references in the left tree, then click Sketch. The selected reference
is used immediately. Front stores XY, Top stores XZ, and Right stores YZ. Their
precise coordinate frames are documented in the
[graphical-plane requirements](requirements/graphical-plane-selection.md).

While choosing a plane, left-drag still orbits, middle/right-drag pans, the wheel
zooms, and F fits the visible references/model. A short left click selects; an
orbit drag does not. Clicking empty space creates nothing. Esc cancels a pending
choice, leaving the document unchanged. If projections overlap, the nearest
forward geometric hit wins, with Front/Top/Right as the equal-depth tie order.
Labels are not hit targets.

In the Sketch tab, Line starts with one click and continues through successive
clicks without reactivation. A preview follows the pointer. Near the origin or
an existing endpoint, the point highlights and the click uses its exact model
coordinates with a persistent coincident relation. Near-horizontal/vertical
segments receive the corresponding relation. Esc ends drawing and preserves
completed edges. Small blue endpoint markers remain visible. In Select mode,
left-drag an endpoint to move it and its connected geometry. Release near another
endpoint or the origin to join exactly with a Coincident relation. Conflicting
moves reject without changing the sketch; Esc cancels the drag. Middle/right
dragging and the wheel pan/zoom the sketch. Temporary dashed guides and H/V
indicators show active drawing inference; ordinary diagonal lines stay free.

Rectangle uses two opposite corners and creates four connected horizontal and
vertical edges. Circle uses a center and radius point; its center can snap to
the origin or an endpoint. Both show previews. Select an entity to edit its
properties in the left panel, or use Edit geometry. Supported connections
propagate during coordinate edits; incompatible edits, including moving a point
fixed to the origin, fail clearly without changing the draft. Exit Sketch
returns to normal modeling mode. The displayed plane boundaries are only guides:
sketching is not confined to them.

To extrude from the active sketch, switch to Features and choose Extrude or
Extruded Cut. Tab changes keep sketch editing active. The left PropertyManager
uses that sketch automatically, with an explicit contour choice. Choose a region
in the viewport, change depth/direction for a live preview, then Confirm. Cancel
returns to sketch editing without losing completed geometry. Open or invalid
profiles stay editable with a clear error. Completed sketch and feature editing
workflows remain available; Cut retains Through All. The viewport stays usable
for orbit, pan and zoom while the panel is open. There is no Update Preview step. Save the part as `.thepart`; reopening restores each sketch's stored
plane reference and coordinates. Planar-face sketching is described below.

This guide describes intended/current controls, not evidence of completed manual
testing. Consult the verification report for the interactions actually exercised.

See the [current sketcher/feature workflow checks](verification/sketcher-feature-workflow.md) and the earlier [sketch and desktop verification report](verification/sketch-relations-desktop.md) for this milestone, and the [graphical-plane report](verification/graphical-plane-selection.md) for earlier checks.

## Sketching on solid faces

In normal part mode, select a planar face and click Sketch, or click Sketch first
and then select the face. Draw in its local frame and use Extrude/Cut as usual.
Faces take picking priority over origin-plane graphics. Curved faces are rejected.
Saving now writes version 6, including solver-independent sketch dimensions and metadata. Changing its upstream support may
reject rather than reattach; see the documented conservative reference strategy.
Valid closed line profiles can be selected even with attached dangling lines.
Multiple regions, strict nested loops/holes and disconnected bodies are supported. Adjacent line-bounded cells may share existing complete edges/vertices; unsplit crossings and overlapping segments remain unsupported.

## Smart Dimension and solver relations

See [the current sketch interaction guide](requirements/parametric-constraints.md).
Activate Smart Dimension, select geometry and move to preview. Click empty space
to place, then enter mm (or degrees for an angle) in the floating Modify editor.
Enter confirms; Escape/Cancel preserves the prior model. Two parallel lines give
spacing, two nonparallel lines angle, two points H/V/aligned distance. Placement
chooses orientation/sector. Double-click a label to edit. No dimension form is
required on the left. Display/Delete Relations opens relation controls separately.

Fully defined color is based on whole-sketch DOF=0, not per-entity inference.
The adapter is Qt independent. Only the isolated PlaneGCS DLL compiles as C++20;
other code stays C++17. Its Eigen/Boost header prefix defaults to
%LOCALAPPDATA%/TheCAD/dev/sketch-solver; override with CMake
-DTHECAD_SOLVER_DEPS_PREFIX=C:/path/to/headers. Qt/OCCT still use the original SDK.
