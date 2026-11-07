#include "ui/CadViewport.h"
#include "presentation/SelectionAppearance.h"
#include <Graphic3d_ZLayerId.hxx>
#include <QLineF>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace thecad::ui
{
using namespace modeling;

void CadViewport::setSketchBoundaryReferences(std::vector<SketchProjection> references)
{
    m_sketchBoundaryReferences = std::move(references);
    m_boundarySnap.reset();
    m_firstBoundarySnap.reset();
}

std::optional<CadViewport::BoundarySnap> CadViewport::boundarySnapAt(
    const QPointF& position, Point2 local) const
{
    if (!m_selectionPolicy.allows(SelectionKind::Edge))
        return {};
    std::optional<BoundarySnap> pointHit, curveHit;
    double pointDistance = m_pickingTolerance, curveDistance = m_pickingTolerance;
    for (std::size_t i = 0; i < m_sketchBoundaryReferences.size(); ++i)
    {
        const auto& e = m_sketchBoundaryReferences[i].geometry;
        const auto testPoint = [&](PointRole role, Point2 point) {
            const double distance = QLineF(position, screenPoint(point)).length();
            if (distance < pointDistance)
            {
                pointDistance = distance;
                pointHit = BoundarySnap{i, role, point};
            }
        };
        Point2 onCurve;
        if (e.kind == EntityKind::Line)
        {
            testPoint(PointRole::First, e.first);
            testPoint(PointRole::Second, e.second);
            testPoint(PointRole::Midpoint, {(e.first.x + e.second.x) / 2,
                                          (e.first.y + e.second.y) / 2});
            const double dx = e.second.x - e.first.x, dy = e.second.y - e.first.y;
            const double t = std::clamp(((local.x - e.first.x) * dx + (local.y - e.first.y) * dy) /
                                            (dx * dx + dy * dy), 0.0, 1.0);
            onCurve = {e.first.x + t * dx, e.first.y + t * dy};
        }
        else
        {
            testPoint(PointRole::Center, e.first);
            const double angle = std::atan2(local.y - e.first.y, local.x - e.first.x);
            onCurve = {e.first.x + e.radius * std::cos(angle), e.first.y + e.radius * std::sin(angle)};
        }
        const double distance = QLineF(position, screenPoint(onCurve)).length();
        if (distance < curveDistance)
        {
            curveDistance = distance;
            curveHit = BoundarySnap{i, {}, onCurve};
        }
    }
    // On a tiny circle the outline is easier to pick when it is closer than
    // the center. Other corners and midpoints have first priority.
    if (pointHit && (pointHit->role != PointRole::Center || pointDistance <= curveDistance))
        return pointHit;
    return curveHit;
}

void CadViewport::attachBoundarySnap(Sketch& sketch, SketchPointRef point, const BoundarySnap& snap)
{
    const auto& option = m_sketchBoundaryReferences.at(snap.index);
    const auto id = useSketchProjection(sketch, option, true);
    const auto circle = option.geometry.kind == EntityKind::Circle;
    const auto kind = snap.role == PointRole::Midpoint ? RelationKind::Midpoint :
        snap.role ? RelationKind::Coincident : circle ? RelationKind::PointOnCircle : RelationKind::PointOnLine;
    const auto role = snap.role && snap.role != PointRole::Midpoint ? *snap.role :
        circle ? PointRole::Center : PointRole::First;
    sketch.relations.push_back({QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
                                kind, point, {id, role}});
}

void CadViewport::setSketchModifierPreview(const Sketch* candidate)
{
    if (!isReady())
        return;
    if (!m_sketchModifierPreview.IsNull())
    {
        m_graphics.context()->Remove(m_sketchModifierPreview, Standard_False);
        m_sketchModifierPreview.Nullify();
    }
    if (candidate && m_sketch)
    {
        Sketch preview = *candidate;
        preview.relations.clear();
        preview.dimensions.clear();
        preview.polygons.clear();
        preview.projections.clear();
        preview.replications.clear();
        preview.entities.erase(std::remove_if(preview.entities.begin(), preview.entities.end(),
            [&](const auto& item) {
                return std::any_of(m_sketch->entities.begin(), m_sketch->entities.end(),
                    [&](const auto& old) { return old.id == item.id && old.construction == item.construction; });
            }), preview.entities.end());
        if (!preview.entities.empty())
        {
            m_sketchModifierPreview = new AIS_Shape(sketchShape(preview));
            m_sketchModifierPreview->SetColor(presentation::hoverOrange());
            m_sketchModifierPreview->SetWidth(3 * devicePixelRatioF());
            m_sketchModifierPreview->SetZLayer(Graphic3d_ZLayerId_Topmost);
            m_graphics.context()->Display(m_sketchModifierPreview, AIS_WireFrame, -1, Standard_False);
        }
    }
    update();
}
}
