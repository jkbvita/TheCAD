# 0049. Match the approved SolidWorks extrusion PropertyManagers

The owner explicitly requested SolidWorks' Extrude and Extruded Cut UI as
the baseline. Rebuild these two editors around its compact section order:
From, Direction 1, Direction 2, Thin Feature, Selected Contours and Feature
Scope. Keep the existing document parameters, persistence and modeling
algorithms.

Use a title/icon and left-aligned check, cancel and preview controls.
Place reverse direction beside the end condition, followed by the direction
reference, depth and draft controls. Optional direction and thin-wall groups
expand when enabled. Show at least two decimal places while retaining all
six supported decimal places; opening a feature still selects its depth
for immediate keyboard entry.

Reference boxes activate viewport/FeatureManager selection directly.
Selected Contours displays chosen members, rather than an inventory of
every available region. Clicking a member keeps it selected; Delete and
the contextual remove/clear commands remove selections without deleting
sketch geometry. The list's source rows retain the contour-to-geometry
mapping, including unselected regions used by viewport picking.

When the command has no source sketch, activate a profile selection box and
show the FeatureManager beside the editor. Picking a sketch restores depth
entry. Reparenting the editor must explicitly show it again, because removing
it from a stacked widget hides it. Neither profile nor reference selection
may reframe the camera.

Keep TheCAD's Dark/Light themes and original, scalable icons. The modeling
scope and limitations documented in decision 0042 remain in effect; this
UI change does not implement additional SolidWorks modeling features.

References inspected: the owner's supplied Boss-Extrude screenshots and
[official Extrude PropertyManager documentation](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_extrude_propertymanager.htm?format=P),
[PropertyManager overview](https://help.solidworks.com/2025/english/SolidWorks/sldworks/r_pm_overview.htm?id=3.10),
and [contour selection](https://help.solidworks.com/2026/English/SolidWorks/sldworks/t_Selecting_Contours.htm).

The reference for this change was documentation and the supplied screenshots;
SolidWorks GUI interactions were not tested for this change. No proprietary
SolidWorks code, icons or application resources are included.
