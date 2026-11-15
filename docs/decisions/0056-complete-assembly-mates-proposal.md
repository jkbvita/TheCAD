# 0056. Complete assembly mate architecture

**Status: approved by the owner on 2026-11-15.**

The owner explicitly requested research and implementation of all SolidWorks
assembly mates. The functional goal is authorized. This record proposes the
major data/solver changes needed to achieve it. It does not claim those changes
were already implemented. The owner approved the recommended architecture in
the explicit approval reply for proposal 0056. The accepted application at `10cb319`
remains the working baseline.

See the [official-documentation research and coverage table](../research/solidworks-assembly-mates.md).
That report identifies 22 Mate PropertyManager types and additional Magnetic,
Belt, Lock To Sketch and In Place workflows. Implementing only more enum labels
would not reproduce their geometry or interaction.

## Recommended architecture

Extend the existing assembly modules; retain OCCT, its SVD mathematics, the
part/sketch engines, PlaneGCS, linked immutable source parts, source caches,
independent rigid instance poses and ten-action history. Do not introduce a
second CAD application, a new physics engine or a new package dependency.

### Typed mate definitions and geometry

Replace the fixed two-reference payload with a backend-neutral, typed
`MateDefinition`. Each type owns its reference roles and parameters. Selection
groups include a component-instance ID plus existing stable local names/IDs,
ordered chains with traversal direction, and optional reference-component IDs.

Example roles: Symmetric has `plane` and two `entities`; Width has two selection
sets; Hinge has separate concentric/coincident pairs plus an optional angular
pair; Path has a point, an ordered chain and optional orientation references.
Ratios, pitch, ranges, placement modes and rotation locks are explicit typed
parameters, not overloaded distance values.

Add an assembly-only, read-only geometry resolver/cache for points, lines,
planes, circles, cylinders, cones, spheres, coordinate frames, supported
profiles, bounded curves/surfaces and ordered chains. Keep their distinct
geometry and topology. Expose named topology from PartDocument through a narrow
read-only interface when its existing datum resolver is insufficient. Do not
change datum/sketch solver semantics to accommodate assembly contacts.

Do not approximate profiles with their bounding boxes or curved surfaces with
their axes. Unsupported or ambiguous references remain explicit errors. Keep
persisted references separate from transient OCCT objects/evaluated geometry.

### Constraint solving and motion state

Partition constraint evaluation into analytic standard relationships,
multi-reference advanced relationships, bounded path/contact relationships and
mechanical couplers. Solve active relationships together; independent graph
blocks can solve separately. Retain the optimization that unmated floating
instances do not contribute zero columns to a dense solve.

Add inequalities/active bounds for Limit and Free Width/Slot motion. Motion in
the interior remains free; reaching a bound must not permanently lock the
component. Add curve/surface parameters for path and contact evaluation, with
continuation across connected segments and validated contact sides. Cache
resolved geometry and use warm starts, not repeated BRep serialization or
unrestricted global shape-distance searches per mouse event.

Gear, Rack/Pinion, Screw, Linear Coupler and Universal Joint need explicit
reference frames, relative-motion anchors and continuous phase state. A rigid
pose alone cannot distinguish one turn from several turns. Preserve that state
through dragging, undo/redo and save/reopen. These are kinematic constraints;
they do not imply collision prevention, forces or deformable-body simulation.

This is a proposed implementation strategy, **not a claim about SolidWorks'
proprietary solver internals**.

### Schema, errors and transactions

Introduce `.theasmb` schema 2 with stable string mate-kind keys, named reference
groups, typed parameters, relative anchors and mechanical/path phase state.
Read schema 1 and map its four types without changing their accepted behavior.
Opening does not rewrite the file; saving newer definitions uses schema 2.
Retain strict field/finite-value validation, atomic writes and transactional
candidate application.

Model explicit active/suppressed/unsolved mate states and per-mate diagnostics.
Keep the last valid component poses when a candidate does not solve. To match
the documented force/keep-unsolved workflow, any action that leaves another
mate broken must identify affected mates and require an explicit user choice.
Never silently delete, suppress, retarget or disable a mate. Unreadable sources
and corrupt files still fail safely. Missing-entity repair must be a reviewable
operation using stable references, not an automatic nearest-face substitution.

Derived geometry caches remain transient and keyed by immutable source version.
Particular serialized contact/phase parameters must be revalidated against
that version before source rebuild can commit.

### PropertyManager and selection

Use Standard, Advanced and Mechanical tabs with type-specific selection groups,
compatible choices, alignment, rotation locks, units/modes and live previews.
Keep viewport/tree preselection and editable default suggestions; add documented
compatible fallback choices. Extend viewport picking to vertices, edges,
profiles and chains without sacrificing screen-space tolerance or camera stability.

Support continuing after one mate, Quick Mates, multiple/common-reference
mating, editing/replacing references, suppression, naming and clear mate status
in the tree. Keep cancellation and operation history consistent. A command is
enabled only when its geometry/solver/persistence path actually works.

### Workflows outside the three mate tabs

The complete coverage goal also tracks Magnetic, Belt, Lock To Sketch and In
Place. They must be implemented with their dependency workflows, rather than
fake equivalent buttons:

- Magnetic requires published connector frames, ground-plane metadata,
  proximity selection, connector cycling and persistent locks.
- Belt/Chain requires ordered pulley references, routing sides, a generated
  path/length relationship, engagement and optional generated belt parts.
- Lock To Sketch and In Place require explicit ownership and update rules for
  assembly-generated layouts/parts and in-context references.

Add narrowly scoped connector/layout metadata and generated-component support
in the document layer, with a new part schema only when needed. Existing linked
user part files must never be implicitly rewritten. Generated part creation and
save destinations must be explicit. Preserve the part modeling implementation.
Broader in-context modeling tools are not implied by these dependency workflows.

Motion/Simulation bushing, force and damping analysis, motion-study authoring,
and general collision/contact dynamics are distinct products/features; they
are not supplied by a kinematic mate implementation. If the owner intended
those too, that would require another explicit architecture decision.

## Implementation sequence and acceptance

The sequence is a way to validate the full requested goal, not a reduced scope:

1. Typed references/definitions, v1 migration, Standard relationships and their
   entity matrix; relative Lock, origin/frame alignment, rotation locking,
   non-planar geometry and point/edge picking.
2. Advanced selection groups, Profile Center, Symmetric, all Width modes,
   Limit Distance/Angle, Path controls and Linear/Linear Coupler.
3. Mechanical Hinge, Gear, Rack/Pinion, Screw, Slot, Universal Joint and Cam,
   including multi-turn/reversed motion, continuity and source regeneration.
4. Full manager/Quick Mate/common-reference workflows, suppression/error
   handling and linked misaligned-concentric behavior.
5. Magnetic/Belt/generated relationship dependencies and their UI/persistence.

For **every** mate: test compatible/incompatible entities, remaining movement,
fixed/ungrounded components, conflicting systems, edit/delete/suppress,
cancel/undo/redo, atomic persistence and named-source regeneration. Roundtrip
v1 fixtures and v2 definitions. Exercise the documented type options, not only
one happy-path example per name.

Mechanisms need deterministic fixtures: slider and hinge stops; tab/groove;
open/closed spline paths; pin in straight/arced slots; unequal gear ratios across
multiple turns; rack travel; lead screw reversal; angled-shaft universal joint;
cam contact across tangent face joins; engaged/disengaged belt routing; connector
snapping and locking. Verify warm-state continuity and rejection of singular or
ambiguous definitions. Compare geometry/remaining DOFs, not just screenshots.

Preserve all existing core/UI suites, Dark/Light/HiDPI, unchanged camera state
on selection and reference/presentation reuse. Log cold/warm timings on the
accepted six-component fixture plus larger active graphs. Do not accept a broad
mate menu that regresses the already-fixed interactive performance.

## Alternative and decision needed

The alternative is a new external assembly/kinematics solver backend. It might
reduce some numerical implementation work, but introduces a dependency/license
review, a second integration boundary and separate behavior/performance risks;
it does not eliminate the geometry, selection, persistence or workflow work.

**Recommend extending the existing assembly solver and document as described
above.** The owner approved this major architecture/data/error-state extension
under AGENTS.md. The implementation must still satisfy the acceptance checks;
approval alone does not mark any mate implemented.
