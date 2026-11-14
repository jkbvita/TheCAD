# 0051. Planar fit-point Spline

The owner requested Spline in the sketch toolbar, following the approved
planar-tool priorities in decision 0040. Implement the regular interpolating
Spline workflow: click two or more fit points, preview while moving the cursor,
double-click or Enter to finish, and click the first point to close a loop.
Esc discards an unfinished stroke and leaves the tool. The tool remains active
after a completed stroke, like the other sketch drawing tools.

Store one native Spline entity with its stable geometry ID. First/Second remain
the endpoint roles; consecutive interior fit-point slots use explicit point
roles starting at 100. Slots are stable while editing; insertion/removal of
individual fit points is outside this first version. Extend .thepart schema to
27 only when splines are present. Existing documents retain their prior schema.

Use OCCT's interpolating B-spline geometry, including periodic closed curves,
for display and modeling. Approximation is only used for screen-space picking,
with deflection derived from the view scale. Do not store polylines as splines.
Reuse PlaneGCS's existing point equations for fit-point coordinates, snapping,
coincidence, point dimensions and dragging. A spline is fully defined only when
all its fit points are fixed. The solver engine and existing constraints retain
their behavior; no new spline tangent or curvature equations are introduced.

Fit points can be dragged and edited numerically through Edit Geometry's
scrollable coordinate table. Invalid edits are rejected before acceptance.
Include interior coordinates in history fingerprints and drag-change detection.
Use the existing sketch contour architecture for closed spline/line boundaries,
Extrude and Cut, stable feature references and regeneration. Construction
splines remain reference geometry and do not bound profiles.

Advanced Style Spline, tangent/curvature handles, Insert Spline Point, spline
length driving dimensions, spline trim/extend/offset, and spline sweep paths
are outside this first fit-point implementation. Sketch replication tools retain
their existing supported entity types and reject spline copies explicitly.
No new dependency or license is introduced.

References: [SolidWorks multiple-point splines](https://help.solidworks.com/2024/English/SolidWorks/sldworks/t_Creating_Multiple_Point_Splines.htm)
and [its spline representation overview](https://help.solidworks.com/2026/english/api/sldworksapiprogguide/Overview/Splines.htm?id=1.4.61).
These are documentation references; the local SolidWorks GUI was not inspected
for this change. No proprietary code or graphics are included.
