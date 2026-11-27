# 0040. Sketch tool priorities

The owner prioritized two sketch tool groups in the discussion about the next
TheCAD milestone. The roadmap places both groups ahead of Assemblies.

- Planar tools include Arc, Spline, Ellipse, Conic, Slot, and Sketch Text.
- Spatial and model-derived tools include 3D Sketch, Offset on Surface,
  Silhouette Entities, and Intersection Curve.

## Proposed delivery order

1. **Arc and Straight Slot.** Reuse circular arc geometry for slot ends.
   Verify that arcs participate in sketch snapping, constraints and dimensions.
   Verify that slots keep their width and shape when edited and can drive
   Extruded Cut. Include stable sketch references and save/reopen coverage.
2. **Spline, Ellipse, Conic, and Sketch Text.** Add editable curve geometry
   and text outlines. Verify that closed boundaries can drive part features.
3. **Planar Intersection Curve and Silhouette Entities.** Start with a
   plane/part section and a body outline projected onto a sketch plane. Keep
   references to the source model so derived curves can update when it changes.
4. **3D Sketch and Offset on Surface.** Establish an editable spatial sketch
   with points and lines first. Then use it for curves associated with model
   faces. Decide on spatial constraint solving and the first surface offset
   method as part of that milestone.

The order is a proposal. The owner approved the priorities. Variant coverage
and the spatial document and solver design remain separate decisions. Existing
planar sketches and their PlaneGCS integration are the compatibility baseline.

[Arcs](https://help.solidworks.com/2026/English/SolidWorks/Sldworks/c_Arcs.htm)
[Slots](https://help.solidworks.com/2025/english/solidworks/sldworks/c_slots.htm)
[Intersection Curve](https://help.solidworks.com/2023/english/SolidWorks/sldworks/c_Intersection_Curves.htm)
[Silhouette Entities](https://help.solidworks.com/2020/english/SolidWorks/sldworks/c_silhouette_entities.htm)
[Offset on Surface](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_offset_on_surface_pm.htm)
