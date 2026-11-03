# Native TheCAD documents

The project owner superseded the previous naming convention and approved these
permanent extensions for the parametric modeling milestone:

| Extension | Document kind | Current support |
| --- | --- | --- |
| `.thepart` | Part | Version 3 parametric parts with sketch relations; versions 1/2 import |
| `.theasmb` | Assembly | Reserved; no assembly implementation |
| `.thedrw` | Technical drawing | Reserved; no drawing implementation |

Existing `.tcpart` files remain readable legacy input. Save never writes to a
`.tcpart` path: conversion must use a `.thepart` destination. The UI prompts for a
new path when saving an opened legacy document and retains the original legacy
file unchanged. The old `.tcasmb` and `.tcdrw` names have no implemented readers.

The filename extensions are permanent. The internal serialization remains
provisional; it does not settle a final parametric architecture, feature-reference
strategy, or topological naming scheme.

## Version 3 part structure

A `.thepart` is a UTF-8 JSON object with these root fields:

- `format`: exactly `TheCAD.Part`.
- `version`: exactly `3` for current writers.
- `units`: exactly `mm`.
- `features`: ordered array of feature records.
- `resultFeature`: the ID of the last solid-producing feature, or an empty string
  for an empty or sketch-only part.

For example, a rectangle extruded into a solid and cut with a circular profile:

```json
{
  "format": "TheCAD.Part",
  "version": 3,
  "units": "mm",
  "features": [
    {
      "id": "sketch-1", "name": "Sketch1", "type": "sketch",
      "plane": "XY", "dependencies": [], "relations": [],
      "entities": [
        { "id": "entity-1", "type": "rectangle", "first": [0, 0], "second": [40, 30] }
      ]
    },
    {
      "id": "extrude-1", "name": "Extrude1", "type": "extrude",
      "sketch": "sketch-1", "contour": ["entity-1"],
      "length": 20, "direction": "positive", "dependencies": ["sketch-1"]
    },
    {
      "id": "sketch-2", "name": "Sketch2", "type": "sketch",
      "plane": "XY", "dependencies": [],
      "entities": [
        { "id": "entity-1", "type": "circle", "center": [20, 15], "radius": 4 }
      ]
    },
    {
      "id": "cut-1", "name": "ExtrudedCut1", "type": "cut",
      "sketch": "sketch-2", "contour": ["entity-1"],
      "length": 5, "throughAll": true, "direction": "positive",
      "dependencies": ["sketch-2", "extrude-1"]
    }
  ],
  "resultFeature": "cut-1"
}
```

## Sketch data and references

Sketch entity coordinates are exact model-space millimeters on the sketch's
origin plane. The right-handed mappings are:

| Plane | Sketch X coordinate | Sketch Y coordinate | Positive normal |
| --- | --- | --- | --- |
| XY | World X | World Y | +Z |
| XZ | World X | World Z | -Y |
| YZ | World Y | World Z | +X |

A line stores `first` and `second` coordinate pairs. A legacy rectangle stores two
opposite corners in those fields. New rectangle drawing creates four line entities
with coincident corners and horizontal/vertical relations. A circle stores `center`
and `radius`.
Every entity has an explicit stable ID scoped to its sketch. New entities receive
IDs before new drawing references are created; valid existing IDs are preserved. A sketch may contain
multiple independent closed contours and open geometry.

Extrude/Cut explicitly reference both a source sketch ID and a nonempty `contour`
array of its entity IDs. A rectangle or circle contour has one entity ID; a
line-loop contour references all member line IDs. The selected IDs must still
identify a valid closed contour on recomputation. The reader and evaluator never
silently choose another profile if that contour becomes invalid or is removed.
These references deliberately name model entities, not screen pixels or transient
rendered objects.

## Persistent sketch relations

Every version 3 sketch contains a `relations` array, including when empty. Each
relation has a unique stable `id` scoped to the sketch, `type`, `first`, and
`second` point reference. The supported types are `coincident`, `horizontal`,
and `vertical`.

A point reference contains exactly `entity` and `point`. The immutable local
origin is `{"entity":"","point":"origin"}`; its exact coordinates are (0,0).
Line endpoints use `first` or `second` with the line's stable entity ID. Circle
centers use `center`. Legacy rectangle diagonal corners may use `first` and
`second`. Unknown entities, invalid point roles, and dangling references fail
validation rather than being redirected to another point.

For example, a line beginning at the origin and remaining horizontal records:

```json
[
  {"id":"origin-join","type":"coincident",
   "first":{"entity":"line-a","point":"first"},
   "second":{"entity":"","point":"origin"}},
  {"id":"horizontal-a","type":"horizontal",
   "first":{"entity":"line-a","point":"first"},
   "second":{"entity":"line-a","point":"second"}}
]
```

Coincident relations require exactly equal X and Y coordinates. Horizontal and
vertical relations apply to the two endpoints of one line and require equal Y
or X respectively. Coordinates are the model data; the reader verifies the
stored relations against them and rejects inconsistent files. It does not
silently repair a malformed document.

Coordinate edits propagate through the affected equality groups. The origin
stays fixed; conflicting requested coordinates or collapsed geometry reject the
edit transactionally. Deleting an entity explicitly removes its attached
relations. This limited equality mechanism introduces no solver dependency and
does not implement dimensions, angles, tangency, or a general constraint solver.

## Feature records and recomputation

Each feature has `id`, `name`, `type`, and `dependencies`. IDs contain 1–128 ASCII
letters, digits, underscores, or hyphens. IDs are unique in the part. Names contain
1–128 printable characters. Additional supported fields are:

| Type | Fields |
| --- | --- |
| `box` | `parameters` object with `width`, `depth`, `height`; retained for legacy box documents |
| `sketch` | `plane` (`XY`, `XZ`, `YZ`), `entities` and `relations` arrays |
| `extrude` | `sketch`, `contour`, positive `length`, `direction` (`positive` or `negative`) |
| `cut` | Extrude fields plus Boolean `throughAll`; `length` is specified depth, retained even when through-all is active |

This provisional evaluator walks the ordered history. Sketches do not replace
the current solid. An initial extrusion generates a precise OCCT BRep solid;
subsequent additive extrusions join the prior solid. A cut applies an OCCT Boolean
to the prior body. Solid operations must retain one valid, positive-volume solid.
Open, unsuitable, or invalid selected contours are rejected. Direction is explicit
relative to the origin-plane normal; through-all computes its required extent
from the existing body's bounds rather than using a fixed guessed depth.

Sketches have no dependencies in this origin-plane milestone. An extrusion's
dependencies are its source sketch followed by the prior solid feature when one
exists. A cut's dependencies are its source sketch followed by the prior body.
The reader validates those exact backward references and feature order. There is
no suppression, rollback UI, reordering, face attachment, or general dependency
solver. Future planar-face support and permanent topology naming need separately
approved semantics; neither is encoded implicitly in the current origin planes.

Editing a sketch or feature recomputes a candidate history from its parameters.
Only a fully successful recomputation replaces the active document and shape.
Failure preserves the previous feature data and BRep. On reopen, the complete
editable history is read and the actual solid is reconstructed. No mesh,
screenshot, or opaque geometric snapshot is used as the authoritative model.
Camera position is view state and is not saved.

## Legacy versions and safe file operations

Version 1 consists of the original root fields with `version: 1`, exactly one
`box` feature, an empty dependency list, and `resultFeature` referencing the box.
It has no feature `name` field. Valid legacy boxes are loaded with their IDs and
exact dimensions preserved. Conversion writes the equivalent editable box as a
version 3 `.thepart`; it does not reinterpret a box as an unapproved extrusion.

Version 2 has the same parametric entity/history records but no sketch
`relations` field. It remains readable without inventing constraints from
apparently touching coordinates. Saving upgrades it to version 3. Existing
single-record rectangle entities remain supported and retain their IDs.

Save uses Qt `QSaveFile` with direct-write fallback disabled. Failed atomic
replacement preserves an existing destination. Save reuses the active `.thepart`
path; Save As selects another path. New, Open, Exit, and close honor unsaved
Save/Discard/Cancel choices. Invalid/unsupported files are validated before active
document replacement.

Readers reject unsupported formats/versions/units, unknown or missing fields,
invalid IDs/references/order, unsupported features, invalid numeric geometry,
and oversized input. Prototype limits are 1 MiB per document, 500 features,
1000 entities and 4000 relations per sketch, and finite coordinates/lengths within 1e9 mm. Open
sketches are valid document data but are not automatically valid extrusion
profiles. No general constraint solver, automatic dimensioning, or advanced relations
are implied by this schema.
