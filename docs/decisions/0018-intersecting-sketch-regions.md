# 0018 — Intersection-aware sketch regions for Extrude and Cut

Status: owner-approved scope, 2026-11-05, through the request to fix extrusion and
cutting of intersecting sketch geometry and the supplied circle/rectangle image.
This supersedes the unsplit-crossing restriction in 0010/0011. It retains the
approved workflow: explicitly select one or more bounded regions in Extrude/Cut.
No new solver, sketch geometry type, dependency or license decision is introduced.

## Derived planar regions

The existing whole-edge graph evaluator remains in use for nonintersecting
profiles and existing complete shared line boundaries, preserving their selected
entity-ID sets. Crossing lines, endpoint T-junctions and intersections involving
circles or legacy rectangles route to the new isolated PlanarRegions module.

The module creates private OCCT edges from the current sketch and uses the
existing General Fuse/Splitter algorithm to subdivide them at intersections.
BuilderFace assembles exact bounded planar cells from both directed sides of the
fragments, including nested holes. Unbounded areas and dangling/internal edges do
not become profiles. Lines/circles retain analytic kernel geometry; they are not
polygon approximations. Point and construction entities are excluded.

The original sketch is never trimmed, split or replaced by this analysis.
Stable entity IDs, dimensional references, Midpoint/Equal groups and other sketch
relations retain their original geometry. User sketch selection continues to
address those original entities. Temporary intersections are region-analysis
topology, not new sketch points or an enabled Arc/Trim tool.

## Persistent region references

Whole source entities retain plain legacy boundary IDs when a region uses every
fragment of that entity. Partial boundaries use versioned `@region-edge-v1:`
references. These encode source entity/rectangle-side identity, endpoint roles,
intersection participants and branch, and the oriented boundary interval.
Length-prefixed components avoid delimiter ambiguity. References contain no
rounded coordinates, mesh indices, kernel traversal indices or process pointers.

Line/circle intersection branches use signed position along the source line;
circle/circle branches use the side of the line between their centers. Thus the
two intersections remain distinct. Circle fragments are joined across artificial
parameter seams before naming. References are invariant under tested rigid
translation/rotation, rescaling above kernel tolerance, entity reordering and edits
preserving the boundary graph. Branch tolerance is normalized by the actual source
direction length so small-scale sketches do not acquire different branch labels.
Duplicate complete region identities reject rather than choosing by list order.

This remains conservative prototype naming, not a permanent topology-naming
system. Changing/removing intersection participants, hole boundaries or branches
can invalidate a saved region. Regeneration then rejects transactionally and
preserves the prior valid document/body; the user reselects explicitly. It never
switches silently to another cell with the same set of source curves.

Schema 12 persists fragment references in the existing contour/contours arrays.
Versions 1–11 remain readable; new references disguised as an older schema reject.
The canonical makeExtrusions/makeCuts APIs operate on the exact selected faces.
The legacy selectContour helper, which returns a Sketch of complete source
entities, explicitly rejects fragment selections rather than inventing arc
entities or returning an empty sketch.

## Interaction, solids and limits

The PropertyManager lists Region 1, Region 2, etc. Interior clicks toggle actual
face-classifier hits. No region is chosen implicitly. Clicking a shared boundary
can match multiple cells and retains the existing explicit-selection diagnostic.
Preview, confirmation, cancellation, feature editing and undo/redo remain on the
existing document transactions. Adjacent selected prisms fuse; cuts subtract the
chosen cells from the existing part, including Through All and inclined frames.

Coincident/collinear overlapping source edges remain ambiguous and reject with a
cleanup diagnostic. Invalid or zero-width kernel regions reject. Open crossings
alone create no bounded region. Derived topology is limited to 12,000 fragments,
1,000 bounded regions and 1,000 references per region, alongside existing sketch
and 1 MiB document limits. No automatic trimming, healing or body-scope redesign
is added.

## Verification and deployment

Release compilation succeeded. All 24 CTest cases passed (21.37 seconds). Focused
model checks cover circle/rectangle cells, circle lenses/crescents, crossed line
dividers, T-junctions, nested holes, construction exclusion, all sketch planes,
inclined frames, analytic extrusion/cut volumes, fusion, blind/Through All cuts,
stable references, source preservation, regeneration and persistence failures.
Normal/high-DPI UI tests click cells, toggle multiple selections, check preview
volumes, create Extrude/Cut features, undo/redo and save/reopen. Existing geometry,
dimensions, annotations, relations, midpoint tools and feature-tree tests pass.

The isolated package is build/package/TheCAD-Intersection-Regions in the relations
worktree. The desktop shortcut targets its executable after verifying the hash
against the tested Release build. Older packages, open sessions and the original
checkout remain intact. No automatic merge is performed.

Primary technical reference: OCCT's own
[Boolean Operations documentation](https://github.com/Open-Cascade-SAS/OCCT/wiki/boolean_operations),
the installed 7.9.3 headers, and the official source. Existing OCCT license and
runtime notices are retained; no proprietary application code or graphics is used.
