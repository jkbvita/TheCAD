# 0027 — Plane definitions, persistent support naming and SolidWorks navigation

Owner-authorized task, 2026-11-05. Based on accepted local commit 48c585d, isolated
on `codex/planes-selection-solidworks-navigation`. The original checkout and its
untracked part remain untouched. The owner explicitly approved including
persistent topology naming when asked about the previous conservative signatures.

## Persistent supports

`modeling/TopologyNames` maps generating roles to their current OCCT images.
Box faces use primitive builder roles. Prism caps use the generating feature and
stable selected-region boundary IDs; side faces use stable sketch edge/fragment
provenance. Copied inputs and Boolean results propagate names through OCCT's
Modified/IsDeleted history, including result simplification. Edges and vertices
also receive names from their incident named topology. Returned supports use the
orientation of the actual solid subshape, not an intermediate builder image.

Names describe provenance, never nearest-coordinate matching or traversal order.
Multiple images of one name are ambiguous. An edit that removes, splits or changes
the type of a referenced support fails before document assignment. No sketch is
reattached to a different face. Explicitly selecting another support remains the
recovery path. This is a foundation for the current box/extrude/cut feature set,
not a claim of a general naming solution for future fillets, lofts or imported BReps.

Schema 16 persists support names for reference geometry and face-attached sketches.
Schemas 1–15 still load. A legacy index is upgraded only while its old full-body
signature still matches the regenerated source, so migration never guesses a
support on a changed body. Unnamed/ambiguous topology retains conservative
signature validation. Support names are bounded to 8192 characters; larger
provenance descriptions remain conservative rather than producing unreadable files.

## Plane construction

Existing offset, XYZ, three-point, angled and parallel-through-point definitions
remain. Add Mid Plane, perpendicular-to-line/axis-through-point, cylinder tangent
at a surface point and cylinder tangent parallel to a plane. Midplanes average
actual model-space reference locations and canonicalize the parallel normal,
independent of reference order and opposite normal signs. Nonparallel/coincident
midplane references reject with an explicit explanation. Cylinder tangent variants
require enough independent references; Reverse switches the parallel tangent side.

The docked PropertyManager infers these combinations before or after launch,
retains First/Second/Third Reference selections, reports definition status and
enables Confirm only for valid, unique geometry. Coincident means a zero offset;
signed offset and angle changes update the preview. Existing edit, visibility,
transactional deletion, history and dependent regeneration remain in use.

## Presentation and camera

Reference planes use explicit unlit blue-gray shading, 91% transparency neutrally
and 25% when selected (owner correction, 2026-11-06), with clear boundaries and depth testing. Origin-plane
display extents use body dimensions, capped independently of infinite plane math.
Neither a plane presentation update nor selection changes its definition/camera.

Selection's source color is sRGB (6,180,255). Native selected owners remain in
the selection model, while opaque blue overlays render selected solid faces. Models use neutral light
gray (#D0D0D0) in both themes; hover never overrides a selected surface.
Hover uses orange boundary-only geometry, excluding cylindrical implementation
seams. Exact camera-ray/face intersections avoid rejecting an interior face because
the sensitive-frustum witness point falls outside the screen-distance tolerance.
Those exact hits also supply rotation pivots. Tree selection shares the appearance.

Middle drag orbits around a picked point or retained pivot; Ctrl+middle pans;
Shift+middle zooms; wheel zoom preserves the point beneath the cursor. Unmodified
left drag never navigates the camera. The same gestures work during sketch editing
without changing the 2D frame or sending creation/drag events. Near-edge-on sketch
ray intersections reject instead of placing unstable coordinates. Normal To (Ctrl+8)
returns an active sketch or selected planar reference/face to its normal view.
Camera transitions remain explicit except deliberate sketch entry/document framing.

No solver semantics or third-party dependencies change. Kernel provenance
tracking is optional on existing modeling APIs and preserves the geometry results.
