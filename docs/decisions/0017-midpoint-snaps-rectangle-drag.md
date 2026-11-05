# 0017 — Persistent midpoint snapping and free rectangle corner dragging

Status: owner-approved request, 2026-11-05. The owner requested midpoint snapping
for Point/Line creation and resizing an undimensioned rectangle using any of its
three corners away from the anchored origin. The baseline is d1ce05c, including
the accepted concurrent feature-tree, hexagon and sketch-relation work.

## Midpoint snapping

The Point, Line and Centerline commands include current line midpoints in their
screen-space snap candidates. Candidates use the existing logical-pixel tolerance
and nearest-distance policy; actual endpoints/origin/centers win exact ties. An
orange midpoint marker and Midpoint tooltip appear only near an eligible target.
Leaving it or exiting the tool hides the temporary marker. No permanent midpoint
point entity is inserted into each line.

PointRole::Midpoint is a derived, transient snap reference to a stable Line ID.
Its position is recomputed from the current endpoints. It is excluded from the
independent sketch-point list and cannot be serialized as a geometric point or
dimension reference. Placing a Point or attaching a new line endpoint converts
the snap into a persisted Midpoint relation: first is the actual point/endpoint,
second is the source line's First reference. Point/line contextual selection can
also apply Midpoint. Existing point/endpoint dragging supports the same target;
own/connected line midpoints are excluded to avoid snapping an endpoint onto its
own segment. Repeated acquisition does not duplicate the relation.

PlaneGCS supplies point-on-line and equal-half-length equations. Together they
constrain the actual point to the line's mathematical midpoint through later
translation, rotation, resizing and driving edits. If an explicit Point on Line
already supplies the first equation, only the midpoint's derived duplicate is
omitted; the existing relation remains. No upstream solver code is modified.
Underdefined geometry retains its existing degrees of freedom: a free source
endpoint may move during solving, and attachments follow the current solved line.

Midpoint has an original green triangle callout and works with the existing
inspection/deletion, transaction and undo/redo systems. Schema 11 adds the
`midpoint` relation type and retains readers for versions 1–10. Stored references
remain ordinary stable point references; transient midpoint roles are rejected.
To dimension a midpoint, first place a real Point there, then dimension that point.
Arc midpoints and direct dimensions to an uncreated virtual midpoint are outside
this request. No dependencies or licenses are added.

## Rectangle dragging

The failure came from treating both cursor coordinates as hard numeric edits in
the exact Coincident/H/V path. An adjacent corner shares one coordinate group
with the origin; small pointer movement away from that axis rejected the whole
drag. The opposite corner's two free coordinates already worked.

Point dragging now projects an origin-pinned coordinate onto its existing group
while applying the other coordinate. All three free corners can resize the
rectangle, while its origin corner remains fixed. This uses the existing exact
coordinate-equality engine rather than a rectangle-specific geometry rewrite.
Numeric property edits retain strict conflict checking. Dimensioned/nonlinear
sketches keep their existing PlaneGCS drag behavior and driving dimensions.

## Verification and delivery

Release compilation succeeded. All 23 CTest cases passed (48.81 seconds), including
the focused midpoint/rectangle model test and normal/high-DPI GUI workflows.
Coverage includes all three sketch planes, all free corners, origin anchoring,
driving width, hover-only cues, Point/Line/Centerline creation, point dragging,
existing on-line relations, persistent follow behavior, relation deletion,
undo/redo, save/reopen and malformed/older-version rejection.

The desktop package is isolated in the worktree at
`build/package/TheCAD-Midpoint-Rectangle`. The desktop shortcut is updated only
after the package's executable hash matches the tested Release executable. Older
packages and open sessions remain intact; restart from the desktop shortcut to
use the new implementation. No automatic merge is performed.
