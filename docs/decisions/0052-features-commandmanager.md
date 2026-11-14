# 0052. Features CommandManager layout

The owner requested the top Features tab to follow SolidWorks before starting
assembly work. Use large Extruded/Revolved Boss/Base and Extruded Cut commands,
stacked Sweep/Revolved Cut commands, compact separators, Fillet/Chamfer and
Linear/Circular Pattern flyouts, Mirror and a Reference Geometry menu. Remove
the extra group-caption row from this page. Retain the tabs below the commands,
TheCAD's themes and original icons. Unimplemented commands are not added.

Flyouts retain the last used family command. The main button runs that command;
the arrow opens its menu. Reference Geometry opens the Plane/Axis/Point chooser.
All entries reuse the existing QActions, enablement and modeling workflows.
Keep the command composition and painting in a shared widget component. Narrow
windows scroll this page rather than forcing a larger minimum window width.

Release builds and architecture, document, modeling, solid-feature and dedicated
toolbar suites passed (5/5 CTest suites). Toolbar tests exercise real flyout
clicks, retained defaults, command enablement, narrow width, Dark/Light themes
and 150% scaling. Existing optional saved-part fillet fixtures were skipped.

Manual check: click the primary commands, choose Chamfer and Circular Pattern
from their arrows, reopen each flyout to switch back, and select each Reference
Geometry type. Check both themes and a narrow window. No geometry, solver,
persistence or new dependency changes are involved.

References: [CommandManager](https://help.solidworks.com/2026/english/SolidWorks/sldworks/c_commandmanager.htm)
and [Features Toolbar](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_Features_Toolbar.htm).
The reference was official documentation and the owner's earlier screenshot;
no local SolidWorks UI inspection was performed for this change.
