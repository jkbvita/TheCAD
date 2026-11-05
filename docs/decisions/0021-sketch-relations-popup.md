# 0021 — Reliable Shift-release sketch relations overlay

Status: owner-approved, 2026-11-05. Following the official-documentation check, the
owner explicitly requested implementing: Shift-select sketch entities, release
Shift, show applicable relations beside the cursor, then apply one and dismiss.
This repairs the approved workflow from 0015; it introduces no new constraint type.

## Activation and display

The toolbar is a native child widget of the viewport, raised above the OCCT native
surface. It no longer creates a separate nonactivating tool window. It retains
keyboard focus in the current sketch control while its buttons accept mouse input.
Its position is offset from the last selection cursor position and clamped inside
the viewport in logical pixels. Tooltips and original relation icons are retained.

The viewport owns the selection gesture. Completing it consumes the pending state
and emits a toolbar request only for a nonempty geometry selection in Select mode.
The application's existing event filter catches a genuine Shift release throughout
this MainWindow, including releases delivered to a side-panel control after a focus
change. Auto-repeat releases do not finish the gesture. Clicking empty space with
Shift or pressing/releasing Shift without changing selection does not open an old
popup. Leaving sketch edit, switching drawing tools or deactivating the window
cancels the pending gesture. Starting Shift selection clears a stale relation-icon
selection and continues to suspend drawing/Smart Dimension/inspector picking.

## Apply and dismiss

Commands use the existing type-based availableRelations list and solver-backed
transactional application. One click dismisses the bar and applies the chosen
relation to captured stable IDs. Rejected conflicts preserve the previous sketch;
successful application remains a single undoable edit. The bar is usable repeatedly
with subsequent selections. Construction conversion retains the existing behavior.

The bar also dismisses on selection/tool changes, outside clicks, Escape, moving
far from its cursor-to-toolbar corridor, viewport/window resize or movement,
navigation wheel input and application/window deactivation. Dismissal alone does
not modify the sketch. A guarded widget pointer clears on viewport destruction.

## Scope and references

The owner-approved Shift convention remains unchanged. SolidWorks documents
Ctrl-click for multi-selection, context toolbars on selection, and the Sketch
Relations context toolbar. Its right-click persistent variant is not added here.

- [Context toolbars](https://help.solidworks.com/2022/english/Solidworks/sldworks/c_context_toolbars.htm)
- [Sketch relations overview](https://help.solidworks.com/2025/english/Solidworks/sldworks/c_Sketch_Relations_Overview.htm)
- [Horizontal and vertical sketch relations](https://help.solidworks.com/2025/English/Solidworks/sldworks/c_horizontal_vertical_sketch_relations.htm)

No changes to PlaneGCS, saved schema, entity IDs, geometry or relation mathematics.
No dependencies or license changes. Live native UI appearance remains for owner
verification; automated checks are recorded in the verification checklist.
