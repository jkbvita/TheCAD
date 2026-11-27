# 0008: Provisional planar face attachments and bounded contours

Date: 2026-11-03
Status: Owner-approved milestone; conservative implementation, not permanent topology naming

The owner approved visible origin planes, model-space axes, planar solid face
sketching and contour recognition beside open geometry. Existing local work is
the baseline. The implementation retains C++ / Qt / OCCT and the existing history.

## Face reference strategy

An attachment stores the source solid-producing feature ID, a one-based OCCT face
map index, a SHA-256 fingerprint of the entire untriangulated source BRep, and a
right-handed local frame (origin, outward normal and X direction). The frame is
computed from the actual planar face, never the camera. Its origin is the face's
surface centroid. X is projected global X, falling back to projected global Y
near parallelism; Y follows the right-handed frame. A reversed face reverses the
support normal. Local sketch coordinates remain mathematically unbounded.

During regeneration the source must be an earlier solid-producing feature. Its
entire BRep must match the saved signature before the face index is used. The
indexed face must still be planar and reproduce the saved local frame. Missing,
changed or inconsistent support rejects the candidate edit/load transactionally.
There is no nearest-face search, index-only rebinding or silent fallback to an
origin plane. Dependency records include the supporting feature.

This deliberately rejects even some harmless upstream changes and topology
reordering. Fingerprints depend on OCCT serialization; future kernel migration
may need explicit migration. Automatic reattachment, an existing-sketch support
replacement UI, and permanent topology naming remain unimplemented. Undo the
upstream change or recreate the sketch on the intended face. Approve a separate
reference strategy before adding resilient support tracking.

## Planar contour graph

Line endpoints form nodes within the existing joining tolerance; entities form
edges with stable IDs. A low-link bridge pass removes edges that cannot bound a
region from contour analysis only. Each remaining independent degree-two cycle
is assembled into an OCCT wire/face and validated before use. Original geometry
is untouched; selected contours store only their perimeter entity IDs. Bridges
between cycles and attached open trees do not invalidate otherwise valid cycles.

Isolated circles and legacy rectangle primitives retain their existing paths.
Unsplit crossings, overlapping edges, touching/shared-boundary cycles and nested
regions reject clearly. General planar arrangement splitting and inner-wire
semantics require a later milestone; no approximate screen-space region test is
introduced. Point selection tests the actual OCCT face interior.

## Serialization

Parts with face attachments write version 4. Origin-only parts continue writing
version 3. Readers accept versions 1 through 4; native extensions are unchanged.
The optional attachment fields and validation are described in
[the native format guide](../file-formats.md).

No solver or dependency was added. Smart Dimension remains a
[proposal requiring owner approval](../requirements/smart-dimension-proposal.md).
