# 0047. Preserve the seam at a draft junction

When two selected straight convex edges meet across a draft shoulder,
their constant-radius fillet surfaces can intersect directly. The native
corner builder can instead insert a transition face that rounds over the
shoulder. For the two-edge junction with four distinct planar supports,
first try the existing exact-section cutters together. Accept their
intersection only if both radius faces share a connected sharp seam, the
cut exposes no cutter walls or caps, and the result is one valid solid.
Other selections and rejected intersections use the existing builders.

New and edited fillets save this policy in schema 26. Older feature
geometry is replayed as saved until the fillet is edited. During that
edit, a converted sketch line can preserve its start orientation if its
uniquely named source edge remains straight and parallel but its vertex
name disappears with the patch. The new endpoint name is persisted.
An unresolved source edge or ambiguous vertex still prevents the edit.

The geometry test checks both radii and their shared seam. Set
`THECAD_TEST_DRAFT_JUNCTION_PART` to run the saved-part model and viewport
regressions, including the downstream features and a save-reopen check.
