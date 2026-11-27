# 0024 — Fully defined geometry colors and stable sketch cameras

Status: owner-requested corrections, 2026-11-05. The owner clarified that a
Horizontal relation alone must leave a line blue, and reported zoom resetting
when adding geometry to a sketch attached to a body face. This supersedes the
partial-definition coloring rules in 0019.

Black now means an entity has no remaining geometric freedom. Lines require both
endpoints fixed; circles require center and radius fixed; legacy rectangles
require both defining corners fixed. Standalone points and endpoint markers still
use their own coordinate mobility. Construction geometry uses the same rules and
retains its dashed style. A defined subset stays black beside unrelated free
geometry. Hover and selection colors continue to override base definition colors.
An explicit dimension is not required when geometric relations already fully
define the entity, for example Fix or coincidence with defined geometry.

The existing read-only PlaneGCS diagnosis checks mobility of the actual geometry
parameters. Direction-only observer variables are removed because they answered
the previous coloring question. No constraints are added, no geometry is fixed by
coloring, and the solver, drag behavior, stable IDs and schema 13 are preserved.
Definition remains a local solver diagnosis with the existing nonlinear/singular
configuration limitations.

Every accepted sketch edit regenerates and redisplays its supporting body. That
redisplay previously called Reset View, reorienting and fitting an active sketch
after each edit. Body redisplay now preserves the camera during sketch editing.
Entering/leaving a sketch and explicit Fit/standard-view commands retain their
existing behavior. Ordinary part redisplay still fits as before.

Tests cover partial and complete definition, independent points, coupled
relations, ordinary and construction curves, circles, polygons, legacy rectangles,
dimension removal and unchanged solver DOFs. Body-face GUI tests draw lines and
circles after zoom/pan on top and side faces, then drag and undo/redo while checking
camera scale, eye, center, direction and up. These run at normal and 150% scaling.
No new dependencies, serialization changes or licensing changes are introduced.

See [verification](../verification/sketch-definition-camera.md).
