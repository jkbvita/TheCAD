# Reference geometry, camera and theme verification

Use Reference Geometry on the Features tab. Reference drafts occupy the docked
PropertyManager. Automatic inference is enabled initially. Select supports in the
viewport, FeatureManager or reference lists; Clear removes one support. Choose a
specific method to restrict the lists, or XYZ for direct numeric entry. Enter
confirms a valid definition; Esc cancels. Edit Feature reopens a custom reference.

| Feature | Supported methods |
| --- | --- |
| Plane | Offset from a plane/planar face; three non-collinear points; angle through an axis/straight edge relative to a parallel reference plane; plane parallel to a reference plane through a point; XYZ position and normal |
| Axis | Straight edge/sketch line/reference axis; cylindrical/conical face; two points/vertices; intersection of two planes; normal to a plane through a point; XYZ position and direction |
| Point | XYZ; existing point/vertex; center of a circular solid edge or sketch circle; axis–plane or axis–axis intersection; point projected onto a plane |

Angled planes contain the selected axis and make the requested angle with the
reference plane. Non-planar normal-axis support, tangent/mid-planes, face-center
points and points distributed along curves remain unsupported. Solid subshape
references retain conservative topology signatures: if their source solid changes,
regeneration rejects an uncertain support instead of reattaching it. References
to datums and stable sketch entities update associatively.

## Owner's manual checklist

1. Create an offset plane, edit its distance, delete it with Delete and with its
   context menu; check Undo and Redo.
2. Create an axis from an edge or cylindrical face; select, hide/show, edit and
   delete it.
3. Create an XYZ point and a circular-edge-center point; edit and delete them.
4. Sketch/extrude on a custom plane. Delete the plane: check the exact dependent
   names, cancel safely, then explicitly approve the full deletion. Undo restores
   the plane, sketch and solid together. Try multi-selecting independent references.
5. Rotate/zoom/pan, then select solids, sketches, origin planes and custom
   references from the FeatureManager. The camera should remain unchanged.
6. Select and hover solid faces, edges and vertices. Try overlapping references,
   selection filters and blank-space deselection without camera movement.
7. Try three-point and angled planes; two-point, two-plane and normal axes;
   point intersections. Add, replace and clear references; invalid combinations
   must disable Confirm. Edit an existing reference and cancel a preview.
8. In Dark, inspect solid edges, underdefined/defined sketches, construction
   curves, green relations, dimension text/arrows and translucent reference planes.
9. Edit → Preferences: switch Dark/Light/Follow system. Restart and verify the
   chosen theme returns. Changing theme must not mark the part modified or alter
   the camera. Native Windows title-bar controls remain under Windows/Qt control.
10. Save/reopen a `.thepart`, including one launched by dropping it on the desktop
    shortcut. Verify reference definitions, supports, visibility and deletion
    results persist, and the saved solid is visible.

## Automated coverage

`document.reference-geometry` exercises every existing datum method, degeneracy,
associative regeneration, circular-edge centers, exact transitive dependency
reporting, protected references, transactional deletion and save/reopen.

`ui.reference-managers` and `.hidpi` exercise actual tree and viewport picks;
camera eye/target/up/scale/projection preservation; feature/reference editing and
previews; multiple deletion, Undo/Redo; the real default-Cancel dependency dialog;
automatic inference and clearing; circular-edge picking; theme persistence and
unchanged document state. The existing light-theme solver, document, modeling,
startup, sketch, selection and graphical-plane regressions also remain enabled.

Verified on Windows 11 with MSVC 2022, Qt 6.8.4 and OCCT 7.9.3: Release builds
successfully, and all 33 CTest targets pass across the full run and focused
normal/150%-scale modeling reruns. Preview-volume tests measure the actual
mouse-created profile area instead of assuming pixel clicks produce exact
integer dimensions. The packaged startup test also passes with bundled runtime
dependencies and absolute/relative filenames containing Unicode and spaces.
Its optional owner-file test remains intentionally skipped unless a path is
provided. `git diff --check` is clean.

The separate portable package is
`build/package/TheCAD-Reference-Managers-Dark-Theme`; its executable SHA-256
matches the Release executable. The desktop shortcut was backed up and pointed
to this package only after verifying it still targeted the prior accepted build.
One normal-UI visual check confirmed the dark workspace and docked PropertyManager
layout. Earlier packages, owner documents and existing application windows remain.
