#include "modeling/Measurement.h"
#include <BOPAlgo_BuilderFace.hxx>
#include <BOPTools_AlgoTools2D.hxx>
#include <BOPTools_AlgoTools3D.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepTopAdaptor_FClass2d.hxx>
#include <GProp_GProps.hxx>
#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <IntTools_Context.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <cmath>

namespace thecad::modeling
{
std::optional<double> projectedMeasureArea(const TopoDS_Shape& shape, const gp_Pln& plane)
{
    if (shape.IsNull() || (shape.ShapeType() != TopAbs_FACE && shape.ShapeType() != TopAbs_SOLID &&
                           shape.ShapeType() != TopAbs_COMPOUND))
        return {};
    try
    {
        if (shape.ShapeType() == TopAbs_FACE)
        {
            BRepAdaptor_Surface s(TopoDS::Face(shape));
            if (s.GetType() == GeomAbs_Plane)
            {
                GProp_GProps props;
                BRepGProp::SurfaceProperties(shape, props, 1e-10);
                return props.Mass() *
                       std::abs(s.Plane().Axis().Direction().Dot(plane.Axis().Direction()));
            }
        }
        // Partition the exact orthographic silhouette in projector coordinates.
        // A ray through each bounded cell separates material from through-holes
        // and counts overlapping projected faces only once.
        Handle(HLRBRep_Algo) hlr = new HLRBRep_Algo;
        hlr->Add(shape);
        hlr->Projector(HLRAlgo_Projector(plane.Position().Ax2()));
        hlr->Update();
        hlr->Hide();
        HLRBRep_HLRToShape curves(hlr);
        TopTools_ListOfShape input;
        for (const auto& group : {curves.VCompound(), curves.OutLineVCompound(),
                                  curves.Rg1LineVCompound(), curves.RgNLineVCompound()})
            for (TopExp_Explorer e(group, TopAbs_EDGE); e.More(); e.Next())
                input.Append(e.Current());
        if (input.IsEmpty())
            return {};
        for (const auto& edge : input)
            if (!BRepLib::BuildCurve3d(TopoDS::Edge(edge)))
                return {};
        TopoDS_Shape fragments = input.First();
        if (input.Size() > 1)
        {
            BRepAlgoAPI_Splitter split;
            split.SetArguments(input);
            split.SetNonDestructive(true);
            split.Build();
            if (!split.IsDone() || split.HasErrors())
                return {};
            fragments = split.Shape();
        }
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(fragments, TopAbs_EDGE, edges);
        TopTools_ListOfShape directed;
        const TopoDS_Face support = BRepBuilderAPI_MakeFace(gp_Pln(gp::XOY()));
        for (int i = 1; i <= edges.Extent(); ++i)
        {
            auto e = TopoDS::Edge(edges(i));
            BOPTools_AlgoTools2D::BuildPCurveForEdgeOnFace(e, support);
            directed.Append(e.Oriented(TopAbs_FORWARD));
            directed.Append(e.Oriented(TopAbs_REVERSED));
        }
        BOPAlgo_BuilderFace cells;
        cells.SetFace(support);
        cells.SetShapes(directed);
        cells.SetAvoidInternalShapes(true);
        cells.Perform();
        if (cells.HasErrors())
            return {};
        Handle(IntTools_Context) context = new IntTools_Context;
        IntCurvesFace_ShapeIntersector ray;
        ray.Load(shape, Precision::Confusion());
        double sum = 0;
        for (const auto& cell : cells.Areas())
        {
            auto face = TopoDS::Face(cell);
            BRepTopAdaptor_FClass2d inside(face, Precision::Confusion());
            if (inside.PerformInfinitePoint() == TopAbs_IN)
                continue;
            gp_Pnt xy;
            gp_Pnt2d uv;
            if (BOPTools_AlgoTools3D::PointInFace(face, xy, uv, context) != 0)
                return {};
            gp_Pnt origin = plane.Location().Translated(gp_Vec(plane.XAxis().Direction()) * xy.X() +
                                                        gp_Vec(plane.YAxis().Direction()) * xy.Y());
            ray.Perform(gp_Lin(origin, plane.Axis().Direction()), -Precision::Infinite(),
                        Precision::Infinite());
            if (!ray.IsDone())
                return {};
            if (ray.NbPnt())
            {
                GProp_GProps props;
                BRepGProp::SurfaceProperties(face, props, 1e-10);
                sum += props.Mass();
            }
        }
        return sum > 0 && std::isfinite(sum) ? std::optional<double>(sum) : std::nullopt;
    }
    catch (const Standard_Failure&)
    {
        return {};
    }
}
} // namespace thecad::modeling
