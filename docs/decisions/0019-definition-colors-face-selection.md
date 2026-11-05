# 0019 — Independent sketch definition colors and planar face selection

The coloring rules below are superseded by [0024](0024-full-definition-and-sketch-camera.md)
following the owner's correction. The planar face selection behavior is unchanged.

Status: owner-approved behavior, 2026-11-05. The owner requested black defined
geometry, explicitly including horizontal line strokes whose endpoints can still
move, and a light-blue selected planar body face cleared by an empty-space click.
This supersedes the whole-sketch-only coloring described in 0012.

Line direction, circle radius and point position are classified independently.
A Horizontal/Vertical line stroke is black while any movable endpoint stays blue.
Parallel/Perpendicular/Collinear constraints and driving dimensions can propagate
direction definition. A length alone does not define direction. A driven circle
size gives a black outline; its center stays blue until its position is defined.
Legacy rectangles have intrinsic horizontal/vertical edges. Construction curves
keep their dashed style. Hover orange and selection light blue override definition
colors; the origin datum remains red. No Fix relation or drag restriction is added.

The existing PlaneGCS adapter performs read-only local mobility diagnosis on a
private geometry copy. Its dependent parameter list identifies coordinates and
radii that can still move. Each line gets an auxiliary free angle and one
observation equation, preserving geometry freedoms while reporting angle mobility.
The ordinary solve path, upstream PlaneGCS, persisted geometry and schema 12 are
unchanged. Color is recomputed on sketch changes, live dragging and restoration.
This is a local Jacobian diagnosis, consistent with solver DOF status, rather than
a claim of global uniqueness for every nonlinear constraint configuration.

Solid selection uses OCCT's existing subshape owners and shaded light-blue
selection style (RGB 0.55, 0.80, 1.0). Repeat-clicking a face keeps it selected;
another face replaces it. Empty viewport clicks and Escape clear it. Selecting a
reference plane or feature, entering sketch edit, starting a feature operation or
replacing the displayed body clears the old face highlight. Logical sketch support
selection and visual selection are kept together. Existing planar-face validation
and attached-sketch workflows remain in place. Selection is not a document edit.

No new dependencies, serialization changes or licensing changes are introduced.
Verification is recorded in the linked manual checklist and automated regression
tests; no claim is made that SolidWorks was inspected during this task.
