# 0041. Three-point Arc and Straight Slot

The owner agreed to start the sketch-tool roadmap with Arc and Straight Slot.
The initial Arc command uses start, end, and a point on the curve. Straight Slot
uses two end centers and a width pick. Both commands preview their geometry
before the third click. Arc endpoints and slot centers can snap to existing
points, the origin, line midpoints, and supported body references.

An arc is a stable sketch entity with a center, two endpoints, a radius, and a
direction. The existing PlaneGCS adapter constrains its endpoints to the
supporting circle. OCCT builds the exact trimmed circle for display and region
selection. The planar region builder handles the mixed line and arc outlines
used by slots. Construction arcs stay outside part profiles.

A straight slot uses two straight edges and two semicircular caps with stable
IDs. Coincidence, tangency, and equal-radius relations preserve the profile
during edits. Smart Dimension on a cap can drive the slot width through its
diameter. Sketches with arcs use .thepart schema 23. Older sketches retain their
existing storage versions.

This batch covers three-point arcs and straight slots. Centerpoint and tangent
arc creation, curved slots, and arc support in Trim, Extend, Split, Offset, and
sketch patterns remain separate work.

[SolidWorks 3 Point Arc](https://help.solidworks.com/2026/english/SolidWorks/Sldworks/t_Sketching_3_Point_Arcs.htm)
[SolidWorks Straight Slot](https://help.solidworks.com/2026/english/SolidWorks/sldworks/t_sketch_creating_straight_slot.htm)
