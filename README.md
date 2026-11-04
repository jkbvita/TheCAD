# TheCAD

TheCAD is a standalone, open-source mechanical CAD application under development,
not a FreeCAD fork. The Windows prototype uses C++, Qt 6, Open CASCADE Technology
(OCCT), and CMake as owner-approved provisional choices.

## Current sketch-creation workflow

Click **Sketch** in the SKETCH tab, hover a labeled Front, Top, or Right plane
in the viewport, then click it to enter normal-view sketch editing. Alternatively,
select a visible plane or its reference in the part tree first, then click Sketch.
Esc cancels a pending plane choice without creating a sketch. No plane-selection
dropdown or confirmation dialog is used.

Front = XY, Top = XZ, and Right = YZ, preserving the existing saved sketch frames.
The finite reference graphics do not limit the infinite sketching plane. See the
[coordinate mapping and interaction rules](docs/requirements/graphical-plane-selection.md).

## Sketching and modeling

- Empty parts, origin-plane sketches and sketches on selected planar solid faces.
- Subtle translucent planes with blue/orange boundaries, dashed centerlines and plane-aligned labels.
- Model-space origin points and axis arrowheads; filled blue/orange sketch endpoints.
- Closed-region recognition alongside dangling open lines.
- Antialiased continuous lines, exact origin/endpoint snapping and always-visible endpoint markers.
- Select-mode endpoint dragging, transactional joining and connected-geometry updates.
- Rectangles made of four connected edges, and circles with snappable centers.
- Persistent geometric relations, PlaneGCS solving, editable driving/reference Smart Dimensions and constrained dragging.
- Multiple selected regions, shared line boundaries, nested loops/holes and multiple solid bodies per part.
- Light Features, Sketch and Evaluate command tabs, native menus and actual feature history.
- A compact part tree with Front, Top and Right planes, Origin, and modeling history.
  Used sketches appear beneath every feature that references them; each entry edits
  the same shared sketch and can be selected to create another feature.
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
the [parametric constraint guide](docs/requirements/parametric-constraints.md).

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
The provisional standalone PlaneGCS adapter solves supported dimensions and relations,
regenerating dependent features before accepting changes.

Point places selectable, draggable, dimensionable points with persistent snapping.
Centerline draws construction lines using the usual Line interaction; select a
line and use **For Construction** to convert it in either direction. Points and
construction geometry are excluded from extrusion boundaries.

Polygon creates a regular six-sided polygon: click the center, then set size and
orientation. There is no side-count control or separate polygon editor. Use Line
for other outlines. Edges/vertices support ordinary selection and constraints;
driving dimensions preserve regularity. Existing saved polygons remain readable.
See [basic sketch tool checks](docs/verification/basic-sketch-tools.md).

Arc, Spline, Trim, Convert,
Offset, Mirror, Linear Sketch Pattern, Move, Repair
Sketch, Shaded Sketch Contours, Measure, Mass Properties and Check Geometry are
disabled until implemented.

While editing a closed sketch, switch to Features and click Extrude or Extruded
Cut. The active sketch is used automatically; select the intended regions in the
viewport or left PropertyManager. Depth/reverse/Through All changes update the
preview live. Confirm adds the feature; Cancel returns to sketch editing with
completed geometry intact. Tab switching alone does not finish a sketch.

## Sketch relations

Hold **Shift** and click sketch entities to toggle a multiple selection. Release
Shift for the floating toolbar of applicable relations. Shift selection switches
the active drawing/dimension command to Select. The toolbar stays until you choose
a command, clear selection, press Esc or move sufficiently far away; it has no timer.
Use its **For Construction / Normal Geometry** command to convert selected entities.
The dedicated Centerline tool remains on the Sketch ribbon.

Green relation symbols support hover tooltips, selection and Delete without
removing geometry. **View > Hide/Show > Sketch Relations** controls their visibility.
**Display/Delete Relations** lists constraints associated with selected geometry
and supports deleting or changing a compatible relation. See the
[relation checks and supported types](docs/verification/sketch-relations-system.md).

## Smart Dimension

Activate Smart Dimension and select one line/circle, two points or two lines.
Move to preview, then click empty space to place. Enter the value in the small
Modify editor beside the annotation; Enter confirms and Escape/Cancel rejects.
Placement chooses horizontal/vertical/aligned point distance or the angular
sector. Parallel lines give perpendicular spacing; nonparallel lines give angle.
A circle defaults to diameter; R chooses radius and D diameter before placement.
The command stays active for repeated dimensions. Double-click an annotation to
edit it. Display/Delete Relations remains available for relation management.

Dimensions follow current geometry throughout endpoint dragging and numeric edits.
In Select mode or idle Smart Dimension, drag text or dimension lines to reposition
the annotation without changing its value. Circle dimensions use diametric/radial
lines and circumference arrows; dragging rotates their placement. Placements survive
save/reopen in `.thepart` schema 9; earlier supported files remain readable.
Smart Dimension and drawing buttons stay visibly pressed while active; Esc exits.
Geometry highlights on hover. Picking defaults to 8 logical pixels, adjustable for
the session under **Tools > Sketch Picking Tolerance**.
See the [manual checks and limits](docs/verification/dimension-graphics-selection.md).

**Ctrl+Z / Ctrl+Y** undo and redo the last ten completed modeling edits, including
geometry, dimensions and annotation placement. A new edit after Undo clears Redo;
New/Open clears session history. In Select mode, selected geometry/annotations
turn light blue. Press **Delete** or two consecutive **D** keys to remove them;
Ctrl+click additional contour edges to delete them together. Geometry deletion
also removes its attached dimensions; selecting/deleting only a dimension leaves
its geometry intact. See [history and deletion checks](docs/verification/edit-history-sketch-deletion.md).

See [current behavior and limits](docs/requirements/parametric-constraints.md) and
[verification](docs/verification/parametric-constraints.md). Whole-sketch DOF=0 is
required for black fully-defined geometry; mixed sketches remain blue.

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

`.theasmb` and `.thedrw` are reserved names. Assemblies, drawings, spatial sketches,
automatic dimensioning, resilient face reattachment, and permanent topology naming
are outside this milestone. Independent simple closed contours are supported;
strictly nested loops and multiple bodies are supported; shared existing line boundaries are supported; unsplit crossings/overlaps and
touching circle or legacy rectangle boundaries are rejected clearly.

## License status

The existing GPL v3 text in [LICENSE](LICENSE) is unchanged. Application license
confirmation remains an owner decision. Dependency notices are documented
separately; this milestone does not select or change the application license.
