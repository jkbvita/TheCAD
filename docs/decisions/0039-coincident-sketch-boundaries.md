# 0039. Coincident sketch boundaries

The owner reported that overlapping sketch edges blocked feature creation.
Use the existing Open CASCADE Splitter arrangement to treat coincident
spans as one geometric boundary. Keep every source entity and its split
images. Name a shared edge using the first stable source key, with a source
endpoint as a fallback at a split that has no primary intersection identity.

Extrude and Extruded Cut continue to use explicitly selected bounded cells.
A shared side separates its adjacent regions and contributes one boundary to
each. Duplicate geometry does not create additional regions. Open overlaps
remain open. Invalid zero-width geometry still fails validation.

Use the existing whole-entity and fragment reference formats. Entity order
does not determine the canonical source. Changing the overlap topology or
removing a source used to identify a region can require reselection.

[Open CASCADE Boolean Operations](https://github.com/Open-Cascade-SAS/OCCT/wiki/boolean_operations)
