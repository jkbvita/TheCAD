# 0012 — Geometry-attached dimensions and sketch picking

Status: owner approved, 2026-11-04, through the “Dimension Graphics and Sketch
Selection” implementation request. This extends the approved direct Smart
Dimension workflow. No solver, kernel, dependency or license changes.

Dimension equations retain their existing stable entity/point references, values,
types, sense and driving/reference status. A separate presentation helper resolves
the current references for text, extension lines, dimension lines and arrows.
Both rendering and dimension picking use that layout, including during candidate
geometry drags. Reference text and angular arcs use current measurements.

Placement uses coordinates relative to the current references: midpoint and line
direction for aligned distances/line spacing, midpoint and sketch axes for H/V,
line intersection and first selected ray for angle, and center/radius for circles.
Circle placement therefore preserves angle and proportional distance from center
as radius changes. Dragging changes this placement only; it preserves dimension
kind, sector and numerical target. Circular dragging rotates the graphical line,
independently of the radius/diameter equation. Diameter has a boundary-to-boundary
line and two arrows; radius has a radial line and a circumference arrow.

Select mode and idle Smart Dimension support repeated annotation dragging.
When geometry has already been selected and a new dimension is previewing, clicks
continue that placement workflow. Escape during an annotation drag restores its
previous placement; Escape again exits Smart Dimension. Double-click edits the
driving value. The existing candidate/regeneration transaction handles release.

The action is checkable before its toolbar button is created. Mode changes update
Smart Dimension, Select, Line, Rectangle and Circle checks, including Escape,
repeated dimensions and leaving sketch editing. Checked actions have a persistent
pressed background and border.

Picking defaults to 8 Qt logical pixels. Tools > Sketch Picking Tolerance changes
the session setting (1–32 pixels). Points take priority, followed by dimension
annotations and then the nearest entity. Equal-distance ties retain document
order. Line/rectangle picks use closest points on finite segments; circle picks
use circumference distance in the normal orthographic sketch view.
For small circles, a nearer circumference wins over the circle's center marker;
exact center picks still select the center and drawing snaps remain unchanged.
Annotation picks use text extents and actual graphical segments, not an enclosing box around
the whole dimension. Hover uses the same priorities and orange highlighting.

Schema 7 adds optional relative `placement` coordinates alongside the current
absolute annotation snapshot. Versions 1–6 remain readable; their absolute
placement is attached before regeneration. Existing schema validation rejects
missing/malformed schema-7 placement and unsupported fields. No backend-specific
types are serialized. Old application versions cannot open schema 7.

Reference: official [SOLIDWORKS sketch dimensioning](https://help.solidworks.com/2026/english/SolidWorks/sldworks/t_Dimensioning_a_2D_Sketch.htm?id=28.19.1.0)
and [circular dimensions](https://help.solidworks.com/2024/english/SolidWorks/sldworks/t_Creating_Circular_Dimensions.htm?id=28.19.1.8).
The official [education fundamentals](https://www.solidworks.com/sites/default/files/2023-04/Fundamentals3DDesign_SIM_ENG_SV.pdf)
also describes Smart Dimension selection/placement and dimension repositioning.
No local SolidWorks interaction inspection was performed for this task. This is
the approved subset of familiar engineering conventions; visual acceptance is
the owner's manual review.
