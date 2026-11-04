# 0015 — Clean part tree and fixed hexagon command

Status: owner approved, 2026-11-04, through the request in this chat to match the
two supplied tree references in English and remove Polygon's editing controls.

## Part tree

The part is the single root. Its first entries are Front Plane, Top Plane,
Right Plane and Origin, in that order. The crossed-out reference rows (History,
Sensors, Design Binder, Annotations, Equations and Material) are omitted.
Unconsumed sketches and modeling features follow in their history order.

A sketch referenced by an Extrude or Extruded Cut is displayed as a child of
that feature and omitted from the root history rows. If multiple features use
one sketch, each displays a child referencing that same sketch. Selecting any
occurrence permits sketch editing or creating another feature from it. Editing
regenerates every dependent feature through the existing document transactions.
Collapse state and the selected occurrence are retained across UI refreshes.

This is a presentation of the existing ordered history and `sketchId` references;
no document ownership, dependency or file-format change is introduced.

## Polygon

The Polygon command always creates a regular six-sided polygon by center and
radius/orientation clicks. Remove both Sides and Edit Polygon from the UI.
Other outlines can be drawn with Line. Existing edge/vertex selection, dragging,
dimensions, relations, undo/redo and extrusion remain available.

This supersedes the variable-side UI and numeric editor approved in 0014.
Keep existing saved polygons and general modeling validation readable without
converting their geometry or changing schema 8. No new dependency is introduced.
