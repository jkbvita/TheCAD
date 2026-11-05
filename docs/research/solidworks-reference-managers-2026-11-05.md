# SolidWorks reference PropertyManagers — 2026-11-05

Normal Windows UI interaction used SOLIDWORKS Premium 2025 SP2.0 and a disposable
copy, `build/SolidWorks-Reference-Managers-Research.SLDPRT`, of the prepared plate
with a circular through-hole. The source research model was preserved. No
proprietary code, binaries, icons or graphics were extracted. The implementation
also builds on the earlier [selection investigation](solidworks-selection-construction-2026-11-05.md).

## Interactions actually inspected

- Insert → Reference Geometry contains Plane, Axis and Point as separate commands.
- Plane opens a docked PropertyManager with first, second and third reference
  fields. Initially it requests references and constraints. Selecting a planar
  face produces a translucent offset-plane preview, a Fully defined message,
  distance input selected for typing, Flip offset and normal controls. Parallel,
  perpendicular, coincident, angle and mid-plane controls appear for the support.
- Confirm creates Plane1 in the tree. Selecting it highlights its boundary and
  handles; its context toolbar exposes editing and visibility controls. Edit
  Feature reopens the saved face reference and 10 mm distance. Cancel leaves the
  definition unchanged. Selecting Plane1 before invoking Plane infers an offset
  from Plane1 and previews a second plane. Confirm creates Plane2.
- Point opens a docked selections field and Arc Center, Center of Face,
  Intersection, Projection and On point methods. Selecting the hole's circular
  edge previews a magenta point at its center and infers Arc Center. Confirm
  creates Point1. Edit Feature reopens Edge<1> and the same center preview.
  Its context menu offers Delete, and Delete removes the point and tree entry.
- Axis with the cylindrical hole wall preselected infers Cylindrical/Conical Face
  and previews a dashed axis through the cylinder. The PropertyManager lists
  One Line/Edge/Axis, Two Planes, Two Points/Vertices, Cylindrical/Conical Face,
  and Point and Face/Plane. Confirm creates an explicit Axis1 tree feature;
  it is distinct from temporary axis display. Its context toolbar exposes
  Edit Feature and hide/show. Editing reopens the saved face selection and
  preview; Cancel preserves it. Delete removes Axis1 and its display.
- Delete on Plane1 also removed dependent Plane2 in this local configuration.
  No confirmation dialog appeared. The configuration was left unchanged;
  the dialog itself was **not** observed. TheCAD must always show its own
  dependency confirmation, as explicitly requested by the owner.
- Ordinary selection visibly highlighted objects without reframing this model.
  Exact numeric camera preservation was tested in TheCAD, not measured through
  SolidWorks internals.

The intermittent SolidWorks low-memory message was continued past under the
owner's earlier explicit instruction. All experiments were confined to the
disposable copy. Three-point/angled planes, axis intersections, point projections
and downstream-sketch deletion were not directly exercised in SolidWorks here.

## Official documentation and scope

- [Plane PropertyManager](https://help.solidworks.com/2024/english/SolidWorks/Sldworks/HIDD_DVE_CREATE_PLANE.htm)
  describes reference-dependent controls, fully defined validation, parallel,
  perpendicular, coincident, mid-plane and other combinations.
- [Creating Planes](https://help.solidworks.com/2019/english/SolidWorks/sldworks/t_Creating_Planes.htm)
  describes automatic inference, adding references and removing an individual
  reference. This informed editable reference fields and explicit clearing.
- [Reference Axis PropertyManager](https://help.solidworks.com/2024/English/SolidWorks/sldworks/hidd_dve_ref_axis.htm)
  documents all five prioritized geometric axis methods, including conical faces.
- [Point PropertyManager](https://help.solidworks.com/2025/English/SolidWorks/sldworks/hidd_dve_refpoint.htm)
  documents method inference, circular centers and axis/plane or axis/axis
  intersections. Non-planar intersection support is outside this milestone.
- [Confirm Delete](https://help.solidworks.com/2026/english/SolidWorks/sldworks/HIDD_CONFIRM_DELETE_NEW.htm)
  documents dependent-item listing, Basic/Advanced modes and suppressed dialogs.
  TheCAD deliberately requires confirmation for its complete dependency closure
  and cannot leave dangling features or silently reattach supports.

## Prioritization

Implemented methods retain the existing 15 datum definitions: XYZ point/axis/plane;
point/vertex/circular-edge center; axis–plane or axis–axis point intersection;
point projection onto a plane; axis on a straight edge/sketch line/reference axis
or cylindrical/conical face; two-point, two-plane and plane-normal axes; offset,
three-point, angled and parallel-through-point planes. Selection infers these
supported methods. An explicit method is available when the intended construction
needs a restricted selection field or direct XYZ entry.

Future candidates: mid-planes, face-center points, points distributed along
curves, tangent/surface-constrained planes, planes parallel to the current view,
multiple-plane creation and intersections with non-planar surfaces. These are
documented reference material, not enabled placeholder commands.
