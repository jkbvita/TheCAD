# Native TheCAD documents

The project owner approved three permanent filename extensions on 2026-11-02:

| Extension | Document kind | Current support |
| --- | --- | --- |
| `.tcpart` | Part | Version 1 box-part saving and loading |
| `.tcasmb` | Assembly | Naming reserved; no implementation |
| `.tcdrw` | Technical drawing | Naming reserved; no implementation |

These naming conventions do not approve assembly or drawing functionality.
Only `.tcpart` is accepted by the current application. The internal serialization
is provisional and may evolve; the `.tcpart` extension is permanent.

## Part structure, version 1

A `.tcpart` is a UTF-8 JSON document. For example:

```json
{
    "format": "TheCAD.Part",
    "version": 1,
    "units": "mm",
    "features": [
        {
            "id": "box-1",
            "type": "box",
            "parameters": { "width": 100, "depth": 70, "height": 40 },
            "dependencies": []
        }
    ],
    "resultFeature": "box-1"
}
```

The feature parameters are authoritative exact modeling inputs, not display-mesh
vertices. Opening a document regenerates an actual Open CASCADE boundary
representation solid from those dimensions and checks its topology. Thus the
supported geometry and editable parameters survive saving without relying on a
screenshot, tessellation, or opaque binary snapshot. Camera position is not model
data and is not serialized; opening resets the view to fit the restored solid.

Version 1 deliberately supports exactly one box feature. Its ID is a stable,
nonempty identifier of at most 128 ASCII letters, digits, underscores, or hyphens;
its dependencies must be an empty array. `resultFeature` must reference that ID.
The width, depth, and height are finite positive millimeter lengths above OCCT's
linear tolerance and at most 1,000,000,000 mm per axis. This prototype safety limit
rejects extreme numeric inputs before invoking the geometry kernel. The feature ID
is preserved on round-trip.

The feature list, stable IDs, explicit dependencies, and result reference provide
places for future parametric operations and dependency references. This milestone
does not implement a general feature graph or approve future feature behavior.
Adding feature types, dependency processing, or changed semantics requires a
versioned reader/writer update and appropriate owner approval. Current readers
reject unsupported versions, unknown fields, feature types, nonempty dependencies,
incorrect references, invalid geometry, and files larger than 1 MiB rather than
silently discarding data. Version and format checks prevent assembly, drawing,
foreign, or future documents from being misinterpreted as supported parts.

## Safe replacement and file operations

Saving uses Qt `QSaveFile` with direct-write fallback disabled. The completed
file replaces its destination atomically; a failed write does not truncate the
previous document. The active UI document and path change only after a successful
load or save. Loading builds and validates a candidate document before replacing
the current one, so invalid files leave the current document intact.

New documents begin with the prototype box and count as unsaved. Save reuses the
current path, while Save As requests a new `.tcpart` path using a normal Windows
file dialog. Navigation changes only the view and does not mark a part modified.
New, Open, Exit, and closing the window offer Save, Discard, or Cancel before
replacing or closing a modified part. This prototype does not introduce parameter
editing controls or additional modeling tools.
