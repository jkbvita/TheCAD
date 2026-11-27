# 0028 — Modular application and viewport architecture

Status: Accepted. Owner response: “Approve this modular architecture.”

The owner requested a researched, behavior-preserving modular refactor and
explicitly approved the proposed session/storage/graphics/camera/interaction/UI
split on 2026-11-06. Baseline: `0a0e027`; feature branch:
`codex/modular-architecture`. The original checkout and untracked part are preserved.

## Evidence and scope

- FreeCAD's developer handbook separates application/document operations (`App`)
  from presentation (`Gui`):
  https://freecad.github.io/DevelopersHandbook/technical/TheApplicationModule.html
- Qt documents separation of stored data, views and input/delegate responsibilities:
  https://doc.qt.io/qt-6/model-view-programming.html
- OCCT's OCAF guide identifies document data, commands, undo/redo, persistence and
  view synchronization as distinct application responsibilities:
  https://occt3d.com/dev/doc/overview/html/occt_user_guides__ocaf.html

These sources inform the component boundaries. This refactor keeps the current
OCCT/PlaneGCS backend, sequential document regeneration and schema 16. Adopting
OCAF's document framework would be a separate migration, with different persistence
and history risks, and is outside this approved behavior-preserving task.

## Boundaries and ownership

- `modeling`: geometry, constraints, dimensions, topology provenance and selection
  policies. No application/session, document, rendering or widget dependency.
- `document`: feature history and regeneration. `PartFileStore` owns parsing,
  migrations, validation of serialized fields and atomic saves; shared internal
  validation avoids divergent rules between loading and interactive regeneration.
- `application`: `DocumentSession` owns active document/draft, path, revisions and
  ten-action undo/redo. `FeatureOperation` builds transactional extrude/cut previews.
  `SketchToolController` owns tool activation. No widgets or graphics resources.
- `presentation`: shared engineering selection-color/appearance policy.
- `viewport`: `GraphicsScene` owns the OCCT viewer/context/native view lifetime;
  `CameraNavigation` owns gesture state and camera math; `DimensionPresenter` owns
  dimension labels/guides; `SketchPicker` performs read-only screen-space picking
  through a supplied projection. No main-window or QWidget dependency.
- `ui` builds shared widgets, the viewport QWidget adapter and the application
  shell separately. `FeatureTreePresenter` projects document data into the existing
  tree, retaining expansion and selection behavior. Commands consume application
  services; the widget adapter translates native events into component operations.

The existing MainWindow and CadViewport public interfaces remain integration
facades. Components own their state; the facades do not retain duplicate aliases
of camera, graphics, dimension or undo state. Tests inspect the relevant component
where necessary rather than requiring the old facade-owned fields.

Each directory declares its own CMake target(s). Explicit one-way dependencies and
an architecture regression check prevent lower layers from importing the UI. UI
widget object names, shortcuts, geometry calculations, picking rules, file formats,
history depth and camera scheduling remain compatibility requirements.

## Alternatives and consequences

Keeping the current ownership would retain coupling between document history,
widgets and rendering. The selected incremental extraction gives these resources
explicit owners and independently buildable layers while preserving integration
facades. An OCAF document migration or dynamic plug-in runtime would broaden the
behavior/persistence scope and is not part of this decision.

The GUI-free build verifies that lower-layer separation is real. Architecture
checks fail forbidden imports/target dependencies. Tests that previously inspected
private facade fields now inspect the extracted owners; their behavioral
expectations remain the same. Some tool-specific event wiring and PropertyManager
composition still belong to the UI adapters and can be extracted further through
these interfaces when needed.
