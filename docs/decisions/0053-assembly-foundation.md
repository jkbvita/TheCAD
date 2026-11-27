# 0053. Linked components and basic rigid-body mates

The owner approved the recommended scope in
[the milestone proposal](../plans/assembly-first-milestone.md): linked part
instances, insertion/placement, Fix/Float, selection/tree, .theasmb persistence,
ten-action history and basic coincident/concentric/distance/angle mates.

Keep assembly values/solver in modeling, the document/source cache and atomic
storage in document, history in an assembly session, and the assembly workspace
and property editors in UI. Use a dedicated assembly viewport adapter sharing
the graphics scene, camera controller and appearance policies. The part window
and PlaneGCS remain independent. Documents can open in separate top-level
windows; this milestone does not replace the part shell with an MDI framework.

Components share immutable loaded source parts and have independent rigid
transforms and stable instance IDs. Persist relative paths where possible;
never rewrite external part files. Reference component-local named topology or
datum IDs. Resolve body names in the current source result. Missing or ambiguous
references reject a candidate; do not substitute a nearby face.

Solve all active mates together using rigid translations/unit-quaternion
rotations and damped nonlinear least squares. Use the existing OCCT SVD math
for rank-deficient linear steps; no new solver dependency. Fixed components are
excluded from unknowns. Coincident and distance mates use two planes,
concentric mates use axes (including cylinders), and angle mates use two planes
or two axes. Validate combinations before solving. Preserve free movement and
report remaining assembly degrees of freedom; conflicting changes roll back.

Use .theasmb schema 1 with strict validation and atomic writes. Source reloads,
component edits and mates are transactional and undoable. Component deletion
must identify dependent mates and ask before removing those mates; external
part files are never deleted. Origin references are protected.

See the milestone proposal for deferred assembly workflows and acceptance
checks. The first implementation uses a bounded dense mate system suitable
for small assemblies; large-assembly optimization is a later milestone.
