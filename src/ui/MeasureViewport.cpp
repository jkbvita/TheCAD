#include "ui/CadViewport.h"
#include "presentation/SelectionAppearance.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRep_Tool.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Prs3d_PointAspect.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <algorithm>

namespace thecad::ui
{
using namespace modeling;
using Kind = SelectionKind;

std::optional<MeasurePick> CadViewport::resolveMeasurePick(const SelectionRef& ref)
{
    MeasurePick pick{ref, {}, QString::fromLatin1(selectionKindName(ref.kind))};
    try
    {
        if (isTopology(ref.kind))
        {
            prepareBodyPicking();
            if (ref.generation != m_shapeGeneration)
                return {};
            const auto& map = ref.kind == Kind::Face     ? m_selectionFaces
                              : ref.kind == Kind::Edge   ? m_selectionEdges
                              : ref.kind == Kind::Vertex ? m_selectionVertices
                                                         : m_selectionBodies;
            if (ref.index < 1 || ref.index > map.Extent())
                return {};
            pick.entity.shape = map(ref.index);
            pick.label += QString(" <%1>").arg(ref.index);
        }
        else if (ref.kind == Kind::SketchEntity || ref.kind == Kind::SketchPoint)
        {
            if (!m_sketch || ref.featureId != m_sketchFeatureId)
                return {};
            if (ref.kind == Kind::SketchPoint)
            {
                const auto p =
                    sketchPoint(*m_sketch, sketchPointValue(*m_sketch, {ref.entityId, ref.role}));
                pick.entity.shape = BRepBuilderAPI_MakeVertex(p);
                pick.label += " · " + QString::fromStdString(ref.entityId);
            }
            else
            {
                auto it = std::find_if(m_sketch->entities.begin(), m_sketch->entities.end(),
                                       [&](const auto& e) { return ref.entityId == e.id; });
                if (it == m_sketch->entities.end())
                    return {};
                if (it->kind == EntityKind::Point)
                    pick.entity.shape =
                        BRepBuilderAPI_MakeVertex(sketchPoint(*m_sketch, it->first));
                else
                {
                    Sketch one;
                    one.plane = m_sketch->plane;
                    one.attachment = m_sketch->attachment;
                    one.referencePlaneId = m_sketch->referencePlaneId;
                    one.referencePlaneFrame = m_sketch->referencePlaneFrame;
                    one.entities = {*it};
                    TopExp_Explorer edges(sketchShape(one), TopAbs_EDGE);
                    if (!edges.More())
                        return {};
                    pick.entity.shape = edges.Current();
                    edges.Next();
                    if (edges.More())
                        return {};
                }
                pick.label += " · " + QString::number(it - m_sketch->entities.begin() + 1);
            }
        }
        else if (ref.kind == Kind::OriginPlane)
        {
            const auto plane = SketchPlane(ref.index);
            pick.entity.plane = gp_Pln(sketchAxes(plane));
            pick.highlight = referencePlaneFace(plane, m_planeHalfExtent);
            pick.label = referencePlaneName(plane);
        }
        else if (ref.featureId == "origin:" && ref.kind == Kind::ReferencePoint)
        {
            pick.entity.shape = BRepBuilderAPI_MakeVertex(gp::Origin());
            pick.label = "Origin";
        }
        else
        {
            auto d = std::find_if(m_datums.begin(), m_datums.end(),
                                  [&](const auto& v) { return v.id == ref.featureId; });
            if (d == m_datums.end())
                return {};
            pick.label = QString::fromStdString(d->name);
            if (d->geometry.kind == DatumKind::Point)
                pick.entity.shape = BRepBuilderAPI_MakeVertex(d->geometry.frame.Location());
            else if (d->geometry.kind == DatumKind::Axis)
                pick.entity.axis = gp_Lin(d->geometry.frame.Axis());
            else
                pick.entity.plane = gp_Pln(d->geometry.frame);
            pick.highlight = datumDisplayShape(d->geometry, 50);
        }
        if (pick.highlight.IsNull())
            pick.highlight = pick.entity.shape;
        return pick;
    }
    catch (const Standard_Failure&)
    {
        return {};
    }
    catch (const std::exception&)
    {
        return {};
    }
}
void CadViewport::setMeasurePicking(bool active)
{
    if (active == m_measurePicking)
        return;
    auto refs = selection();
    prepareBodyPicking();
    if (active && refs.empty() && m_treeBodySelected && m_selectionBodies.Extent() == 1)
        refs.push_back({Kind::Body, m_bodyFeatureId, {}, PointRole::First, 1, m_shapeGeneration});
    m_measurePicking = active;
    m_measurePlanePicking = false;
    m_measurePointMode = false;
    m_measurePicks.clear();
    m_dragMeasureLabel = -1;
    if (QWidget::mouseGrabber() == this)
        releaseMouse();
    if (active)
        for (const auto& ref : refs)
            if (m_measurePicks.size() < 6)
                if (auto pick = resolveMeasurePick(ref))
                    m_measurePicks.push_back(*pick);
    refreshMeasureHighlights();
    showMeasurement();
    update();
}
void CadViewport::refreshMeasureHighlights()
{
    if (!isReady())
        return;
    auto ctx = m_graphics.context();
    for (auto& h : m_measureHighlights)
        ctx->Remove(h, false);
    m_measureHighlights.clear();
    const Quantity_Color green(.12, .86, .28, Quantity_TOC_sRGB);
    for (const auto& pick : m_measurePicks)
    {
        auto shape = pick.highlight;
        if (shape.IsNull())
            continue;
        const bool datum = pick.entity.plane.has_value() || pick.entity.axis.has_value();
        if (datum)
            shape = presentation::selectionOutline(shape);
        Handle(AIS_Shape) h = new AIS_Shape(shape);
        h->SetColor(green);
        h->SetWidth(3 * devicePixelRatioF());
        h->Attributes()->SetPointAspect(
            new Prs3d_PointAspect(Aspect_TOM_POINT, green, 8 * devicePixelRatioF()));
        h->SetInfiniteState(true);
        h->SetZLayer(Graphic3d_ZLayerId_Top);
        ctx->Display(h,
                     !datum &&
                             (shape.ShapeType() == TopAbs_FACE || shape.ShapeType() == TopAbs_SOLID)
                         ? AIS_Shaded
                         : AIS_WireFrame,
                     -1, false);
        m_measureHighlights.push_back(h);
    }
    m_measureOffsets.clear();
    update();
}
void CadViewport::clearMeasurePicks()
{
    m_measurePicks.clear();
    refreshMeasureHighlights();
    emit measureChanged();
}
void CadViewport::setMeasurePointMode(bool active)
{
    if (active != m_measurePointMode)
    {
        m_measurePointMode = active;
        clearMeasurePicks();
    }
}
void CadViewport::removeMeasurePick(int row)
{
    if (row < 0 || row >= int(m_measurePicks.size()))
        return;
    m_measurePicks.erase(m_measurePicks.begin() + row);
    refreshMeasureHighlights();
    emit measureChanged();
}
gp_Pln CadViewport::measureScreenPlane() const
{
    if (!isReady())
        return gp_Pln(gp::XOY());
    const auto cam = m_graphics.view()->Camera();
    return gp_Pln(cam->Center(), cam->Direction());
}
void CadViewport::acceptMeasurePick(MeasurePick pick)
{
    if (m_measurePlanePicking)
    {
        auto plane = pick.entity.plane;
        if (!plane && !pick.entity.shape.IsNull() && pick.entity.shape.ShapeType() == TopAbs_FACE)
        {
            BRepAdaptor_Surface s(TopoDS::Face(pick.entity.shape));
            if (s.GetType() == GeomAbs_Plane)
                plane = s.Plane();
        }
        if (!plane)
        {
            emit measurePickMessage("Select a planar face or reference plane.");
            return;
        }
        m_measurePlanePicking = false;
        emit measurePlanePicked(*plane, pick.label);
        return;
    }
    if (!pick.location)
    {
        auto i = std::find_if(m_measurePicks.begin(), m_measurePicks.end(),
                              [&](const auto& p) { return p.ref == pick.ref; });
        if (i != m_measurePicks.end())
        {
            removeMeasurePick(int(i - m_measurePicks.begin()));
            return;
        }
    }
    if (m_measurePointMode && !pick.location &&
        (pick.entity.shape.IsNull() || pick.entity.shape.ShapeType() != TopAbs_VERTEX))
    {
        emit measurePickMessage("Pick a location on the model in the viewport.");
        return;
    }
    if (m_measurePointMode && m_measurePicks.size() == 2)
        m_measurePicks.clear();
    if (m_measurePicks.size() == 6)
    {
        emit measurePickMessage("Six entities selected. Remove one or clear the list.");
        return;
    }
    m_measurePicks.push_back(std::move(pick));
    refreshMeasureHighlights();
    emit measureChanged();
}
void CadViewport::pickMeasurement(const PickCandidate& item, const std::optional<QPointF>& cursor)
{
    auto pick = resolveMeasurePick(item.value.reference);
    if (!pick)
    {
        emit measurePickMessage("This selection has no supported measurement.");
        return;
    }
    if (!m_measurePointMode || m_measurePlanePicking)
    {
        acceptMeasurePick(*pick);
        return;
    }
    std::optional<gp_Pnt> point;
    const auto& s = pick->entity.shape;
    if (!s.IsNull() && s.ShapeType() == TopAbs_VERTEX)
        point = BRep_Tool::Pnt(TopoDS::Vertex(s));
    else if (cursor)
    {
        const auto pixel = nativePosition(*cursor);
        double x, y, z, dx, dy, dz;
        m_graphics.view()->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
        const gp_Lin ray({x, y, z}, gp_Dir(dx, dy, dz));
        if (!s.IsNull() && (s.ShapeType() == TopAbs_FACE || s.ShapeType() == TopAbs_SOLID))
        {
            IntCurvesFace_ShapeIntersector inter;
            inter.Load(s, Precision::Confusion());
            inter.Perform(ray, -Precision::Infinite(), Precision::Infinite());
            const auto cam = m_graphics.view()->Camera();
            double nearest = Precision::Infinite();
            if (inter.IsDone())
                for (int i = 1; i <= inter.NbPnt(); ++i)
                {
                    const auto p = inter.Pnt(i);
                    double depth = gp_Vec(cam->Eye(), p).Dot(gp_Vec(cam->Direction()));
                    if (depth >= 0 && depth < nearest)
                    {
                        point = p;
                        nearest = depth;
                    }
                }
        }
        else
        {
            auto result = measureEntities({pick->entity, {{}, {}, ray}});
            if (result && result->span)
                point = result->span->first;
        }
    }
    if (!point)
    {
        emit measurePickMessage("No exact point at this location.");
        return;
    }
    auto vertex = BRepBuilderAPI_MakeVertex(*point).Vertex();
    pick->entity = {vertex};
    pick->highlight = vertex;
    pick->location = true;
    pick->label = "Point on " + pick->label;
    acceptMeasurePick(*pick);
}
void CadViewport::measureTreeItem(const QString& id, bool solid)
{
    SelectionRef ref;
    ref.featureId = id.toStdString();
    if (id == "origin:")
        ref.kind = Kind::ReferencePoint;
    else if (id.startsWith("origin:"))
    {
        ref.kind = Kind::OriginPlane;
        ref.featureId.clear();
        ref.index = id == "origin:XY" ? 0 : id == "origin:XZ" ? 1 : 2;
    }
    else if (solid)
    {
        prepareBodyPicking();
        if (m_selectionBodies.Extent() != 1)
        {
            emit measurePickMessage("Pick the required solid body in the viewport.");
            return;
        }
        ref = {Kind::Body, m_bodyFeatureId, {}, PointRole::First, 1, m_shapeGeneration};
    }
    else
    {
        auto d = std::find_if(m_datums.begin(), m_datums.end(),
                              [&](const auto& v) { return v.id == ref.featureId; });
        if (d == m_datums.end())
            return;
        ref.kind = d->geometry.kind == DatumKind::Plane  ? Kind::ReferencePlane
                   : d->geometry.kind == DatumKind::Axis ? Kind::ReferenceAxis
                                                         : Kind::ReferencePoint;
    }
    if (auto p = resolveMeasurePick(ref))
        acceptMeasurePick(*p);
}
} // namespace thecad::ui
