# 0045. Interactive Fillet and Chamfer previews

The interactive preview displays the solved solid and colors its named
treatment faces. The face is yellow when it replaces material inside the
stock and amber when it adds material outside. A face that cannot be
classified simply has no color overlay.

The previous edge preview subtracted each full solid from the other and
meshed the differences before confirmation became available. Those extra
operations delayed or prevented the display of a successfully solved
fillet. Other features retain their volumetric previews. The committed
edge treatment, the kernel validity checks, and the file format are
unchanged. The preview reuses stock triangulation on unchanged faces
and shares the result mesh with its highlight.

FreeCAD's Part Design dress-up code uses the shared OCCT builders and
checks the adjacent faces and their continuity when collecting edges.
TheCAD now uses the same eligibility principle in the picker. Smooth
seams, degenerate edges and edges without two incident faces are excluded
from new picks. Existing saved references remain available when editing.

[FreeCAD edge eligibility](https://github.com/FreeCAD/FreeCAD/blob/main/src/Mod/PartDesign/App/FeatureDressUp.cpp)
[FreeCAD Fillet](https://github.com/FreeCAD/FreeCAD/blob/main/src/Mod/PartDesign/App/FeatureFillet.cpp)
[FreeCAD Chamfer](https://github.com/FreeCAD/FreeCAD/blob/main/src/Mod/PartDesign/App/FeatureChamfer.cpp)

The optional saved-part workflow test uses an actual viewport click
and measures the time until a valid preview can be confirmed. Supply
THECAD_TEST_FILLET_PART, and optionally THECAD_TEST_FILLET_EDGE,
THECAD_TEST_FILLET_SIZE, and THECAD_TEST_FILLET_BUDGET_MS. The test does
not write the saved part.
