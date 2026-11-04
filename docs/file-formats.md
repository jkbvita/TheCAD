# Native TheCAD documents

Permanent owner-approved extensions are .thepart (Part), .theasmb (Assembly), and
.thedrw (Drawing). Only Part is implemented. Older .tcpart documents are read-only
inputs: conversion saves to a separate .thepart path, retaining the original file.
No assembly/drawing reader or placeholder is implemented. Internal serialization
is provisional and versioned; filename conventions are permanent.

## Version 8 part structure

The UTF-8 JSON root contains exactly format: "TheCAD.Part", version: 8, units:
"mm", features (ordered array) and resultFeature (last solid feature ID, or empty
for empty/sketch-only documents). Writers always emit version 8. Readers accept
versions 1–8 and reject unknown versions, fields, units and unsupported features.

Every feature has stable id, printable name, type and explicit dependencies.
Types and additional fields:

| Type | Fields |
| --- | --- |
| box | parameters: width/depth/height; legacy editable box |
| sketch | plane: XY/XZ/YZ, space: planar2d, entities, relations, dimensions, polygons; optional attachment |
| extrude | sketch ID, contour OR contours, length, direction: positive/negative |
| cut | Extrude fields plus Boolean throughAll; specified length retained |

An initial sketch can be empty. A solid operation references an earlier sketch and
the previous solid result, in that order. Face-attached sketches depend on their
supporting feature. resultFeature identifies the final solid-producing feature.
The reader validates backward references and exact dependency order.

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
reference one line's endpoints. Parallel/perpendicular reference the first point
of two distinct lines. Tangent supports a line and circle, or external circles;
the previous geometry supplies the side for a line/circle solve.

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

One selected region uses contour: [boundary entity IDs]. Several use contours:
[[region boundary IDs], ...], never both fields. Regions are actual planar faces.
Each loop excludes its immediate nested children as holes; those child interiors
can also be selected independently. A region's reference set includes outer and
immediate hole boundaries. Selecting all adjacent nested regions fills them.
Unrelated open branches remain sketch data and do not become feature boundaries.

Selections are explicit and exact. Missing/changing boundaries, including a new
hole that changes an existing selected region, fail instead of silently selecting
a different profile. Independent and strictly nested closed loops work; unsplit overlaps and crossings
remain unsupported. Adjacent line-bounded cells sharing
complete existing edges/vertices are supported; shared internal edges are omitted
when their union forms an enclosing hole boundary. Invalid/open selected
geometry never produces a stored solid.

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
features. Saving intentionally writes version 8 .thepart to the selected path.
Version 5 adds dimensions and multiple region selections. Version 7 adds relative
annotation placement; version 8 adds points, construction flags, on-geometry
relations and regular polygon groups. Older applications
reject newer unsupported versions safely; readers retain all supported older records.

Only a fully successful candidate history replaces the document and precise OCCT
BRep. Opening invalid files, dimensional edits and failed dependent regeneration
retain the old document. Save uses QSaveFile with direct-write fallback disabled;
failed replacement preserves the existing destination. Save reuses the active
path, Save As switches paths, and unsaved changes use Save/Discard/Cancel.

Limits: 1 MiB input/output, 500 features, 1000 entities and at most 4000 relations
and 4000 dimensions per sketch; finite coordinates/values within 1e9 mm.
Session undo/redo retains ten completed edits and is not serialized in this file.
There is no suppression, history reordering, spatial sketching,
assembly/drawing implementation, straight/reflex angular sectors, trimming or automatic repair.
