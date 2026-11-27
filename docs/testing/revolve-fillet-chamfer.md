# Basic revolve and edge features

Restart from the updated desktop shortcut after installation.

1. Sketch a closed profile beside a centerline. Click Revolved Boss/Base, select
   the sketch/regions and axis, and set 360 degrees. Check 90/180 degrees and
   Reverse. Use the two pick controls to select an axis or contours in the viewport.
2. Create a solid, then a profile through it. Use Revolved Cut and check the
   removed material preview, angle and direction. A non-intersecting cut must
   report an error and leave the part unchanged.
3. Select a solid edge and click Fillet. Set the radius, add/remove edges and
   confirm. Try a circular edge as well as a straight one. Repeat with Chamfer
   using its equal-distance parameter.
4. Double-click each new tree feature or use Edit Feature. Change parameters and
   selections; confirm, undo and redo. Cancel an edit and check the original
   geometry. Try an excessive blend size: confirmation must be disabled.
5. Orbit/zoom before selecting edges, references or contours. Selection and
   parameter changes must retain the camera. Check blue selected references and
   orange hover outlines in Dark and Light themes.
6. Save/reopen .thepart, edit the source sketch or upstream body parameters and
   verify regeneration. Export the result to STL/STEP. Existing sketches,
   Extrude/Cut and reference features should continue working.

Initial scope: solid single-direction revolves, constant-radius edge fillets and
equal-distance edge chamfers. Unsupported advanced variants are listed in
[decision 0031](../decisions/0031-basic-revolve-and-edge-features.md).

Verification (2026-11-07): Release build succeeded and all 43 CTest targets passed
in 43.10 seconds. New geometry checks cover full/partial/reverse revolves in XY,
XZ and YZ, axis-touching profiles, circle/torus profiles, multiple bodies, revolved
cuts, straight/circular-edge blends, source edits, persistent names, save/reopen,
preview isolation and transactional failures. UI workflows passed at 100% and
150% scaling for all four commands, viewport picking, editing, invalid-size
rejection, cancel, active-sketch resume, undo/redo and exact camera preservation.
Existing sketch, solver, reference, document and exchange regressions also passed.
The optional owner-saved-model startup case was not configured; synthetic fixtures
were used. The new feature tests use direct Qt events and geometry/state assertions. No
SolidWorks GUI sessions or manual visual review were performed for this batch.

Standalone package validation also passed: the complete solid-feature probe ran
with only packaged DLLs and Windows on PATH, and packaged absolute/relative
Unicode saved-file launch passed. The package has 61 imported runtime DLLs and is
83.2 MiB. Built/package TheCAD.exe SHA-256:
AA39FC49710D02FC06BB9776748491D4D498C2A258C7941F4BFA327F6DBAAA69.

The desktop shortcut targets build/package/
TheCAD-Revolve-Edge-Features-20261107/TheCAD.exe. The previous link is preserved in
build/desktop-shortcut-backup/TheCAD-before-revolve-edge-features-20261107.lnk.
Previous packages and the original checkout's untitled.tcpart remain untouched.
No owner decision remains pending for this batch.
