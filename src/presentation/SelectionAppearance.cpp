#include "presentation/SelectionAppearance.h"
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_ZLayerId.hxx>
namespace thecad::presentation
{
Quantity_Color modelGray()
{
    return {208.0 / 255, 208.0 / 255, 208.0 / 255, Quantity_TOC_sRGB};
}
Quantity_Color selectionBlue()
{
    return {6.0 / 255, 180.0 / 255, 1, Quantity_TOC_sRGB};
}
Quantity_Color hoverOrange()
{
    return {1, 0.53, 0.10, Quantity_TOC_sRGB};
}
TopoDS_Shape selectionOutline(const TopoDS_Shape& source)
{
    if (source.IsNull() || source.ShapeType() == TopAbs_EDGE || source.ShapeType() == TopAbs_VERTEX)
        return source;
    BRep_Builder builder;
    TopoDS_Compound edges;
    builder.MakeCompound(edges);
    for (TopExp_Explorer e(source, TopAbs_EDGE); e.More(); e.Next())
        if (source.ShapeType() != TopAbs_FACE ||
            !BRep_Tool::IsClosed(TopoDS::Edge(e.Current()), TopoDS::Face(source)))
            builder.Add(edges, e.Current());
    return edges;
}
void styleReferencePlane(const Handle(AIS_Shape) & shape, bool selected, bool hovered, int variant)
{
    const Quantity_Color neutral(0.23 + 0.025 * (variant % 3), 0.35 + 0.02 * (variant % 3),
                                 0.47 + 0.02 * (variant % 3), Quantity_TOC_sRGB);
    shape->SetColor(selected ? selectionBlue() : neutral);
    shape->SetTransparency(selected ? 0.25 : 0.91);
    shape->Attributes()->ShadingAspect()->Aspect()->SetShadingModel(Graphic3d_TOSM_UNLIT);
    shape->Attributes()->SetFaceBoundaryDraw(Standard_True);
    shape->Attributes()->SetFaceBoundaryUpperContinuity(GeomAbs_C0);
    shape->Attributes()->SetFaceBoundaryAspect(
        new Prs3d_LineAspect(selected  ? selectionBlue()
                             : hovered ? hoverOrange()
                                       : Quantity_Color(0.43, 0.58, 0.73, Quantity_TOC_sRGB),
                             Aspect_TOL_SOLID, selected || hovered ? 2 : 1));
    shape->SetZLayer(Graphic3d_ZLayerId_Default);
}
void styleSelectedShape(const Handle(AIS_Shape) & shape, double ratio)
{
    shape->SetColor(selectionBlue());
    shape->SetWidth(2.5 * ratio);
    shape->Attributes()->SetPointAspect(
        new Prs3d_PointAspect(Aspect_TOM_POINT, selectionBlue(), 9 * ratio));
    shape->Attributes()->SetFaceBoundaryDraw(Standard_True);
    shape->Attributes()->SetFaceBoundaryAspect(
        new Prs3d_LineAspect(selectionBlue(), Aspect_TOL_SOLID, 2 * ratio));
    shape->SetTransparency(0);
    shape->SetPolygonOffsets(Aspect_POM_Fill, -1, -1);
    shape->Attributes()->ShadingAspect()->Aspect()->SetShadingModel(Graphic3d_TOSM_UNLIT);
}
} // namespace thecad::presentation
