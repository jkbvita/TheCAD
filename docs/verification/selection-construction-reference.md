# Selection, construction and reference geometry verification

Branch: `codex/unified-selection-construction`, based on local commit 545b88b.
The original checkout and all owner files are preserved. Research preceded code:
[direct SolidWorks observations and official references](../research/solidworks-selection-construction-2026-11-05.md).

## Automated verification

Release compilation succeeded and all **31 CTest cases passed** (79.75 seconds),
including 12 GUI cases and five runs at 150% scaling. The focused additions exercise:

- Typed candidate priorities, filters, explicit hidden alternatives, additive
  selection and generation invalidation without Qt.
- Normal/mixed/construction conversion preserving geometry and IDs, dimensions,
  relations, profile exclusion/restoration, history snapshots and native save/load.
- Hidden reference validity, persisted visibility and schema-13 compatibility.
- Real OCCT face, straight/circular edge, vertex and body picking, normal hover,
  Shift selection, F5/F6, zoom/DPI tolerance, occlusion, Select Other cancellation
  and selection of a deeper vertex. Seven-pixel offsets pick an edge, eleven-pixel
  offsets do not, at both tested zooms and display scales. Artificial periodic
  circle seam vertices are excluded.
- Cylindrical faces as axis preselection, without accepting them as sketch planes.
- Property checkbox mixed states, undo/redo, reference hide/show and save/reopen.
- Independent reference point/axis picking, mixed reference selection, three-point
  plane preselection and preservation of consecutive XYZ reference-point creation.
- Select Other lists overlapping origin planes by name and both coincident
  dimension annotations, and accepts the requested alternative.

GUI tests inspect OCCT owners, presentation styles and document state instead of
running repeated screenshot comparisons. Existing sketch/dimension/constraint,
region extrusion/cut, reference-feature, file-opening and camera tests also run.

Final standalone package: `build/package/TheCAD-Selection-Construction-Final/TheCAD.exe`.
Restart from the desktop TheCAD shortcut to load it. Previous packages and running
copies are preserved. Schema 14 persists reference visibility and reads supported
earlier documents; older application builds cannot read the new schema.

The final package's saved-file startup test also passed, using absolute and
relative Unicode/space paths with development Qt/shader overrides removed from the
child. Its SHA256 matches the tested Release executable:
`31FC08D9D71CC659757CF4B2F2BA4EF1880336E420B38DD2B8425EC6A33A4F51`.
Runtime verification resolves all 50 imported DLLs inside the 67.1 MiB package.
The desktop shortcut now targets this package; its previous version is backed up
at `build/desktop-shortcut-backup/TheCAD-before-unified-selection.lnk`.

Main changes are in `modeling/Selection`, the viewport's selection adapters,
`SelectionUi`, `SketchTools` construction-state commands, reference creation/picking,
and `PartDocument` visibility serialization. Tests are in `SelectionTests`,
`SelectionWorkflowTests`, and the existing document/workflow suites. No new
dependencies or licenses are introduced. No owner decision is required for the
completed milestone; persistent topology naming remains a separately approved
future project as described in decision 0025.

## Owner manual checklist

1. **Face:** click the interior of a planar body face. It fills light blue;
   hovering another item is orange. Click blank space to clear. A selected planar
   face can start a sketch; a cylindrical wall can be selected but cannot.
2. **Block edge:** click near an edge, then Shift-click another. Both stay blue;
   Shift-click one again removes only it. Orbit and repeat at your normal zoom.
3. **Hole rim:** click its circular rim, the surrounding top face, and the visible
   cylindrical wall separately. The corresponding edge or face alone highlights.
4. **Vertex:** click near a corner at different zooms. It selects the individual
   vertex, not an unrelated back corner. Use the Vertices filter to isolate it.
5. **Filters:** F5 shows the bottom toolbar. Choose Faces/Edges/Vertices/Sketch
   geometry/Reference geometry; only the chosen types can be picked. Multiple
   filters combine. F6 bypasses and restores them. Clear filters restores all
   types. Change Pick tolerance if needed; it is measured in logical pixels.
6. **Overlap:** right-click overlapping geometry → Select Other. Hover a candidate
   to preview it, Tab to cycle, click/Enter to accept, Escape to cancel. Deeper
   candidates are explicitly marked behind. Normal picking does not select them.
7. **Construction:** in an edited sketch select a line, then Shift-select a circle.
   Use For Construction in the left properties or right-click menu. Both become
   dashed and retain their dimensions and relations. Centerline still draws
   construction lines directly.
8. **Reverse/mixed:** select one normal and one construction curve. The checkbox
   shows mixed state. Check for all construction, clear for all normal. Undo/redo
   each conversion and verify the same entity IDs/dimensions remain attached.
9. **Profiles:** a construction circle or a boundary broken by a construction edge
   must not offer a closed extrusion/cut region. Convert back and check the region
   returns. Points must never become profile boundaries.
10. **Plane:** select a planar face and create an offset reference plane using the
    docked preview/Confirm controls; alternatively use three reference points.
    Select it in the tree or viewport, hide/show it, and start a dependent sketch.
11. **Axis:** select a cylindrical wall then Reference Axis. Its reference should
    be prefilled. Confirm, select/hide/show it. Also check the two-point method.
12. **Point:** create an XYZ reference point, or select a vertex and create a point
    on that reference. It has an independent FeatureManager item. Check it can
    participate in an axis or plane definition without becoming a sketch point.
13. **Save/reopen:** save construction entities and hidden/visible references,
    reopen via File → Open and by dropping the file onto the desktop shortcut.
    Flags, parameters, constraints and reference visibility must be restored.

## Known limits and inspection scope

Direct SolidWorks interaction succeeded on the prepared model. A disposable copy
was used for axis/plane creation. Following the owner's clarification that the
memory warning is a bug, line and circle construction conversion and reversal were
also directly exercised without changing their displayed parameters. Numeric pick
tolerance, mixed-state behavior and several advanced reference interactions remain
documentation-based, not claimed as directly tested in SolidWorks.

Runtime topology selections clear on regeneration; persistent operations continue
using conservative source/body signatures, not a new naming system. Unsupported
arcs/splines, coordinate systems and automatic temporary axes remain unavailable.
Select Other provides a candidate list rather than SolidWorks' full face-hiding
workflow. The primary visual/usability acceptance is the owner's manual testing.
