# Native TheCAD documents

Permanent owner-approved extensions are .thepart (Part), .theasmb (Assembly), and
.thedrw (Drawing). Only Part is implemented. Older .tcpart documents are read-only
inputs: conversion saves to a separate .thepart path, retaining the original file.
No assembly/drawing reader or placeholder is implemented. Internal serialization
is provisional and versioned; filename conventions are permanent.

## Current part structure (versions 16–23)

The UTF-8 JSON root contains exactly format: "TheCAD.Part", version: 16 through 23, units:
"mm", features (ordered array) and resultFeature (last solid feature ID, or empty
for empty/sketch/reference-only documents). Writers use the highest version
required by the part. Sketch arcs require 23, Solid Circular Pattern requires 22, linked sketch offsets
require 21, linked sketch projections
and copies require 20, sweeps require 19, solid Linear Pattern and Mirror require
18, revolve/fillet/chamfer require 17, and earlier feature sets use 16. Readers accept versions 1–23 and
reject unknown versions, fields, units and unsupported features.

Arcs store the start, end, center, radius, and a clockwise flag in local sketch
coordinates. A straight slot stores two arc and two line entities with regular
sketch relations. It has no separate entity or file format.

Every feature has stable id, printable name, type and explicit dependencies.
Types and additional fields:

| Type | Fields |
| --- | --- |
| box | parameters: width/depth/height; legacy editable box |
| sketch | plane: XY/XZ/YZ, space: planar2d, entities, relations, dimensions, polygons; optional attachment OR referencePlane; optional projections and replications (schema 20), offsets (schema 21) |
| reference | definition: method, references, position, direction, value |
| extrude | sketch ID, contour OR contours, length, direction: positive/negative |
| cut | Extrude fields plus Boolean throughAll; specified length retained |
| revolve / revolved-cut | sketch ID, contour OR contours, angle in degrees, direction, axis GeometryReference (schema 17) |
| fillet / chamfer | positive size in mm (radius / equal distance), explicit edges array of named BodyEdge GeometryReferences (schema 17) |
| linear-pattern | direction GeometryReference, Boolean wholePart, seeds array, positive spacing in mm, integer instances including seed, Boolean reverse (schema 18) |
| circular-pattern | axis GeometryReference, Boolean wholePart, seeds array, angle in degrees, integer instances including seed, Boolean equalSpacing, Boolean reverse (schema 22) |
| mirror | planar GeometryReference, Boolean wholePart, seeds array (schema 18) |
| sweep / swept-cut | profile sketch ID in `sketch`, separate path sketch ID in `path`, one region's boundary references in `contour` (schema 19) |

An initial sketch can be empty. A profile-based solid operation references an earlier sketch and
the previous solid result, in that order. Face-attached sketches depend on their
supporting feature. resultFeature identifies the final solid-producing feature.
The reader validates backward references and exact dependency order.

Pattern and mirror features store their GeometryReference in `reference`. Their
`seeds` list contains distinct solid feature IDs in history order and is empty
in `wholePart` mode. The preceding solid, selected seeds and source geometry
feature contribute dependencies in that order, with duplicates removed. Circular
Pattern stores a named circular edge as BodyCircleAxis (source 10) to use the
line normal to the circle through its center. With equal spacing, the angle is
the total span. Otherwise, it is the increment between successive instances.

Sweeps depend on the profile sketch, path sketch, and preceding solid when one
exists, in that order. Regeneration follows the full open path and resolves the
selected region by its stored boundary references.

A projection stores the sketch `entity` ID, the earlier solid `feature` ID,
its persistent `edge` name, and `startVertex` when a named endpoint is
available. Projections update the matching entities in the sketch frame before
solving. Source features add dependencies in first-use order.

A replication links existing `source` and `target` entity IDs in the same
sketch. `mirror` identifies a line for reflection. Otherwise it is empty and
the copy is rotated by `angle` radians and translated by `x` and `y` mm.
References between copies follow the ordered replication list.

An offset group stores its signed `distance` in mm, `closed` flag and ordered
`segments`. Each segment records its `source` and `target` sketch entity IDs
and a `reversed` flag for traversal of the source. For lines the sign gives the
side relative to the oriented source. For a circle it gives the change in radius.
Trim, Extend and Split store their resulting ordinary entities and relations.

## Reference geometry (version 13)

A reference feature stores its construction definition, not evaluated geometry or
solver handles. `method` is one of the stable string keys `point-xyz`,
`point-on-reference`, `point-axis-plane`, `point-two-axes`, `point-projection`,
`axis-xyz`, `axis-on-reference`, `axis-two-points`, `axis-two-planes`,
`axis-normal-to-plane`, `plane-xyz`, `plane-offset`, `plane-three-points`,
`plane-at-angle`, or `plane-through-point`. Position/direction are three finite
numbers; value is a signed offset in mm or angle in degrees. Direction is normalized
for evaluation. Zero direction, missing/wrong-type references, degeneracies and
forward/cyclic dependencies reject transactionally.

Each reference contains exactly source, feature, entity, role, index and signature.
Source numbers are fixed: 0 origin, 1 base plane, 2 base axis, 3 earlier datum,
4 sketch point, 5 sketch line, 6 solid vertex, 7 solid straight edge, 8 solid face.
Unused strings are empty; unused index is zero and unused role is First (1).
Base indices are 0/1/2 for Front/Top/Right or X/Y/Z. Sketch references use stable
feature/entity IDs and point roles (including a derived midpoint); body references
use one-based topology indices and a SHA-256 signature of the source BRep. A changed
body requires explicit support reselection. Dependencies are unique source feature
IDs in first-use order. Datums never replace resultFeature or form solid boundaries.

A sketch can store `referencePlane`, the stable ID of an earlier plane datum.
Its frame is regenerated from that feature, never serialized as another independent
pose. This is mutually exclusive with face attachment. Sketch coordinates and
PlaneGCS constraints stay planar in the resolved local frame. Derived contour and
annotation geometry retain that frame during extrusion, cutting and editing.

## Sketch geometry and stable references

Lines store id/type/first/second; legacy rectangles use two opposite corners;
circles use id/type/center/radius; points use id/type/position. Version 8 entities
also store a Boolean construction flag. Coordinates are local model-space mm, not pixels.
New rectangle drawing creates four connected lines with H/V relations.
Entity IDs are unique within each sketch; constraint IDs are unique across its
relations and dimensions. Valid IDs contain 1–128 ASCII letters, digits, '_' or '-'.
Deleted entity identities are not reused within the same replacement edit.

Origin frames are XY=(X,Y,+Z), XZ=(X,Z,-Y), YZ=(Y,Z,+X). An attachment instead
provides its own backend-independent orthonormal frame. Point references contain
exactly entity and point: first/second for line or legacy rectangle corners,
first for standalone points, center for circles, or {"entity":"","point":"origin"} for fixed local (0,0).
Missing entities or invalid roles fail; no nearest-point retargeting is performed.

space is planar2d. Spatial sketch kinds require a future version and backend;
this reader rejects them explicitly. GCS, Eigen and Boost types or solver handles
are never serialized. Version 5 readers accept omitted space/dimensions as
planar2d/empty for compatibility with the earlier multi-contour version-5 writer.
Version 6 requires both space and dimensions; a missing array is an invalid file.
Current writers always include them. Version 8 additionally requires polygons,
an array of groups containing id, center (a Point entity ID) and ordered edges
(existing Line IDs). See [regular polygon semantics](decisions/0014-basic-sketch-tools.md).
Points and construction entities never form extrusion boundaries.

## Relations and dimensions

Relations contain id/type/first/second. Types: coincident, horizontal, vertical,
fixed, parallel, perpendicular, tangent, point-on-line, point-on-circle. The last
two require version 8 and constrain the first point to the second entity. Fixed adds position: [x,y], an exact
anchor for first; second is the origin placeholder. Horizontal/vertical relations
reference one line's endpoints; schema 9 also permits two standalone sketch points. Parallel/perpendicular reference the first point
of two distinct lines. Tangent supports a line and circle, or external circles;
the previous geometry supplies the side for a line/circle solve.

Schema 9 adds collinear, equal, concentric, equal-curve-length and fixed-entity.
Collinear references two lines without merging endpoints. Equal references two
lines (length) or circles (radius). Concentric references circle centers or a
standalone point and circle center. Equal Curve Length uses geometric length:
line length or full circle circumference. Line/line commands use First references;
circles use Center and standalone points use First.

Schema 10 adds an optional group ID to Equal and Equal Curve Length relations.
One multi-selection command shares that ID across its pairwise solver equations.
Each distinct member shows one icon for that group; deletion releases the whole
group. Independent commands retain independent group IDs. Readers migrate the
schema-9 UI's generated UUID-number batch IDs; arbitrary legacy pair IDs stay
independent. Missing group metadata means an ordinary binary relation.

Schema 11 adds `midpoint`: first references an actual sketch point or endpoint,
second references the First point of a different Line entity. Its equations bind
the first point to the average of the line's current endpoints. A derived midpoint
snap is never serialized as a point role; dimensions to midpoints use a real Point
with this relation. Stable geometry and relation IDs survive later solving.

A fixed-entity stores position, secondPosition and radius, with second the origin
placeholder. Only fields relevant to the entity drive it: both endpoints for a
line, center/radius for a circle, position for a point. These snapshots are validated
and included in undo history. The legacy fixed type remains a single-point anchor.

Dimensions contain exactly id, type, first, second, value, driving, annotation
and sense; version 7 onward adds relative placement (coordinates or null for
legacy attachment). Supported types: length, horizontal, vertical, radius, diameter, distance,
line-distance, angle. The final three require version 6.
Length references one line's first/second endpoints; H/V reference two distinct
points, including centers or the origin. Radius/diameter reference a circle
center; second is the origin placeholder. A reference dimension has driving=false
and its value is recomputed as a measurement, not added as an equation.

Example driving line length:

~~~json
{"id":"dimension-1","type":"length",
 "first":{"entity":"line-a","point":"first"},
 "second":{"entity":"line-a","point":"second"},
 "value":50,"driving":true,"annotation":[25,15],"sense":1,"placement":null}
~~~

Linear values are mm; angular values are degrees. Length/radius/diameter must exceed kernel tolerance. H/V values are
nonnegative, with sense +1 or -1 retaining the coordinate side. Annotation is an
independent local [x,y] location; moving it never changes the target equation.
Distance references two points and measures true aligned length. Line-distance
references two distinct lines and preserves their perpendicular spacing and
parallelism. Angle references two lines; first/second endpoint roles choose each
ray direction, sense chooses its signed rotation, and value is degrees strictly
between 0 and 180. Other values remain mm. No transient drag target, graphics
presentation or camera state is stored.

The reader reconstructs and solves every sketch before dependent operations.
Legacy Coincident/H/V coordinates must already agree exactly; malformed equality
records are rejected. New dimensional targets and solver relations solve on a
candidate copy. Contradictions, redundancy, failed convergence or invalid solved
geometry reject the transaction; constraints are never silently disabled.

## Selected planar regions and multiple bodies

One selected region uses contour: [boundary references]. Several use contours:
[[region boundary IDs], ...], never both fields. Regions are actual planar faces.
Each loop excludes its immediate nested children as holes; those child interiors
can also be selected independently. A region's reference set includes outer and
immediate hole boundaries. Selecting all adjacent nested regions fills them.
Unrelated open branches remain sketch data and do not become feature boundaries.

Selections are explicit and exact. Missing/changing boundaries, including a new
hole that changes an existing selected region, fail instead of silently selecting
a different profile. Independent/nested loops, shared edges, unsplit crossings,
T-junctions and intersections with circles/rectangles form selectable cells.
Schema 12 permits versioned `@region-edge-v1:` fragment references alongside whole
entity IDs. Fragment references encode source/side identity, intersection branch,
endpoint identity and boundary orientation using length-prefixed components.
They contain no rounded coordinates or kernel traversal index. The sketch itself
is not subdivided or rewritten. Artificial circle seams are excluded from naming.
Malformed or missing references and new tokens in older schemas reject.
Coincident spans use a deterministic source key and retain the same reference
format. Invalid zero-width geometry remains unsupported.

Extrude fuses selected prisms and the existing result. A result may contain several
valid positive-volume solids. Cuts affect intersected solids in the current part
and can split/remove bodies while retaining a nonempty valid solid result.
No persistent body naming or per-body operation scope is stored yet.
Through All computes extent from current bounds in the chosen plane direction.

## Conservative face attachment (introduced in version 4)

An attachment object contains source (earlier solid feature ID), face (one-based
OCCT face map index), signature (SHA-256 of the source's untriangulated BRep), and
frame (nine numbers: origin XYZ, unit outward normal XYZ, unit X direction XYZ).
Y is derived. Source, fingerprint and orthonormal frame are verified. A changed
supporting BRep fails regeneration; there is no automatic face reattachment or
permanent topology naming. See decision 0008.

## Compatibility and safety

Version 1 is exactly one legacy box with no feature name. Version 2 adds named
parametric history/entities but no relations. Version 3 adds Coincident/H/V;
version 4 adds face attachments. These are read without inventing constraints or
features. Saving intentionally writes version 13 .thepart to the selected path.
Version 5 adds dimensions and multiple region selections. Version 7 adds relative
annotation placement; version 8 adds points, construction flags, on-geometry
relations and regular polygon groups. Version 9 adds contextual relation types and
full-entity Fix snapshots. Version 10 adds equality-group IDs; version 11 adds
persistent Midpoint relations. Version 12 adds intersection-boundary references.
Version 13 adds parametric reference geometry and reference-plane sketch supports.
Older applications
reject newer unsupported versions safely; readers retain all supported older records.

Only a fully successful candidate history replaces the document and precise OCCT
BRep. Opening invalid files, dimensional edits and failed dependent regeneration
retain the old document. Save uses QSaveFile with direct-write fallback disabled;
failed replacement preserves the existing destination. Save reuses the active
path, Save As switches paths, and unsaved changes use Save/Discard/Cancel.

Limits: 1 MiB input/output, 500 features, 1000 entities and at most 4000 relations
and 4000 dimensions per sketch; finite coordinates/values within 1e9 mm. Intersection
analysis allows at most 12,000 fragments, 1,000 regions and 1,000 references per region.
Session undo/redo retains ten completed edits and is not serialized in this file.
There is no suppression, history reordering, spatial sketching,
assembly/drawing implementation, straight/reflex angular sectors, trimming or automatic repair.
