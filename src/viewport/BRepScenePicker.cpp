#include "viewport/BRepScenePicker.h"

#include <BRep_Tool.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <Precision.hxx>
#include <Standard_Real.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <limits>

namespace thecad::viewport
{
struct BRepScenePicker::Cache
{
    TopoDS_Shape shape;
    TopTools_IndexedMapOfShape faces;
    std::unique_ptr<IntCurvesFace_ShapeIntersector> intersector;
};

BRepScenePicker::BRepScenePicker() = default;
BRepScenePicker::~BRepScenePicker() = default;

void BRepScenePicker::setScene(std::vector<PickableBRep> scene)
{
    for (auto it = m_cache.begin(); it != m_cache.end();)
    {
        const auto item = std::find_if(scene.begin(), scene.end(), [&](const auto& candidate) {
            return candidate.id == it->first && !candidate.shape.IsNull() &&
                   candidate.shape.IsSame(it->second->shape);
        });
        if (item == scene.end())
            it = m_cache.erase(it);
        else
            ++it;
    }

    for (const auto& item : scene)
    {
        if (item.id.empty() || item.shape.IsNull())
            continue;
        const auto found = m_cache.find(item.id);
        if (found != m_cache.end() && found->second->shape.IsSame(item.shape))
            continue;

        auto cache = std::make_unique<Cache>();
        cache->shape = item.shape;
        TopExp::MapShapes(item.shape, TopAbs_FACE, cache->faces);
        if (cache->faces.Extent() > 0)
        {
            cache->intersector = std::make_unique<IntCurvesFace_ShapeIntersector>();
            cache->intersector->Load(item.shape, Precision::Confusion());
        }
        m_cache[item.id] = std::move(cache);
    }
    m_scene = std::move(scene);
}

void BRepScenePicker::clear()
{
    m_scene.clear();
    m_cache.clear();
}

std::optional<BRepSurfaceHit> BRepScenePicker::surfaceAt(const Handle(V3d_View)& view,
                                                        const QPoint& physicalPixel)
{
    if (view.IsNull())
        return {};
    double x, y, z, dx, dy, dz;
    view->ConvertWithProj(physicalPixel.x(), physicalPixel.y(), x, y, z, dx, dy, dz);
    return surfaceAlongRay(gp_Lin(gp_Pnt(x, y, z), gp_Dir(dx, dy, dz)),
                           view->Camera()->Eye(), view->Camera()->Direction());
}

std::optional<BRepSurfaceHit> BRepScenePicker::surfaceAlongRay(const gp_Lin& worldRay,
                                                              const gp_Pnt& cameraEye,
                                                              const gp_Dir& cameraDirection)
{
    std::optional<BRepSurfaceHit> nearest;
    const gp_Vec viewDirection(cameraDirection);
    for (const auto& item : m_scene)
    {
        if (!item.visible || item.shape.IsNull())
            continue;
        const auto cacheIt = m_cache.find(item.id);
        if (cacheIt == m_cache.end() || !cacheIt->second->intersector)
            continue;

        auto& cache = *cacheIt->second;
        const auto inverse = item.placement.Inverted();
        const gp_Lin localRay(worldRay.Location().Transformed(inverse),
                              worldRay.Direction().Transformed(inverse));
        cache.intersector->Perform(localRay, -1e15, 1e15);
        for (int index = 1; index <= cache.intersector->NbPnt(); ++index)
        {
            const auto worldPoint = cache.intersector->Pnt(index).Transformed(item.placement);
            const double depth = gp_Vec(cameraEye, worldPoint).Dot(viewDirection);
            if (depth < 0.0 || (nearest && depth >= nearest->depth))
                continue;
            const auto face = cache.intersector->Face(index);
            const int faceIndex = cache.faces.FindIndex(face);
            if (faceIndex > 0)
                nearest = BRepSurfaceHit{item.id, faceIndex, face, worldPoint, depth};
        }
    }
    return nearest;
}

bool BRepScenePicker::occludes(const Handle(V3d_View)& view, const gp_Pnt& worldPoint,
                               double depthTolerance)
{
    if (view.IsNull())
        return false;
    int x, y;
    view->Convert(worldPoint.X(), worldPoint.Y(), worldPoint.Z(), x, y);
    const auto surface = surfaceAt(view, QPoint(x, y));
    if (!surface)
        return false;
    const auto camera = view->Camera();
    const double depth = gp_Vec(camera->Eye(), worldPoint).Dot(gp_Vec(camera->Direction()));
    return depth > surface->depth + std::max(0.0, depthTolerance);
}

std::optional<BRepEdgeScreenHit> BRepScenePicker::closestDisplayedEdgePoint(
    const TopoDS_Edge& edge, const gp_Trsf& placement, const Handle(V3d_View)& view,
    const QPointF& logicalCursor, double devicePixelRatio)
{
    if (view.IsNull() || devicePixelRatio <= 0.0)
        return {};
    Handle(Poly_PolygonOnTriangulation) polygon;
    Handle(Poly_Triangulation) triangulation;
    TopLoc_Location location;
    BRep_Tool::PolygonOnTriangulation(edge, polygon, triangulation, location);
    if (polygon.IsNull() || triangulation.IsNull() || polygon->NbNodes() < 2)
        return {};

    const auto project = [&](const gp_Pnt& point) {
        int x, y;
        view->Convert(point.X(), point.Y(), point.Z(), x, y);
        return QPointF(x / devicePixelRatio, y / devicePixelRatio);
    };
    const auto toWorld = [&](const gp_Pnt& point) {
        return point.Transformed(location.Transformation()).Transformed(placement);
    };

    const auto& nodes = polygon->Nodes();
    auto a = toWorld(triangulation->Node(nodes.First()));
    auto screenA = project(a);
    BRepEdgeScreenHit best{{}, std::numeric_limits<double>::infinity()};
    for (int index = nodes.Lower() + 1; index <= nodes.Upper(); ++index)
    {
        const auto b = toWorld(triangulation->Node(nodes(index)));
        const auto screenB = project(b);
        const auto segment = screenB - screenA;
        const double lengthSquared = QPointF::dotProduct(segment, segment);
        const double t = lengthSquared > 0.0
                             ? std::clamp(QPointF::dotProduct(logicalCursor - screenA, segment) /
                                              lengthSquared,
                                          0.0, 1.0)
                             : 0.0;
        const double distance = QLineF(logicalCursor, screenA + segment * t).length();
        if (distance < best.distance)
            best = {a.Translated(gp_Vec(a, b) * t), distance};
        a = b;
        screenA = screenB;
    }
    return std::isfinite(best.distance) ? std::optional<BRepEdgeScreenHit>(best) : std::nullopt;
}
} // namespace thecad::viewport
