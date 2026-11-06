# Plane, selection and navigation manual checks

Use the new desktop package after closing older TheCAD windows if necessary.
Reference Geometry → Plane opens the docked PropertyManager. Automatic inference
works with preselection and with supports picked after launch. Shift/Ctrl-click
adds viewport selections; the FeatureManager supports multiple selections.

1. Select Front Plane, invoke Plane, choose Coincident and confirm. Confirm is
   enabled at zero offset; canceling another draft must leave the part unchanged.
2. Create an offset plane, edit the distance and use Flip Offset. Watch the live
   translucent preview move along the normal.
3. Select two distinct parallel planes/faces and invoke Plane. Mid Plane should
   be inferred and appear halfway between them; repeat in the opposite order.
4. Edit either source plane or the named source solid's dimensions/extrusion.
   Verify the midplane and dependent sketches/features regenerate. Opposite normals
   must give the same midplane; coincident/nonparallel references must explain why
   the combination is invalid. Undo/Redo the source edit.
5. Create a three-point plane; also try parallel-through-point, perpendicular
   through an axis/point, angle about an edge and a cylinder tangent plane.
   Cylinder tangency needs a point on the cylinder or a parallel plane; Reverse
   selects the other parallel tangent. Invalid/incomplete references cannot confirm.
6. Hover a solid face: only its perimeter should turn orange. Move away to clear
   hover. Cylinder seams must not be presented as physical face boundaries.
7. Click the face: its entire fill turns blue (#06B4FF), with a blue
   boundary. Hovering the selected face must preserve blue. Click empty space to
   clear selection: the face returns to light gray (#D0D0D0). Repeat on a reference
   plane, which retains slight selected transparency, and in both themes.
8. Click a circular rim: only that edge should select, leaving adjacent faces
   unfilled. Try vertex and reference-point markers as well.
9. Rotate/pan/zoom first, then select references or solid features from the tree.
   Check multiple reference selections too. Selection must not change the camera.
10. Middle drag to orbit. Middle-click a vertex/edge/face, then middle-drag to check
    rotation around the picked geometry. Left drag must not rotate the camera.
11. Ctrl + middle drag pans without rotation.
12. Shift + middle drag zooms; the wheel zooms toward the cursor. Try 150% scaling.
13. Enter a 2D sketch, draw, orbit into 3D, pan/zoom and continue drawing. All new
    geometry stays on the original sketch plane. Navigation must not create or
    modify entities, constraints or dimensions. Near-edge-on drawing must reject
    unstable placement instead of jumping geometry.
14. Normal To / Ctrl+8 restores a face-on sketch view. Continue snapping, drawing
    and dragging; existing dimensions and constraints must remain intact.
15. Save/reopen `.thepart`, including desktop-shortcut launch. Verify plane methods,
    source names, visibility and dependencies. Edit sources again after reopening.

Persistent naming covers current primitive/prism roles and Boolean history. A
removed or ambiguously split support intentionally rejects the complete source
edit; reselect the support explicitly. Legacy names migrate only against a verified
original body signature. General tangent surfaces beyond cylinders are unsupported.

Automated tests include `document.plane-topology`, `ui.plane-navigation` and its
150%-scale variant, plus the existing document, camera, sketch, selection, startup
and solver suites. Tests cover source regeneration/save-reopen, split rejection,
mouse chords, fixed pivots/cursor zoom, oblique 2D sketch editing, near-edge-on
rejection, exact selection color, reference-plane transparency, light gray model
shading and automatic midplanes. Render-buffer checks verify that face hover
leaves the interior gray, selection fills it blue and clearing restores gray.

## Verified build — 2026-11-06

- Windows Release build succeeded (MSVC, Qt 6.8.4, OCCT 7.9.3).
- Complete CTest run: 36/36 passed, including normal and 150% GUI variants.
- Standalone package saved-file launch: passed with bundled DLLs and a system-only PATH; absolute and relative Unicode/spaced filenames tested.
- Runtime dependency scan passed: 50 imported DLLs, with bundled license notices.
- Desktop shortcut now targets `build/package/TheCAD-Planes-Selection-Navigation/TheCAD.exe` in this worktree. Its previous target/package and a backup shortcut were retained. Existing user windows were not closed.
- Built and packaged executable SHA-256 match: `0046EE6A1781F1E8492A5AD81DE43B25836492DF0523CA28403C2FDAFCD1CEEB`.
- The optional owner-provided saved-model test was not configured; generated saved-model fixtures were used.
- The original checkout and its untracked `untitled.tcpart` remain untouched. No solver changes or new dependencies.
