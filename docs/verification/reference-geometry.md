# 3D reference geometry verification

Branch: `codex/reference-geometry-3d`. Package in the isolated worktree:
`build/package/TheCAD-Reference-Geometry/TheCAD.exe`. Save and close older running
copies, then reopen the updated desktop TheCAD shortcut.

## Supported construction methods

| Feature | Methods |
| --- | --- |
| Point | XYZ; on point/vertex; axis–plane intersection; two-axis intersection; normal projection onto a plane |
| Axis | XYZ/direction; existing line/straight edge/axis or cylindrical/conical face; two points; two planes; point and plane normal |
| Plane | XYZ/normal; signed offset; three points; angle about an axis parallel to the source plane; parallel plane through a point |

References include Front/Top/Right, X/Y/Z, origin, earlier reference features,
sketch points/centers/midpoints/lines and supported body topology. A plane's origin
is its entered position, first point or inherited frame (as appropriate); reference
order determines normal/direction for three-point planes and two-point axes.

## Automated results

Release build and all 27 CTest cases pass, including nine GUI cases, four at 150%
scaling. The new document test covers construction methods, skew/parallel/zero/
collinear rejection, typed references, explicit dependencies, malformed schema and
self-reference rejection, stable IDs, save/reopen, conservative topology checks,
sketch-line regeneration, cylindrical-axis resolution and preservation of solid
feature identity. Non-axis-aligned sketch/extrude/cut and offset support movement
produce valid solids with expected volumes.

The GUI workflow creates three XYZ points, picks them in the viewport to create
an axis and tilted plane, sketches/extrudes on that plane, edits a defining point,
checks dependent regeneration, Undo/Redo, displayed-solid restoration and persistence.
It also tests zero direction, incomplete references, preselected-plane offset,
Cancel/Enter, dirty-state/history preservation and a coincident origin/datum pick
that must retain the datum feature dependency. All previous modeling/sketch tests
remain passing. Primary native visual review remains for the owner.

## Manual owner checks

1. Use Features → Reference Point (also Insert → Reference Geometry → Point).
   Enter XYZ for points `(0,0,0)`, `(40,0,0)` and `(0,30,20)`, confirming each.
   Check live point preview and named feature-tree entries. Use F to fit if needed.
2. Open Reference Axis → Two points/vertices. Click the first two points. Check
   the active reference advances and Confirm creates a dashed, labelled axis.
   Repeat using Two planes with Front/Top, or Line/edge/face/axis on a cylinder.
3. Open Reference Plane → Through three points and click the three points. Confirm,
   select that plane in the tree and click Sketch. Draw a closed profile and
   extrude it; check its direction follows the tilted local normal. Cut from an
   offset plane in the opposite direction and check the cavity.
4. Select Front/Top/Right, an existing reference plane or a planar solid face and
   open Reference Plane. Offset should be preselected. Type a signed distance;
   the preview should move. Try XYZ/normal and At angle about an axis too.
5. Open Reference Point → Axis and plane intersection. Choose Z axis and a plane
   offset from Front. Check the point lies at their intersection. Try projection
   and two-axis intersection. Parallel or skew references must disable Confirm.
6. Edit an earlier XYZ point or plane from the tree. Confirm changes and check
   dependent axes/planes/sketches/solids regenerate. Ctrl+Z/Ctrl+Y should undo/redo
   one completed feature edit, retaining IDs and the original solid on undo.
7. Enter zero direction or incomplete/invalid numeric text and press Enter: it
   must not create a feature. Cancel/Esc must restore the original part with no
   extra undo entry. Repeat after saving a clean part and verify it stays clean.
8. Save/reopen `.thepart`; check parameters, references, labels and sketch support.
   Repeat picking and panel use at your normal display scaling and viewport angles.

## Limits

These are datum features, not full spatial sketches or a 3D constraint backend.
The implementation covers the methods listed above; SolidWorks' additional tangent,
midplane, curved projection and distribution methods are not included. General
datum deletion/reordering and per-datum visibility controls remain future work.
Solid topology edits may require reselecting face/edge/vertex references; stable
sketch and datum references regenerate automatically. Older TheCAD builds cannot
open schema-13 files, while this build reads earlier supported documents. No new
owner decision is pending for the delivered scope.
