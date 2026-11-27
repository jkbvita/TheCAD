# 0034 — Sketch patterns, mirror and body projections

The owner requested the named sketch tools and supporting-face references on
2026-11-07. Baseline `59ac40f` and branch `codex/sketch-patterns-and-convert`.

## Workflow

- **Convert Entities** accepts planar faces and model edges created before the
  open sketch. A face converts its outer boundary by default. The panel offers
  inner loops and construction geometry. With an attached sketch, its support
  face is the default. This batch projects straight edges and complete circles
  parallel to the sketch plane.
- While editing a face-attached sketch, a supported boundary provides
  endpoints, line midpoints, centers and curve snaps. Placing geometry at a
  snap creates a linked construction reference and the applicable sketch
  relation. Smart Dimension also accepts the boundary. Two fixed references
  give a measured dimension. Construction references are excluded from solid
  contours and can be promoted by Convert Entities.
- **Mirror Entities** copies selected lines, circles and points about a sketch
  line. **Linear Sketch Pattern** uses a single direction with angle, spacing,
  count and reverse controls. **Circular Sketch Pattern** uses an XY center,
  angular span, count and reverse. The count includes the seed. Full-circle
  spacing omits a duplicate at 360 degrees. Coincident copies are skipped.
- Each command previews in the docked PropertyManager and commits one undoable
  sketch edit. Copies remain associated to their seeds and mirror lines.
  Deleting a seed or mirror line releases the corresponding copies in place.
  Pattern settings are entered at creation.

## Implementation

Model projections resolve named OCCT edges and available named start vertices
from the earlier body. The sketch adapter treats projected coordinates as known
inputs. The existing PlaneGCS equations constrain copies by symmetry, affine
position and equal radius. Solver library code and dependencies are unchanged.
The document transaction rejects removed or ambiguous model support. Schema 20
stores projection and replication definitions.

## Remaining toolbar research

The [contour tools decision](0035-sketch-contour-tools.md) covers Trim, Extend,
Split and Offset. Move, Copy, Rotate and Scale need explicit rules for retaining
relations and dimensions. Sketch
fillets, chamfers, slots and broader conversion support depend on additional
curve types and geometry-specific interactions.

## SolidWorks workflow references

- [Using Convert Entities](https://help.solidworks.com/2022/english/SolidWorks/sldworks/t_Using_Convert_Entities.htm)
- [Mirror PropertyManager](https://help.solidworks.com/2025/english/SolidWorks/sldworks/HIDD_DVE_SKETCH_MIRRORING.htm)
- [Linear Sketch Pattern PropertyManager](https://help.solidworks.com/2021/english/SolidWorks/sldworks/HIDD_DVE_SKETCH_PATTERN_LINEAR.htm)
- [Creating Circular Sketch Patterns](https://help.solidworks.com/2025/english/SolidWorks/sldworks/t_Creating_Circular_Sketch_Patterns.htm)
- [Sketch Toolbar](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_sketch_toolbar.htm)
