# Assembly mates implementation — development record

Status: in progress under owner-approved decision 0056, branch
`codex/complete-assembly-mates`. This is a checkpoint, not a full-parity claim.
The accepted desktop package remains unchanged while implementation continues.

## Implemented foundation

- Typed reference groups, geometry descriptors and parameters; stable string mate
  keys; active/suppressed/unsolved state and explicit conflict recovery.
- Analytic and parameterized equations for the 22 main mate types. Basic fixtures
  verify residuals and remaining rigid-body freedom for each type. These do not
  establish that every SolidWorks entity combination is supported.
- Standard/Advanced/Mechanical PropertyManager families, editable groups and
  parameters, preview, editing, names, keeping the manager open and common-reference
  batch mating. Generated relationships are intentionally absent from this manual list.
- Vertex/edge/face picking at 8 logical pixels; origin and sketch references in the
  tree; native depth testing; selection and preview keep the camera unchanged.
- Schema-2 definitions, anchors and continuous phase; schema-1 compatibility;
  strict data validation, transactional edits, ten-action history and atomic saves.
- Independent constraint graph blocks, immutable source/reference caches and
  retained presentation reuse. Part and PlaneGCS solver semantics are unchanged.
- Exact planar belt routing and optional solid-boundary construction; in-memory
  encoding/decoding reuses the existing part format and validation.

## Verification at this checkpoint

Focused CTest suites pass: `assembly.mate-solver`, `assembly.general-mates`,
`assembly.typed-document`, `assembly.document-session`, `ui.assembly-workflow`
and `ui.assembly-workflow.hidpi`. GUI coverage includes Dark mode and Light at
150% scaling, camera stability, entity picking, batch mating and continued mating.
Gear driving is checked over 80 increments (16 input radians), including ratio,
complete-turn state, save/reopen and preserved placements. Generated belt fixtures
check exact path length, annular solid volume and part serialization.

Only official SolidWorks documentation was consulted; no SolidWorks GUI
inspection is claimed. References are in `solidworks-assembly-mates.md` and
additional implementation observations will be recorded as workflows are completed.

## Outstanding before completion

- Published assets/ground planes and magnetic proximity/cycling/locking workflows.
- Persistent Belt/Chain feature, engagement, driving length, layout and generated
  part ownership/update rules; generated Lock To Sketch and In Place workflows.
- Broader contact/profile/path fixtures, source updates and validation of supported
  selection combinations. In particular, automatic cam-chain selection and general
  surface contact need further work; some nonparallel Width combinations reject
  explicitly rather than create an unrelated relationship.
- Relative-frame mechanical behavior beyond the grounded driving fixture, units and
  automatic geometry-derived ratios; remaining PropertyManager refinements.
- Full application build, full essential regression run, final performance checks,
  installation through the desktop shortcut and final feature-branch publication.
