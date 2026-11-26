# Assembly architecture and workflow

## Milestone 1 scope

This document describes the independent-part assembly workflow added on top of
the existing `AssemblyDocument`, `AssemblySession`, `.theasmb` store, mate
solver and Part editing services. It does not replace the assembly model or add
a second part model. A part's `.thepart` file remains authoritative; an
assembly stores a linked component instance and its placement.

This milestone continues the local unified-workspace baseline `5fbd077`
(`codex/unified-main-build`) in the dedicated worktree branch
`codex/assembly-part-creation`. Older and concurrently edited worktrees were
left untouched.

## Reference workflow

Fusion's Edit In Place keeps an external component in its assembly placement,
shows the other components as context, and provides explicit associative and
non-associative reference modes. This implementation follows those interaction
principles while keeping TheCAD's existing mate solver and document structure.

- [Edit In Place](https://help.autodesk.com/cloudhelp/ENU/Fusion-Assemble/files/ASM-EDIT-IN-PLACE.htm)
- [Associative assembly references](https://help.autodesk.com/cloudhelp/ENU/Fusion-Assemble/files/ASM-REFERENCE-OBJECTS.htm)
- [Components](https://help.autodesk.com/cloudhelp/ENU/Fusion-Assemble/files/ASM-COMPONENTS.htm)
- [Assembly relationships](https://help.autodesk.com/cloudhelp/ENU/Fusion-Assemble/files/ASM-JOINTS.htm)

## Ownership and persisted data

`AssemblyComponent` is the per-instance record: stable instance ID, linked
source path and digest, immutable `PartDocument` definition, transform,
visibility/fixed state and cached reference geometry. Repeated instances can
share one in-memory part definition while retaining independent transforms.
The assembly file stores the source path relative to the `.theasmb` file and
existing v2 workflow metadata. The part feature history remains solely in its
`.thepart` file.

`InPlacePart` is the associative placement record. It stores the component
instance ID, stable `AssemblyReference`, rigid offset, and linked/local mode.
For this milestone, the supported associative behavior is that the selected
stable plane or planar reference drives the linked component's placement.
`AssemblyDocument::updateGeneratedParts()` resolves the support and updates the
component transform. Missing, stale or ambiguous references fail the candidate
update; they are never replaced with another reference. No persistent topology
naming or file-format migration was introduced.

The old embedded in-context-part and generated-belt workflows remain intact.
New independent parts use external `.thepart` documents. Blank part documents
are valid linked components, so a newly created part can be assembled before
it has a solid body.

## User workflow

1. **New Part in Context** asks for a new `.thepart` path. It reserves that
   path atomically and refuses to overwrite an existing file. Canceling the
   path picker has no effect. If writing, insertion, association, commit or
   editor startup fails, the component transaction is undone and the newly
   created file is removed. One selected stable assembly plane/reference is
   used for associative placement; with no selection, the part uses assembly
   origin placement. Ambiguous multi-selection is rejected.
2. The empty part is saved to disk, inserted as a linked component, and opened
   in the ordinary `MainWindow` with an Edit In Place toolbar. The active part
   stays in its assembly transform. Other visible components are shown in the
   part viewport as context geometry.
3. Context display can show surroundings, make them transparent, hide them,
   isolate the part, or show only components referenced by its placement
   association. Context geometry is presentation-only and is not selectable or
   projected into a sketch.
4. **Finish Editing** invokes the `part.finish_edit_in_place` command and saves
   the active `.thepart`. Saving from the normal Save command also works. Open
   assembly windows reload linked paths through
   `AssemblyDocument::reloadSources()` and commit the refresh through
   `AssemblySession`, so the refresh has assembly undo/redo history. If source
   regeneration or mate solving fails, the previous assembly state remains
   committed and an error is reported. If an assembly draft is active when a
   linked part is saved, refresh is queued until that draft finishes.
5. **Open Part** opens the linked `.thepart` independently in the normal part
   editor. Saving it notifies open assemblies using the same refresh path.
   Existing independent editors are reused to avoid two unsynchronized
   documents for one path. Opening independently is blocked while that same
   file is currently being edited in place. Editing another part in place is
   blocked while a different EIP session is active in the same assembly window.

Part editors record the SHA-256 revision of the file they opened or last saved.
Saving over that same path is rejected if the file was replaced, changed or
removed since the editor loaded it. Save As can preserve the local edits as a
separate part; in Edit In Place it does not silently retarget the assembly
component.

The assembly viewport no longer fits the camera automatically after every
component update. Selection and source refresh therefore preserve the view the
user chose.

## Architecture and call flow

```mermaid
flowchart LR
  User[Assembly user] --> A[AssemblyWindow Qt adapter]
  A --> D[CommandDispatcher]
  D --> H[assembly.new_part / assembly.open_part / assembly.edit_in_place handlers]
  H --> S[AssemblySession]
  S --> M[AssemblyDocument candidate]
  M --> L[AssemblyFileStore and external .thepart]
  H --> P[MainWindow part editor]
  P --> PS[DocumentSession and PartDocument]
  PS --> F[Save .thepart]
  F --> N[notifyOpenAssemblyWindowsPartSaved]
  N --> R[reloadSources candidate + mate solve]
  R --> S
  P --> V[CadViewport context shapes]
```

`AssemblyWindow` remains the assembly UI adapter and owns the assembly
dispatcher/session. `MainWindow` and `DocumentSession` remain the ordinary
part-editing stack. The dispatcher routes new UI actions using stable command
IDs; modeling and reference validation stay in document services. Existing
assembly features and workflows not yet migrated continue to use their current
handlers.

```mermaid
sequenceDiagram
  participant U as User
  participant AW as AssemblyWindow
  participant CD as CommandDispatcher
  participant AD as AssemblyDocument
  participant PW as MainWindow
  participant PS as DocumentSession
  U->>AW: New Part in Context
  AW->>CD: assembly.new_part(path)
  CD->>AW: createPartInAssembly(path)
  AW->>AD: insert external blank .thepart
  opt one valid selected support
    AW->>AD: linkInPlacePart(instanceId, AssemblyReference, offset)
  end
  AW->>PW: open linked part and set EIP context
  PW->>U: context scene + Finish Editing
  U->>PW: edit features / save
  PW->>PS: save .thepart
  PS->>AW: source-saved notification
  AW->>AD: reloadSources() on candidate
  AD-->>AW: candidate succeeds or rejects safely
  AW->>U: refreshed assembly or retained prior state
```

## Commands and UI entry points

The command catalog includes `assembly.new_part`, `assembly.open_part`,
`assembly.edit_in_place`, `assembly.refresh_references` and
`part.finish_edit_in_place`. The assembly action and component context menu
submit Open/Edit requests through `CommandDispatcher`; there is one Open Part
entry, and it does not bypass document/session checks. The Rebuild action uses
`assembly.refresh_references`. The Edit In Place toolbar's Finish Editing
action uses `part.finish_edit_in_place`. `assembly.new_part` takes a required
`path` parameter. Open/Edit take a component ID from the request or current
selection. The handlers that create/show editor windows remain UI-bound; the
document-level association, refresh and serialization APIs themselves do not
require QWidget.

Existing assembly-only embedded-part and generated-feature commands remain
available through their existing workflow handlers; they are not claimed as
headless APIs in this milestone.

## Transaction and failure behavior

- Creating a part reserves a new file path without replacement, writes the
  blank part, then inserts and solves a candidate assembly. If insertion,
  association, commit or EIP startup fails, the part-file creation and latest
  assembly history entry are rolled back. Existing files are never overwritten.
- Linking an in-place support edits a candidate `AssemblyDocument`, validates
  the plane/frame reference and rigid offset, solves, then replaces the
  committed document.
- Refresh builds a candidate from saved source files, regenerates component
  references and resolves mates before committing through `AssemblySession`.
- Failed refresh does not replace the last valid assembly document.
- A successful linked-source refresh is an assembly history transaction. Part
  edits are recorded by the part's existing `DocumentSession` history.
- Same-path part saves compare the editor's saved file digest with the current
  file digest before writing. A revision mismatch or missing source blocks the
  save and reports the conflict, protecting another editor's saved work.
- Linked-source notifications arriving during an assembly draft are retained
  and applied automatically when that draft closes or cancels.
- Canceling the native file dialog performs no document mutation. There is no
  uncommitted geometry preview for creating an empty linked part.

```mermaid
flowchart TD
  Save[Part saved to .thepart] --> Candidate[Copy current AssemblyDocument]
  Candidate --> Reload[reloadSources on candidate]
  Reload --> Refs[Resolve stable AssemblyReference values]
  Refs --> Solve[Regenerate / solve mates]
  Solve -->|valid| Commit[AssemblySession commit: undo entry]
  Solve -->|invalid or stale| Reject[Report failure]
  Commit --> UI[Refresh tree and viewport]
  Reject --> Keep[Keep previously committed assembly]
```

## Tests

`tests/AssemblyExternalPartTests.cpp`, target
`thecad_assembly_external_part_tests`, verifies a non-GUI end-to-end document
path:

- saves and links a blank external part to stable datum geometry;
- saves/reopens `.theasmb` and checks the association is preserved;
- independently modifies and saves the `.thepart` with sketch/extrude history;
- moves the support reference and reloads the part source, confirming the
  linked instance follows the updated support;
- verifies session undo/redo around source refresh;
- verifies repeated instances share one immutable part definition and that a
  source update retains existing mate identities and component placement;
- verifies invalid/stale references and cyclic dependencies reject without
  replacing committed assembly state.

`tests/AssemblyWorkflowTests.cpp` exercises the Create New Part → EIP → Finish →
reopen assembly → Open Part → source refresh workflow through the command
dispatcher and Qt adapters. `tests/MainWindowTests.cpp` verifies that a stale
part editor cannot overwrite a newer on-disk revision. Viewport-dependent
assembly tests require a real Open CASCADE graphics context; run them on the
desktop for visual verification.

## Adding assembly commands

1. Add a stable descriptor to `src/application/CommandSystem.cpp` and its typed
   parameters to the command request schema.
2. Bind the UI action in `AssemblyWindow::bindCommandHandlers()` or an
   assembly UI adapter, with availability based on the assembly context.
3. Keep geometry/document rules in `AssemblyDocument` and related modeling
   services, not in the handler or widget.
4. Mutate a candidate document and commit through `AssemblySession`; return a
   structured failure without partially changing the committed document.
5. Add a non-GUI regression in the assembly test target for ownership,
   references, save/reopen and undo/redo as applicable.

## Deferred work and limitations

- Cross-part sketch projections and arbitrary geometry links are not
  implemented. Surrounding bodies are non-pickable viewport context only.
- Only stable plane/frame support association driving component placement is
  implemented. Solid face references still reject uncertain regeneration;
  persistent topology naming is explicitly out of scope.
- Nested subassemblies are unsupported. `AssemblyComponent` stores a
  `PartDocument`; nested assembly support needs a component-definition and
  hierarchical instance-path design.
- A missing external source can still prevent assembly load; an unresolved
  component placeholder and relink UI are future work.
- The format has no immutable part-document UUID. Existing path + digest
  behavior is retained; no `.thepart` or `.theasmb` version change was made.
- Mate solver/status behavior is retained. This milestone does not add new
  mate types or a solver redesign.
- EIP is currently one active editor context per assembly window. Context
  presentation does not update live while another source changes until the
  assembly is refreshed.
- The branch for this milestone is `codex/assembly-part-creation`; it is not
  merged automatically.
