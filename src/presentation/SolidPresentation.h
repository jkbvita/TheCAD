#pragma once
#include <AIS_Shape.hxx>

namespace thecad::presentation
{
// Prepare the same mesh that the viewport will use, without creating AIS objects.
void prepareSolidTriangulation(const TopoDS_Shape& shape);

// Draw the original BRep with viewport tessellation and physical edge outlines.
Handle(AIS_Shape) makeSolidPresentation(const TopoDS_Shape& shape);
} // namespace thecad::presentation
