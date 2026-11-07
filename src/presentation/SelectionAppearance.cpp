#include "presentation/SelectionAppearance.h"
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
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
Quantity_Color removedMaterialYellow()
{
    return {1, 0.91, 0.13, Quantity_TOC_sRGB};
}
TopoDS_Shape selectionOutline(const TopoDS_Shape& source)
{
    if (source.IsNull() || source.ShapeType() == TopAbs_EDGE || source.ShapeType() == TopAbs_VERTEX)
        return source;
    BRep_Builder builder;
    TopoDS_Compound edges;
    builder.MakeCompound(edges);
    TopTools_IndexedMapOfShape seams;
    for (TopExp_Explorer faces(source, TopAbs_FACE); faces.More(); faces.Next())
    {
        const auto face = TopoDS::Face(faces.Current());
        for (TopExp_Explorer boundary(face, TopAbs_EDGE); boundary.More(); boundary.Next())
            if (BRep_Tool::IsClosed(TopoDS::Edge(boundary.Current()), face))
                seams.Add(boundary.Current());
    }
    TopTools_IndexedMapOfShape uniqueEdges;
    TopExp::MapShapes(source, TopAbs_EDGE, uniqueEdges);
    for (int i = 1; i <= uniqueEdges.Extent(); ++i)
    {
        const auto edge = TopoDS::Edge(uniqueEdges(i));
        if (!seams.Contains(edge) && !BRep_Tool::Degenerated(edge))
            builder.Add(edges, edge);
    }
    return edges;
}
void styleReferencePlane(const Handle(AIS_Shape) & shape, bool selected, bool hovered, int variant, double pixelRatio)
{
    const Quantity_Color neutral(0.23 + 0.025 * (variant % 3), 0.35 + 0.02 * (variant % 3),
                                 0.47 + 0.02 * (variant % 3), Quantity_TOC_sRGB);
    shape->SetColor(neutral);
    shape->SetTransparency(0.91);
    shape->Attributes()->ShadingAspect()->Aspect()->SetShadingModel(Graphic3d_TOSM_UNLIT);
    shape->Attributes()->SetFaceBoundaryDraw(Standard_True);
    shape->Attributes()->SetFaceBoundaryUpperContinuity(GeomAbs_C0);
    shape->Attributes()->SetFaceBoundaryAspect(
        new Prs3d_LineAspect(selected  ? selectionBlue()
                             : hovered ? hoverOrange()
                                       : Quantity_Color(0.43, 0.58, 0.73, Quantity_TOC_sRGB),
                             Aspect_TOL_SOLID, (selected ? 3 : hovered ? 2 : 1) * pixelRatio));
    shape->SetZLayer(Graphic3d_ZLayerId_Default);
}
void styleSelectedShape(const Handle(AIS_Shape) & shape, double ratio)
{
    shape->SetColor(selectionBlue());
    shape->SetWidth(2.5 * ratio);
    shape->Attributes()->SetPointAspect(
        new Prs3d_PointAspect(Aspect_TOM_POINT, selectionBlue(), 9 * ratio));
    shape->Attributes()->SetFaceBoundaryAspect(
        new Prs3d_LineAspect(selectionBlue(), Aspect_TOL_SOLID, 2 * ratio));
    shape->SetTransparency(0);
    shape->SetPolygonOffsets(Aspect_POM_Fill, -1, -1);
    shape->Attributes()->ShadingAspect()->Aspect()->SetShadingModel(Graphic3d_TOSM_UNLIT);
}
} // namespace thecad::presentation
