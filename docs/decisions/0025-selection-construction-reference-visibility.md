# 0025 — Typed selection, construction controls and reference visibility

Status: owner-authorized milestone, 2026-11-05. Implementation follows the
[research report](../research/solidworks-selection-construction-2026-11-05.md),
written before significant code changes. The owner explicitly requested unified
typed selection, filters, Select Other, contextual construction conversion and
independent reference geometry. No persistent topology-naming rewrite is included.

## Selection

`modeling/Selection` defines typed references, selection sets, filter categories,
candidate ranking and additive toggling without Qt or OCCT dependencies. Stable
sketch entity/annotation IDs, point roles and datum feature IDs are retained.
Part topology uses source feature ID plus an index scoped to the current displayed
shape generation. Every body replacement invalidates those topology selections;
an old candidate cannot attach a command to a new subshape with the same index.
The viewport exposes one typed selection API with adapters for existing sketch
dimension, drag and relation gestures, preserving the PlaneGCS integration.

Part picking activates OCCT solid, face, edge and vertex selection. Candidates
come from geometric/topological sensitive entities; point/edge screen distances
use the same logical-pixel tolerance as sketches. Near-front vertices and edges
precede faces, then plane interiors and whole bodies. Depth checks suppress
occluded topology for ordinary picking, independent of the active filters.
Select Other explicitly exposes deeper alternatives marked as behind. Filters
use union semantics, with no chosen categories meaning unrestricted selection.
F5 toggles the filter toolbar; F6 temporarily bypasses the retained filter mask.
The toolbar also exposes a 1–32 logical pixel tolerance (default 8).

Hover previews are orange and do not edit the part. Committed native subshape
owners and sketch selections are light blue. Shift-click and Ctrl-click toggle
additional entities; blank clicks and Escape clear part selection. Context menus
offer Select Other; its candidate list previews, accepts a choice, cancels with
Escape and cycles with Tab. Right-drag panning does not open that context menu.
Support-selection mode accepts planar faces and planes; neutral mode can select
cylindrical faces without pretending they are sketch supports. Commands inspect
selection type/count before enabling incompatible support operations.

Reference creation consumes compatible typed preselection for cylindrical/line
axes, two-point axes, offset/three-point planes and points on body vertices.
Consecutive XYZ reference-point creation retains its existing default; deriving
a point from a datum point remains available through the method selector.
Existing conservative persistent body signatures remain authoritative. Reference
creation still accepts explicit XYZ/direction entry and docked Confirm/Cancel.
Extrude/Cut region picking and solver-specific sketch gestures retain their
existing exact geometric implementations rather than being replaced by boxes.

## Construction and references

For Construction acts on selected supported curves: lines, circles and legacy
rectangles. It changes only their construction flag, retaining identity,
coordinates, dimensions, relations and polygon membership. The left properties
and right-click context controls show normal, construction or an explicit mixed
state. Mixed selections can become uniformly construction or uniformly normal.
The existing Shift-release context toolbar remains available. Centerline stays
in the Sketch ribbon; no permanent conversion button is added there. Conversion
uses the same validated candidate/history path as other sketch edits.

Construction curves remain dimensionable, constrainable, draggable and selectable,
use dashed styling and cannot bound extrusion/cut contours. Sketch points remain
non-profile geometry. Unsupported arcs/splines remain disabled, not fake tools.

Reference planes/axes/points remain independent history features, with stable
IDs, their existing creation methods, safe regeneration and downstream references.
Tree context/View controls hide/show individual reference features. Visibility
changes are undoable and do not suppress their mathematical definitions.
Schema 14 adds a required boolean `visible` to reference-feature records; schemas
1–13 remain readable, with older reference features defaulting to visible.
Selection filters and hover are session UI state and are not persisted.

## Limits

This is not permanent topology naming. Existing conservative source/body-signature
validation can reject upstream edits rather than risk wrong attachments. A future
generated/modified/deleted subshape mapping design with explicit ambiguity handling
requires owner approval before replacing that policy. Coordinate systems, automatic
temporary axes and unsupported sketch curve types are not enabled in this milestone.
Full SolidWorks box/chain/loop selection and face-hiding controls in Select Other
are not part of the requested initial selection foundation.

No new dependencies or third-party licenses are introduced. The upstream solver
and accepted dimension, undo/deletion, face-sketch zoom and file-launch fixes are
preserved. Verification and the owner checklist are recorded
[here](../verification/selection-construction-reference.md).
