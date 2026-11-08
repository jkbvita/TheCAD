# 0046. Fillet and Chamfer corner termination

FreeCAD's Part workbench delegates the selected edges to
`BRepFilletAPI_MakeFillet` and `BRepFilletAPI_MakeChamfer`. Part Design also
checks and refines the kernel result. TheCAD keeps the native builder as
the first path for general and multi-edge corners.

For a straight convex crease between two planes, the existing fallback
can still use an exact circular section or an equal-distance bevel when
the native builder fails. The cutter now follows collinear splits with
the same supporting planes. A short extension lets the neighboring
faces trim both ends. The fallback rejects exposed cutter caps or side
walls and requires a valid solid with a named treatment face for every
selected edge. Intersecting creases with different supports remain the
native builder's responsibility.

Existing .thepart features retain their original termination rule and
topology history on load. Creating or editing a Fillet or Chamfer uses
the corrected rule, stored as `termination: trimmed` in schema 25.
Older features in the same file keep their legacy rule until edited.
No new dependencies are required.

[FreeCAD Part Fillet](https://github.com/FreeCAD/FreeCAD/blob/main/src/Mod/Part/App/FeatureFillet.cpp)
[FreeCAD Part Chamfer](https://github.com/FreeCAD/FreeCAD/blob/main/src/Mod/Part/App/FeatureChamfer.cpp)
[FreeCAD Part Design Fillet](https://github.com/FreeCAD/FreeCAD/blob/main/src/Mod/PartDesign/App/FeatureFillet.cpp)

The regression checks the requested section and the actual intersection
with inclined end faces. The optional saved-part viewport test in 0045
also runs Chamfer when `THECAD_TEST_EDGE_TOOL=chamfer`.
