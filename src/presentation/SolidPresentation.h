#pragma once
#include <AIS_Shape.hxx>

namespace thecad::presentation
{
// Draw the original BRep with viewport tessellation and physical edge outlines.
Handle(AIS_Shape) makeSolidPresentation(const TopoDS_Shape& shape);
} // namespace thecad::presentation
