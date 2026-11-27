# First assembly milestone — proposed scope

Owner approved the recommended components-and-basic-mates milestone on
2026-11-15, including a separate assembly document and rigid-body mate solver.

The owner requested assembly work after improving the Features CommandManager.
Assembly is a new document/interaction domain. AGENTS.md requires approval for
major architecture and functional decisions; this proposal makes that approval
specific. The toolbar work can proceed independently.

## Recommended milestone

- Create/open/save `.theasmb` documents containing linked `.thepart` components,
  with stable instance IDs and relative source paths. Repeated instances share
  immutable source geometry; instances retain independent rigid placements.
- Insert existing parts, show a component tree, select components/faces, toggle
  visibility, delete components, and Fix/Float them. Fix the first component by
  default; support precise translation/rotation and mouse placement.
- Add a docked Mate editor with preview/confirm/cancel. Start with planar
  coincident, concentric-axis, distance and angle mates using eligible planar
  faces, cylindrical faces and datum references. Reject invalid combinations.
- Preserve persistent local reference names plus the component instance ID.
  Resolve source changes safely; missing/ambiguous references produce an
  actionable error, never automatic attachment to another face.
- Introduce a separate assembly document, storage/session service, component
  renderer and rigid-body mate solver. Reuse existing viewer/navigation and
  presentation components. Preserve the part document and PlaneGCS sketch solver.
- Make insertion, placement, mates, visibility and deletion undoable/redoable.
  Use transactional changes and atomic saves. Report constrained/free state
  and reject conflicting mate changes without corrupting the assembly.

## Out of scope initially

Subassemblies, in-context part editing, flexible/deformable components,
configurations, collision/contact dynamics, mechanical/advanced mates,
component patterns, assembly cuts, virtual parts and STEP assembly import.
Only functional commands should be enabled.

## Acceptance checks

1. Insert two saved parts, fix/float and position them, then undo/redo.
2. Constrain a shaft to a bore with a concentric mate and a shoulder with a
   planar mate/distance. Verify remaining movement and conflicting-mate rollback.
3. Save/reopen and verify source links, placements, mates and visibility.
4. Move the assembly together with its source files; relative links still work.
5. Change a source part and rebuild: valid named references update, invalid
   references are reported safely.
6. Select components and faces without moving the camera. Existing part/sketch
   suites and architecture boundaries continue to pass.

An alternative smaller milestone would deliver linked components, placement,
Fix/Float, tree, persistence and history first, with the mate solver/editor in
the following task. It provides a testable foundation sooner but does not yet
provide a mechanically constrained assembly.

References: [Insert Components](https://help.solidworks.com/2026/english/SolidWorks/sldworks/HIDD_DVE_INSERT_COMPONENT.htm),
[Fix/Float](https://help.solidworks.com/2020/English/SolidWorks/sldworks/t_Fixing_the_Position_of_a_Component.htm),
and [Adding Mates](https://help.solidworks.com/2026/english/SolidWorks/sldworks/t_Adding_Mates_SWassy.htm).
