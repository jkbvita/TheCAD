# TheCAD

TheCAD is a standalone, open-source mechanical CAD application under development,
not a FreeCAD fork. The Windows prototype uses C++, Qt 6, Open CASCADE Technology
(OCCT), and CMake as owner-approved provisional choices.

## Current sketch-creation workflow

Click **Sketch** in the SKETCH tab, hover a labeled Front, Top, or Right plane
in the viewport, then click it to enter normal-view sketch editing. Alternatively,
select a visible plane or its Origin tree reference first, then click Sketch.
Esc cancels a pending plane choice without creating a sketch. No plane-selection
dropdown or confirmation dialog is used.

Front = XY, Top = XZ, and Right = YZ, preserving the existing saved sketch frames.
The finite reference graphics do not limit the infinite sketching plane. See the
[coordinate mapping and interaction rules](docs/requirements/graphical-plane-selection.md).

## Sketching and modeling

- Empty parts, origin-plane sketches and sketches on selected planar solid faces.
- Distinct translucent reference planes and model-space global axes.
- Closed-region recognition alongside dangling open lines.
- Antialiased continuous lines, exact origin/endpoint snapping and always-visible endpoint markers.
- Select-mode endpoint dragging, transactional joining and connected-geometry updates.
- Rectangles made of four connected edges, and circles with snappable centers.
- Persistent coincident, horizontal and vertical relations with safe coordinate editing.
- Light Features, Sketch and Evaluate command tabs, native menus and actual feature history.
- Context entity properties and a docked Extrude/Cut PropertyManager with live previews.
- Direct active-sketch Extrude/Cut with safe cancel/resume; unfinished tools stay disabled.
- Explicit source-sketch and closed-contour selection, length/depth, direction,
  preview, and confirmation for additive Extrude and Extruded Cut.
- Specified-depth and through-all cuts using precise OCCT BRep operations.
- Transactional regeneration after sketch or feature edits.
- Versioned `.thepart` storage of the editable history and parameters.
- Read-only legacy `.tcpart` import with conversion to a separate `.thepart` path.

The window starts maximized with standard Windows controls. The full CAD viewport
retains orbit, pan, zoom, Fit All, and Isometric navigation. Native File commands
provide New, Open, Save, Save As, and Exit with unsaved-change handling.

The short-term goal is to reproduce the fundamental SolidWorks modeling workflows
as faithfully as possible before developing alternatives; redesigns require
individual approval. This milestone improves fundamental sketch interactions,
relations, the familiar desktop layout and independent Windows deployment.

See [face sketching and contour checks](docs/verification/reference-faces-contours.md),
the [attachment strategy](docs/decisions/0008-reference-face-attachments.md), and
the [Smart Dimension proposal](docs/requirements/smart-dimension-proposal.md).

See [Windows build, run, and modeling tutorial](docs/build-windows.md).
With dependencies installed, run from the repository root:

```powershell
.\scripts\build.ps1 -GuiTests
.\scripts\run.ps1
```

## Launch from Windows Explorer

Double-click **Launch TheCAD.cmd** in the repository root. To create an independent
Release folder after building, run `.\scripts\package-windows.ps1`, then double-click
`build/package/TheCAD/TheCAD.exe`. On this computer the executable is
`C:\Users\jakub\projects\TheCAD\build\package\TheCAD\TheCAD.exe`.
Keep its DLLs, plugins and resources together.
See [Windows deployment](docs/windows-deployment.md) for the exact local path and details.

## Sketch interaction

In an active sketch, choose Line and click near the red local origin to start
exactly at (0,0). Each next click finishes an edge and continues from its endpoint.
Snap back to a starting point to close a contour. Near-horizontal/vertical edges
show dashed guides and an H/V indicator, then receive the corresponding relation. Esc stops drawing and keeps all
completed edges. Endpoints stay visible throughout sketch editing. In Select mode, drag a point
to move connected geometry, or release near another endpoint/origin to create
a persistent connection. Esc cancels a drag without changing the sketch. Rectangle uses two opposite corners; Circle uses a
center and a radius point. The origin stays at its true position during pan/zoom.

The property panel propagates supported relations when coordinates change. An
edit that conflicts with the fixed origin or other equalities is rejected.
This is a limited equality system, without a general solver or dimensions.

Smart Dimension, Centerline, Construction Line, Arc, Spline, Trim, Convert,
Offset, Mirror, Linear Sketch Pattern, Move, Display/Delete Relations, Repair
Sketch, Shaded Sketch Contours, Measure, Mass Properties and Check Geometry are
disabled until implemented.

While editing a closed sketch, switch to Features and click Extrude or Extruded
Cut. The active sketch is used automatically; select the intended contour in the
viewport or left PropertyManager. Depth/reverse/Through All changes update the
preview live. Confirm adds the feature; Cancel returns to sketch editing with
completed geometry intact. Tab switching alone does not finish a sketch.

## Source layout

```text
src/main.cpp       Application startup
src/modeling/      Qt-independent sketch data and precise OCCT geometry operations
src/document/      Ordered feature history, regeneration, validation, and file storage
src/ui/            Qt windows, commands, dialogs, tree, and CAD viewport
scripts/           Windows environment, build, and run helpers
tests/             Geometry, history, serialization, viewport, and GUI workflow tests
docs/requirements/ Owner-approved scope and project vision
docs/decisions/    Approval records and unresolved decisions
```

## Project documentation

- [Development rules](AGENTS.md)
- [Documentation guide](docs/README.md)
- [Project vision and principles](docs/requirements/project-vision.md)
- [Sketcher and feature workflow requirements](docs/requirements/sketcher-feature-workflow.md)
- [Sketcher/PropertyManager decision](docs/decisions/0007-sketcher-feature-workflow.md)
- [Current verification](docs/verification/sketcher-feature-workflow.md)
- [Sketch interaction and desktop requirements](docs/requirements/sketch-interactions-desktop.md)
- [Foundational relations](docs/requirements/sketch-relations.md)
- [Provisional relation representation](docs/decisions/0005-sketch-relations.md)
- [Desktop layout and deployment approval](docs/decisions/0006-sketch-desktop-layout.md)
- [Graphical sketch-plane requirements](docs/requirements/graphical-plane-selection.md)
- [Graphical plane-selection decision](docs/decisions/0004-graphical-origin-planes.md)
- [Parametric modeling requirements](docs/requirements/parametric-modeling.md)
- [Provisional technology approval](docs/decisions/0001-provisional-windows-prototype.md)
- [Current workflow and native-format approval](docs/decisions/0003-parametric-sketch-features.md)
- [Dependencies and licenses](docs/dependencies.md)
- [Native document structure and legacy compatibility](docs/file-formats.md)

`.theasmb` and `.thedrw` are reserved names. Assemblies, drawings, a general-purpose constraint
solver, automatic dimensioning, resilient face reattachment, and permanent topology naming
are outside this milestone. Independent simple closed contours are supported;
nested, touching, or intersecting contours are rejected clearly.

## License status

The existing GPL v3 text in [LICENSE](LICENSE) is unchanged. Application license
confirmation remains an owner decision. Dependency notices are documented
separately; this milestone does not select or change the application license.
