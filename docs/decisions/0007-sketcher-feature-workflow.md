# 0007: Sketch interaction and docked feature workflow

Date: 2026-11-03
Status: Owner-approved milestone behavior; implementation remains provisional

The owner approved endpoint dragging/joining, visible sketch inferencing,
direct active-sketch Extrude/Cut and a docked PropertyManager, using the supplied
SolidWorks screenshots as workflow references. Preserve existing implementation
and implement incrementally. See [requirements](../requirements/sketcher-feature-workflow.md).

## Implementation within approved scope

- Retain the equality-based relation representation. Point motion uses stable
  point references and the existing propagation/validation logic. A drag owns an
  immutable starting sketch and a candidate; only a successful release commits.
  Joining creates a normal Coincident relation in the existing schema.
- Keep drawing inference and its guides temporary. H/V inference commits only
  the existing supported line relations. Alignment guides to other points do
  not invent a new persistent alignment constraint or construction entity.
- Use explicit workspace modes: Part, SelectingPlane, EditingSketch and
  FeatureDraft. A feature draft owns a document snapshot containing any active
  sketch changes, while the original sketch edit remains suspended for Cancel.
  Commit replaces the document transactionally; Cancel resumes the sketch and
  its prior dirty state. No intermediate feature is added to history.
- The existing left Qt dock hosts a stack of pages. Its normal FeatureManager
  page is replaced by the current PropertyManager page, then restored. The host
  can accept future tool pages without implementing those tools now.
- Request supported multisample antialiasing and refine only OCCT presentation
  deviation. Never store rendering polylines in the sketch model.

No new dependency, general solver, file schema, permanent topology-reference
strategy, CAD kernel, framework or application license is selected. Native
.thepart schema 3 and existing feature parameters remain compatible.

## Limits and future decisions

This equality system supports the requested coincident/H/V behavior, including
safe rejection of conflicts. A future dimension or general-constraint milestone
needs an owner-approved solver proposal; none is introduced here. Multi/nested
region operations, new sketch attachments and additional modeling tools remain
outside this approval.
