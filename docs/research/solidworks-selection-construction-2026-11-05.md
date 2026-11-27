# SolidWorks interaction investigation — 2026-11-05

Initial research was recorded before implementation, with follow-up observations
added after the owner's warning clarification. The local baseline is commit 545b88b,
in the isolated `build/relations-worktree`; the original checkout and its unsaved
`untitled.tcpart` are untouched. This report distinguishes normal-UI observations,
official documentation, and TheCAD implementation decisions.

## Direct observations in SolidWorks

Windows computer control successfully accessed SOLIDWORKS Premium 2025 SP2.0.
The owner-prepared Part1 test model is a rectangular extrusion with a circular
through-hole. No proprietary binaries, source, icons or graphics were extracted.

- Clicking the top planar face fills that face cyan/light blue, without filling
  the hole. A nearby contextual toolbar and feature breadcrumb appear.
- Clicking the circular hole rim highlights the ring alone, leaving both adjacent
  faces gray. The status bar reports its diameter. Clicking inside the hole wall
  fills the cylindrical surface instead; the planar top remains gray.
- The hole edge's right-click toolbar exposes Select Other. Its list contains
  typed `Face@...` and `Edge@...` candidates beneath that pointer, including
  faces of the cut and extrusion. Candidate preview is orange/yellow, distinct
  from the blue committed selection. Escape dismisses it; ordinary preselection
  outlines are orange.
- F5 opens a bottom selection-filter toolbar. It exposes vertices, edges, faces,
  solid bodies, axes, planes, sketch points, sketch segments, dimensions, and
  more. Its Toggle Selection Filters control is disabled with no filters set.
- File → Save As → Save as copy and open created a disposable copy at
  `build/SolidWorks-Interaction-Research.SLDPRT`. The original Part1 is not saved
  over. All following feature experiments were in that copy.
- With the cylindrical wall selected, Reference Geometry → Axis opens a docked
  PropertyManager with the face preselected and a yellow axial preview. Confirm
  adds Axis1 to the FeatureManager; it is selectable, labeled and dashed.
- Selecting the planar face then Reference Geometry → Plane opens a docked panel
  with the first face reference, a selected 10 mm offset value, Flip offset,
  additional references, preview and Confirm/Cancel. Confirm adds Plane1 to the
  tree, displayed as a labeled translucent rectangular boundary with handles.
- Sketch on selected Plane1 enters a normal-to-plane sketch. Insert Line exposes
  For construction in Options alongside orientation and infinite-length controls.

SolidWorks displayed a critically-low-memory warning and investigation initially
paused. The owner clarified that this warning is a bug and instructed continuation.
The same disposable copy was then used for these additional direct observations:

- A two-click horizontal line is Under Defined and blue. Its Horizontal relation
  remains listed in the PropertyManager. Checking For construction makes that
  existing line dashed; clearing it restores a continuous line. The displayed
  length, angle and existing Horizontal relation do not change.
- A two-click circle exposes the same For construction checkbox. Toggling it on
  makes the circumference dashed and removes the closed-contour shaded fill;
  toggling off restores the continuous circle and its fill. Its displayed center
  coordinates and radius remain unchanged. The circle center remains visible.
- Escape exits the drawing command. The temporary sketch was exited normally.

No unrelated documents or application options were modified. The disposable copy
remains open with its unsaved experimental features; Part1 was not saved over.

Not directly exercised: driving dimensions on construction circles, mixed
construction selections, reference-point or
three-point-plane creation, temporary axes, coordinate systems, exact numerical
pick tolerance, modifier-based multi-selection, hidden-face cycling, and selection
after orbiting. Their behavior must not be represented as personally observed.

## Official documentation findings

- [Construction Geometry](https://help.solidworks.com/2026/english/SolidWorks/sldworks/c_Construction_Geometry.htm?id=cbcda926ab2a4fa086dc426dd047e376):
  selected sketch entities can be converted using the PropertyManager's For
  construction checkbox. Construction entities share centerline styling and are
  ignored by feature profiles. Points and centerlines are construction entities.
- [Line PropertyManager](https://help.solidworks.com/2026/English/SolidWorks/Sldworks/HIDD_DVE_SKETCH_LINE.htm):
  selecting a line exposes its existing and available relations, status,
  construction option and editable parameters.
- [Selection Filter](https://help.solidworks.com/2024/English/solidworks/sldworks/r_Selection_Filter.htm):
  F5 toggles the toolbar; F6 temporarily enables/disables the chosen filters.
- [Selection Methods](https://help.solidworks.com/2018/english/solidworks/acadhelp/c_Selection_Methods.htm):
  preselection feedback identifies type, Ctrl-click extends selection, and tools
  can accept selection before or after activation. TheCAD retains the owner's
  approved Shift-click workflow as well.
- [Select Other](https://help.solidworks.com/2024/English/solidworks/sldworks/HIDD_SelectOther_dlg.htm):
  lists entities beneath the pointer, previews candidates and accepts a choice;
  it can expose otherwise hidden entities. Tab cycles the list.
- [Selecting Reference Geometry](https://help.solidworks.com/2025/English/Solidworks/sldworks/c_selecting_reference_geometry.htm):
  planes/axes can be picked in the graphics area; cylindrical-face temporary axes
  and hide/show controls are distinct from independent reference features.
- [Reference Axis PropertyManager](https://help.solidworks.com/2021/english/solidworks/sldworks/hidd_dve_ref_axis.htm):
  supports cylindrical/conical faces, two points, two planes and line/edge/axis
  references. The matching controls were also seen directly above.

The four user-supplied links were consulted first; several returned only the
dynamic help shell or a fetch error. Indexed official help articles above supply
the actual reference text. Version differences are explicit in the links.

For implementation, the pinned OCCT 7.9.3 public source confirms that its point
[frustum](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_9_3/src/SelectMgr/SelectMgr_RectangularFrustum.cxx)
interprets tolerance as full width, and its
[selector](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_9_3/src/SelectMgr/SelectMgr_ViewerSelector.cxx)
can skip installing custom tolerance when it matches the adaptive maximum.
TheCAD accounts for these details without modifying OCCT.

## Current TheCAD gaps and implementation rules

Baseline inspection found separate face-only OCCT picking, geometric sketch
picking and datum/reference picking. Sketch tolerance already uses 8 logical
pixels, but part selection lacks edges, vertices, bodies, filters and Select
Other. Existing Shift relation selection and all driving dimensions must survive.

Construction is already a persisted per-entity flag excluded from contours.
The generic action currently handles only one line; the Shift-release contextual
action supports several curves. A permanent conversion button is already absent
from the ribbon. Add selection-based, explicit mixed-state controls to the
PropertyManager and context menu, preserving IDs, constraints and history.

Reference points, axes and planes already exist as parametric features with
stable IDs, docked creation/editing, direct XYZ/direction entry, conservative
body-reference signatures, previews, downstream reference planes and persistence.
Keep those foundations; add independent hide/show and consistent typed picking.
Unsupported arcs/splines and full coordinate-system features remain outside the
implemented geometry set; no fake tools should be enabled.

The selection foundation will use one typed, command-independent selection model,
with adapters for the existing sketch interactions. OCCT topology picking will
distinguish vertices/edges/faces; normal clicks prefer visible small topology,
while Select Other explicitly exposes deeper candidates. Filters affect picking,
not just color. Hover stays orange, selection light blue. Tool contexts restrict
eligible types rather than silently interpreting an edge as a support face.

Runtime topology selections are scoped to the displayed shape generation and
cleared on body replacement. Existing persistent references retain conservative
source-feature/body-signature checks. A permanent topology-naming rewrite is not
part of this milestone: a future solution should track generated/modified/deleted
subshapes across each feature, detect ambiguity and require reselection on failure.
That larger architecture needs owner approval before implementation. Never reuse
a transient face/edge index on changed geometry.
