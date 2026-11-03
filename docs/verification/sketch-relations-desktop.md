# Sketch relations, desktop layout and independent launch verification

Date: 2026-11-03
Branch: codex/sketch-relations-and-desktop-layout
Baseline: ff2f354 (graphical origin-plane selection)

## Build and automated checks

Release built locally using the existing MSVC / Qt 6.8.4 / OCCT 7.9.3 environment:

~~~powershell
.\scripts\build.ps1 -GuiTests
.\scripts\package-windows.ps1
~~~

Final CTest result: **12/12 suites passed**. Desktop suites run serially.

| Suite | Coverage |
| --- | --- |
| modeling.box | Existing precise solid construction |
| modeling.sketch | Closed contours, extrusion/cut, persistent equality relations, connected edits, fixed-origin conflicts and malformed references |
| modeling.reference-planes | Exact finite origin-plane geometry and ray picking |
| document.part | Version 3 relation serialization, exact round trips, legacy 1/2 compatibility, transactional regeneration and invalid inputs |
| ui.viewport / ui.viewport.hidpi | Native render surface, maximize/restore/resize, navigation and model-coordinate input at normal/150% scaling |
| ui.documents | File actions, dirty guards, menus, disabled command tooltips/icons, light widget palette and relation-aware property edits |
| ui.modeling-workflow | Four-edge rectangle, two explicit regions, extrusion preview/confirmation, circle cut, feature edits and save/reopen |
| ui.graphical-planes / ui.graphical-planes.hidpi | Plane-first/sketch-first, all three frames, selection/cancel, drawing and persistence |
| ui.sketch-interaction / ui.sketch-interaction.hidpi | Pixel-based snapping, exact origin/endpoint connections, continuous lines/closure, zero/duplicate prevention, selected markers, relational rectangles/circles, pan/zoom snapping and saved extrusion |

The new interaction suites additionally check drawing anchors after coordinate
edits/removal, rejection of sub-tolerance geometry without model mutation, and
correct camera normal/up/center when entering each plane on an existing solid.

Detailed logs are generated in build/windows/*-results.txt and
build/windows/Testing/Temporary/LastTest.log. These are local build artifacts.

## Native desktop checks actually performed

The packaged executable was launched in a fresh Windows process with a working
directory outside the repository, PATH limited to Windows system directories,
and Qt/OCCT development variables removed. Its window and OpenGL viewport were
then inspected through the native computer-use interface.

- Confirmed maximized startup, conventional title controls, readable light
  menus/tree/commands and neutral full-size viewport. Restored and maximized
  the final packaged window and verified that rendering filled the viewport.
- Selected Front Plane and entered a sketch at the centered local origin.
- Activated Line, clicked near the origin, then drew three connected segments
  and a fourth back to the origin without reactivating the tool.
- Used Esc and confirmed completed lines remained. Selected a line body,
  observed endpoint markers, then selected an endpoint independently; the
  status bar identified it as an endpoint.
- Picked the actual enclosed region in Extrude, observed a valid solid preview,
  and confirmed it as Extrude1 in history.
- Created a separate rectangle from the origin. Observed its pointer preview
  and four connected edge entries after placing the opposite corner.
- Zoomed a separate sketch, snapped a circle center to the origin, observed
  the radius preview and completed the circle. The property panel showed exact
  center (0,0) and one supported relation.
- Saved through the native Windows dialog as
  build/windows/test-artifacts/manual-sketch-relations.thepart.
- Inspected that actual JSON: Sketch1 has four lines/nine relations, Sketch2
  has four lines/nine relations, Sketch3 has one circle/one relation. All loop
  endpoint pairs compare exactly equal; line/rectangle/circle anchors are
  exactly (0,0). The file uses schema 3.
- Closed the application, refreshed the package after visual fixes, started a
  fresh process, and opened the same file using the native Open dialog. All
  four features and the regenerated solid returned. Re-entered Sketch3 and
  confirmed its center, radius and relation persisted; its circle remained
  visible over the existing body with the correct camera frame.
- Used Save again after reopening; it reused the current path without a dialog.
- Ran the root Launch TheCAD.cmd in a fresh no-profile process with system-only
  PATH; it returned success and opened the packaged application. Inspected
  its actual window and viewport. The application was left open for the owner.

Native mouse panning was covered by the automated GUI suites, including exact
snapping after pan and zoom. The manual session verified wheel zoom; it does
not claim a separate manual middle-button pan drag or manual 150% DPI session.
Save As/dirty guards/invalid-file behavior were exercised by automated UI tests;
this report does not relabel those checks as manual.

## Problems found and fixed during verification

- The Windows dark widget theme ignored portions of the light per-window
  palette. Qt's palette-aware Fusion widget style now provides readable light
  controls; normal Windows title controls and native file dialogs remain.
- Centering OCCT's target after setting the plane normal could change the view
  direction on an existing off-origin solid. Target centering now precedes
  the final normal/up orientation; all origin planes have regression coverage.
- Existing solids could occlude an active sketch. Sketch geometry, reference
  markers and guides now use OCCT's topmost presentation layer.
- A cut-only checkbox was constructed outside the Extrude form and appeared
  over its instructions. It is explicitly hidden for additive operations.
- Entity properties now hide circle-only/endpoint-only rows as appropriate.

## Deployment evidence and limits

The local independent runtime is approximately 59.4 MiB with 49 imported runtime
DLLs, selected Qt plugins, OCCT shaders and third-party notices. The packaging
script resolves imports again using only the package and Windows system paths;
missing or externally resolved runtime DLLs fail packaging. The packaged and
built executable hashes match.

Double-click C:\Users\jakub\projects\TheCAD\Launch TheCAD.cmd, or the independent
C:\Users\jakub\projects\TheCAD\build\package\TheCAD\TheCAD.exe. Keep the package
folder and its DLLs/plugins/resources together. See [deployment](../windows-deployment.md).

This remains a local Windows x64 prototype, without a signed installer or
certification on other machines. Relations support coordinate equalities only;
there is no complete constraint solver, dimension system, face attachment or
advanced sketch/modeling operation. Unsupported commands stay visibly disabled.
No new dependency or application-license choice was introduced. Earlier
relation-free documents retain their geometry without inventing new relations.

The pre-existing root untitled.tcpart was preserved and excluded from the commit.
The sketch that appeared during initial desktop testing was saved separately as
build/windows/test-artifacts/preserved-startup-sketch.thepart before restarting.
