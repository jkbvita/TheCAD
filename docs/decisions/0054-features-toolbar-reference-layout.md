# 0054. Features toolbar matched to the owner's reference

On 2026-11-15 the owner supplied a SolidWorks Features toolbar screenshot and
requested a closer visual match to it. This revises the layout and the treatment
of future commands in [0052](0052-features-commandmanager.md). It does not approve
or implement additional modeling features.

Use narrow command columns, 20-logical-pixel icons, compact labels and three
26-pixel rows. Keep the primary Extruded/Revolved Boss/Base and Extruded Cut
commands as full-height columns. Put Revolved Cut in its own full-height column;
stack Sweep/Loft/Boundary in the adjacent Boss/Base and Cut columns. Place
Fillet and Linear Pattern together, Mirror in the bottom row of the adjacent
dress-up grid, and Reference Geometry beside Curves. Use three subtle separators
and retain horizontal scrolling in narrow windows and the existing tabs below.

The screenshot includes Loft/Boundary, Hole Wizard, Rib, Draft, Shell, Wrap,
Intersect and Curves commands that TheCAD does not implement. Show those entries
disabled, with explicit "not implemented" tooltips. They have no modeling
callbacks. Every enabled command continues to use its existing QAction/workflow.
These entries are a layout detail, not implementations of those future features.

Use full-width bottom menu buttons rather than side-arrow gutters for Fillet,
Pattern and Reference Geometry. Fillet/Pattern retain the last selected command;
their main button executes it and the bottom arrow opens alternatives. A disabled
last-used command must not disable available alternatives. Preserve keyboard menu
access, menu validation and native Qt action dispatch. Expand a narrow column
when needed to keep a retained command's full label readable.

The Features page uses the existing Dark/Light palettes and original painted
icons, including additional original icons for the disabled entries. No
proprietary icons/resources were copied. The shared assembly button factory
retains its previous size/behavior. Geometry, persistence, solver and assembly
document behavior are unchanged. There are no new dependencies.

## Verification

The Release application compiled successfully. Six CTest suites passed:
`architecture.boundaries`, `ui.documents`, `ui.solid-features`,
`ui.assembly-workflow`, `ui.feature-commandbar`, and
`ui.feature-commandbar.hidpi`. The two pre-existing optional saved-model fillet
cases were not configured and were skipped.

Toolbar tests cover both themes, 150% scaling, column/row alignment, disabled
entries, primary action clicks, bottom-arrow menus, retained choices, keyboard
opening, disabled-choice fallback, reference-command enablement, narrow-window
scrolling and unchanged assembly button sizing. Rendered Dark/Light toolbar
images were checked against the owner's supplied layout. No local SolidWorks
GUI interaction was performed for this adjustment.

Manual check: inspect the three-row layout; use the Fillet/Chamfer and
Linear/Circular Pattern arrows, click their main buttons, open each Reference
Geometry command, and verify the unavailable entries stay disabled. Check both
themes and a narrow window. No owner decision remains pending for this change.

Follow-up: the first check covered a compact window and missed horizontal grid
expansion on maximized windows. Command groups now have a fixed size policy;
all spare width goes to the trailing stretch. A regression reproduced the old
failure and now verifies unchanged command/separator bounds at widths 960,
1440, 1920 and 2560 in both themes and at 150% scaling. The Release build and
architecture, document UI and both toolbar suites pass (4/4).
