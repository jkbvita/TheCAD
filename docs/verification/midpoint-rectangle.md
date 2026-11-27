# Midpoint snapping and rectangle dragging

Use the desktop TheCAD shortcut after saving and closing an older running window.
The current branch is `codex/midpoint-snaps-rectangle-drag`, based on d1ce05c.
Its package is `build/package/TheCAD-Midpoint-Rectangle/TheCAD.exe` in the isolated
relations worktree; other checkouts and prior packages are preserved.

1. Draw a line away from the origin. In Select mode its midpoint should not have
   a permanent dot. Activate Point and approach the middle: an orange marker and
   Midpoint tooltip should appear. Move away: the marker should disappear. Click
   near it: a real Point and persistent green Midpoint relation should be created.
2. Activate Line and start or finish a segment at another line's midpoint. Repeat
   with Centerline. Move, rotate or resize the reference line: the attached point
   or endpoint must remain at its current midpoint. Repeat after save/reopen.
3. Drag a standalone point onto a line midpoint, including a point already on the
   line. Its existing relations should remain. Repeating the snap should not add
   duplicate midpoint relations. Select/Delete the green Midpoint callout to
   release the attachment while retaining geometry; test undo/redo.
4. Draw a rectangle starting at the origin and extending left/up as in the supplied
   screenshot. Drag the top-right corner with a slightly diagonal cursor motion:
   height should change while X stays on the origin axis. Drag the bottom-left
   corner diagonally: width should change while Y stays on the origin axis. The
   opposite corner should change both dimensions. The origin corner stays fixed.
5. Repeat rectangle creation in other quadrants and on Front, Top and Right planes.
   Add a driving width and drag to change height: width must remain unchanged.
   Check live dragging, Escape cancellation and one-step undo/redo.
6. Check midpoint hover/acquisition at different zoom levels and display scaling.
   Ordinary endpoint, origin, circle-center and dimension picking should continue
   working. Circle creation does not acquire line-midpoint cues in this milestone.

Release build and all 23 CTest cases passed, including normal/high-DPI GUI tests
and `sketch.midpoint-rectangle`. The tests cover stable references, persistent
mathematics, all free rectangle corners, origin anchoring and document validation.
New files use schema 11; supported earlier documents remain readable. Virtual
midpoints are not direct dimension targets: place a Point at the midpoint first.
