#pragma once

#include <IntCurvesFace_ShapeIntersector.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <V3d_View.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <QPoint>
#include <QPointF>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace thecad::viewport
{
struct PickableBRep
{
    std::string id;
    TopoDS_Shape shape;
    gp_Trsf placement;
    bool visible = true;
};

struct BRepSurfaceHit
{
    std::string id;
    int faceIndex = 0;
    TopoDS_Face face;
    gp_Pnt point;
    double depth = 0.0;
};

struct BRepEdgeScreenHit
{
    gp_Pnt point;
    double distance = 0.0;
};

// Shared exact BRep visibility queries for single-body and component scenes.
// Shapes are cached by stable scene-item ID; placements remain instance data.
class BRepScenePicker final
{
public:
    BRepScenePicker();
    ~BRepScenePicker();
    BRepScenePicker(const BRepScenePicker&) = delete;
    BRepScenePicker& operator=(const BRepScenePicker&) = delete;

    void setScene(std::vector<PickableBRep> scene);
    void clear();
    std::optional<BRepSurfaceHit> surfaceAt(const Handle(V3d_View)& view,
                                            const QPoint& physicalPixel);
    std::optional<BRepSurfaceHit> surfaceAlongRay(const gp_Lin& worldRay, const gp_Pnt& cameraEye,
                                                  const gp_Dir& cameraDirection);
    bool occludes(const Handle(V3d_View)& view, const gp_Pnt& worldPoint,
                  double depthTolerance);
    static std::optional<BRepEdgeScreenHit> closestDisplayedEdgePoint(
        const TopoDS_Edge& edge, const gp_Trsf& placement, const Handle(V3d_View)& view,
        const QPointF& logicalCursor, double devicePixelRatio);

private:
    struct Cache;
    std::map<std::string, std::unique_ptr<Cache>> m_cache;
    std::vector<PickableBRep> m_scene;
};
} // namespace thecad::viewport
