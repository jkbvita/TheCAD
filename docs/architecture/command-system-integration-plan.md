# Command architecture integration points

Local baseline: 89b650d. Worktree: codex/command-architecture.

Other worktrees are active at build/assembly-next-worktree, build/extrude-ui-worktree,
build/fillet-reference-worktree and build/relations-worktree. This worktree will
not alter their files, reset their history, or modify their branches.

Application command metadata and dispatch belong in src/application. UI adapters
in MainWindow and AssemblyWindow will translate QAction triggers into typed
requests while preserving QAction object names and shortcuts. Existing
DocumentSession, AssemblySession and FeatureOperation remain the authoritative
transaction/history and geometry services. CadViewport remains responsible for
mouse gestures and rendering. Stable SelectionRef values are copied into
context snapshots; context revisions guard later execution. The initial slice
will use existing UI execution functions through dispatch adapters, while
headless execution uses application handlers and the same document services.

No persistent topology naming, solver change or file-format change is planned.
