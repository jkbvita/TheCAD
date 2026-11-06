#pragma once
#include <AIS_Shape.hxx>
namespace thecad::presentation
{
Quantity_Color modelGray();
Quantity_Color selectionBlue();
Quantity_Color hoverOrange();
TopoDS_Shape selectionOutline(const TopoDS_Shape&);
void styleReferencePlane(const Handle(AIS_Shape) &, bool selected, bool hovered, int variant = 0);
void styleSelectedShape(const Handle(AIS_Shape) &, double pixelRatio);
} // namespace thecad::presentation
