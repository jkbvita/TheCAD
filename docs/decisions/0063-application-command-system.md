# 0057 — Application command system

Status: Accepted for the command architecture milestone requested on 2026-11-23.

## Context

The [command audit](../architecture/command-system-audit.md) found that the existing modeling and document layers are reusable, while user commands are entered through unrelated QAction, viewport and PropertyManager paths. Future voice, automation and plugin inputs need a stable application entry point without Qt widget dependency.

## Decision

Use an application-owned registry of stable namespaced IDs, typed request/context/result values and a dispatcher with one explicit interactive session. Bind document-specific handlers through part and assembly hosts. Keep existing `DocumentSession`, `AssemblySession`, `PartDocument`, OCCT, PlaneGCS and CAD file formats. Qt windows remain adapters and migrate their commands incrementally. Cataloged commands without a handler are marked legacy backed and return unavailable through the dispatcher.

For interactive features, build a candidate from the committed document, preview without history, and commit once through the existing session after validation. Reject stale document revisions and missing topology; never substitute a different face or edge.

## Consequences

Mouse, keyboard and direct C++ callers can share command IDs and structured results. The initial vertical slice covers Isometric, Undo/Redo, Line activation, Extrude/Cut, Reference Plane, read-only Measure, and assembly Fix/Float. Other commands retain their existing handlers until migrated. Existing PropertyManagers still own some UI draft parameters; moving those into application-owned typed drafts is the next incremental step.

No voice engine, AI adapter, scripting runtime or new CAD geometry algorithm is included.
