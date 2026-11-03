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

## Preserved modeling foundation

- Empty parts and editable origin-plane sketches on XY, XZ, and YZ.
- Model-coordinate lines, rectangles, and circles with numeric editing.
- Compact FEATURES and SKETCH toolbar tabs and an actual feature-history tree.
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
individual approval. The current task is limited to graphical sketch creation.
The existing prototype implements the approved operations without attempting to
reproduce the entire SolidWorks interface.

See [Windows build, run, and modeling tutorial](docs/build-windows.md).
With dependencies installed, run from the repository root:

```powershell
.\scripts\build.ps1 -GuiTests
.\scripts\run.ps1
```

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
- [Graphical sketch-plane requirements](docs/requirements/graphical-plane-selection.md)
- [Graphical plane-selection decision](docs/decisions/0004-graphical-origin-planes.md)
- [Parametric modeling requirements](docs/requirements/parametric-modeling.md)
- [Provisional technology approval](docs/decisions/0001-provisional-windows-prototype.md)
- [Current workflow and native-format approval](docs/decisions/0003-parametric-sketch-features.md)
- [Dependencies and licenses](docs/dependencies.md)
- [Native document structure and legacy compatibility](docs/file-formats.md)

`.theasmb` and `.thedrw` are reserved names. Assemblies, drawings, a constraint
solver, automatic dimensioning, face attachment, and permanent topology naming
are outside this milestone. Independent simple closed contours are supported;
nested, touching, or intersecting contours are rejected clearly.

## License status

The existing GPL v3 text in [LICENSE](LICENSE) is unchanged. Application license
confirmation remains an owner decision. Dependency notices are documented
separately; this milestone does not select or change the application license.
