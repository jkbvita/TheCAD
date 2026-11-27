# 0042. Boss-Extrude and Cut-Extrude PropertyManagers

The owner requested the SolidWorks part extrusion workflow. The panel groups
start, two directions, thin walls, selected contours, and feature scope. A
single closed profile previews immediately. Multiple regions remain explicit
choices so it is easy to use part of a sketch. Face-attached cuts start inward
by default. The panel retains keyboard depth entry and supports viewport depth
grips for Blind and Mid Plane from sketch, offset, and vertex starts.

The modeling layer builds exact Open CASCADE tools from planar profiles. It
splits tools at limiting surfaces and bodies and rejects limits that leave
part of the profile unbounded. Draft uses the kernel's draft operation, and
thin walls use sketch offsets and planar profiles. The document stores
geometry references and validates all affected features before committing a
change. Up To Body and selected body scope identify a solid through a named
face in the immediately preceding solid state. A face that disappears or
becomes ambiguous requires reselection.

Extrusion settings use optional .thepart schema 24 fields. The original
Blind and one-direction Through All paths remain available and keep their
existing topology identities. There are no additional dependencies.

The implementation covers part workflows. Assembly scope, sheet-metal
options, equations, and direct use of model edges as profile contours remain
outside this implementation. The depth grips provide direct dragging without
the Instant3D ruler. Open thin profiles support one straight chain or one
circular arc. Curved starts require a single direction and a planar end.
Draft is supported for limits parallel to the sketch. Flip side to cut
requires planar limits. End caps require the first boss to use a
closed profile and a single Blind direction.

[SolidWorks Extrude PropertyManager](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_extrude_propertymanager.htm?format=P)
[SolidWorks extrusion end conditions](https://help.solidworks.com/2025/English/SolidWorks/sldworks/c_end_condition_extrude.htm)
[SolidWorks multibody feature scope](https://help.solidworks.com/2021/english/SolidWorks/sldworks/r_feature_scope_multibody_parts.htm)
