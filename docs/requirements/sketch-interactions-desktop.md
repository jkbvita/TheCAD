# Fundamental sketch interactions and desktop layout

The owner approved this milestone on 2026-11-03. It builds on graphical origin
plane selection and the existing standalone C++ / Qt / OCCT implementation.

## Sketch geometry and relations

Each origin-plane sketch has a selectable geometric origin at exact local
coordinates (0, 0). Origin and endpoint snap candidates use a screen-space
tolerance, while committed geometry uses the exact referenced model coordinates.
Panning, zooming and high-DPI scaling must preserve this distinction.

Lines expose independently identifiable start and end points. Selected lines
show endpoint markers. A continuous Line operation previews each segment,
connects successive endpoints, and can close at its starting point. Esc cancels
the unfinished segment while preserving completed segments. Zero-length and
accidentally duplicate segments must be prevented.

The initial persistent relation system supports Coincident, Horizontal and
Vertical. Stable entity and point references, including the sketch origin, are
stored in the native part document. Coordinate edits must preserve supported
relations or fail clearly; they must never silently disconnect geometry.

New rectangles consist of connected edges with supported geometric relations.
Their corners can snap to the origin or endpoints. Circles preview radius after
selecting a center, which may snap to an exact point. Existing sketch plane
transforms, history, contour references and extrusion workflows remain intact.

A complete general-purpose solver, solver dependency, arbitrary face attachment,
advanced constraints and additional modeling operations are not approved here.

## Desktop organization

Use a light native desktop interface with these top-level menus:
File, Edit, View, Insert, Tools, Window and Help.

The command manager has Features, Sketch and Evaluate tabs above the main
viewport, organized similarly to the supplied SolidWorks 2025 screenshots.
Create original icons; do not reproduce proprietary graphics or branding.

Features retains Extrude and Extruded Cut. Sketch keeps existing working tools.
Requested future commands are visible but disabled with explanatory tooltips:
Smart Dimension, related line tools, Arc, Spline, Trim Entities, Convert Entities,
Offset Entities, Mirror Entities, Linear Sketch Pattern, Move Entities,
Display/Delete Relations, Repair Sketch and Shaded Sketch Contours.
Evaluate establishes the tab and leaves unsupported actions disabled.

The left panel presents the actual document, origin references and feature
history. Selecting sketch geometry exposes its properties. Use a light neutral
viewport, retain the orientation indicator and graphical plane-selection
workflow, and preserve rendering, navigation and DPI behavior.

## Independent Windows startup

Retain the development scripts and supply an Explorer-launchable entry point.
Prefer a Release folder containing the executable, required DLLs, Qt plugins
and OCCT resources without development tools or runtime reliance on Codex,
profiles or globally modified PATH settings.

Document packaging and the exact working launcher/executable path. Verify a
fresh-process launch and clearly distinguish native manual checks from
automated or static verification.

## Verification and Git

Use a new feature branch, preserve existing local work, build and run locally,
and test the approved sketch, snapping, persistence and extrusion interactions.
Add automated geometry, relation, snapping and serialization coverage.
Commit and push completed work without merging into main.
