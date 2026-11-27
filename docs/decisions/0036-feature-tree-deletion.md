# 0036 — FeatureManager deletion

The owner reported that sketches and the features built from them could not be
deleted. Baseline `6bc3c3f` and branch `codex/feature-tree-deletion`.

The FeatureManager uses one Delete command for sketches, solid operations and
custom reference geometry. It is available through the Delete key, Edit menu
and tree context menu. Select solid history entries in the tree and custom
datums in the tree or viewport. The part, origin and origin-plane rows are protected.

Delete retains the selected operation's source sketches. A sketch nested under
a feature is the actual shared sketch, so deleting that row uses the same
dependencies as deleting the sketch from any other occurrence. Independent
selections can be deleted together.

The existing reference-deletion confirmation extends to all selected history
entries. It lists the transitive dependents before removing them. Cancel is the
default. The dependency graph includes a solid operation's input body, so later
operations can appear in the list. The existing regeneration transaction
validates the surviving history before commit. Undo and redo operate on the
complete deletion, and the camera remains in place.

[SolidWorks Confirm Delete](https://help.solidworks.com/2026/english/SolidWorks/Sldworks/HIDD_CONFIRM_DELETE_NEW.htm)
